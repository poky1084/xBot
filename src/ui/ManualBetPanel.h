#pragma once

#include "bot/BotTypes.h"

#include <QWidget>

class QFormLayout;
class QJsonObject;
class QLabel;
class QPushButton;
class QDoubleSpinBox;
class QSlider;

class GameParamsWidget;
class TargetBar;

// "Manual Bet" tab, mirroring the reference app's bet panel.
class ManualBetPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ManualBetPanel(QWidget *parent = nullptr);

    // Current game settings shared with the auto-bet engine.
    double amount() const;
    void setAmount(double amount); // restores the saved bet amount on launch
    double target() const;
    BetSide side() const;
    BotConfig config() const;   // fills game/target/side/baseBet into a BotConfig

    // Saved per window: the dice target + side and the limbo target. The Multiplier and
    // Win chance boxes are derived from them (dice: target/side, limbo: target).
    double limboTarget() const { return m_limboTarget; }
    void setTargets(double diceTarget, bool under, double limboTarget);

    // Dice shows the target bar / side button; Limbo shows only the multiplier target; Stake's
    // other games show their own settings (risk, numbers, mines, ...).
    void setGame(Game game);
    Game game() const { return m_game; }

    // Settings of Stake's other games, all games at once (saved per window).
    QJsonObject gameParams() const;
    void setGameParams(const QJsonObject &all);

    void setStartBalance(double balance);
    void setLocked(bool locked);       // disabled while auto-betting
    // Live betting needs crypto-sized amounts (e.g. 8 decimals, min 1e-8).
    void setBetPrecision(int decimals, double minBet);

public slots:
    void onBetResult(const BetRecord &record);
    void updateBalance(double balance);

signals:
    void rollRequested();
    void gameChanged();                // target/side/amount changed
    void amountChanged(double amount); // the bet amount was changed (typed, ½ / 2×, loaded)

private slots:
    void onTargetSlider(int value);
    void onSideToggled();
    void onMultiplierEdited();
    void onChanceEdited();
    void halfAmount();
    void doubleAmount();

private:
    void refreshDerived();
    void applyGameLayout();
    void updateResultStyle();

    QFormLayout *m_form = nullptr;
    GameParamsWidget *m_params = nullptr;
    QDoubleSpinBox *m_amountSpin = nullptr;
    QDoubleSpinBox *m_multiplierSpin = nullptr;
    QDoubleSpinBox *m_chanceSpin = nullptr;
    QLabel *m_profitOnWin = nullptr;
    QPushButton *m_rollBtn = nullptr;
    QPushButton *m_sideBtn = nullptr;
    QLabel *m_targetLabel = nullptr;
    TargetBar *m_targetBar = nullptr;
    QSlider *m_targetSlider = nullptr;
    QWidget *m_scaleWidget = nullptr;   // the 0 .. 100 labels under the slider
    Game m_game = Game::Dice;
    double m_limboTarget = 2.0;
    QString m_resultColor;   // css color of the last-result label (green / red)
    QLabel *m_lastResult = nullptr;
    double m_startBalance = BotConstants::kStartBalance;
};
