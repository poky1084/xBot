#include "ui/ManualBetPanel.h"

#include "ui/GameParamsWidget.h"
#include "ui/TargetBar.h"

#include <QSignalBlocker>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace
{
QDoubleSpinBox *makeAmountSpin()
{
    auto *spin = new QDoubleSpinBox;
    spin->setRange(0.0, 1e9); // 0 is a valid bet
    spin->setDecimals(8);
    spin->setSingleStep(0.5);
    spin->setKeyboardTracking(false);
    return spin;
}

// "2.45x", "1234x" (as the C# bot's card strip).
QString hiloMultText(double m)
{
    return m >= 1000.0 ? QStringLiteral("%1x").arg(m, 0, 'f', 0)
                       : QStringLiteral("%1x").arg(QString::number(m, 'f', 2)
                                                       .remove(QRegularExpression(QStringLiteral("\\.?0+$"))));
}

QString suitSymbol(const QString &suit)
{
    const char16_t ch = suit.isEmpty() ? u'\0' : suit.at(0).toUpper().unicode();
    switch (ch) {
    case 'C': return QString(QChar(0x2663)); // ♣
    case 'H': return QString(QChar(0x2665)); // ♥
    case 'D': return QString(QChar(0x2666)); // ♦
    case 'S': return QString(QChar(0x2660)); // ♠
    default:  return suit;
    }
}

// Draws one playing card (white face, rank top-left, big suit symbol) or its back.
void drawPlayingCard(QPainter &p, const QFont &base, const QRectF &card, const QString &rank,
                     const QString &suit, const QColor &border, qreal borderWidth, bool faceDown,
                     double scale = 1.0)
{
    QPainterPath path;
    path.addRoundedRect(card, 6, 6);
    if (faceDown) {
        p.fillPath(path, QColor(0x1e, 0x3a, 0x8a));
        QPainterPath inner;
        const qreal in = 5 * scale;
        inner.addRoundedRect(card.adjusted(in, in, -in, -in), 4, 4);
        p.setPen(QPen(QColor(0x93, 0xc5, 0xfd), 1.5));
        p.drawPath(inner);
        p.setPen(QPen(border, borderWidth));
        p.drawPath(path);
        return;
    }
    p.fillPath(path, Qt::white);
    p.setPen(QPen(border, borderWidth));
    p.drawPath(path);
    const bool red = suit.compare(QLatin1String("H"), Qt::CaseInsensitive) == 0
                  || suit.compare(QLatin1String("D"), Qt::CaseInsensitive) == 0;
    p.setPen(red ? QColor(0xe5, 0x39, 0x35) : QColor(0x1e, 0x1e, 0x1e));
    QFont rankFont = base;
    rankFont.setPixelSize(qRound(17 * scale));
    rankFont.setBold(true);
    p.setFont(rankFont);
    p.drawText(card.adjusted(6 * scale, 3 * scale, -4 * scale, 0), Qt::AlignLeft | Qt::AlignTop, rank);
    QFont suitFont = base;
    suitFont.setPixelSize(qRound(30 * scale));
    p.setFont(suitFont);
    p.drawText(card.adjusted(0, 16 * scale, 0, 0), Qt::AlignCenter, suitSymbol(suit));
}
} // namespace

// The blackjack table, in one row: the dealer's cards, then the player's hand(s) next to
// them (side by side after a split), each with its title ("Dealer 17", "Hand 1  19").
class BlackjackTable : public QWidget
{
public:
    struct Hand
    {
        QString title;
        QVector<QPair<QString, QString>> cards; // rank, suit
        QString actions;         // "deal, hit, stand" (the hand's past decisions, as index.js)
        bool hiddenCard = false; // dealer: a face-down card after the visible one
        bool current = false;    // the hand the next move is for
        int outcome = 0;         // 1 won, -1 lost, 2 push, 0 open / unknown
    };

    explicit BlackjackTable(QWidget *parent = nullptr) : QWidget(parent) { updateSize(); }

    void setRound(const Hand &dealer, const QVector<Hand> &hands)
    {
        m_dealer = dealer;
        m_hands = hands;
        m_empty = false;
        updateSize();
        update();
    }
    void clearHighlights()
    {
        for (Hand &h : m_hands)
            h.current = false;
        update();
    }

    QSize sizeHint() const override
    {
        const int h = kMargin * 2 + kTitleH + cardH() + kActionsH;
        if (m_empty)
            return {220, h};
        int w = groupWidth(m_dealer);
        for (int i = 0; i < m_hands.size(); ++i) {
            const int gw = groupWidth(m_hands.at(i));
            // The last hand's "Actions: ..." line may run past its cards.
            w += (i == 0 ? kDealerGap : kGroupGap) + (i == m_hands.size() - 1 ? std::max(gw, 170) : gw);
        }
        return {kMargin * 2 + std::max(220, w), h};
    }
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        if (m_empty) {
            QFont f = font();
            f.setPixelSize(12);
            p.setFont(f);
            p.setPen(QColor(0x73, 0x73, 0x73));
            p.drawText(rect(), Qt::AlignCenter, QObject::tr("Press Bet Blackjack to deal."));
            return;
        }
        // Each group's "Actions:" line may use the gap after it; the last one the rest.
        drawGroup(p, m_dealer, kMargin, kMargin, groupWidth(m_dealer) + kDealerGap - 6);
        int x = kMargin + groupWidth(m_dealer);
        for (int i = 0; i < m_hands.size(); ++i) {
            x += i == 0 ? kDealerGap : kGroupGap;
            const bool last = i == m_hands.size() - 1;
            const int gw = groupWidth(m_hands.at(i));
            drawGroup(p, m_hands.at(i), x, kMargin, last ? width() - x - kMargin : gw + kGroupGap - 6);
            x += gw;
        }
    }

