#include "bot/ShuffleTransport.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QWebSocket>

#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
#include <QWebSocketHandshakeOptions>
#endif

namespace
{
constexpr int kRequestTimeoutMs = 15000;
constexpr int kAckTimeoutMs = 10000;
constexpr int kBalanceFallbackMs = 3000;

const char kGraphqlPath[] = "main-api/graphql/api/graphql";

double numberOf(const QJsonValue &v)
{
    if (v.isDouble())
        return v.toDouble();
    if (v.isString())
        return v.toString().toDouble();
    return 0.0;
}

TransportReply failure(const QString &why, int status = 0)
{
    TransportReply r;
    r.ok = false;
    r.status = status;
    r.error = why;
    return r;
}

// Same alphabet as the web client's randomString().
QString randomString(int length)
{
    static const char chars[] = "_-0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_-";
    QString out;
    out.reserve(length);
    for (int i = 0; i < length; ++i)
        out += QLatin1Char(chars[QRandomGenerator::global()->bounded(int(sizeof(chars)) - 1)]);
    return out;
}

QString defaultUserAgent()
{
    return QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                          "(KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36");
}

QString firstGraphqlError(const QJsonObject &json)
{
    const QJsonArray errs = json.value(QStringLiteral("errors")).toArray();
    if (errs.isEmpty())
        return {};
    const QString msg = errs.first().toObject().value(QStringLiteral("message")).toString();
    return msg.isEmpty() ? QStringLiteral("API error") : msg;
}
} // namespace

ShuffleTransport::ShuffleTransport(QObject *parent)
    : IStakeTransport(parent)
{
    m_ackTimer = new QTimer(this);
    m_ackTimer->setSingleShot(true);
    connect(m_ackTimer, &QTimer::timeout, this, [this] {
        failConnection(tr("Shuffle websocket: no connection_ack — check Token, CORRELATION_USER_ID "
                          "and BROWSER_ID."));
    });

    m_fallbackTimer = new QTimer(this);
    m_fallbackTimer->setSingleShot(true);
    connect(m_fallbackTimer, &QTimer::timeout, this, [this] {
        // BalanceUpdated only pushes changes, so read the full list of balances once per
        // connection over HTTPS (values pushed by the websocket are never overwritten).
        fetchBalancesHttp();
    });
}

ShuffleTransport::~ShuffleTransport()
{
    closeSocket();
}

void ShuffleTransport::setCredentials(const ShuffleCredentials &cred)
{
    const bool wasReady = isReady();

    ShuffleCredentials next = cred;
    next.site = normalizeSite(cred.site);
    next.token = cred.token.trimmed();
    next.correlationUserId = cred.correlationUserId.trimmed();
    next.browserId = cred.browserId.trimmed();
    next.userAgent = cred.userAgent.trimmed();

    const bool changed = next.site != m_cred.site || next.token != m_cred.token
                         || next.correlationUserId != m_cred.correlationUserId
                         || next.browserId != m_cred.browserId;
    m_cred = next;

    if (changed) {
        ++m_generation;
        closeSocket();
        m_balances.clear();
        m_fresh.clear();
        m_wsFresh.clear();
        m_httpInFlight = false;
    }
    if (wasReady != isReady())
        emit readyChanged(isReady());
}

bool ShuffleTransport::isReady(QString *whyNot) const
{
    QString why;
    if (m_cred.site.isEmpty())
        why = tr("Invalid site/mirror domain.");
    else if (m_cred.token.isEmpty())
        why = tr("Token is empty.");
    else if (m_cred.correlationUserId.isEmpty())
        why = tr("CORRELATION_USER_ID is empty.");
    else if (m_cred.browserId.isEmpty())
        why = tr("BROWSER_ID is empty.");
    if (whyNot)
        *whyNot = why;
    return why.isEmpty();
}

QString ShuffleTransport::bearerToken() const
{
    QString t = m_cred.token.trimmed();
    if (t.startsWith(QLatin1String("Bearer "), Qt::CaseInsensitive))
        t = t.mid(7).trimmed(); // the user pasted the whole header value
    return t;
}

