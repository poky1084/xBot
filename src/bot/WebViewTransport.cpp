#include "bot/WebViewTransport.h"
#include "ui/CookieGrabberDialog.h"
#include <QJsonDocument>

WebViewTransport::WebViewTransport(QObject *parent) : IStakeTransport(parent) {}

bool WebViewTransport::isReady(QString *whyNot) const
{
    if (!m_browser) {
        if (whyNot) *whyNot = "Browser is not open.";
        return false;
    }
    if (!m_browser->isPageLoaded()) {
        if (whyNot) *whyNot = "Browser page is still loading.";
        return false;
    }
    return true;
}

void WebViewTransport::post(const QString &path, const QJsonObject &body, TransportCallback cb)
{
    if (!m_browser) {
        TransportReply r;
        r.ok = false;
        r.error = "Browser not initialized";
        cb(r);
        return;
    }

    QString fullUrl = "https://" + m_cred.site + "/" + path;
    QString jsonBody = QJsonDocument(body).toJson(QJsonDocument::Compact);

    m_browser->executeFetch(fullUrl, "POST", jsonBody, m_cred.apiKey, 
        [cb](bool success, int status, QJsonObject json, QString error) {
            TransportReply reply;
            reply.ok = success;
            reply.status = status;
            reply.json = json;
            reply.error = error;
            cb(reply);
        });
}