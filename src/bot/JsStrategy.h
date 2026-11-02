#pragma once

#include "bot/ScriptStrategy.h"

#include "bot/BeepSound.h"

#include <QObject>
#include <QString>

#include <memory>
#include <cmath>

class QJSEngine;
class QJSValue;

// Receives print() / log() / stop() calls from the script (exposed to JavaScript as
// a host object; the script itself only sees the print / log / stop / console
// wrappers defined in JsStrategy.cpp).
class JsHost : public QObject
{
    Q_OBJECT

public:
    explicit JsHost(ScriptStrategy *owner, QObject *parent = nullptr)
        : QObject(parent)
        , m_owner(owner)
    {
    }

    Q_INVOKABLE void print(const QString &text) { m_owner->log(text, false); }
    Q_INVOKABLE void error(const QString &text) { m_owner->log(text, true); }
    Q_INVOKABLE void stop(const QString &reason) { m_owner->requestStop(reason); }
    Q_INVOKABLE void resetStats() { m_owner->requestResetStats(); }
    // beep(): the reference bot's bell tone (e.g. to signal a hit).
    Q_INVOKABLE void beep() { BeepSound::play(); }
    Q_INVOKABLE void resetSeed(const QString &clientSeed) { m_owner->requestResetSeed(clientSeed); }
    Q_INVOKABLE void vault(double amount)
    {
        if (std::isnan(amount))
            m_owner->log(QStringLiteral("Please specify vault amount."), true);
        else
            m_owner->requestVault(amount);
    }

private:
    ScriptStrategy *m_owner;
};

// Runs a user-written JavaScript (QJSEngine, ECMAScript) strategy. It follows the
// same contract as the Lua strategy (see LuaStrategy.h), with JavaScript syntax:
//
//   game = "dice";          // optional: "dice", "limbo" or one of Stake's games (Stake only):
//                           // "keno", "mines", "plinko", "wheel", "hilo", "blackjack", "baccarat",
//                           // "roulette", "dragontower", "flip", "rps", "pump", "snakes", "cases",
//                           // "darts", "diamonds", "videopoker", "bluesamurai", "scarabspin",
//                           // "tomeoflife", "bars", "chicken", "tarot", "drill", "packs", "moles",
//                           // "primedice". Their settings use the reference bot's variables, e.g.
//                           // keno: numbers = [1,2,3] (1 .. 40); risk = "low";
//                           // mines: fields = [1,2] (1 .. 25, 1 = top-left); mines = 3;
//                           // dragontower eggs / moles picks are 1-based too (1 = the first);
//                           // baccarat: nextbet = {player: 0, banker: 0.004, tie: 0};
//                           // roulette: chips = [{value: "colorRed", amount: 0.001}];
//                           // hilo / blackjack: function round() { return HILO_BET_HIGH; } /
//                           //   { return "BLACKJACK_STAND"; } decides each move (currentBet = the
//                           //   open bet); see StakeGames::scriptVariables() for every game.
//   currency = "btc";       // optional: currency code to bet in (default: the current bet
//                           // currency); may be changed in dobet()
//   nextbet = 0.00000100;   // required
//   chance = 49.5;          // dice
//   bethigh = true;         // dice: true = roll over, false = roll under
//   target = 2;             // limbo
//
//   function dobet() {      // optional, called after every finished bet
//       if (win) nextbet = basebet; else nextbet = previousbet * 2;
//   }
//
// Settings are plain globals: assign them without a declaration or with `var`.
// Top-level `let` / `const` bindings are not properties of the global object and
// may not be visible to the host.
//
// Read-only globals refreshed before every dobet(): win, previousbet, lastBet
// {amount, roll, Roll, profit, payout, payoutMultiplier, win, id, name}, lastprofit,
// currentprofit, roll, balance, profit, bets, wins, losses, wagered, currentstreak,
// winstreak, losestreak, startbalance, and `bet` (Stake's bet object of the last bet, e.g.
// bet.state.playerHand, bet.payoutMultiplier). Constants HILO_BET_HIGH (5), HILO_BET_LOW (4),
// HILO_BET_EQUAL (2), HILO_SKIP (7), HILO_CASHOUT (3). Video poker helpers: getOptimalHold(hand),
// getTargetedHold(hand, combos), checkTargetHit(hand, combos), evaluateHand(hand).
// Functions: stop([reason]), print(...) / log(...) / console.log(...), beep() (plays the
//   reference bot's bell tone),
//   resetstats()  resets the session statistics (the balance is kept; startbalance
//                 becomes the current balance),
//   resetseed([clientseed])  rotates the seed pair; clientseed is an optional string
//                 (resetseed("123xcv123")), otherwise a random 10-character
//                 alphanumeric one is used. Safe to call at any time; in live mode the
//                 rotation waits for a bet in flight and the next bet waits until the
//                 server confirmed it.
//   vault(amount)  deposits amount of the current currency into the vault (live mode), e.g. vault(0.001).
//
// Sandbox: QJSEngine has no file, network or OS access. A call that runs longer
// than the time limit is interrupted. Unlike Lua there is no memory limit.
class JsStrategy : public ScriptStrategy
{
public:
    JsStrategy();
    ~JsStrategy() override;
    JsStrategy(const JsStrategy &) = delete;
    JsStrategy &operator=(const JsStrategy &) = delete;

    Status init(const QString &source, double balance, double &nextBet,
                BotConfig &runCfg) override;
    Status onBet(const BetInfo &info, double &nextBet, BotConfig &runCfg) override;
    Status callRound(const QJsonObject &currentBet, QJsonValue &result) override;

    bool isLoaded() const override { return m_engine != nullptr; }
    void setGlobalString(const char *name, const QString &value) override;
    void close() override;

protected:
    void onStatsReset(double balance) override;

private:
    bool readOutputs(double &nextBet, BotConfig &runCfg);
    // Runs `fn` (a script evaluation or a function call) under the time limit.
    // Returns false and sets m_error when it threw or timed out.
    template <typename Fn>
    bool runGuarded(const char *what, Fn &&fn);
    bool readNumber(const char *name, double &out);
    QJSValue readGlobal(const char *name);
    void publish(const char *name, double value);
    void publishBool(const char *name, bool value);
    void publishLastBet(const BetInfo &info);
    void publishJson(const char *name, const QJsonValue &value);
    // The script value of global `name` as JSON (undefined when it is not set).
    QJsonValue readJson(const char *name);

    std::unique_ptr<QJSEngine> m_engine;
    std::unique_ptr<JsHost> m_host;

    // Run counters
    qint64 m_bets = 0;
    qint64 m_wins = 0;
    qint64 m_losses = 0;
    qint64 m_streak = 0;
    double m_wagered = 0.0;
};
