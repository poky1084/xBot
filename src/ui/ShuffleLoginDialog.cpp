#include "ui/ShuffleLoginDialog.h"

#include "ui/WebViewSession.h"
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <windows.h>
#include <wrl.h>
#include <WebView2.h>

using namespace Microsoft::WRL;

// Returns a JSON string with the three values once all of them exist, otherwise "".
static const wchar_t *kReadStorageScript = LR"JS(
(function () {
    try {
        var root = JSON.parse(localStorage.getItem("persist:root"));
        var token = JSON.parse(root.auth).accessToken;
        var correlation = localStorage.getItem('CORRELATION_USER_ID');
        var browser = localStorage.getItem('BROWSER_ID');
        if (token && correlation && browser)
            return JSON.stringify({ token: token, correlation: correlation, browser: browser });
    } catch (e) {}
    return "";
})()
)JS";

ShuffleLoginDialog::ShuffleLoginDialog(const QString &site, QWidget *parent)
    : QDialog(parent)
{
    // Accept "shuffle.com", "https://shuffle.com/" etc.
    QString host = site.trimmed();
    const int scheme = host.indexOf(QLatin1String("://"));
    if (scheme >= 0)
        host = host.mid(scheme + 3);
    const int slash = host.indexOf(QLatin1Char('/'));
    if (slash >= 0)
        host = host.left(slash);
    m_site = host.isEmpty() ? QStringLiteral("shuffle.com") : host;

    setWindowTitle(tr("Log in to Shuffle"));
    resize(1000, 720);

    auto *root = new QVBoxLayout(this);

    auto *hint = new QLabel(tr(
        "Log in to your Shuffle account in the browser below. As soon as you are logged in, "
        "the token, CORRELATION_USER_ID and BROWSER_ID are filled in automatically and this "
        "window closes."));
    hint->setWordWrap(true);
    root->addWidget(hint);

    m_webContainer = new QWidget(this);
    m_webContainer->setStyleSheet(QStringLiteral("background-color: white;"));
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
    m_pollTimer->setInterval(1000);
    connect(m_pollTimer, &QTimer::timeout, this, &ShuffleLoginDialog::pollStorage);

    QTimer::singleShot(50, this, [this] { initializeWebView(); });
}

ShuffleLoginDialog::~ShuffleLoginDialog()
{
    if (m_pollTimer)
        m_pollTimer->stop();
    if (m_controller) {
        m_controller->Close();
        m_controller->Release();
    }
    if (m_webview)
        m_webview->Release();
    if (m_env)
        m_env->Release();
}

bool ShuffleLoginDialog::isAvailable()
{
    LPWSTR versionInfo = nullptr;
    const HRESULT hr = GetAvailableCoreWebView2BrowserVersionString(nullptr, &versionInfo);
    if (SUCCEEDED(hr) && versionInfo) {
        CoTaskMemFree(versionInfo);
        return true;
    }
    return false;
}

void ShuffleLoginDialog::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);
    updateBounds();
}

void ShuffleLoginDialog::updateBounds()
{
    if (m_controller && m_webContainer)
        m_controller->put_Bounds({0, 0, m_webContainer->width(), m_webContainer->height()});
}

void ShuffleLoginDialog::initializeWebView()
{
    HWND hwnd = reinterpret_cast<HWND>(m_webContainer->winId());
    QPointer<ShuffleLoginDialog> self(this);

    // Every WebView2 environment in this process (same user-data folder) must use identical
    // browser arguments, so keep them the same as the hidden browser used for Stake.
    if (qEnvironmentVariableIsEmpty("WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS")) {
        qputenv("WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS",
                "--disable-background-timer-throttling --disable-renderer-backgrounding "
                "--disable-backgrounding-occluded-windows "
                "--disable-features=CalculateNativeWinOcclusion");
    }

    // Own user-data folder for windows opened with "+" (they start logged out).
    const std::wstring userDataFolder = WebViewSession::folderW(this);
    CreateCoreWebView2EnvironmentWithOptions(
        nullptr, userDataFolder.empty() ? nullptr : userDataFolder.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [self, hwnd](HRESULT result, ICoreWebView2Environment *env) -> HRESULT {
                if (!self)
                    return S_OK;
                if (FAILED(result) || !env) {
                    self->m_status->setText(tr("Failed to create WebView2 environment."));
                    return result;
                }
                self->m_env = env;
                self->m_env->AddRef();

                self->m_env->CreateCoreWebView2Controller(
                    hwnd,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [self](HRESULT result, ICoreWebView2Controller *controller) -> HRESULT {
                            if (!self)
                                return S_OK;
                            if (FAILED(result) || !controller) {
                                self->m_status->setText(tr("Failed to create the browser window."));
                                return result;
                            }
                            self->m_controller = controller;
                            self->m_controller->AddRef();
                            self->m_controller->put_IsVisible(TRUE);
                            self->m_controller->get_CoreWebView2(&self->m_webview);
                            self->updateBounds();

                            // Same light anti-automation tweak as the Stake browser.
                            self->m_webview->AddScriptToExecuteOnDocumentCreated(
                                LR"(Object.defineProperty(navigator, 'webdriver', { get: () => undefined });)",
                                nullptr);

                            self->m_webview->Navigate(
                                (L"https://" + self->m_site.toStdWString()).c_str());
                            self->m_status->setText(
                                tr("Loading https://%1 … log in to continue.").arg(self->m_site));
                            self->m_pollTimer->start();
                            return S_OK;
                        }).Get());
                return S_OK;
            }).Get());
}

void ShuffleLoginDialog::pollStorage()
{
    if (!m_webview || m_polling)
        return;
    m_polling = true;

    QPointer<ShuffleLoginDialog> self(this);
    const HRESULT hr = m_webview->ExecuteScript(
        kReadStorageScript,
        Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
            [self](HRESULT result, LPCWSTR resultJson) -> HRESULT {
                if (!self)
                    return S_OK;
                self->m_polling = false;
                if (FAILED(result) || !resultJson)
                    return S_OK;

                // resultJson is the JSON encoding of the returned string, e.g. "\"{...}\"".
                const QJsonDocument wrapper = QJsonDocument::fromJson(
                    QByteArray("[") + QString::fromWCharArray(resultJson).toUtf8() + "]");
                const QString payload = wrapper.array().at(0).toString();
                if (payload.isEmpty())
                    return S_OK; // not logged in yet

                const QJsonObject o = QJsonDocument::fromJson(payload.toUtf8()).object();
                const QString token = o.value("token").toString();
                const QString correlation = o.value("correlation").toString();
                const QString browser = o.value("browser").toString();
                if (token.isEmpty() || correlation.isEmpty() || browser.isEmpty())
                    return S_OK;

                self->m_token = token;
                self->m_correlationId = correlation;
                self->m_browserId = browser;
                self->m_pollTimer->stop();
                self->m_status->setText(tr("Logged in — credentials captured."));
                self->accept();
                return S_OK;
            }).Get());
    if (FAILED(hr))
        m_polling = false;
}
