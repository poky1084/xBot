#include "bot/BotEngine.h"

#include "bot/StakeClient.h"

#include <QRandomGenerator>

#include <algorithm>
#include <cmath>

namespace
{
constexpr int kMaxRetryStreak = 999999;
constexpr int kSeedRotationTimeoutMs = 15000;
constexpr int kConnectGraceMs = 60000;   // after (re)configuring the connection
constexpr int kConnectWaitMs = 40000;    // how long Start / Roll wait for it
constexpr int kConnectPollMs = 300;
constexpr int kSeedRotationRetries = 3;
constexpr int kSeedRotationRetryDelayMs = 500;
constexpr int kAutoClientSeedLength = 10;
} // namespace

BotEngine::BotEngine(QObject *parent)
    : QObject(parent)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &BotEngine::placeNextBet);

    m_seedWaitTimer.setSingleShot(true);
    connect(&m_seedWaitTimer, &QTimer::timeout, this, [this] {
        if (!m_seedRotationPending)
            return;
        emit logMessage(tr("No answer to the seed rotation - continuing."), true);
        seedRotationFinished(true); // no retry after a timeout
    });
}

// ------------------------------------------------------------ live plumbing

void BotEngine::setLiveClient(StakeClient *client)
{
    if (m_live)
        disconnect(m_live, nullptr, this, nullptr);
    m_live = client;
    if (m_live)
        connect(m_live, &StakeClient::diceFinished, this, &BotEngine::onLiveFinished);
}

void BotEngine::setLiveEnabled(bool enabled)
{
    if (m_liveEnabled == enabled)
        return;
    m_liveEnabled = enabled;
    m_liveBalanceKnown = false; // balance must be re-fetched from the server
}

void BotEngine::syncBalance(double balance)
{
    if (!liveMode())
        return;
    if (m_stats.bets == 0)
        m_stats.reset(balance);
    else
        m_stats.balance = balance;
    m_liveBalanceKnown = true;
}

// True while the connection is still coming up, so a failed readiness check is only temporary:
// the hidden browser page is loading, or the transport is ready and the first balance has not
// arrived yet. A permanent problem (empty token, extension offline) is not waited for.
bool BotEngine::connectionPending(const QString &whyNot) const
{
    if (!m_connectGraceActive || m_connectGrace.elapsed() > kConnectGraceMs)
        return false;
    if (whyNot.contains(QLatin1String("loading"), Qt::CaseInsensitive))
        return true;
    return m_live && m_live->transport() && m_live->transport()->isReady() && !m_liveBalanceKnown;
}

bool BotEngine::liveReady(QString *whyNot) const
{
    QString why;
    if (!m_live || !m_live->transport()) {
        why = tr("No live connection configured (Options → Live betting).");
    } else {
        QString transportWhy;
        if (!m_live->transport()->isReady(&transportWhy))
            why = transportWhy.isEmpty() ? tr("Live connection is not ready.") : transportWhy;
        else if (!m_liveBalanceKnown)
            why = tr("Not logged in yet — use \"Log in\" in Options → Live betting.");
    }
    if (whyNot)
        *whyNot = why;
    return why.isEmpty();
}

void BotEngine::requestLiveBet(const BotConfig &cfg, double betSize, bool manual)
{
    m_inFlight = true;
    m_inFlightManual = manual;
    m_inFlightCfg = cfg;

    if (cfg.game == Game::Limbo) {
        LimboBetRequest req;
        req.amount = betSize;
        req.multiplierTarget = cfg.multiplierTarget;
        m_live->placeLimbo(req);
        return;
    }

    DiceBetRequest req;
    req.amount = betSize;
    req.target = cfg.target;
    req.over = cfg.side == BetSide::Over;
    m_live->placeDice(req);
}

