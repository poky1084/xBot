#include "ui/StakeLoginDialog.h"

#include "ui/WebViewSession.h"
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <windows.h>
#include <wrl.h>
#include <WebView2.h>

using namespace Microsoft::WRL;

// Checks the key in the page (same origin, so Cloudflare lets it through) at most every 3 s.
// `%1` = the key as a JSON string. Returns a JSON string {"ua","name","checked"}:
//   name     the account name when the key works ("" = not logged in / not checked yet)
//   checked  the answer belongs to this key
static QString pollScript(const QString &key)
{
    const QString keyJson = QString::fromUtf8(
        QJsonDocument(QJsonArray{key}).toJson(QJsonDocument::Compact)); // ["..."]
    return QStringLiteral(R"JS(
(function () {
    var key = %1[0];
    var r = { ua: navigator.userAgent, name: '', checked: false };
    try {
        if (key) {
            var now = Date.now();
            if (window.__xbotStakeKey !== key) {
                window.__xbotStakeKey = key;
                window.__xbotStakeName = '';
                window.__xbotStakeChecked = false;
                window.__xbotStakeCheckAt = 0;
            }
            if (!window.__xbotStakeCheckAt || now - window.__xbotStakeCheckAt > 3000) {
                window.__xbotStakeCheckAt = now;
                fetch('/_api/graphql', {
                    method: 'POST',
                    credentials: 'include',
                    headers: {
                        'Content-Type': 'application/json',
                        'Accept': 'application/json',
                        'x-access-token': key,
                        'x-xbot-check': '1'
                    },
                    body: JSON.stringify({ query: 'query XbotUser { user { id name } }', variables: {} })
                }).then(function (res) {
                    return res.json().catch(function () { return null; }).then(function (j) {
                        if (window.__xbotStakeKey !== key)
                            return;
                        var u = j && j.data && j.data.user;
                        window.__xbotStakeName = (res.ok && u && (u.name || u.id)) ? String(u.name || u.id) : '';
                        window.__xbotStakeChecked = true;
                    });
                }).catch(function () {});
            }
        }
    } catch (e) {}
    r.name = window.__xbotStakeName || '';
    r.checked = !!window.__xbotStakeChecked;
    return JSON.stringify(r);
})()
)JS").arg(keyJson);
}

StakeLoginDialog::StakeLoginDialog(const QString &site, QWidget *parent)
    : QDialog(parent)
    , m_site(site.trimmed().isEmpty() ? QStringLiteral("stake.com") : site.trimmed())
{
    setWindowTitle(tr("Log in to %1").arg(m_site));
    resize(1000, 720);

    auto *root = new QVBoxLayout(this);

    auto *hint = new QLabel(tr(
        "Log in to your Stake account in the browser below. As soon as you are logged in, the "
        "API key (x-access-token) and cf_clearance are filled in automatically and this window "
        "closes."));
    hint->setWordWrap(true);
    root->addWidget(hint);

    m_webContainer = new QWidget(this);
    m_webContainer->setStyleSheet(QStringLiteral("background-color: white;"));
    root->addWidget(m_webContainer, 1);

    m_status = new QLabel(tr("Initializing WebView2…"));
    m_status->setWordWrap(true);
    root->addWidget(m_status);

    auto *btnRow = new QHBoxLayout;
    m_useBtn = new QPushButton(tr("Use captured data"));
    m_useBtn->setToolTip(tr("Take the API key / cf_clearance found so far without waiting for "
                            "the automatic login check."));
    m_useBtn->setEnabled(false);
    auto *cancelBtn = new QPushButton(tr("Cancel"));
    btnRow->addStretch();
    btnRow->addWidget(m_useBtn);
    btnRow->addWidget(cancelBtn);
    root->addLayout(btnRow);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_useBtn, &QPushButton::clicked, this, &StakeLoginDialog::finish);

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(1000);
    connect(m_pollTimer, &QTimer::timeout, this, &StakeLoginDialog::poll);

    QTimer::singleShot(50, this, [this] { initializeWebView(); });
}

StakeLoginDialog::~StakeLoginDialog()
{
    if (m_pollTimer)
        m_pollTimer->stop();
    if (m_webview && m_requestHooked) {
        EventRegistrationToken token;
        token.value = m_requestToken;
        m_webview->remove_WebResourceRequested(token);
    }
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

bool StakeLoginDialog::isAvailable()
{
    LPWSTR versionInfo = nullptr;
    const HRESULT hr = GetAvailableCoreWebView2BrowserVersionString(nullptr, &versionInfo);
    if (SUCCEEDED(hr) && versionInfo) {
        CoTaskMemFree(versionInfo);
        return true;
    }
    return false;
}

void StakeLoginDialog::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);
    updateBounds();
}

void StakeLoginDialog::updateBounds()
{
    if (m_controller && m_webContainer)
        m_controller->put_Bounds({0, 0, m_webContainer->width(), m_webContainer->height()});
}

