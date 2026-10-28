#include "ui/AutoBetPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QSignalBlocker>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace
{
QComboBox *makeModeCombo()
{
    auto *combo = new QComboBox;
    combo->addItem(QStringLiteral("Reset to base"));
    combo->addItem(QStringLiteral("Increase by"));
    return combo;
}

QDoubleSpinBox *makePctSpin()
{
    auto *spin = new QDoubleSpinBox;
    // Negative = the next bet shrinks (-50% halves it); -100% would make it 0.
    spin->setRange(-100.0, 10000.0);
    spin->setDecimals(1);
    spin->setSingleStep(10.0);
    spin->setSuffix(QStringLiteral(" %"));
    return spin;
}
} // namespace

AutoBetPanel::AutoBetPanel(QWidget *parent)
    : QWidget(parent)
{
    // Tell the panel it only wants the minimum height it needs, not stretch to fill the tab
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 4, 8, 4);
    root->setSpacing(4);

    auto *grid = new QGridLayout;
    // Label columns keep their natural width; both input columns share the extra space.
    grid->setColumnStretch(0, 0);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(2, 0);
    grid->setColumnStretch(3, 1);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(3);

    auto *formHost = new QWidget;
    formHost->setLayout(grid);
    formHost->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed); // Prevent vertical stretch

    m_baseBetSpin = new QDoubleSpinBox;
    m_baseBetSpin->setRange(0.0, 1e9); // 0 is a valid bet
    m_baseBetSpin->setDecimals(8);
    m_baseBetSpin->setSingleStep(0.5);
    grid->addWidget(new QLabel(tr("Base bet:")), 0, 0);
    grid->addWidget(m_baseBetSpin, 0, 1);

    m_betCountSpin = new QSpinBox;
    m_betCountSpin->setRange(0, 10000000);
    m_betCountSpin->setSpecialValueText(tr("∞"));
    grid->addWidget(new QLabel(tr("Number of bets (0 = ∞):")), 0, 2);
    grid->addWidget(m_betCountSpin, 0, 3);

    // Left column, under the base bet: the odds the auto-bet plays at. The two boxes are
    // linked (win chance = 99 / multiplier, 1% house edge).
    m_multiplierSpin = new QDoubleSpinBox;
    m_multiplierSpin->setRange(0.99, BotConstants::kMaxLimboTarget);
    m_multiplierSpin->setDecimals(4);
    m_multiplierSpin->setSingleStep(0.01);
    m_multiplierSpin->setKeyboardTracking(false);
    m_multiplierSpin->setSuffix(QStringLiteral(" x"));
    m_multiplierSpin->setValue(2.0);
    grid->addWidget(new QLabel(tr("Multiplier:")), 1, 0);
    grid->addWidget(m_multiplierSpin, 1, 1);

    m_chanceSpin = new QDoubleSpinBox;
    m_chanceSpin->setRange(BotConstants::kMinChance, BotConstants::kMaxChance);
    m_chanceSpin->setDecimals(2);
    m_chanceSpin->setSingleStep(0.1);
    m_chanceSpin->setKeyboardTracking(false);
    m_chanceSpin->setSuffix(QStringLiteral(" %"));
    m_chanceSpin->setValue(49.5);
    grid->addWidget(new QLabel(tr("Win chance:")), 2, 0);
    grid->addWidget(m_chanceSpin, 2, 1);

    const QString oddsTip =
        tr("The odds the auto-bet plays at. Changing one updates the other\n"
           "(win chance = 99 / multiplier). Dice keeps the Under / Over side of the\n"
           "Manual tab; Limbo uses the multiplier as its target.\n"
           "Only used by the auto-bet; manual bets use the Manual tab.");
    m_multiplierSpin->setToolTip(oddsTip);
    m_chanceSpin->setToolTip(oddsTip);

    // Fixed preset by default: reset to base on loss as well.
    m_onLossMode = makeModeCombo();
    m_onLossMode->setCurrentIndex(0);
    m_onLossPct = makePctSpin();
    m_onLossPct->setValue(0.0);
    auto *onLossRow = new QHBoxLayout;
    onLossRow->setSpacing(4);
    onLossRow->setContentsMargins(0, 0, 0, 0);
    onLossRow->addWidget(m_onLossMode, 1);
    onLossRow->addWidget(m_onLossPct, 1);
    grid->addWidget(new QLabel(tr("On loss:")), 1, 2);
    grid->addLayout(onLossRow, 1, 3);

    m_onWinMode = makeModeCombo();
    m_onWinPct = makePctSpin();
    auto *onWinRow = new QHBoxLayout;
    onWinRow->setSpacing(4);
    onWinRow->setContentsMargins(0, 0, 0, 0);
    onWinRow->addWidget(m_onWinMode, 1);
    onWinRow->addWidget(m_onWinPct, 1);
    grid->addWidget(new QLabel(tr("On win:")), 2, 2);
    grid->addLayout(onWinRow, 2, 3);

    m_stopProfitSpin = new QDoubleSpinBox;
    m_stopProfitSpin->setRange(0.0, 1e9);
    m_stopProfitSpin->setDecimals(8);
    m_stopProfitSpin->setSpecialValueText(tr("off"));
    grid->addWidget(new QLabel(tr("Stop on profit (0 = off):")), 3, 2);
    grid->addWidget(m_stopProfitSpin, 3, 3);
    m_stopProfitSpin->setToolTip(
        tr("Stops the auto-bet once this run has won at least this amount\n"
           "(counted from when you pressed Start, in the betting currency).\n"
           "Example: 0.0001 = stop when the run is 0.0001 or more in profit. 0 = off."));

    m_stopLossSpin = new QDoubleSpinBox;
    m_stopLossSpin->setRange(0.0, 1e9);
    m_stopLossSpin->setDecimals(8);
    m_stopLossSpin->setSpecialValueText(tr("off"));
    grid->addWidget(new QLabel(tr("Stop on loss (0 = off):")), 4, 2);
    grid->addWidget(m_stopLossSpin, 4, 3);
    m_stopLossSpin->setToolTip(
        tr("Stops the auto-bet once this run has lost at least this amount\n"
           "(counted from when you pressed Start, in the betting currency).\n"
           "Example: 0.0005 = stop when the run is 0.0005 or more in loss. 0 = off."));

    m_useConditions = new QCheckBox;
    m_useConditions->setToolTip(
        tr("Ticked: the Condition Builder always runs the auto-bet (even with no blocks).\n"
           "Base bet (first bet and \"reset amount\"), multiplier and win chance are used;\n"
           "the other settings are ignored. Dice side comes from the Manual tab.\n"
           "Unticked: the settings above are used."));
    // Left column, under the win chance (the right column runs two rows longer).
    grid->addWidget(m_useConditions, 3, 0, 1, 2);
    setConditionCount(0);

    root->addWidget(formHost);

    // ---- Buttons -------------------------------------------------------
    m_startBtn = new QPushButton(tr("Start auto-bet"));
    m_startBtn->setIcon(QIcon(QStringLiteral(":/icons/torrent-start.svg")));
    m_stopBtn = new QPushButton(tr("Stop"));
    m_stopBtn->setIcon(QIcon(QStringLiteral(":/icons/torrent-stop.svg")));
    m_stopBtn->setEnabled(false);

    auto *btnRow = new QHBoxLayout;
    btnRow->setSpacing(4);
    btnRow->setContentsMargins(0, 0, 0, 0);
    btnRow->addWidget(m_startBtn);
    btnRow->addWidget(m_stopBtn);
    btnRow->addStretch();

    // Buttons sit under the checkbox in the left column.
    grid->addLayout(btnRow, 4, 0, 1, 2);

    m_baseBetSpin->setToolTip(tr("The first bet of the run, and the amount \"Reset to base\" goes back to."));
    m_betCountSpin->setToolTip(tr("Stops after this many bets in this run. 0 = no limit."));
    const QString ruleTip =
        tr("Reset to base: the next bet is the base bet.\n"
           "Increase by: the next bet is the previous bet + this percentage\n"
           "(100% doubles it, 50% multiplies it by 1.5, -50% halves it).\n"
           "Typing a percentage other than 0 selects \"Increase by\" automatically.");
    for (QWidget *w : {static_cast<QWidget *>(m_onWinMode), static_cast<QWidget *>(m_onWinPct),
                       static_cast<QWidget *>(m_onLossMode), static_cast<QWidget *>(m_onLossPct)})
        w->setToolTip(ruleTip);

    m_startBtn->setToolTip(tr("Settings are locked in when you press Start."));

    // CRUCIAL: Absorbs extra vertical space when dragging the splitter down,
    // keeping all widgets pinned neatly to the top.
    root->addStretch();

    m_editWidgets = {m_baseBetSpin,   m_multiplierSpin,  m_chanceSpin,
                     m_betCountSpin,  m_onWinMode,
                     m_onWinPct,      m_onLossMode,      m_onLossPct,
                     m_stopProfitSpin, m_stopLossSpin,
                     m_useConditions};

    // Stretch horizontally, but keep vertical size perfectly fixed
    for (QWidget *w : std::as_const(m_editWidgets)) {
        QSizePolicy sp = w->sizePolicy();
        sp.setHorizontalPolicy(QSizePolicy::Expanding);
        sp.setVerticalPolicy(QSizePolicy::Fixed);
        w->setSizePolicy(sp);
    }

    // Lock the vertical height of the buttons as well
    for (QPushButton *b : {m_startBtn, m_stopBtn}) {
        b->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    }

    // ---- Connections ----------------------------------------------------
    connect(m_startBtn, &QPushButton::clicked, this, &AutoBetPanel::startRequested);
    connect(m_stopBtn, &QPushButton::clicked, this, &AutoBetPanel::stopRequested);
    connect(m_onWinMode, &QComboBox::currentIndexChanged, this,
            [this](int) { updateRuleWidgets(); });
    connect(m_onLossMode, &QComboBox::currentIndexChanged, this,
            [this](int) { updateRuleWidgets(); });
    // A percentage only does something with "Increase by": typing one (also negative) selects it.
    connect(m_onWinPct, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (v != 0.0 && m_onWinMode->currentIndex() == 0)
            m_onWinMode->setCurrentIndex(1);
    });
    connect(m_onLossPct, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (v != 0.0 && m_onLossMode->currentIndex() == 0)
            m_onLossMode->setCurrentIndex(1);
    });
    // Multiplier <-> win chance stay in step.
    connect(m_multiplierSpin, &QDoubleSpinBox::valueChanged, this, [this](double mult) {
        if (mult <= 0.0)
            return;
        const QSignalBlocker b(m_chanceSpin);
        m_chanceSpin->setValue(std::clamp(99.0 / mult, BotConstants::kMinChance,
                                          BotConstants::kMaxChance));
    });
    connect(m_chanceSpin, &QDoubleSpinBox::valueChanged, this, [this](double chance) {
        if (chance <= 0.0)
            return;
        const QSignalBlocker b(m_multiplierSpin);
        m_multiplierSpin->setValue(std::floor(99.0 / chance * 10000.0) / 10000.0);
    });
    connect(m_useConditions, &QCheckBox::toggled, this, [this](bool) { updateRuleWidgets(); });

    updateRuleWidgets();
}