void BotEngine::onLiveFinished(const LiveBetResult &r)
{
    if (!m_inFlight)
        return; // not a bet this engine asked for

    m_inFlight = false;
    // resetseed() asked for a rotation while this bet was in flight: send it now.
    if (m_seedRotationQueued && m_seedRotationPending)
        dispatchSeedRotation();
    const bool manual = m_inFlightManual;
    const BotConfig cfg = m_inFlightCfg;

    if (!r.ok) {
        emit logMessage(tr("Bet failed: %1").arg(r.error), true);
        if (manual || !m_running) {
            finishScriptDrain(false);
            return;
        }

        if (r.retryable && ++m_retryStreak <= kMaxRetryStreak) {
            // The server refused this bet without placing it (rate limit,
            // parallel bet, seed rotation) -> same bet again after a short backoff.
            emit logMessage(tr("Retrying (%1/%2)…").arg(m_retryStreak).arg(kMaxRetryStreak), false);
            if (!m_paused) {
                const int wait = r.retryDelayMs > 0 ? std::min(500, r.retryDelayMs * m_retryStreak)
                                                    : std::min(2000, 250 * m_retryStreak);
                m_timer.start(wait);
            }
            return;
        }
        if (r.outcomeUnknown) {
            stopWithReason(tr("halted: no confirmation from the server — the last bet may have "
                              "gone through, check your bet history and balance"));
        } else {
            stopWithReason(tr("halted (%1)").arg(r.error));
        }
        return;
    }

    m_retryStreak = 0;
    recordBet(cfg, r.amount, r.roll, r.win, r.multiplier, r.payout, r.nonce, r.betId);

    // A manual roll has no progression/stop logic. If the user pressed Stop
    // while the request was in flight the bet is still recorded (the money has
    // moved) but nothing further is scheduled.
    if (manual || !m_running) {
        finishScriptDrain(!manual);
        return;
    }

    if (advanceProgression(r.win, cfg))
        return;
    if (checkStopConditions())
        return;
    if (!m_paused)
        scheduleNext();
}

void BotEngine::finishScriptDrain(bool feedLastBet)
{
    if (!m_scriptDraining)
        return;
    m_scriptDraining = false;

    if (feedLastBet && m_script->isLoaded()) {
        ScriptStrategy::BetInfo info;
        info.win = m_lastRec.win;
        info.bet = m_lastRec.bet;
        info.profit = m_lastRec.profit;
        info.payout = m_lastRec.payout;
        info.roll = m_lastRec.roll;
        info.balance = m_stats.balance;
        info.runProfit = m_stats.profit - m_runStartProfit;

        double ignoredBet = 0.0;
        BotConfig ignoredCfg = m_runCfg;
        if (m_script->onBet(info, ignoredBet, ignoredCfg) == ScriptStrategy::Status::Error) {
            const QString err = m_script->lastError();
            emit scriptOutput(err, true);
            emit scriptError(err);
        }
    }
    m_scriptActive = false;
    m_script->close();
}

// ----------------------------------------------------------------- manual

BetRecord BotEngine::rollOnce(const BotConfig &cfg)
{
    if (liveMode()) {
        if (m_inFlight)
            return {};
        QString why;
        if (m_rollPending && !m_rollRetrying)
            return {}; // a roll is already waiting for the connection
        if (!liveReady(&why)) {
            if (connectionPending(why)
                && (!m_rollPending || m_rollClock.elapsed() < kConnectWaitMs)) {
                if (!m_rollPending) {
                    m_rollPending = true;
                    m_rollClock.start();
                    emit logMessage(tr("Connecting to the casino… the bet is placed as soon as it is ready."),
                                    false);
                }
                QTimer::singleShot(kConnectPollMs, this, [this, cfg] {
                    if (!m_rollPending)
                        return;
                    m_rollRetrying = true;
                    rollOnce(cfg);
                    m_rollRetrying = false;
                });
                return {};
            }
            m_rollPending = false;
            emit logMessage(why, true);
            return {};
        }
        m_rollPending = false;
        if (cfg.baseBet < 0.0 || cfg.baseBet > m_stats.balance) {
            emit logMessage(tr("Bet %1 exceeds the available balance %2.")
                                .arg(cfg.baseBet, 0, 'f', 8)
                                .arg(m_stats.balance, 0, 'f', 8),
                            true);
            return {};
        }
        requestLiveBet(cfg, cfg.baseBet, true);
        return {}; // result arrives via betPlaced()
    }

    if (!m_provider) {
        emit logMessage(tr("No dice provider configured."), true);
        return {};
    }

    if (m_stats.bets == 0)
        m_stats.reset(cfg.startBalance);

    const double bet = std::clamp(cfg.baseBet, 0.0, std::max(0.0, m_stats.balance));
    BetRecord rec = placeBet(cfg, bet);
    return rec;
}

