#pragma once

#include "bot/BotTypes.h"
#include "bot/ConditionRunner.h"
#include "bot/IDiceProvider.h"
#include "bot/JsStrategy.h"
#include "bot/LuaStrategy.h"
#include "bot/ScriptStrategy.h"

#include <QElapsedTimer>
#include <QTimer>

#include <functional>

class StakeClient;
struct LiveBetResult;

class BotEngine : public QObject
{
    Q_OBJECT

public:
    explicit BotEngine(QObject *parent = nullptr);

    void setProvider(IDiceProvider *provider) { m_provider = provider; }
    IDiceProvider *provider() const { return m_provider; }

    void setConfig(const BotConfig &cfg) { m_config = cfg; }
    const BotConfig &config() const { return m_config; }

    // Condition Builder rules for the next auto-bet run. An empty list falls
    // back to the on-win / on-loss rules from the config.
    void setConditions(const BlockList &blocks) { m_condBlocks = blocks; }

    // Script strategy (Lua or JavaScript) for the next run. A non-empty script
    // replaces both the Condition Builder and the on-win / on-loss rules; an empty
    // one turns it off.
    void setScript(const QString &source, ScriptLanguage language = ScriptLanguage::Lua)
    {
        m_scriptSource = source;
        m_scriptLanguage = language;
    }
    bool scriptActive() const { return m_scriptActive; }

    // Currency hooks (set by MainWindow) for the script variable `currency`.
    //   getter:   the currency code being bet in right now (lowercase).
    //   switcher: switches to another one (live: the bet currency, simulation: just the
    //             label) and returns the balance in it; false + error if that is not possible.
    using CurrencyGetter = std::function<QString()>;
    using CurrencySwitcher = std::function<bool(const QString &currency, double &balance, QString &error)>;
    void setCurrencyHooks(CurrencyGetter getter, CurrencySwitcher switcher)
    {
        m_currencyGetter = std::move(getter);
        m_currencySwitcher = std::move(switcher);
    }

    // Casino hooks (set by MainWindow) for the script variable `casino` ("stake" / "shuffle").
    //   getter:   the casino bets go to right now.
    //   switcher: switches to the other one WITHOUT resetting the session; false + error
    //             if that is not possible.
    using CasinoGetter = std::function<QString()>;
    using CasinoSwitcher = std::function<bool(const QString &casino, QString &error)>;
    void setCasinoHooks(CasinoGetter getter, CasinoSwitcher switcher)
    {
        m_casinoGetter = std::move(getter);
        m_casinoSwitcher = std::move(switcher);
    }

    const SessionStats &stats() const { return m_stats; }

    bool isRunning() const { return m_running; }
    bool isPaused() const { return m_paused; }

    // Runs a single bet immediately (manual "Roll Dice"); no session needed.
    // In live mode the bet is asynchronous: the returned record is empty and the
    // result arrives through betPlaced().
    BetRecord rollOnce(const BotConfig &cfg);

    // Clears the session and restores the starting balance.
    void resetSession(double initialBalance)
    {
        m_stats.reset(initialBalance);
        m_runStartProfit = 0.0; // the run's profit is measured from the fresh session
    }

    // Live seed rotation finished (success or failure). Releases the next bet that
    // a script's resetseed() was holding back; a failed rotation is retried a few times.
    void seedRotationFinished(bool ok = true);

