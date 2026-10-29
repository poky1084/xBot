#pragma once

#include <QHash>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <functional>

class QTimer;
class QWebSocket;
class QWebSocketServer;

// Result of one POST to the casino API.
//  ok     -> we got a JSON object back (it may still carry {"errors":[...]}
//            from the API; the caller decides what that means).
//  !ok    -> transport failure (timeout, HTTP error without JSON, Cloudflare
//            page, extension offline, ...). `error` explains.
struct TransportReply
{
    bool ok = false;
    int status = 0;          // HTTP status when known (0 for the extension path)
    QJsonObject json;
    QString error;
};

using TransportCallback = std::function<void(const TransportReply &)>;

// Shared credentials / endpoint settings.
struct StakeCredentials
{
    QString site = QStringLiteral("stake.com"); // mirror domain, no scheme
    QString apiKey;                             // sent as x-access-token
    QString clearance;                          // cf_clearance cookie (cookie mode)
    QString userAgent;                          // must match the browser that got the cookie
};

// Strips scheme/path and lowercases; returns an empty string if the result is
// not a plain hostname.
QString normalizeSite(const QString &input);

class IStakeTransport : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    virtual QString name() const = 0;
    virtual bool isReady(QString *whyNot = nullptr) const = 0;
    // `path` is relative, e.g. "_api/casino/dice/roll". The callback is always
    // invoked exactly once, on the thread that owns this object.
    virtual void post(const QString &path, const QJsonObject &body, TransportCallback cb) = 0;

signals:
    void readyChanged(bool ready);
};

// ------------------------------------------------------------------ cookie
// Direct HTTPS with the cf_clearance cookie + x-access-token header.
// NOTE: Cloudflare can bind cf_clearance to the TLS fingerprint of the browser
// that solved the challenge. If this transport gets HTTP 403, use the
// extension transport (requests then go out through the real browser).
class CookieTransport final : public IStakeTransport
{
    Q_OBJECT

public:
    explicit CookieTransport(QObject *parent = nullptr);

    void setCredentials(const StakeCredentials &cred);
    const StakeCredentials &credentials() const { return m_cred; }

    QString name() const override { return QStringLiteral("Cookie"); }
    bool isReady(QString *whyNot = nullptr) const override;
    void post(const QString &path, const QJsonObject &body, TransportCallback cb) override;

private:
    QNetworkAccessManager m_nam;
    StakeCredentials m_cred;
};

// --------------------------------------------------------------- extension
// Local WebSocket server the browser extension connects to. The extension runs in every
// casino tab it is installed for (Stake, Shuffle, Goated, Wolfbet, Duel and their mirrors);
// each tab opens its own connection and says which site it is on. A request is sent to the
// tab of the casino it belongs to, which performs fetch() inside the page (with the browser's
// own cookies and Cloudflare session) and returns the answer.
//
// Wire protocol:
//   ext -> app : {"type":"hello","host":"duel.com","version":"2.0.0"}            (on connect)
//   Stake (post(), the original protocol, also understood by older extensions):
//   app -> ext : {"id": "<guid>", "payload": {"url": "...", "options": {method, headers, body}}}
//   ext -> app : {"id": "<guid>", "response": <API JSON object | JSON string>}
//                {"id": "<guid>", "error": "..."}
//   Every casino (fetch()):
//   app -> ext : {"id": "<guid>", "payload": {"raw": true, "url": "...", "timeoutMs": 15000,
//                 "options": {method, headers, body}}}
//   ext -> app : {"id": "<guid>", "status": 200, "body": "<answer text>"}
//                {"id": "<guid>", "status": 0, "error": "..."}            (network error / timeout)
//   Reading the page's localStorage (readStorage()):
//   app -> ext : {"id": "<guid>", "payload": {"raw": true, "op": "storage", "key": "security:uuid"}}
//   ext -> app : {"id": "<guid>", "status": 200, "body": "{\"value\": <string | null>}"}
//
// One tab per site is used; a second tab of the same site is told to stand by (close code 4001)
// and takes over when the first one closes.
class ExtensionTransport final : public IStakeTransport
{
    Q_OBJECT

public:
    explicit ExtensionTransport(QObject *parent = nullptr);
    ~ExtensionTransport() override;

