#pragma once

#include <QString>
#include <QStringList>
#include <QtGlobal>

// ---- Casino selection in code ---------------------------------------------------
// Default casino ("stake", "shuffle", "goated", "wolfbet", "duel" or "paradice") used on the first launch only. After that the
// casino last selected (Casino menu / script) is saved on exit and restored at startup.
namespace AppConfig {
inline const QString casino = QStringLiteral("stake");
}

// Connection settings for live (real-money) betting.
struct LiveSettings
{
    enum class Casino { Stake, Shuffle, Goated, Wolfbet, Duel, Paradice };
    static constexpr int kCasinoCount = 6; // size of per-casino arrays indexed by int(Casino)

    // "shuffle" / "goated" / "wolfbet" / "duel" / "paradice" (any case) -> that casino, anything else -> Stake.
    static Casino fromName(const QString &name)
    {
        if (name.compare(QLatin1String("shuffle"), Qt::CaseInsensitive) == 0)
            return Casino::Shuffle;
        if (name.compare(QLatin1String("goated"), Qt::CaseInsensitive) == 0)
            return Casino::Goated;
        if (name.compare(QLatin1String("wolfbet"), Qt::CaseInsensitive) == 0)
            return Casino::Wolfbet;
        if (name.compare(QLatin1String("duel"), Qt::CaseInsensitive) == 0)
            return Casino::Duel;
        if (name.compare(QLatin1String("paradice"), Qt::CaseInsensitive) == 0)
            return Casino::Paradice;
        return Casino::Stake;
    }

    // Lowercase name used in settings, scripts and bet records.
    static QString nameOf(Casino c)
    {
        switch (c) {
        case Casino::Shuffle: return QStringLiteral("shuffle");
        case Casino::Goated:  return QStringLiteral("goated");
        case Casino::Wolfbet: return QStringLiteral("wolfbet");
        case Casino::Duel:    return QStringLiteral("duel");
        case Casino::Paradice: return QStringLiteral("paradice");
        default:              return QStringLiteral("stake");
        }
    }

    bool enabled = false;          // never persisted: every launch starts in simulation
    Casino casino = fromName(AppConfig::casino); // set by AppConfig::casino, switchable in the "Casino" menu

    // ---- Stake
    bool extensionMode = false;    // false = cookie (cf_clearance), true = browser extension
    QString site = QStringLiteral("stake.com");
    QString apiKey;
    QString clearance;             // cf_clearance cookie value
    QString userAgent;             // User-Agent of the browser that got the cookie

    // ---- Shuffle (Direct mode only: plain HTTPS + WebSocket, no WebView2 / extension)
    QString shuffleSite = QStringLiteral("shuffle.com");
    QString shuffleToken;          // "Token" in Options (sent as "Authorization: Bearer <token>")
    QString correlationUserId;     // CORRELATION_USER_ID
    QString browserId;             // BROWSER_ID

    // ---- Goated (Direct mode only: plain HTTPS). Only a token is needed.
    QString goatedToken;           // localStorage "g3_token" (sent as "Authorization: Bearer <token>")

    // ---- Wolfbet (Direct mode only: plain HTTPS). Only a token is needed.
    QString wolfbetToken;          // "value" inside the "token" cookie (sent as "Authorization: Bearer <token>")

    // ---- Paradice (Direct mode: plain HTTPS GraphQL). Only a token is needed.
    QString paradiceToken;         // localStorage "token" (sent as "x-access-token: <token>")

    // ---- Duel (Direct mode only: plain HTTPS). Bets also need a security token, which the
    // transport fetches by itself (and again every ~10 minutes).
    QString duelSite = QStringLiteral("duel.com"); // "Site / mirror" in Options
    QString duelToken;             // "duel" cookie ("Token" in Options)
    QString duelUuid;              // localStorage "security:uuid"
    QString duelClearance;         // "cf_clearance" cookie
    QString duelUserAgent;         // User-Agent of the login browser (cf_clearance is bound to it)

    // ---- Browser extension: the casino's requests go out from its own tab in the browser (the
    // "Browser extension" login method in Options). Stake's flag is extensionMode above.
    bool shuffleExtension = false; // HTTPS only; Shuffle's balance WebSocket stays in the app
    bool goatedExtension = false;
    bool wolfbetExtension = false;
    bool duelExtension = false;    // replaces the hidden WebView2 browser
    bool paradiceExtension = false;

    QString currency;              // lowercase API currency code ("btc", "usdt", ...) of the active casino
    quint16 extensionPort = 4444;  // local WebSocket port the extension connects to

