#include "bot/StakeClient.h"

#include "bot/BotTypes.h"

#include "bot/DuelTransport.h"
#include "bot/GoatedTransport.h"
#include "bot/ParadiceTransport.h"
#include "bot/ShuffleTransport.h"
#include "bot/WolfbetTransport.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
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
    const bool parallel = (containsCI(type, "parallelCasinoBet") && !containsCI(type, "internalError"))
                          || containsCI(msg, "only one bet at a time")   // Wolfbet
                          || containsCI(msg, "concurrent requests");     // Goated
    const bool rateLimit = containsCI(msg, "slow down") || containsCI(msg, "slowdown") // Paradice: "slowdown_please"
                           || msg.contains(QLatin1String("429"))
                           || containsCI(msg, "too many attempts"); // Wolfbet / Laravel throttle
    const bool seedRotate = containsCI(msg, "new seed pair") || containsCI(msg, "choose a new seed")
                            || containsCI(msg, "seedRotate") || containsCI(type, "seedRotate")
                            || (containsCI(msg, "seed") && containsCI(msg, "rotat"));
    return parallel || rateLimit || seedRotate;
}

// Short retry delay for errors that clear immediately; 0 for rate limits (keep the
// engine's growing backoff there) and for everything else.
int fastRetryDelayMs(const QString &type, const QString &msg)
{
    const bool rateLimit = containsCI(msg, "slow down") || containsCI(msg, "slowdown") // Paradice: "slowdown_please"
                           || msg.contains(QLatin1String("429"))
                           || containsCI(msg, "too many attempts");
    if (rateLimit)
        return 0;
    if ((containsCI(type, "parallelCasinoBet") && !containsCI(type, "internalError"))
        || containsCI(msg, "only one bet at a time") || containsCI(msg, "concurrent requests"))
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
    if (auto *goated = qobject_cast<GoatedTransport *>(transport)) {
        // Goated: balances are read over HTTPS; same signals as the other casinos.
        m_shuffleConnections << connect(goated, &GoatedTransport::balancesUpdated, this,
                                        &StakeClient::balancesReady);
        m_shuffleConnections << connect(goated, &GoatedTransport::balancesFailed, this,
                                        &StakeClient::balancesFailed);
    }
    if (auto *wolfbet = qobject_cast<WolfbetTransport *>(transport)) {
        // Wolfbet: balances are read over HTTPS; same signals as the other casinos.
        m_shuffleConnections << connect(wolfbet, &WolfbetTransport::balancesUpdated, this,
                                        &StakeClient::balancesReady);
        m_shuffleConnections << connect(wolfbet, &WolfbetTransport::balancesFailed, this,
                                        &StakeClient::balancesFailed);
    }
    if (auto *paradice = qobject_cast<ParadiceTransport *>(transport)) {
        // Paradice: balances are read over HTTPS; same signals as the other casinos.
        m_shuffleConnections << connect(paradice, &ParadiceTransport::balancesUpdated, this,
                                        &StakeClient::balancesReady);
        m_shuffleConnections << connect(paradice, &ParadiceTransport::balancesFailed, this,
                                        &StakeClient::balancesFailed);
    }
    if (auto *duel = qobject_cast<DuelTransport *>(transport)) {
        // Duel: balances are read over HTTPS; same signals as the other casinos.
        m_shuffleConnections << connect(duel, &DuelTransport::balancesUpdated, this,
                                        &StakeClient::balancesReady);
        m_shuffleConnections << connect(duel, &DuelTransport::balancesFailed, this,
                                        &StakeClient::balancesFailed);
    }
}

bool StakeClient::isShuffle() const
{
    return qobject_cast<ShuffleTransport *>(m_transport) != nullptr;
}

bool StakeClient::isGoated() const
{
    return qobject_cast<GoatedTransport *>(m_transport) != nullptr;
}

bool StakeClient::isWolfbet() const
{
    return qobject_cast<WolfbetTransport *>(m_transport) != nullptr;
}

bool StakeClient::isDuel() const
{
    return qobject_cast<DuelTransport *>(m_transport) != nullptr;
}

