#include "bot/StakeTransport.h"

#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkCookie>
#include <QNetworkCookieJar>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QWebSocket>
#include <QWebSocketCorsAuthenticator>
#include <QWebSocketServer>

namespace
{
constexpr int kRequestTimeoutMs = 15000;

TransportReply failure(const QString &why, int status = 0)
{
    TransportReply r;
    r.ok = false;
    r.status = status;
    r.error = why;
    return r;
}

// Runs `cb(reply)` on the next event-loop turn so callers never get re-entered
// from inside their own post() call.
void deferred(QObject *context, const TransportCallback &cb, const TransportReply &reply)
{
    QTimer::singleShot(0, context, [cb, reply] { cb(reply); });
}

QString snippet(const QByteArray &data)
{
    QString s = QString::fromUtf8(data.left(300)).simplified();
    return s;
}
} // namespace

QString normalizeSite(const QString &input)
{
    QString s = input.trimmed().toLower();
    if (s.startsWith(QLatin1String("https://")))
        s.remove(0, 8);
    else if (s.startsWith(QLatin1String("http://")))
        s.remove(0, 7);
    const int slash = s.indexOf(QLatin1Char('/'));
    if (slash >= 0)
        s.truncate(slash);

    static const QRegularExpression hostRe(
        QStringLiteral("^[a-z0-9]([a-z0-9.-]*[a-z0-9])?$"));
    return hostRe.match(s).hasMatch() ? s : QString();
}

// ================================================================== cookie

CookieTransport::CookieTransport(QObject *parent)
    : IStakeTransport(parent)
{
}

void CookieTransport::setCredentials(const StakeCredentials &cred)
{
    const bool wasReady = isReady();
    m_cred = cred;
    m_cred.site = normalizeSite(cred.site);

    // Fresh jar so a stale session from the previous credentials is never reused.
    // (QNetworkAccessManager deletes the jar it previously owned by itself.)
    m_nam.setCookieJar(new QNetworkCookieJar);

    if (!m_cred.clearance.isEmpty() && !m_cred.site.isEmpty()) {
        QNetworkCookie cookie("cf_clearance", m_cred.clearance.toUtf8());
        cookie.setPath(QStringLiteral("/"));
        cookie.setSecure(true);
        m_nam.cookieJar()->setCookiesFromUrl({cookie}, QUrl(QStringLiteral("https://") + m_cred.site));
    }

    if (wasReady != isReady())
        emit readyChanged(isReady());
}

bool CookieTransport::isReady(QString *whyNot) const
{
    QString why;
    if (m_cred.site.isEmpty())
        why = tr("Invalid site/mirror domain.");
    else if (m_cred.apiKey.trimmed().isEmpty())
        why = tr("API key is empty.");
    else if (m_cred.clearance.trimmed().isEmpty())
        why = tr("cf_clearance cookie is empty.");
    if (whyNot)
        *whyNot = why;
    return why.isEmpty();
}

void CookieTransport::post(const QString &path, const QJsonObject &body, TransportCallback cb)
{
    QString why;
    if (!isReady(&why)) {
        deferred(this, cb, failure(why));
        return;
    }

    QNetworkRequest req(QUrl(QStringLiteral("https://%1/%2").arg(m_cred.site, path)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("x-access-token", m_cred.apiKey.trimmed().toUtf8());
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  m_cred.userAgent.isEmpty()
                      ? QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                                       "AppleWebKit/537.36 (KHTML, like Gecko) "
                                       "Chrome/122.0.0.0 Safari/537.36")
                      : m_cred.userAgent);
    req.setTransferTimeout(kRequestTimeoutMs);

    QNetworkReply *reply = m_nam.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, cb] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray data = reply->readAll();

        const QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isObject()) {
            // The API reports bet errors as JSON (often with a 4xx status):
            // hand that JSON up so the caller can read errorType/message.
            TransportReply r;
            r.ok = true;
            r.status = status;
            r.json = doc.object();
            cb(r);
            return;
        }

        if (status == 403 || status == 503) {
            cb(failure(tr("HTTP %1: blocked by Cloudflare — refresh the cf_clearance cookie "
                          "(same User-Agent as the browser) or switch to Extension mode.")
                           .arg(status),
                       status));
        } else if (status > 0) {
            cb(failure(tr("HTTP %1: %2").arg(status).arg(snippet(data)), status));
        } else {
            cb(failure(reply->errorString(), 0));
        }
    });
}

