#pragma once

#include "bot/ScriptStrategy.h"

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
//   game = "dice";          // optional: "dice" or "limbo"
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
// {amount, roll, profit, payout}, lastprofit, roll, balance, profit, bets, wins,
// losses, wagered, currentstreak, startbalance.
// Functions: stop([reason]), print(...) / log(...) / console.log(...),
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
    void publishLastBet(double amount, double roll, double profit, double payout);

    std::unique_ptr<QJSEngine> m_engine;
    std::unique_ptr<JsHost> m_host;

    // Run counters
    qint64 m_bets = 0;
    qint64 m_wins = 0;
    qint64 m_losses = 0;
    qint64 m_streak = 0;
    double m_wagered = 0.0;
};
