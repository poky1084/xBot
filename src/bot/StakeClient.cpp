#include "bot/StakeClient.h"

#include "bot/ShuffleTransport.h"

#include <QJsonArray>
#include <QRandomGenerator>
#include <QTimer>

#include <cmath>
#include <utility>

namespace
{
double numberOf(const QJsonValue &v)
{
    if (v.isDouble())
        return v.toDouble();
    if (v.isString())
        return v.toString().toDouble();
    return 0.0;
}

QString firstErrorText(const QJsonObject &json, QString *errType)
{
    const QJsonArray errs = json.value(QStringLiteral("errors")).toArray();
    if (errs.isEmpty())
        return {};
    const QJsonObject e = errs.first().toObject();
    const QString type = e.value(QStringLiteral("errorType")).toString();
    QString msg = e.value(QStringLiteral("message")).toString();
    if (msg.isEmpty())
        msg = type;
    if (msg.isEmpty())
        msg = QStringLiteral("API error");
    if (errType)
        *errType = type;
    return msg;
}

bool containsCI(const QString &haystack, const char *needle)
{
    return haystack.contains(QLatin1String(needle), Qt::CaseInsensitive);
}

// Server answered with an error and the bet did not go through, so repeating
// it is safe. (Same rules as the C# bot's IsParallelBetError / IsRateLimitError
// / IsSeedRotateError.)
bool isRetryable(const QString &type, const QString &msg)
{
    const bool parallel = containsCI(type, "parallelCasinoBet") && !containsCI(type, "internalError");
    const bool rateLimit = containsCI(msg, "slow down") || msg.contains(QLatin1String("429"));
    const bool seedRotate = containsCI(msg, "new seed pair") || containsCI(msg, "choose a new seed")
                            || containsCI(msg, "seedRotate") || containsCI(type, "seedRotate")
                            || (containsCI(msg, "seed") && containsCI(msg, "rotat"));
    return parallel || rateLimit || seedRotate;
}

// Short retry delay for errors that clear immediately; 0 for rate limits (keep the
// engine's growing backoff there) and for everything else.
int fastRetryDelayMs(const QString &type, const QString &msg)
{
    const bool rateLimit = containsCI(msg, "slow down") || msg.contains(QLatin1String("429"));
    if (rateLimit)
        return 0;
    if (containsCI(type, "parallelCasinoBet") && !containsCI(type, "internalError"))
        return 40;
    if (containsCI(msg, "new seed pair") || containsCI(msg, "choose a new seed")
        || containsCI(msg, "seedRotate") || containsCI(type, "seedRotate")
        || (containsCI(msg, "seed") && containsCI(msg, "rotat")))
        return 100;
    return 0;
}

// The API wraps the bet in one property ({"diceRoll": {...}}), optionally
// inside {"data": {...}}. Pick the first object that looks like a bet.
QJsonObject findBetObject(const QJsonObject &json)
{
    QJsonObject container = json;
    if (json.value(QStringLiteral("data")).isObject())
        container = json.value(QStringLiteral("data")).toObject();

    for (auto it = container.constBegin(); it != container.constEnd(); ++it) {
        if (!it.value().isObject())
            continue;
        const QJsonObject o = it.value().toObject();
        if (o.contains(QStringLiteral("payoutMultiplier")) || o.contains(QStringLiteral("state")))
            return o;
    }
    return {};
}

LiveBetResult failed(const QString &error, const QString &type = {},
                     bool retryable = false, bool unknown = false)
{
    LiveBetResult r;
    r.ok = false;
    r.error = error;
    r.errorType = type;
    r.retryable = retryable;
    r.outcomeUnknown = unknown;
    return r;
}
} // namespace

StakeClient::StakeClient(QObject *parent)
    : QObject(parent)
{
}

