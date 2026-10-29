#pragma once

#include "bot/StakeTransport.h"
#include "ui/DuelBrowser.h" // complete type for QPointer<DuelBrowser> (Qt headers only)

#include <QDateTime>
#include <QList>
#include <QMap>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <functional>

// Credentials for duel.com (all read by "Open Duel.com & Log in" in Options).
struct DuelCredentials
{
    QString token;      // value of the "duel" session cookie ("Token" in Options)
    QString uuid;       // localStorage "security:uuid" (also sent as x-duel-device-identifier)
    QString clearance;  // value of the "cf_clearance" cookie (optional, but Cloudflare may need it)
    QString userAgent;  // User-Agent of the browser that got cf_clearance (it is bound to it)
    QString site = QStringLiteral("duel.com"); // Duel's host or a mirror ("Site / mirror")
};

// Transport for duel.com. Duel's Cloudflare drops connections that are not from a real browser
// (plain Qt HTTPS times out / is closed), so every request is a fetch() inside a hidden WebView2
// page on duel.com (see ui/DuelBrowser.h): the browser's own TLS, cookies and User-Agent are
// used, plus the header "x-duel-device-identifier: <uuid>".
//
//  * Security token: bets need a short-lived "security_token" (valid 600 s):
//      POST https://duel.com/api/v2/user/security/token  {"uuid":"<uuid>","code":"0000","type":"standard"}
//      -> {"success":true,"token":"...","token_type":"standard","expires_in":600}
//    withSecurityToken() hands out the cached one and fetches a new one shortly before it
//    expires (or when a bet reports it as expired: invalidateSecurityToken()).
//  * post() / get()    -> HTTPS to https://duel.com/<path>.
//  * requestBalances() -> GET https://duel.com/api/v2/metadata?uuid=<uuid>
//                         answer: {..., "user":{..., "balances":[{"balance_type":106,
//                           "balance":"1.0156","balance_type_name":"USDC",...}]}}
//                         ("user" is null / missing when the session is not logged in).
//
// Currencies are numbers on Duel: 101 = btc, 102 = bch, ... in the order of the site's wallet
// list (see currencyCodes()).
class DuelTransport final : public IStakeTransport
{
    Q_OBJECT

public:
    explicit DuelTransport(QObject *parent = nullptr);

    void setCredentials(const DuelCredentials &cred);
    // The hidden browser the requests go through (owned by the main window).
    void setBrowser(DuelBrowser *browser);
    DuelBrowser *browser() const { return m_browser; }
    // Requests go through the browser extension's Duel tab instead of the hidden browser
    // (nullptr = hidden browser). The tab's own "duel" cookie is used: no token needed.
    void setExtension(ExtensionTransport *ext);
    bool usesExtension() const { return m_ext != nullptr; }
    QStringList pageHosts() const { return {m_cred.site}; }
    const DuelCredentials &credentials() const { return m_cred; }

    QString name() const override
    {
        return m_ext ? QStringLiteral("Duel (extension)") : QStringLiteral("Duel (direct)");
    }
    bool isReady(QString *whyNot = nullptr) const override;
    void post(const QString &path, const QJsonObject &body, TransportCallback cb) override;
    void get(const QString &path, TransportCallback cb);

    // Calls `cb` exactly once with a valid security token, or with an empty token and the error.
    using SecurityTokenCallback = std::function<void(const QString &token, const QString &error, int status)>;
    void withSecurityToken(SecurityTokenCallback cb);
    // The server said the token is no longer valid: the next withSecurityToken() fetches a new one.
    void invalidateSecurityToken();

    // Reads all balances over HTTPS (lowercase currency code -> amount).
    void requestBalances();

    static QString host() { return QStringLiteral("duel.com"); }

    // Duel's currency list in the site's order; currency id = 101 + index.
    static const QStringList &currencyCodes();
    static int currencyId(const QString &code);   // 0 = unknown
    static QString currencyCode(int id);          // "" = unknown

    // Accepts a cookie value as copied from DevTools: strips "name=", quotes and anything
    // after a ';' (a copied cookie line).
    static QString cleanCookie(const QString &raw, const QString &name);
    static QString cleanUuid(const QString &raw);

    // Path of the metadata request that carries the balances.
    static QString metadataPath(const QString &uuid);
    // Reads user.balances of a metadata answer (lowercase code -> balance). Returns false with
    // `error` set when the answer has no logged-in user. Currency ids / names seen in the answer
    // are remembered (so a currency added to the site later is known by its id).
    static bool parseMetadataBalances(const QJsonObject &json, QMap<QString, double> *balances,
                                      QString *error = nullptr);
    // Every currency known (site order by id): the fixed list plus any seen in an answer.
    static QStringList allCurrencyCodes();

signals:
    // Every balance known (lowercase currency code -> available amount).
    void balancesUpdated(const QMap<QString, double> &balances);
    void balancesFailed(const QString &error);
    // Informational message for the log (e.g. the balance could not be read).
    void notice(const QString &message);

private:
    void send(const QString &method, const QString &path, const QByteArray &body, TransportCallback cb,
              bool balancesOnly = false);
    void fetchSecurityToken();
    // Extension mode: the Duel tab's own localStorage "security:uuid" (its session is bound to it).
    QString uuid() const { return (m_ext && !m_tabUuid.isEmpty()) ? m_tabUuid : m_cred.uuid; }
    // Calls `next` once the tab's UUID is known (at once without the extension); error = why not.
    void withTabUuid(std::function<void(const QString &error)> next);

    QPointer<DuelBrowser> m_browser;
    QPointer<ExtensionTransport> m_ext;
    QString m_tabUuid;                                         // read from the Duel tab
    bool m_readingTabUuid = false;
    QList<std::function<void(const QString &)>> m_tabUuidWaiters;
    DuelCredentials m_cred;
    quint32 m_generation = 0; // bumped when the credentials change (drops stale replies)

    QString m_securityToken;
    QDateTime m_securityExpiry;
    bool m_fetchingToken = false;
    QList<SecurityTokenCallback> m_tokenWaiters;
};
