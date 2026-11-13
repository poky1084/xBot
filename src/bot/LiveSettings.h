#pragma once

#include <QString>
#include <QStringList>
#include <QtGlobal>

// ---- Casino selection in code ---------------------------------------------------
// Default casino ("stake", "shuffle", "goated", "wolfbet", "duel", "paradice", "pasino", "tower" or "winna") used on the first launch only. After that the
// casino last selected (Casino menu / script) is saved on exit and restored at startup.
namespace AppConfig {
inline const QString casino = QStringLiteral("stake");
}

// Connection settings for live (real-money) betting.
struct LiveSettings
{
    enum class Casino { Stake, Shuffle, Goated, Wolfbet, Duel, Paradice, Pasino, Tower, Winna };
    static constexpr int kCasinoCount = 9; // size of per-casino arrays indexed by int(Casino)

    // "shuffle" / "goated" / "wolfbet" / "duel" / "paradice" / "pasino" / "tower" / "winna" (any case) -> that casino, anything else -> Stake.
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
        if (name.compare(QLatin1String("pasino"), Qt::CaseInsensitive) == 0)
            return Casino::Pasino;
        if (name.compare(QLatin1String("tower"), Qt::CaseInsensitive) == 0
            || name.compare(QLatin1String("tower.bet"), Qt::CaseInsensitive) == 0)
            return Casino::Tower;
        if (name.compare(QLatin1String("winna"), Qt::CaseInsensitive) == 0
            || name.compare(QLatin1String("winna.com"), Qt::CaseInsensitive) == 0)
            return Casino::Winna;
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
        case Casino::Pasino:  return QStringLiteral("pasino");
        case Casino::Tower:   return QStringLiteral("tower");
        case Casino::Winna:   return QStringLiteral("winna");
        default:              return QStringLiteral("stake");
        }
    }

    // The casino's own site, used when "Site / mirror" is empty.
    static QString defaultSite(Casino c)
    {
        switch (c) {
        case Casino::Shuffle: return QStringLiteral("shuffle.com");
        case Casino::Goated:  return QStringLiteral("www.goated.com");
        case Casino::Wolfbet: return QStringLiteral("wolfbet.com");
        case Casino::Duel:    return QStringLiteral("duel.com");
        case Casino::Paradice: return QStringLiteral("paradice.in");
        case Casino::Pasino:  return QStringLiteral("pasino.com");
        case Casino::Tower:   return QStringLiteral("tower.bet");
        case Casino::Winna:   return QStringLiteral("winna.com");
        default:              return QStringLiteral("stake.com");
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

    // ---- "Site / mirror" in Options of the casinos below (host, e.g. "goated.com" or a mirror).
    // Their API hosts are derived from it ("apis.goated.com" -> "apis.<mirror>"), see siteBase().
    QString goatedSite = defaultSite(Casino::Goated);
    QString wolfbetSite = defaultSite(Casino::Wolfbet);
    QString paradiceSite = defaultSite(Casino::Paradice);
    QString pasinoSite = defaultSite(Casino::Pasino);
    QString towerSite = defaultSite(Casino::Tower);
    QString winnaSite = defaultSite(Casino::Winna);

    // ---- Goated (Direct mode only: plain HTTPS). Only a token is needed.
    QString goatedToken;           // localStorage "g3_token" (sent as "Authorization: Bearer <token>")

    // ---- Wolfbet (Direct mode only: plain HTTPS). Only a token is needed.
    QString wolfbetToken;          // "value" inside the "token" cookie (sent as "Authorization: Bearer <token>")

    // ---- Paradice (Direct mode: plain HTTPS GraphQL). Only a token is needed.
    QString paradiceToken;         // localStorage "token" (sent as "x-access-token: <token>")

    // ---- Pasino (Direct mode: plain HTTPS). Only a token is needed.
    QString pasinoToken;           // localStorage "token" (sent as "token" in every request body)

    // ---- Tower.bet (Direct mode: plain HTTPS behind Cloudflare).
    QString towerToken;            // "__tt" cookie (sent as "Authorization: Bearer <token>")
    QString towerClearance;        // "cf_clearance" cookie (Direct mode only)
    QString towerUserAgent;        // User-Agent of the browser that got cf_clearance

    // ---- Winna.com (Direct mode: plain HTTPS behind Cloudflare).
    QString winnaSession;          // "connect.sid" cookie (account session)
    QString winnaClearance;        // "cf_clearance" cookie (Direct mode only)
    QString winnaUserAgent;        // User-Agent of the browser that got cf_clearance
    QString winnaDeviceId;         // localStorage "wn-antifraud-device-id" -> "id"

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
    bool pasinoExtension = false;
    bool towerExtension = false;
    bool winnaExtension = false;

    QString currency;              // lowercase API currency code ("btc", "usdt", ...) of the active casino
    quint16 extensionPort = 4444;  // local WebSocket port the extension connects to

    bool isShuffle() const { return casino == Casino::Shuffle; }
    bool isGoated() const { return casino == Casino::Goated; }
    bool isWolfbet() const { return casino == Casino::Wolfbet; }
    bool isDuel() const { return casino == Casino::Duel; }
    bool isParadice() const { return casino == Casino::Paradice; }
    bool isPasino() const { return casino == Casino::Pasino; }
    bool isTower() const { return casino == Casino::Tower; }
    bool isWinna() const { return casino == Casino::Winna; }
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
        case Casino::Pasino:  return pasinoExtension;
        case Casino::Tower:   return towerExtension;
        case Casino::Winna:   return winnaExtension;
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
        case Casino::Pasino:  pasinoExtension = on; break;
        case Casino::Tower:   towerExtension = on; break;
        case Casino::Winna:   winnaExtension = on; break;
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
        case Casino::Pasino:  pasinoToken = from.pasinoToken; break;
        // Tower / Winna: session, cf_clearance, User-Agent and device id belong together (the
        // session is bound to the device id, cf_clearance to the User-Agent): replace them as one.
        case Casino::Tower:
            towerToken = from.towerToken;
            towerClearance = from.towerClearance;
            towerUserAgent = from.towerUserAgent;
            break;
        case Casino::Winna:
            winnaSession = from.winnaSession;
            winnaClearance = from.winnaClearance;
            winnaUserAgent = from.winnaUserAgent;
            winnaDeviceId = from.winnaDeviceId;
            break;
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
        case Casino::Pasino:  return pasinoToken == o.pasinoToken;
        case Casino::Tower:
            return towerToken == o.towerToken && towerClearance == o.towerClearance
                   && towerUserAgent == o.towerUserAgent;
        case Casino::Winna:
            return winnaSession == o.winnaSession && winnaClearance == o.winnaClearance
                   && winnaUserAgent == o.winnaUserAgent && winnaDeviceId == o.winnaDeviceId;
        case Casino::Duel:
            return duelToken == o.duelToken && duelUuid == o.duelUuid
                   && duelClearance == o.duelClearance;
        default:              return apiKey == o.apiKey && clearance == o.clearance;
        }
    }
    bool anyUsesExtension() const
    {
        return extensionMode || shuffleExtension || goatedExtension || wolfbetExtension || duelExtension
               || paradiceExtension || pasinoExtension || towerExtension
               || winnaExtension;
    }
    // Sites of the browser tab the extension uses for casino `c` (compared without "www.").
    QStringList extensionHosts(Casino c) const { return {siteHost(c)}; }
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

    // "Site / mirror" typed for casino `c`, as stored (may be empty / a URL).
    QString siteOf(Casino c) const
    {
        switch (c) {
        case Casino::Shuffle: return shuffleSite;
        case Casino::Goated:  return goatedSite;
        case Casino::Wolfbet: return wolfbetSite;
        case Casino::Duel:    return duelSite;
        case Casino::Paradice: return paradiceSite;
        case Casino::Pasino:  return pasinoSite;
        case Casino::Tower:   return towerSite;
        case Casino::Winna:   return winnaSite;
        default:              return site;
        }
    }
    void setSiteOf(Casino c, const QString &typed)
    {
        switch (c) {
        case Casino::Shuffle: shuffleSite = typed; break;
        case Casino::Goated:  goatedSite = typed; break;
        case Casino::Wolfbet: wolfbetSite = typed; break;
        case Casino::Duel:    duelSite = typed; break;
        case Casino::Paradice: paradiceSite = typed; break;
        case Casino::Pasino:  pasinoSite = typed; break;
        case Casino::Tower:   towerSite = typed; break;
        case Casino::Winna:   winnaSite = typed; break;
        default:              site = typed; break;
        }
    }
    // Host of casino `c` ("https://Goated.io/" -> "goated.io"; empty -> its own site).
    QString siteHost(Casino c) const { return hostOf(siteOf(c), defaultSite(c)); }

    // Host of the active casino.
    QString activeSite() const { return siteHost(casino); }

    // ---- Sites in use by the running app. The transports and the currency lists build their
    // URLs from these (static helpers, no settings object at hand). MainWindow calls
    // publishSites() whenever its live settings change.
    void publishSites() const
    {
        for (int i = 0; i < kCasinoCount; ++i)
            currentSiteRef(static_cast<Casino>(i)) = siteHost(static_cast<Casino>(i));
    }
    // Host in use for casino `c` (e.g. "www.goated.com" if typed so).
    static QString currentSite(Casino c)
    {
        const QString h = currentSiteRef(c);
        return h.isEmpty() ? defaultSite(c) : h;
    }
    // The same without "www.": the domain the API subdomains hang off ("goated.com" ->
    // "apis.goated.com"; on a mirror "goated.io" -> "apis.goated.io").
    static QString siteBase(Casino c)
    {
        QString h = currentSite(c);
        if (h.startsWith(QLatin1String("www.")))
            h = h.mid(4);
        return h;
    }

private:
    static QString &currentSiteRef(Casino c)
    {
        static QString sites[kCasinoCount];
        return sites[int(c)];
    }
};