void StakeClient::setTransport(IStakeTransport *transport)
{
    for (const QMetaObject::Connection &c : std::as_const(m_shuffleConnections))
        disconnect(c);
    m_shuffleConnections.clear();

    m_transport = transport;

    if (auto *shuffle = qobject_cast<ShuffleTransport *>(transport)) {
        shuffle->setWantedCurrency(m_currency);
        // Balances are pushed by Shuffle's websocket; hand them to the same signals the
        // Stake GraphQL query uses.
        m_shuffleConnections << connect(shuffle, &ShuffleTransport::balancesUpdated, this,
                                        &StakeClient::balancesReady);
        m_shuffleConnections << connect(shuffle, &ShuffleTransport::balancesFailed, this,
                                        &StakeClient::balancesFailed);
    }
}

bool StakeClient::isShuffle() const
{
    return qobject_cast<ShuffleTransport *>(m_transport) != nullptr;
}

void StakeClient::setCurrency(const QString &currency)
{
    m_currency = currency.trimmed().toLower();
    if (auto *shuffle = qobject_cast<ShuffleTransport *>(m_transport))
        shuffle->setWantedCurrency(m_currency);
}

QString StakeClient::randomIdentifier()
{
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    QString id;
    id.reserve(21);
    for (int i = 0; i < 21; ++i)
        id += QLatin1Char(alphabet[QRandomGenerator::global()->bounded(62)]);
    return id;
}

QString StakeClient::randomClientSeed(int length)
{
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    QString seed;
    seed.reserve(length);
    for (int i = 0; i < length; ++i)
        seed += QLatin1Char(alphabet[QRandomGenerator::system()->bounded(62)]);
    return seed;
}

void StakeClient::rotateSeedPair(const QString &clientSeed)
{
    if (isShuffle()) {
        rotateShuffleSeed(clientSeed);
        return;
    }

    QString why;
    if (!m_transport || !m_transport->isReady(&why)) {
        const QString msg = m_transport ? why : tr("No transport configured.");
        QTimer::singleShot(0, this, [this, msg] { emit seedRotateFailed(msg); });
        return;
    }

    QJsonObject variables;
    variables.insert(QStringLiteral("seed"), clientSeed);

    QJsonObject body;
    body.insert(QStringLiteral("operationName"), QStringLiteral("RotateSeedPair"));
    body.insert(QStringLiteral("variables"), variables);
    body.insert(QStringLiteral("query"), QStringLiteral(
        "mutation RotateSeedPair($seed: String!) {\n"
        "  rotateSeedPair(seed: $seed) {\n"
        "    clientSeed {\n"
        "      user {\n"
        "        id\n"
        "        activeClientSeed {\n"
        "          id\n"
        "          seed\n"
        "        }\n"
        "        activeServerSeed {\n"
        "          id\n"
        "          nonce\n"
        "          seedHash\n"
        "          nextSeedHash\n"
        "        }\n"
        "      }\n"
        "    }\n"
        "  }\n"
        "}"));

    m_transport->post(QStringLiteral("_api/graphql"), body, [this, clientSeed](const TransportReply &reply) {
        if (!reply.ok) {
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit seedRotateFailed(reply.error);
            return;
        }
        QString type;
        const QString apiError = firstErrorText(reply.json, &type);
        if (!apiError.isEmpty()) {
            emit seedRotateFailed(apiError);
            return;
        }

        const QJsonObject user = reply.json.value(QStringLiteral("data")).toObject()
                                     .value(QStringLiteral("rotateSeedPair")).toObject()
                                     .value(QStringLiteral("clientSeed")).toObject()
                                     .value(QStringLiteral("user")).toObject();
        if (user.isEmpty()) {
            emit seedRotateFailed(tr("Unexpected seed rotation response."));
            return;
        }

        const QString activeSeed = user.value(QStringLiteral("activeClientSeed")).toObject()
                                       .value(QStringLiteral("seed")).toString();
        const QJsonObject server = user.value(QStringLiteral("activeServerSeed")).toObject();
        emit seedRotated(activeSeed.isEmpty() ? clientSeed : activeSeed,
                         qint64(numberOf(server.value(QStringLiteral("nonce")))),
                         server.value(QStringLiteral("seedHash")).toString(),
                         server.value(QStringLiteral("nextSeedHash")).toString());
    });
}

