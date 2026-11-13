#pragma once

#include "bot/LiveSettings.h"
#include "bot/StakeTransport.h"

#include <QDateTime>
#include <QMap>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <functional>

// Credentials for winna.com. The account session is the "connect.sid" cookie (httpOnly); the
// site is behind Cloudflare, so plain HTTPS from the app needs "cf_clearance" with the
// User-Agent of the browser that got it.
struct WinnaCredentials
{
    QString session;          // "connect.sid" cookie value ("s%3A…")
    QString clearance;        // "cf_clearance" cookie (may be empty when not challenged)
    QString userAgent;        // User-Agent of the browser that got cf_clearance (empty = default)
    QString deviceId;         // localStorage "wn-antifraud-device-id" -> "id" (x-antifraud-device-id)
};

// HTTPS transport for winna.com: straight from the app ("Direct mode") or through the browser
// extension's winna.com tab (setExtension(): the tab sends its own cookies).
//
// Account API (https://api2.winna.com, session cookie):
//  * requestBalances() -> GET /v1/payments/wallets
//      {"success":true,"response":{"payload":[{"coin":"USDC","balance":"500000000" (1e-8 units),
//       "is_vault":false,"origin_amount":5,…},…]}}  every coin twice (main + vault): the main
//      (non-vault) wallet is the betting balance.
//  * POST /v1/user/change-wallet-coin {"coin":"USDT"}  selects the wallet the games use
//  * POST /v1/games/our-game/start {"game":"dice","platform":"GPL_DESKTOP"}
//      -> {"response":{"result":{"jwt":"<game token>","token":"87344-USDC-USDC-<name>",…}}}
// Game API (https://originals.winna.com, header "token: <game jwt>" + "sid: <uuid>"):
//  * post("dice/play" | "limbo/play", body) -> see StakeClient::placeWinnaDice() / placeWinnaLimbo().
// The game token belongs to one coin and game: post() switches the wallet coin and starts a new
// game session when the bet currency or the game changes or the token is about to expire.
class WinnaTransport final : public IStakeTransport
{
    Q_OBJECT

public:
    explicit WinnaTransport(QObject *parent = nullptr);

    void setCredentials(const WinnaCredentials &cred);
    const WinnaCredentials &credentials() const { return m_cred; }

    // Requests go through the extension's winna.com tab (nullptr = straight from the app).
    void setExtension(ExtensionTransport *ext);
    bool usesExtension() const { return m_ext != nullptr; }
    static QStringList pageHosts() { return {LiveSettings::siteBase(LiveSettings::Casino::Winna)}; }

    QString name() const override
    {
        return m_ext ? QStringLiteral("Winna (extension)") : QStringLiteral("Winna (direct)");
    }
    bool isReady(QString *whyNot = nullptr) const override;
    // A game request on originals.winna.com (path e.g. "dice/play") with the game token of the
    // currency set by setCoin(). Fetches / renews the token first when needed.
    void post(const QString &path, const QJsonObject &body, TransportCallback cb) override;

    // The coin the game token must be for ("usdc", "btc", …).
    void setCoin(const QString &code);

    // Reads all balances (lowercase currency code -> amount).
    void requestBalances();

    // New seed pair with `clientSeed`: reads the active seed (GET /v1/games/our-game/get-active-seed
    // -> its "id"), then POST /v1/games/our-game/unhash {"seedId","clientSeed"} (the active seed is
    // revealed, a new one becomes active). `done(error, newSeed)`: error "" = rotated; newSeed =
    // {"id","nonce","client_seed","server_seed" (hash),"next_server_seed"}.
    void rotateSeed(const QString &clientSeed,
                    std::function<void(const QString &error, const QJsonObject &newSeed)> done);

    // Moves `amount` of `coin` into the vault: POST /v2/vault/deposit {"coin":"USDC","balance":"1"}
    // ("balance" = the amount) -> {"success":true}. `done(error)`: "" = deposited.
    void vaultDeposit(const QString &coin, double amount, std::function<void(const QString &error)> done);

    static QString webHost() { return LiveSettings::currentSite(LiveSettings::Casino::Winna); }
    static QString apiBase() { return QStringLiteral("https://api2.") + LiveSettings::siteBase(LiveSettings::Casino::Winna); }
    static QString gameBase() { return QStringLiteral("https://originals.") + LiveSettings::siteBase(LiveSettings::Casino::Winna); }
    // Page the game frames are served from (Origin / Referer of game requests).
    static QString gamesHost() { return QStringLiteral("games-prod.") + LiveSettings::siteBase(LiveSettings::Casino::Winna); }
    static QString defaultUserAgent();

    // An account API request (https://api2.winna.com/<path>) with the site's headers and cookies
    // (also used by CasinoCurrencies for the currency list).
    static QNetworkRequest buildApiRequest(const QString &path, const WinnaCredentials &cred,
                                           bool viaExtension = false);

    // wallets answer -> lowercase code -> balance of the main (non-vault) wallet.
    static QMap<QString, double> parseWallets(const QByteArray &body, bool *ok = nullptr);
    static QStringList parseWalletCoins(const QByteArray &body);
    // The site's coins (fallback for the dropdown until the list was fetched once).
    static QStringList defaultCurrencies();

    static QString apiCurrency(const QString &code) { return code.trimmed().toUpper(); }

    // Accept the values as copied: bare, quoted, "name=value", URL-decoded "s:…" session.
    static QString cleanSession(const QString &raw);
    static QString cleanClearance(const QString &raw);
    static QString cleanDeviceId(const QString &raw); // also {"id":"…","owner":…} JSON

    // Readable error text of a failed answer.
    static QString errorText(int status, const QByteArray &data);

signals:
    void balancesUpdated(const QMap<QString, double> &balances);
    void balancesFailed(const QString &error);
    // A short note for the log (game session started for a coin, …).
    void notice(const QString &message);

private:
    using RawDone = std::function<void(int status, const QByteArray &data, const QString &netError)>;
    void sendApi(const QByteArray &method, const QString &path, const QJsonObject &body, RawDone done);
    // Makes sure a game token for m_coin exists, then calls `then(error)` ("" = ready).
    // `game`: the originals game the session is started for ("dice", "limbo").
    void ensureGameToken(const QString &game, std::function<void(const QString &error)> then);
    void sendGame(const QString &path, const QJsonObject &body, TransportCallback cb, bool retried);
    TransportReply interpret(int status, const QByteArray &data, const QString &netError) const;

    QNetworkAccessManager m_nam;
    QPointer<ExtensionTransport> m_ext;
    WinnaCredentials m_cred;
    quint32 m_generation = 0; // bumped when the credentials change (drops stale replies)

    QString m_coin;           // wanted game coin (lowercase)
    QString m_gameToken;      // game jwt
    QString m_gameCoin;       // coin the game jwt was made for (lowercase)
    QString m_gameName;       // game the jwt was made for ("dice", "limbo")
    QDateTime m_gameExpiry;   // jwt "exp"
    QString m_sid;            // game session id ("sid" header)
    bool m_tokenBusy = false;
    QList<std::function<void(const QString &)>> m_tokenWaiters;
};