bool StakeClient::isParadice() const
{
    return qobject_cast<ParadiceTransport *>(m_transport) != nullptr;
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
    if (isGoated()) {
        rotateGoatedSeed(clientSeed);
        return;
    }
    if (isWolfbet()) {
        rotateWolfbetSeed(); // the client seed cannot be chosen on Wolfbet
        return;
    }
    if (isDuel()) {
        rotateDuelSeed(clientSeed);
        return;
    }
    if (isParadice()) {
        rotateParadiceSeed(); // the client seed cannot be chosen on Paradice
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
    if (isGoated()) {
        goatedVaultDeposit(amount);
        return;
    }
    if (isWolfbet()) {
        QTimer::singleShot(0, this, [this] { emit vaultFailed(tr("Wolfbet: vault deposits are not supported.")); });
        return;
    }
    if (isDuel()) {
        QTimer::singleShot(0, this, [this] { emit vaultFailed(tr("Duel: vault deposits are not supported.")); });
        return;
    }
    if (isParadice()) {
        QTimer::singleShot(0, this, [this] { emit vaultFailed(tr("Paradice: vault deposits are not supported.")); });
        return;
    }
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
    data.insert(QStringLiteral("currency"), ShuffleTransport::apiCurrency(m_currency));
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
    // Goated: GET /user/wallet/balances; results arrive through the transport's signals.
    if (auto *goated = qobject_cast<GoatedTransport *>(m_transport)) {
        goated->requestBalances();
        return;
    }
    // Wolfbet: GET /api/v2/user/balances; results arrive through the transport's signals.
    if (auto *wolfbet = qobject_cast<WolfbetTransport *>(m_transport)) {
        wolfbet->requestBalances();
        return;
    }
    // Duel: GET /api/v2/metadata (user.balances); results arrive through the transport's signals.
    if (auto *duel = qobject_cast<DuelTransport *>(m_transport)) {
        duel->requestBalances();
        return;
    }
    // Paradice: GraphQL "me { wallets }"; results arrive through the transport's signals.
    if (auto *paradice = qobject_cast<ParadiceTransport *>(m_transport)) {
        paradice->requestBalances();
        return;
    }

    postBalanceQuery(m_transport, false);
}

void StakeClient::fetchBalancesVia(IStakeTransport *via)
{
    if (!via || via == m_transport || !via->isReady())
        return; // only for a Stake transport that is not the active one
    postBalanceQuery(via, true);
}

void StakeClient::postBalanceQuery(IStakeTransport *via, bool background)
{
    QJsonObject body;
    body.insert(QStringLiteral("operationName"), QStringLiteral("UserBalances"));
    body.insert(QStringLiteral("variables"), QJsonObject());
    body.insert(QStringLiteral("query"),
                QStringLiteral("query UserBalances{user{balances{available{amount currency}}}}"));

    auto fail = [this, background](const QString &error) {
        if (background)
            emit backgroundBalancesFailed(error);
        else
            emit balancesFailed(error);
    };

    via->post(QStringLiteral("_api/graphql"), body,
              [this, via, background, fail](const TransportReply &reply) {
        // Active fetch: the casino / transport was switched meanwhile -> stale answer.
        // Background fetch: the transport became the active one meanwhile -> its own
        // fetchBalances() covers it.
        if (background ? (via == m_transport) : (via != m_transport))
            return;
        if (!reply.ok) {
            if (!background && reply.status == 0) {
                emit sessionLost(reply.error);
            }
            fail(reply.error);
            return;
        }
        QString type;
        const QString apiError = firstErrorText(reply.json, &type);
        if (!apiError.isEmpty()) {
            fail(apiError);
            return;
        }

        const QJsonValue balsVal = reply.json.value(QStringLiteral("data")).toObject()
                                       .value(QStringLiteral("user")).toObject()
                                       .value(QStringLiteral("balances"));
        if (!balsVal.isArray()) {
            fail(tr("Login failed: unexpected response (is the API key valid?)"));
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
        if (background)
            emit backgroundBalancesReady(balances);
        else
            emit balancesReady(balances);
    });
}

void StakeClient::lookupBetIid(const QString &betId, std::function<void(const QString &)> done)
{
    QString why;
    if (betId.isEmpty() || !m_transport || isShuffle() || isGoated() || isWolfbet() || isDuel() || isParadice() || !m_transport->isReady(&why)) {
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
    if (isGoated()) {
        placeGoatedDice(request);
        return;
    }
    if (isWolfbet()) {
        placeWolfbetDice(request);
        return;
    }
    if (isDuel()) {
        placeDuelDice(request);
        return;
    }
    if (isParadice()) {
        placeParadiceDice(request);
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
        placeShuffleLimbo(request);
        return;
    }
    if (isGoated()) {
        placeGoatedLimbo(request);
        return;
    }
    if (isWolfbet()) {
        placeWolfbetLimbo(request);
        return;
    }
    if (isDuel()) {
        placeDuelLimbo(request);
        return;
    }
    if (isParadice()) {
        const LiveBetResult r = failed(tr("Paradice: Limbo is not supported (dice only)."));
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
    data.insert(QStringLiteral("currency"), ShuffleTransport::apiCurrency(m_currency));
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
        // Win only when the payout multiplier is >= 1x (a partial payout is a loss).
        r.win = r.amount > 0.0 ? BotConstants::isWinMultiplier(r.payout / r.amount)
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

// ---------------------------------------------------------------- Goated dice
namespace
{
// Looks `names` up in `o` and then, breadth-first, in nested objects (depth <= 3), because the
// bet may come wrapped ({"data": {"bet": {...}}}). Returns true and fills `out` when found.
bool findGoatedValue(const QJsonObject &o, const QStringList &names, QJsonValue *out, int depth = 0)
{
    for (const QString &n : names) {
        const QJsonValue v = o.value(n);
        if (!v.isUndefined() && !v.isNull() && !v.isObject() && !v.isArray()) {
            *out = v;
            return true;
        }
    }
    if (depth >= 3)
        return false;
    for (auto it = o.constBegin(); it != o.constEnd(); ++it) {
        if (it.value().isObject() && findGoatedValue(it.value().toObject(), names, out, depth + 1))
            return true;
    }
    return false;
}

bool goatedNumber(const QJsonObject &o, const QStringList &names, double *out)
{
    QJsonValue v;
    if (!findGoatedValue(o, names, &v))
        return false;
    if (v.isDouble()) {
        *out = v.toDouble();
        return true;
    }
    if (v.isString()) {
        bool ok = false;
        const double d = v.toString().toDouble(&ok);
        if (ok) {
            *out = d;
            return true;
        }
    }
    return false;
}

bool goatedBool(const QJsonObject &o, const QStringList &names, bool *out)
{
    QJsonValue v;
    if (!findGoatedValue(o, names, &v))
        return false;
    if (v.isBool()) {
        *out = v.toBool();
        return true;
    }
    if (v.isString()) {
        const QString s = v.toString().trimmed().toLower();
        if (s == QLatin1String("win") || s == QLatin1String("won") || s == QLatin1String("true")) {
            *out = true;
            return true;
        }
        if (s == QLatin1String("loss") || s == QLatin1String("lose") || s == QLatin1String("lost")
            || s == QLatin1String("false")) {
            *out = false;
            return true;
        }
    }
    return false;
}

// Error text of a Goated answer: "message" / "error" (string or object) / "errors":[...].
QString goatedErrorText(const QJsonObject &json)
{
    const QJsonValue err = json.value(QStringLiteral("error"));
    if (err.isString() && !err.toString().isEmpty())
        return err.toString();
    if (err.isObject()) {
        const QString m = err.toObject().value(QStringLiteral("message")).toString();
        if (!m.isEmpty())
            return m;
    }
    const QJsonArray errs = json.value(QStringLiteral("errors")).toArray();
    if (!errs.isEmpty()) {
        const QJsonValue first = errs.first();
        const QString m = first.isString() ? first.toString()
                                           : first.toObject().value(QStringLiteral("message")).toString();
        if (!m.isEmpty())
            return m;
    }
    return json.value(QStringLiteral("message")).toString();
}
} // namespace

// Goated dice: POST https://apis.goated.com/casino/dice/v2
//   {"user_value": 51, "roll_type": "OVER", "amount": 0.1, "currency": "USDC", "is_fiat": false}
// The answer's layout was not captured, so it is read defensively (several field names, and
// the bet may be wrapped in "data"). If no roll can be found the raw answer is logged.
void StakeClient::placeGoatedDice(const DiceBetRequest &request)
{
    const double target = std::round(request.target * 100.0) / 100.0;

    QJsonObject body;
    body.insert(QStringLiteral("user_value"), target);
    body.insert(QStringLiteral("roll_type"),
                request.over ? QStringLiteral("OVER") : QStringLiteral("UNDER"));
    body.insert(QStringLiteral("amount"), std::round(request.amount * 1e8) / 1e8);
    body.insert(QStringLiteral("currency"), m_currency.toUpper());
    body.insert(QStringLiteral("is_fiat"), false);

    const double requestedAmount = request.amount;
    const bool over = request.over;
    m_transport->post(QStringLiteral("casino/dice/v2"), body,
                      [this, requestedAmount, target, over](const TransportReply &reply) {
        if (!reply.ok) {
            const bool isTimeout = (reply.status == 0);
            const bool unknown = (reply.status >= 500);
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit diceFinished(failed(reply.error, {}, isTimeout, unknown));
            return;
        }

        const QJsonObject &json = reply.json;
        const QString errText = goatedErrorText(json);
        const bool successFalse = json.contains(QStringLiteral("success"))
                                  && !json.value(QStringLiteral("success")).toBool();
        if (successFalse || reply.status >= 400) {
            const QString msg = errText.isEmpty()
                ? tr("Goated bet rejected (HTTP %1).").arg(reply.status) : errText;
            LiveBetResult r = failed(msg, {}, isRetryable({}, msg), false);
            r.retryDelayMs = fastRetryDelayMs({}, msg);
            emit diceFinished(r);
            return;
        }

        // Rate limit notice injected by the transport (HTTP 429 with a non-JSON body).
        if (!errText.isEmpty() && isRetryable({}, errText)) {
            LiveBetResult r = failed(errText, {}, true, false);
            r.retryDelayMs = fastRetryDelayMs({}, errText);
            emit diceFinished(r);
            return;
        }

        double roll = 0.0;
        const bool haveRoll = goatedNumber(json,
            {QStringLiteral("roll"), QStringLiteral("roll_result"), QStringLiteral("roll_value"),
             QStringLiteral("rolled"), QStringLiteral("result_value"), QStringLiteral("result"),
             QStringLiteral("number"), QStringLiteral("outcome")}, &roll);
        if (!haveRoll) {
            const QString raw = QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)).left(400);
            // The bet may or may not have been placed: do not retry blindly.
            emit diceFinished(failed(errText.isEmpty()
                                         ? tr("Unexpected Goated bet response (no roll found): %1").arg(raw)
                                         : errText,
                                     {}, false, true));
            return;
        }

        double amount = requestedAmount;
        goatedNumber(json, {QStringLiteral("amount"), QStringLiteral("bet_amount"),
                            QStringLiteral("wager")}, &amount);
        double payout = 0.0;
        const bool havePayout = goatedNumber(json,
            {QStringLiteral("payout"), QStringLiteral("win_amount"), QStringLiteral("payout_amount"),
             QStringLiteral("winnings"), QStringLiteral("profit_amount")}, &payout);
        double multiplier = 0.0;
        const bool haveMult = goatedNumber(json,
            {QStringLiteral("multiplier"), QStringLiteral("payout_multiplier"),
             QStringLiteral("payoutMultiplier")}, &multiplier);

        bool win = false;
        if (!goatedBool(json, {QStringLiteral("win"), QStringLiteral("won"), QStringLiteral("is_win"),
                               QStringLiteral("isWin"), QStringLiteral("is_winner"),
                               QStringLiteral("result_type"), QStringLiteral("outcome")}, &win)) {
            if (amount > 0.0 && havePayout)
                win = BotConstants::isWinMultiplier(payout / amount);
            else
                win = over ? roll > target : roll < target;
        }

        // A server "win" flag on a partial payout still is a loss: only >= 1x counts.
        if (win && amount > 0.0 && havePayout)
            win = BotConstants::isWinMultiplier(payout / amount);

        LiveBetResult r;
        r.ok = true;
        r.amount = amount;
        r.roll = roll;
        r.win = win;
        r.payout = havePayout ? payout : 0.0;
        if (win) {
            const double chance = over ? 100.0 - target : target;
            r.multiplier = haveMult ? multiplier
                           : (amount > 0.0 && havePayout && payout > 0.0)
                                 ? payout / amount
                                 : (chance > 0.0 ? 99.0 / chance : 0.0);
            if (!havePayout)
                r.payout = amount * r.multiplier;
        }

        QJsonValue idv;
        if (findGoatedValue(json, {QStringLiteral("id"), QStringLiteral("bet_id"), QStringLiteral("betId"),
                                   QStringLiteral("_id")}, &idv))
            r.betId = idv.isString() ? idv.toString() : QString::number(qint64(idv.toDouble()));
        double nonce = -1;
        if (goatedNumber(json, {QStringLiteral("nonce")}, &nonce))
            r.nonce = qint64(nonce);
        emit diceFinished(r);
    });
}

// Wolfbet: GET https://wolfbet.com/api/v1/game/seed/refresh
// Answer: {"server_seed_hashed":"<hash of the new server seed>"}. The request has no client seed
// parameter, so only the server seed is rotated.
void StakeClient::rotateWolfbetSeed()
{
    auto *wolfbet = qobject_cast<WolfbetTransport *>(m_transport);
    QString why;
    if (!wolfbet || !wolfbet->isReady(&why)) {
        const QString msg = wolfbet ? why : tr("No transport configured.");
        QTimer::singleShot(0, this, [this, msg] { emit seedRotateFailed(msg); });
        return;
    }

    wolfbet->get(QStringLiteral("api/v1/game/seed/refresh"), [this](const TransportReply &reply) {
        if (!reply.ok) {
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit seedRotateFailed(reply.error);
            return;
        }
        const QString hash = reply.json.value(QStringLiteral("server_seed_hashed")).toString();
        if (hash.isEmpty()) {
            QString msg = goatedErrorText(reply.json);
            if (msg.isEmpty())
                msg = tr("Wolfbet seed rotation rejected (HTTP %1).").arg(reply.status);
            emit seedRotateFailed(msg);
            return;
        }
        emit seedRotated(QString(), -1, hash, QString()); // client seed stays as it is
    });
}

// Wolfbet dice: POST https://wolfbet.com/api/v1/dice/manual/play
//   {"currency":"doge","game":"dice","amount":"0.00000001","multiplier":"1.98","rule":"under",
//    "bet_value":"50","auto":1}
// Answer: {"bet":{"hash","nonce","currency","amount","profit" (net, negative on a loss),"multiplier",
//   "bet_value","rule","result_value" (the roll),"state":"win"|"loss",...},"userBalance":{...}}
// Errors are read defensively (the error layout was not captured).
void StakeClient::placeWolfbetDice(const DiceBetRequest &request)
{
    const double target = std::round(request.target * 100.0) / 100.0;
    const bool over = request.over;
    // Win chance in percent, as the script / panel means it (any value, 2 decimals).
    const double chance = std::round((over ? 100.0 - target : target) * 100.0) / 100.0;
    if (!(chance > 0.0) || chance >= 100.0) {
        const LiveBetResult r = failed(tr("Wolfbet: invalid win chance %1.").arg(chance));
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }

    // Wolfbet rolls 0.00 - 99.99: "under X" wins with X %, "over X" with 99.99 - X %. So the
    // value sent for "over" is 99.99 - chance (not 100 - chance), or the site answers
    // "Incorrect win chance given".
    const double betValue = over ? std::round((99.99 - chance) * 100.0) / 100.0 : chance;

    // Fair multiplier at a 1 % house edge (50 % -> 1.98), cut (never rounded up) to 4 decimals,
    // shown with at least 2 decimals like the site does.
    const double mult = std::floor(99.0 / chance * 10000.0 + 1e-9) / 10000.0;
    QString multText = QString::number(mult, 'f', 4);
    while (multText.endsWith(QLatin1Char('0')) && multText.size() - multText.indexOf(QLatin1Char('.')) > 3)
        multText.chop(1);
    // bet_value: no trailing zeros ("50", "50.49").
    QString targetText = QString::number(betValue, 'f', 2);
    while (targetText.endsWith(QLatin1Char('0')))
        targetText.chop(1);
    if (targetText.endsWith(QLatin1Char('.')))
        targetText.chop(1);

    QJsonObject body;
    body.insert(QStringLiteral("currency"), m_currency.toLower());
    body.insert(QStringLiteral("game"), QStringLiteral("dice"));
    body.insert(QStringLiteral("amount"), QString::number(request.amount, 'f', 8));
    body.insert(QStringLiteral("multiplier"), multText);
    body.insert(QStringLiteral("rule"), over ? QStringLiteral("over") : QStringLiteral("under"));
    body.insert(QStringLiteral("bet_value"), targetText);
    body.insert(QStringLiteral("auto"), 1);

    const double requestedAmount = request.amount;
    m_transport->post(QStringLiteral("api/v1/dice/manual/play"), body,
                      [this, requestedAmount, betValue, over, mult](const TransportReply &reply) {
        if (!reply.ok) {
            const bool isTimeout = (reply.status == 0);
            const bool unknown = (reply.status >= 500);
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit diceFinished(failed(reply.error, {}, isTimeout, unknown));
            return;
        }

        const QJsonObject &json = reply.json;
        const QJsonObject bet = json.value(QStringLiteral("bet")).toObject();
        if (bet.isEmpty()) {
            // No bet in the answer: an API error (or rate limit notice) - the bet was not placed.
            QString errText = goatedErrorText(json);
            if (errText.isEmpty())
                errText = tr("Wolfbet bet rejected (HTTP %1).").arg(reply.status);
            if (reply.status >= 400 || isRetryable({}, errText)) {
                LiveBetResult r = failed(errText, {}, isRetryable({}, errText), false);
                r.retryDelayMs = fastRetryDelayMs({}, errText);
                emit diceFinished(r);
                return;
            }
            const QString raw = QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)).left(400);
            // The bet may or may not have been placed: do not retry blindly.
            emit diceFinished(failed(tr("Unexpected Wolfbet bet response (no bet found): %1").arg(raw),
                                     {}, false, true));
            return;
        }

        if (!bet.contains(QStringLiteral("result_value"))) {
            const QString raw = QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)).left(400);
            emit diceFinished(failed(tr("Unexpected Wolfbet bet response (no roll found): %1").arg(raw),
                                     {}, false, true));
            return;
        }

        LiveBetResult r;
        r.ok = true;
        r.amount = bet.contains(QStringLiteral("amount")) ? numberOf(bet.value(QStringLiteral("amount")))
                                                          : requestedAmount;
        r.roll = numberOf(bet.value(QStringLiteral("result_value")));

        const QString state = bet.value(QStringLiteral("state")).toString().trimmed().toLower();
        const double profit = numberOf(bet.value(QStringLiteral("profit")));
        // Win only when the payout multiplier is >= 1x: with a stake, net profit >= 0;
        // the server's "state" is only used for a 0 bet (where profit is always 0).
        if (r.amount > 0.0 && bet.contains(QStringLiteral("profit")))
            r.win = BotConstants::isWinMultiplier((r.amount + profit) / r.amount);
        else if (!state.isEmpty())
            r.win = (state == QLatin1String("win"));
        else
            r.win = over ? r.roll > betValue : r.roll < betValue;

        if (r.win) {
            const double betMult = numberOf(bet.value(QStringLiteral("multiplier")));
            r.multiplier = betMult > 0.0 ? betMult : mult;
            // "profit" is the net win; the payout (stake included) is what the other casinos report.
            r.payout = r.amount * r.multiplier;
        }
        // The server's net profit is the truth for the payout (stake included), also for a
        // 1x push or a partial payout that the >= 1x rule above classed as a loss.
        if (r.amount > 0.0 && bet.contains(QStringLiteral("profit")))
            r.payout = std::max(0.0, r.amount + profit);

        r.betId = bet.value(QStringLiteral("hash")).toString();
        r.nonce = bet.contains(QStringLiteral("nonce")) ? qint64(numberOf(bet.value(QStringLiteral("nonce")))) : -1;
        emit diceFinished(r);
    });
}

