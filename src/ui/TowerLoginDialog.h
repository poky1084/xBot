#pragma once

#include <QDialog>
#include <QPointer>
#include <QString>

struct ICoreWebView2Environment;
struct ICoreWebView2Controller;
struct ICoreWebView2;

class QLabel;
class QPushButton;
class QTimer;

// Opens tower.bet in an embedded Edge (WebView2) window. The user logs in there; once the
// session works (GET /api/accounts answers with the token), everything the app needs is read
// and the dialog closes by itself:
//   token      the "__tt" cookie (sent as "Authorization: Bearer <token>")
//   clearance  the "cf_clearance" cookie (httpOnly: read through the WebView2 cookie manager;
//              may be empty when Cloudflare did not challenge)
//   userAgent  the browser's User-Agent (cf_clearance only works with the same one)
// "Use captured data" takes what was found so far without waiting for the login check.
class TowerLoginDialog : public QDialog
{
    Q_OBJECT

public:
    // `site` = Tower's host or a mirror (empty = tower.bet).
    explicit TowerLoginDialog(const QString &site = QString(), QWidget *parent = nullptr);
    ~TowerLoginDialog() override;

    QString token() const { return m_token; }
    QString clearance() const { return m_clearance; }
    QString userAgent() const { return m_userAgent; }

    static bool isAvailable();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void initializeWebView();
    void poll();
    void readCookies(bool loggedIn);
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
};