// Shuffle: ChangeGameSeed mutation (gameSeedChangeAndReveal) - reveals the old server
// seed and starts a new seed pair with `clientSeed` (POST https://<site>/main-api/graphql/api/graphql).
// The reply is a list of seeds: the ACTIVE one (new client seed, nonce 0), the NEXT one
// and the revealed old one.
void StakeClient::rotateShuffleSeed(const QString &clientSeed)
{
    QString why;
    if (!m_transport || !m_transport->isReady(&why)) {
        const QString msg = m_transport ? why : tr("No transport configured.");
        QTimer::singleShot(0, this, [this, msg] { emit seedRotateFailed(msg); });
        return;
    }

    QJsonObject variables;
    variables.insert(QStringLiteral("clientSeed"), clientSeed);

    QJsonObject body;
    body.insert(QStringLiteral("operationName"), QStringLiteral("ChangeGameSeed"));
    body.insert(QStringLiteral("variables"), variables);
    QJsonObject clientLibrary;
    clientLibrary.insert(QStringLiteral("name"), QStringLiteral("@apollo/client"));
    clientLibrary.insert(QStringLiteral("version"), QStringLiteral("4.2.9"));
    QJsonObject extensions;
    extensions.insert(QStringLiteral("clientLibrary"), clientLibrary);
    body.insert(QStringLiteral("extensions"), extensions);
    body.insert(QStringLiteral("query"), QStringLiteral(
        "mutation ChangeGameSeed($clientSeed: String!) {\n"
        "  gameSeedChangeAndReveal(newClientSeed: $clientSeed) {\n"
        "    ...GameSeedFields\n"
        "    __typename\n"
        "  }\n"
        "}\n"
        "\n"
        "fragment GameSeedFields on GameSeed {\n"
        "  id\n"
        "  clientSeed\n"
        "  seed\n"
        "  hashedSeed\n"
        "  status\n"
        "  currentNonce\n"
        "  createdAt\n"
        "  __typename\n"
        "}"));

    m_transport->post(QStringLiteral("main-api/graphql/api/graphql"), body,
                      [this, clientSeed](const TransportReply &reply) {
        if (!reply.ok) {
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit seedRotateFailed(reply.error);
            return;
        }
        QString type;
        const QString apiError = firstErrorText(reply.json, &type);
        if (!apiError.isEmpty()) {
            emit seedRotateFailed(apiError);
            return;
        }
        const QJsonValue result = reply.json.value(QStringLiteral("data")).toObject()
                                      .value(QStringLiteral("gameSeedChangeAndReveal"));
        QJsonArray seeds;
        if (result.isArray())
            seeds = result.toArray();
        else if (result.isObject())
            seeds.append(result);
        if (seeds.isEmpty()) {
            emit seedRotateFailed(tr("Unexpected seed rotation response."));
            return;
        }

        QString activeClient = clientSeed;
        QString activeHash;
        QString nextHash;
        qint64 nonce = 0;
        for (const QJsonValue &v : std::as_const(seeds)) {
            const QJsonObject o = v.toObject();
            const QString status = o.value(QStringLiteral("status")).toString();
            if (status == QLatin1String("ACTIVE")) {
                const QString cs = o.value(QStringLiteral("clientSeed")).toString();
                if (!cs.isEmpty())
                    activeClient = cs;
                activeHash = o.value(QStringLiteral("hashedSeed")).toString();
                nonce = qint64(numberOf(o.value(QStringLiteral("currentNonce"))));
            } else if (status == QLatin1String("NEXT")) {
                nextHash = o.value(QStringLiteral("hashedSeed")).toString();
            }
        }
        emit seedRotated(activeClient, nonce, activeHash, nextHash);
    });
}