// Duel: POST https://duel.com/api/v2/client-seed/rotate {"client_seed":"R7mWIxg9Qf30VSJY"}
// Answer: {"success":true,"data":{"client_seed":"R7mWIxg9Qf30VSJY","server_seed_hashed":"031f...",
//   "next_server_seed_hash":"53b1...","nonce":0}}
void StakeClient::rotateDuelSeed(const QString &clientSeed)
{
    QString why;
    if (!m_transport || !m_transport->isReady(&why)) {
        const QString msg = m_transport ? why : tr("No transport configured.");
        QTimer::singleShot(0, this, [this, msg] { emit seedRotateFailed(msg); });
        return;
    }

    QJsonObject body;
    body.insert(QStringLiteral("client_seed"), clientSeed);

    m_transport->post(QStringLiteral("api/v2/client-seed/rotate"), body,
                      [this, clientSeed](const TransportReply &reply) {
        if (!reply.ok) {
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit seedRotateFailed(reply.error);
            return;
        }
        const QJsonObject &json = reply.json;
        QJsonObject data = json.value(QStringLiteral("data")).toObject();
        if (data.isEmpty())
            data = json; // also accept the fields at the top level
        const bool success = !json.contains(QStringLiteral("success"))
                             || json.value(QStringLiteral("success")).toBool();
        const QString hash = data.value(QStringLiteral("server_seed_hashed")).toString();
        if (!success || reply.status >= 400 || (hash.isEmpty() && !data.contains(QStringLiteral("client_seed")))) {
            QString msg = goatedErrorText(json);
            if (msg.isEmpty())
                msg = tr("Duel seed rotation rejected (HTTP %1).").arg(reply.status);
            emit seedRotateFailed(msg);
            return;
        }
        QString activeClient = data.value(QStringLiteral("client_seed")).toString();
        if (activeClient.isEmpty())
            activeClient = clientSeed;
        const qint64 nonce = data.contains(QStringLiteral("nonce"))
                                 ? qint64(numberOf(data.value(QStringLiteral("nonce")))) : 0;
        emit seedRotated(activeClient, nonce, hash,
                         data.value(QStringLiteral("next_server_seed_hash")).toString());
    });
}

