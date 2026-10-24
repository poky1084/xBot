#pragma once

#include "bot/BotTypes.h"

#include <QWidget>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;

// "Auto Bet" tab: progression rules and stop conditions, mirroring the
// reference app. Settings lock while a session is running.
class AutoBetPanel : public QWidget
{
    Q_OBJECT

public:
    explicit AutoBetPanel(QWidget *parent = nullptr);

    // Reads the auto-bet portion of the config (rules, stops, delay, count).
    void applyTo(BotConfig &cfg) const;
    void setFrom(const BotConfig &cfg);

    void setRunning(bool running);
    // Live betting needs crypto-sized amounts (e.g. 8 decimals, min 1e-8).
    void setBetPrecision(int decimals, double minBet);

signals:
    void startRequested();
    void stopRequested();

private:
    void loadRulePreset(const OnResultRule &onWin, const OnResultRule &onLoss);
    void updateRuleWidgets();

    QDoubleSpinBox *m_baseBetSpin = nullptr;
    QSpinBox *m_betCountSpin = nullptr;
    QComboBox *m_onWinMode = nullptr;
    QDoubleSpinBox *m_onWinPct = nullptr;
    QComboBox *m_onLossMode = nullptr;
    QDoubleSpinBox *m_onLossPct = nullptr;
    QDoubleSpinBox *m_stopProfitSpin = nullptr;
    QDoubleSpinBox *m_stopLossSpin = nullptr;
    QSpinBox *m_delaySpin = nullptr;
    QPushButton *m_startBtn = nullptr;
    QPushButton *m_stopBtn = nullptr;
    QList<QWidget *> m_editWidgets;
};
