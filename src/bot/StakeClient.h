#pragma once

#include "bot/BotTypes.h"
#include "bot/StakeTransport.h"

#include <QList>
#include <QMap>
#include <QMetaObject>
#include <QObject>
#include <QString>

#include <functional>

struct DiceBetRequest
{
    double amount = 0.0;
    double target = 50.0;   // roll target, same meaning as BotConfig::target
    bool over = false;      // false = roll under target, true = roll over target
};

struct LimboBetRequest
{
    double amount = 0.0;
    double multiplierTarget = 2.0;
};

// One of Stake's other games (keno, mines, hilo, ...): see bot/StakeGames.h.
struct GameBetRequest
{
    Game game = Game::Keno;
    double amount = 0.0;     // total stake (baccarat / roulette: the sum of the sides / chips)
    QJsonObject params;      // BotConfig::gameParams
};

// Outcome of one live bet. Everything here comes from the server.
struct LiveBetResult
{
    bool ok = false;

    // Failure details (ok == false)
    QString error;
    QString errorType;
    // The server answered with an error and the bet was NOT placed (rate limit,
    // parallel-bet collision, seed rotation notice) -> safe to retry.
    bool retryable = false;
    // No answer at all (timeout, disconnect). The bet MAY have been placed.
    bool outcomeUnknown = false;
    // Suggested wait before repeating a retryable bet (0 = engine's default backoff).
    // Parallel-bet collisions and seed-rotation notices clear within milliseconds, so
    // they get a short delay instead of the rate-limit backoff.
    int retryDelayMs = 0;
    // Resend at once instead of the engine's 5 s retry wait (Paradice "slowdown_please").
    bool retryNow = false;

    // Success details
    double amount = 0.0;
    double payout = 0.0;
    double multiplier = 0.0;
    double roll = 0.0;
    bool win = false;
    QString betId;
    qint64 nonce = -1;
    // The account balance right after this bet, when the casino reports it (Tower "newBalance",
    // Pasino "balance"); < 0 = not reported. The engine's balance is set to it.
    double balanceAfter = -1.0;

    // Stake's other games (placeGame() / sendGameAction()):
    // The bet is not settled yet (hilo / blackjack / video poker waiting for the next move,
    // blue samurai bonus spins): BotEngine sends the next step with sendGameAction().
    bool active = false;
    QJsonObject bet;        // the casino's bet object ({id, amount, payoutMultiplier, state, ...})
    QString targetText;     // bet list Target / Roll columns (see StakeGames::summarize())
    QString rollText;
};

// Talks to the casino's REST/GraphQL API through an IStakeTransport.
class StakeClient : public QObject
{
    Q_OBJECT

public:
    explicit StakeClient(QObject *parent = nullptr);

    // The transport decides the casino: a ShuffleTransport switches this client to
    // Shuffle's GraphQL API (dice and limbo; balances come from its websocket), a
    // GoatedTransport to Goated's REST API (dice and limbo; balances over HTTPS), a
    // WolfbetTransport to Wolfbet's REST API (dice only; balances over HTTPS), a DuelTransport
    // to Duel's REST API (dice and limbo; balances over HTTPS), a ParadiceTransport to Paradice's
    // GraphQL API (dice only; balances over HTTPS), a PasinoTransport to Pasino's REST API
    // (dice and limbo; balances over HTTPS), a TowerTransport to Tower.bet's REST API (dice and limbo;
    // balances over HTTPS, cf_clearance in Direct mode), a WinnaTransport to Winna.com's
    // originals API (dice and limbo; balances over HTTPS, cf_clearance in Direct mode).
    void setTransport(IStakeTransport *transport);
    IStakeTransport *transport() const { return m_transport; }
    bool isShuffle() const;
    bool isGoated() const;
    bool isWolfbet() const;
    bool isDuel() const;
    bool isParadice() const;
    bool isPasino() const;
    bool isTower() const;
    bool isWinna() const;
    bool isStake() const; // none of the above: Stake (or one of its mirrors)