// Paradice: mutation updateServerSeed (no arguments: only the server seed is renewed).
// Answer: {"data":{"updateServerSeed":{"id","serverSeed" (hash of the new active seed),
//   "serverSeedNonce" (0),"clientSeed" (unchanged),"serverSeedNext" (hash of the following one)}}}
void StakeClient::rotateParadiceSeed()
{
    QString why;
    if (!m_transport || !m_transport->isReady(&why)) {
        const QString msg = m_transport ? why : tr("No transport configured.");
        QTimer::singleShot(0, this, [this, msg] { emit seedRotateFailed(msg); });
        return;
    }

    QJsonObject body;
    body.insert(QStringLiteral("operationName"), QStringLiteral("updateServerSeed"));
    body.insert(QStringLiteral("variables"), QJsonObject());
    body.insert(QStringLiteral("query"), QStringLiteral(
        "mutation updateServerSeed {\n  updateServerSeed {\n    id\n    serverSeed\n"
        "    serverSeedNonce\n    clientSeed\n    serverSeedNext\n    __typename\n  }\n}\n"));

    m_transport->post(QString(), body, [this](const TransportReply &reply) {
        if (!reply.ok) {
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit seedRotateFailed(reply.error);
            return;
        }
        const QJsonObject data = reply.json.value(QStringLiteral("data")).toObject()
                                     .value(QStringLiteral("updateServerSeed")).toObject();
        const QString hash = data.value(QStringLiteral("serverSeed")).toString();
        if (data.isEmpty() || hash.isEmpty()) {
            QString msg = firstErrorText(reply.json, nullptr);
            if (msg.isEmpty())
                msg = tr("Paradice seed rotation rejected (HTTP %1).").arg(reply.status);
            emit seedRotateFailed(msg);
            return;
        }
        const qint64 nonce = data.contains(QStringLiteral("serverSeedNonce"))
                                 ? qint64(numberOf(data.value(QStringLiteral("serverSeedNonce")))) : 0;
        emit seedRotated(data.value(QStringLiteral("clientSeed")).toString(), nonce, hash,
                         data.value(QStringLiteral("serverSeedNext")).toString());
    });
}

