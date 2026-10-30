#pragma once

#include <QNetworkCookie>
#include <QNetworkCookieJar>

// Cookie jar that never adds cookies to a request and never stores the ones a reply sets.
// For transports that send the casino's cookies themselves (Cookie header from Options): Qt's
// default jar would keep cookies from earlier replies (e.g. an old session) for as long as the
// app runs and put them into later requests next to / instead of the ones from Options.
class NoCookieJar final : public QNetworkCookieJar
{
public:
    using QNetworkCookieJar::QNetworkCookieJar;

    QList<QNetworkCookie> cookiesForUrl(const QUrl &) const override { return {}; }
    bool setCookiesFromUrl(const QList<QNetworkCookie> &, const QUrl &) override { return false; }
};

// Same per request (for a shared QNetworkAccessManager): no cookies from / to its jar.
#include <QNetworkRequest>
inline void manualCookies(QNetworkRequest &req)
{
    req.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    req.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
}