    // ---- Live mode (real bets through a StakeClient) -----------------------
    // In live mode the server decides roll, payout and balance; the simulated
    // provider is not used.
    void setLiveClient(StakeClient *client);
    void setLiveEnabled(bool enabled);
    bool liveMode() const { return m_liveEnabled && m_live != nullptr; }
    bool betInFlight() const { return m_inFlight; }
    bool hasLiveBalance() const { return m_liveBalanceKnown; }
    void invalidateLiveBalance() { m_liveBalanceKnown = false; } // wait for a fresh server balance
    // The live connection was just (re)configured and is logging in (hidden browser loading,
    // first balance on its way): Start / Roll wait for it for a while instead of failing.
    void markLiveConnecting() { m_connectGrace.start(); m_connectGraceActive = true; }
    // Start was pressed while the connection was still coming up; it begins when ready.
    bool startPending() const { return m_startPending; }
    // Overwrites the tracked balance with the server's figure (ignored while a
    // bet is in flight, because the figure may predate that bet).
    void syncBalance(double balance);

public slots:
    void start();
    void stop(const QString &reason = {});
    void pause();
    void resume();

signals:
    void started();
    void stopped(const QString &reason);
    void pausedChanged(bool paused);
    void betPlaced(const BetRecord &record);
    void logMessage(const QString &message, bool isError);
    void scriptError(const QString &message);   // script failed to load or run
    void scriptOutput(const QString &message, bool isError); // print()/log() text and script errors
    // The game being played changed (a script set game = "dice" / "limbo").
    void gameChanged(Game game);
    // A script called resetstats(): the statistics were reset (the balance is kept).
    void statsReset();
    // A script called resetseed(): rotate the seed pair with this client seed
    // (MainWindow does the work). Never emitted while a live bet is in flight.
    void seedRotationRequested(const QString &clientSeed);
    // A script called vault(amount) in live mode (MainWindow sends the deposit).
    void vaultRequested(double amount);

private slots:
    void placeNextBet();
    void onLiveFinished(const LiveBetResult &result);

private:
    BetRecord placeBet(const BotConfig &cfg, double betSize);
    BetRecord recordBet(const BotConfig &cfg, double betSize, double roll, bool win,
                        double mult, double payout, qint64 nonce,
                        const QString &betId = QString());
    bool checkStopConditions(); // true if a stop condition fired (engine stopped)
    void scheduleNext();
    bool advanceProgression(bool win, const BotConfig &cfg); // true if a stop fired
    void stopWithReason(const QString &reason);
    // After Stop with a scripted bet still in flight: hand that bet's result to the
    // script one last time (so its print()s show the latest roll), then close it.
    void finishScriptDrain(bool feedLastBet);
    void notifyGameChange();
    QString currentCurrency() const;
    bool applyScriptCasino();   // true if switching failed and the run was halted
    bool applyScriptCurrency(); // true if switching failed and the run was halted
    double resetStatsFromScript();
    void rotateSeedFromScript(const QString &clientSeed);
    void vaultFromScript(double amount);
    void dispatchSeedRotation();

    bool liveReady(QString *whyNot) const;
    void requestLiveBet(const BotConfig &cfg, double betSize, bool manual);

    LuaStrategy m_luaStrategy;
    JsStrategy m_jsStrategy;
    ScriptStrategy *m_script = &m_luaStrategy; // back end of the current / next run
    QString m_scriptSource;
    ScriptLanguage m_scriptLanguage = ScriptLanguage::Lua;
    bool m_scriptActive = false;
    bool m_awaitTransport = false;  // casino was just switched: wait until its connection is ready
    QElapsedTimer m_awaitTimer;
    CasinoGetter m_casinoGetter;
    CasinoSwitcher m_casinoSwitcher;
    CurrencyGetter m_currencyGetter;
    CurrencySwitcher m_currencySwitcher;
    bool m_scriptDraining = false; // stopped, waiting for the last in-flight scripted bet
    BetRecord m_lastRec;           // last finished bet (fed to the script)

    BotConfig m_config;
    BotConfig m_runCfg;           // config of the running auto-bet (conditions may change target/side)
    Game m_announcedGame = Game::Dice; // last game reported through gameChanged()
    BlockList m_condBlocks;
    ConditionRunner m_conditions;
    double m_runStartProfit = 0.0;
    SessionStats m_stats;
    IDiceProvider *m_provider = nullptr;
    QTimer m_timer;
    double m_nextBetSize = 1.0;
    bool m_running = false;
    bool m_paused = false;

    // Live mode state
    StakeClient *m_live = nullptr;
    bool m_liveEnabled = false;
    bool m_liveBalanceKnown = false;
    bool connectionPending(const QString &whyNot) const;  // still logging in (not a permanent failure)
    QElapsedTimer m_connectGrace;
    bool m_connectGraceActive = false;
    bool m_startPending = false;
    bool m_startRetrying = false;
    QElapsedTimer m_startClock;
    bool m_rollPending = false;
    bool m_rollRetrying = false;
    QElapsedTimer m_rollClock;
    bool m_inFlight = false;
    bool m_inFlightManual = false;
    BotConfig m_inFlightCfg;
    int m_retryStreak = 0;

    // resetseed() in live mode: the next bet waits until the server answered the
    // rotation (with a safety timeout so a lost answer cannot freeze the run).
    bool m_seedRotationPending = false;
    bool m_seedRotationQueued = false;   // waiting for the bet in flight to finish
    int m_seedRotationRetries = 0;
    QString m_pendingSeed;
    QTimer m_seedWaitTimer;
};