void AutoBetPanel::applyTo(BotConfig &cfg) const
{
    cfg.baseBet = m_baseBetSpin->value();
    cfg.stopAfterBets = m_betCountSpin->value();
    cfg.onWin = {m_onWinMode->currentIndex() == 1, m_onWinPct->value()};
    cfg.onLoss = {m_onLossMode->currentIndex() == 1, m_onLossPct->value()};
    cfg.stopProfit = m_stopProfitSpin->value();
    cfg.stopLoss = m_stopLossSpin->value();
    applyOddsTo(cfg);
}

void AutoBetPanel::applyOddsTo(BotConfig &cfg) const
{
    // Odds: dice keeps the side already in cfg (from the Manual tab) and gets its target
    // from the win chance; limbo uses the multiplier as its target.
    const double chance = std::round(m_chanceSpin->value() * 100.0) / 100.0;
    const double diceTarget = cfg.side == BetSide::Under ? chance : 100.0 - chance;
    cfg.target = std::clamp(diceTarget, BotConstants::kMinChance, BotConstants::kMaxChance);
    cfg.multiplierTarget = std::clamp(m_multiplierSpin->value(), BotConstants::kMinLimboTarget,
                                      BotConstants::kMaxLimboTarget);
}

double AutoBetPanel::multiplier() const
{
    return m_multiplierSpin->value();
}

