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

namespace
{
constexpr int kCloseDuplicate = 4001; // the extension stands by and retries later

// "https://www.duel.com" -> "duel.com"; "" when the origin is not an https page.
QString hostOfOrigin(const QString &origin)
{
    if (!origin.startsWith(QLatin1String("https://")))
        return QString();
    return ExtensionTransport::bareHost(normalizeSite(origin));
}

// Headers a page cannot set (the browser sends its own) are not forwarded.
bool forwardHeader(const QByteArray &name)
{
    static const QList<QByteArray> skip{"origin", "referer", "user-agent", "cookie", "host",
                                        "content-length", "connection", "accept-encoding"};
    return !skip.contains(name.toLower());
}
} // namespace

ExtensionTransport::ExtensionTransport(QObject *parent)
    : IStakeTransport(parent)
{
}

ExtensionTransport::~ExtensionTransport()
{
    stop();
}

QString ExtensionTransport::bareHost(const QString &host)
{
    QString h = host.trimmed().toLower();
    if (h.startsWith(QLatin1String("www.")))
        h.remove(0, 4);
    return h;
}

const QStringList &ExtensionTransport::stakeHosts()
{
    static const QStringList hosts{
        QStringLiteral("stake.bet"),     QStringLiteral("stake.com"),       QStringLiteral("stake.pet"),
        QStringLiteral("stake.kim"),     QStringLiteral("stake.us"),        QStringLiteral("stake.games"),
        QStringLiteral("stake.mba"),     QStringLiteral("stake.ac"),        QStringLiteral("stake.jp"),
        QStringLiteral("stake.bz"),      QStringLiteral("stake.krd"),       QStringLiteral("stake.ceo"),
        QStringLiteral("staketr.com"),   QStringLiteral("primedice.com"),   QStringLiteral("primedice.games"),
        QStringLiteral("stake1021.com"), QStringLiteral("stake1067.com"),   QStringLiteral("stake3075.com")};
    return hosts;
}

void ExtensionTransport::setCredentials(const StakeCredentials &cred)
{
    m_cred = cred;
    m_cred.site = normalizeSite(cred.site);
}

void ExtensionTransport::setAllowedHosts(const QStringList &hosts)
{
    m_allowedHosts.clear();
    for (const QString &h : hosts) {
        const QString b = bareHost(normalizeSite(h));
        if (!b.isEmpty() && !m_allowedHosts.contains(b))
            m_allowedHosts << b;
    }
}