// Paradice dice: POST https://api.paradice.in/api.php (GraphQL)
//   mutation rollDice($number: Float!, $betAmount: Float!, $side: RollSideEnum!, $currency: CurrencyEnum!)
//   variables: {"betAmount":"0.00000001","number":"50.00","side":"ABOVE"|"BELOW","currency":"BTC"}
// The roll runs 0.00 - 100.00; "ABOVE 50" wins with 50 % (same meaning as the app's target).
// Answer: {"data":{"rollDice":{"id","number","roll" (string),"rollSide","win" (bool),"betAmount",
//   "winAmount","currency","multiplier","chance",...}}}; errors come as {"errors":[{"message"}]}.
void StakeClient::placeParadiceDice(const DiceBetRequest &request)
{
    const double target = std::round(request.target * 100.0) / 100.0;
    const bool over = request.over;
    const double chance = over ? 100.0 - target : target;
    if (!(chance > 0.0) || chance >= 100.0) {
        const LiveBetResult r = failed(tr("Paradice: invalid target %1.").arg(target));
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }

    QJsonObject vars;
    vars.insert(QStringLiteral("betAmount"), QString::number(request.amount, 'f', 8));
    vars.insert(QStringLiteral("number"), QString::number(target, 'f', 2));
    vars.insert(QStringLiteral("side"), over ? QStringLiteral("ABOVE") : QStringLiteral("BELOW"));
    vars.insert(QStringLiteral("currency"), ParadiceTransport::apiCurrency(m_currency));

    QJsonObject body;
    body.insert(QStringLiteral("operationName"), QStringLiteral("rollDice"));
    body.insert(QStringLiteral("variables"), vars);
    body.insert(QStringLiteral("query"), QStringLiteral(
        "mutation rollDice($number: Float!, $betAmount: Float!, $side: RollSideEnum!, "
        "$currency: CurrencyEnum!) {\n"
        "  rollDice(\n    number: $number\n    betAmount: $betAmount\n    side: $side\n"
        "    currency: $currency\n  ) {\n"
        "    id\n    number\n    roll\n    rollSide\n    win\n    betAmount\n    winAmount\n"
        "    currency\n    multiplier\n    chance\n    game\n    __typename\n  }\n}\n"));

    const double requestedAmount = request.amount;
    m_transport->post(QString(), body,
                      [this, requestedAmount, target, over, chance](const TransportReply &reply) {
        if (!reply.ok) {
            const bool isTimeout = (reply.status == 0);
            const bool unknown = (reply.status >= 500);
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit diceFinished(failed(reply.error, {}, isTimeout, unknown));
            return;
        }

        const QJsonObject &json = reply.json;
        const QJsonObject bet = json.value(QStringLiteral("data")).toObject()
                                    .value(QStringLiteral("rollDice")).toObject();
        if (bet.isEmpty()) {
            // No bet in the answer: a GraphQL error (or rate limit notice) - the bet was not placed.
            QString type;
            QString errText = firstErrorText(json, &type);
            if (errText.isEmpty())
                errText = tr("Paradice bet rejected (HTTP %1).").arg(reply.status);
            if (reply.status >= 400 || json.contains(QStringLiteral("errors")) || isRetryable(type, errText)) {
                LiveBetResult r = failed(errText, type, isRetryable(type, errText), false);
                r.retryDelayMs = fastRetryDelayMs(type, errText);
                // "slowdown_please": the bet was not placed - keep betting without the 5 s wait.
                r.retryNow = containsCI(errText, "slowdown");
                emit diceFinished(r);
                return;
            }
            const QString raw = QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)).left(400);
            emit diceFinished(failed(tr("Unexpected Paradice bet response (no bet found): %1").arg(raw),
                                     {}, false, true));
            return;
        }
        if (!bet.contains(QStringLiteral("roll"))) {
            const QString raw = QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)).left(400);
            emit diceFinished(failed(tr("Unexpected Paradice bet response (no roll found): %1").arg(raw),
                                     {}, false, true));
            return;
        }

        LiveBetResult r;
        r.ok = true;
        r.amount = bet.contains(QStringLiteral("betAmount")) ? numberOf(bet.value(QStringLiteral("betAmount")))
                                                             : requestedAmount;
        r.roll = numberOf(bet.value(QStringLiteral("roll")));
        if (bet.value(QStringLiteral("win")).isBool())
            r.win = bet.value(QStringLiteral("win")).toBool();
        else
            r.win = over ? r.roll > target : r.roll < target;

        double mult = numberOf(bet.value(QStringLiteral("multiplier")));
        if (!(mult > 0.0))
            mult = std::floor(99.0 / chance * 10000.0 + 1e-9) / 10000.0; // 1 % house edge
        if (r.win) {
            r.multiplier = mult;
            r.payout = r.amount * mult;
            // "winAmount" may be the payout (stake included) or the net profit: take whichever
            // it is closer to, so the server's figure decides.
            const double winAmount = numberOf(bet.value(QStringLiteral("winAmount")));
            if (r.amount > 0.0 && winAmount > 0.0) {
                const double profit = r.amount * (mult - 1.0);
                r.payout = std::abs(winAmount - r.payout) <= std::abs(winAmount - profit)
                               ? winAmount : r.amount + winAmount;
            }
        }

        r.betId = bet.value(QStringLiteral("id")).toString();
        r.nonce = -1; // not part of the answer
        emit diceFinished(r);
    });
}

// Duel dice: POST https://duel.com/api/v2/dice/bet
//   {"amount":"0","bet_type":"over","currency":102,"security_token":"<token>","target":"5005"}
// The roll runs 0 - 10000 (shown as 0.00 - 100.00), so the target is sent in hundredths:
// "over 50.05" -> "5005". The currency is Duel's number (101 = btc, 102 = bch, ...).
// Answer: {"data":{"round":{"id","result" (the roll, in hundredths),"is_win","amount_currency"
//   (the stake),"amount_won" (the payout, stake included, 0 on a loss),"bet_type","target",
//   "win_chance","multiplier","nonce","client_seed",...}}} (also accepted without "data").
// The security token expires every 600 s: the transport renews it before that, and a bet the
// server rejects for an invalid / expired token is sent once more with a fresh one.
void StakeClient::placeDuelDice(const DiceBetRequest &request)
{
    const int currencyId = DuelTransport::currencyId(m_currency);
    if (currencyId == 0) {
        const LiveBetResult r = failed(tr("Duel: unknown currency \"%1\" (use one of: %2).")
                                           .arg(m_currency,
                                                DuelTransport::currencyCodes().join(QStringLiteral(", "))));
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }

    const qint64 target = qint64(std::llround(request.target * 100.0));
    if (target <= 0 || target >= 10000) {
        const LiveBetResult r = failed(tr("Duel: invalid target %1.").arg(request.target, 0, 'f', 2));
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }

    // Amount as a plain decimal string without trailing zeros ("0", "0.00012"), like the site.
    QString amountText = QString::number(std::max(0.0, request.amount), 'f', 8);
    while (amountText.contains(QLatin1Char('.')) && amountText.endsWith(QLatin1Char('0')))
        amountText.chop(1);
    if (amountText.endsWith(QLatin1Char('.')))
        amountText.chop(1);

    QJsonObject fields;
    fields.insert(QStringLiteral("amount"), amountText);
    fields.insert(QStringLiteral("bet_type"), request.over ? QStringLiteral("over") : QStringLiteral("under"));
    fields.insert(QStringLiteral("currency"), currencyId);
    fields.insert(QStringLiteral("target"), QString::number(target));
    sendDuelBet(QStringLiteral("api/v2/dice/bet"), fields, request.amount, false,
                double(target) / 100.0, request.over, false);
}