// Shuffle: VaultDeposit mutation (POST https://<site>/main-api/graphql/api/graphql).
void StakeClient::vaultDeposit(double amount)
{
    if (!isShuffle()) {
        stakeVaultDeposit(amount);
        return;
    }
    QString why;
    if (!m_transport || !m_transport->isReady(&why)) {
        const QString msg = m_transport ? why : tr("No transport configured.");
        QTimer::singleShot(0, this, [this, msg] { emit vaultFailed(msg); });
        return;
    }
    if (!(amount > 0.0)) {
        QTimer::singleShot(0, this, [this] { emit vaultFailed(tr("Please specify vault amount.")); });
        return;
    }

    QJsonObject data;
    data.insert(QStringLiteral("currency"), m_currency.toUpper());
    data.insert(QStringLiteral("amount"), QString::number(amount, 'f', 8));

    QJsonObject variables;
    variables.insert(QStringLiteral("data"), data);

    QJsonObject body;
    body.insert(QStringLiteral("operationName"), QStringLiteral("VaultDeposit"));
    body.insert(QStringLiteral("variables"), variables);
    QJsonObject clientLibrary;
    clientLibrary.insert(QStringLiteral("name"), QStringLiteral("@apollo/client"));
    clientLibrary.insert(QStringLiteral("version"), QStringLiteral("4.2.9"));
    QJsonObject extensions;
    extensions.insert(QStringLiteral("clientLibrary"), clientLibrary);
    body.insert(QStringLiteral("extensions"), extensions);
    body.insert(QStringLiteral("query"), QStringLiteral(
        "mutation VaultDeposit($data: VaultDepositInput!) {\n"
        "  vaultDeposit(data: $data) {\n"
        "    id\n"
        "    type\n"
        "    currency\n"
        "    amount\n"
        "    createdAt\n"
        "    afterVaultBalance\n"
        "    __typename\n"
        "  }\n"
        "}"));

    m_transport->post(QStringLiteral("main-api/graphql/api/graphql"), body, [this, amount](const TransportReply &reply) {
        if (!reply.ok) {
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit vaultFailed(reply.error);
            return;
        }
        QString type;
        const QString apiError = firstErrorText(reply.json, &type);
        if (!apiError.isEmpty()) {
            emit vaultFailed(apiError);
            return;
        }
        const QJsonObject dep = reply.json.value(QStringLiteral("data")).toObject()
                                    .value(QStringLiteral("vaultDeposit")).toObject();
        if (dep.isEmpty()) {
            emit vaultFailed(tr("Unexpected vault response."));
            return;
        }
        const double done = dep.contains(QStringLiteral("amount"))
                                ? numberOf(dep.value(QStringLiteral("amount"))) : amount;
        emit vaultDeposited(done, dep.value(QStringLiteral("currency")).toString());
    });
}

// Stake: CreateVaultDeposit mutation (POST https://<site>/_api/v1/graphql). The amount is
// a Float and the currency a lowercase code ("usdc").
void StakeClient::stakeVaultDeposit(double amount)
{
    QString why;
    if (!m_transport || !m_transport->isReady(&why)) {
        const QString msg = m_transport ? why : tr("No transport configured.");
        QTimer::singleShot(0, this, [this, msg] { emit vaultFailed(msg); });
        return;
    }
    if (!(amount > 0.0)) {
        QTimer::singleShot(0, this, [this] { emit vaultFailed(tr("Please specify vault amount.")); });
        return;
    }

    QJsonObject variables;
    variables.insert(QStringLiteral("currency"), m_currency.toLower());
    variables.insert(QStringLiteral("amount"), amount);

    QJsonObject body;
    body.insert(QStringLiteral("operationName"), QStringLiteral("CreateVaultDeposit"));
    body.insert(QStringLiteral("variables"), variables);
    body.insert(QStringLiteral("query"), QStringLiteral(
        "mutation CreateVaultDeposit($currency: CurrencyEnum!, $amount: Float!) {\n"
        "  createVaultDeposit(currency: $currency, amount: $amount) {\n"
        "    id\n"
        "    amount\n"
        "    currency\n"
        "    __typename\n"
        "  }\n"
        "}"));

    m_transport->post(QStringLiteral("_api/v1/graphql"), body, [this, amount](const TransportReply &reply) {
        if (!reply.ok) {
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit vaultFailed(reply.error);
            return;
        }
        QString type;
        const QString apiError = firstErrorText(reply.json, &type);
        if (!apiError.isEmpty()) {
            emit vaultFailed(apiError);
            return;
        }
        const QJsonObject dep = reply.json.value(QStringLiteral("data")).toObject()
                                    .value(QStringLiteral("createVaultDeposit")).toObject();
        if (dep.isEmpty()) {
            emit vaultFailed(tr("Unexpected vault response."));
            return;
        }
        const double done = dep.contains(QStringLiteral("amount"))
                                ? numberOf(dep.value(QStringLiteral("amount"))) : amount;
        emit vaultDeposited(done, dep.value(QStringLiteral("currency")).toString().toUpper());
    });
}