// =============================================================== extension

ExtensionTransport::ExtensionTransport(QObject *parent)
    : IStakeTransport(parent)
{
}

ExtensionTransport::~ExtensionTransport()
{
    stop();
}

void ExtensionTransport::setCredentials(const StakeCredentials &cred)
{
    m_cred = cred;
    m_cred.site = normalizeSite(cred.site);
}

bool ExtensionTransport::start(quint16 port, QString *errorOut)
{
    if (m_server && m_server->isListening() && m_port == port)
        return true;
    stop();

    m_server = new QWebSocketServer(QStringLiteral("QDiceBot"),
                                    QWebSocketServer::NonSecureMode, this);
    // Loopback only: the original 0.0.0.0 bind exposes the session to the LAN.
    if (!m_server->listen(QHostAddress::LocalHost, port)) {
        if (errorOut)
            *errorOut = m_server->errorString();
        delete m_server;
        m_server = nullptr;
        return false;
    }
    m_port = port;

    connect(m_server, &QWebSocketServer::originAuthenticationRequired, this,
            [this](QWebSocketCorsAuthenticator *auth) {
                const bool allowed = originAllowed(auth->origin());
                auth->setAllowed(allowed);
                if (!allowed)
                    emit originRejected(auth->origin());
            });
    connect(m_server, &QWebSocketServer::newConnection, this, &ExtensionTransport::onNewConnection);
    return true;
}

void ExtensionTransport::stop()
{
    failAll(tr("Extension server stopped."));
    if (m_socket) {
        QWebSocket *s = m_socket;
        m_socket = nullptr;
        s->disconnect(this);
        s->close();
        s->deleteLater();
    }
    if (m_server) {
        m_server->close();
        delete m_server;
        m_server = nullptr;
    }
    m_port = 0;
}

bool ExtensionTransport::isListening() const
{
    return m_server && m_server->isListening();
}

bool ExtensionTransport::isConnected() const
{
    return m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
}

bool ExtensionTransport::isReady(QString *whyNot) const
{
    QString why;
    if (!isListening())
        why = tr("Extension server is not running.");
    else if (!isConnected())
        why = tr("Browser extension not connected (ws://127.0.0.1:%1).").arg(m_port);
    else if (m_cred.site.isEmpty())
        why = tr("Invalid site/mirror domain.");
    else if (m_cred.apiKey.trimmed().isEmpty())
        why = tr("API key is empty.");
    if (whyNot)
        *whyNot = why;
    return why.isEmpty();
}

bool ExtensionTransport::originAllowed(const QString &origin) const
{
    // A browser always sends an Origin for WebSocket handshakes, so this stops
    // arbitrary web pages from connecting to localhost and impersonating the
    // extension (they would then receive your API key in every request).
    if (origin.isEmpty())
        return true; // non-browser local client
    if (origin.startsWith(QLatin1String("chrome-extension://"))
        || origin.startsWith(QLatin1String("moz-extension://")))
        return true;
    // Extensions that open the socket from a content script on the casino page
    // carry that page's origin.
    if (!m_cred.site.isEmpty()
        && (origin == QLatin1String("https://") + m_cred.site
            || origin == QLatin1String("https://www.") + m_cred.site))
        return true;
    return false;
}

