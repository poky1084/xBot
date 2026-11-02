#pragma once

#include "bot/BotTypes.h"
#include "bot/ConditionRunner.h"
#include "bot/IDiceProvider.h"
#include "bot/JsStrategy.h"
#include "bot/LuaStrategy.h"
#include "bot/ScriptStrategy.h"

#include <QElapsedTimer>
#include <QJsonObject>
#include <QJsonValue>
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

    // Condition Builder rules for the next auto-bet run (used only in condition mode,
    // see setConditionMode()).
    void setConditions(const BlockList &blocks) { m_condBlocks = blocks; }
    // true = Condition Builder mode: the blocks drive the run (even when there are none)
    // and the Auto Bet tab's rules are not used.
    void setConditionMode(bool on) { m_condMode = on; }

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
    //   label:    display name of a casino ("stake" -> "Stake") for log messages.
    using CasinoLabel = std::function<QString(const QString &casino)>;
    void setCasinoHooks(CasinoGetter getter, CasinoSwitcher switcher, CasinoLabel label = {})
    {
        m_casinoGetter = std::move(getter);
        m_casinoSwitcher = std::move(switcher);
        m_casinoLabel = std::move(label);
    }

    const SessionStats &stats() const { return m_stats; }

    bool isRunning() const { return m_running; }

    // Casino menu while an auto-bet (rules, Condition Builder or script) runs: switches the
    // site the following bets go to, the same way a script's `casino = "..."` does (session
    // kept, the next bet waits until the new connection is ready). A bet in flight finishes
    // on the old casino first. A script's `casino` / `currency` variables follow the switch.
    void switchCasino(const QString &casino);

    // Game menu while betting runs: switch the game for the following bets. `betCfg`
    // carries the new game plus the Manual Bet tab's target / side / limbo target, which
    // replace the running ones (rules / Condition Builder runs). Script runs: the script's
    // `game` variable is set and its next dobet() decides target / chance for the new game.
    // A bet already in flight finishes with the old game.
    void switchGame(const BotConfig &betCfg);

    // The Auto Bet tab was edited while the auto-bet runs (or its start is pending): base bet,
    // bet count, on win / on loss, stop on profit / loss, multiplier / win chance / direction,
    // "Use Condition Builder" and the blocks apply from the next bet on. The odds only replace
    // the running ones when they were edited (a condition may have changed the chance). Script
    // runs are not touched.
    void updateAutoSettings(const BotConfig &cfg, bool condMode, const BlockList &blocks);
    bool isPaused() const { return m_paused; }

    // Runs a single bet immediately (manual "Roll Dice"); no session needed.
    // In live mode the bet is asynchronous: the returned record is empty and the
    // result arrives through betPlaced().
    BetRecord rollOnce(const BotConfig &cfg);

    // Manual hilo / blackjack (Manual Bet tab buttons): a manual bet of these games is not
    // played through automatically - it stays open and each move is sent with manualGameMove():
    // hilo "high", "low", "equal", "skip", "cashout"; blackjack "hit", "stand", "double",
    // "split", "insurance", "noInsurance". manualGameUpdated() reports every state.
    bool manualGameOpen() const { return m_manualGameOpen; }
    void manualGameMove(const QString &move);

    // Clears the session and restores the starting balance.
    void resetSession(double initialBalance)
    {
        m_stats.reset(initialBalance);
        m_runStartProfit = 0.0; // the run's profit is measured from the fresh session
        m_runStartBets = 0;
    }

    // Live seed rotation finished (success or failure). Releases the next bet that
    // a script's resetseed() was holding back; a failed rotation is retried a few times.
    void seedRotationFinished(bool ok = true);

    // ---- Live mode (real bets through a StakeClient) -----------------------
    // In live mode the server decides roll, payout and balance; the simulated
    // provider is not used.
    void setLiveClient(StakeClient *client);
    void setLiveEnabled(bool enabled);
    // Pause between bets (Options -> Game -> Delays between bet).
    void setBetDelayMs(int ms) { m_betDelayMs = ms < 0 ? 0 : ms; }
    int betDelayMs() const { return m_betDelayMs; }
    // Live: when the next bet is larger than the balance, wait (re-checking every 5 s) instead
    // of halting - e.g. until a deposit arrives.
    void setRetryNoBalance(bool on) { m_retryNoBalance = on; }
    bool retryNoBalance() const { return m_retryNoBalance; }
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
    // Manual hilo round: the bet as last reported; active = waiting for the next move.
    void manualGameUpdated(Game game, const QJsonObject &bet, bool active);
    // A hilo / blackjack round of an auto-bet / script run (only shown: the engine plays it).
    void autoGameUpdated(Game game, const QJsonObject &bet, bool active);