// Duel limbo: POST https://duel.com/api/v2/limbo/bet
//   {"amount":"0.0114738","target":"2.00","currency":106,"security_token":"<token>","instant":false}
// Answer: {"success":true,"data":{"round":{"id","result":"5.388335" (the multiplier rolled),"is_win",
//   "amount_currency" (the stake),"amount_won" (the payout, stake included),"target","win_chance",
//   "multiplier","nonce",...}}}
void StakeClient::placeDuelLimbo(const LimboBetRequest &request)
{
    const int currencyId = DuelTransport::currencyId(m_currency);
    if (currencyId == 0) {
        const LiveBetResult r = failed(tr("Duel: unknown currency \"%1\" (use one of: %2).")
                                           .arg(m_currency,
                                                DuelTransport::currencyCodes().join(QStringLiteral(", "))));
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }
    const double target = std::round(request.multiplierTarget * 100.0) / 100.0;
    if (!(target > 1.0)) {
        const LiveBetResult r = failed(tr("Duel: invalid Limbo multiplier %1.").arg(target));
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }

    QString amountText = QString::number(std::max(0.0, request.amount), 'f', 8);
    while (amountText.contains(QLatin1Char('.')) && amountText.endsWith(QLatin1Char('0')))
        amountText.chop(1);
    if (amountText.endsWith(QLatin1Char('.')))
        amountText.chop(1);

    QJsonObject fields;
    fields.insert(QStringLiteral("amount"), amountText);
    fields.insert(QStringLiteral("target"), QString::number(target, 'f', 2)); // "2.00", like the site
    fields.insert(QStringLiteral("currency"), currencyId);
    fields.insert(QStringLiteral("instant"), false);
    sendDuelBet(QStringLiteral("api/v2/limbo/bet"), fields, request.amount, true, target, false, false);
}

namespace
{
// The bet was refused because the security token is missing, invalid or expired.
bool isDuelTokenError(int status, const QString &text)
{
    if (status == 419)
        return true;
    const bool mentionsToken = containsCI(text, "security") || containsCI(text, "token");
    const bool tokenProblem = containsCI(text, "expired") || containsCI(text, "invalid")
                              || containsCI(text, "required") || containsCI(text, "missing")
                              || containsCI(text, "not valid") || containsCI(text, "incorrect");
    return mentionsToken && (tokenProblem || status == 401 || status == 403 || status == 422);
}
} // namespace

void StakeClient::sendDuelBet(const QString &path, const QJsonObject &fields, double requestedAmount,
                              bool limbo, double target, bool over, bool tokenRetried)
{
    auto *duel = qobject_cast<DuelTransport *>(m_transport);
    if (!duel) {
        const LiveBetResult r = failed(tr("No transport configured."));
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }
    QPointer<DuelTransport> guard(duel);

    duel->withSecurityToken([this, guard, path, fields, requestedAmount, limbo, target, over,
                             tokenRetried](const QString &token, const QString &tokenError, int tokenStatus) {
        if (!guard || guard.data() != m_transport) {
            emit diceFinished(failed(tr("Duel: the casino was switched before the bet was sent.")));
            return;
        }
        if (token.isEmpty()) {
            // No token, so no bet was sent: safe to retry when it was a network / rate limit problem.
            const bool retry = tokenStatus == 0 || tokenStatus == 429 || tokenStatus >= 500
                               || isRetryable({}, tokenError);
            if (tokenStatus == 0)
                emit sessionLost(tokenError);
            LiveBetResult r = failed(tr("Duel: could not get a security token: %1").arg(tokenError),
                                     {}, retry, false);
            emit diceFinished(r);
            return;
        }

        QJsonObject body = fields;
        body.insert(QStringLiteral("security_token"), token);

        guard->post(path, body,
                    [this, guard, path, fields, requestedAmount, limbo, target, over,
                     tokenRetried](const TransportReply &reply) {
            // Security token expired / rejected: the bet was not placed. Get a new token and
            // send the same bet once more.
            const QString replyText = reply.ok ? goatedErrorText(reply.json) : reply.error;
            // {"data":{"round":{...}}} (also accepted: {"round":{...}}).
            QJsonObject round = reply.json.value(QStringLiteral("round")).toObject();
            if (round.isEmpty())
                round = reply.json.value(QStringLiteral("data")).toObject()
                            .value(QStringLiteral("round")).toObject();
            const bool hasRound = reply.ok && !round.isEmpty();
            if (!hasRound && !tokenRetried && guard && isDuelTokenError(reply.status, replyText)) {
                guard->invalidateSecurityToken();
                sendDuelBet(path, fields, requestedAmount, limbo, target, over, true);
                return;
            }

            if (!reply.ok) {
                const bool isTimeout = (reply.status == 0);
                const bool unknown = (reply.status >= 500);
                if (reply.status == 0)
                    emit sessionLost(reply.error);
                emit diceFinished(failed(reply.error, {}, isTimeout, unknown));
                return;
            }

            const QJsonObject &json = reply.json;
            if (round.isEmpty()) {
                // No round in the answer: an API error (or rate limit notice) - the bet was not placed.
                QString errText = replyText;
                if (errText.isEmpty())
                    errText = tr("Duel bet rejected (HTTP %1).").arg(reply.status);
                if (reply.status >= 400 || isRetryable({}, errText)) {
                    LiveBetResult r = failed(errText, {}, isRetryable({}, errText), false);
                    r.retryDelayMs = fastRetryDelayMs({}, errText);
                    emit diceFinished(r);
                    return;
                }
                const QString raw = QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)).left(400);
                // The bet may or may not have been placed: do not retry blindly.
                emit diceFinished(failed(tr("Unexpected Duel bet response (no round found): %1").arg(raw),
                                         {}, false, true));
                return;
            }
            if (!round.contains(QStringLiteral("result"))) {
                const QString raw = QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)).left(400);
                emit diceFinished(failed(tr("Unexpected Duel bet response (no roll found): %1").arg(raw),
                                         {}, false, true));
                return;
            }

            LiveBetResult r;
            r.ok = true;
            // "amount_currency" is the stake the server took (same unit as the balance).
            r.amount = round.contains(QStringLiteral("amount_currency"))
                           ? numberOf(round.value(QStringLiteral("amount_currency")))
                           : requestedAmount;
            // Dice: the roll in hundredths (5005 -> 50.05). Limbo: the multiplier rolled ("5.388335").
            r.roll = limbo ? numberOf(round.value(QStringLiteral("result")))
                           : numberOf(round.value(QStringLiteral("result"))) / 100.0;

            if (round.value(QStringLiteral("is_win")).isBool())
                r.win = round.value(QStringLiteral("is_win")).toBool();
            else if (limbo)
                r.win = r.roll >= target;
            else
                r.win = over ? r.roll > target : r.roll < target;

            if (r.win) {
                double mult = numberOf(round.value(QStringLiteral("multiplier")));
                if (!(mult > 0.0) && limbo)
                    mult = target;
                if (!(mult > 0.0)) {
                    // Fallback: 0.1 % edge (what the server answered with).
                    const double chance = numberOf(round.value(QStringLiteral("win_chance")));
                    mult = chance > 0.0 ? 0.999 / chance : 0.0;
                }
                r.multiplier = mult;
                r.payout = r.amount * r.multiplier;
                // The server's payout (stake included) is the truth when it is there.
                const double won = numberOf(round.value(QStringLiteral("amount_won")));
                if (won > 0.0)
                    r.payout = won;
            }

            const QJsonValue idv = round.value(QStringLiteral("id"));
            r.betId = idv.isDouble() ? QString::number(qint64(idv.toDouble())) : idv.toString();
            r.nonce = round.contains(QStringLiteral("nonce"))
                          ? qint64(numberOf(round.value(QStringLiteral("nonce")))) : -1;
            emit diceFinished(r);
        });
    });
}