void ExtensionTransport::onNewConnection()
{
    while (m_server && m_server->hasPendingConnections()) {
        QWebSocket *sock = m_server->nextPendingConnection();
        if (!originAllowed(sock->origin())) {
            emit originRejected(sock->origin());
            sock->close(QWebSocketProtocol::CloseCodePolicyViolated,
                        QStringLiteral("origin not allowed"));
            sock->deleteLater();
            continue;
        }

        if (m_socket) { // extension reconnected: drop the stale socket
            QWebSocket *old = m_socket;
            m_socket = nullptr;
            old->disconnect(this);
            old->close();
            old->deleteLater();
            failAll(tr("Extension reconnected; request dropped."));
        }

        m_socket = sock;
        connect(sock, &QWebSocket::textMessageReceived, this, &ExtensionTransport::onText);
        connect(sock, &QWebSocket::disconnected, this, [this, sock] { onSocketClosed(sock); });
        emit readyChanged(true);
    }
}

void ExtensionTransport::onSocketClosed(QWebSocket *socket)
{
    const bool wasCurrent = (socket == m_socket);
    if (wasCurrent)
        m_socket = nullptr;
    socket->deleteLater();
    if (wasCurrent) {
        failAll(tr("Browser extension disconnected."));
        emit readyChanged(false);
    }
}

void ExtensionTransport::post(const QString &path, const QJsonObject &body, TransportCallback cb)
{
    QString why;
    if (!isReady(&why)) {
        deferred(this, cb, failure(why));
        return;
    }

    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);

    QJsonObject headers;
    headers.insert(QStringLiteral("Content-Type"), QStringLiteral("application/json"));
    headers.insert(QStringLiteral("x-access-token"), m_cred.apiKey.trimmed());

    QJsonObject options;
    options.insert(QStringLiteral("method"), QStringLiteral("POST"));
    options.insert(QStringLiteral("headers"), headers);
    // The extension expects the body as a JSON *string* (same as the C# client).
    options.insert(QStringLiteral("body"),
                   QString::fromUtf8(QJsonDocument(body).toJson(QJsonDocument::Compact)));

    QJsonObject payload;
    payload.insert(QStringLiteral("url"), QStringLiteral("https://%1/%2").arg(m_cred.site, path));
    payload.insert(QStringLiteral("options"), options);

    QJsonObject msg;
    msg.insert(QStringLiteral("id"), id);
    msg.insert(QStringLiteral("payload"), payload);

    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, id] {
        finish(id, failure(tr("Request timed out (extension).")));
    });
    m_pending.insert(id, Pending{std::move(cb), timer});
    timer->start(kRequestTimeoutMs);

    m_socket->sendTextMessage(
        QString::fromUtf8(QJsonDocument(msg).toJson(QJsonDocument::Compact)));
}

void ExtensionTransport::onText(const QString &message)
{
    const QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (!doc.isObject())
        return;
    const QJsonObject obj = doc.object();
    const QString id = obj.value(QStringLiteral("id")).toString();
    if (id.isEmpty() || !m_pending.contains(id))
        return; // late reply for a request that already timed out

    const QJsonValue resp = obj.value(QStringLiteral("response"));
    TransportReply reply;
    if (resp.isObject()) {
        reply.ok = true;
        reply.json = resp.toObject();
    } else if (resp.isString()) {
        const QJsonDocument inner = QJsonDocument::fromJson(resp.toString().toUtf8());
        if (inner.isObject()) {
            reply.ok = true;
            reply.json = inner.object();
        } else {
            reply = failure(tr("Extension returned a non-JSON response: %1")
                                .arg(snippet(resp.toString().toUtf8())));
        }
    } else if (obj.value(QStringLiteral("error")).isString()) {
        reply = failure(obj.value(QStringLiteral("error")).toString());
    } else {
        reply = failure(tr("Unexpected response from the extension."));
    }
    finish(id, reply);
}

void ExtensionTransport::finish(const QString &id, const TransportReply &reply)
{
    auto it = m_pending.find(id);
    if (it == m_pending.end())
        return;
    Pending p = it.value();
    m_pending.erase(it);
    if (p.timer) {
        p.timer->stop();
        p.timer->deleteLater();
    }
    if (p.cb)
        p.cb(reply);
}

void ExtensionTransport::failAll(const QString &why)
{
    const QStringList ids = m_pending.keys();
    for (const QString &id : ids)
        finish(id, failure(why));
}
