#pragma once

#include <QDialog>
#include <QPointer>
#include <QString>

#include <functional>

struct ICoreWebView2Environment;
struct ICoreWebView2Controller;
struct ICoreWebView2;

class QLabel;
class QPushButton;
class QTimer;

// Opens winna.com in an embedded Edge (WebView2) window. The user logs in there; once the
// session works (GET api2.winna.com/v1/payments/wallets answers), everything the app needs is
// read and the dialog closes by itself:
//   session    "connect.sid=…; chat.jwt=…; …": the session cookie and every other winna.com
//              cookie (httpOnly ones too: read through the WebView2 cookie manager)
//   clearance  the "cf_clearance" cookie (httpOnly; may be empty when Cloudflare did not challenge)
//   deviceId   localStorage "wn-antifraud-device-id" -> "id" (x-antifraud-device-id header)
//   userAgent  the browser's User-Agent (cf_clearance only works with the same one)
// "Use captured data" takes what was found so far without waiting for the login check.
class WinnaLoginDialog : public QDialog
{
    Q_OBJECT

public:
    // `freshStart`: delete winna.com's cookies and site storage in this browser profile first,
    // so the login starts like after "Clear data" (no stale session cookies left over).
    explicit WinnaLoginDialog(bool freshStart = false, QWidget *parent = nullptr);
    ~WinnaLoginDialog() override;

    QString session() const { return m_token; }
    QString deviceId() const { return m_deviceId; }
    QString clearance() const { return m_clearance; }
    QString userAgent() const { return m_userAgent; }

    static bool isAvailable();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void initializeWebView();
    void poll();
    void readCookies(bool loggedIn);
    void clearSiteData(std::function<void()> then);
    void openSite();
    void updateBounds();
    void finish();

    QLabel *m_status = nullptr;
    QWidget *m_webContainer = nullptr;
    QTimer *m_pollTimer = nullptr;
    QPushButton *m_useBtn = nullptr;
    bool m_polling = false;

    ICoreWebView2Environment *m_env = nullptr;
    ICoreWebView2Controller *m_controller = nullptr;
    ICoreWebView2 *m_webview = nullptr;

    QString m_site;
    QString m_token;
    QString m_clearance;
    QString m_userAgent;
    QString m_deviceId;
    bool m_freshStart = false;
};