// BROWSER_ID-<random 21>-t::CORRELATION_USER_ID (same as the web client).
QString ShuffleTransport::correlationId() const
{
    return m_cred.browserId + QLatin1Char('-') + randomString(21) + QStringLiteral("-t::")
           + m_cred.correlationUserId;
}

// ------------------------------------------------------------------ HTTPS

void ShuffleTransport::post(const QString &path, const QJsonObject &body, TransportCallback cb)
{
    QString why;
    if (!isReady(&why)) {
        QTimer::singleShot(0, this, [cb, why] { cb(failure(why)); });
        return;
    }

    QNetworkRequest req(QUrl(QStringLiteral("https://%1/%2").arg(m_cred.site, path)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("Authorization", QByteArray("Bearer ") + bearerToken().toUtf8());
    req.setRawHeader("X-Correlation-Id", correlationId().toUtf8());
    req.setRawHeader("Origin", QStringLiteral("https://%1").arg(m_cred.site).toUtf8());
    req.setRawHeader("Referer", QStringLiteral("https://%1/").arg(m_cred.site).toUtf8());
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  m_cred.userAgent.isEmpty() ? defaultUserAgent() : m_cred.userAgent);
    req.setTransferTimeout(kRequestTimeoutMs);

    QNetworkReply *reply = m_nam.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, cb] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray data = reply->readAll();

        const QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isObject()) {
            TransportReply r;
            r.ok = true;
            r.status = status;
            r.json = doc.object();
            cb(r);
            return;
        }

        if (status == 429) {
            // Report it like an API error so the engine's "slow down" retry logic applies.
            QJsonObject err;
            err.insert(QStringLiteral("message"), QStringLiteral("HTTP 429: slow down"));
            QJsonObject json;
            json.insert(QStringLiteral("errors"), QJsonArray{err});
            TransportReply r;
            r.ok = true;
            r.status = status;
            r.json = json;
            cb(r);
        } else if (status == 401) {
            cb(failure(tr("HTTP 401: unauthorized — the token was rejected or has expired."), status));
        } else if (status == 403 || status == 503) {
            cb(failure(tr("HTTP %1: blocked (Cloudflare or the token/ids were refused). Direct mode "
                          "has no browser session to pass a challenge.")
                           .arg(status),
                       status));
        } else if (status > 0) {
            cb(failure(tr("HTTP %1: %2")
                           .arg(status)
                           .arg(QString::fromUtf8(data.left(300)).simplified()),
                       status));
        } else {
            cb(failure(reply->errorString(), 0));
        }
    });
}

void ShuffleTransport::fetchBalancesHttp()
{
    if (m_httpInFlight)
        return;
    QString why;
    if (!isReady(&why)) {
        emit balancesFailed(why);
        return;
    }
    m_httpInFlight = true;
    const quint32 generation = m_generation;

    QJsonObject body;
    body.insert(QStringLiteral("operationName"), QStringLiteral("GetMyBalances"));
    body.insert(QStringLiteral("variables"), QJsonObject());
    body.insert(QStringLiteral("query"), QStringLiteral(
        "query GetMyBalances {\n"
        "  me {\n"
        "    id\n"
        "    account {\n"
        "      id\n"
        "      balances {\n"
        "        currency\n"
        "        amount: availableAmount\n"
        "        __typename\n"
        "      }\n"
        "      __typename\n"
        "    }\n"
        "    __typename\n"
        "  }\n"
        "}"));

    post(QString::fromLatin1(kGraphqlPath), body, [this, generation](const TransportReply &reply) {
        if (generation != m_generation)
            return; // credentials changed meanwhile
        m_httpInFlight = false;
        if (!reply.ok) {
            emit balancesFailed(reply.error);
            return;
        }
        const QString apiError = firstGraphqlError(reply.json);
        if (!apiError.isEmpty()) {
            emit balancesFailed(apiError);
            return;
        }
        const QJsonValue balsVal = reply.json.value(QStringLiteral("data")).toObject()
                                       .value(QStringLiteral("me")).toObject()
                                       .value(QStringLiteral("account")).toObject()
                                       .value(QStringLiteral("balances"));
        if (!balsVal.isArray()) {
            emit balancesFailed(tr("Login failed: unexpected response (is the token valid?)"));
            return;
        }
        const QJsonArray arr = balsVal.toArray();
        for (const QJsonValue &v : arr) {
            const QJsonObject o = v.toObject();
            const QString cur = o.value(QStringLiteral("currency")).toString().toLower();
            if (cur.isEmpty())
                continue;
            if (!m_wsFresh.contains(cur)) // a pushed value is newer than this reply
                m_balances.insert(cur, numberOf(o.value(QStringLiteral("amount"))));
            m_fresh.insert(cur);
        }
        emitBalances();
    });
}