private:
    int groupWidth(const Hand &h) const
    {
        const int n = int(h.cards.size()) + (h.hiddenCard ? 1 : 0);
        return n <= 0 ? cardW() : n * cardW() + (n - 1) * gap();
    }

    void drawGroup(QPainter &p, const Hand &h, int x, int y, int textWidth) const
    {
        const QColor text = palette().color(QPalette::WindowText);
        QColor accent;
        switch (h.outcome) {
        case 1:  accent = QColor(0x2e, 0x9e, 0x46); break;
        case -1: accent = QColor(0xc9, 0x37, 0x2a); break;
        case 2:  accent = QColor(0x9e, 0x9e, 0x9e); break;
        default: accent = h.current ? QColor(0x1e, 0x88, 0xe5) : QColor(); break;
        }
        QFont titleFont = font();
        titleFont.setPixelSize(12);
        titleFont.setBold(true);
        p.setFont(titleFont);
        p.setPen(accent.isValid() ? accent : text);
        p.drawText(QRectF(x, y, std::max(groupWidth(h), 110), kTitleH - 2),
                   Qt::AlignLeft | Qt::AlignVCenter, h.title);
        const QColor border = accent.isValid() ? accent : QColor(0xc8, 0xc8, 0xc8);
        const qreal width = accent.isValid() ? 2.5 : 1.0;
        int cx = x;
        for (const auto &c : h.cards) {
            drawPlayingCard(p, font(), QRectF(cx + 0.5, y + kTitleH + 0.5, cardW() - 1, cardH() - 1),
                            c.first, c.second, border, width, false, scale());
            cx += cardW() + gap();
        }
        if (h.hiddenCard)
            drawPlayingCard(p, font(), QRectF(cx + 0.5, y + kTitleH + 0.5, cardW() - 1, cardH() - 1),
                            {}, {}, QColor(0xc8, 0xc8, 0xc8), 1.0, true, scale());
        if (!h.actions.isEmpty()) {
            QFont actFont = font();
            actFont.setPixelSize(11);
            p.setFont(actFont);
            p.setPen(QColor(0x9a, 0x9a, 0x9a));
            p.drawText(QRectF(x, y + kTitleH + cardH() + 3, std::max(textWidth, cardW()), kActionsH - 3),
                       Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                       QObject::tr("Actions: %1").arg(h.actions));
        }
    }

    void updateSize()
    {
        setMinimumSize(sizeHint());
        updateGeometry();
    }

    // Full-size cards; after a split (two hands) they are drawn at kSplitScale.
    static constexpr int kCardW = 54;
    static constexpr int kCardH = 76;
    static constexpr int kGap = 8;
    static constexpr double kSplitScale = 0.72;
    double scale() const { return m_hands.size() > 1 ? kSplitScale : 1.0; }
    int cardW() const { return qRound(kCardW * scale()); }
    int cardH() const { return qRound(kCardH * scale()); }
    int gap() const { return qRound(kGap * scale()); }
    static constexpr int kGroupGap = 24;  // between split hands
    static constexpr int kDealerGap = 40; // between the dealer and the player
    static constexpr int kTitleH = 20;
    static constexpr int kActionsH = 32; // up to two lines of "Actions: ..."
    static constexpr int kMargin = 4;
    Hand m_dealer;
    QVector<Hand> m_hands;
    bool m_empty = true;
};

// The hilo card showcase: the start card and every card drawn since, each with the guess
// and the running multiplier under it (the C# bot's lvHiloCards strip).
class HiloCardStrip : public QWidget
{
public:
    struct Card
    {
        QString rank;
        QString suit;
        QString caption;
        bool bust = false;
        bool skipped = false; // drawn by a skip: grey card, as on Stake
        QString guess;        // the guess that drew this card: badge before it, as on Stake
    };

    explicit HiloCardStrip(QWidget *parent = nullptr) : QWidget(parent) { updateSize(); }

    // outcome: 0 = round open, 1 = cashed out, -1 = lost.
    void setCards(const QVector<Card> &cards, int outcome)
    {
        m_cards = cards;
        m_outcome = outcome;
        updateSize();
        update();
    }
    void setOutcome(int outcome)
    {
        m_outcome = outcome;
        update();
    }

    QSize sizeHint() const override
    {
        const int n = int(m_cards.size());
        const int w = n == 0 ? 220 : kMargin * 2 + n * kCardW + (n - 1) * kGap;
        return {w, kMargin * 2 + kCardH + kCaptionH};
    }
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QColor text = palette().color(QPalette::WindowText);
        if (m_cards.isEmpty()) {
            QFont f = font();
            f.setPixelSize(12);
            p.setFont(f);
            p.setPen(QColor(0x73, 0x73, 0x73));
            p.drawText(rect(), Qt::AlignCenter, QObject::tr("Press New Game to deal a start card."));
            return;
        }
        for (int i = 0; i < m_cards.size(); ++i) {
            const Card &c = m_cards.at(i);
            const QRectF card(kMargin + i * (kCardW + kGap) + 0.5, kMargin + 0.5, kCardW - 1, kCardH - 1);
            const bool last = i == m_cards.size() - 1;
            QColor border(0xc8, 0xc8, 0xc8);
            qreal width = 1.0;
            if (last) {
                border = m_outcome < 0 ? QColor(0xc9, 0x37, 0x2a)
                       : m_outcome > 0 ? QColor(0x2e, 0x9e, 0x46)
                                       : QColor(0x1e, 0x88, 0xe5);
                width = 2.5;
            }
            QPainterPath path;
            path.addRoundedRect(card, 6, 6);
            p.fillPath(path, c.skipped ? QColor(0x7d, 0x87, 0x8c) : QColor(Qt::white));
            p.setPen(QPen(border, width));
            p.drawPath(path);

            const bool red = c.suit.compare(QLatin1String("H"), Qt::CaseInsensitive) == 0
                          || c.suit.compare(QLatin1String("D"), Qt::CaseInsensitive) == 0;
            p.setPen(red ? QColor(0xe5, 0x39, 0x35) : QColor(0x1e, 0x1e, 0x1e));
            QFont rankFont = font();
            rankFont.setPixelSize(17);
            rankFont.setBold(true);
            p.setFont(rankFont);
            p.drawText(card.adjusted(6, 3, -4, 0), Qt::AlignLeft | Qt::AlignTop, c.rank);
            QFont suitFont = font();
            suitFont.setPixelSize(30);
            p.setFont(suitFont);
            p.drawText(card.adjusted(0, 16, 0, 0), Qt::AlignCenter, suitSymbol(c.suit));

            QFont capFont = font();
            capFont.setPixelSize(12);
            capFont.setBold(true);
            p.setFont(capFont);
            p.setPen(c.bust ? QColor(0xc9, 0x37, 0x2a) : text);
            p.drawText(QRectF(card.left() - kGap / 2.0, card.bottom() + 2, kCardW + kGap, kCaptionH - 2),
                       Qt::AlignHCenter | Qt::AlignTop, c.caption);
        }
        // Guess / skip badges on top, straddling the gap before each drawn card (as on Stake).
        for (int i = 1; i < m_cards.size(); ++i)
            drawBadge(p, QPointF(kMargin + i * (kCardW + kGap) - kGap / 2.0, kMargin + kCardH * 0.45),
                      m_cards.at(i).guess);
    }