// ------------------------------------------------------------------- auto

void BotEngine::start()
{
    if (m_running)
        return;
    if (m_startPending && !m_startRetrying)
        return; // already waiting for the connection

    if (liveMode()) {
        if (m_inFlight) {
            emit logMessage(tr("Wait for the current bet to finish."), true);
            return;
        }
        QString why;
        if (!liveReady(&why)) {
            // The connection is still logging in (e.g. right after launch): begin as soon as it
            // is ready instead of failing.
            if (connectionPending(why)
                && (!m_startPending || m_startClock.elapsed() < kConnectWaitMs)) {
                if (!m_startPending) {
                    m_startPending = true;
                    m_startClock.start();
                    emit logMessage(tr("Connecting to the casino… betting starts as soon as it is ready."),
                                    false);
                }
                QTimer::singleShot(kConnectPollMs, this, [this] {
                    if (!m_startPending)
                        return;
                    m_startRetrying = true;
                    start();
                    m_startRetrying = false;
                });
                return;
            }
            m_startPending = false;
            emit logMessage(why, true);
            emit stopped(why);
            return;
        }
        m_startPending = false;
        m_retryStreak = 0;
    } else {
        if (!m_provider) {
            emit logMessage(tr("No dice provider configured."), true);
            return;
        }

        QString err;
        if (!m_provider->open(&err)) {
            emit logMessage(err, true);
            emit stopped(err);
            return;
        }

        if (m_stats.bets == 0)
            m_stats.reset(m_config.startBalance);
    }

    // Script mode (Lua / JavaScript) is self-contained: nothing from the Manual / Auto Bet tabs is used
    // (amount, chance, side, on-win/on-loss rules, stop limits, delay, condition
    // blocks). Only the start balance (an Options setting) is kept.
    const bool scriptMode = !m_scriptSource.trimmed().isEmpty();
    if (scriptMode) {
        BotConfig clean;
        clean.startBalance = m_config.startBalance;
        clean.game = m_config.game; // the script may switch it with game = "dice" / "limbo"
        m_config = clean;
    }

    m_seedRotationPending = false;
    m_seedRotationQueued = false;
    m_seedWaitTimer.stop();
    m_nextBetSize = m_config.baseBet;
    m_runCfg = m_config;
    m_awaitTransport = false;
    m_runCfg.casino = m_casinoGetter ? m_casinoGetter().trimmed().toLower() : QString();
    m_runCfg.currency = currentCurrency(); // what a script sees in `currency` at start-up
    m_announcedGame = m_config.game;
    m_runStartProfit = m_stats.profit;
    m_conditions.setBlocks(scriptMode ? BlockList() : m_condBlocks);
    m_conditions.reset(m_config);

    m_scriptActive = false;
    if (scriptMode) {
        m_script = m_scriptLanguage == ScriptLanguage::JavaScript
            ? static_cast<ScriptStrategy *>(&m_jsStrategy)
            : static_cast<ScriptStrategy *>(&m_luaStrategy);
        const QString lang = scriptLanguageName(m_scriptLanguage);
        m_script->setLogger([this](const QString &msg, bool isError) {
            emit scriptOutput(msg, isError);
        });
        m_script->setResetStatsHandler([this] { return resetStatsFromScript(); });
        m_script->setResetSeedHandler([this](const QString &seed) { rotateSeedFromScript(seed); });
        m_script->setVaultHandler([this](double amount) { vaultFromScript(amount); });
        const auto st = m_script->init(m_scriptSource, m_stats.balance, m_nextBetSize, m_runCfg);
        if (st == ScriptStrategy::Status::Error) {
            const QString err = m_script->lastError();
            emit scriptOutput(err, true);
            emit scriptError(err);
            emit stopped(tr("%1 error").arg(lang));
            return;
        }
        if (st == ScriptStrategy::Status::Stop) {
            emit logMessage(tr("[%1] script stopped during start-up").arg(lang), false);
            emit stopped(tr("stopped by %1 script").arg(lang));
            m_script->close();
            return;
        }
        // The script may have chosen another casino / currency in its body.
        if (applyScriptCasino() || applyScriptCurrency()) {
            emit stopped(tr("%1 error").arg(lang)); // see applyScriptCurrency(): already reported
            return;
        }
        m_scriptActive = true;
        emit scriptError(QString()); // clears the editor's status line
        if (m_runCfg.game == Game::Limbo)
            emit logMessage(tr("%1 strategy active (limbo, first bet %2, target %3x).")
                                .arg(lang)
                                .arg(m_nextBetSize, 0, 'f', 8)
                                .arg(m_runCfg.multiplierTarget, 0, 'f', 2),
                            false);
        else
            emit logMessage(tr("%1 strategy active (dice, first bet %2, chance %3%, %4).")
                                .arg(lang)
                                .arg(m_nextBetSize, 0, 'f', 8)
                                .arg(m_runCfg.chance(), 0, 'f', 2)
                                .arg(m_runCfg.side == BetSide::Over ? tr("over") : tr("under")),
                            false);
        //notifyGameChange();
    } else if (m_conditions.active())
        emit logMessage(tr("Condition builder active: %1 block(s) replace the on-win/on-loss rules.")
                            .arg(m_conditions.blockCount()),
                        false);
    m_running = true;
    m_paused = false;

    emit logMessage(tr("Auto-bet started."), false);
    emit started();
    emit pausedChanged(false);
    scheduleNext();
}