// -------------------------------------------------------------- WebSocket

void ShuffleTransport::requestBalances()
{
    QString why;
    if (!isReady(&why)) {
        QTimer::singleShot(0, this, [this, why] { emit balancesFailed(why); });
        return;
    }

    if (!m_socket) {
        openSocket(); // answer arrives through balancesUpdated() / balancesFailed()
        return;
    }
    if (!m_acked)
        return; // still connecting / logging in

    if (!m_balances.isEmpty())
        QTimer::singleShot(0, this, [this] { emitBalances(); });
    else if (!m_fallbackTimer->isActive())
        fetchBalancesHttp();
}

void ShuffleTransport::openSocket()
{
    closeSocket();
    m_acked = false;
    m_connectFailureReported = false;
    m_balances.clear(); // values cached before (e.g. before switching to Stake and back) are stale
    m_fresh.clear();
    m_wsFresh.clear();

    auto *sock = new QWebSocket(QStringLiteral("https://") + m_cred.site,
                                QWebSocketProtocol::VersionLatest, this);
    m_socket = sock;

    connect(sock, &QWebSocket::connected, this, [this, sock] {
        if (sock != m_socket)
            return;
        QJsonObject payload;
        payload.insert(QStringLiteral("x-correlation-id"), correlationId());
        payload.insert(QStringLiteral("authorization"), bearerToken());
        QJsonObject init;
        init.insert(QStringLiteral("type"), QStringLiteral("connection_init"));
        init.insert(QStringLiteral("payload"), payload);
        sendJson(init);
        m_ackTimer->start(kAckTimeoutMs);
    });
    connect(sock, &QWebSocket::textMessageReceived, this, [this, sock](const QString &message) {
        if (sock == m_socket)
            onSocketText(message);
    });
    connect(sock, &QWebSocket::disconnected, this, [this, sock] {
        if (sock == m_socket)
            onSocketClosed();
    });
    const auto onError = [this, sock](QAbstractSocket::SocketError) {
        if (sock == m_socket)
            failConnection(tr("Shuffle websocket error: %1").arg(sock->errorString()));
    };
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(sock, &QWebSocket::errorOccurred, this, onError);
#else
    connect(sock, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error), this, onError);
#endif

    QNetworkRequest req(QUrl(
        QStringLiteral("wss://%1/main-api/bp-subscription/subscription/graphql").arg(m_cred.site)));
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  m_cred.userAgent.isEmpty() ? defaultUserAgent() : m_cred.userAgent);
#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
    QWebSocketHandshakeOptions options;
    options.setSubprotocols({QStringLiteral("graphql-transport-ws")});
    sock->open(req, options);
#else
    // Qt < 6.4 has no handshake options; ask for the subprotocol through the raw header.
    req.setRawHeader("Sec-WebSocket-Protocol", "graphql-transport-ws");
    sock->open(req);
#endif
}