    // API currency code, lowercase ("btc", "usdt", ...).
    void setCurrency(const QString &currency);
    QString currency() const { return m_currency; }

    // Login check + balances (UserBalances GraphQL query).
    void fetchBalances();

    // Stake's UserBalances query sent through `via` (the Stake extension / hidden browser) while
    // another casino is the active transport. Answers arrive through backgroundBalancesReady() /
    // backgroundBalancesFailed() and never touch the active casino's signals.
    void fetchBalancesVia(IStakeTransport *via);

    void placeDice(const DiceBetRequest &request);
    void placeLimbo(const LimboBetRequest &request);
    // Stake only: keno, mines, plinko, hilo, blackjack, ... (everything in StakeGames except
    // dice / limbo). The result arrives through diceFinished(); an unfinished bet comes back
    // with `active` set and is continued with sendGameAction(). When Stake answers
    // "existingGame" for a game that can be left open (hilo, blackjack, video poker, mines),
    // that open bet is fetched and returned as `active` so it can be finished first.
    void placeGame(const GameBetRequest &request);
    // Next step of an unfinished bet (StakeGames::nextAction()). `requestedAmount` is the
    // bet's stake (used when the answer does not repeat it).
    void sendGameAction(Game game, const QString &endpoint, const QJsonObject &body,
                        const QJsonObject &params, double requestedAmount);
    // Fetches the open bet of `game` (_api/casino/active-bet/<slug>): through diceFinished()
    // as an `active` result, or a retryable "existingGame" failure when nothing is open.
    // Used after "existingGame" and to show an unfinished hilo / blackjack round.
    void fetchActiveGame(Game game, const QJsonObject &params, double requestedAmount);
    // The roll target the casino got for the last dice bet, in the roll's own scale (Pasino
    // "High > 5249" -> 52.49, Tower ">50750" -> 50.75). It differs from the app's target where the casino has another house edge or range. < 0 = the app's
    // target was sent as it is (and for limbo bets).
    double lastDiceTarget() const { return m_lastDiceTarget; }

    // RotateSeedPair GraphQL mutation: sets a new client seed and rotates the
    // server seed. Result arrives through seedRotated() / seedRotateFailed().
    void rotateSeedPair(const QString &clientSeed);

    // Moves `amount` of the current currency into the vault (Shuffle: VaultDeposit,
    // Stake: CreateVaultDeposit mutation). Result arrives through vaultDeposited() / vaultFailed().
    void vaultDeposit(double amount);

    // BetLookup GraphQL query: resolves a numeric bet id to its public iid
    // ("casino:xxxx") used by the ?modal=bet&iid= link. `done` is called exactly
    // once with the iid, or an empty string on any failure.
    void lookupBetIid(const QString &betId, std::function<void(const QString &iid)> done);

    // Random alphanumeric client seed (default 10 characters).
    static QString randomClientSeed(int length = 10);

signals:
    // currency (lowercase) -> available amount
    void balancesReady(const QMap<QString, double> &balances);
    void balancesFailed(const QString &error);
    void backgroundBalancesReady(const QMap<QString, double> &balances);
    void backgroundBalancesFailed(const QString &error);
    void diceFinished(const LiveBetResult &result);
	void sessionLost(const QString &reason);

    // Active seed pair after a successful rotation.
    void seedRotated(const QString &clientSeed, qint64 nonce,
                     const QString &seedHash, const QString &nextSeedHash);
    void seedRotateFailed(const QString &error);

