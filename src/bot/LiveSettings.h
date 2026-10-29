#pragma once

#include <QString>
#include <QtGlobal>

// ---- Casino selection in code ---------------------------------------------------
// Default casino ("stake", "shuffle", "goated", "wolfbet" or "duel") used on the first launch only. After that the
// casino last selected (Casino menu / script) is saved on exit and restored at startup.
namespace AppConfig {
inline const QString casino = QStringLiteral("stake");
}

// Connection settings for live (real-money) betting.
struct LiveSettings
{
    enum class Casino { Stake, Shuffle, Goated, Wolfbet, Duel };
    static constexpr int kCasinoCount = 5; // size of per-casino arrays indexed by int(Casino)

    // "shuffle" / "goated" / "wolfbet" / "duel" (any case) -> that casino, anything else -> Stake.
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

    // ---- Duel (Direct mode only: plain HTTPS). Bets also need a security token, which the
    // transport fetches by itself (and again every ~10 minutes).
    QString duelSite = QStringLiteral("duel.com"); // "Site / mirror" in Options
    QString duelToken;             // "duel" cookie ("Token" in Options)
    QString duelUuid;              // localStorage "security:uuid"
    QString duelClearance;         // "cf_clearance" cookie
    QString duelUserAgent;         // User-Agent of the login browser (cf_clearance is bound to it)

    QString currency;              // lowercase API currency code ("btc", "usdt", ...) of the active casino
    quint16 extensionPort = 4444;  // local WebSocket port the extension connects to

    bool isShuffle() const { return casino == Casino::Shuffle; }
    bool isGoated() const { return casino == Casino::Goated; }
    bool isWolfbet() const { return casino == Casino::Wolfbet; }
    bool isDuel() const { return casino == Casino::Duel; }
    // Casinos reached with plain HTTPS from the app ("Direct mode"), i.e. everything but Stake.
    bool isDirect() const { return casino != Casino::Stake; }
    QString casinoName() const { return nameOf(casino); }

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
        return isShuffle() ? shuffleSite : site;
    }
};