double AutoBetPanel::winChance() const
{
    return m_chanceSpin->value();
}

void AutoBetPanel::setOdds(double multiplier, double chance)
{
    const QSignalBlocker b1(m_multiplierSpin);
    const QSignalBlocker b2(m_chanceSpin);
    m_multiplierSpin->setValue(multiplier);
    m_chanceSpin->setValue(chance);
}

void AutoBetPanel::setFrom(const BotConfig &cfg)
{
    m_baseBetSpin->setValue(cfg.baseBet);
    m_betCountSpin->setValue(cfg.stopAfterBets);
    {
        const QSignalBlocker b1(m_onWinPct);
        const QSignalBlocker b2(m_onLossPct);
        m_onWinMode->setCurrentIndex(cfg.onWin.increase ? 1 : 0);
        m_onWinPct->setValue(cfg.onWin.pct);
        m_onLossMode->setCurrentIndex(cfg.onLoss.increase ? 1 : 0);
        m_onLossPct->setValue(cfg.onLoss.pct);
    }
    m_stopProfitSpin->setValue(cfg.stopProfit);
    m_stopLossSpin->setValue(cfg.stopLoss);
    updateRuleWidgets();
}

void AutoBetPanel::setBetPrecision(int decimals, double minBet)
{
    m_baseBetSpin->setDecimals(decimals);
    m_baseBetSpin->setMinimum(minBet);
    m_stopProfitSpin->setDecimals(decimals);
    m_stopLossSpin->setDecimals(decimals);
}