void StakeLoginDialog::initializeWebView()
{
    HWND hwnd = reinterpret_cast<HWND>(m_webContainer->winId());
    QPointer<StakeLoginDialog> self(this);

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
    const HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
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

                            // Same light anti-automation tweak as the hidden Stake browser.
                            self->m_webview->AddScriptToExecuteOnDocumentCreated(
                                LR"(Object.defineProperty(navigator, 'webdriver', { get: () => undefined });)",
                                nullptr);

                            // Catch the x-access-token the site itself sends with its GraphQL
                            // requests (read only, the requests are not changed).
                            self->m_webview->AddWebResourceRequestedFilter(
                                L"*/_api/graphql*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
                            EventRegistrationToken token{};
                            const HRESULT hookHr = self->m_webview->add_WebResourceRequested(
                                Callback<ICoreWebView2WebResourceRequestedEventHandler>(
                                    [self](ICoreWebView2 *,
                                           ICoreWebView2WebResourceRequestedEventArgs *args) -> HRESULT {
                                        if (!self || !args)
                                            return S_OK;
                                        ICoreWebView2WebResourceRequest *request = nullptr;
                                        if (FAILED(args->get_Request(&request)) || !request)
                                            return S_OK;
                                        ICoreWebView2HttpRequestHeaders *headers = nullptr;
                                        if (SUCCEEDED(request->get_Headers(&headers)) && headers) {
                                            BOOL own = FALSE; // our own login check
                                            headers->Contains(L"x-xbot-check", &own);
                                            LPWSTR value = nullptr;
                                            if (!own
                                                && SUCCEEDED(headers->GetHeader(L"x-access-token", &value))
                                                && value) {
                                                const QString v = QString::fromWCharArray(value).trimmed();
                                                CoTaskMemFree(value);
                                                if (!v.isEmpty() && v != QLatin1String("null")
                                                    && v != QLatin1String("undefined"))
                                                    self->m_headerKey = v;
                                            }
                                            headers->Release();
                                        }
                                        request->Release();
                                        return S_OK;
                                    }).Get(),
                                &token);
                            if (SUCCEEDED(hookHr)) {
                                self->m_requestToken = token.value;
                                self->m_requestHooked = true;
                            }

                            self->m_webview->Navigate(
                                (L"https://" + self->m_site.toStdWString()
                                 + L"/?tab=login&modal=auth").c_str());
                            self->m_status->setText(
                                tr("Loading https://%1 … log in to continue.").arg(self->m_site));
                            self->m_pollTimer->start();
                            return S_OK;
                        }).Get());
                return S_OK;
            }).Get());
    if (FAILED(hr)) // e.g. the WebView2 Runtime is not installed: the callback never runs
        m_status->setText(tr("Failed to start WebView2 (error 0x%1). Is the WebView2 Runtime installed?")
                              .arg(QString::number(static_cast<quint32>(hr), 16)));
}

void StakeLoginDialog::poll()
{
    if (!m_webview || m_polling)
        return;
    m_polling = true;
    LPWSTR source = nullptr;
    if (SUCCEEDED(m_webview->get_Source(&source)) && source) {
        const QString host = QUrl(QString::fromWCharArray(source)).host().toLower();
        CoTaskMemFree(source);
        if (!host.isEmpty())
            m_pageHost = host;
    }
    readCookies(); // then checkLogin()
}

// "session" is an httpOnly cookie (not visible to document.cookie): read the cookies through
// the WebView2 cookie manager.
void StakeLoginDialog::readCookies()
{
    ICoreWebView2CookieManager *cookieManager = nullptr;
    ICoreWebView2_2 *webview2 = nullptr;
    if (SUCCEEDED(m_webview->QueryInterface(IID_ICoreWebView2_2, reinterpret_cast<void **>(&webview2)))
        && webview2) {
        webview2->get_CookieManager(&cookieManager);
        webview2->Release();
    }
    if (!cookieManager) {
        // Old runtime: the header we caught is still usable.
        m_status->setText(tr("This WebView2 version cannot read cookies - update the WebView2 Runtime."));
        checkLogin();
        return;
    }

    QPointer<StakeLoginDialog> self(this);
    // Every cookie of the profile (empty URI): cf_clearance may sit on ".stake.com", on "www."
    // or on the mirror the site redirected to. Only the ones of this site are kept below.
    const HRESULT hr = cookieManager->GetCookies(
        L"",
        Callback<ICoreWebView2GetCookiesCompletedHandler>(
            [self, cookieManager](HRESULT result, ICoreWebView2CookieList *list) -> HRESULT {
                cookieManager->Release();
                if (!self)
                    return S_OK;
                if (SUCCEEDED(result) && list) {
                    UINT count = 0;
                    list->get_Count(&count);
                    QString session;
                    QString clearance;
                    for (UINT i = 0; i < count; ++i) {
                        ICoreWebView2Cookie *cookie = nullptr;
                        if (FAILED(list->GetValueAtIndex(i, &cookie)) || !cookie)
                            continue;
                        LPWSTR name = nullptr;
                        LPWSTR value = nullptr;
                        LPWSTR domain = nullptr;
                        cookie->get_Name(&name);
                        cookie->get_Value(&value);
                        cookie->get_Domain(&domain);
                        const QString n = name ? QString::fromWCharArray(name) : QString();
                        const QString v = value ? QString::fromWCharArray(value) : QString();
                        const QString d = domain ? QString::fromWCharArray(domain) : QString();
                        if (!v.isEmpty() && self->isSiteCookie(d)) {
                            if (n == QLatin1String("session"))
                                session = v;
                            else if (n == QLatin1String("cf_clearance"))
                                clearance = v;
                        }
                        if (name)
                            CoTaskMemFree(name);
                        if (value)
                            CoTaskMemFree(value);
                        if (domain)
                            CoTaskMemFree(domain);
                        cookie->Release();
                    }
                    self->m_cookieKey = session; // gone after a logout
                    if (!clearance.isEmpty())
                        self->m_clearance = clearance; // newest value (Cloudflare renews it)
                }
                self->checkLogin();
                return S_OK;
            }).Get());
    if (FAILED(hr)) {
        cookieManager->Release();
        checkLogin();
    }
}

