#pragma once

#include "bot/BotTypes.h"

#include <QWidget>

class QLabel;
class QPushButton;
class QDoubleSpinBox;
class QSlider;

class TargetBar;

// "Manual Bet" tab, mirroring the reference app's bet panel.
class ManualBetPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ManualBetPanel(QWidget *parent = nullptr);

    // Current game settings shared with the auto-bet engine.
    double amount() const;
    double target() const;
    BetSide side() const;
    BotConfig config() const;   // fills game/target/side/baseBet into a BotConfig

    // Dice shows the target bar / side button; Limbo shows only the multiplier target.
    void setGame(Game game);
    Game game() const { return m_game; }

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
