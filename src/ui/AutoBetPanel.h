#pragma once

#include "bot/BotTypes.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QJsonObject;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;

class GameParamsWidget;

// "Auto Bet" tab: progression rules and stop conditions, mirroring the
// reference app. Settings lock while a session is running.
class AutoBetPanel : public QWidget
{
    Q_OBJECT

public:
    explicit AutoBetPanel(QWidget *parent = nullptr);

    // Reads the auto-bet portion of the config (rules, stops, count).
    void applyTo(BotConfig &cfg) const;
    void setFrom(const BotConfig &cfg);
    // Only the Multiplier / Win chance / Direction: sets the dice target and side (from the
    // Direction box, not the Manual tab) and the limbo target. Used for every auto-bet run,
    // also in Condition Builder mode. Stake's other games: their settings from this tab
    // (BotConfig::gameParams) instead.
    void applyOddsTo(BotConfig &cfg) const;

    // The game being played: dice / limbo show Multiplier / Win chance / Direction, Stake's
    // other games their own settings (risk, numbers, mines, ...) in that place.
    void setGame(Game game);
    // Leaves out the game choices the selected casino does not offer (GameParamsWidget::setCasino).
    void setCasino(const QString &casinoKey);
    // Settings of Stake's other games for the auto-bet, all games at once (saved per window).
    QJsonObject gameParams() const;
    void setGameParams(const QJsonObject &all);

    // Auto-bet odds (left column). Saved / restored by MainWindow.
    double multiplier() const;
    // The app's win chance (1 % house edge, 99 / multiplier), whatever casino is shown.
    double winChance() const;
    void setOdds(double multiplier, double chance);
    BetSide side() const;
    void setSide(BetSide side);

    // The casino the odds are shown for: `rtp` = what its dice pays per 100 % chance (99 for a
    // 1 % edge, Pasino 95, Tower 98.5, Duel 99.9). The Win chance box shows that casino's real
    // chance for the multiplier; `floorChance` = the casino cuts the chance down (Tower).
    // `dice` = false (limbo): the Direction box is disabled.
    // oddsUsed = false: Stake's other games (keno, mines, ...) take their odds from this tab's
    // game settings (see setGame()), so Multiplier / Win chance / Direction are not used.
    void setOddsContext(double rtp, bool floorChance, bool dice, bool oddsUsed = true);

    void setRunning(bool running);

    // Condition Builder: its blocks replace the on win / on loss rules only when this box
    // is ticked. `count` = number of saved blocks (shown in the label).
    void setConditionCount(int count);
    bool useConditions() const;
    void setUseConditions(bool on);
    // Live betting needs crypto-sized amounts (e.g. 8 decimals, min 1e-8).
    void setBetPrecision(int decimals, double minBet);

signals:
    void startRequested();
    void stopRequested();
    // Any setting was changed (a finished edit: Enter, leaving the box, arrows, a pick).
    // MainWindow applies it to a running auto-bet straight away.
    void settingsEdited();

private:
    void loadRulePreset(const OnResultRule &onWin, const OnResultRule &onLoss);
    void updateRuleWidgets();
    void applyGameLayout();
    double casinoChanceFor(double multiplier) const;

    double m_rtp = 99.0;
    bool m_floorChance = false;
    bool m_dice = true;
    bool m_oddsUsed = true;
    QComboBox *m_directionCombo = nullptr;
    Game m_game = Game::Dice;
    GameParamsWidget *m_params = nullptr;
    QWidget *m_oddsHost = nullptr; // Multiplier / Win chance / Direction (dice and limbo)

    QDoubleSpinBox *m_baseBetSpin = nullptr;
    QDoubleSpinBox *m_multiplierSpin = nullptr;
    QDoubleSpinBox *m_chanceSpin = nullptr;
    QSpinBox *m_betCountSpin = nullptr;
    QComboBox *m_onWinMode = nullptr;
    QDoubleSpinBox *m_onWinPct = nullptr;
    QComboBox *m_onLossMode = nullptr;
    QDoubleSpinBox *m_onLossPct = nullptr;
    QCheckBox *m_useConditions = nullptr;
    bool m_running = false;
    QDoubleSpinBox *m_stopProfitSpin = nullptr;
    QDoubleSpinBox *m_stopLossSpin = nullptr;
    QPushButton *m_startBtn = nullptr;
    QPushButton *m_stopBtn = nullptr;
    QList<QWidget *> m_editWidgets;
};