private:
    // White rounded square with the guess: green ˄ / ˅ (with a bar for "or same"), green =
    // for equal, orange » for skip.
    static void drawBadge(QPainter &p, const QPointF &centre, const QString &guess)
    {
        if (guess.isEmpty())
            return;
        const QRectF box(centre.x() - kBadge / 2.0, centre.y() - kBadge / 2.0, kBadge, kBadge);
        QPainterPath path;
        path.addRoundedRect(box, 4, 4);
        p.fillPath(path, Qt::white);
        p.setPen(QPen(QColor(0xb1, 0xba, 0xd3), 1.5));
        p.drawPath(path);

        const qreal cx = box.center().x();
        const qreal cy = box.center().y();
        const qreal w = kBadge * 0.22; // half width of a chevron
        const qreal h = kBadge * 0.13; // half height
        QPen pen(QColor(0x1f, 0xc8, 0x1f), 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setBrush(Qt::NoBrush);
        if (guess.startsWith(QLatin1String("higher")) || guess.startsWith(QLatin1String("lower"))) {
            const bool up = guess.startsWith(QLatin1String("higher"));
            const bool orEqual = guess.endsWith(QLatin1String("Equal"));
            const qreal y0 = orEqual ? cy - 2 : cy;
            p.setPen(pen);
            QPainterPath chevron;
            chevron.moveTo(cx - w, up ? y0 + h : y0 - h);
            chevron.lineTo(cx, up ? y0 - h : y0 + h);
            chevron.lineTo(cx + w, up ? y0 + h : y0 - h);
            p.drawPath(chevron);
            if (orEqual) // the "or same" bar
                p.drawLine(QPointF(cx - w, y0 + h + 4), QPointF(cx + w, y0 + h + 4));
        } else if (guess == QLatin1String("equal")) {
            p.setPen(pen);
            p.drawLine(QPointF(cx - w, cy - 2.5), QPointF(cx + w, cy - 2.5));
            p.drawLine(QPointF(cx - w, cy + 2.5), QPointF(cx + w, cy + 2.5));
        } else if (guess == QLatin1String("skip")) {
            pen.setColor(QColor(0xff, 0xa5, 0x00));
            p.setPen(pen);
            for (const qreal dx : {-3.0, 3.0}) {
                QPainterPath chevron;
                chevron.moveTo(cx + dx - h, cy - w);
                chevron.lineTo(cx + dx + h, cy);
                chevron.lineTo(cx + dx - h, cy + w);
                p.drawPath(chevron);
            }
        }
    }

    void updateSize()
    {
        setMinimumSize(sizeHint());
        updateGeometry();
    }

    static constexpr int kCardW = 54;
    static constexpr int kCardH = 76;
    static constexpr int kGap = 8;
    static constexpr int kMargin = 4;
    static constexpr int kCaptionH = 20;
    static constexpr int kBadge = 22;
    QVector<Card> m_cards;
    int m_outcome = 0;
};

