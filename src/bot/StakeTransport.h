#pragma once

#include <QHash>
#include <QJsonObject>
#include <QNetworkAccessManager>
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
// Local WebSocket server the browser extension connects to. Each request is
// forwarded to the extension, which performs fetch() inside the browser (with
// the browser's own Cloudflare session) and answers with the API JSON.
//
// Wire protocol (identical to the C# BrowserFetch):
//   app -> ext : {"id": "<guid>", "payload": {"url": "...", "options": {method, headers, body}}}
//   ext -> app : {"id": "<guid>", "response": <API JSON object | JSON string>}
class ExtensionTransport final : public IStakeTransport
{
    Q_OBJECT

public:
    explicit ExtensionTransport(QObject *parent = nullptr);
    ~ExtensionTransport() override;

    void setCredentials(const StakeCredentials &cred);

    // Binds 127.0.0.1 only (never the LAN). Safe to call repeatedly.
    bool start(quint16 port, QString *errorOut = nullptr);
    void stop();
    bool isListening() const;
    bool isConnected() const;
    quint16 port() const { return m_port; }

    QString name() const override { return QStringLiteral("Extension"); }
    bool isReady(QString *whyNot = nullptr) const override;
    void post(const QString &path, const QJsonObject &body, TransportCallback cb) override;

signals:
    // A connection attempt was refused because of its Origin header.
    void originRejected(const QString &origin);

private slots:
    void onNewConnection();

private:
    struct Pending
    {
        TransportCallback cb;
        QTimer *timer = nullptr;
    };

    bool originAllowed(const QString &origin) const;
    void onText(const QString &message);
    void onSocketClosed(QWebSocket *socket);
    void finish(const QString &id, const TransportReply &reply);
    void failAll(const QString &why);

    QWebSocketServer *m_server = nullptr;
    QPointer<QWebSocket> m_socket;
    QHash<QString, Pending> m_pending;
    StakeCredentials m_cred;
    quint16 m_port = 0;
};