bool ExtensionTransport::start(quint16 port, QString *errorOut)
{
    if (m_server && m_server->isListening() && m_port == port)
        return true;
    stop();

    m_server = new QWebSocketServer(QStringLiteral("xBot"),
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
    const bool hadSockets = !m_sockets.isEmpty();
    const QList<QWebSocket *> sockets = m_sockets.keys();
    m_sockets.clear();
    for (QWebSocket *s : sockets) {
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
    if (hadSockets)
        emit readyChanged(false);
}

bool ExtensionTransport::isListening() const
{
    return m_server && m_server->isListening();
}

bool ExtensionTransport::isConnected() const
{
    for (auto it = m_sockets.constBegin(); it != m_sockets.constEnd(); ++it) {
        if (it.key()->state() == QAbstractSocket::ConnectedState)
            return true;
    }
    return false;
}

bool ExtensionTransport::isConnectedTo(const QStringList &hosts) const
{
    return socketFor(hosts, false) != nullptr;
}

QStringList ExtensionTransport::connectedHosts() const
{
    QStringList out;
    for (auto it = m_sockets.constBegin(); it != m_sockets.constEnd(); ++it) {
        if (!it.value().isEmpty() && it.key()->state() == QAbstractSocket::ConnectedState
            && !out.contains(it.value()))
            out << it.value();
    }
    return out;
}

// The tab for the first of `hosts` that has one; `allowUnknown`: else a tab that did not say
// its site (an older extension version connecting from the extension's own origin).
QWebSocket *ExtensionTransport::socketFor(const QStringList &hosts, bool allowUnknown) const
{
    for (const QString &h : hosts) {
        const QString want = bareHost(h);
        if (want.isEmpty())
            continue;
        for (auto it = m_sockets.constBegin(); it != m_sockets.constEnd(); ++it) {
            if (it.value() == want && it.key()->state() == QAbstractSocket::ConnectedState)
                return it.key();
        }
    }
    if (allowUnknown) {
        for (auto it = m_sockets.constBegin(); it != m_sockets.constEnd(); ++it) {
            if (it.value().isEmpty() && it.key()->state() == QAbstractSocket::ConnectedState)
                return it.key();
        }
    }
    return nullptr;
}

bool ExtensionTransport::isReady(QString *whyNot) const
{
    QString why;
    if (!isListening())
        why = tr("Extension server is not running.");
    else if (m_cred.site.isEmpty())
        why = tr("Invalid site/mirror domain.");
    else if (!socketFor(QStringList{m_cred.site} + stakeHosts(), true))
        why = isConnected()
                  ? tr("No Stake tab with the extension is open (open https://%1).").arg(m_cred.site)
                  : tr("Browser extension not connected (ws://127.0.0.1:%1).").arg(m_port);
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
    // extension (they would then receive your API key / tokens in every request).
    if (origin.isEmpty())
        return true; // non-browser local client
    if (origin.startsWith(QLatin1String("chrome-extension://"))
        || origin.startsWith(QLatin1String("moz-extension://")))
        return true;
    // The extension opens the socket from a content script on the casino page, so it carries
    // that page's origin: any casino site it runs on, or a mirror typed in Options.
    const QString host = hostOfOrigin(origin);
    if (host.isEmpty())
        return false;
    return host == bareHost(m_cred.site) || m_allowedHosts.contains(host)
           || stakeHosts().contains(host);
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
        connect(sock, &QWebSocket::textMessageReceived, this,
                [this, sock](const QString &message) { onText(sock, message); });
        connect(sock, &QWebSocket::disconnected, this, [this, sock] { onSocketClosed(sock); });
        // The page's origin names the site; the "hello" message confirms it.
        addSocket(sock, hostOfOrigin(sock->origin()));
    }
}

// Registers a tab for `host`. A second tab of the same site stands by; a tab of unknown site
// (older extension) replaces the previous unknown one, like before.
void ExtensionTransport::addSocket(QWebSocket *sock, const QString &host)
{
    for (auto it = m_sockets.begin(); it != m_sockets.end(); ++it) {
        if (it.key() == sock || it.value() != host)
            continue;
        if (!host.isEmpty() && it.key()->state() == QAbstractSocket::ConnectedState) {
            m_sockets.remove(sock);
            sock->disconnect(this);
            sock->close(static_cast<QWebSocketProtocol::CloseCode>(kCloseDuplicate),
                        QStringLiteral("another tab of this site is connected"));
            sock->deleteLater();
            emit readyChanged(isConnected());
            return;
        }
        QWebSocket *old = it.key();
        m_sockets.erase(it);
        failAll(tr("Extension reconnected; request dropped."), old);
        old->disconnect(this);
        old->close();
        old->deleteLater();
        break;
    }
    m_sockets.insert(sock, host);
    emit readyChanged(true);
}

void ExtensionTransport::onSocketClosed(QWebSocket *socket)
{
    const bool known = m_sockets.remove(socket); // Qt 6: true when it was there
    socket->deleteLater();
    if (known) {
        failAll(tr("Browser extension disconnected."), socket);
        emit readyChanged(isConnected());
    }
}

QString ExtensionTransport::sendRequest(QWebSocket *sock, const QJsonObject &payload, Pending p,
                                        int timeoutMs)
{
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonObject msg;
    msg.insert(QStringLiteral("id"), id);
    msg.insert(QStringLiteral("payload"), payload);

    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, id] {
        auto it = m_pending.constFind(id);
        if (it == m_pending.constEnd())
            return;
        if (it->fetchCb)
            finishFetch(id, 0, QByteArray(), tr("Request timed out (extension)."));
        else
            finish(id, failure(tr("Request timed out (extension).")));
    });
    p.timer = timer;
    p.socket = sock;
    m_pending.insert(id, std::move(p));
    timer->start(timeoutMs);

    sock->sendTextMessage(QString::fromUtf8(QJsonDocument(msg).toJson(QJsonDocument::Compact)));
    return id;
}

