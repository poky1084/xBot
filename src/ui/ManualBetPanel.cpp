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

QString hiloGuessSymbol(const QString &guess)
{
    if (guess.startsWith(QLatin1String("higher")))
        return QString(QChar(0x2191)); // ↑
    if (guess.startsWith(QLatin1String("lower")))
        return QString(QChar(0x2193)); // ↓
    if (guess == QLatin1String("equal"))
        return QStringLiteral("=");
    if (guess == QLatin1String("skip"))
        return QString(QChar(0x00BB)); // »
    return {};
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
} // namespace

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
            p.fillPath(path, Qt::white);
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
    }

private:
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

    left->addLayout(form);
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
    updateHiloButtons();
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
        const QString sym = hiloGuessSymbol(guess);
        c.caption = sym.isEmpty() ? hiloMultText(current) : sym + QLatin1Char(' ') + hiloMultText(current);
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
