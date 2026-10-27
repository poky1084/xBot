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

    m_onWinMode = makeModeCombo();
    m_onWinPct = makePctSpin();
    auto *onWinRow = new QHBoxLayout;
    onWinRow->setSpacing(4);
    onWinRow->setContentsMargins(0, 0, 0, 0);
    onWinRow->addWidget(m_onWinMode, 1);
    onWinRow->addWidget(m_onWinPct, 1);
    grid->addWidget(new QLabel(tr("On win:")), 1, 0);
    grid->addLayout(onWinRow, 1, 1);

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

    m_stopProfitSpin = new QDoubleSpinBox;
    m_stopProfitSpin->setRange(0.0, 1e9);
    m_stopProfitSpin->setDecimals(8);
    m_stopProfitSpin->setSpecialValueText(tr("off"));
    grid->addWidget(new QLabel(tr("Stop on profit (0 = off):")), 2, 0);
    grid->addWidget(m_stopProfitSpin, 2, 1);
    m_stopProfitSpin->setToolTip(
        tr("Stops the auto-bet once this run has won at least this amount\n"
           "(counted from when you pressed Start, in the betting currency).\n"
           "Example: 0.0001 = stop when the run is 0.0001 or more in profit. 0 = off."));

    m_stopLossSpin = new QDoubleSpinBox;
    m_stopLossSpin->setRange(0.0, 1e9);
    m_stopLossSpin->setDecimals(8);
    m_stopLossSpin->setSpecialValueText(tr("off"));
    grid->addWidget(new QLabel(tr("Stop on loss (0 = off):")), 2, 2);
    grid->addWidget(m_stopLossSpin, 2, 3);
    m_stopLossSpin->setToolTip(
        tr("Stops the auto-bet once this run has lost at least this amount\n"
           "(counted from when you pressed Start, in the betting currency).\n"
           "Example: 0.0005 = stop when the run is 0.0005 or more in loss. 0 = off."));

    m_useConditions = new QCheckBox;
    m_useConditions->setToolTip(
        tr("Ticked: the Condition Builder always runs the auto-bet (even with no blocks).\n"
           "Only the base bet above is used (first bet and \"reset amount\"); the other\n"
           "settings are ignored, chance and side come from the Manual tab.\n"
           "Unticked: the settings above are used."));
    grid->addWidget(m_useConditions, 3, 0, 1, 4);
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

    root->addLayout(btnRow);

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

    m_editWidgets = {m_baseBetSpin,   m_betCountSpin,    m_onWinMode,
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