void StakeClient::fetchBalances()
{
    QString why;
    if (!m_transport || !m_transport->isReady(&why)) {
        const QString msg = m_transport ? why : tr("No transport configured.");
        QTimer::singleShot(0, this, [this, msg] { emit balancesFailed(msg); });
        return;
    }

    // Shuffle: connect / reuse the balance websocket; results arrive through the
    // transport's signals (see setTransport()).
    if (auto *shuffle = qobject_cast<ShuffleTransport *>(m_transport)) {
        shuffle->requestBalances();
        return;
    }

    QJsonObject body;
    body.insert(QStringLiteral("operationName"), QStringLiteral("UserBalances"));
    body.insert(QStringLiteral("variables"), QJsonObject());
    body.insert(QStringLiteral("query"),
                QStringLiteral("query UserBalances{user{balances{available{amount currency}}}}"));

    m_transport->post(QStringLiteral("_api/graphql"), body, [this](const TransportReply &reply) {
        if (!reply.ok) {
			if (reply.status == 0) {
                emit sessionLost(reply.error);
            }
            emit balancesFailed(reply.error);
            return;
        }
        QString type;
        const QString apiError = firstErrorText(reply.json, &type);
        if (!apiError.isEmpty()) {
            emit balancesFailed(apiError);
            return;
        }

        const QJsonValue balsVal = reply.json.value(QStringLiteral("data")).toObject()
                                       .value(QStringLiteral("user")).toObject()
                                       .value(QStringLiteral("balances"));
        if (!balsVal.isArray()) {
            emit balancesFailed(tr("Login failed: unexpected response (is the API key valid?)"));
            return;
        }

        QMap<QString, double> balances;
        const QJsonArray arr = balsVal.toArray();
        for (const QJsonValue &v : arr) {
            const QJsonObject avail = v.toObject().value(QStringLiteral("available")).toObject();
            const QString cur = avail.value(QStringLiteral("currency")).toString().toLower();
            if (!cur.isEmpty())
                balances.insert(cur, numberOf(avail.value(QStringLiteral("amount"))));
        }
        emit balancesReady(balances);
    });
}

void StakeClient::lookupBetIid(const QString &betId, std::function<void(const QString &)> done)
{
    QString why;
    if (betId.isEmpty() || !m_transport || isShuffle() || !m_transport->isReady(&why)) {
        QTimer::singleShot(0, this, [done] { done(QString()); });
        return;
    }

    QJsonObject vars;
    vars.insert(QStringLiteral("betId"), betId);

    QJsonObject body;
    body.insert(QStringLiteral("operationName"), QStringLiteral("BetLookup"));
    body.insert(QStringLiteral("variables"), vars);
    body.insert(QStringLiteral("query"),
                QStringLiteral("query BetLookup($betId:String){bet(betId:$betId){iid}}"));

    m_transport->post(QStringLiteral("_api/graphql"), body, [done](const TransportReply &reply) {
        if (!reply.ok) {
            done(QString());
            return;
        }
        const QString iid = reply.json.value(QStringLiteral("data")).toObject()
                                .value(QStringLiteral("bet")).toObject()
                                .value(QStringLiteral("iid")).toString();
        done(iid);
    });
}

