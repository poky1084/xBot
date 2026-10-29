#pragma once

#include <QByteArray>
#include <QString>
#include <QWidget>

#include <functional>

struct ICoreWebView2Environment;
struct ICoreWebView2Controller;
struct ICoreWebView2;

// Hidden Edge (WebView2) window on https://duel.com used to send Duel's API requests.
//
// Duel's Cloudflare drops connections that do not come from a real browser (requests from Qt
// time out / are closed even with a valid cf_clearance), so every request runs as a fetch()
// inside the page: the browser's own TLS, cookies ("duel", cf_clearance) and User-Agent are
// used. It shares the WebView2 session folder with "Open Duel.com & Log in", so after logging
// in there this browser is logged in too.
//
// Create it with no parent (like the Stake hidden browser), set WebViewSession::kProperty,
// set Qt::WA_DontShowOnScreen and show() it.
class DuelBrowser : public QWidget
{
    Q_OBJECT

public:
    explicit DuelBrowser(QWidget *parent = nullptr);
    ~DuelBrowser() override;

    static bool isAvailable();
    bool isPageLoaded() const { return m_pageLoaded; }

    // status = HTTP status (0 = network / page problem: `error` explains), body = response text.
    using FetchCallback = std::function<void(int status, const QByteArray &body, const QString &error)>;

    // fetch(<https://duel.com/path>) in the page. `body` empty = no body (GET).
    // `uuid` goes into "x-duel-device-identifier" (empty = the page's localStorage "security:uuid").
    // `balancesOnly`: for the metadata request, only {"user":{"balances":[...]}} is passed back
    // (the full answer is ~70 KB).
    void fetch(const QString &method, const QString &path, const QByteArray &body,
               const QString &uuid, FetchCallback cb, bool balancesOnly = false);

    // Puts the "duel" session cookie into the browser (a token pasted in Options without using
    // the login button). Empty = leave the browser's cookie as it is.
    void setSessionCookie(const QString &token);

signals:
    void pageLoaded();

protected:
    void resizeEvent(class QResizeEvent *event) override;

private:
    void initializeWebView();
    void applySessionCookie();

    ICoreWebView2Environment *m_env = nullptr;
    ICoreWebView2Controller *m_controller = nullptr;
    ICoreWebView2 *m_webview = nullptr;
    bool m_pageLoaded = false;
    QString m_sessionCookie;
};
