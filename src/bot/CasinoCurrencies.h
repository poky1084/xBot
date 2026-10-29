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
    return casino == LiveSettings::Casino::Goated || casino == LiveSettings::Casino::Wolfbet;
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
// `token`: the login token, for casinos whose list needs it (Wolfbet). Returns false when
// nothing was requested (no source, or the needed token is empty).
inline bool fetch(LiveSettings::Casino casino, QObject *context,
                  std::function<void(const QStringList &)> done,
                  const QString &token = QString())
{
    if (!hasSource(casino))
        return false;

    QNetworkRequest req;
    std::function<QStringList(const QByteArray &)> parse;
    if (casino == LiveSettings::Casino::Goated) {
        req.setUrl(QUrl(QStringLiteral("https://apis.goated.com/main/exchange-rates")));
        req.setRawHeader("Origin", "https://www.goated.com");
        req.setRawHeader("Referer", "https://www.goated.com/");
        parse = detail::parseGoated;
    } else if (casino == LiveSettings::Casino::Wolfbet) {
        const QString clean = WolfbetTransport::cleanToken(token);
        if (clean.isEmpty())
            return false;
        req.setUrl(QUrl(QStringLiteral("https://wolfbet.com/api/v2/user/balances")));
        req.setRawHeader("Authorization", QByteArray("Bearer ") + clean.toUtf8());
        req.setRawHeader("Referer", "https://wolfbet.com/");
        req.setRawHeader("x-client-type", "Web-Application");
        req.setRawHeader("x-requested-with", "XMLHttpRequest");
        parse = detail::parseWolfbet;
    }
    req.setRawHeader("Accept", "application/json, text/plain, */*");
    req.setHeader(QNetworkRequest::UserAgentHeader, detail::userAgent());
    req.setTransferTimeout(15000);

    QNetworkReply *reply = detail::nam()->get(req);
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