// resetstats() from a script: the statistics start over, the balance stays (so a
// strategy behaves the same in simulation and live mode). Returns that balance.
double BotEngine::resetStatsFromScript()
{
    m_stats.reset(m_stats.balance);
    m_runStartProfit = 0.0;
    emit statsReset();
    return m_stats.balance;
}

// resetseed([clientSeed]) from a script. Without a seed a random 10-character
// alphanumeric one is used. Simulation: instant. Live: the rotation is an asynchronous
// request, so
//   * it waits for a bet that is still in flight (the server refuses to rotate the seed
//     while a bet is being processed) and is sent as soon as that bet is done, and
//   * the next bet is held back until the server answered, so no bet is placed on
//     the old seed pair or collides with the rotation.
void BotEngine::rotateSeedFromScript(const QString &clientSeed)
{
    QString seed = clientSeed.trimmed();
    if (seed.isEmpty())
        seed = StakeClient::randomClientSeed(kAutoClientSeedLength);

    if (!liveMode()) {
        emit seedRotationRequested(seed);
        return;
    }

    m_pendingSeed = seed;           // a newer request replaces one that is still waiting
    m_seedRotationPending = true;
    m_seedRotationRetries = 0;
    if (m_inFlight) {
        m_seedRotationQueued = true;
        emit logMessage(tr("Seed rotation waits for the bet in flight."), false);
        return;
    }
    dispatchSeedRotation();
}

// vault(amount) from a script: only a live Shuffle account has a vault; MainWindow
// sends the request (fire and forget - bets are not held back for it).
void BotEngine::vaultFromScript(double amount)
{
    if (!(amount > 0.0)) {
        emit scriptOutput(tr("vault(): the amount must be greater than 0."), true);
        return;
    }
    if (!liveMode()) {
        emit scriptOutput(tr("vault() only works in live mode."), true);
        return;
    }
    emit vaultRequested(amount);
}

void BotEngine::dispatchSeedRotation()
{
    m_seedRotationQueued = false;
    m_seedWaitTimer.start(kSeedRotationTimeoutMs);
    emit seedRotationRequested(m_pendingSeed);
}

