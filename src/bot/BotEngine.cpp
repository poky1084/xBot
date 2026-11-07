#include "bot/BotEngine.h"

#include "bot/SimulatedProvider.h"
#include "bot/StakeClient.h"
#include "bot/StakeFair.h"
#include "bot/StakeGames.h"

#include <QJsonArray>
#include <QRandomGenerator>
#include <QRegularExpression>

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
constexpr int kMaxGameSteps = 200; // moves of one hilo / blackjack / ... bet before giving up

// The amount actually bet: 8 decimals, cut (not rounded up). The tiny epsilon keeps
// values such as 0.000000029999999 from dropping a satoshi.
double betAmount(double v)
{
    return std::floor(v * 1e8 + 1e-6) / 1e8;
}
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
        emit logMessage(tr("%1: no answer to the seed rotation - continuing.").arg(currentCasinoLabel()), true);
        seedRotationFinished(true); // no retry after a timeout
    });
}

// ------------------------------------------------------------ live plumbing

void BotEngine::setProvider(IDiceProvider *provider)
{
    m_provider = provider;
    if (auto *sim = dynamic_cast<SimulatedProvider *>(provider)) {
        // Stake's other games in simulation: the same requests as live, answered from the
        // simulation's seed pair.
        if (!m_simClient) {
            m_simClient = new StakeClient(this);
            connect(m_simClient, &StakeClient::diceFinished, this, &BotEngine::onLiveFinished);
        }
        delete m_simTransport;
        m_simTransport = new SimulatedStakeTransport(sim, this);
        m_simClient->setTransport(m_simTransport);
        m_simClient->setCurrency(QStringLiteral("sim"));
    }
}

// Code mode in simulation: the seed pair the script starts with (its serverSeed / clientSeed /
// nonce). The "Verify a roll" seeds when "Use these seeds for simulation" is ticked, otherwise the
// latest revealed pair from nonce 1, otherwise the active pair as it is (see setScriptSeedSource()).
void BotEngine::prepareScriptSeeds()
{
    m_runCfg.serverSeed.clear();
    m_runCfg.clientSeed.clear();
    m_runCfg.nonce = 0;
    auto *sim = dynamic_cast<SimulatedProvider *>(m_provider);
    if (liveMode() || !sim)
        return;
    QString server, client, from;
    qint64 nonce = 1;
    if (m_scriptSeedSource && m_scriptSeedSource(server, client, nonce, from) && !server.isEmpty()
        && !client.isEmpty()) {
        sim->setServerSeed(server);
        sim->setClientSeed(client);
        sim->setNonce(std::max<qint64>(1, nonce));
        emit simulationSeedsChanged();
    }
    if (from.isEmpty())
        from = tr("the active seed pair");
    emit logMessage(tr("Script seeds: %1 (nonce %2).").arg(from).arg(sim->nonce()), false);
    syncScriptSeeds();
}

void BotEngine::syncScriptSeeds()
{
    auto *sim = dynamic_cast<SimulatedProvider *>(m_provider);
    if (liveMode() || !sim)
        return;
    m_runCfg.serverSeed = sim->serverSeed();
    m_runCfg.clientSeed = sim->clientSeed();
    m_runCfg.nonce = sim->nonce();
}

// The script set serverSeed / clientSeed / nonce: the simulation bets with them from now on.
void BotEngine::applyScriptSeeds()
{
    auto *sim = dynamic_cast<SimulatedProvider *>(m_provider);
    if (liveMode() || !sim || m_runCfg.serverSeed.isEmpty())
        return;
    bool changed = false;
    if (m_runCfg.serverSeed != sim->serverSeed()) {
        sim->setServerSeed(m_runCfg.serverSeed);
        changed = true;
    }
    if (m_runCfg.clientSeed != sim->clientSeed()) {
        sim->setClientSeed(m_runCfg.clientSeed);
        changed = true;
    }
    if (m_runCfg.nonce >= 1 && m_runCfg.nonce != sim->nonce()) {
        sim->setNonce(m_runCfg.nonce);
        changed = true;
    }
    if (changed)
        emit simulationSeedsChanged();
}

StakeClient *BotEngine::gameClient() const
{
    return liveMode() ? m_live : m_simClient;
}

void BotEngine::clearSimulatedGames()
{
    if (m_simTransport)
        m_simTransport->clearOpenGames();
}

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