ManualBetPanel::ManualBetPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(24);

    // ---- Left column: bet inputs --------------------------------------
    auto *left = new QVBoxLayout;
    left->setSpacing(6);

    m_amountSpin = makeAmountSpin();
    m_amountSpin->setValue(0.0);
    auto *halfBtn = new QPushButton(QStringLiteral("½"));
    auto *doubleBtn = new QPushButton(QStringLiteral("2×"));
    // The global QPushButton style has 12px horizontal padding, which leaves
    // almost no room for the text in a narrow button. "amountBtn" removes it.
    halfBtn->setObjectName(QStringLiteral("amountBtn"));
    doubleBtn->setObjectName(QStringLiteral("amountBtn"));
    halfBtn->setFixedWidth(40);
    doubleBtn->setFixedWidth(40);
    connect(halfBtn, &QPushButton::clicked, this, &ManualBetPanel::halfAmount);
    connect(doubleBtn, &QPushButton::clicked, this, &ManualBetPanel::doubleAmount);

    auto *amountRow = new QHBoxLayout;
    amountRow->setSpacing(4);
    amountRow->addWidget(m_amountSpin, 1);
    amountRow->addWidget(halfBtn);
    amountRow->addWidget(doubleBtn);

    auto *form = new QFormLayout;
    m_form = form;
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(6);
    form->addRow(tr("Bet amount:"), amountRow);

    m_multiplierSpin = new QDoubleSpinBox;
    m_multiplierSpin->setRange(0.99, 9900.0); // 99.99% .. 0.01% win chance
    m_multiplierSpin->setDecimals(4);
    m_multiplierSpin->setSingleStep(0.01);
    m_multiplierSpin->setKeyboardTracking(false);
    form->addRow(tr("Multiplier:"), m_multiplierSpin);

    m_chanceSpin = new QDoubleSpinBox;
    m_chanceSpin->setRange(BotConstants::kMinChance, BotConstants::kMaxChance);
    m_chanceSpin->setDecimals(2);
    m_chanceSpin->setSingleStep(0.1);
    m_chanceSpin->setSuffix(QStringLiteral(" %"));
    m_chanceSpin->setKeyboardTracking(false);
    form->addRow(tr("Win chance %:"), m_chanceSpin);

    m_profitOnWin = new QLabel(QStringLiteral("0.00000000"));
    m_profitOnWin->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(tr("Profit on win:"), m_profitOnWin);

    m_rollBtn = new QPushButton(tr("Roll Dice"));
    m_rollBtn->setObjectName(QStringLiteral("rollDiceBtn"));
    m_rollBtn->setMinimumHeight(32);
    QFont rollFont = m_rollBtn->font();
    rollFont.setBold(true);
    m_rollBtn->setFont(rollFont);
    connect(m_rollBtn, &QPushButton::clicked, this, &ManualBetPanel::rollRequested);

    // Settings of Stake's other games (shown instead of Multiplier / Win chance).
    m_params = new GameParamsWidget;
    // Hilo "Stop on" only matters for auto-bet runs (Auto Bet tab); manual bets play "None".
    // Hilo "Pattern" too: manual hilo is played with the buttons below.
    m_params->setHiddenKeys({QStringLiteral("suitMode"), QStringLiteral("hiloPattern")});
    m_params->setVisible(false);
    connect(m_params, &GameParamsWidget::paramsChanged, this, &ManualBetPanel::gameChanged);

    // Hilo: play a round by hand, like the C# bot (New Game deals the start card).
    m_hiloBox = new QWidget;
    {
        auto *grid = new QGridLayout(m_hiloBox);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(4);
        grid->setVerticalSpacing(4);
        const auto makeBtn = [this](const QString &text, const QString &move) {
            auto *b = new QPushButton(text);
            b->setObjectName(QStringLiteral("amountBtn")); // no wide padding
            b->setMinimumHeight(30);
            connect(b, &QPushButton::clicked, this, [this, move] { emit hiloMoveRequested(move); });
            return b;
        };
        m_hiloHigh = makeBtn(tr("High"), QStringLiteral("high"));
        m_hiloLow = makeBtn(tr("Low"), QStringLiteral("low"));
        m_hiloEqual = makeBtn(tr("Equal"), QStringLiteral("equal"));
        m_hiloSkip = makeBtn(tr("Skip"), QStringLiteral("skip"));
        m_hiloCashout = makeBtn(tr("Cash out"), QStringLiteral("cashout"));
        m_hiloNewGame = new QPushButton(tr("New Game"));
        m_hiloNewGame->setObjectName(QStringLiteral("amountBtn"));
        m_hiloNewGame->setMinimumHeight(30);
        QFont bold = m_hiloNewGame->font();
        bold.setBold(true);
        m_hiloNewGame->setFont(bold);
        m_hiloCashout->setFont(bold);
        connect(m_hiloNewGame, &QPushButton::clicked, this, &ManualBetPanel::rollRequested);
        grid->addWidget(m_hiloHigh, 0, 0);
        grid->addWidget(m_hiloLow, 0, 1);
        grid->addWidget(m_hiloEqual, 0, 2);
        grid->addWidget(m_hiloSkip, 0, 3);
        grid->addWidget(m_hiloNewGame, 1, 0, 1, 2);
        grid->addWidget(m_hiloCashout, 1, 2, 1, 2);
    }
    m_hiloBox->setVisible(false);

    // Blackjack: play the hand by hand (one row, like Stake), insurance row when needed.
    m_bjBox = new QWidget;
    {
        auto *col = new QVBoxLayout(m_bjBox);
        col->setContentsMargins(0, 0, 0, 0);
        col->setSpacing(4);
        const auto makeBtn = [this](const QString &text, const QString &move) {
            auto *b = new QPushButton(text);
            b->setObjectName(QStringLiteral("amountBtn"));
            b->setMinimumHeight(30);
            connect(b, &QPushButton::clicked, this, [this, move] { emit blackjackMoveRequested(move); });
            return b;
        };
        auto *row = new QHBoxLayout;
        row->setSpacing(4);
        m_bjHit = makeBtn(tr("Hit"), QStringLiteral("hit"));
        m_bjStand = makeBtn(tr("Stand"), QStringLiteral("stand"));
        m_bjSplit = makeBtn(tr("Split"), QStringLiteral("split"));
        m_bjDouble = makeBtn(tr("Double"), QStringLiteral("double"));
        for (QPushButton *b : {m_bjHit, m_bjStand, m_bjSplit, m_bjDouble})
            row->addWidget(b, 1);
        col->addLayout(row);
        m_bjInsRow = new QWidget;
        auto *ins = new QHBoxLayout(m_bjInsRow);
        ins->setContentsMargins(0, 0, 0, 0);
        ins->setSpacing(4);
        m_bjInsurance = makeBtn(tr("Insurance"), QStringLiteral("insurance"));
        m_bjNoInsurance = makeBtn(tr("No insurance"), QStringLiteral("noInsurance"));
        ins->addWidget(m_bjInsurance, 1);
        ins->addWidget(m_bjNoInsurance, 1);
        m_bjInsRow->setVisible(false);
        col->addWidget(m_bjInsRow);
    }
    m_bjBox->setVisible(false);

    left->addLayout(form);
    left->addWidget(m_bjBox); // right under the bet amount
    left->addWidget(m_params);
    left->addWidget(m_hiloBox);
    left->addWidget(m_rollBtn);
    left->addStretch();
    root->addLayout(left);

    // ---- Right column: target bar + slider ----------------------------
    auto *right = new QVBoxLayout;
    right->setSpacing(6);

    m_sideBtn = new QPushButton;
    connect(m_sideBtn, &QPushButton::clicked, this, &ManualBetPanel::onSideToggled);

    m_targetLabel = new QLabel;
    auto *targetRow = new QHBoxLayout;
    targetRow->addWidget(m_sideBtn);
    targetRow->addWidget(m_targetLabel);
    targetRow->addStretch();
    right->addLayout(targetRow);

    m_targetBar = new TargetBar;
    right->addWidget(m_targetBar);

    m_targetSlider = new QSlider(Qt::Horizontal);
    m_targetSlider->setRange(1, 9999); // target x100 (0.01 .. 99.99)
    m_targetSlider->setValue(5050);
    connect(m_targetSlider, &QSlider::valueChanged, this, &ManualBetPanel::onTargetSlider);

    m_scaleWidget = new QWidget;
    auto *scaleRow = new QHBoxLayout(m_scaleWidget);
    scaleRow->setContentsMargins(0, 0, 0, 0);
    scaleRow->setSpacing(0);
    for (const char *mark : {"0", "25", "50", "75", "100"}) {
        auto *label = new QLabel(QLatin1String(mark));
        label->setStyleSheet(QStringLiteral("color:#737373;"));
        scaleRow->addWidget(label, 1, mark[0] == '0' ? Qt::AlignLeft
                                                     : (mark[0] == '1' ? Qt::AlignRight : Qt::AlignHCenter));
    }
    right->addWidget(m_targetSlider);
    right->addWidget(m_scaleWidget);

    // Hilo card showcase (scrolls sideways once the round has more cards than fit).
    m_hiloCards = new HiloCardStrip;
    m_hiloScroll = new QScrollArea;
    m_hiloScroll->setWidget(m_hiloCards);
    m_hiloScroll->setWidgetResizable(true);
    m_hiloScroll->setFrameShape(QFrame::NoFrame);
    m_hiloScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_hiloScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_hiloScroll->setFixedHeight(m_hiloCards->sizeHint().height()
                                 + m_hiloScroll->horizontalScrollBar()->sizeHint().height() + 2);
    m_hiloScroll->setVisible(false);
    right->addWidget(m_hiloScroll);
    m_hiloStatus = new QLabel;
    m_hiloStatus->setStyleSheet(QStringLiteral("color:#737373;"));
    m_hiloStatus->setVisible(false);
    right->addWidget(m_hiloStatus);

    // Blackjack table (dealer on top, the hand(s) below).
    m_bjTable = new BlackjackTable;
    m_bjScroll = new QScrollArea;
    m_bjScroll->setWidget(m_bjTable);
    m_bjScroll->setWidgetResizable(true);
    m_bjScroll->setFrameShape(QFrame::NoFrame);
    m_bjScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_bjScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_bjScroll->setFixedHeight(m_bjTable->sizeHint().height()
                               + m_bjScroll->horizontalScrollBar()->sizeHint().height() + 2);
    m_bjScroll->setVisible(false);
    right->addWidget(m_bjScroll);
    m_bjStatus = new QLabel;
    m_bjStatus->setStyleSheet(QStringLiteral("color:#737373;"));
    m_bjStatus->setVisible(false);
    right->addWidget(m_bjStatus);

    m_lastResult = new QLabel(tr("—"));
    QFont big = m_lastResult->font();
    big.setPointSize(16);
    big.setBold(true);
    m_lastResult->setFont(big);
    right->addWidget(m_lastResult);
    right->addStretch();

    root->addLayout(right, 1);

    // ---- Connections ---------------------------------------------------
    connect(m_amountSpin, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        refreshDerived();
        emit gameChanged();
        emit amountChanged(value);
    });
    connect(m_multiplierSpin, &QDoubleSpinBox::editingFinished, this,
            &ManualBetPanel::onMultiplierEdited);
    connect(m_chanceSpin, &QDoubleSpinBox::editingFinished, this,
            &ManualBetPanel::onChanceEdited);

    refreshDerived();
}

