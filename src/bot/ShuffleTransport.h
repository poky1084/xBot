#pragma once

#include "bot/StakeTransport.h"

#include <QMap>
#include <QPointer>
#include <QSet>
#include <QString>

class QNetworkReply;
class QTimer;
class QWebSocket;

// Credentials for shuffle.game "Direct mode".
struct ShuffleCredentials
{
    QString site = QStringLiteral("shuffle.com"); // mirror domain, no scheme
    QString token;                                 // JWT, sent as "Authorization: Bearer <token>"
    QString correlationUserId;                     // CORRELATION_USER_ID
    QString browserId;                             // BROWSER_ID
    QString userAgent;                             // optional override
};

// Direct (no WebView2, no browser extension) transport for shuffle.game.
//
//  * post()            -> HTTPS POST to https://<site>/<path> with the Bearer token and an
//                         X-Correlation-Id header (BROWSER_ID-<random21>-t::CORRELATION_USER_ID).
//  * requestBalances() -> opens wss://<site>/main-api/bp-subscription/subscription/graphql
//                         (subprotocol graphql-transport-ws), authenticates with connection_init,
//                         subscribes to BalanceUpdated and reports every balance it receives.
//
// The BalanceUpdated subscription is push-only (it reports changes), so the full list of
// balances is also read once over HTTPS shortly after the subscription started.
class ShuffleTransport final : public IStakeTransport
{
    Q_OBJECT

public:
    explicit ShuffleTransport(QObject *parent = nullptr);
    ~ShuffleTransport() override;

    void setCredentials(const ShuffleCredentials &cred);
    const ShuffleCredentials &credentials() const { return m_cred; }

    // Lowercase currency code the bot bets with ("" = none chosen yet).
    void setWantedCurrency(const QString &currency) { m_wanted = currency.trimmed().toLower(); }

    QString name() const override { return QStringLiteral("Shuffle (direct)"); }
    bool isReady(QString *whyNot = nullptr) const override;
    void post(const QString &path, const QJsonObject &body, TransportCallback cb) override;

    // Connects the balance WebSocket when needed and reports balances through
    // balancesUpdated() / balancesFailed(). Cheap to call repeatedly.
    void requestBalances();
    void closeSocket();
    bool socketReady() const { return m_acked; }

signals:
    // Every balance known so far (lowercase currency code -> available amount).
    void balancesUpdated(const QMap<QString, double> &balances);
    void balancesFailed(const QString &error);

private:
    QString correlationId() const;
    QString bearerToken() const;
    void openSocket();
    void onSocketText(const QString &message);
    void onSocketClosed();
    void sendJson(const QJsonObject &obj);
    void failConnection(const QString &why);
    void fetchBalancesHttp();
    void emitBalances();

    QNetworkAccessManager m_nam;
    ShuffleCredentials m_cred;
    QString m_wanted;

    QPointer<QWebSocket> m_socket;
    QTimer *m_ackTimer = nullptr;       // no connection_ack in time
    QTimer *m_fallbackTimer = nullptr;  // no balance pushed in time -> read it over HTTPS
    bool m_acked = false;
    bool m_connectFailureReported = false;
    bool m_httpInFlight = false;
    QMap<QString, double> m_balances;
    QSet<QString> m_fresh;              // currencies updated since the socket was opened
    QSet<QString> m_wsFresh;            // ... of which pushed by the WebSocket
    quint32 m_generation = 0;           // bumped when the credentials change (drops stale replies)
};