// Wolfbet limbo: POST https://wolfbet.com/api/v2/limbo/manual/play
//   {"currency":"usdt","game":"limbo","amount":"0.00000001","multiplier":"2","auto":1}
// Answer: {"bet":{"hash","nonce","amount","profit" (net),"multiplier","result_value" (the result),
//   "state":"win"|"loss",...},"userBalance":{...}}
void StakeClient::placeWolfbetLimbo(const LimboBetRequest &request)
{
    const double target = std::round(request.multiplierTarget * 100.0) / 100.0;
    if (!(target > 1.0)) {
        const LiveBetResult r = failed(tr("Wolfbet: invalid Limbo multiplier %1.").arg(target));
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }

    // "2", "2.5", "1.01": no trailing zeros, like the site.
    QString multText = QString::number(target, 'f', 2);
    while (multText.endsWith(QLatin1Char('0')))
        multText.chop(1);
    if (multText.endsWith(QLatin1Char('.')))
        multText.chop(1);

    QJsonObject body;
    body.insert(QStringLiteral("currency"), m_currency.toLower());
    body.insert(QStringLiteral("game"), QStringLiteral("limbo"));
    body.insert(QStringLiteral("amount"), QString::number(request.amount, 'f', 8));
    body.insert(QStringLiteral("multiplier"), multText);
    body.insert(QStringLiteral("auto"), 1);

    const double requestedAmount = request.amount;
    m_transport->post(QStringLiteral("api/v2/limbo/manual/play"), body,
                      [this, requestedAmount, target](const TransportReply &reply) {
        if (!reply.ok) {
            const bool isTimeout = (reply.status == 0);
            const bool unknown = (reply.status >= 500);
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit diceFinished(failed(reply.error, {}, isTimeout, unknown));
            return;
        }

        const QJsonObject &json = reply.json;
        const QJsonObject bet = json.value(QStringLiteral("bet")).toObject();
        if (bet.isEmpty()) {
            QString errText = goatedErrorText(json);
            if (errText.isEmpty())
                errText = tr("Wolfbet bet rejected (HTTP %1).").arg(reply.status);
            if (reply.status >= 400 || isRetryable({}, errText)) {
                LiveBetResult r = failed(errText, {}, isRetryable({}, errText), false);
                r.retryDelayMs = fastRetryDelayMs({}, errText);
                emit diceFinished(r);
                return;
            }
            const QString raw = QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)).left(400);
            emit diceFinished(failed(tr("Unexpected Wolfbet bet response (no bet found): %1").arg(raw),
                                     {}, false, true));
            return;
        }
        if (!bet.contains(QStringLiteral("result_value"))) {
            const QString raw = QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)).left(400);
            emit diceFinished(failed(tr("Unexpected Wolfbet bet response (no result found): %1").arg(raw),
                                     {}, false, true));
            return;
        }

        LiveBetResult r;
        r.ok = true;
        r.amount = bet.contains(QStringLiteral("amount")) ? numberOf(bet.value(QStringLiteral("amount")))
                                                          : requestedAmount;
        r.roll = numberOf(bet.value(QStringLiteral("result_value")));

        const QString state = bet.value(QStringLiteral("state")).toString().trimmed().toLower();
        const double profit = numberOf(bet.value(QStringLiteral("profit")));
        // Win only when the payout multiplier is >= 1x: with a stake, net profit >= 0;
        // the server's "state" is only used for a 0 bet (where profit is always 0).
        if (r.amount > 0.0 && bet.contains(QStringLiteral("profit")))
            r.win = BotConstants::isWinMultiplier((r.amount + profit) / r.amount);
        else if (!state.isEmpty())
            r.win = (state == QLatin1String("win"));
        else
            r.win = r.roll >= target;

        if (r.win) {
            const double betMult = numberOf(bet.value(QStringLiteral("multiplier")));
            r.multiplier = betMult > 0.0 ? betMult : target;
            r.payout = r.amount * r.multiplier;
        }
        // The server's net profit is the truth for the payout (stake included), also for a
        // 1x push or a partial payout that the >= 1x rule above classed as a loss.
        if (r.amount > 0.0 && bet.contains(QStringLiteral("profit")))
            r.payout = std::max(0.0, r.amount + profit);

        r.betId = bet.value(QStringLiteral("hash")).toString();
        r.nonce = bet.contains(QStringLiteral("nonce")) ? qint64(numberOf(bet.value(QStringLiteral("nonce")))) : -1;
        emit diceFinished(r);
    });
}

// Goated limbo: POST https://apis.goated.com/casino/limbo/bet
//   {"multiplier": 2, "amount": 0.001, "currency": "USDC", "is_fiat": false}
// Answer: {"success":true,"data":{"id","multiplier_user":2,"multiplier_output":2.21 (the result),
//   "multiplier":2 (paid multiplier),"payout":0.002,"amount":0.001,"nonce":978,...}}
void StakeClient::placeGoatedLimbo(const LimboBetRequest &request)
{
    const double target = std::round(request.multiplierTarget * 100.0) / 100.0;

    QJsonObject body;
    body.insert(QStringLiteral("multiplier"), target);
    body.insert(QStringLiteral("amount"), std::round(request.amount * 1e8) / 1e8);
    body.insert(QStringLiteral("currency"), m_currency.toUpper());
    body.insert(QStringLiteral("is_fiat"), false);

    const double requestedAmount = request.amount;
    m_transport->post(QStringLiteral("casino/limbo/bet"), body,
                      [this, requestedAmount, target](const TransportReply &reply) {
        if (!reply.ok) {
            const bool isTimeout = (reply.status == 0);
            const bool unknown = (reply.status >= 500);
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit diceFinished(failed(reply.error, {}, isTimeout, unknown));
            return;
        }

        const QJsonObject &json = reply.json;
        const QString errText = goatedErrorText(json);
        const bool successFalse = json.contains(QStringLiteral("success"))
                                  && !json.value(QStringLiteral("success")).toBool();
        if (successFalse || reply.status >= 400 || (!errText.isEmpty() && isRetryable({}, errText))) {
            const QString msg = errText.isEmpty()
                ? tr("Goated bet rejected (HTTP %1).").arg(reply.status) : errText;
            LiveBetResult r = failed(msg, {}, isRetryable({}, msg), false);
            r.retryDelayMs = fastRetryDelayMs({}, msg);
            emit diceFinished(r);
            return;
        }

        double result = 0.0;
        if (!goatedNumber(json, {QStringLiteral("multiplier_output"), QStringLiteral("result"),
                                 QStringLiteral("roll_value")}, &result)) {
            const QString raw = QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)).left(400);
            emit diceFinished(failed(errText.isEmpty()
                                         ? tr("Unexpected Goated bet response (no result found): %1").arg(raw)
                                         : errText,
                                     {}, false, true));
            return;
        }

        double amount = requestedAmount;
        goatedNumber(json, {QStringLiteral("amount")}, &amount);
        double payout = 0.0;
        const bool havePayout = goatedNumber(json, {QStringLiteral("payout")}, &payout);

        LiveBetResult r;
        r.ok = true;
        r.amount = amount;
        r.roll = result;
        r.payout = havePayout ? payout : 0.0;
        // The server's payout is the truth; for a 0 bet fall back to comparing the result.
        r.win = (amount > 0.0 && havePayout) ? BotConstants::isWinMultiplier(payout / amount)
                                             : result >= target;
        if (r.win) {
            double mult = 0.0;
            r.multiplier = (amount > 0.0 && payout > 0.0) ? payout / amount
                           : goatedNumber(json, {QStringLiteral("multiplier"),
                                                 QStringLiteral("multiplier_user")}, &mult) && mult > 0.0
                                 ? mult : target;
            if (!havePayout)
                r.payout = amount * r.multiplier;
        }

        QJsonValue idv;
        if (findGoatedValue(json, {QStringLiteral("id"), QStringLiteral("bet_id")}, &idv))
            r.betId = idv.isString() ? idv.toString() : QString::number(qint64(idv.toDouble()));
        double nonce = -1;
        if (goatedNumber(json, {QStringLiteral("nonce")}, &nonce))
            r.nonce = qint64(nonce);
        emit diceFinished(r);
    });
}