double ManualBetPanel::amount() const
{
    return m_amountSpin->value();
}

void ManualBetPanel::setAmount(double amount)
{
    if (!(amount >= 0.0)) // also rejects NaN from a damaged settings file
        amount = 0.0;
    m_amountSpin->setValue(amount);
}

void ManualBetPanel::setTargets(double diceTarget, bool under, double limboTarget)
{
    // Also rejects NaN from a damaged settings value (falls back to the defaults).
    if (!(diceTarget >= BotConstants::kMinChance && diceTarget <= BotConstants::kMaxChance))
        diceTarget = 50.5;
    if (!(limboTarget >= BotConstants::kMinLimboTarget && limboTarget <= BotConstants::kMaxLimboTarget))
        limboTarget = 2.0;
    m_limboTarget = limboTarget;
    m_sideBtn->setProperty("under", under);
    {
        const QSignalBlocker blocker(m_targetSlider);
        m_targetSlider->setValue(qRound(diceTarget * 100));
    }
    refreshDerived();
    emit gameChanged();
}

double ManualBetPanel::target() const
{
    return m_targetSlider->value() / 100.0;
}

BetSide ManualBetPanel::side() const
{
    return m_sideBtn->property("under").toBool() ? BetSide::Under : BetSide::Over;
}

BotConfig ManualBetPanel::config() const
{
    BotConfig cfg;
    cfg.game = m_game;
    cfg.multiplierTarget = m_limboTarget;
    cfg.side = side();
    cfg.target = target();
    cfg.baseBet = amount();
    cfg.startBalance = m_startBalance;
    cfg.gameParams = m_params->params(); // empty for dice / limbo
    return cfg;
}

QJsonObject ManualBetPanel::gameParams() const
{
    return m_params->allParams();
}

void ManualBetPanel::setGameParams(const QJsonObject &all)
{
    m_params->setAllParams(all);
    m_params->setGame(m_game);
}

void ManualBetPanel::setGame(Game game)
{
    if (m_game == game)
        return;
    m_game = game;
    applyGameLayout();
    refreshDerived();
    emit gameChanged();
}