void BotEngine::seedRotationFinished(bool ok)
{
    if (!m_seedRotationPending || m_seedRotationQueued)
        return; // not ours (e.g. the toolbar button) or not sent yet

    if (!ok && m_running && m_seedRotationRetries < kSeedRotationRetries) {
        ++m_seedRotationRetries;
        emit logMessage(tr("Seed rotation failed - retrying (%1/%2)…")
                            .arg(m_seedRotationRetries)
                            .arg(kSeedRotationRetries),
                        false);
        m_seedWaitTimer.start(kSeedRotationTimeoutMs);
        QTimer::singleShot(kSeedRotationRetryDelayMs, this, [this] {
            if (m_seedRotationPending && !m_seedRotationQueued && !m_inFlight)
                emit seedRotationRequested(m_pendingSeed);
        });
        return;
    }

    m_seedRotationPending = false;
    m_seedWaitTimer.stop();
    // The held-back bet is placed now (placeNextBet() returned early while waiting).
    if (m_running && !m_paused && !m_inFlight)
        scheduleNext();
}

QString BotEngine::currentCurrency() const
{
    return m_currencyGetter ? m_currencyGetter().trimmed().toLower() : QString();
}

// A script set `casino`: switch the site the bets go to. The session (stats, history,
// balance graph) is deliberately kept. Returns true when that was not possible; the run
// is then halted / not started.
bool BotEngine::applyScriptCasino()
{
    const QString have = m_casinoGetter ? m_casinoGetter().trimmed().toLower() : QString();
    const QString want = m_runCfg.casino.trimmed().toLower();
    if (want.isEmpty() || want == have || !m_casinoSwitcher) {
        m_runCfg.casino = have;
        return false;
    }

    QString err;
    if (!m_casinoSwitcher(want, err)) {
        const QString msg = tr("cannot switch casino to %1: %2").arg(want, err);
        emit scriptOutput(msg, true);
        emit scriptError(msg);
        if (m_running) {
            stopWithReason(tr("halted (%1)").arg(msg));
        } else {
            m_scriptActive = false;
            m_script->close();
        }
        return true;
    }

    m_runCfg.casino = want;
    // The new casino's connection (e.g. the hidden Stake browser) needs a moment: hold the
    // next bet back until it is ready instead of failing and retrying.
    m_awaitTransport = true;
    m_awaitTimer.start();
    // The new casino brings its own bet currency; do not try to "switch back" to the old one.
    m_runCfg.currency = currentCurrency();
    emit logMessage(tr("Casino switched to %1.").arg(want), false);
    return false;
}

// A script set `currency`: switch the betting currency (before the next bet is placed).
// Returns true when that was not possible; the run is then halted / not started.
bool BotEngine::applyScriptCurrency()
{
    const QString have = currentCurrency();
    const QString want = m_runCfg.currency.trimmed().toLower();
    if (want.isEmpty() || want == have || !m_currencySwitcher) {
        m_runCfg.currency = have;
        return false;
    }

    double balance = m_stats.balance;
    QString err;
    if (!m_currencySwitcher(want, balance, err)) {
        const QString msg = tr("cannot switch currency to %1: %2").arg(want.toUpper(), err);
        emit scriptOutput(msg, true);
        emit scriptError(msg);
        if (m_running) {
            stopWithReason(tr("halted (%1)").arg(msg));
        } else {
            m_scriptActive = false;
            m_script->close();
        }
        return true;
    }

    m_runCfg.currency = want;
    emit logMessage(tr("Currency switched to %1.").arg(want.toUpper()), false);
    if (liveMode()) {
        // The session figures were in the old currency: start a fresh session in the new
        // one. This also republishes balance / startbalance / profit / ... to the script.
        m_stats.balance = balance;
        m_script->requestResetStats();
    }
    return false;
}

void BotEngine::notifyGameChange()
{
    if (m_runCfg.game == m_announcedGame)
        return;
    m_announcedGame = m_runCfg.game;
    emit logMessage(tr("Game switched to %1.").arg(gameName(m_announcedGame)), false);
    emit gameChanged(m_announcedGame);
}

void BotEngine::stop(const QString &reason)
{
    if (m_startPending) { // Start was waiting for the connection: cancel it
        m_startPending = false;
        emit logMessage(tr("Start cancelled."), false);
        emit stopped(reason.isEmpty() ? tr("stopped by user") : reason);
        return;
    }
    if (!m_running)
        return;
    stopWithReason(reason.isEmpty() ? tr("stopped by user") : reason);
}

