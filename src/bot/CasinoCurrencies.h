#pragma once

// Valid bet currencies per casino, for the Options dialog's "Bet currency" dropdown.
//
// Each casino with a known source has a public request that lists every currency it
// supports. The list is fetched when the Options dialog shows that casino and cached (in
// memory and in QSettings "CurrencyLists/<casino>", shared by all windows), so the dropdown
// is filled at once next time, also offline. Codes are lowercase, like the bet currency.
//
// Sources:
//   Goated:  GET https://apis.goated.com/main/exchange-rates (public)
//            -> data.crypto (USDC, ETH, USDT, …) then data.fiats (USD, EUR, …)
//   Wolfbet: GET https://wolfbet.com/api/v2/user/balances (needs the login token)
//            -> userBalances[].currency: every currency, also those at 0 ("free" included)
//   Paradice: POST https://api.paradice.in/api.php, GraphQL "me { wallets }" (needs the token)
//            -> data.me.wallets[].currency: every wallet, also those at 0 (BTC, USDT_TRC, PRDC, …)
//   Pasino:  POST https://api.pasino.com/coin/get-balances {"token","language"} (needs the token)
//            -> coins[].coin: every coin, also those at 0 (BTC, ETH, …, GEM)
//   Tower:   GET https://tower.bet/api/currencies (Bearer token; cf_clearance + User-Agent in
//            Direct mode) -> [].symbol in the site's "order"; until the first fetch the site's
//            list as of this writing (TowerTransport::defaultCurrencies()).
//   Winna:   GET https://api2.winna.com/v1/payments/wallets (session cookie; cf_clearance +
//            User-Agent in Direct mode) -> response.payload[].coin (each listed twice: main and
//            vault); until the first fetch the site's list (WinnaTransport::defaultCurrencies()).
//   Stake:   no public list; the Options dialog collects the currencies from the
//            UserBalances answer (fetched automatically once Site + API key are filled in)
//            and merges them into this cache.
//   Duel:    the site's wallet list (id 101 = btc, 102 = bch, ...; DuelTransport::currencyCodes()),
//            plus the account's currencies from GET /api/v2/metadata (user.balances), which the
//            login / balance fetch reads through the hidden Duel browser (Duel's Cloudflare
//            refuses plain HTTPS); the Options dialog merges them in (setLiveCurrencies).
//   Shuffle: fixed list (the site's wallet list); "gram" is sent to Shuffle as "TON",
//            see ShuffleTransport::apiCurrency().
//
// Header-only and without Q_OBJECT, so it needs no extra build / moc entry.

#include "bot/LiveSettings.h"
#include "bot/DuelTransport.h"
#include "bot/GoatedTransport.h"
#include "bot/ParadiceTransport.h"
#include "bot/PasinoTransport.h"
#include "bot/TowerTransport.h"
#include "bot/WinnaTransport.h"
#include "bot/WolfbetTransport.h"

#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSettings>
#include <QStringList>
#include <QUrl>

#include <algorithm>
#include <functional>