    bool isShuffle() const { return casino == Casino::Shuffle; }
    bool isGoated() const { return casino == Casino::Goated; }
    bool isWolfbet() const { return casino == Casino::Wolfbet; }
    bool isDuel() const { return casino == Casino::Duel; }
    bool isParadice() const { return casino == Casino::Paradice; }
    // Casinos reached with plain HTTPS from the app ("Direct mode"), i.e. everything but Stake.
    bool isDirect() const { return casino != Casino::Stake; }
    QString casinoName() const { return nameOf(casino); }

    bool usesExtension(Casino c) const
    {
        switch (c) {
        case Casino::Shuffle: return shuffleExtension;
        case Casino::Goated:  return goatedExtension;
        case Casino::Wolfbet: return wolfbetExtension;
        case Casino::Duel:    return duelExtension;
        case Casino::Paradice: return paradiceExtension;
        default:              return extensionMode;
        }
    }
    void setUsesExtension(Casino c, bool on)
    {
        switch (c) {
        case Casino::Shuffle: shuffleExtension = on; break;
        case Casino::Goated:  goatedExtension = on; break;
        case Casino::Wolfbet: wolfbetExtension = on; break;
        case Casino::Duel:    duelExtension = on; break;
        case Casino::Paradice: paradiceExtension = on; break;
        default:              extensionMode = on; break;
        }
    }
    bool activeUsesExtension() const { return usesExtension(casino); }

    // The login data of casino `c` (token, ids, API key) taken from `from`.
    void copyCredentials(Casino c, const LiveSettings &from)
    {
        switch (c) {
        case Casino::Shuffle:
            shuffleToken = from.shuffleToken;
            correlationUserId = from.correlationUserId;
            browserId = from.browserId;
            break;
        case Casino::Goated:  goatedToken = from.goatedToken; break;
        case Casino::Wolfbet: wolfbetToken = from.wolfbetToken; break;
        case Casino::Paradice: paradiceToken = from.paradiceToken; break;
        case Casino::Duel:
            duelToken = from.duelToken;
            duelUuid = from.duelUuid;
            duelClearance = from.duelClearance;
            break;
        default:
            apiKey = from.apiKey;
            clearance = from.clearance;
            break;
        }
    }
    bool sameCredentials(Casino c, const LiveSettings &o) const
    {
        switch (c) {
        case Casino::Shuffle:
            return shuffleToken == o.shuffleToken && correlationUserId == o.correlationUserId
                   && browserId == o.browserId;
        case Casino::Goated:  return goatedToken == o.goatedToken;
        case Casino::Wolfbet: return wolfbetToken == o.wolfbetToken;
        case Casino::Paradice: return paradiceToken == o.paradiceToken;
        case Casino::Duel:
            return duelToken == o.duelToken && duelUuid == o.duelUuid
                   && duelClearance == o.duelClearance;
        default:              return apiKey == o.apiKey && clearance == o.clearance;
        }
    }
    bool anyUsesExtension() const
    {
        return extensionMode || shuffleExtension || goatedExtension || wolfbetExtension || duelExtension
               || paradiceExtension;
    }
    // Sites of the browser tab the extension uses for casino `c` (compared without "www.").
    QStringList extensionHosts(Casino c) const
    {
        switch (c) {
        case Casino::Shuffle: return {hostOf(shuffleSite, QStringLiteral("shuffle.com"))};
        case Casino::Goated:  return {QStringLiteral("goated.com")};
        case Casino::Wolfbet: return {QStringLiteral("wolfbet.com")};
        case Casino::Duel:    return {duelSite};
        case Casino::Paradice: return {QStringLiteral("paradice.in")};
        default:              return {hostOf(site, QStringLiteral("stake.com"))};
        }
    }
    // Every casino site a tab may connect from (the mirrors typed in Options).
    QStringList allExtensionHosts() const
    {
        QStringList out;
        for (int i = 0; i < kCasinoCount; ++i)
            out += extensionHosts(static_cast<Casino>(i));
        return out;
    }

    // "https://Duel.com/dice/" -> "duel.com": the host of a typed site / mirror; empty -> `fallback`.
    static QString hostOf(const QString &typed, const QString &fallback)
    {
        QString h = typed.trimmed();
        const int scheme = h.indexOf(QLatin1String("://"));
        if (scheme >= 0)
            h = h.mid(scheme + 3);
        const int slash = h.indexOf(QLatin1Char('/'));
        if (slash >= 0)
            h = h.left(slash);
        h = h.trimmed().toLower();
        return h.isEmpty() ? fallback : h;
    }

    // Host of the active casino.
    QString activeSite() const
    {
        if (isGoated())
            return QStringLiteral("goated.com");
        if (isWolfbet())
            return QStringLiteral("wolfbet.com");
        if (isDuel())
            return duelSite;
        if (isParadice())
            return QStringLiteral("paradice.in");
        return isShuffle() ? shuffleSite : site;
    }
};