void BotEngine::pause()
{
    if (!m_running || m_paused)
        return;
    m_paused = true;
    m_timer.stop();
    emit logMessage(tr("Paused after %1 bets (P/L %2)")
                        .arg(m_stats.bets)
                        .arg(m_stats.profit, 0, 'f', 8),
                    false);
    emit pausedChanged(true);
}

void BotEngine::resume()
{
    if (!m_running || !m_paused)
        return;
    m_paused = false;
    emit logMessage(tr("Resumed"), false);
    emit pausedChanged(false);
    if (!m_inFlight) // otherwise the in-flight result schedules the next bet
        scheduleNext();
}

void BotEngine::stopWithReason(const QString &reason)
{
    // A live scripted bet that is still in flight finishes after this; keep the script
    // loaded until that last result has been handed to it (see finishScriptDrain).
    if (m_scriptActive && m_inFlight && !m_inFlightManual) {
        m_scriptDraining = true;
    } else {
        m_scriptActive = false;
        m_script->close();
    }
    m_running = false;
    m_paused = false;
    m_timer.stop();
    m_seedRotationPending = false;
    m_seedRotationQueued = false;
    m_seedWaitTimer.stop();
    emit logMessage(tr("Auto-bet %1 (%2 bets, P/L %3)")
                        .arg(reason)
                        .arg(m_stats.bets)
                        .arg(m_stats.profit, 0, 'f', 8),
                    false);
    emit stopped(reason);
}

void BotEngine::scheduleNext()
{
    m_timer.start(std::max({0, m_config.delayMs, m_betDelayMs}));
}

// ------------------------------------------------------------------- core

BetRecord BotEngine::recordBet(const BotConfig &cfg, double betSize, double roll, bool win,
                               double mult, double payout, qint64 nonce,
                               const QString &betId)
{
    BetRecord rec;
    rec.nonce = nonce;
    rec.betId = betId;
    if (m_casinoGetter)
        rec.casino = m_casinoGetter().trimmed().toLower(); // casino selected when the bet was made

    const double profit = payout - betSize;

    m_stats.balance += profit;
    m_stats.profit += profit;
    m_stats.wagered += betSize;
    ++m_stats.bets;
    if (win) {
        ++m_stats.wins;
        if (mult >= 10.0)
            ++m_stats.bigWins;
    } else {
        ++m_stats.losses;
    }
    m_stats.trackBet(betSize, win);

    rec.index = m_stats.bets;
    rec.time = QDateTime::currentDateTime();
    rec.game = cfg.game;
    rec.bet = betSize;
    rec.target = cfg.game == Game::Limbo ? cfg.multiplierTarget : cfg.target;
    rec.side = cfg.side;
    rec.roll = truncate2(roll); // 2 decimals, cut not rounded (history, UI and scripts)
    rec.mult = mult;
    rec.win = win;
    rec.payout = payout;
    rec.profit = profit;
    rec.balance = m_stats.balance;

    m_lastRec = rec;
    emit betPlaced(rec);
    return rec;
}

BetRecord BotEngine::placeBet(const BotConfig &cfg, double betSize)
{
    const qint64 nonce = m_provider->currentNonce();

    double roll = 0.0;
    QString err;
    const bool limbo = cfg.game == Game::Limbo;
    const bool got = limbo ? m_provider->nextLimbo(&roll, &err) : m_provider->nextRoll(&roll, &err);
    if (!got) {
        emit logMessage(err, true);
        if (m_running)
            stopWithReason(err);
        BetRecord rec;
        rec.nonce = nonce;
        return rec;
    }

    const bool isUnder = cfg.side == BetSide::Under;
    const bool win = limbo ? (roll >= cfg.multiplierTarget)
                           : (isUnder ? (roll < cfg.target) : (roll > cfg.target));
    const double mult = cfg.multiplier();
    const double payout = win ? betSize * mult : 0.0;
    return recordBet(cfg, betSize, roll, win, mult, payout, nonce);
}

