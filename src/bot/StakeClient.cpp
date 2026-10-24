#include "bot/StakeClient.h"

#include <QJsonArray>
#include <QRandomGenerator>
#include <QTimer>

#include <cmath>

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

void StakeClient::fetchBalances()
{
    QString why;
    if (!m_transport || !m_transport->isReady(&why)) {
        const QString msg = m_transport ? why : tr("No transport configured.");
        QTimer::singleShot(0, this, [this, msg] { emit balancesFailed(msg); });
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
    if (betId.isEmpty() || !m_transport || !m_transport->isReady(&why)) {
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
            emit diceFinished(failed(apiError, type, isRetryable(type, apiError), false));
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
