#include "ui/AutoBetPanel.h"

#include <QComboBox>
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
    spin->setRange(0.0, 10000.0);
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

    m_stopLossSpin = new QDoubleSpinBox;
    m_stopLossSpin->setRange(0.0, 1e9);
    m_stopLossSpin->setDecimals(8);
    m_stopLossSpin->setSpecialValueText(tr("off"));
    grid->addWidget(new QLabel(tr("Stop on loss (0 = off):")), 2, 2);
    grid->addWidget(m_stopLossSpin, 2, 3);

    m_delaySpin = new QSpinBox;
    m_delaySpin->setRange(0, 10000);
    m_delaySpin->setSingleStep(10);
    m_delaySpin->setValue(0);
    m_delaySpin->setSuffix(QStringLiteral(" ms"));
    m_delaySpin->setMaximumWidth(110);

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
    btnRow->addSpacing(12);
    btnRow->addWidget(new QLabel(tr("Delay:")));
    btnRow->addWidget(m_delaySpin);
    btnRow->addStretch();

    root->addLayout(btnRow);

    m_startBtn->setToolTip(tr("Settings are locked in when you press Start."));

    // CRUCIAL: Absorbs extra vertical space when dragging the splitter down,
    // keeping all widgets pinned neatly to the top.
    root->addStretch();

    m_editWidgets = {m_baseBetSpin,   m_betCountSpin,    m_onWinMode,
                     m_onWinPct,      m_onLossMode,      m_onLossPct,
                     m_stopProfitSpin, m_stopLossSpin,   m_delaySpin};

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
    cfg.delayMs = m_delaySpin->value();
}

void AutoBetPanel::setFrom(const BotConfig &cfg)
{
    m_baseBetSpin->setValue(cfg.baseBet);
    m_betCountSpin->setValue(cfg.stopAfterBets);
    m_onWinMode->setCurrentIndex(cfg.onWin.increase ? 1 : 0);
    m_onWinPct->setValue(cfg.onWin.pct);
    m_onLossMode->setCurrentIndex(cfg.onLoss.increase ? 1 : 0);
    m_onLossPct->setValue(cfg.onLoss.pct);
    m_stopProfitSpin->setValue(cfg.stopProfit);
    m_stopLossSpin->setValue(cfg.stopLoss);
    m_delaySpin->setValue(cfg.delayMs);
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
    for (QWidget *w : std::as_const(m_editWidgets))
        w->setEnabled(!running);
    m_startBtn->setEnabled(!running);
    m_stopBtn->setEnabled(running);
}

void AutoBetPanel::loadRulePreset(const OnResultRule &onWin, const OnResultRule &onLoss)
{
    m_onWinMode->setCurrentIndex(onWin.increase ? 1 : 0);
    m_onWinPct->setValue(onWin.pct);
    m_onLossMode->setCurrentIndex(onLoss.increase ? 1 : 0);
    m_onLossPct->setValue(onLoss.pct);
    updateRuleWidgets();
}

void AutoBetPanel::updateRuleWidgets()
{
    m_onWinPct->setEnabled(m_onWinMode->currentIndex() == 1);
    m_onLossPct->setEnabled(m_onLossMode->currentIndex() == 1);
}