    // Result of vaultDeposit(): the deposited amount (as confirmed by the server) / the error.
    void vaultDeposited(double amount, const QString &currency);
    void vaultFailed(const QString &error);

private:
    void postBalanceQuery(IStakeTransport *via, bool background);
    void postBet(const QString &endpoint, const QJsonObject &body, double requestedAmount);
    // placeGame() / sendGameAction(): posts and turns the answer into a LiveBetResult.
    void postGameBet(Game game, const QString &endpoint, const QJsonObject &body,
                     const QJsonObject &params, double requestedAmount, bool firstRequest);
    LiveBetResult gameResult(Game game, const QJsonObject &bet, const QJsonObject &params,
                             double requestedAmount) const;
    void placeShuffleDice(const DiceBetRequest &request);
    void placeShuffleLimbo(const LimboBetRequest &request);
    // Shuffle blackjack (BlackjackStart / BlackjackNext / BlackjackActiveBet GraphQL). The
    // answers are turned into Stake's blackjack bet shape, so the engine, the Manual tab and
    // the scripts handle both casinos the same way.
    void placeShuffleBlackjack(const GameBetRequest &request);
    void sendShuffleBlackjack(const QString &stakeAction, const QJsonObject &params, double requestedAmount);
    void fetchShuffleBlackjack(const QJsonObject &params, double requestedAmount);
    void postShuffleBlackjack(const QString &operation, const QString &query, const QJsonObject &variables,
                              const QString &resultKey, const QJsonObject &params, double requestedAmount,
                              bool firstRequest);
    QString shuffleWindowId();
    // Shuffle keno (KenoPlay GraphQL), answered in Stake's keno shape.
    void placeShuffleKeno(const GameBetRequest &request);
    // Shuffle mines (MinesAutoBet: all tiles revealed in one request, like Stake's mines bet).
    void placeShuffleMines(const GameBetRequest &request);
    // Shuffle plinko (PlinkoPlay GraphQL), answered in Stake's plinko shape.
    void placeShufflePlinko(const GameBetRequest &request);
    // Shuffle wheel (WheelPlay GraphQL), answered in Stake's wheel shape.
    void placeShuffleWheel(const GameBetRequest &request);
    // Shuffle tower (TowerAutoBet: one pick per floor in one request) = the Tower game (Stake's Dragon Tower).
    void placeShuffleTower(const GameBetRequest &request);
    // Shuffle chicken (ChickenAuto: cross up to the chosen lane in one request).
    void placeShuffleChicken(const GameBetRequest &request);
    QJsonObject shuffleAutoBetInfo(); // the autoBetInfo the site sends with auto bets
    QString m_shuffleAutoSessionId; // autoBetInfo.sessionId (one per app session)
    int m_shuffleAutoBetCount = 0;  // autoBetInfo.currentBetCount
    // Shuffle hilo (HiloStart / HiloNext / HiloActiveBet), also turned into Stake's shape.
    void placeShuffleHilo(const GameBetRequest &request);
    void sendShuffleHilo(const QString &endpoint, const QJsonObject &stakeBody, const QJsonObject &params,
                         double requestedAmount);
    void fetchShuffleHilo(const QJsonObject &params, double requestedAmount);
    void postShuffleHilo(const QString &operation, const QString &query, const QJsonObject &variables,
                         const QString &resultKey, const QJsonObject &params, double requestedAmount,
                         bool firstRequest);
    void placeGoatedDice(const DiceBetRequest &request);
    void placeGoatedLimbo(const LimboBetRequest &request);
    // Goated blackjack (REST: casino/blackjack/create, hit, stand, double-down, split,
    // buy-insurance, reject-insurance), answered in Stake's blackjack shape.
    void placeGoatedBlackjack(const GameBetRequest &request);
    void sendGoatedBlackjack(const QString &stakeAction, const QJsonObject &params, double requestedAmount);
    void postGoatedBlackjack(const QString &endpoint, const QJsonObject &body, const QJsonObject &params,
                             double requestedAmount, bool firstRequest = false);
    // Goated hilo (casino/hilo/bet, PATCH casino/hilo/select {choice: H / L / S},
    // casino/hilo/cashout), answered in Stake's hilo shape.
    void placeGoatedHilo(const GameBetRequest &request);
    void sendGoatedHilo(const QString &endpoint, const QJsonObject &stakeBody, const QJsonObject &params,
                        double requestedAmount);
    void fetchGoatedHilo(const QJsonObject &params, double requestedAmount);
    void handleGoatedHiloReply(const TransportReply &reply, const QJsonObject &params, double requestedAmount,
                               bool firstRequest, bool activeQuery);
    QString m_goatedHiloRank; // the card the next guess is about
    // Goated keno (casino/keno/bet), answered in Stake's keno shape.
    void placeGoatedKeno(const GameBetRequest &request);
    QString m_goatedAutobetSessionId; // autobet_session_id sent with Goated keno / mines bets
    QString goatedAutobetSessionId();
    // Goated mines (casino/mines/multi-select-bet: all tiles in one request).
    void placeGoatedMines(const GameBetRequest &request);
    // Goated plinko (casino/plinko).
    void placeGoatedPlinko(const GameBetRequest &request);
    // Goated wheel (casino/wheel).
    void placeGoatedWheel(const GameBetRequest &request);
    // Goated diamonds (casino/diamonds).
    void placeGoatedDiamonds(const GameBetRequest &request);
    // Goated tower (casino/tower/multi-select-bet: one pick per floor in one request).
    void placeGoatedTower(const GameBetRequest &request);
    // Goated Goat Run (casino/goat-run/multi-hop-bet: hop to the chosen lane in one request).
    void placeGoatedGoatRun(const GameBetRequest &request);
    // GET casino/blackjack/active: the open hand (or a quiet "nothing open").
    void fetchGoatedBlackjack(const QJsonObject &params, double requestedAmount);
    void handleGoatedBlackjackReply(const TransportReply &reply, const QJsonObject &params,
                                    double requestedAmount, bool firstRequest, bool activeQuery);
    void placeWolfbetDice(const DiceBetRequest &request);
    void placeWolfbetLimbo(const LimboBetRequest &request);
    void rotateWolfbetSeed();
    void placeParadiceDice(const DiceBetRequest &request);
    void rotateParadiceSeed();
    void placePasinoDice(const DiceBetRequest &request);
    void placePasinoLimbo(const LimboBetRequest &request);
    void placeTowerDice(const DiceBetRequest &request);
    void placeTowerLimbo(const LimboBetRequest &request);
    void placeWinnaDice(const DiceBetRequest &request);
    // `overWord`: index into the words tried for "over" (the site's word was not captured yet).
    void sendWinnaDice(double amount, double target, bool over, int overWord);
    void placeWinnaLimbo(const LimboBetRequest &request);
    void rotateWinnaSeed(const QString &clientSeed);
    void rotateTowerSeed(const QString &clientSeed);
    void rotatePasinoSeed(const QString &clientSeed);
    void placeDuelDice(const DiceBetRequest &request);
    void rotateDuelSeed(const QString &clientSeed);
    void placeDuelLimbo(const LimboBetRequest &request);
    // Shared by Duel dice and limbo: adds the security token to `fields`, posts to `path` and
    // reads {"data":{"round":{...}}}. limbo: the result is the multiplier, a win is result >= target;
    // dice: the result is the roll in hundredths, compared with target over / under.
    void sendDuelBet(const QString &path, const QJsonObject &fields, double requestedAmount,
                     bool limbo, double target, bool over, bool tokenRetried);
    void rotateGoatedSeed(const QString &clientSeed);
    void goatedVaultDeposit(double amount);
    void rotateShuffleSeed(const QString &clientSeed);
    void stakeVaultDeposit(double amount);
    static QString randomIdentifier();

    IStakeTransport *m_transport = nullptr;
    QList<QMetaObject::Connection> m_shuffleConnections;
    QString m_currency;
    // Pasino sends the client seed with every bet ("client_seed"); set by rotateSeedPair().
    QString m_pasinoClientSeed;
    // Winna: index of the word for "roll over" that the site accepted (see placeWinnaDice()).
    int m_winnaOverWord = 0;
    double m_lastDiceTarget = -1.0; // see lastDiceTarget()
    // Shuffle blackjack: the open bet (BlackjackNext needs its id and the number of steps).
    QString m_shuffleWindowId;
    QString m_shuffleBjBetId;
    int m_shuffleBjSteps = 0;
    QString m_shuffleHiloBetId;
    int m_shuffleHiloSteps = 0;
    QString m_shuffleHiloRank; // the card the next guess is about
};
