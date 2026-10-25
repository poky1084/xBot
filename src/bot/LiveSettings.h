#pragma once

#include <QString>
#include <QtGlobal>

// ---- Casino selection in code ---------------------------------------------------
// Change this one value ("stake" or "shuffle") to pick the casino at startup.
// It takes priority over the value saved in the settings; the Casino menu can still
// switch for the current session.
namespace AppConfig {
inline const QString casino = QStringLiteral("stake");
}

// Connection settings for live (real-money) betting.
struct LiveSettings
{
    enum class Casino { Stake, Shuffle };

    // "shuffle" (any case) -> Shuffle, anything else -> Stake.
    static Casino fromName(const QString &name)
    {
        return name.compare(QLatin1String("shuffle"), Qt::CaseInsensitive) == 0 ? Casino::Shuffle
                                                                               : Casino::Stake;
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

    QString currency;              // lowercase API currency code ("btc", "usdt", ...) of the active casino
    bool remember = false;         // persist apiKey/clearance/userAgent/token/ids on this machine
    quint16 extensionPort = 4444;  // local WebSocket port the extension connects to

    bool isShuffle() const { return casino == Casino::Shuffle; }
    // Host of the active casino.
    QString activeSite() const { return isShuffle() ? shuffleSite : site; }
};
