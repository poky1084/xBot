#include "ui/CookieGrabberDialog.h" 

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QStringList>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <windows.h>
#include <wrl.h>
#include <WebView2.h>

using namespace Microsoft::WRL;

static std::wstring toWString(const QString &str) {
    return str.toStdWString();
}

CookieGrabberDialog::CookieGrabberDialog(const QString &site, const QString &userAgent, QWidget *parent)
    : QDialog(parent), m_site(site), m_requestedUserAgent(userAgent)
{
    setWindowTitle(tr("Open site & capture cf_clearance (WebView2)"));
    resize(1000, 720);

    auto *root = new QVBoxLayout(this);

    auto *hint = new QLabel(tr(
        "A native Edge browser opens below. The dialog will automatically "
        "close once cf_clearance is captured."));
    hint->setWordWrap(true);
    root->addWidget(hint);

    m_webContainer = new QWidget(this);
    m_webContainer->setStyleSheet("background-color: white;");
    root->addWidget(m_webContainer, 1);

    m_status = new QLabel(tr("Initializing WebView2…"));
    m_status->setWordWrap(true);
    root->addWidget(m_status);

    auto *btnRow = new QHBoxLayout;
    auto *cancelBtn = new QPushButton(tr("Cancel"));
    btnRow->addStretch();
    btnRow->addWidget(cancelBtn);
    root->addLayout(btnRow);

    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(1500);
    connect(m_pollTimer, &QTimer::timeout, this, [this]() {
        fetchCookies(m_site);
    });

    QTimer::singleShot(50, this, [this] { initializeWebView(m_site); });
}

CookieGrabberDialog::~CookieGrabberDialog()
{
    if (m_pollTimer) m_pollTimer->stop();
    if (m_controller) m_controller->Release();
    if (m_env) m_env->Release();
    if (m_webview) m_webview->Release();
}

bool CookieGrabberDialog::isAvailable()
{
    LPWSTR versionInfo = nullptr;
    HRESULT hr = GetAvailableCoreWebView2BrowserVersionString(nullptr, &versionInfo);
    if (SUCCEEDED(hr) && versionInfo) {
        CoTaskMemFree(versionInfo);
        return true;
    }
    return false;
}

void CookieGrabberDialog::initializeWebView(const QString &site)
{
    HWND hwnd = (HWND)m_webContainer->winId();
    
    // No CoreWebView2EnvironmentOptions needed.
    // The stealth script below is sufficient to avoid automation detection.
    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, nullptr, nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this, hwnd, site](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(result)) {
                    m_status->setText(tr("Failed to create WebView2 environment."));
                    return result;
                }
                m_env = env;
                m_env->AddRef();

                m_env->CreateCoreWebView2Controller(
                    hwnd,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this, site](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                            if (FAILED(result)) return result;
                            
                            m_controller = controller;
                            m_controller->AddRef();
                            m_controller->put_Bounds({0, 0, m_webContainer->width(), m_webContainer->height()});
                            m_controller->get_CoreWebView2(&m_webview);

                            // Inject the stealth script (this is what actually hides the automation)
                            m_webview->AddScriptToExecuteOnDocumentCreated(
                                LR"(
                                    Object.defineProperty(navigator, 'webdriver', { get: () => undefined });
                                    window.chrome = { runtime: {} };
                                    const originalQuery = window.navigator.permissions.query;
                                    window.navigator.permissions.query = (parameters) => (
                                        parameters.name === 'notifications' ?
                                        Promise.resolve({ state: Notification.permission }) :
                                        originalQuery(parameters)
                                    );
                                )", nullptr);

                            // Apply the custom User-Agent from settings (if provided)
                            ICoreWebView2Settings* settings = nullptr;
                            if (SUCCEEDED(m_webview->get_Settings(&settings)) && settings) {
                                ICoreWebView2Settings2* settings2 = nullptr;
                                if (SUCCEEDED(settings->QueryInterface(IID_ICoreWebView2Settings2, (void**)&settings2)) && settings2) {
                                    if (!m_requestedUserAgent.isEmpty()) {
                                        std::wstring wua = m_requestedUserAgent.toStdWString();
                                        settings2->put_UserAgent(wua.c_str());
                                        m_userAgent = m_requestedUserAgent;
                                    } else {
                                        LPWSTR defaultUa = nullptr;
                                        settings2->get_UserAgent(&defaultUa);
                                        m_userAgent = QString::fromWCharArray(defaultUa);
                                        CoTaskMemFree(defaultUa);
                                    }
                                    settings2->Release();
                                }
                                settings->Release();
                            }

                            EventRegistrationToken navTok;
                            m_webview->add_NavigationCompleted(
                                Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                    [this](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs *args) -> HRESULT {
                                        BOOL ok = FALSE;
                                        args->get_IsSuccess(&ok);
                                        if (ok && !m_pageLoaded) {
                                            m_pageLoaded = true;
                                            emit pageLoaded();
                                        }
                                        return S_OK;
                                    }).Get(), &navTok);

                            m_webview->Navigate(toWString("https://" + site).c_str());
                            m_status->setText(tr("Loading https://%1 …").arg(site));
                            
                            m_pollTimer->start();
                            return S_OK;
                        }).Get());
                return S_OK;
            }).Get());
}

