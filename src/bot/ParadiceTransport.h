#pragma once

#include "bot/StakeTransport.h"

#include <QMap>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QString>

// Credentials for paradice.in: only the token (localStorage "token").
struct ParadiceCredentials
{
    QString token;            // JWT, sent as "x-access-token: <token>"
    QString userAgent;        // optional override
};

// HTTPS transport for paradice.in: straight from the app ("Direct mode") or through the browser
// extension's paradice.in tab (setExtension()). Every call is GraphQL on one endpoint.
//
//  * post()            -> HTTPS POST to https://api.paradice.in/api.php with the token in
//                         "x-access-token" (the path argument is ignored: there is one endpoint).
//  * requestBalances() -> query { me { wallets { balance currency } } }
//                         answer: {"data":{"me":{"wallets":[{"balance":8.5e-7,"currency":"BTC"},…]}}}
//                         every wallet is listed (also those at 0); reported through
//                         balancesUpdated() / balancesFailed() with lowercase codes.
class ParadiceTransport final : public IStakeTransport
{
    Q_OBJECT

public:
    explicit ParadiceTransport(QObject *parent = nullptr);

    void setCredentials(const ParadiceCredentials &cred);
    const ParadiceCredentials &credentials() const { return m_cred; }

    // Requests go through the extension's paradice.in tab (nullptr = straight from the app).
    void setExtension(ExtensionTransport *ext);
    bool usesExtension() const { return m_ext != nullptr; }
    static QStringList pageHosts() { return {QStringLiteral("paradice.in")}; }

    QString name() const override
    {
        return m_ext ? QStringLiteral("Paradice (extension)") : QStringLiteral("Paradice (direct)");
    }
    bool isReady(QString *whyNot = nullptr) const override;
    void post(const QString &path, const QJsonObject &body, TransportCallback cb) override;

    // Reads all balances (lowercase currency code -> amount).
    void requestBalances();

    static QString webHost() { return QStringLiteral("paradice.in"); }
    static QString apiUrl() { return QStringLiteral("https://api.paradice.in/api.php"); }

    // The GraphQL query that lists every wallet (also used by CasinoCurrencies).
    static QString balancesQuery();
    // {"data":{"me":{"wallets":[...]}}} -> lowercase code -> balance. `ok` = the wallets were there.
    static QMap<QString, double> parseWallets(const QJsonObject &json, bool *ok = nullptr);

    // Paradice's CurrencyEnum ("BTC", "USDT_TRC") for the app's lowercase code ("btc", "usdt_trc").
    static QString apiCurrency(const QString &code) { return code.trimmed().toUpper(); }

    // Accepts the token as copied: the bare JWT, JSON-quoted, with "Bearer " in front, or a
    // copied "x-access-token: <jwt>" header line.
    static QString cleanToken(const QString &raw);

signals:
    // Every balance known (lowercase currency code -> available amount).
    void balancesUpdated(const QMap<QString, double> &balances);
    void balancesFailed(const QString &error);

private:
    QNetworkRequest makeRequest() const;
    TransportReply interpret(int status, const QByteArray &data, const QString &netError) const;

    QNetworkAccessManager m_nam;
    QPointer<ExtensionTransport> m_ext;
    ParadiceCredentials m_cred;
    quint32 m_generation = 0; // bumped when the credentials change (drops stale replies)
};
