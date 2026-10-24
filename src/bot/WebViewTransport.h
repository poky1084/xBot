#pragma once
#include "StakeTransport.h"
#include <QPointer>
#include <QJsonObject>

class CookieGrabberDialog;

class WebViewTransport : public IStakeTransport
{
    Q_OBJECT
public:
    explicit WebViewTransport(QObject *parent = nullptr);
    void setBrowser(CookieGrabberDialog *browser) { m_browser = browser; }
    void setCredentials(const StakeCredentials &cred) { m_cred = cred; }

    QString name() const override { return "WebView2 Browser"; }
    bool isReady(QString *whyNot = nullptr) const override;
    void post(const QString &path, const QJsonObject &body, TransportCallback cb) override;

private:
    QPointer<CookieGrabberDialog> m_browser;
    StakeCredentials m_cred;
};