namespace CasinoCurrencies
{
inline QString cacheKey(LiveSettings::Casino casino)
{
    return QStringLiteral("CurrencyLists/") + LiveSettings::nameOf(casino);
}

inline QHash<int, QStringList> &memoryCache()
{
    static QHash<int, QStringList> cache;
    return cache;
}

// True when the casino's currency list can be fetched (more casinos are added over time).
inline bool hasSource(LiveSettings::Casino casino)
{
    return casino == LiveSettings::Casino::Goated || casino == LiveSettings::Casino::Wolfbet
           || casino == LiveSettings::Casino::Paradice || casino == LiveSettings::Casino::Pasino
           || casino == LiveSettings::Casino::Tower || casino == LiveSettings::Casino::Winna;
}

// Casinos with a fixed currency list (in the site's own order).
inline QStringList builtIn(LiveSettings::Casino casino)
{
    if (casino == LiveSettings::Casino::Shuffle) {
        return {QStringLiteral("btc"),  QStringLiteral("eth"),   QStringLiteral("usdt"),
                QStringLiteral("usdc"), QStringLiteral("shfl"),  QStringLiteral("sol"),
                QStringLiteral("ltc"),  QStringLiteral("xrp"),   QStringLiteral("trx"),
                QStringLiteral("doge"), QStringLiteral("pol"),   QStringLiteral("avax"),
                QStringLiteral("bnb"),  QStringLiteral("gram"),  QStringLiteral("bonk"),
                QStringLiteral("shib"), QStringLiteral("wif"),   QStringLiteral("pump"),
                QStringLiteral("trump"), QStringLiteral("dai"),  QStringLiteral("cash")};
    }
    return {};
}

// Last list fetched for the casino (empty = never fetched); the fixed list for casinos
// that have one.
inline QStringList cached(LiveSettings::Casino casino)
{
    const QStringList fixed = builtIn(casino);
    if (!fixed.isEmpty())
        return fixed;
    QHash<int, QStringList> &mem = memoryCache();
    if (!mem.contains(int(casino)))
        mem.insert(int(casino), QSettings().value(cacheKey(casino)).toStringList());
    // Duel: the site's wallet list until the first fetch.
    if (casino == LiveSettings::Casino::Duel && mem.value(int(casino)).isEmpty())
        return DuelTransport::allCurrencyCodes();
    // Tower: the site's list until the first fetch.
    if (casino == LiveSettings::Casino::Tower && mem.value(int(casino)).isEmpty())
        return TowerTransport::defaultCurrencies();
    // Winna: the site's list until the first fetch.
    if (casino == LiveSettings::Casino::Winna && mem.value(int(casino)).isEmpty())
        return WinnaTransport::defaultCurrencies();
    return mem.value(int(casino));
}

inline void store(LiveSettings::Casino casino, const QStringList &codes)
{
    memoryCache().insert(int(casino), codes);
    QSettings().setValue(cacheKey(casino), codes);
}

namespace detail
{
inline QNetworkAccessManager *nam()
{
    static QPointer<QNetworkAccessManager> manager;
    if (!manager)
        manager = new QNetworkAccessManager(qApp);
    return manager;
}

inline QString userAgent()
{
    return QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                          "(KHTML, like Gecko) Chrome/155.0.0.0 Safari/537.36");
}

// Keys of a JSON object, lowercase and sorted.
inline QStringList codesOf(const QJsonObject &obj)
{
    QStringList out;
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
        out << it.key().trimmed().toLower();
    out.removeAll(QString());
    std::sort(out.begin(), out.end());
    return out;
}

// Goated /main/exchange-rates: {"success":true,"data":{"crypto":{…},"fiats":{…}}}
// Wolfbet /api/v2/user/balances: {"userBalances":[{"amount":"0","currency":"btc"},…]}
inline QStringList parseWolfbet(const QByteArray &body)
{
    const QJsonArray arr = QJsonDocument::fromJson(body).object()
                               .value(QStringLiteral("userBalances")).toArray();
    QStringList out;
    for (const QJsonValue &v : arr) {
        const QString c = v.toObject().value(QStringLiteral("currency")).toString().trimmed().toLower();
        if (!c.isEmpty() && !out.contains(c))
            out << c;
    }
    std::sort(out.begin(), out.end());
    return out;
}

// Paradice: {"data":{"me":{"wallets":[{"balance":0,"currency":"USDT_TRC"},…]}}}
inline QStringList parseParadice(const QByteArray &body)
{
    QStringList out = ParadiceTransport::parseWallets(QJsonDocument::fromJson(body).object()).keys();
    std::sort(out.begin(), out.end());
    return out;
}

// Pasino: {"coins":[{"coin":"BTC","balance":"0.00000000"},…],"success":true}
inline QStringList parsePasino(const QByteArray &body)
{
    QStringList out = PasinoTransport::parseCoins(QJsonDocument::fromJson(body).object()).keys();
    std::sort(out.begin(), out.end());
    return out;
}

inline QStringList parseGoated(const QByteArray &body)
{
    const QJsonObject root = QJsonDocument::fromJson(body).object();
    const QJsonObject data = root.value(QStringLiteral("data")).toObject();
    QStringList out = codesOf(data.value(QStringLiteral("crypto")).toObject());
    for (const QString &fiat : codesOf(data.value(QStringLiteral("fiats")).toObject())) {
        if (!out.contains(fiat))
            out << fiat;
    }
    return out;
}
} // namespace detail