void CookieGrabberDialog::fetchCookies(const QString &site)
{
    if (!m_webview) return;

    ICoreWebView2CookieManager* cookieManager = nullptr;
    ICoreWebView2_2* webview2 = nullptr;

    if (SUCCEEDED(m_webview->QueryInterface(IID_ICoreWebView2_2, (void**)&webview2)) && webview2) {
        webview2->get_CookieManager(&cookieManager);
        webview2->Release();
    }

    if (!cookieManager) return;

    QString url = "https://" + site;
    cookieManager->GetCookies(
        toWString(url).c_str(),
        Callback<ICoreWebView2GetCookiesCompletedHandler>(
            [this, cookieManager](HRESULT result, ICoreWebView2CookieList* cookieList) -> HRESULT {
                
                if (cookieManager) cookieManager->Release();
                if (FAILED(result) || !cookieList) return result;

                UINT count = 0;
                cookieList->get_Count(&count);
                QStringList allCookies;

                for (UINT i = 0; i < count; ++i) {
                    ICoreWebView2Cookie* cookie = nullptr;
                    cookieList->GetValueAtIndex(i, &cookie);

                    LPWSTR name, value;
                    cookie->get_Name(&name);
                    cookie->get_Value(&value);

                    QString cookieName = QString::fromWCharArray(name);
                    QString cookieValue = QString::fromWCharArray(value);
                    allCookies.append(cookieName);

                    if (cookieName == "cf_clearance") {
                        m_clearance = cookieValue;
                        m_status->setText(tr("cf_clearance captured successfully!"));
                        
                        CoTaskMemFree(name);
                        CoTaskMemFree(value);
                        cookie->Release();
                        
                        m_pollTimer->stop();
                        accept();
                        return S_OK;
                    }
                    CoTaskMemFree(name);
                    CoTaskMemFree(value);
                    cookie->Release();
                }

                if (allCookies.isEmpty()) {
                    m_status->setText(tr("Waiting for page to load... (No cookies yet)"));
                } else {
                    m_status->setText(tr("Waiting for cf_clearance... Current cookies: %1").arg(allCookies.join(", ")));
                }
                
                return S_OK;
            }).Get());
}

void CookieGrabberDialog::executeFetch(const QString &url, const QString &method, const QString &body, 
                                       const QString &apiKey, std::function<void(bool, QJsonObject, QString)> callback)
{
    if (!m_webview) {
        callback(false, QJsonObject(), "Browser is not ready");
        return;
    }

    QString escapedBody = body;
    escapedBody.replace("\\", "\\\\").replace("\"", "\\\"").replace("\n", "\\n").replace("\r", "\\r");

    QString js = QString(R"(
        (async () => {
            try {
                const r = await fetch("%1", {
                    method: '%2',
                    credentials: 'include',
                    headers: { 'Content-Type': 'application/json', 'x-access-token': '%3' },
                    body: %4
                });
                const text = await r.text();
                return JSON.stringify({ status: r.status, body: text });
            } catch (e) {
                return JSON.stringify({ status: 0, body: String(e) });
            }
        })()
    )").arg(url, method, apiKey, body.isEmpty() ? "null" : "\"" + escapedBody + "\"");

    QJsonObject evalArgs;
    evalArgs["expression"] = js;
    evalArgs["awaitPromise"] = true;
    evalArgs["returnByValue"] = true;
    QByteArray argsBytes = QJsonDocument(evalArgs).toJson(QJsonDocument::Compact);

    m_webview->CallDevToolsProtocolMethod(L"Runtime.evaluate", QString::fromUtf8(argsBytes).toStdWString().c_str(),
        Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler>(
            [callback](HRESULT result, LPCWSTR jsonResult) -> HRESULT {
                if (FAILED(result)) {
                    callback(false, QJsonObject(), "DevTools call failed");
                    return result;
                }

                QJsonDocument doc = QJsonDocument::fromJson(QString::fromWCharArray(jsonResult).toUtf8());
                QJsonObject outer = doc.object();
                
                if (outer.contains("result") && outer["result"].toObject().contains("value")) {
                    QString innerStr = outer["result"].toObject()["value"].toString();
                    QJsonObject inner = QJsonDocument::fromJson(innerStr.toUtf8()).object();
                    
                    int status = inner["status"].toInt();
                    QString body = inner["body"].toString();
                    
                    if (status >= 200 && status < 300) {
                        QJsonObject jsonResp = QJsonDocument::fromJson(body.toUtf8()).object();
                        callback(true, jsonResp, "");
                    } else {
                        callback(false, QJsonObject(), QString("HTTP %1: %2").arg(status).arg(body));
                    }
                } else {
                    callback(false, QJsonObject(), "Invalid DevTools response");
                }
                return S_OK;
            }).Get());
}