void ShuffleTransport::closeSocket()
{
    m_ackTimer->stop();
    m_fallbackTimer->stop();
    m_acked = false;
    if (m_socket) {
        QWebSocket *s = m_socket;
        m_socket = nullptr;
        s->disconnect(this);
        s->close();
        s->deleteLater();
    }
}

void ShuffleTransport::sendJson(const QJsonObject &obj)
{
    if (m_socket)
        m_socket->sendTextMessage(
            QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
}

void ShuffleTransport::failConnection(const QString &why)
{
    const bool wasAcked = m_acked;
    closeSocket();
    // After a successful login a dropped socket is reopened by the next requestBalances()
    // (the main window polls every few seconds) without bothering the user.
    if (wasAcked || m_connectFailureReported)
        return;
    m_connectFailureReported = true;
    emit balancesFailed(why);
}

void ShuffleTransport::onSocketClosed()
{
    const int code = m_socket ? int(m_socket->closeCode()) : 0;
    const QString reason = m_socket ? m_socket->closeReason() : QString();
    QString why;
    if (code == 4401)
        why = tr("Shuffle websocket: unauthorized (4401) — the token was rejected or has expired.");
    else if (code == 4403)
        why = tr("Shuffle websocket: forbidden (4403) — the token or ids were refused.");
    else
        why = tr("Shuffle websocket closed before login (code %1%2).")
                  .arg(code)
                  .arg(reason.isEmpty() ? QString() : QStringLiteral(": ") + reason);
    failConnection(why);
}

void ShuffleTransport::onSocketText(const QString &message)
{
    const QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (!doc.isObject())
        return;
    const QJsonObject obj = doc.object();
    const QString type = obj.value(QStringLiteral("type")).toString();

    if (type == QLatin1String("connection_ack")) {
        m_ackTimer->stop();
        m_acked = true;

        QJsonObject sub;
        sub.insert(QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces));
        sub.insert(QStringLiteral("type"), QStringLiteral("subscribe"));
        QJsonObject payload;
        payload.insert(QStringLiteral("variables"), QJsonObject());
        payload.insert(QStringLiteral("operationName"), QStringLiteral("BalanceUpdated"));
        payload.insert(QStringLiteral("extensions"), QJsonObject());
        payload.insert(QStringLiteral("query"), QStringLiteral(
            "subscription BalanceUpdated {\n"
            "  balanceUpdated {\n"
            "    currency\n"
            "    amount\n"
            "    windowId\n"
            "    __typename\n"
            "  }\n"
            "}"));
        sub.insert(QStringLiteral("payload"), payload);
        sendJson(sub);

        m_fallbackTimer->start(kBalanceFallbackMs);
        return;
    }

    if (type == QLatin1String("ping")) {
        QJsonObject pong;
        pong.insert(QStringLiteral("type"), QStringLiteral("pong"));
        sendJson(pong);
        return;
    }

    if (type == QLatin1String("next")) {
        const QJsonObject bal = obj.value(QStringLiteral("payload")).toObject()
                                    .value(QStringLiteral("data")).toObject()
                                    .value(QStringLiteral("balanceUpdated")).toObject();
        const QString cur = bal.value(QStringLiteral("currency")).toString().toLower();
        if (cur.isEmpty())
            return;
        m_balances.insert(cur, numberOf(bal.value(QStringLiteral("amount"))));
        m_fresh.insert(cur);
        m_wsFresh.insert(cur);
        emitBalances();
        return;
    }

    if (type == QLatin1String("error")) {
        const QJsonValue p = obj.value(QStringLiteral("payload"));
        QString msg;
        if (p.isArray() && !p.toArray().isEmpty())
            msg = p.toArray().first().toObject().value(QStringLiteral("message")).toString();
        else if (p.isObject())
            msg = p.toObject().value(QStringLiteral("message")).toString();
        emit balancesFailed(tr("Balance subscription error: %1")
                                .arg(msg.isEmpty() ? QStringLiteral("unknown") : msg));
    }
}

void ShuffleTransport::emitBalances()
{
    emit balancesUpdated(m_balances);
}
