#pragma once

#include <QString>
#include <QtGlobal>

// Connection settings for live (real-money) betting.
struct LiveSettings
{
    bool enabled = false;          // never persisted: every launch starts in simulation
    bool extensionMode = false;    // false = cookie (cf_clearance), true = browser extension
    QString site = QStringLiteral("stake.com");
    QString apiKey;
    QString clearance;             // cf_clearance cookie value
    QString userAgent;             // User-Agent of the browser that got the cookie
    QString currency;              // lowercase API currency code ("btc", "usdt", ...)
    bool remember = false;         // persist apiKey/clearance/userAgent on this machine
    quint16 extensionPort = 4444;  // local WebSocket port the extension connects to
};
