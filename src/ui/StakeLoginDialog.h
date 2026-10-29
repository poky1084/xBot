#pragma once

#include <QDialog>
#include <QPointer>
#include <QString>

#include <cstdint>

struct ICoreWebView2Environment;
struct ICoreWebView2Controller;
struct ICoreWebView2;

class QLabel;
class QPushButton;
class QTimer;

// Opens stake.com (or the mirror set in Options) in an embedded Edge (WebView2) window. The
// user logs in there; once the session works (a GraphQL `user` query answers with the account),
// everything the app needs is read and the dialog closes by itself:
//   apiKey     the x-access-token: taken from the header the site sends to /_api/graphql,
//              or else from the "session" cookie (httpOnly: read through the cookie manager)
//   clearance  the "cf_clearance" cookie (may be empty when Cloudflare did not challenge)
//   userAgent  the browser's User-Agent (cf_clearance only works with the same one)
//   userName   the account name (only shown in the status line)
// "Use captured data" takes what was found so far without waiting for the login check.
class StakeLoginDialog : public QDialog
{
    Q_OBJECT

public:
    // `site` = Stake's host or a mirror (empty = stake.com).
    explicit StakeLoginDialog(const QString &site = QString(), QWidget *parent = nullptr);
    ~StakeLoginDialog() override;

    QString apiKey() const { return m_apiKey; }
    QString clearance() const { return m_clearance; }
    QString userAgent() const { return m_userAgent; }
    QString userName() const { return m_userName; }

    static bool isAvailable();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void initializeWebView();
    void poll();
    void readCookies();
    void checkLogin();
    void updateBounds();
    void updateStatus();
    void finish();
    // Logged in: closes now when cf_clearance is known, else after a short wait for it.
    void loggedIn();
    bool isSiteCookie(const QString &cookieDomain) const;

    QLabel *m_status = nullptr;
    QWidget *m_webContainer = nullptr;
    QTimer *m_pollTimer = nullptr;
    QPushButton *m_useBtn = nullptr;
    bool m_polling = false;
    int m_clearanceWait = -1; // polls left to wait for cf_clearance after the login (-1 = not logged in yet)

    ICoreWebView2Environment *m_env = nullptr;
    ICoreWebView2Controller *m_controller = nullptr;
    ICoreWebView2 *m_webview = nullptr;
    int64_t m_requestToken = 0; // EventRegistrationToken.value of WebResourceRequested
    bool m_requestHooked = false;

    QString m_site;
    QString m_pageHost;  // host of the page now shown (the site may redirect to a mirror)
    QString m_headerKey; // x-access-token seen on a request of the page
    QString m_cookieKey; // "session" cookie
    QString m_apiKey;    // what is handed out (header first, else cookie)
    QString m_clearance;
    QString m_userAgent;
    QString m_userName;
};
