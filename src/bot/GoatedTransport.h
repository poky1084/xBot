#pragma once

#include "bot/StakeTransport.h"

#include <QMap>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QString>

// Credentials for goated.com: only the token (localStorage "g3_token").
struct GoatedCredentials
{
    QString token;            // JWT, sent as "Authorization: Bearer <token>"
    QString userAgent;        // optional override
};

// HTTPS transport for goated.com: straight from the app ("Direct mode") or through the browser
// extension's goated.com tab (setExtension()).
//
//  * post()            -> HTTPS POST to https://apis.goated.com/<path> with the Bearer token.
//  * requestBalances() -> GET https://apis.goated.com/user/wallet/balances
//                         answer: {"success":true,"data":{"USDC":0.99324}}
//                         reported through balancesUpdated() / balancesFailed().
class GoatedTransport final : public IStakeTransport
{
    Q_OBJECT

public:
    explicit GoatedTransport(QObject *parent = nullptr);

    void setCredentials(const GoatedCredentials &cred);
    const GoatedCredentials &credentials() const { return m_cred; }

    // Requests go through the extension's goated.com tab (nullptr = straight from the app).
    void setExtension(ExtensionTransport *ext);
    bool usesExtension() const { return m_ext != nullptr; }
    static QStringList pageHosts() { return {QStringLiteral("goated.com")}; }

    QString name() const override
    {
        return m_ext ? QStringLiteral("Goated (extension)") : QStringLiteral("Goated (direct)");
    }
    bool isReady(QString *whyNot = nullptr) const override;
    void post(const QString &path, const QJsonObject &body, TransportCallback cb) override;

    // Reads all balances over HTTPS (lowercase currency code -> amount).
    void requestBalances();

    static QString apiHost() { return QStringLiteral("apis.goated.com"); }
    static QString webHost() { return QStringLiteral("www.goated.com"); }

    // Accepts a token as copied from DevTools: strips quotes, a leading "Bearer " and
    // anything after a ';' (a copied "cookie" line).
    static QString cleanToken(const QString &raw);

signals:
    // Every balance known (lowercase currency code -> available amount).
    void balancesUpdated(const QMap<QString, double> &balances);
    void balancesFailed(const QString &error);

private:
    QNetworkRequest makeRequest(const QString &path, bool json) const;
    void send(const QByteArray &method, const QString &path, const QByteArray &body, bool json,
              TransportCallback cb);
    TransportReply interpret(int status, const QByteArray &data, const QString &netError) const;

    QNetworkAccessManager m_nam;
    QPointer<ExtensionTransport> m_ext;
    GoatedCredentials m_cred;
    QString m_deviceId;
    QString m_sessionId;
    quint32 m_generation = 0; // bumped when the credentials change (drops stale replies)
};