bool BotEngine::advanceProgression(bool win, const BotConfig &cfg)
{
    if (m_scriptActive) {
        ScriptStrategy::BetInfo info;
        info.win = win;
        info.bet = m_lastRec.bet;
        info.profit = m_lastRec.profit;
        info.payout = m_lastRec.payout;
        info.roll = m_lastRec.roll;
        info.balance = m_stats.balance;
        info.runProfit = m_stats.profit - m_runStartProfit;

        const auto st = m_script->onBet(info, m_nextBetSize, m_runCfg);
        if (st == ScriptStrategy::Status::Error) {
            const QString err = m_script->lastError();
            emit scriptOutput(err, true);
            emit scriptError(err);
            stopWithReason(tr("halted (%1 error)").arg(scriptLanguageName(m_scriptLanguage)));
            return true;
        }
        if (st == ScriptStrategy::Status::Stop) {
            const QString why = m_script->stopReason();
            const QString lang = scriptLanguageName(m_scriptLanguage);
            stopWithReason(why.isEmpty() ? tr("stopped by %1 script").arg(lang)
                                         : tr("stopped by %1 script: %2").arg(lang, why));
            return true;
        }
        // dobet() may have set `casino` / `currency`: switch before the next bet is placed.
        if (applyScriptCasino() || applyScriptCurrency())
            return true;
        //notifyGameChange();
        return false;
    }
    if (m_conditions.active()) {
        const bool stop = m_conditions.onBetResult(win, m_stats.profit - m_runStartProfit,
                                                   m_stats.balance, m_nextBetSize, m_runCfg);
        if (stop) {
            stopWithReason(tr("stopped by condition"));
            return true;
        }
        return false;
    }
    m_nextBetSize = (win ? cfg.onWin : cfg.onLoss).nextBet(cfg.baseBet, m_nextBetSize);
    return false;
}

bool BotEngine::checkStopConditions()
{
    const BotConfig &cfg = m_config;
    if (cfg.stopProfit > 0.0 && m_stats.profit >= cfg.stopProfit) {
        stopWithReason(tr("hit stop-on-profit (+%1)").arg(m_stats.profit, 0, 'f', 8));
        return true;
    }
    if (cfg.stopLoss > 0.0 && -m_stats.profit >= cfg.stopLoss) {
        stopWithReason(tr("hit stop-on-loss (%1)").arg(m_stats.profit, 0, 'f', 8));
        return true;
    }
    if (cfg.stopAfterBets > 0 && m_stats.bets >= cfg.stopAfterBets) {
        stopWithReason(tr("finished %1 bets").arg(m_stats.bets));
        return true;
    }
    return false;
}

void BotEngine::placeNextBet()
{
    if (!m_running || m_paused)
        return;
    if (m_seedRotationPending)
        return; // resetseed() is waiting for the server; seedRotationFinished() resumes

    if (liveMode()) {
        if (m_inFlight)
            return;
        if (m_awaitTransport) {
            QString why;
            if (m_live && m_live->transport() && m_live->transport()->isReady(&why)) {
                m_awaitTransport = false;
            } else if (m_awaitTimer.elapsed() > 60000) {
                m_awaitTransport = false;
                stopWithReason(tr("halted (casino connection not ready: %1)").arg(why));
                return;
            } else {
                m_timer.start(300); // check again shortly
                return;
            }
        }
        // Real money: never silently shrink a bet to "whatever is left". If the
        // progression asks for more than the balance, stop instead.
        const double bet = m_nextBetSize;
        if (bet < 0.0 || bet > m_stats.balance) {
            stopWithReason(tr("halted (next bet %1 exceeds balance %2)")
                               .arg(bet, 0, 'f', 8)
                               .arg(m_stats.balance, 0, 'f', 8));
            return;
        }
        requestLiveBet(m_runCfg, bet, false);
        return;
    }

    const BotConfig &cfg = m_runCfg;

    // Bet exactly the amount the strategy asked for (0 is a valid bet). Never
    // shrink it to the remaining balance: if it can't be covered, halt instead.
    const double bet = m_nextBetSize;
    if (bet < 0.0 || bet > m_stats.balance) {
        stopWithReason(tr("halted (next bet %1 exceeds balance %2)")
                           .arg(bet, 0, 'f', 8)
                           .arg(m_stats.balance, 0, 'f', 8));
        return;
    }

    const BetRecord rec = placeBet(cfg, bet);
    if (advanceProgression(rec.win, cfg))
        return;

    if (checkStopConditions())
        return;

    scheduleNext();
}
