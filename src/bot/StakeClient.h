#pragma once

#include "bot/StakeTransport.h"

#include <QMap>
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

    void setTransport(IStakeTransport *transport) { m_transport = transport; }
    IStakeTransport *transport() const { return m_transport; }

    // API currency code, lowercase ("btc", "usdt", ...).
    void setCurrency(const QString &currency) { m_currency = currency.trimmed().toLower(); }
    QString currency() const { return m_currency; }

    // Login check + balances (UserBalances GraphQL query).
    void fetchBalances();

    void placeDice(const DiceBetRequest &request);
    void placeLimbo(const LimboBetRequest &request);

    // RotateSeedPair GraphQL mutation: sets a new client seed and rotates the
    // server seed. Result arrives through seedRotated() / seedRotateFailed().
    void rotateSeedPair(const QString &clientSeed);

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
    void diceFinished(const LiveBetResult &result);
	void sessionLost(const QString &reason);

    // Active seed pair after a successful rotation.
    void seedRotated(const QString &clientSeed, qint64 nonce,
                     const QString &seedHash, const QString &nextSeedHash);
    void seedRotateFailed(const QString &error);

private:
    void postBet(const QString &endpoint, const QJsonObject &body, double requestedAmount);
    static QString randomIdentifier();

    IStakeTransport *m_transport = nullptr;
    QString m_currency;
};