void AutoBetPanel::setRunning(bool running)
{
    m_running = running;
    m_startBtn->setEnabled(!running);
    m_stopBtn->setEnabled(running);
    updateRuleWidgets();
}

void AutoBetPanel::setConditionCount(int count)
{
    m_useConditions->setText(
        count > 0 ? tr("Use Condition Builder").arg(count)
                  : tr("Use Condition Builder").arg(count));
    updateRuleWidgets();
}

bool AutoBetPanel::useConditions() const
{
    return m_useConditions->isChecked();
}

void AutoBetPanel::setUseConditions(bool on)
{
    m_useConditions->setChecked(on);
    updateRuleWidgets();
}

void AutoBetPanel::loadRulePreset(const OnResultRule &onWin, const OnResultRule &onLoss)
{
    const QSignalBlocker b1(m_onWinPct);
    const QSignalBlocker b2(m_onLossPct);
    m_onWinMode->setCurrentIndex(onWin.increase ? 1 : 0);
    m_onWinPct->setValue(onWin.pct);
    m_onLossMode->setCurrentIndex(onLoss.increase ? 1 : 0);
    m_onLossPct->setValue(onLoss.pct);
    updateRuleWidgets();
}

void AutoBetPanel::updateRuleWidgets()
{
    // The settings are always editable, also during a run and with "Use Condition Builder"
    // ticked (they are read when Start is pressed; in condition mode only the base bet
    // takes effect). Only the mode checkbox is locked while a run is going.
    for (QWidget *w : std::as_const(m_editWidgets))
        w->setEnabled(w == m_useConditions ? !m_running : true);
}