// Fetches the casino's currency list. On success the list is cached and `done` runs with it
// (only while `context` is alive); on failure nothing happens and the cached list stays.
// `token`: the login token, for casinos whose list needs it (Wolfbet, Paradice, Pasino, Tower);
// `clearance` / `userAgent`: Tower's / Winna's cf_clearance cookie and its browser's User-Agent
// (for Winna `token` is the "connect.sid" session and `deviceId` the antifraud device id). Returns false when
// nothing was requested (no source, or the needed token is empty).
inline bool fetch(LiveSettings::Casino casino, QObject *context,
                  std::function<void(const QStringList &)> done,
                  const QString &token = QString(),
                  const QString &clearance = QString(),
                  const QString &userAgent = QString(),
                  const QString &deviceId = QString())
{
    if (!hasSource(casino))
        return false;

    QNetworkRequest req;
    QByteArray postBody; // non-empty: sent as a POST
    std::function<QStringList(const QByteArray &)> parse;
    if (casino == LiveSettings::Casino::Goated) {
        req.setUrl(QUrl(QStringLiteral("https://%1/main/exchange-rates").arg(GoatedTransport::apiHost())));
        req.setRawHeader("Origin", QStringLiteral("https://%1").arg(GoatedTransport::webHost()).toUtf8());
        req.setRawHeader("Referer", QStringLiteral("https://%1/").arg(GoatedTransport::webHost()).toUtf8());
        parse = detail::parseGoated;
    } else if (casino == LiveSettings::Casino::Wolfbet) {
        const QString clean = WolfbetTransport::cleanToken(token);
        if (clean.isEmpty())
            return false;
        req.setUrl(QUrl(QStringLiteral("https://%1/api/v2/user/balances").arg(WolfbetTransport::host())));
        req.setRawHeader("Authorization", QByteArray("Bearer ") + clean.toUtf8());
        req.setRawHeader("Referer", QStringLiteral("https://%1/").arg(WolfbetTransport::host()).toUtf8());
        req.setRawHeader("x-client-type", "Web-Application");
        req.setRawHeader("x-requested-with", "XMLHttpRequest");
        parse = detail::parseWolfbet;
    } else if (casino == LiveSettings::Casino::Paradice) {
        const QString clean = ParadiceTransport::cleanToken(token);
        if (clean.isEmpty())
            return false;
        req.setUrl(QUrl(ParadiceTransport::apiUrl()));
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        req.setRawHeader("x-access-token", clean.toUtf8());
        req.setRawHeader("Origin", QStringLiteral("https://%1").arg(ParadiceTransport::webHost()).toUtf8());
        req.setRawHeader("Referer", QStringLiteral("https://%1/").arg(ParadiceTransport::webHost()).toUtf8());
        QJsonObject body;
        body.insert(QStringLiteral("variables"), QJsonObject());
        body.insert(QStringLiteral("query"), ParadiceTransport::balancesQuery());
        postBody = QJsonDocument(body).toJson(QJsonDocument::Compact);
        parse = detail::parseParadice;
    } else if (casino == LiveSettings::Casino::Pasino) {
        const QString clean = PasinoTransport::cleanToken(token);
        if (clean.isEmpty())
            return false;
        req.setUrl(QUrl(PasinoTransport::apiBase() + QLatin1Char('/') + PasinoTransport::balancesPath()));
        req.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));
        req.setRawHeader("Origin", QStringLiteral("https://%1").arg(PasinoTransport::webHost()).toUtf8());
        req.setRawHeader("Referer", QStringLiteral("https://%1/").arg(PasinoTransport::webHost()).toUtf8());
        postBody = PasinoTransport::makeBody(clean);
        parse = detail::parsePasino;
    } else if (casino == LiveSettings::Casino::Tower) {
        const QString clean = TowerTransport::cleanToken(token);
        if (clean.isEmpty())
            return false;
        // Already has the site's headers, cookies and User-Agent (cf_clearance is bound to it).
        req = TowerTransport::buildRequest(QStringLiteral("currencies"), clean,
                                           TowerTransport::cleanClearance(clearance), userAgent);
        parse = TowerTransport::parseCurrencies;
    } else if (casino == LiveSettings::Casino::Winna) {
        WinnaCredentials wc;
        wc.session = WinnaTransport::cleanSession(token);
        if (wc.session.isEmpty())
            return false;
        wc.clearance = WinnaTransport::cleanClearance(clearance);
        wc.userAgent = userAgent.trimmed();
        wc.deviceId = WinnaTransport::cleanDeviceId(deviceId);
        req = WinnaTransport::buildApiRequest(QStringLiteral("v1/payments/wallets"), wc);
        parse = WinnaTransport::parseWalletCoins;
    }
    if (casino != LiveSettings::Casino::Tower && casino != LiveSettings::Casino::Winna) {
        req.setRawHeader("Accept", "application/json, text/plain, */*");
        req.setHeader(QNetworkRequest::UserAgentHeader, detail::userAgent());
    }
    req.setTransferTimeout(15000);

    QNetworkReply *reply = postBody.isEmpty() ? detail::nam()->get(req)
                                              : detail::nam()->post(req, postBody);
    QObject::connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    QObject::connect(reply, &QNetworkReply::finished, context, [reply, casino, parse, done] {
        if (reply->error() != QNetworkReply::NoError)
            return;
        const QStringList codes = parse(reply->readAll());
        if (codes.isEmpty())
            return;
        store(casino, codes);
        if (done)
            done(codes);
    });
    return true;
}
} // namespace CasinoCurrencies
