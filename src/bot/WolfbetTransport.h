#pragma once

#include "bot/StakeTransport.h"

#include <QMap>
#include <QNetworkAccessManager>
#include <QString>

// Credentials for wolfbet.com: only the token (the "value" inside the "token" cookie).
struct WolfbetCredentials
{
    QString token;            // JWT, sent as "Authorization: Bearer <token>"
    QString userAgent;        // optional override
};

// Direct HTTPS transport for wolfbet.com (no WebView2, no extension, no websocket).
//
//  * post()            -> HTTPS POST to https://wolfbet.com/<path> with the Bearer token.
//  * requestBalances() -> GET https://wolfbet.com/api/v2/user/balances
//                         answer: {"userBalances":[{"amount":"6.41","currency":"doge",...}],"nonce":...}
//                         reported through balancesUpdated() / balancesFailed().
class WolfbetTransport final : public IStakeTransport
{
    Q_OBJECT

public:
    explicit WolfbetTransport(QObject *parent = nullptr);

    void setCredentials(const WolfbetCredentials &cred);
    const WolfbetCredentials &credentials() const { return m_cred; }

    QString name() const override { return QStringLiteral("Wolfbet (direct)"); }
    bool isReady(QString *whyNot = nullptr) const override;
    void post(const QString &path, const QJsonObject &body, TransportCallback cb) override;
    // Same as post() but a GET request (e.g. "api/v1/game/seed/refresh").
    void get(const QString &path, TransportCallback cb);

    // Reads all balances over HTTPS (lowercase currency code -> amount).
    void requestBalances();

    static QString host() { return QStringLiteral("wolfbet.com"); }

    // Accepts the token in any form it is likely to be copied in:
    //  * the bare JWT,
    //  * the whole "token" cookie value: {"value":"<jwt>","expire":...} (URL-encoded or not),
    //  * with a leading "Bearer ", quotes, or a trailing "; ..." (a copied cookie line).
    static QString cleanToken(const QString &raw);

signals:
    // Every balance known (lowercase currency code -> available amount).
    void balancesUpdated(const QMap<QString, double> &balances);
    void balancesFailed(const QString &error);

private:
    QNetworkRequest makeRequest(const QString &path, bool json) const;
    void handleReply(class QNetworkReply *reply, TransportCallback cb);

    QNetworkAccessManager m_nam;
    WolfbetCredentials m_cred;
    quint32 m_generation = 0; // bumped when the credentials change (drops stale replies)
};
