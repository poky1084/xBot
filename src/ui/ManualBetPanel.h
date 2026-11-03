#pragma once

#include "bot/BotTypes.h"

#include <QWidget>

class QFormLayout;
class QJsonObject;
class QLabel;
class QPushButton;
class QDoubleSpinBox;
class QScrollArea;
class QSlider;

class GameParamsWidget;
class BlackjackTable;
class HiloCardStrip;
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
    // Leaves out the game choices the selected casino does not offer (GameParamsWidget::setCasino).
    void setCasino(const QString &casinoKey);
    Game game() const { return m_game; }

    // Settings of Stake's other games, all games at once (saved per window).
    QJsonObject gameParams() const;
    void setGameParams(const QJsonObject &all);

    void setStartBalance(double balance);
    void setLocked(bool locked);       // disabled while auto-betting
    // Live betting needs crypto-sized amounts (e.g. 8 decimals, min 1e-8).
    void setBetPrecision(int decimals, double minBet);

    // Height the visible controls actually take (top margin to the lowest visible control,
    // plus the bottom margin), so the tab can be just as tall as its content. 0 = not laid
    // out yet (use sizeHint()).
    int contentHeight() const;

public slots:
    void onBetResult(const BetRecord &record);
    void updateBalance(double balance);
    // Manual hilo round (BotEngine::manualGameUpdated): shows its cards and enables the
    // High / Low / Equal / Skip / Cash out buttons while it waits for a move.
    // playable = false: an auto-bet / script round (cards only, the buttons stay off).
    void showHiloRound(const QJsonObject &bet, bool active, bool playable = true);
    // Blackjack: the dealer's and the player's cards; Hit / Stand / Split / Double (and
    // Insurance / No insurance when the dealer shows an ace) while a manual hand waits.
    void showBlackjackRound(const QJsonObject &bet, bool active, bool playable = true);

signals:
    void rollRequested();
    void hiloMoveRequested(const QString &move); // "high", "low", "equal", "skip", "cashout"
    // "hit", "stand", "double", "split", "insurance", "noInsurance"
    void blackjackMoveRequested(const QString &move);
    void gameChanged();                // target/side/amount changed
    void amountChanged(double amount); // the bet amount was changed (typed, ½ / 2×, loaded)
    // Something shown / hidden that can change contentHeight() (e.g. the insurance row).
    void contentHeightChanged();

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
    void updateHiloButtons();
    void updateBlackjackButtons();

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
    // Hilo manual play (C# bot): move buttons and the card showcase.
    QWidget *m_hiloBox = nullptr;
    QPushButton *m_hiloHigh = nullptr;
    QPushButton *m_hiloLow = nullptr;
    QPushButton *m_hiloEqual = nullptr;
    QPushButton *m_hiloSkip = nullptr;
    QPushButton *m_hiloCashout = nullptr;
    QPushButton *m_hiloNewGame = nullptr;
    QScrollArea *m_hiloScroll = nullptr;
    HiloCardStrip *m_hiloCards = nullptr;
    QLabel *m_hiloStatus = nullptr;
    bool m_hiloActive = false;  // a round waits for a move
    int m_hiloRounds = 0;       // cards drawn after the start card
    QString m_hiloRank;         // the card the next guess is about
    bool m_locked = false;
    // Blackjack manual play.
    QWidget *m_bjBox = nullptr;
    QWidget *m_bjInsRow = nullptr;
    QPushButton *m_bjHit = nullptr;
    QPushButton *m_bjStand = nullptr;
    QPushButton *m_bjSplit = nullptr;
    QPushButton *m_bjDouble = nullptr;
    QPushButton *m_bjInsurance = nullptr;
    QPushButton *m_bjNoInsurance = nullptr;
    QScrollArea *m_bjScroll = nullptr;
    BlackjackTable *m_bjTable = nullptr;
    QLabel *m_bjStatus = nullptr;
    bool m_bjActive = false;     // a manual hand waits for a move
    bool m_bjInsurance_ = false; // the dealer shows an ace: insurance has to be decided first
    bool m_bjCanSplit = false;
    bool m_bjCanDouble = false;
    bool m_bjCanHit = true;   // false only when the casino's allowed actions leave it out
    bool m_bjCanStand = true;
    double m_startBalance = BotConstants::kStartBalance;
};