// A Cloudflare / proxy gateway failure (HTTP 502 / 503 / 504 / 520-524 page) rather than an answer
// from the casino itself.
static bool isGatewayError(const QString &error)
{
    static const QRegularExpression re(QStringLiteral("HTTP (502|503|504|52[0-4])\\b|Bad Gateway|Gateway Time-?out|"
                                                      "Service Unavailable|cloudflare"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re.match(error).hasMatch();
}

void BotEngine::requestLiveBet(const BotConfig &cfg, double betSize, bool manual)
{
    m_inFlight = true;
    m_inFlightClock.start();
    m_inFlightManual = manual;
    m_inFlightCfg = cfg;
    // The row of this bet names the casino it went to, even if the casino is switched before
    // its result arrives (Casino menu / script).
    // Simulated bets have no casino (like simulated dice rolls).
    m_inFlightCasino = (liveMode() && m_casinoGetter) ? m_casinoGetter().trimmed().toLower() : QString();
    m_gameSteps = 0;
    m_gameFallback = 0;
    m_gameBet = QJsonObject();
    m_gameChoice = QJsonValue(QJsonValue::Undefined);
    {
        // Baccarat amounts / roulette chips with amounts fix the stake (the base bet is not used).
        bool fixed = false;
        const double stake = StakeGames::fixedStake(cfg.game, cfg.gameParams, &fixed);
        if (fixed)
            betSize = stake;
    }
    m_gameAmount = betSize;

    if (!isDiceOrLimbo(cfg.game)) {
        GameBetRequest req;
        req.game = cfg.game;
        req.amount = betSize;
        req.params = cfg.gameParams;
        gameClient()->placeGame(req);
        return;
    }

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

bool BotEngine::continueGame(const LiveBetResult &r, QString *error)
{
    const Game game = m_inFlightCfg.game;
    if (++m_gameSteps > kMaxGameSteps) {
        if (error)
            *error = tr("no result after %1 moves").arg(kMaxGameSteps);
        return false;
    }
    m_gameBet = r.bet;
    if (r.amount > 0.0)
        m_gameAmount = r.amount;
    m_gameFallback = 0;
    m_gameChoice = QJsonValue(QJsonValue::Undefined);
    // Hilo / blackjack: the script's round() decides the move (the reference bot's contract).
    // Without a script (or a round() function) the game's built-in play is used.
    if (StakeGames::usesRoundFunction(game) && m_scriptActive && m_script && m_script->isLoaded()) {
        QJsonValue choice;
        if (m_script->callRound(r.bet, choice) == ScriptStrategy::Status::Error) {
            const QString err = m_script->lastError();
            emit scriptOutput(err, true);
            emit scriptError(err);
        } else {
            m_gameChoice = choice;
        }
    }
    return sendGameStep(error);
}

bool BotEngine::sendGameStep(QString *error)
{
    const Game game = m_inFlightCfg.game;
    StakeGames::Action act;
    if (!StakeGames::nextAction(game, m_gameBet, m_inFlightCfg.gameParams, m_gameChoice,
                                m_gameFallback, &act, error))
        return false;
    if (m_inFlightManual || game == Game::VideoPoker)
        emit logMessage(act.description, false);
    gameClient()->sendGameAction(game, act.endpoint, act.body, m_inFlightCfg.gameParams, m_gameAmount);
    return true;
}

void BotEngine::onLiveFinished(const LiveBetResult &result)
{
    if (!m_inFlight)
        return; // not a bet this engine asked for
    if (m_loadingOpenGame) {
        m_loadingOpenGame = false;
        if (!(result.ok && result.active)) {
            m_inFlight = false; // nothing open: no error, nothing recorded
            // A round shown as open (finished elsewhere, or on the casino switched away from)
            // is closed: its buttons go off.
            if (m_manualGameOpen && m_manualGame == m_inFlightCfg.game)
                closeManualGame(QJsonObject());
            return;
        }
        // An open round: shown below like a manual bet waiting for its next move.
    }

    LiveBetResult r = result;
    if (!isDiceOrLimbo(m_inFlightCfg.game) && gameClient()) {
        const Game game = m_inFlightCfg.game;
        if (r.ok && r.active && m_inFlightManual && (game == Game::Hilo || game == Game::Blackjack)) {
            // Manual hilo / blackjack: wait for the Manual Bet tab's buttons instead of playing on.
            m_manualGameOpen = true;
            m_manualGame = game;
            m_manualGameCfg = m_inFlightCfg;
            m_manualGameBet = r.bet;
            m_manualGameAmount = r.amount > 0.0 ? r.amount : m_gameAmount;
            m_manualGameCasino = m_inFlightCasino;
            m_manualGameSim = !liveMode();
            m_inFlight = false;
            emit manualGameUpdated(game, r.bet, true);
            return;
        }
        if (r.ok && r.active) {
            // Not settled yet (hilo / blackjack / video poker / bonus spins): next move. The bet
            // stays "in flight" until the casino reports it finished.
            if ((game == Game::Hilo || game == Game::Blackjack) && !m_inFlightManual)
                emit autoGameUpdated(game, r.bet, true); // the Manual tab's card showcase
            QString err;
            if (continueGame(r, &err))
                return;
            r = LiveBetResult();
            r.ok = false;
            r.error = liveMode()
                ? tr("%1: %2 - the game is still open on Stake, finish it there or start again "
                     "to continue it").arg(gameDisplayName(game), err)
                : tr("%1: %2 - the simulated round is still open, start again to continue it")
                      .arg(gameDisplayName(game), err);
        } else if (!r.ok && m_gameSteps > 0) {
            if (game == Game::Blackjack && !m_inFlightManual && m_gameFallback < 2
                && (r.errorType.contains(QLatin1String("InvalidAction"), Qt::CaseInsensitive)
                    || r.error.contains(QLatin1String("invalid action"), Qt::CaseInsensitive))) {
                // The move was not allowed (e.g. insurance already decided): like the reference
                // bot, decline insurance, then stand.
                ++m_gameFallback;
                QString err;
                if (sendGameStep(&err))
                    return;
            } else if (r.errorType.contains(QLatin1String("insignificantBet"), Qt::CaseInsensitive)
                       && m_gameAmount <= 0.0) {
                // A 0 bet that cannot be cashed out / continued: record it as settled at 0.
                r = LiveBetResult();
                r.ok = true;
                r.bet = m_gameBet;
                r.amount = 0.0;
                r.payout = 0.0;
                r.multiplier = 0.0;
                r.betId = m_gameBet.value(QStringLiteral("id")).toString();
                const StakeGames::Summary sum = StakeGames::summarize(game, m_gameBet, m_inFlightCfg.gameParams);
                r.roll = sum.roll;
                r.targetText = sum.targetText;
                r.rollText = sum.rollText;
            }
        }
    }

    m_inFlight = false;
    // The Casino menu asked for a switch while this bet was in flight: do it now, before
    // the script / progression runs and the next bet is placed.
    if (m_running)
        applyPendingCasino();
    else
        m_pendingCasino.clear();
    // resetseed() asked for a rotation while this bet was in flight: send it now.
    if (m_seedRotationQueued && m_seedRotationPending)
        dispatchSeedRotation();
    const bool manual = m_inFlightManual;
    const BotConfig cfg = m_inFlightCfg;
    const QString betCasino = m_inFlightCasino; // read before any switch below can matter

    if (!r.ok) {
        // Shuffle behind Cloudflare: a 502 / 503 / 504 / 52x gateway page means the request did not
        // get an answer from Shuffle itself. Keep betting (the same bet again after the retry wait)
        // instead of halting, and log a short line instead of the whole HTML page.
        if (betCasino == QLatin1String("shuffle") && isGatewayError(r.error)) {
            const QRegularExpression code(QStringLiteral("HTTP (5\\d\\d)"));
            const QRegularExpressionMatch m = code.match(r.error);
            r.error = tr("Shuffle is not answering (HTTP %1, Cloudflare gateway error) - betting continues")
                          .arg(m.hasMatch() ? m.captured(1) : QStringLiteral("5xx"));
            r.outcomeUnknown = false;
            r.retryable = true;
        }
        // Paradice "slowdown_please" during an auto-bet is retried at once: not logged each time.
        if (!(r.retryNow && r.retryable && !manual && m_running))
            emit logMessage(tr("Bet failed: %1").arg(r.error), true);
        flushPauseLog(); // no bet was recorded: the numbers are final
        if (manual && m_manualGameOpen && cfg.game == m_manualGame) {
            // The round is gone on Stake (expired / finished elsewhere): close it here too;
            // otherwise it stays open for another move (e.g. cash out before any guess).
            if (r.errorType.contains(QLatin1String("notFound"), Qt::CaseInsensitive)
                || r.error.contains(QLatin1String("not found"), Qt::CaseInsensitive))
                closeManualGame(m_manualGameBet);
            else
                emit manualGameUpdated(m_manualGame, m_manualGameBet, true);
        }
        if (manual || !m_running) {
            flushStopLog();
            finishScriptDrain(false);
            return;
        }

        if (r.retryable && ++m_retryStreak <= kMaxRetryStreak) {
            // The server refused this bet without placing it (rate limit,
            // parallel bet, seed rotation) -> same bet again after a short backoff.
            if (r.retryNow) {
                // Paradice "slowdown_please": resend at once (the normal bet delay still
                // applies), without logging every refusal.
                if (!m_paused)
                    m_timer.start(std::max(0, m_betDelayMs));
                return;
            }
            emit logMessage(tr("Retrying (%1/%2)…").arg(m_retryStreak).arg(kMaxRetryStreak), false);
            if (!m_paused) {
                m_timer.start(5000); // every retry waits 5 s, whatever the error
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
    // The casino reported the balance after this bet (Tower "newBalance", Pasino "balance"):
    // start from the figure that, with this bet's profit added, lands exactly on it.
    if (r.balanceAfter >= 0.0 && liveMode())
        m_stats.balance = r.balanceAfter - (r.payout - r.amount);
    const double casinoTarget = (cfg.game == Game::Dice && m_live) ? m_live->lastDiceTarget() : -1.0;
    m_lastBetJson = r.bet;
    // Simulated bets have no casino bet to look up (no "View" link), like simulated dice rolls.
    const BetRecord rec = recordBet(cfg, r.amount, r.roll, r.win, r.multiplier, r.payout,
                                    r.nonce, betCasino.isEmpty() ? QString() : r.betId, betCasino,
                                    casinoTarget, r.targetText, r.rollText);
    if (manual && (cfg.game == Game::Hilo || cfg.game == Game::Blackjack)) {
        m_manualGame = cfg.game;
        closeManualGame(r.bet.isEmpty() ? m_manualGameBet : r.bet); // settled
    } else if ((cfg.game == Game::Hilo || cfg.game == Game::Blackjack) && !r.bet.isEmpty()) {
        emit autoGameUpdated(cfg.game, r.bet, false);
    }
    flushPauseLog(); // a pause that came while this bet was in flight: now the numbers are final
    flushStopLog(); // a stop that came while this bet was in flight: now the numbers are final

    // Game settings that stop the run (C# bot): diamonds "Stop on pattern", hilo "Stop on".
    const QString gameStop = isDiceOrLimbo(cfg.game) ? QString()
                                                      : StakeGames::stopReason(cfg.game, r.bet, cfg.gameParams);
    if (!gameStop.isEmpty())
        emit logMessage(tr("[STOP] %1").arg(gameStop), false);

    // A manual roll has no progression/stop logic. If the user pressed Stop
    // while the request was in flight the bet is still recorded (the money has
    // moved) but nothing further is scheduled.
    if (manual || !m_running) {
        finishScriptDrain(!manual);
        return;
    }

    if (!gameStop.isEmpty()) {
        stopWithReason(tr("stopped (%1)").arg(gameStop));
        return;
    }
    if (advanceProgression(rec.win, cfg)) // the engine's verdict (>= 1x), not the raw reply
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
        const ScriptStrategy::BetInfo info = lastBetInfo(m_lastRec.win);

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
        if (m_inFlight && !m_running) {
            // Never ignore the button silently: a request that never got an answer (e.g. an
            // open-round check after switching game) would block manual betting for good.
            if (m_inFlightClock.isValid() && m_inFlightClock.elapsed() > 15000) {
                emit logMessage(tr("The previous request got no answer - dropped it, betting now."), false);
                m_inFlight = false;
                m_loadingOpenGame = false;
            } else {
                emit logMessage(m_loadingOpenGame
                                    ? tr("Checking the casino for an open round - try again in a moment.")
                                    : tr("Wait for the current bet to finish."),
                                true);
                return {};
            }
        }
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
        bool fixedStake = false;
        const double stake = StakeGames::fixedStake(cfg.game, cfg.gameParams, &fixedStake);
        const double amount = fixedStake ? stake : cfg.baseBet;
        if (amount < 0.0 || amount > m_stats.balance) {
            emit logMessage(tr("Bet %1 exceeds the available balance %2.")
                                .arg(amount, 0, 'f', 8)
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

    if (!isDiceOrLimbo(cfg.game)) {
        // Stake's other games in simulation: played like a live bet (the result arrives
        // through betPlaced()).
        QString why;
        if (!m_simClient || !StakeFair::canSimulate(cfg.game, &why)) {
            emit logMessage(why.isEmpty() ? tr("%1 can't be simulated.").arg(gameDisplayName(cfg.game)) : why, true);
            return {};
        }
        if (m_inFlight) {
            emit logMessage(tr("Wait for the current bet to finish."), true);
            return {};
        }
        bool fixedStake = false;
        const double stake = StakeGames::fixedStake(cfg.game, cfg.gameParams, &fixedStake);
        const double amount = fixedStake ? stake : cfg.baseBet;
        if (amount < 0.0 || amount > m_stats.balance) {
            emit logMessage(tr("Bet %1 exceeds the available balance %2.")
                                .arg(amount, 0, 'f', 8)
                                .arg(m_stats.balance, 0, 'f', 8),
                            true);
            return {};
        }
        requestLiveBet(cfg, cfg.baseBet, true);
        return {};
    }

    const double bet = std::clamp(cfg.baseBet, 0.0, std::max(0.0, m_stats.balance));
    BetRecord rec = placeBet(cfg, bet);
    return rec;
}

void BotEngine::runScriptCommand(const QString &code, ScriptLanguage language)
{
    emit scriptOutput(QStringLiteral("> %1").arg(code), false); // echo, like a REPL
    if (!m_scriptActive || !m_script || !m_script->isLoaded()) {
        // No script running: the console's own script in the editor's language. Its variables
        // stay between commands; balance is the current one.
        ScriptStrategy *console = language == ScriptLanguage::JavaScript
            ? static_cast<ScriptStrategy *>(&m_consoleJs)
            : static_cast<ScriptStrategy *>(&m_consoleLua);
        console->setLogger([this](const QString &msg, bool isError) { emit scriptOutput(msg, isError); });
        if (!console->isLoaded()) {
            BotConfig cfg = m_config;
            cfg.game = Game::Dice; // no game variables needed
            double nextBet = m_config.baseBet;
            if (console->init(QString(), m_stats.balance, nextBet, cfg) == ScriptStrategy::Status::Error) {
                emit scriptOutput(console->lastError(), true);
                return;
            }
        }
        console->setGlobalNumber("balance", m_stats.balance);
        if (console->runCommand(code) == ScriptStrategy::Status::Error)
            emit scriptOutput(console->lastError(), true);
        return;
    }
    const ScriptStrategy::Status st = m_script->runCommand(code);
    if (st == ScriptStrategy::Status::Error) {
        emit scriptOutput(m_script->lastError(), true);
        return;
    }
    if (st == ScriptStrategy::Status::Stop && m_running) {
        // stop() from the command line: the same as from dobet().
        const QString why = m_script->stopReason();
        stopWithReason(why.isEmpty() ? tr("stopped from the console") : tr("stopped from the console: %1").arg(why));
    }
}

void BotEngine::loadOpenGame(const BotConfig &cfg)
{
    if (cfg.game != Game::Hilo && cfg.game != Game::Blackjack)
        return;
    // Never while a run starts / runs (a script's game = "blackjack" switches the Game menu
    // while the run is being set up: an open-game request then would hold the first bet).
    if (!liveMode() || m_running || m_scriptActive || m_inFlight || m_startPending || m_rollPending)
        return;
    if (!m_live->isStake() && !m_live->isShuffle() && !m_live->isGoated()
        && !(m_live->isWolfbet() && cfg.game == Game::Hilo)
        && !m_live->isDuel()
        && !(m_live->isPasino() && cfg.game == Game::Blackjack))
        return; // hilo and blackjack: Stake, Shuffle, Goated and Duel; hilo also on Wolfbet
    if (!liveReady(nullptr))
        return;
    // Always ask the casino (the round may have been played on, or finished, on the website
    // or on another casino meanwhile); the answer replaces what the Manual tab shows.
    m_inFlight = true;
    m_inFlightClock.start();
    m_inFlightManual = true;
    m_inFlightCfg = cfg;
    m_inFlightCasino = m_casinoGetter ? m_casinoGetter().trimmed().toLower() : QString();
    m_gameSteps = 0;
    m_gameFallback = 0;
    m_gameBet = QJsonObject();
    m_gameAmount = cfg.baseBet;
    m_loadingOpenGame = true;
    // No answer at all (lost request): free the engine again after a while, so manual bets
    // are not blocked for good.
    QTimer::singleShot(15000, this, [this] {
        if (m_loadingOpenGame && m_inFlight && !m_running && m_inFlightClock.elapsed() >= 15000) {
            m_loadingOpenGame = false;
            m_inFlight = false;
        }
    });
    m_live->fetchActiveGame(cfg.game, cfg.gameParams, cfg.baseBet);
}

void BotEngine::closeManualGame(QJsonObject bet)
{
    const bool wasOpen = m_manualGameOpen;
    m_manualGameOpen = false;
    m_manualGameBet = QJsonObject();
    if (wasOpen || !bet.isEmpty())
        emit manualGameUpdated(m_manualGame, bet, false);
}

void BotEngine::manualGameMove(const QString &move)
{
    const QString gameLabel = gameDisplayName(m_manualGame);
    if (m_running) {
        emit logMessage(tr("%1: stop the auto-bet before playing manually.").arg(gameLabel), true);
        return;
    }
    if (!m_manualGameOpen) {
        emit logMessage(tr("No round is open - start one with the bet button first."), true);
        return;
    }
    if (m_inFlight)
        return; // the previous move is still on its way
    if (!liveMode() && !m_manualGameSim) {
        emit logMessage(tr("%1: the round is open on %2 - turn live betting back on to play on.")
                            .arg(gameLabel, casinoLabel(m_manualGameCasino)),
                        true);
        return;
    }
    if (liveMode() && m_manualGameSim) {
        emit logMessage(tr("%1: this round was dealt in simulation - turn live betting off to play on.")
                            .arg(gameLabel),
                        true);
        return;
    }
    const QString casino = m_casinoGetter ? m_casinoGetter().trimmed().toLower() : QString();
    if (liveMode() && !m_manualGameCasino.isEmpty() && casino != m_manualGameCasino) {
        emit logMessage(tr("%1: the round is open on %2 - switch back to it to play on.")
                            .arg(gameLabel, casinoLabel(m_manualGameCasino)),
                        true);
        return;
    }

    if (m_manualGame == Game::Blackjack) {
        static const QStringList moves = {QStringLiteral("hit"), QStringLiteral("stand"),
                                          QStringLiteral("double"), QStringLiteral("split"),
                                          QStringLiteral("insurance"), QStringLiteral("noInsurance")};
        if (!moves.contains(move)) {
            emit logMessage(tr("Blackjack: unknown move \"%1\".").arg(move), true);
            return;
        }
        m_inFlight = true;
        m_inFlightClock.start();
        m_inFlightManual = true;
        m_inFlightCfg = m_manualGameCfg;
        m_inFlightCasino = m_manualGameCasino;
        m_gameBet = m_manualGameBet;
        m_gameAmount = m_manualGameAmount;
        m_gameSteps = 1;
        m_gameFallback = 0;
        m_gameChoice = move; // StakeGames::nextAction() turns it into the blackjack action
        QString err;
        if (!sendGameStep(&err)) {
            m_inFlight = false;
            emit logMessage(tr("Blackjack: %1").arg(err), true);
        }
        return;
    }

    // The card the guess is about: the last one drawn, else the start card.
    const QJsonObject state = m_manualGameBet.value(QStringLiteral("state")).toObject();
    const QJsonArray rounds = state.value(QStringLiteral("rounds")).toArray();
    QString rank = rounds.isEmpty() ? QString()
                                    : rounds.last().toObject().value(QStringLiteral("card")).toObject()
                                          .value(QStringLiteral("rank")).toString();
    if (rank.isEmpty())
        rank = state.value(QStringLiteral("startCard")).toObject().value(QStringLiteral("rank")).toString();
    rank = rank.toUpper();

    // As in the C# bot: High on an ace = "higher", Low on a king = "lower", otherwise the
    // "or equal" guesses; nothing is higher than a king / lower than an ace.
    QString guess;
    const QString m = move.trimmed().toLower();
    if (m == QLatin1String("high")) {
        if (rank == QLatin1String("K")) {
            emit logMessage(tr("Hilo: nothing is higher than a King - pick Low, Equal or Skip."), true);
            return;
        }
        guess = rank == QLatin1String("A") ? QStringLiteral("higher") : QStringLiteral("higherEqual");
    } else if (m == QLatin1String("low")) {
        if (rank == QLatin1String("A")) {
            emit logMessage(tr("Hilo: nothing is lower than an Ace - pick High, Equal or Skip."), true);
            return;
        }
        guess = rank == QLatin1String("K") ? QStringLiteral("lower") : QStringLiteral("lowerEqual");
    } else if (m == QLatin1String("equal") || m == QLatin1String("skip") || m == QLatin1String("cashout")) {
        guess = m;
    } else {
        emit logMessage(tr("Hilo: unknown move \"%1\".").arg(move), true);
        return;
    }

    m_inFlight = true;
    m_inFlightClock.start();
    m_inFlightManual = true;
    m_inFlightCfg = m_manualGameCfg;
    m_inFlightCasino = m_manualGameCasino;
    m_gameBet = m_manualGameBet;
    m_gameAmount = m_manualGameAmount;
    m_gameSteps = 1; // a move of an open bet (a 0 bet's "insignificantBet" cash out settles at 0)
    m_gameFallback = 0;
    m_gameChoice = guess;
    QString err;
    if (!sendGameStep(&err)) {
        m_inFlight = false;
        emit logMessage(tr("Hilo: %1").arg(err), true);
    }
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
    // (amount, chance, side, on-win/on-loss rules, stop limits, condition
    // blocks). Only the start balance (an Options setting) is kept.
    const bool scriptMode = !m_scriptSource.trimmed().isEmpty();
    QString simWhy;
    if (!scriptMode && !liveMode() && !isDiceOrLimbo(m_config.game)
        && (!m_simClient || !StakeFair::canSimulate(m_config.game, &simWhy))) {
        const QString err = simWhy.isEmpty() ? tr("%1 can't be simulated.").arg(gameDisplayName(m_config.game))
                                             : simWhy;
        emit logMessage(err, true);
        emit stopped(err);
        return;
    }
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
    m_runStartBets = m_stats.bets;
    m_conditions.setBlocks(scriptMode ? BlockList() : m_condBlocks);
    m_conditions.setEnabled(!scriptMode && m_condMode);
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
        prepareScriptSeeds();
        const auto st = m_script->init(m_scriptSource, m_stats.balance, m_nextBetSize, m_runCfg);
        applyScriptSeeds();
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
        if (!isDiceOrLimbo(m_runCfg.game))
            emit logMessage(tr("%1 strategy active (%2, first bet %3).")
                                .arg(lang, scriptGameName(m_runCfg.game))
                                .arg(m_nextBetSize, 0, 'f', 8),
                            false);
        else if (m_runCfg.game == Game::Limbo)
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
        m_titleGameValid = false;
        announceRunGame(); // the script's first game in the window title
        // The game set at the top of the script (game = "blackjack") becomes the Game menu's
        // choice too (Manual / Auto Bet tabs and title follow). Later switches in dobet() only
        // change the title.
        m_announcedGame = m_runCfg.game;
        emit gameChanged(m_runCfg.game);
    } else if (m_conditions.active())
        emit logMessage(tr("Condition builder active: %1 block(s), first bet %2. "
                           "Only the Auto Bet base bet is used.")
                            .arg(m_conditions.blockCount())
                            .arg(m_config.baseBet, 0, 'f', 8),
                        false);
    m_running = true;
    if (m_manualGameOpen) // an open manual hilo round is finished by the run (existingGame)
        closeManualGame(QJsonObject()); // no final state: the panel keeps its cards
    m_paused = false;

    if (!scriptMode && !m_conditions.active()) {
        // On win / on loss rules: show exactly what this run will do.
        auto ruleText = [this](const OnResultRule &r) {
            return r.increase ? tr("%1%2%").arg(r.pct >= 0.0 ? QStringLiteral("+") : QString())
                                    .arg(r.pct, 0, 'f', 1)
                              : tr("reset to base");
        };
        auto limitText = [this](double v) {
            return v > 0.0 ? QString::number(v, 'f', 8) : tr("off");
        };
        emit logMessage(tr("Auto-bet started: base bet %1, on win %2, on loss %3, "
                           "stop on profit %4, stop on loss %5, bets %6.")
                            .arg(m_config.baseBet, 0, 'f', 8)
                            .arg(ruleText(m_config.onWin), ruleText(m_config.onLoss),
                                 limitText(m_config.stopProfit), limitText(m_config.stopLoss),
                                 m_config.stopAfterBets > 0 ? QString::number(m_config.stopAfterBets)
                                                            : tr("unlimited")),
                        false);
        if (m_config.baseBet <= 0.0 && (m_config.onWin.increase || m_config.onLoss.increase))
            emit logMessage(tr("Base bet is 0 - increasing 0 by any percentage stays 0. "
                               "Set a base bet in the Auto Bet tab."),
                            true);
    } else {
        emit logMessage(tr("Auto-bet started."), false);
    }
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
    m_runStartBets = 0;
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
        emit logMessage(tr("%1: seed rotation waits for the bet in flight.").arg(currentCasinoLabel()), false);
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
        emit logMessage(tr("%1: seed rotation failed - retrying (%2/%3)…")
                            .arg(currentCasinoLabel())
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
        const QString msg = tr("cannot switch casino from %1 to %2: %3")
                                .arg(casinoLabel(have), casinoLabel(want), err);
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
    // The new casino bets in its own currency (the one set for it in Options). The script's
    // `currency` still holds the old casino's currency: overwrite it, otherwise the next bet
    // would try to "switch back" to it (e.g. USDC on a Wolfbet account that has none).
    m_runCfg.currency = currentCurrency();
    if (m_script) {
        m_script->setGlobalString("casino", want);
        if (!m_runCfg.currency.isEmpty())
            m_script->setGlobalString("currency", m_runCfg.currency);
    }
    emit logMessage(m_runCfg.currency.isEmpty()
                        ? tr("Casino switched from %1 to %2.").arg(casinoLabel(have), casinoLabel(want))
                        : tr("Casino switched from %1 to %2 (currency: %3).")
                              .arg(casinoLabel(have), casinoLabel(want), m_runCfg.currency.toUpper()),
                    false);
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
        // The session is kept (bets, profit, history, graph go on); only the balance becomes
        // the new currency's. The script sees it in `balance` before its next dobet().
        m_stats.balance = balance;
    }
    return false;
}

void BotEngine::switchCasino(const QString &casino)
{
    const QString want = casino.trimmed().toLower();
    if (want.isEmpty() || !m_running)
        return;
    m_pendingCasino = want;
    if (m_inFlight) {
        emit logMessage(tr("Casino switch to %1 waits for the bet in flight.").arg(casinoLabel(want)), false);
        return;
    }
    applyPendingCasino();
}

void BotEngine::applyPendingCasino()
{
    if (m_pendingCasino.isEmpty())
        return;
    const QString want = m_pendingCasino;
    m_pendingCasino.clear();

    const QString have = m_casinoGetter ? m_casinoGetter().trimmed().toLower() : QString();
    if (want == have || !m_casinoSwitcher)
        return;

    QString err;
    if (!m_casinoSwitcher(want, err)) {
        // From the menu: report it and keep betting on the current casino.
        emit logMessage(tr("Cannot switch casino to %1: %2").arg(casinoLabel(want), err), true);
        return;
    }

    m_runCfg.casino = want;
    // The new casino's connection needs a moment: the next bet waits until it is ready.
    m_awaitTransport = true;
    m_awaitTimer.start();
    // The new casino brings its own bet currency.
    m_runCfg.currency = currentCurrency();
    if (m_scriptActive && m_script) {
        // Otherwise the script's old `casino` / `currency` would switch straight back.
        m_script->setGlobalString("casino", want);
        if (!m_runCfg.currency.isEmpty())
            m_script->setGlobalString("currency", m_runCfg.currency);
    }
    emit logMessage(have.isEmpty() ? tr("Casino switched to %1.").arg(casinoLabel(want))
                                   : tr("Casino switched from %1 to %2.").arg(casinoLabel(have), casinoLabel(want)),
                    false);
}

QString BotEngine::casinoLabel(const QString &casino) const
{
    if (casino.isEmpty())
        return tr("Casino");
    return m_casinoLabel ? m_casinoLabel(casino) : casino;
}

QString BotEngine::currentCasinoLabel() const
{
    return casinoLabel(m_casinoGetter ? m_casinoGetter().trimmed().toLower() : QString());
}

void BotEngine::switchGame(const BotConfig &betCfg)
{
    const Game game = betCfg.game;
    m_config.game = game; // also what a start that is still pending begins with

    if (m_scriptActive && m_script) {
        // Script run: set the script's `game` variable. Its next dobet() sees it, the values it
        // sets (target for limbo, chance / bethigh for dice) are read back and the bet after
        // that is placed in the new game. A bet already scheduled uses the old game.
        m_script->setGlobalString("game", scriptGameName(game));
        m_announcedGame = game;
        emit logMessage(tr("Game switched to %1 - the script's next dobet() uses it.")
                            .arg(scriptGameName(game)),
                        false);
        return;
    }
    if (!m_scriptSource.trimmed().isEmpty())
        return; // script start still pending: it begins with m_config.game
    m_config.target = betCfg.target;
    m_config.side = betCfg.side;
    m_config.multiplierTarget = betCfg.multiplierTarget;
    m_config.gameParams = betCfg.gameParams;
    if (!m_running)
        return;

    m_runCfg.game = game;
    m_runCfg.target = betCfg.target;
    m_runCfg.side = betCfg.side;
    m_runCfg.multiplierTarget = betCfg.multiplierTarget;
    m_runCfg.gameParams = betCfg.gameParams;
    m_conditions.setBetBase(m_runCfg);
    m_announcedGame = game;
    QString simWhy;
    if (!liveMode() && !isDiceOrLimbo(game) && (!m_simClient || !StakeFair::canSimulate(game, &simWhy))) {
        stopWithReason(tr("halted (%1)").arg(simWhy.isEmpty() ? tr("%1 can't be simulated").arg(gameDisplayName(game))
                                                             : simWhy));
        return;
    }
    if (!isDiceOrLimbo(game))
        emit logMessage(tr("Game switched to %1.").arg(scriptGameName(game)), false);
    else if (game == Game::Limbo)
        emit logMessage(tr("Game switched to limbo (target %1x).")
                            .arg(m_runCfg.multiplierTarget, 0, 'f', 2),
                        false);
    else
        emit logMessage(tr("Game switched to dice (chance %1%, %2).")
                            .arg(m_runCfg.chance(), 0, 'f', 2)
                            .arg(m_runCfg.side == BetSide::Over ? tr("over") : tr("under")),
                        false);
}

void BotEngine::updateAutoSettings(const BotConfig &cfg, bool condMode, const BlockList &blocks)
{
    if (m_scriptActive || !m_scriptSource.trimmed().isEmpty())
        return; // script runs ignore the Auto Bet tab

    const BotConfig old = m_config;
    const bool oddsEdited = old.target != cfg.target || old.side != cfg.side
                            || old.multiplierTarget != cfg.multiplierTarget
                            || old.gameParams != cfg.gameParams;
    const auto copyRules = [&cfg](BotConfig &dst) {
        dst.baseBet = cfg.baseBet;
        dst.stopAfterBets = cfg.stopAfterBets;
        dst.onWin = cfg.onWin;
        dst.onLoss = cfg.onLoss;
        dst.stopProfit = cfg.stopProfit;
        dst.stopLoss = cfg.stopLoss;
    };
    const auto copyOdds = [&cfg](BotConfig &dst) {
        dst.target = cfg.target;
        dst.side = cfg.side;
        dst.multiplierTarget = cfg.multiplierTarget;
        dst.gameParams = cfg.gameParams;
    };
    copyRules(m_config);
    copyOdds(m_config);
    const bool condModeChanged = condMode != m_condMode;
    m_condMode = condMode;
    m_condBlocks = blocks;
    if (!m_running)
        return; // a pending start picks everything up from m_config

    copyRules(m_runCfg);
    if (oddsEdited)
        copyOdds(m_runCfg);
    // Sitting at the base bet: the next bet is the new base bet. Otherwise the progression
    // carries on and the new base is used at its next reset.
    if (std::abs(m_nextBetSize - old.baseBet) < 1e-12)
        m_nextBetSize = cfg.baseBet;

    m_conditions.setBlocks(blocks);
    m_conditions.setEnabled(condMode);
    if (condModeChanged && condMode) {
        m_conditions.reset(m_config); // fresh streaks / counters for the conditions
    } else {
        m_conditions.setBetBase(m_config);
        m_conditions.setBaseBet(m_config.baseBet);
    }

    if (oddsEdited) {
        if (!isDiceOrLimbo(m_runCfg.game))
            emit logMessage(tr("Auto-bet updated: %1 settings.").arg(scriptGameName(m_runCfg.game)), false);
        else if (m_runCfg.game == Game::Limbo)
            emit logMessage(tr("Auto-bet updated: limbo target %1x.")
                                .arg(m_runCfg.multiplierTarget, 0, 'f', 2),
                            false);
        else
            emit logMessage(tr("Auto-bet updated: dice chance %1%, %2.")
                                .arg(m_runCfg.chance(), 0, 'f', 2)
                                .arg(m_runCfg.side == BetSide::Over ? tr("over") : tr("under")),
                            false);
    }
    if (condModeChanged)
        emit logMessage(condMode ? tr("Auto-bet updated: Condition Builder on.")
                                 : tr("Auto-bet updated: Condition Builder off (on win / on loss rules)."),
                        false);
}

void BotEngine::announceRunGame()
{
    // "cross" from a script: the casino's own road-crossing game (Goat Run on Goated, Chicken
    // elsewhere), so the title / menus never show a game the casino does not have.
    if (isCrossGame(m_runCfg.game)) {
        const QString casino = (!m_runCfg.casino.isEmpty() ? m_runCfg.casino
                                : m_casinoGetter ? m_casinoGetter() : QString()).trimmed().toLower();
        m_runCfg.game = casino == QLatin1String("goated") ? Game::GoatRun : Game::Chicken;
    }
    if (m_titleGameValid && m_runCfg.game == m_titleGame)
        return;
    m_titleGame = m_runCfg.game;
    m_titleGameValid = true;
    emit runGameChanged(m_titleGame);
}

void BotEngine::notifyGameChange()
{
    if (m_runCfg.game == m_announcedGame)
        return;
    m_announcedGame = m_runCfg.game;
    emit logMessage(tr("Game switched to %1.").arg(scriptGameName(m_announcedGame)), false);
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
    if (m_inFlight && !m_inFlightManual) {
        // The bet in flight is still recorded when it lands: log after that, so the line
        // shows the latest bet count and profit.
        m_pauseLogPending = true;
    } else {
        logPause();
    }
    emit pausedChanged(true);
}

void BotEngine::logPause()
{
    emit logMessage(tr("Paused after %1 bets (P/L %2)")
                        .arg(m_stats.bets)
                        .arg(m_stats.profit, 0, 'f', 8),
                    false);
}

void BotEngine::flushPauseLog()
{
    if (!m_pauseLogPending)
        return;
    m_pauseLogPending = false;
    logPause();
}

void BotEngine::resume()
{
    if (!m_running || !m_paused)
        return;
    m_paused = false;
    flushPauseLog(); // resumed before the in-flight bet landed: keep the log order
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
    m_pendingCasino.clear();
    m_seedRotationPending = false;
    m_seedRotationQueued = false;
    m_seedWaitTimer.stop();
    if (m_inFlight && !m_inFlightManual) {
        // The bet in flight is still recorded when it lands: log after that.
        m_pendingStopReason = reason;
        m_stopLogPending = true;
    } else {
        flushPauseLog();
        logStop(reason);
    }
    emit stopped(reason);
}

void BotEngine::logStop(const QString &reason)
{
    emit logMessage(tr("Auto-bet %1 (%2 bets, P/L %3)")
                        .arg(reason)
                        .arg(m_stats.bets)
                        .arg(m_stats.profit, 0, 'f', 8),
                    false);
}

void BotEngine::flushStopLog()
{
    if (!m_stopLogPending)
        return;
    m_stopLogPending = false;
    logStop(m_pendingStopReason);
}

void BotEngine::scheduleNext()
{
    m_timer.start(std::max(0, m_betDelayMs));
}

// ------------------------------------------------------------------- core

ScriptStrategy::BetInfo BotEngine::lastBetInfo(bool win) const
{
    ScriptStrategy::BetInfo info;
    info.win = win;
    info.bet = m_lastRec.bet;
    info.profit = m_lastRec.profit;
    info.payout = m_lastRec.payout;
    info.roll = m_lastRec.rawRoll; // full value, not cut to 2 decimals
    info.balance = m_stats.balance;
    info.runProfit = m_stats.profit - m_runStartProfit;
    info.multiplier = m_lastRec.mult;
    info.betId = m_lastRec.betId;
    info.game = m_lastRec.game;
    info.betJson = m_lastBetJson;
    return info;
}

BetRecord BotEngine::recordBet(const BotConfig &cfg, double betSize, double roll, bool win,
                               double mult, double payout, qint64 nonce,
                               const QString &betId, const QString &casino,
                               double casinoTarget, const QString &targetText,
                               const QString &rollText)
{
    BetRecord rec;
    rec.nonce = nonce;
    rec.betId = betId;
    // Live bets: the casino the bet was sent to (captured in requestLiveBet()). Simulated bets
    // are recorded at once, so the casino selected now is the right one.
    if (!casino.isEmpty())
        rec.casino = casino;
    else if (m_casinoGetter)
        rec.casino = m_casinoGetter().trimmed().toLower();

    const double profit = payout - betSize;

    // Single source of truth for win / loss: only a payout multiplier >= 1x is a win,
    // whatever the casino's own "win" flag or a non-zero payout says.
    const double payoutMult = BotConstants::settledMultiplier(betSize, payout, mult, win);
    win = BotConstants::isWinMultiplier(payoutMult);
    if (betSize > 0.0 && payout > 0.0)
        mult = payoutMult; // show the multiplier actually paid (also for a < 1x partial payout)

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
    rec.target = cfg.game == Game::Limbo ? cfg.multiplierTarget
                 : cfg.game == Game::Dice ? cfg.target : 0.0;
    rec.casinoTarget = cfg.game == Game::Dice ? casinoTarget : -1.0;
    rec.targetText = targetText;
    rec.rollText = rollText;
    rec.side = cfg.side;
    rec.roll = truncate2(roll); // 2 decimals, cut not rounded (history and UI)
    rec.rawRoll = roll;         // scripts get the full value
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
    if (!isDiceOrLimbo(cfg.game)) {
        // Stake's other games are simulated through requestLiveBet() (placeNextBet / rollOnce).
        QString why;
        StakeFair::canSimulate(cfg.game, &why);
        const QString err = why.isEmpty() ? tr("%1 can't be simulated here.").arg(gameDisplayName(cfg.game)) : why;
        emit logMessage(err, true);
        if (m_running)
            stopWithReason(tr("halted (%1)").arg(err));
        return BetRecord();
    }
    m_lastBetJson = QJsonObject();
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
        const ScriptStrategy::BetInfo info = lastBetInfo(win);

        syncScriptSeeds(); // the next nonce
        const auto st = m_script->onBet(info, m_nextBetSize, m_runCfg);
        applyScriptSeeds();
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
        announceRunGame(); // dobet() set `game`: the window title follows
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
    // Measured from the moment Start was pressed (not the whole session), so an earlier
    // profit / loss or earlier bets do not stop a new run straight away.
    const BotConfig &cfg = m_config;
    const double runProfit = m_stats.profit - m_runStartProfit;
    const qint64 runBets = m_stats.bets - m_runStartBets;
    if (cfg.stopProfit > 0.0 && runProfit >= cfg.stopProfit - 1e-12) {
        stopWithReason(tr("hit stop-on-profit (+%1)").arg(runProfit, 0, 'f', 8));
        return true;
    }
    if (cfg.stopLoss > 0.0 && -runProfit >= cfg.stopLoss - 1e-12) {
        stopWithReason(tr("hit stop-on-loss (%1)").arg(runProfit, 0, 'f', 8));
        return true;
    }
    if (cfg.stopAfterBets > 0 && runBets >= cfg.stopAfterBets) {
        stopWithReason(tr("finished %1 bets").arg(runBets));
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
        bool fixedStake = false;
        const double stake = StakeGames::fixedStake(m_runCfg.game, m_runCfg.gameParams, &fixedStake);
        const double bet = fixedStake ? stake : betAmount(m_nextBetSize);
        if (m_retryNoBalance && bet >= 0.0 && bet > m_stats.balance) {
            // Option "Retry bet when bet higher than balance": keep the run alive and look again in 5 s
            // (the balance is refreshed from the server in the meantime).
            if (!m_noBalanceLogged) {
                m_noBalanceLogged = true;
                emit logMessage(tr("Next bet %1 exceeds balance %2 - Retry betting...")
                                    .arg(bet, 0, 'f', 8)
                                    .arg(m_stats.balance, 0, 'f', 8),
                                true);
            }
            m_timer.start(5000);
            return;
        }
        m_noBalanceLogged = false;
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

    if (!isDiceOrLimbo(cfg.game)) {
        // Stake's other games in simulation: the same flow as a live bet, answered by the
        // simulated Stake (the result comes back through onLiveFinished()).
        if (m_inFlight)
            return;
        QString why;
        if (!m_simClient || !StakeFair::canSimulate(cfg.game, &why)) {
            stopWithReason(tr("halted (%1)").arg(why.isEmpty() ? tr("%1 can't be simulated").arg(gameDisplayName(cfg.game))
                                                               : why));
            return;
        }
        bool fixedStake = false;
        const double stake = StakeGames::fixedStake(cfg.game, cfg.gameParams, &fixedStake);
        const double gameBet = fixedStake ? stake : betAmount(m_nextBetSize);
        if (gameBet < 0.0 || gameBet > m_stats.balance) {
            stopWithReason(tr("halted (next bet %1 exceeds balance %2)")
                               .arg(gameBet, 0, 'f', 8)
                               .arg(m_stats.balance, 0, 'f', 8));
            return;
        }
        requestLiveBet(cfg, gameBet, false);
        return;
    }

    // Bet exactly the amount the strategy asked for (0 is a valid bet). Never
    // shrink it to the remaining balance: if it can't be covered, halt instead.
    const double bet = betAmount(m_nextBetSize);
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
