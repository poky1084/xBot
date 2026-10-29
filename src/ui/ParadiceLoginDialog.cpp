#include "ui/ParadiceLoginDialog.h"

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

// Returns the token once it exists, otherwise "". It is stored either as a plain string
// or JSON-encoded (with quotes).
static const wchar_t *kReadStorageScript = LR"JS(
(function () {
    try {
        var t = localStorage.getItem('token');
        if (t) {
            t = String(t).trim();
            if (t.length > 1 && t.charAt(0) === '"' && t.charAt(t.length - 1) === '"')
                t = t.substring(1, t.length - 1);
            // Only a JWT counts (logged out the key may hold "null" or "").
            if (t.split('.').length === 3)
                return t;
        }
    } catch (e) {}
    return "";
})()
)JS";

ParadiceLoginDialog::ParadiceLoginDialog(QWidget *parent)
    : QDialog(parent)
    , m_site(QStringLiteral("paradice.in"))
{
    setWindowTitle(tr("Log in to Paradice"));
    resize(1000, 720);

    auto *root = new QVBoxLayout(this);

    auto *hint = new QLabel(tr(
        "Log in to your Paradice account in the browser below. As soon as you are logged in, "
        "the token is filled in automatically and this window closes."));
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
    connect(m_pollTimer, &QTimer::timeout, this, &ParadiceLoginDialog::pollStorage);

    QTimer::singleShot(50, this, [this] { initializeWebView(); });
}

ParadiceLoginDialog::~ParadiceLoginDialog()
{
    if (m_pollTimer)
        m_pollTimer->stop();
    if (m_controller) {
        m_controller->put_IsVisible(FALSE); // hidden first: it lets go of the mouse cursor
        m_controller->Close();
        m_controller->Release();
    }
    if (m_webview)
        m_webview->Release();
    if (m_env)
        m_env->Release();
}

bool ParadiceLoginDialog::isAvailable()
{
    LPWSTR versionInfo = nullptr;
    const HRESULT hr = GetAvailableCoreWebView2BrowserVersionString(nullptr, &versionInfo);
    if (SUCCEEDED(hr) && versionInfo) {
        CoTaskMemFree(versionInfo);
        return true;
    }
    return false;
}

void ParadiceLoginDialog::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);
    updateBounds();
}

void ParadiceLoginDialog::updateBounds()
{
    if (m_controller && m_webContainer)
        m_controller->put_Bounds({0, 0, m_webContainer->width(), m_webContainer->height()});
}

void ParadiceLoginDialog::initializeWebView()
{
    HWND hwnd = reinterpret_cast<HWND>(m_webContainer->winId());
    QPointer<ParadiceLoginDialog> self(this);

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

void ParadiceLoginDialog::pollStorage()
{
    if (!m_webview || m_polling)
        return;
    m_polling = true;

    QPointer<ParadiceLoginDialog> self(this);
    const HRESULT hr = m_webview->ExecuteScript(
        kReadStorageScript,
        Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
            [self](HRESULT result, LPCWSTR resultJson) -> HRESULT {
                if (!self)
                    return S_OK;
                self->m_polling = false;
                if (FAILED(result) || !resultJson)
                    return S_OK;

                // resultJson is the JSON encoding of the returned string, e.g. "\"eyJ...\"".
                const QJsonDocument wrapper = QJsonDocument::fromJson(
                    QByteArray("[") + QString::fromWCharArray(resultJson).toUtf8() + "]");
                const QString payload = wrapper.array().at(0).toString();
                if (payload.isEmpty())
                    return S_OK; // not logged in yet

                QString token = payload.trimmed();
                if (token.startsWith(QLatin1String("Bearer "), Qt::CaseInsensitive))
                    token = token.mid(7).trimmed();
                if (token.isEmpty())
                    return S_OK;

                self->m_token = token;
                self->m_pollTimer->stop();
                self->m_status->setText(tr("Logged in — token captured."));
                self->accept();
                return S_OK;
            }).Get());
    if (FAILED(hr))
        m_polling = false;
}