void ExtensionTransport::post(const QString &path, const QJsonObject &body, TransportCallback cb)
{
    QString why;
    if (!isReady(&why)) {
        deferred(this, cb, failure(why));
        return;
    }
    QWebSocket *sock = socketFor(QStringList{m_cred.site} + stakeHosts(), true);

    QJsonObject headers;
    headers.insert(QStringLiteral("Content-Type"), QStringLiteral("application/json"));
    headers.insert(QStringLiteral("x-access-token"), m_cred.apiKey.trimmed());

    QJsonObject options;
    options.insert(QStringLiteral("method"), QStringLiteral("POST"));
    options.insert(QStringLiteral("headers"), headers);
    // The extension expects the body as a JSON *string* (same as the C# client).
    options.insert(QStringLiteral("body"),
                   QString::fromUtf8(QJsonDocument(body).toJson(QJsonDocument::Compact)));

    // The request goes to the site of the connected tab (same origin as the page).
    QString site = m_sockets.value(sock);
    if (site.isEmpty())
        site = m_cred.site;
    QJsonObject payload;
    payload.insert(QStringLiteral("url"), QStringLiteral("https://%1/%2").arg(site, path));
    payload.insert(QStringLiteral("options"), options);

    Pending p;
    p.cb = std::move(cb);
    sendRequest(sock, payload, std::move(p), kRequestTimeoutMs);
}

void ExtensionTransport::fetch(const QStringList &pageHosts, const QByteArray &method,
                               const QNetworkRequest &req, const QByteArray &body, FetchCallback cb)
{
    QString why;
    QWebSocket *sock = nullptr;
    if (!isListening()) {
        why = tr("Extension server is not running.");
    } else {
        sock = socketFor(pageHosts, false);
        if (!sock)
            why = isConnected() ? tr("No tab with the extension is open on %1.")
                                      .arg(pageHosts.isEmpty() ? QStringLiteral("?") : pageHosts.first())
                                : tr("Browser extension not connected (ws://127.0.0.1:%1).").arg(m_port);
    }
    if (!sock) {
        QTimer::singleShot(0, this, [cb, why] { cb(0, QByteArray(), why); });
        return;
    }

    QJsonObject headers;
    const QVariant contentType = req.header(QNetworkRequest::ContentTypeHeader);
    if (contentType.isValid())
        headers.insert(QStringLiteral("Content-Type"), contentType.toString());
    const QList<QByteArray> names = req.rawHeaderList();
    for (const QByteArray &name : names) {
        if (forwardHeader(name) && name.toLower() != "content-type")
            headers.insert(QString::fromLatin1(name), QString::fromUtf8(req.rawHeader(name)));
    }

    QJsonObject options;
    options.insert(QStringLiteral("method"), QString::fromLatin1(method));
    options.insert(QStringLiteral("headers"), headers);
    if (!body.isEmpty())
        options.insert(QStringLiteral("body"), QString::fromUtf8(body));

    const int timeoutMs = req.transferTimeout() > 0 ? req.transferTimeout() : kRequestTimeoutMs;
    QJsonObject payload;
    payload.insert(QStringLiteral("raw"), true);
    payload.insert(QStringLiteral("url"), req.url().toString(QUrl::FullyEncoded));
    payload.insert(QStringLiteral("options"), options);
    payload.insert(QStringLiteral("timeoutMs"), timeoutMs);

    Pending p;
    p.fetchCb = std::move(cb);
    // A little longer than the page's own timeout, so its answer ("timeout") arrives first.
    sendRequest(sock, payload, std::move(p), timeoutMs + 3000);
}

void ExtensionTransport::readStorage(const QStringList &pageHosts, const QString &key,
                                     StorageCallback cb)
{
    readValue(QStringLiteral("storage"), pageHosts, key, std::move(cb));
}

void ExtensionTransport::readCookie(const QStringList &pageHosts, const QString &name,
                                    StorageCallback cb)
{
    readValue(QStringLiteral("cookie"), pageHosts, name, std::move(cb));
}

