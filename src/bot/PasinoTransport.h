#pragma once

#include "bot/LiveSettings.h"
#include "bot/StakeTransport.h"

#include <QMap>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QString>

// Credentials for pasino.com: only the token (localStorage "token", 64 hex characters).
struct PasinoCredentials
{
    QString token;            // sent inside every request body as "token"
    QString userAgent;        // optional override
};

// HTTPS transport for pasino.com: straight from the app ("Direct mode") or through the browser
// extension's pasino.com tab (setExtension()).
//
//  * post(path, body)  -> HTTPS POST to https://api.pasino.com/<path>. The token and
//                         "language":"en" are added to the JSON body. Like the site, the JSON is
//                         sent with "Content-Type: application/x-www-form-urlencoded".
//                         Answers: {"success":true|false,"message":"",...}
//  * requestBalances() -> POST /coin/get-balances {"token","language"}
//                         answer: {"coins":[{"coin":"BTC","image":"…","balance":"0.00000000"},…],
//                                  "success":true}
//                         every coin is listed (also those at 0); reported through
//                         balancesUpdated() / balancesFailed() with lowercase codes.
class PasinoTransport final : public IStakeTransport
{
    Q_OBJECT

public:
    explicit PasinoTransport(QObject *parent = nullptr);

    void setCredentials(const PasinoCredentials &cred);
    const PasinoCredentials &credentials() const { return m_cred; }

    // Requests go through the extension's pasino.com tab (nullptr = straight from the app).
    void setExtension(ExtensionTransport *ext);
    bool usesExtension() const { return m_ext != nullptr; }
    static QStringList pageHosts() { return {LiveSettings::siteBase(LiveSettings::Casino::Pasino)}; }

    QString name() const override
    {
        return m_ext ? QStringLiteral("Pasino (extension)") : QStringLiteral("Pasino (direct)");
    }
    bool isReady(QString *whyNot = nullptr) const override;
    void post(const QString &path, const QJsonObject &body, TransportCallback cb) override;

    // Reads all balances (lowercase currency code -> amount).
    void requestBalances();

    static QString webHost() { return LiveSettings::currentSite(LiveSettings::Casino::Pasino); }
    static QString apiBase() { return QStringLiteral("https://api.") + LiveSettings::siteBase(LiveSettings::Casino::Pasino); }
    static QString balancesPath() { return QStringLiteral("coin/get-balances"); }

    // {"coins":[{"coin":"BTC","balance":"0.1"},…]} -> lowercase code -> balance.
    // `ok` = the coin list was there.
    static QMap<QString, double> parseCoins(const QJsonObject &json, bool *ok = nullptr);
    // The error text of a {"success":false,"message":"…"} answer ("" when it succeeded).
    static QString errorText(const QJsonObject &json);

    // Pasino's coin code ("BTC", "GEM") for the app's lowercase code ("btc", "gem").
    static QString apiCurrency(const QString &code) { return code.trimmed().toUpper(); }

    // Accepts the token as copied: the bare value, JSON-quoted, with "Bearer " in front,
    // "token: <value>", or a whole copied request body {"token":"…",…}.
    static QString cleanToken(const QString &raw);

    // Request body with the token and language filled in (also used by CasinoCurrencies).
    static QByteArray makeBody(const QString &token, QJsonObject body = QJsonObject());

signals:
    // Every balance known (lowercase currency code -> available amount).
    void balancesUpdated(const QMap<QString, double> &balances);
    void balancesFailed(const QString &error);

private:
    QNetworkRequest makeRequest(const QString &path) const;
    TransportReply interpret(int status, const QByteArray &data, const QString &netError) const;

    QNetworkAccessManager m_nam;
    QPointer<ExtensionTransport> m_ext;
    PasinoCredentials m_cred;
    quint32 m_generation = 0; // bumped when the credentials change (drops stale replies)
};