void StakeClient::placeDice(const DiceBetRequest &request)
{
    QString why;
    if (!m_transport || !m_transport->isReady(&why)) {
        const LiveBetResult r = failed(m_transport ? why : tr("No transport configured."));
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }
    if (m_currency.isEmpty()) {
        const LiveBetResult r = failed(tr("No currency selected — log in and pick one in Options."));
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }

    if (isShuffle()) {
        placeShuffleDice(request);
        return;
    }

    const double target = std::round(request.target * 100.0) / 100.0;

    QJsonObject body;
    body.insert(QStringLiteral("target"), target);
    body.insert(QStringLiteral("condition"),
                request.over ? QStringLiteral("above") : QStringLiteral("below"));
    body.insert(QStringLiteral("identifier"), randomIdentifier());
    body.insert(QStringLiteral("amount"), request.amount);
    body.insert(QStringLiteral("currency"), m_currency);

    postBet(QStringLiteral("_api/casino/dice/roll"), body, request.amount);
}

void StakeClient::placeLimbo(const LimboBetRequest &request)
{
    QString why;
    if (!m_transport || !m_transport->isReady(&why)) {
        const LiveBetResult r = failed(m_transport ? why : tr("No transport configured."));
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }
    if (m_currency.isEmpty()) {
        const LiveBetResult r = failed(tr("No currency selected — log in and pick one in Options."));
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }

    if (isShuffle()) {
        const LiveBetResult r = failed(tr("Limbo is not supported for Shuffle yet (dice only)."));
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }

    const double target = std::round(request.multiplierTarget * 100.0) / 100.0;

    QJsonObject body;
    body.insert(QStringLiteral("multiplierTarget"), target);
    body.insert(QStringLiteral("identifier"), randomIdentifier());
    body.insert(QStringLiteral("amount"), request.amount);
    body.insert(QStringLiteral("currency"), m_currency);

    postBet(QStringLiteral("_api/casino/limbo/bet"), body, request.amount);
}

// Shared by dice and limbo: both answer with {"<game>Bet"/"diceRoll": {...}}.
void StakeClient::postBet(const QString &endpoint, const QJsonObject &body, double requestedAmount)
{
    m_transport->post(endpoint, body, [this, requestedAmount](const TransportReply &reply) {
        if (!reply.ok) {
            // Treat timeouts (status 0) as retryable so the bot doesn't halt.
            // We only mark 5xx server errors as "unknown" to prevent halting on network blips.
            const bool isTimeout = (reply.status == 0);
            const bool unknown = (reply.status >= 500);
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit diceFinished(failed(reply.error, {}, isTimeout, unknown));
            return;
        }

        QString type;
        const QString apiError = firstErrorText(reply.json, &type);
        if (!apiError.isEmpty()) {
            LiveBetResult r = failed(apiError, type, isRetryable(type, apiError), false);
            r.retryDelayMs = fastRetryDelayMs(type, apiError);
            emit diceFinished(r);
            return;
        }

        const QJsonObject bet = findBetObject(reply.json);
        if (bet.isEmpty()) {
            emit diceFinished(failed(tr("Unexpected bet response (no bet object)."), {}, true, false));
            return;
        }

        LiveBetResult r;
        r.ok = true;
        r.amount = bet.contains(QStringLiteral("amount")) ? numberOf(bet.value(QStringLiteral("amount")))
                                                          : requestedAmount;
        r.multiplier = numberOf(bet.value(QStringLiteral("payoutMultiplier")));
        r.payout = numberOf(bet.value(QStringLiteral("payout")));
        r.roll = numberOf(bet.value(QStringLiteral("state")).toObject().value(QStringLiteral("result")));
        r.betId = bet.value(QStringLiteral("id")).toString();
        r.nonce = bet.contains(QStringLiteral("nonce"))
                      ? qint64(numberOf(bet.value(QStringLiteral("nonce")))) : -1;
        const bool active = bet.value(QStringLiteral("active")).toBool(false);
        r.win = !active && r.multiplier >= 1.0;
        emit diceFinished(r);
    });
}

