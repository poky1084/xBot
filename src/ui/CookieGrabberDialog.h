#pragma once

#include <QDialog>
#include <QString>
#include <QTimer>
#include <QJsonObject>
#include <functional>

struct ICoreWebView2Environment;
struct ICoreWebView2Controller;
struct ICoreWebView2;

class QLabel;

class CookieGrabberDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CookieGrabberDialog(const QString &site, const QString &userAgent, QWidget *parent = nullptr);
    ~CookieGrabberDialog() override;

    QString clearance() const { return m_clearance; }
    QString userAgent() const { return m_userAgent; }

    static bool isAvailable();

    // True once the hidden browser finished loading the site.
    bool isPageLoaded() const { return m_pageLoaded; }

    // Executes a fetch request inside the WebView2 browser context
    // `status` is the HTTP status (0 = network/page failure). API errors that come back as
    // JSON with a 4xx status are delivered as ok == true so the caller can read errorType.
    void executeFetch(const QString &url, const QString &method, const QString &body,
                      const QString &apiKey,
                      std::function<void(bool ok, int status, QJsonObject json, QString error)> callback);

signals:
    void pageLoaded();

private:
    bool m_pageLoaded = false;
    void initializeWebView(const QString &site);
    void fetchCookies(const QString &site);

    QLabel *m_status = nullptr;
    QWidget *m_webContainer = nullptr;
    QTimer *m_pollTimer = nullptr;
    
    ICoreWebView2Environment *m_env = nullptr;
    ICoreWebView2Controller *m_controller = nullptr;
    ICoreWebView2 *m_webview = nullptr;

    QString m_site;
    QString m_requestedUserAgent;
    QString m_clearance;
    QString m_userAgent;
};