#pragma once

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
    // (dice and limbo; balances over HTTPS).
    void setTransport(IStakeTransport *transport);
    IStakeTransport *transport() const { return m_transport; }
    bool isShuffle() const;
    bool isGoated() const;
    bool isWolfbet() const;
    bool isDuel() const;
    bool isParadice() const;
    bool isPasino() const;

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
    void placeShuffleDice(const DiceBetRequest &request);
    void placeShuffleLimbo(const LimboBetRequest &request);
    void placeGoatedDice(const DiceBetRequest &request);
    void placeGoatedLimbo(const LimboBetRequest &request);
    void placeWolfbetDice(const DiceBetRequest &request);
    void placeWolfbetLimbo(const LimboBetRequest &request);
    void rotateWolfbetSeed();
    void placeParadiceDice(const DiceBetRequest &request);
    void rotateParadiceSeed();
    void placePasinoDice(const DiceBetRequest &request);
    void placePasinoLimbo(const LimboBetRequest &request);
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
};