// Goated: POST https://apis.goated.com/casino/provably-fair/rotate-seed {"client_seed": "..."}
// Answer: {"success":true,"data":{"server_seed_hash","next_server_seed_hash","client_seed","nonce":0,...}}
void StakeClient::rotateGoatedSeed(const QString &clientSeed)
{
    QString why;
    if (!m_transport || !m_transport->isReady(&why)) {
        const QString msg = m_transport ? why : tr("No transport configured.");
        QTimer::singleShot(0, this, [this, msg] { emit seedRotateFailed(msg); });
        return;
    }

    QJsonObject body;
    body.insert(QStringLiteral("client_seed"), clientSeed);

    m_transport->post(QStringLiteral("casino/provably-fair/rotate-seed"), body,
                      [this, clientSeed](const TransportReply &reply) {
        if (!reply.ok) {
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit seedRotateFailed(reply.error);
            return;
        }
        const QJsonObject &json = reply.json;
        const bool successFalse = json.contains(QStringLiteral("success"))
                                  && !json.value(QStringLiteral("success")).toBool();
        if (successFalse || reply.status >= 400) {
            const QString msg = goatedErrorText(json);
            emit seedRotateFailed(msg.isEmpty() ? tr("Goated seed rotation rejected (HTTP %1).").arg(reply.status)
                                                : msg);
            return;
        }
        const QJsonObject data = json.value(QStringLiteral("data")).toObject();
        if (data.isEmpty()) {
            emit seedRotateFailed(tr("Unexpected seed rotation response."));
            return;
        }
        const QString active = data.value(QStringLiteral("client_seed")).toString();
        emit seedRotated(active.isEmpty() ? clientSeed : active,
                         qint64(numberOf(data.value(QStringLiteral("nonce")))),
                         data.value(QStringLiteral("server_seed_hash")).toString(),
                         data.value(QStringLiteral("next_server_seed_hash")).toString());
    });
}

// Goated: POST https://apis.goated.com/user/vault/transfer/in {"currency": "USDC", "amount": 0.001}
// Answer: {"success":true,"data":{"vault_balance":{...},"wallet_balance":{...}}} - the amount is
// not echoed, so the requested one is reported once the server accepted the transfer.
void StakeClient::goatedVaultDeposit(double amount)
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
    if (m_currency.isEmpty()) {
        QTimer::singleShot(0, this, [this] {
            emit vaultFailed(tr("No currency selected — log in and pick one in Options."));
        });
        return;
    }

    const QString currency = m_currency.toUpper();
    const double rounded = std::round(amount * 1e8) / 1e8;
    QJsonObject body;
    body.insert(QStringLiteral("currency"), currency);
    body.insert(QStringLiteral("amount"), rounded);

    m_transport->post(QStringLiteral("user/vault/transfer/in"), body,
                      [this, currency, rounded](const TransportReply &reply) {
        if (!reply.ok) {
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit vaultFailed(reply.error);
            return;
        }
        const QJsonObject &json = reply.json;
        const bool successFalse = json.contains(QStringLiteral("success"))
                                  && !json.value(QStringLiteral("success")).toBool();
        if (successFalse || reply.status >= 400) {
            const QString msg = goatedErrorText(json);
            emit vaultFailed(msg.isEmpty() ? tr("Goated vault transfer rejected (HTTP %1).").arg(reply.status)
                                           : msg);
            return;
        }
        if (!json.value(QStringLiteral("data")).isObject()) {
            emit vaultFailed(tr("Unexpected vault response."));
            return;
        }
        emit vaultDeposited(rounded, currency);
    });
}

// Shuffle limbo: the LimboPlay GraphQL mutation (same query the JavaScript bot uses).
// amount is a string with 8 decimals, bet the multiplier target with 2 decimals.
void StakeClient::placeShuffleLimbo(const LimboBetRequest &request)
{
    const double target = std::round(request.multiplierTarget * 100.0) / 100.0;

    QJsonObject data;
    data.insert(QStringLiteral("amount"), QString::number(request.amount, 'f', 8));
    data.insert(QStringLiteral("bet"), QString::number(target, 'f', 2));
    data.insert(QStringLiteral("currency"), ShuffleTransport::apiCurrency(m_currency));
    data.insert(QStringLiteral("windowId"), randomClientSeed(10));

    QJsonObject variables;
    variables.insert(QStringLiteral("data"), data);

    QJsonObject body;
    body.insert(QStringLiteral("operationName"), QStringLiteral("LimboPlay"));
    body.insert(QStringLiteral("variables"), variables);
    body.insert(QStringLiteral("query"), QStringLiteral(
        "mutation LimboPlay($data: LimboPlayInput!) {\n"
        "  limboPlay(data: $data) {\n"
        "    id\n"
        "    currency\n"
        "    amount\n"
        "    payout\n"
        "    afterBalance\n"
        "    shuffleOriginalActions {\n"
        "      id\n"
        "      action {\n"
        "        limbo {\n"
        "          resultRaw\n"
        "          resultValue\n"
        "          userValue\n"
        "          __typename\n"
        "        }\n"
        "        __typename\n"
        "      }\n"
        "      __typename\n"
        "    }\n"
        "    __typename\n"
        "  }\n"
        "}"));

    const double requestedAmount = request.amount;
    m_transport->post(QStringLiteral("main-api/graphql/api/graphql"), body,
                      [this, requestedAmount](const TransportReply &reply) {
        if (!reply.ok) {
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
                                     .value(QStringLiteral("limboPlay")).toObject();
        const QJsonArray actions = play.value(QStringLiteral("shuffleOriginalActions")).toArray();
        const QJsonObject limbo = actions.isEmpty()
            ? QJsonObject()
            : actions.first().toObject().value(QStringLiteral("action")).toObject()
                  .value(QStringLiteral("limbo")).toObject();
        if (play.isEmpty() || limbo.isEmpty()) {
            emit diceFinished(failed(tr("Unexpected bet response (no limboPlay result)."), {}, true, false));
            return;
        }

        LiveBetResult r;
        r.ok = true;
        r.amount = play.contains(QStringLiteral("amount")) ? numberOf(play.value(QStringLiteral("amount")))
                                                           : requestedAmount;
        r.payout = numberOf(play.value(QStringLiteral("payout")));
        r.roll = numberOf(limbo.value(QStringLiteral("resultValue")));
        r.betId = play.value(QStringLiteral("id")).toString();
        r.nonce = -1; // the mutation does not return the seed nonce

        const double userValue = numberOf(limbo.value(QStringLiteral("userValue")));

        // The server's payout is the truth; for a 0 bet fall back to comparing the result.
        r.win = r.amount > 0.0 ? BotConstants::isWinMultiplier(r.payout / r.amount)
                               : r.roll >= userValue;
        if (r.win) {
            r.multiplier = (r.amount > 0.0 && r.payout > 0.0)
                               ? r.payout / r.amount
                               : userValue;
        }
        emit diceFinished(r);
    });
}