void ManualBetPanel::applyGameLayout()
{
    const bool limbo = m_game == Game::Limbo;
    const bool dice = m_game == Game::Dice;
    const bool other = !isDiceOrLimbo(m_game);
    m_sideBtn->setVisible(dice);
    m_targetBar->setVisible(dice);
    m_targetSlider->setVisible(dice);
    m_scaleWidget->setVisible(dice);
    m_targetLabel->setVisible(!other);
    // Multiplier / Win chance / Profit on win belong to dice and limbo; the other games show
    // their own settings instead.
    for (QWidget *w : {static_cast<QWidget *>(m_multiplierSpin), static_cast<QWidget *>(m_chanceSpin),
                       static_cast<QWidget *>(m_profitOnWin)}) {
        w->setVisible(!other);
        if (QWidget *label = m_form->labelForField(w))
            label->setVisible(!other);
    }
    m_params->setGame(m_game);
    m_params->setVisible(other && m_params->hasFields());
    m_rollBtn->setText(dice ? tr("Roll Dice") : tr("Bet %1").arg(gameDisplayName(m_game)));
    const bool hilo = m_game == Game::Hilo;
    m_rollBtn->setVisible(!hilo); // hilo: New Game in the button box
    m_hiloBox->setVisible(hilo);
    m_hiloScroll->setVisible(hilo);
    m_hiloStatus->setVisible(hilo && !m_hiloStatus->text().isEmpty());
    const bool blackjack = m_game == Game::Blackjack;
    m_bjBox->setVisible(blackjack);
    m_bjScroll->setVisible(blackjack);
    m_bjStatus->setVisible(blackjack && !m_bjStatus->text().isEmpty());
    // Blackjack's "actions handled by bot logic" note is for auto-bets; here the buttons play.
    QStringList hidden = {QStringLiteral("suitMode"), QStringLiteral("hiloPattern")};
    if (blackjack)
        hidden << QStringLiteral("note");
    m_params->setHiddenKeys(hidden);
    m_params->setVisible(other && m_params->hasFields());
    updateHiloButtons();
    updateBlackjackButtons();
    updateResultStyle();

    m_multiplierSpin->blockSignals(true);
    if (limbo)
        m_multiplierSpin->setRange(BotConstants::kMinLimboTarget, BotConstants::kMaxLimboTarget);
    else
        m_multiplierSpin->setRange(0.99, 9900.0);
    m_multiplierSpin->blockSignals(false);

    // Chance can only be typed for dice; for limbo it is derived from the multiplier.
    m_chanceSpin->setReadOnly(limbo);
    m_chanceSpin->setButtonSymbols(limbo ? QAbstractSpinBox::NoButtons
                                         : QAbstractSpinBox::UpDownArrows);
}

// The app-wide stylesheet sets "QWidget { font-size: 12px; }", which beats setFont(),
// so the size of the result label has to be set in its own stylesheet.
void ManualBetPanel::updateResultStyle()
{
    // >>> Font size of the last result: limbo (first number) / dice (second number) <<<
    const int pointSize = m_game == Game::Limbo ? 45 : (m_game == Game::Dice ? 17 : 20);
    QString css = QStringLiteral("font-size: %1pt; font-weight: bold;").arg(pointSize);
    if (!m_resultColor.isEmpty())
        css += QStringLiteral(" color: %1;").arg(m_resultColor);
    m_lastResult->setStyleSheet(css);
}

void ManualBetPanel::setStartBalance(double balance)
{
    m_startBalance = balance;
}

void ManualBetPanel::setBetPrecision(int decimals, double minBet)
{
    m_amountSpin->setDecimals(decimals);
    m_amountSpin->setMinimum(minBet);
}

void ManualBetPanel::setLocked(bool locked)
{
    const QList<QWidget *> lockedWidgets = {m_amountSpin, m_multiplierSpin,
                                            m_chanceSpin, m_targetSlider, m_params};
    for (QWidget *w : lockedWidgets)
        w->setEnabled(!locked);
    m_rollBtn->setEnabled(!locked);
    m_locked = locked;
    updateHiloButtons();
    updateBlackjackButtons();
}

void ManualBetPanel::updateBlackjackButtons()
{
    const bool play = m_bjActive && !m_locked;
    m_bjInsRow->setVisible(m_bjActive && m_bjInsurance_);
    m_bjInsurance->setEnabled(play && m_bjInsurance_);
    m_bjNoInsurance->setEnabled(play && m_bjInsurance_);
    const bool moves = play && !m_bjInsurance_; // insurance is decided first
    m_bjHit->setEnabled(moves);
    m_bjStand->setEnabled(moves);
    m_bjSplit->setEnabled(moves && m_bjCanSplit);
    m_bjDouble->setEnabled(moves && m_bjCanDouble);
    // A hand is open: finish it before dealing the next one.
    m_rollBtn->setEnabled(!m_locked && !(m_game == Game::Blackjack && m_bjActive));
}

namespace
{
bool bjHasAction(const QJsonObject &hand, const char *action)
{
    for (const QJsonValue &a : hand.value(QStringLiteral("actions")).toArray())
        if (a.toString() == QLatin1String(action))
            return true;
    return false;
}

bool bjHandDone(const QJsonObject &hand)
{
    return bjHasAction(hand, "stand") || bjHasAction(hand, "bust") || bjHasAction(hand, "full")
        || bjHasAction(hand, "double") || bjHasAction(hand, "blackjack");
}
} // namespace