private slots:
    void placeNextBet();
    void onLiveFinished(const LiveBetResult &result);

private:
    BetRecord placeBet(const BotConfig &cfg, double betSize);
    BetRecord recordBet(const BotConfig &cfg, double betSize, double roll, bool win,
                        double mult, double payout, qint64 nonce,
                        const QString &betId = QString(), const QString &casino = QString(),
                        double casinoTarget = -1.0, const QString &targetText = QString(),
                        const QString &rollText = QString());
    // Stake games that take several requests (hilo, blackjack, video poker, blue samurai
    // bonus spins, an open mines game): sends the next move for the unfinished bet `r`
    // (asks the script's round() for hilo / blackjack). False + error when no move is possible.
    bool continueGame(const LiveBetResult &r, QString *error);
    bool sendGameStep(QString *error);
    // Fills the script's view of the last bet (shared by advanceProgression / finishScriptDrain).
    ScriptStrategy::BetInfo lastBetInfo(bool win) const;
    bool checkStopConditions(); // true if a stop condition fired (engine stopped)
    void scheduleNext();
    // "Auto-bet <reason> (N bets, P/L x)". When the stop happens while a bet is still in flight
    // the line is held back until that bet has been recorded, so it shows the final numbers.
    void logStop(const QString &reason);
    void flushStopLog();
    QString m_pendingStopReason;
    bool m_stopLogPending = false;
    // Same idea for "Paused after N bets (P/L x)": when Pause is pressed while a bet is in
    // flight, that bet is still recorded afterwards, so the line is held back until then.
    void logPause();
    void flushPauseLog();
    bool m_pauseLogPending = false;
    bool advanceProgression(bool win, const BotConfig &cfg); // true if a stop fired
    void stopWithReason(const QString &reason);
    // After Stop with a scripted bet still in flight: hand that bet's result to the
    // script one last time (so its print()s show the latest roll), then close it.
    void finishScriptDrain(bool feedLastBet);
    void applyPendingCasino(); // the Casino menu's switch (see switchCasino)
    QString m_pendingCasino;
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
    CasinoLabel m_casinoLabel;
    QString casinoLabel(const QString &casino) const; // display name for log messages
    QString currentCasinoLabel() const;
    CurrencyGetter m_currencyGetter;
    CurrencySwitcher m_currencySwitcher;
    bool m_scriptDraining = false; // stopped, waiting for the last in-flight scripted bet
    BetRecord m_lastRec;           // last finished bet (fed to the script)

    BotConfig m_config;
    int m_betDelayMs = 0;
    bool m_retryNoBalance = false;
    bool m_noBalanceLogged = false;
    BotConfig m_runCfg;           // config of the running auto-bet (conditions may change target/side)
    Game m_announcedGame = Game::Dice; // last game reported through gameChanged()
    BlockList m_condBlocks;
    bool m_condMode = false;
    ConditionRunner m_conditions;
    double m_runStartProfit = 0.0;
    qint64 m_runStartBets = 0;    // session bet count when the run started ("Number of bets")
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
    // Multi-step game in flight (see continueGame()).
    int m_gameSteps = 0;          // moves sent for the current bet
    int m_gameFallback = 0;       // blackjack: 1 = retry with noInsurance, 2 = with stand
    QJsonObject m_gameBet;        // the open bet as last reported
    QJsonValue m_gameChoice;      // what round() returned for it
    double m_gameAmount = 0.0;
    QJsonObject m_lastBetJson;    // the last settled bet (the scripts' `bet` variable)
    QString m_inFlightCasino; // casino the bet in flight was sent to (its row shows this one)
    // Manual hilo round waiting for a button (see manualGameMove()).
    bool m_manualGameOpen = false;
    Game m_manualGame = Game::Hilo;
    BotConfig m_manualGameCfg;
    QJsonObject m_manualGameBet;
    double m_manualGameAmount = 0.0;
    QString m_manualGameCasino;
    void closeManualGame(QJsonObject bet); // by value: may be m_manualGameBet itself
    int m_retryStreak = 0;

    // resetseed() in live mode: the next bet waits until the server answered the
    // rotation (with a safety timeout so a lost answer cannot freeze the run).
    bool m_seedRotationPending = false;
    bool m_seedRotationQueued = false;   // waiting for the bet in flight to finish
    int m_seedRotationRetries = 0;
    QString m_pendingSeed;
    QTimer m_seedWaitTimer;
};
