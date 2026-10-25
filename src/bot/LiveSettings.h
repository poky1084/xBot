#pragma once

#include <QString>
#include <QtGlobal>

// Connection settings for live (real-money) betting.
struct LiveSettings
{
    enum class Casino { Stake, Shuffle };

    bool enabled = false;          // never persisted: every launch starts in simulation
    Casino casino = Casino::Stake; // selected in the "Casino" menu

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