void StakeLoginDialog::checkLogin()
{
    // The header is what the site really sends; the cookie is the fallback (same value on
    // stake.com).
    const QString key = !m_headerKey.isEmpty() ? m_headerKey : m_cookieKey;
    if (key != m_apiKey) {
        m_apiKey = key;
        m_userName.clear();
    }
    m_useBtn->setEnabled(!m_apiKey.isEmpty());

    QPointer<StakeLoginDialog> self(this);
    const QString script = pollScript(m_apiKey);
    const HRESULT hr = m_webview->ExecuteScript(
        reinterpret_cast<LPCWSTR>(script.utf16()),
        Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
            [self, key](HRESULT result, LPCWSTR resultJson) -> HRESULT {
                if (!self)
                    return S_OK;
                self->m_polling = false;
                if (FAILED(result) || !resultJson) {
                    self->updateStatus();
                    return S_OK;
                }

                // resultJson is the JSON encoding of the returned string: "\"{...}\"".
                const QJsonDocument wrapper = QJsonDocument::fromJson(
                    QByteArray("[") + QString::fromWCharArray(resultJson).toUtf8() + "]");
                const QJsonObject info =
                    QJsonDocument::fromJson(wrapper.array().at(0).toString().toUtf8()).object();

                const QString ua = info.value(QStringLiteral("ua")).toString().trimmed();
                if (!ua.isEmpty())
                    self->m_userAgent = ua;
                const QString name = info.value(QStringLiteral("name")).toString().trimmed();
                if (!key.isEmpty() && key == self->m_apiKey && !name.isEmpty()) {
                    self->m_userName = name;
                    self->loggedIn();
                    return S_OK;
                }
                // The site's own header was rejected (e.g. stale): try the cookie next time.
                if (info.value(QStringLiteral("checked")).toBool() && name.isEmpty()
                    && key == self->m_headerKey && !self->m_cookieKey.isEmpty()
                    && self->m_cookieKey != key)
                    self->m_headerKey.clear();
                self->updateStatus();
                return S_OK;
            }).Get());
    if (FAILED(hr)) {
        m_polling = false;
        updateStatus();
    }
}

// The cookie's domain (".stake.com", "stake.com", "www.stake.com") belongs to the site typed
// in Options or to the page now shown.
bool StakeLoginDialog::isSiteCookie(const QString &cookieDomain) const
{
    QString d = cookieDomain.trimmed().toLower();
    while (d.startsWith(QLatin1Char('.')))
        d.remove(0, 1);
    if (d.isEmpty())
        return false;
    for (QString host : {m_site, m_pageHost}) {
        host = host.trimmed().toLower();
        if (host.isEmpty())
            continue;
        if (host == d || host.endsWith(QLatin1Char('.') + d) || d.endsWith(QLatin1Char('.') + host))
            return true;
    }
    return false;
}

void StakeLoginDialog::loggedIn()
{
    if (!m_clearance.isEmpty()) {
        finish();
        return;
    }
    // Cloudflare may set cf_clearance a moment after the login: keep reading the cookies for
    // a few more polls (1 s each), then close anyway (no challenge = no cookie).
    if (m_clearanceWait < 0)
        m_clearanceWait = 8;
    if (m_clearanceWait-- == 0) {
        finish();
        return;
    }
    m_status->setText(tr("Logged in as %1 — waiting for the cf_clearance cookie… (%2)")
                          .arg(m_userName)
                          .arg(m_clearanceWait + 1));
}

void StakeLoginDialog::updateStatus()
{
    if (m_apiKey.isEmpty()) {
        m_status->setText(m_clearance.isEmpty()
                              ? tr("Waiting for the login…")
                              : tr("Found cf_clearance - waiting for the login…"));
        return;
    }
    m_status->setText(tr("Found the API key%1 - checking the login…")
                          .arg(m_clearance.isEmpty() ? QString() : tr(" and cf_clearance")));
}

void StakeLoginDialog::finish()
{
    if (m_apiKey.isEmpty())
        return;
    m_pollTimer->stop();
    m_status->setText(m_clearance.isEmpty() ? tr("API key captured (no cf_clearance cookie).")
                                            : tr("API key and cf_clearance captured."));
    accept();
}
