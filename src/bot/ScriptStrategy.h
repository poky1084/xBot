#pragma once

#include "bot/BotTypes.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <functional>

enum class ScriptLanguage { Lua, JavaScript };

inline QString scriptLanguageName(ScriptLanguage l)
{
    return l == ScriptLanguage::JavaScript ? QStringLiteral("JavaScript") : QStringLiteral("Lua");
}

// Common interface of the scripting back ends (LuaStrategy, JsStrategy). Both run
// the same strategy contract: the script body runs once at the start, then an
// optional dobet() function is called after every finished bet; the globals
// nextbet / chance / bethigh / target / game / casino / currency (and the settings of Stake's
// other games, see StakeGames::scriptVariables()) are read back after each call.
class ScriptStrategy
{
public:
    enum class Status { Ok, Stop, Error };

    using Logger = std::function<void(const QString &message, bool isError)>;
    // resetstats(): the engine resets the session statistics and returns the balance
    // the fresh session starts from.
    using ResetStatsHandler = std::function<double()>;
    // resetseed([clientseed]): the engine rotates the (simulated or live) seed pair.
    // `clientSeed` is empty when the script gave none (the engine then makes one up).
    using ResetSeedHandler = std::function<void(const QString &clientSeed)>;
    // vault(amount): the engine moves `amount` of the current currency into the vault
    // (live mode).
    using VaultHandler = std::function<void(double amount)>;

    // Result of one finished bet, published to the script before dobet().
    struct BetInfo
    {
        bool win = false;
        double bet = 0.0;
        double profit = 0.0;     // profit of that bet
        double payout = 0.0;     // amount paid out for that bet (0 on a loss)
        double roll = 0.0;
        double balance = 0.0;
        double runProfit = 0.0;  // profit of the whole run
        double multiplier = 0.0; // payout multiplier of that bet
        QString betId;
        Game game = Game::Dice;
        QJsonObject betJson;     // Stake's bet object of that bet (the script's `bet`), else empty
    };

    virtual ~ScriptStrategy() = default;

    void setLogger(Logger logger) { m_logger = std::move(logger); }
    void setResetStatsHandler(ResetStatsHandler handler) { m_resetStatsHandler = std::move(handler); }
    void setResetSeedHandler(ResetSeedHandler handler) { m_resetSeedHandler = std::move(handler); }
    void setVaultHandler(VaultHandler handler) { m_vaultHandler = std::move(handler); }

    // Compiles and runs the script body. `nextBet` and `runCfg` (target / side)
    // are taken from what the script set.
    virtual Status init(const QString &source, double balance, double &nextBet,
                        BotConfig &runCfg) = 0;
    // Call after every bet. Updates nextBet / runCfg from the script's globals.
    virtual Status onBet(const BetInfo &info, double &nextBet, BotConfig &runCfg) = 0;

    // Hilo / blackjack: publishes `currentBet` (the open bet) and calls the script's round()
    // function. `result` gets what it returned (undefined when there is no round()).
    virtual Status callRound(const QJsonObject &currentBet, QJsonValue &result) = 0;

    virtual bool isLoaded() const = 0;
    // Console command line: runs `code` in the loaded script's globals. An expression's value
    // is printed (print()), statements just run (nextbet = 0.001, stop(), ...).
    virtual Status runCommand(const QString &code) = 0;
    // Overwrites a string variable of the running script (used when the casino is switched
    // from the menu: `casino` / `currency` must follow, or the script would switch back).
    virtual void setGlobalString(const char *name, const QString &value) = 0;
    virtual void close() = 0;

    const QString &lastError() const { return m_error; }
    const QString &stopReason() const { return m_stopReason; }

    // --- used by the script back ends' callbacks ---------------------------
    void requestStop(const QString &reason)
    {
        m_stop = true;
        m_stopReason = reason;
    }
    void log(const QString &message, bool isError)
    {
        if (m_logger)
            m_logger(message, isError);
    }
    // resetstats(): resets the session statistics (and the script's own counters).
    void requestResetStats()
    {
        if (!m_resetStatsHandler)
            return;
        onStatsReset(m_resetStatsHandler());
    }
    // resetseed([clientseed]): rotates the seed pair.
    void requestResetSeed(const QString &clientSeed = QString())
    {
        if (m_resetSeedHandler)
            m_resetSeedHandler(clientSeed);
    }
    // vault(amount): deposits `amount` into the vault.
    void requestVault(double amount)
    {
        if (m_vaultHandler)
            m_vaultHandler(amount);
    }

protected:
    // Called after resetstats(): zero the run counters / globals (bets, wins, losses,
    // wagered, currentstreak, profit) and set startbalance to `balance`.
    virtual void onStatsReset(double balance) = 0;

    Logger m_logger;
    ResetStatsHandler m_resetStatsHandler;
    ResetSeedHandler m_resetSeedHandler;
    VaultHandler m_vaultHandler;
    QString m_error;
    QString m_stopReason;
    bool m_stop = false;
};
