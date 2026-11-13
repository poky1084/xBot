#pragma once

#include "bot/LiveSettings.h"
#include "bot/StakeTransport.h"

#include <QMap>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QString>
#include <QStringList>

// Credentials for tower.bet. The site is behind Cloudflare: plain HTTPS from the app needs the
// "cf_clearance" cookie together with the User-Agent of the browser that got it.
struct TowerCredentials
{
    QString token;            // "__tt" cookie, sent as "Authorization: Bearer <token>"
    QString clearance;        // "cf_clearance" cookie (httpOnly; may be empty when not challenged)
    QString userAgent;        // User-Agent of the browser that got cf_clearance (empty = default)
};

// HTTPS transport for tower.bet: straight from the app ("Direct mode", with cf_clearance) or
// through the browser extension's tower.bet tab (setExtension(): the tab sends its own cookies).
//
//  * post(path, body)  -> HTTPS POST https://tower.bet/api/<path> (JSON) with the Bearer token.
//                         Laravel-style errors: {"message":"…","errors":{…}} with HTTP 4xx.
//  * requestBalances() -> GET /api/accounts
//                         answer: [{"hash":"e4e72b85e4","balance":"0.48999596","symbol":"USDC",
//                                   "type":"main"}]
//                         only the currencies the account holds are listed (any other = 0);
//                         reported through balancesUpdated() / balancesFailed(), lowercase codes.
//  * currencies:       GET /api/currencies -> [{"symbol":"USDC","order":7,…},…] (CasinoCurrencies)
class TowerTransport final : public IStakeTransport
{
    Q_OBJECT

public:
    explicit TowerTransport(QObject *parent = nullptr);

    void setCredentials(const TowerCredentials &cred);
    const TowerCredentials &credentials() const { return m_cred; }

    // Requests go through the extension's tower.bet tab (nullptr = straight from the app).
    void setExtension(ExtensionTransport *ext);
    bool usesExtension() const { return m_ext != nullptr; }
    static QStringList pageHosts() { return {LiveSettings::siteBase(LiveSettings::Casino::Tower)}; }

    QString name() const override
    {
        return m_ext ? QStringLiteral("Tower (extension)") : QStringLiteral("Tower (direct)");
    }
    bool isReady(QString *whyNot = nullptr) const override;
    void post(const QString &path, const QJsonObject &body, TransportCallback cb) override;

    // Reads all balances (lowercase currency code -> amount).
    void requestBalances();

    static QString webHost() { return LiveSettings::currentSite(LiveSettings::Casino::Tower); }
    static QString apiBase() { return QStringLiteral("https://%1/api").arg(webHost()); }
    static QString defaultUserAgent();

    // A request to https://tower.bet/api/<path> with the headers the site sends (also used by
    // CasinoCurrencies for the currency list).
    static QNetworkRequest buildRequest(const QString &path, const QString &token,
                                        const QString &clearance, const QString &userAgent);

    // [{"symbol":"USDC","balance":"0.49","type":"main"},…] -> lowercase code -> balance.
    // `ok` = the answer was that list.
    static QMap<QString, double> parseAccounts(const QByteArray &body, bool *ok = nullptr);
    // [{"symbol":"USDC","order":7,…},…] -> lowercase codes in the site's order.
    static QStringList parseCurrencies(const QByteArray &body);
    // The site's currency list (fallback for the dropdown until it was fetched once).
    static QStringList defaultCurrencies();

    // Tower's symbol ("USDC") for the app's lowercase code ("usdc").
    static QString apiCurrency(const QString &code) { return code.trimmed().toUpper(); }

    // Accept the values as copied: bare, JSON-quoted, "Bearer <t>", "__tt=<t>", "cf_clearance=<v>".
    static QString cleanToken(const QString &raw);
    static QString cleanClearance(const QString &raw);

    // Readable error text of a failed answer (Laravel {"message"}, Cloudflare page, …).
    static QString errorText(int status, const QByteArray &data);

signals:
    // Every balance known (lowercase currency code -> available amount).
    void balancesUpdated(const QMap<QString, double> &balances);
    void balancesFailed(const QString &error);

private:
    TransportReply interpret(int status, const QByteArray &data, const QString &netError) const;

    QNetworkAccessManager m_nam;
    QPointer<ExtensionTransport> m_ext;
    TowerCredentials m_cred;
    quint32 m_generation = 0; // bumped when the credentials change (drops stale replies)
};