void ManualBetPanel::showBlackjackRound(const QJsonObject &bet, bool active, bool playable)
{
    m_bjActive = active && playable;
    if (bet.isEmpty()) {
        m_bjTable->clearHighlights();
        m_bjStatus->clear();
        m_bjStatus->setVisible(false);
        m_bjInsurance_ = m_bjCanSplit = m_bjCanDouble = false;
        updateBlackjackButtons();
        return;
    }
    const QJsonObject state = bet.value(QStringLiteral("state")).toObject();
    const QJsonArray dealerHands = state.value(QStringLiteral("dealer")).toArray();
    const QJsonArray playerHands = state.value(QStringLiteral("player")).toArray();
    const auto cardsOf = [](const QJsonObject &hand) {
        QVector<QPair<QString, QString>> out;
        for (const QJsonValue &c : hand.value(QStringLiteral("cards")).toArray()) {
            const QJsonObject o = c.toObject();
            out.append({o.value(QStringLiteral("rank")).toString(), o.value(QStringLiteral("suit")).toString()});
        }
        return out;
    };
    const auto actionsText = [](const QJsonObject &hand) {
        QStringList list;
        for (const QJsonValue &a : hand.value(QStringLiteral("actions")).toArray())
            list << a.toString();
        return list.join(QStringLiteral(", "));
    };
    const auto valueText = [](const QJsonObject &hand) {
        const QJsonValue v = hand.value(QStringLiteral("value"));
        return v.isDouble() ? QString::number(v.toInt()) : v.toString();
    };

    BlackjackTable::Hand dealer;
    const QJsonObject dealerHand = dealerHands.isEmpty() ? QJsonObject() : dealerHands.first().toObject();
    dealer.cards = cardsOf(dealerHand);
    dealer.actions = actionsText(dealerHand);
    dealer.hiddenCard = active && dealer.cards.size() == 1; // the hole card is still face down
    dealer.title = dealer.cards.isEmpty() ? tr("Dealer") : tr("Dealer  %1").arg(valueText(dealerHand));
    const int dealerValue = dealerHand.value(QStringLiteral("value")).toInt();

    QVector<BlackjackTable::Hand> hands;
    int current = -1;
    for (int i = 0; i < playerHands.size(); ++i) {
        const QJsonObject h = playerHands.at(i).toObject();
        BlackjackTable::Hand hand;
        hand.cards = cardsOf(h);
        hand.actions = actionsText(h);
        const QString name = playerHands.size() > 1 ? tr("Hand %1").arg(i + 1) : tr("Player");
        hand.title = QStringLiteral("%1  %2").arg(name, valueText(h));
        if (active && current < 0 && !bjHandDone(h))
            current = i;
        if (!active) {
            const int v = h.value(QStringLiteral("value")).toInt();
            if (v > 21)
                hand.outcome = -1;
            else if (bjHasAction(h, "blackjack") && !(dealerValue == 21 && dealer.cards.size() == 2))
                hand.outcome = 1;
            else if (dealerValue > 21 || v > dealerValue)
                hand.outcome = 1;
            else if (v == dealerValue)
                hand.outcome = 2;
            else
                hand.outcome = -1;
        }
        hands.append(hand);
    }
    if (current >= 0 && hands.size() > 1)
        hands[current].current = true;
    else if (current >= 0)
        hands[current].current = playable; // one hand: blue frame while it is your move
    m_bjTable->setRound(dealer, hands);

    // What the buttons may do now.
    const QJsonObject first = playerHands.isEmpty() ? QJsonObject() : playerHands.first().toObject();
    const bool dealerAce = !dealer.cards.isEmpty() && dealer.cards.first().first == QLatin1String("A");
    m_bjInsurance_ = active && dealerAce && !bjHasAction(first, "insurance")
                     && !bjHasAction(first, "noInsurance");
    const QJsonObject cur = current >= 0 ? playerHands.at(current).toObject() : QJsonObject();
    const QJsonArray curCards = cur.value(QStringLiteral("cards")).toArray();
    m_bjCanDouble = active && curCards.size() == 2;
    m_bjCanSplit = active && playerHands.size() == 1 && curCards.size() == 2
                   && curCards.at(0).toObject().value(QStringLiteral("rank"))
                          == curCards.at(1).toObject().value(QStringLiteral("rank"));

    // The past decisions show under each hand ("Actions: deal, hit"); no status line.
    m_bjStatus->clear();
    m_bjStatus->setVisible(false);
    updateBlackjackButtons();
}

void ManualBetPanel::updateHiloButtons()
{
    const bool play = m_hiloActive && !m_locked;
    m_hiloHigh->setEnabled(play && m_hiloRank != QLatin1String("K"));
    m_hiloLow->setEnabled(play && m_hiloRank != QLatin1String("A"));
    m_hiloEqual->setEnabled(play);
    m_hiloSkip->setEnabled(play);
    m_hiloCashout->setEnabled(play && m_hiloRounds > 0); // Stake: no cash out before a guess
    // While a round is open it has to be finished first (New Game would only reload it).
    m_hiloNewGame->setEnabled(!m_locked && !m_hiloActive);
    // Tool tips with what the buttons guess on the current card.
    const QString r = m_hiloRank;
    m_hiloHigh->setToolTip(r == QLatin1String("A") ? tr("Higher than the Ace")
                                                   : tr("Higher or the same"));
    m_hiloLow->setToolTip(r == QLatin1String("K") ? tr("Lower than the King")
                                                  : tr("Lower or the same"));
}

void ManualBetPanel::showHiloRound(const QJsonObject &bet, bool active, bool playable)
{
    m_hiloActive = active && playable;
    if (bet.isEmpty()) {
        // Closed without a final state (e.g. an auto-bet took the round over).
        m_hiloCards->setOutcome(0);
        m_hiloStatus->clear();
        m_hiloStatus->setVisible(false);
        updateHiloButtons();
        return;
    }
    const QJsonObject state = bet.value(QStringLiteral("state")).toObject();
    const QJsonObject start = state.value(QStringLiteral("startCard")).toObject();
    const QJsonArray rounds = state.value(QStringLiteral("rounds")).toArray();

    QVector<HiloCardStrip::Card> cards;
    HiloCardStrip::Card first;
    first.rank = start.value(QStringLiteral("rank")).toString();
    first.suit = start.value(QStringLiteral("suit")).toString();
    first.caption = tr("Start");
    cards.append(first);
    double current = 0.0;
    for (const QJsonValue &v : rounds) {
        const QJsonObject round = v.toObject();
        const QJsonObject card = round.value(QStringLiteral("card")).toObject();
        HiloCardStrip::Card c;
        c.rank = card.value(QStringLiteral("rank")).toString();
        c.suit = card.value(QStringLiteral("suit")).toString();
        const QString guess = round.value(QStringLiteral("guess")).toString();
        current = round.value(QStringLiteral("payoutMultiplier")).toDouble();
        c.bust = current <= 0.0 && guess != QLatin1String("skip");
        // As on Stake: the card a skip drew is shown grey.
        c.skipped = guess == QLatin1String("skip");
        c.guess = guess;
        c.caption = hiloMultText(current); // the guess is shown by the badge
        cards.append(c);
    }
    m_hiloRounds = int(rounds.size());
    m_hiloRank = cards.last().rank.toUpper();

    const double finalMult = bet.value(QStringLiteral("payoutMultiplier")).toDouble(current);
    const int outcome = active ? 0 : (finalMult > 0.0 ? 1 : -1);
    m_hiloCards->setCards(cards, outcome);

    if (active && !playable)
        m_hiloStatus->setText(rounds.isEmpty() ? tr("Auto-bet round started.")
                                               : tr("Auto-bet round — %1.").arg(hiloMultText(current)));
    else if (active)
        m_hiloStatus->setText(rounds.isEmpty() ? tr("Round open — pick High, Low, Equal or Skip.")
                                               : tr("Round open — %1, play on or cash out.")
                                                     .arg(hiloMultText(current)));
    else
        m_hiloStatus->setText(outcome > 0 ? tr("Cashed out at %1.").arg(hiloMultText(finalMult))
                                          : tr("Lost."));
    m_hiloStatus->setVisible(m_game == Game::Hilo);
    if (active && m_game == Game::Hilo) {
        // The round's multiplier so far, where the final one is shown when it ends
        // (neutral color until then; 1x before the first guess).
        m_lastResult->setText(QStringLiteral("%1x").arg(rounds.isEmpty() ? 1.0 : current, 0, 'f', 4));
        m_resultColor.clear();
        updateResultStyle();
    }
    updateHiloButtons();

    // Keep the newest card in view.
    QTimer::singleShot(0, this, [this] {
        m_hiloScroll->horizontalScrollBar()->setValue(m_hiloScroll->horizontalScrollBar()->maximum());
    });
}