// Shuffle dice: the DicePlay GraphQL mutation (same query the JavaScript bot uses).
// amount / bet are strings with 8 / 2 decimals, direction is BELOW / ABOVE.
void StakeClient::placeShuffleDice(const DiceBetRequest &request)
{
    const double target = std::round(request.target * 100.0) / 100.0;

    QJsonObject data;
    data.insert(QStringLiteral("amount"), QString::number(request.amount, 'f', 8));
    data.insert(QStringLiteral("bet"), QString::number(target, 'f', 2));
    data.insert(QStringLiteral("currency"), m_currency.toUpper());
    data.insert(QStringLiteral("direction"),
                request.over ? QStringLiteral("ABOVE") : QStringLiteral("BELOW"));

    QJsonObject variables;
    variables.insert(QStringLiteral("data"), data);

    QJsonObject body;
    body.insert(QStringLiteral("operationName"), QStringLiteral("DicePlay"));
    body.insert(QStringLiteral("variables"), variables);
    body.insert(QStringLiteral("query"), QStringLiteral(
        "mutation DicePlay($data: DicePlayInput!) {\n"
        "  dicePlay(data: $data) {\n"
        "    id\n"
        "    currency\n"
        "    amount\n"
        "    payout\n"
        "    afterBalance\n"
        "    shuffleOriginalActions {\n"
        "      id\n"
        "      action {\n"
        "        dice {\n"
        "          ...DiceFields\n"
        "          __typename\n"
        "        }\n"
        "        __typename\n"
        "      }\n"
        "      __typename\n"
        "    }\n"
        "    updatedAt\n"
        "    createdAt\n"
        "    __typename\n"
        "  }\n"
        "}\n"
        "\n"
        "fragment DiceFields on DiceActionModel {\n"
        "  userDiceDirection\n"
        "  userValue\n"
        "  resultValue\n"
        "  resultRaw\n"
        "  __typename\n"
        "}"));

    const double requestedAmount = request.amount;
    m_transport->post(QStringLiteral("main-api/graphql/api/graphql"), body,
                      [this, requestedAmount](const TransportReply &reply) {
        if (!reply.ok) {
            // Same policy as the Stake bets: a timeout is retried, a 5xx may have gone through.
            const bool isTimeout = (reply.status == 0);
            const bool unknown = (reply.status >= 500);
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit diceFinished(failed(reply.error, {}, isTimeout, unknown));
            return;
        }

        QString type;
        const QString apiError = firstErrorText(reply.json, &type);
        if (!apiError.isEmpty()) {
            LiveBetResult r = failed(apiError, type, isRetryable(type, apiError), false);
            r.retryDelayMs = fastRetryDelayMs(type, apiError);
            emit diceFinished(r);
            return;
        }

        const QJsonObject play = reply.json.value(QStringLiteral("data")).toObject()
                                     .value(QStringLiteral("dicePlay")).toObject();
        const QJsonArray actions = play.value(QStringLiteral("shuffleOriginalActions")).toArray();
        const QJsonObject dice = actions.isEmpty()
            ? QJsonObject()
            : actions.first().toObject().value(QStringLiteral("action")).toObject()
                  .value(QStringLiteral("dice")).toObject();
        if (play.isEmpty() || dice.isEmpty()) {
            emit diceFinished(failed(tr("Unexpected bet response (no dicePlay result)."), {}, true, false));
            return;
        }

        LiveBetResult r;
        r.ok = true;
        r.amount = play.contains(QStringLiteral("amount")) ? numberOf(play.value(QStringLiteral("amount")))
                                                           : requestedAmount;
        r.payout = numberOf(play.value(QStringLiteral("payout")));
        r.roll = numberOf(dice.value(QStringLiteral("resultValue")));
        r.betId = play.value(QStringLiteral("id")).toString();
        r.nonce = -1; // the dice mutation does not return the seed nonce

        const double userValue = numberOf(dice.value(QStringLiteral("userValue")));
        const bool above = dice.value(QStringLiteral("userDiceDirection")).toString()
                               .compare(QLatin1String("ABOVE"), Qt::CaseInsensitive) == 0;

        // The server's payout is the truth; for a 0 bet fall back to comparing the roll.
        r.win = r.amount > 0.0 ? r.payout > 0.0
                               : (above ? r.roll > userValue : r.roll < userValue);

        if (r.win) {
            const double chance = above ? 100.0 - userValue : userValue;
            r.multiplier = (r.amount > 0.0 && r.payout > 0.0)
                               ? r.payout / r.amount
                               : (chance > 0.0 ? 99.0 / chance : 0.0);
        }
        emit diceFinished(r);
    });
}