    // Stake's credentials (post() is Stake's request).
    void setCredentials(const StakeCredentials &cred);
    // Sites (mirrors typed in Options) a tab may connect from, besides the built-in list.
    void setAllowedHosts(const QStringList &hosts);

    // Binds 127.0.0.1 only (never the LAN). Safe to call repeatedly.
    bool start(quint16 port, QString *errorOut = nullptr);
    void stop();
    bool isListening() const;
    bool isConnected() const;                              // any tab connected
    bool isConnectedTo(const QStringList &hosts) const;    // a tab on one of `hosts`
    QStringList connectedHosts() const;                    // sites of the connected tabs
    quint16 port() const { return m_port; }

    // "www.duel.com" -> "duel.com" (hosts are compared without "www.").
    static QString bareHost(const QString &host);
    // Stake / Primedice domains the extension runs on.
    static const QStringList &stakeHosts();

    QString name() const override { return QStringLiteral("Extension"); }
    bool isReady(QString *whyNot = nullptr) const override;
    void post(const QString &path, const QJsonObject &body, TransportCallback cb) override;

    // status = HTTP status (0 = no answer: `error` explains), body = answer text.
    using FetchCallback = std::function<void(int status, const QByteArray &body, const QString &error)>;
    // Sends `req` (URL and headers; Origin, Referer, User-Agent and Cookie come from the browser)
    // from the tab on one of `pageHosts`. `cb` is always called exactly once, never re-entrantly.
    void fetch(const QStringList &pageHosts, const QByteArray &method, const QNetworkRequest &req,
               const QByteArray &body, FetchCallback cb);
    // Reads localStorage[`key`] in the tab on one of `pageHosts` (needs extension 2.0.1+).
    // `ok` false = no tab / no answer (`error` explains); a missing key gives ok with "".
    using StorageCallback = std::function<void(bool ok, const QString &value, const QString &error)>;
    void readStorage(const QStringList &pageHosts, const QString &key, StorageCallback cb);
    // Reads the cookie `name` of the tab's site, httpOnly ones included (needs extension 2.1.0+).
    void readCookie(const QStringList &pageHosts, const QString &name, StorageCallback cb);

signals:
    // A connection attempt was refused because of its Origin header.
    void originRejected(const QString &origin);

private slots:
    void onNewConnection();

private:
    struct Pending
    {
        TransportCallback cb;       // post()
        FetchCallback fetchCb;      // fetch()
        QTimer *timer = nullptr;
        QPointer<QWebSocket> socket;
    };

    bool originAllowed(const QString &origin) const;
    QWebSocket *socketFor(const QStringList &hosts, bool allowUnknown) const;
    void addSocket(QWebSocket *sock, const QString &host);
    void onText(QWebSocket *sock, const QString &message);
    void onSocketClosed(QWebSocket *socket);
    void finish(const QString &id, const TransportReply &reply);
    void finishFetch(const QString &id, int status, const QByteArray &body, const QString &error);
    void failAll(const QString &why, QWebSocket *only = nullptr);
    QString sendRequest(QWebSocket *sock, const QJsonObject &payload, Pending p, int timeoutMs);
    void readValue(const QString &op, const QStringList &pageHosts, const QString &key, StorageCallback cb);

    QWebSocketServer *m_server = nullptr;
    QHash<QWebSocket *, QString> m_sockets; // connected tabs -> their site ("" = not known yet)
    QHash<QString, Pending> m_pending;
    StakeCredentials m_cred;
    QStringList m_allowedHosts;
    quint16 m_port = 0;
};

// Sends an HTTP request either straight from the app (`nam`) or, when `ext` is set, through
// the browser extension's tab on one of `pageHosts`. `done` gets the HTTP status (0 = no
// answer), the body and the network error text; it is called exactly once, on `context`'s
// thread, never from inside this call.
using HttpDone = std::function<void(int status, const QByteArray &data, const QString &netError)>;
void sendHttp(QObject *context, QNetworkAccessManager &nam, ExtensionTransport *ext,
              const QStringList &pageHosts, const QByteArray &method, const QNetworkRequest &req,
              const QByteArray &body, HttpDone done);