// {"op":"storage"|"cookie","key":...} -> {"status":200,"body":"{\"value\":...}"}
void ExtensionTransport::readValue(const QString &op, const QStringList &pageHosts,
                                   const QString &key, StorageCallback cb)
{
    QWebSocket *sock = isListening() ? socketFor(pageHosts, false) : nullptr;
    if (!sock) {
        const QString why = tr("No tab with the extension is open on %1.")
                                .arg(pageHosts.isEmpty() ? QStringLiteral("?") : pageHosts.first());
        QTimer::singleShot(0, this, [cb, why] { cb(false, QString(), why); });
        return;
    }
    QJsonObject payload;
    payload.insert(QStringLiteral("raw"), true);
    payload.insert(QStringLiteral("op"), op);
    payload.insert(QStringLiteral("key"), key);

    Pending p;
    p.fetchCb = [cb](int status, const QByteArray &body, const QString &error) {
        if (status != 200) {
            cb(false, QString(),
               error.isEmpty() ? tr("The tab could not read it (update the browser extension).")
                               : error);
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(body).object();
        if (!o.contains(QStringLiteral("value"))) { // an older extension tried to fetch() it
            cb(false, QString(), tr("The browser extension is too old: reload the updated one."));
            return;
        }
        cb(true, o.value(QStringLiteral("value")).toString(), QString());
    };
    sendRequest(sock, payload, std::move(p), 5000);
}

void ExtensionTransport::onText(QWebSocket *sock, const QString &message)
{
    const QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (!doc.isObject())
        return;
    const QJsonObject obj = doc.object();

    if (obj.value(QStringLiteral("type")).toString() == QLatin1String("hello")) {
        const QString host = bareHost(normalizeSite(obj.value(QStringLiteral("host")).toString()));
        if (!host.isEmpty() && m_sockets.contains(sock) && m_sockets.value(sock) != host) {
            m_sockets.remove(sock);
            addSocket(sock, host); // may put it on standby when that site already has a tab
        }
        return;
    }

    const QString id = obj.value(QStringLiteral("id")).toString();
    auto it = m_pending.constFind(id);
    if (id.isEmpty() || it == m_pending.constEnd())
        return; // late reply for a request that already timed out

    const QJsonValue resp = obj.value(QStringLiteral("response"));
    if (it->fetchCb) {
        if (obj.contains(QStringLiteral("status"))) {
            finishFetch(id, obj.value(QStringLiteral("status")).toInt(),
                        obj.value(QStringLiteral("body")).toString().toUtf8(),
                        obj.value(QStringLiteral("error")).toString());
        } else if (resp.isObject() || resp.isArray()) { // older extension: JSON only
            const QJsonDocument inner = resp.isObject() ? QJsonDocument(resp.toObject())
                                                        : QJsonDocument(resp.toArray());
            finishFetch(id, 200, inner.toJson(QJsonDocument::Compact), QString());
        } else if (resp.isString()) {
            finishFetch(id, 200, resp.toString().toUtf8(), QString());
        } else {
            const QString err = obj.value(QStringLiteral("error")).toString();
            finishFetch(id, 0, QByteArray(),
                        err.isEmpty() ? tr("Unexpected response from the extension.") : err);
        }
        return;
    }

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
    if (p.fetchCb)
        p.fetchCb(0, QByteArray(), reply.error);
    else if (p.cb)
        p.cb(reply);
}

void ExtensionTransport::finishFetch(const QString &id, int status, const QByteArray &body,
                                     const QString &error)
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
    if (p.fetchCb)
        p.fetchCb(status, body, error);
    else if (p.cb)
        p.cb(failure(error.isEmpty() ? tr("Unexpected response from the extension.") : error, status));
}

// Fails the requests waiting for an answer (all, or those sent to the tab `only`).
void ExtensionTransport::failAll(const QString &why, QWebSocket *only)
{
    const QStringList ids = m_pending.keys();
    for (const QString &id : ids) {
        auto it = m_pending.constFind(id);
        if (it == m_pending.constEnd())
            continue;
        if (only && it->socket && it->socket != only)
            continue;
        finish(id, failure(why));
    }
}

// ------------------------------------------------------------------ HTTP helper

void sendHttp(QObject *context, QNetworkAccessManager &nam, ExtensionTransport *ext,
              const QStringList &pageHosts, const QByteArray &method, const QNetworkRequest &req,
              const QByteArray &body, HttpDone done)
{
    if (ext) {
        ext->fetch(pageHosts, method, req, body,
                   [done](int status, const QByteArray &data, const QString &error) {
            done(status, data, error);
        });
        return;
    }
    QNetworkReply *reply = nullptr;
    if (method == "GET")
        reply = nam.get(req);
    else if (method == "POST")
        reply = nam.post(req, body);
    else
        reply = nam.sendCustomRequest(req, method, body);
    QObject::connect(reply, &QNetworkReply::finished, context, [reply, done] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray data = reply->readAll();
        done(status, data, status > 0 ? QString() : reply->errorString());
    });
}