void ManualBetPanel::onBetResult(const BetRecord &record)
{
    if (record.game == Game::Dice)
        m_targetBar->setLastRoll(record.roll);
    if (!isDiceOrLimbo(record.game)) {
        // "3 hits  2.0000x", "fourOfAKind  22.0000x", "1.5000x"
        const QString mult = QStringLiteral("%1x").arg(record.mult, 0, 'f', 4);
        // A Roll text that is itself the multiplier ("1.6477x": the bet list's figure, cut
        // rather than rounded) is shown alone, not next to a rounded copy of it.
        bool rollIsMult = false;
        if (record.rollText.endsWith(QLatin1Char('x')))
            record.rollText.chopped(1).toDouble(&rollIsMult);
        m_lastResult->setText(record.rollText.isEmpty() ? mult
                              : rollIsMult              ? record.rollText
                                                        : QStringLiteral("%1  %2").arg(record.rollText, mult));
        m_resultColor = record.win ? QStringLiteral("#2e9e46") : QStringLiteral("#c9372a");
        updateResultStyle();
        return;
    }
    m_lastResult->setText(QStringLiteral("%1%2 %3")
                              .arg(record.roll, 0, 'f', 2)
                              .arg(record.game == Game::Limbo ? QStringLiteral("x") : QString())
                              .arg(record.win ? tr("")
                                              : tr("")));
    m_resultColor = record.win ? QStringLiteral("#2e9e46") : QStringLiteral("#c9372a");
    updateResultStyle();
}

void ManualBetPanel::updateBalance(double balance)
{
    Q_UNUSED(balance);
}

void ManualBetPanel::onTargetSlider(int value)
{
    Q_UNUSED(value);
    refreshDerived();
    emit gameChanged();
}

void ManualBetPanel::onSideToggled()
{
    const bool under = m_sideBtn->property("under").toBool();
    const bool newUnder = !under;
    m_sideBtn->setProperty("under", newUnder);

    // Mirror the target around 50 like the reference app does.
    const double newTarget = std::clamp(100.0 - target(), BotConstants::kMinChance,
                                        BotConstants::kMaxChance);
    m_targetSlider->setValue(qRound(newTarget * 100));

    refreshDerived();
    emit gameChanged();
}

void ManualBetPanel::onMultiplierEdited()
{
    if (m_game == Game::Limbo) {
        m_limboTarget = std::clamp(m_multiplierSpin->value(), BotConstants::kMinLimboTarget,
                                   BotConstants::kMaxLimboTarget);
        refreshDerived();
        emit gameChanged();
        return;
    }
    const double chance = std::clamp(99.0 / m_multiplierSpin->value(), BotConstants::kMinChance,
                                     BotConstants::kMaxChance);
    const double targetFromMult = side() == BetSide::Under ? chance : 100.0 - chance;
    m_targetSlider->setValue(qRound(targetFromMult * 100));
}

void ManualBetPanel::onChanceEdited()
{
    if (m_game == Game::Limbo)
        return; // read-only in limbo
    const bool under = side() == BetSide::Under;
    const double targetFromChance = under ? m_chanceSpin->value() : 100.0 - m_chanceSpin->value();
    m_targetSlider->setValue(qRound(std::clamp(targetFromChance, BotConstants::kMinChance,
                                               BotConstants::kMaxChance) * 100));
}

void ManualBetPanel::halfAmount()
{
    m_amountSpin->setValue(std::max(0.0, m_amountSpin->value() / 2.0));
}

void ManualBetPanel::doubleAmount()
{
    m_amountSpin->setValue(
        std::min(m_startBalance, m_amountSpin->value() * 2.0));
}

void ManualBetPanel::refreshDerived()
{
    if (!isDiceOrLimbo(m_game))
        return; // no multiplier / chance: the game's own settings decide the odds
    if (m_game == Game::Limbo) {
        m_targetLabel->setText(tr("Target: %1x").arg(m_limboTarget, 0, 'f', 2));
        m_chanceSpin->blockSignals(true);
        m_chanceSpin->setValue(std::clamp(BotConstants::limboChance(m_limboTarget),
                                          BotConstants::kMinChance, BotConstants::kMaxChance));
        m_chanceSpin->blockSignals(false);
        m_multiplierSpin->blockSignals(true);
        m_multiplierSpin->setValue(m_limboTarget);
        m_multiplierSpin->blockSignals(false);
        m_profitOnWin->setText(QString::number(amount() * m_limboTarget - amount(), 'f', 8));
        return;
    }

    const bool under = side() == BetSide::Under;
    const double t = target();

    m_targetBar->setGame(t, side());
    m_targetSlider->blockSignals(true);
    m_targetSlider->setValue(qRound(t * 100));
    m_targetSlider->blockSignals(false);

    m_sideBtn->setText(under ? tr("Roll Under ⇄") : tr("Roll Over ⇄"));
    m_sideBtn->setProperty("under", under);
    m_targetLabel->setText(tr("Target: %1").arg(t, 0, 'f', 2));

    const double chance = BotConstants::winChance(t, under);
    const double mult = BotConstants::payoutMultiplier(t, under);

    m_chanceSpin->blockSignals(true);
    m_chanceSpin->setValue(chance);
    m_chanceSpin->blockSignals(false);
    m_multiplierSpin->blockSignals(true);
    m_multiplierSpin->setValue(mult);
    m_multiplierSpin->blockSignals(false);

    m_profitOnWin->setText(QString::number(amount() * mult - amount(), 'f', 8));
}
