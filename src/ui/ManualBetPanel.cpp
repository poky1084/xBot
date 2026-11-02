#include "ui/ManualBetPanel.h"

#include "ui/GameParamsWidget.h"
#include "ui/TargetBar.h"

#include <QSignalBlocker>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
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
} // namespace

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
    m_params->setHiddenKeys({QStringLiteral("suitMode")});
    m_params->setVisible(false);
    connect(m_params, &GameParamsWidget::paramsChanged, this, &ManualBetPanel::gameChanged);

    left->addLayout(form);
    left->addWidget(m_params);
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
}

void ManualBetPanel::onBetResult(const BetRecord &record)
{
    if (record.game == Game::Dice)
        m_targetBar->setLastRoll(record.roll);
    if (!isDiceOrLimbo(record.game)) {
        // "3 hits  2.0000x", "fourOfAKind  22.0000x", "1.5000x"
        const QString mult = QStringLiteral("%1x").arg(record.mult, 0, 'f', 4);
        m_lastResult->setText(record.rollText.isEmpty() || record.rollText == mult
                                  ? mult
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
