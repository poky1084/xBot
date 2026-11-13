#include "ui/WinnaLoginDialog.h"

#include "bot/LiveSettings.h"

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

#include <memory>

#include <windows.h>
#include <wrl.h>
#include <WebView2.h>

using namespace Microsoft::WRL;

// Returns a JSON string {"device","ua","loggedIn"}.
//  device    localStorage "wn-antifraud-device-id" -> "id"
//  loggedIn  GET api2.winna.com/v1/payments/wallets answered with the wallet list (checked in the
//            background at most every 3 s; kept in window.__xbotWinnaLoggedIn for the next poll)
static const wchar_t *kPollScript = LR"JS(
(function () {
    var r = { device: '', ua: navigator.userAgent, loggedIn: false };
    try {
        var d = localStorage.getItem('wn-antifraud-device-id');
        if (d) {
            try { r.device = String(JSON.parse(d).id || ''); } catch (e) { r.device = String(d); }
        }
    } catch (e) {}
    try {
        var now = Date.now();
        if (!window.__xbotWinnaCheckAt || now - window.__xbotWinnaCheckAt > 3000) {
            window.__xbotWinnaCheckAt = now;
            var headers = { 'Accept': 'application/json, text/plain, */*' };
            if (r.device)
                headers['x-antifraud-device-id'] = r.device;
            fetch('https://api2.' + location.hostname.replace(/^www\./, '') + '/v1/payments/wallets?_wnNoCache=' + now, {
                method: 'GET', credentials: 'include', headers: headers
            }).then(function (res) {
                return res.json().catch(function () { return null; }).then(function (j) {
                    window.__xbotWinnaLoggedIn = !!(res.ok && j && j.success !== false && j.response
                                                    && Array.isArray(j.response.payload));
                });
            }).catch(function () {});
        }
    } catch (e) {}
    r.loggedIn = !!window.__xbotWinnaLoggedIn;
    return JSON.stringify(r);
})()
)JS";

WinnaLoginDialog::WinnaLoginDialog(bool freshStart, const QString &site, QWidget *parent)
    : QDialog(parent)
    , m_site(LiveSettings::hostOf(site, LiveSettings::defaultSite(LiveSettings::Casino::Winna)))
    , m_freshStart(freshStart)
{
    setWindowTitle(tr("Log in to Winna.com"));
    resize(1000, 720);

    auto *root = new QVBoxLayout(this);

    auto *hint = new QLabel(tr(
        "Log in to your Winna.com account in the browser below. As soon as you are logged in, "
        "the session (\"connect.sid\" cookie), cf_clearance, device id and User-Agent are filled "
        "in automatically and this window closes."));
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
    m_useBtn->setToolTip(tr("Take the session / cf_clearance found so far without waiting "
                            "for the automatic login check."));
    m_useBtn->setEnabled(false);
    auto *cancelBtn = new QPushButton(tr("Cancel"));
    btnRow->addStretch();
    btnRow->addWidget(m_useBtn);
    btnRow->addWidget(cancelBtn);
    root->addLayout(btnRow);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_useBtn, &QPushButton::clicked, this, &WinnaLoginDialog::finish);

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(1000);
    connect(m_pollTimer, &QTimer::timeout, this, &WinnaLoginDialog::poll);

    QTimer::singleShot(50, this, [this] { initializeWebView(); });
}

WinnaLoginDialog::~WinnaLoginDialog()
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

bool WinnaLoginDialog::isAvailable()
{
    LPWSTR versionInfo = nullptr;
    const HRESULT hr = GetAvailableCoreWebView2BrowserVersionString(nullptr, &versionInfo);
    if (SUCCEEDED(hr) && versionInfo) {
        CoTaskMemFree(versionInfo);
        return true;
    }
    return false;
}

void WinnaLoginDialog::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);
    updateBounds();
}

void WinnaLoginDialog::updateBounds()
{
    if (m_controller && m_webContainer)
        m_controller->put_Bounds({0, 0, m_webContainer->width(), m_webContainer->height()});
}

void WinnaLoginDialog::initializeWebView()
{
    HWND hwnd = reinterpret_cast<HWND>(m_webContainer->winId());
    QPointer<WinnaLoginDialog> self(this);

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

                            // Same light anti-automation tweak as the Stake browser.
                            self->m_webview->AddScriptToExecuteOnDocumentCreated(
                                LR"(Object.defineProperty(navigator, 'webdriver', { get: () => undefined });)",
                                nullptr);

                            if (self->m_freshStart) {
                                self->m_status->setText(tr("Removing the old %1 login data…").arg(self->siteBase()));
                                QPointer<WinnaLoginDialog> again(self);
                                self->clearSiteData([again] {
                                    if (again)
                                        again->openSite();
                                });
                            } else {
                                self->openSite();
                            }
                            return S_OK;
                        }).Get());
                return S_OK;
            }).Get());
    if (FAILED(hr)) // e.g. the WebView2 Runtime is not installed: the callback never runs
        m_status->setText(tr("Failed to start WebView2 (error 0x%1). Is the WebView2 Runtime installed?")
                              .arg(QString::number(static_cast<quint32>(hr), 16)));
}

void WinnaLoginDialog::poll()
{
    if (!m_webview || m_polling)
        return;
    m_polling = true;

    QPointer<WinnaLoginDialog> self(this);
    const HRESULT hr = m_webview->ExecuteScript(
        kPollScript,
        Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
            [self](HRESULT result, LPCWSTR resultJson) -> HRESULT {
                if (!self)
                    return S_OK;
                if (FAILED(result) || !resultJson) {
                    self->m_polling = false;
                    return S_OK;
                }

                // resultJson is the JSON encoding of the returned string: "\"{...}\"".
                const QJsonDocument wrapper = QJsonDocument::fromJson(
                    QByteArray("[") + QString::fromWCharArray(resultJson).toUtf8() + "]");
                const QJsonObject info =
                    QJsonDocument::fromJson(wrapper.array().at(0).toString().toUtf8()).object();

                const QString device = info.value(QStringLiteral("device")).toString().trimmed();
                const QString ua = info.value(QStringLiteral("ua")).toString().trimmed();
                if (!ua.isEmpty())
                    self->m_userAgent = ua;
                if (!device.isEmpty())
                    self->m_deviceId = device;
                self->readCookies(info.value(QStringLiteral("loggedIn")).toBool());
                return S_OK;
            }).Get());
    if (FAILED(hr))
        m_polling = false;
}

// "connect.sid" and "cf_clearance" are httpOnly cookies (not visible to document.cookie): read
// them through the WebView2 cookie manager.
void WinnaLoginDialog::openSite()
{
    if (!m_webview)
        return;
    m_webview->Navigate((L"https://" + m_site.toStdWString() + L"/").c_str());
    m_status->setText(tr("Loading https://%1 … log in to continue.").arg(m_site));
    m_pollTimer->start();
}

// Deletes every cookie of winna.com and its subdomains (api2, originals, games-prod, …) and the
// sites' local storage from this browser profile, then calls `then`. Other casinos' data stays.
void WinnaLoginDialog::clearSiteData(std::function<void()> then)
{
    ICoreWebView2CookieManager *cookieManager = nullptr;
    ICoreWebView2_2 *webview2 = nullptr;
    if (SUCCEEDED(m_webview->QueryInterface(IID_ICoreWebView2_2, reinterpret_cast<void **>(&webview2)))
        && webview2) {
        webview2->get_CookieManager(&cookieManager);
        webview2->Release();
    }

    // Local / session storage, IndexedDB, caches of the winna.com origins (DevTools protocol),
    // one after the other; the last one continues with `then`.
    auto clearStorage = [this, then]() {
        const QString base = siteBase();
        auto origins = std::make_shared<QStringList>();
        for (const char *sub : {"", "www.", "games-prod.", "originals.", "api2."})
            *origins << QStringLiteral("https://") + QLatin1String(sub) + base;
        auto next = std::make_shared<std::function<void()>>();
        QPointer<WinnaLoginDialog> self(this);
        *next = [self, origins, next, then]() {
            if (!self || !self->m_webview)
                return;
            if (origins->isEmpty()) {
                then();
                return;
            }
            const QString origin = origins->takeFirst();
            const QString params = QStringLiteral(
                "{\"origin\":\"%1\",\"storageTypes\":\"local_storage,session_storage,indexeddb,"
                "cache_storage,service_workers,websql\"}").arg(origin);
            const HRESULT hr = self->m_webview->CallDevToolsProtocolMethod(
                L"Storage.clearDataForOrigin", reinterpret_cast<LPCWSTR>(params.utf16()),
                Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler>(
                    [next](HRESULT, LPCWSTR) -> HRESULT {
                        (*next)();
                        return S_OK;
                    }).Get());
            if (FAILED(hr))
                (*next)();
        };
        (*next)();
    };

    if (!cookieManager) {
        clearStorage();
        return;
    }
    // An empty URI lists every cookie of the profile: delete those of winna.com.
    QPointer<WinnaLoginDialog> self(this);
    const QString base = siteBase();
    const HRESULT hr = cookieManager->GetCookies(
        L"",
        Callback<ICoreWebView2GetCookiesCompletedHandler>(
            [self, cookieManager, clearStorage, base](HRESULT result, ICoreWebView2CookieList *list) -> HRESULT {
                if (SUCCEEDED(result) && list) {
                    UINT count = 0;
                    list->get_Count(&count);
                    for (UINT i = 0; i < count; ++i) {
                        ICoreWebView2Cookie *cookie = nullptr;
                        if (FAILED(list->GetValueAtIndex(i, &cookie)) || !cookie)
                            continue;
                        LPWSTR domain = nullptr;
                        cookie->get_Domain(&domain);
                        const QString d = domain ? QString::fromWCharArray(domain).toLower() : QString();
                        if (domain)
                            CoTaskMemFree(domain);
                        QString bare = d;
                        while (bare.startsWith(QLatin1Char('.')))
                            bare.remove(0, 1);
                        if (bare == base || bare.endsWith(QLatin1Char('.') + base))
                            cookieManager->DeleteCookie(cookie);
                        cookie->Release();
                    }
                }
                cookieManager->Release();
                if (self)
                    clearStorage();
                return S_OK;
            }).Get());
    if (FAILED(hr)) {
        cookieManager->Release();
        clearStorage();
    }
}

void WinnaLoginDialog::readCookies(bool loggedIn)
{
    ICoreWebView2CookieManager *cookieManager = nullptr;
    ICoreWebView2_2 *webview2 = nullptr;
    if (SUCCEEDED(m_webview->QueryInterface(IID_ICoreWebView2_2, reinterpret_cast<void **>(&webview2)))
        && webview2) {
        webview2->get_CookieManager(&cookieManager);
        webview2->Release();
    }
    if (!cookieManager) {
        m_polling = false;
        m_status->setText(tr("This WebView2 version cannot read cookies - update the WebView2 Runtime."));
        return;
    }

    QPointer<WinnaLoginDialog> self(this);
    // winna.com's cookies in the browser's order (cf_clearance is found for this URL).
    const std::wstring url = L"https://" + m_site.toStdWString() + L"/";
    const HRESULT hr = cookieManager->GetCookies(
        url.c_str(),
        Callback<ICoreWebView2GetCookiesCompletedHandler>(
            [self, cookieManager, loggedIn](HRESULT result, ICoreWebView2CookieList *list) -> HRESULT {
                cookieManager->Release();
                if (!self)
                    return S_OK;
                self->m_polling = false;
                if (FAILED(result) || !list)
                    return S_OK;

                UINT count = 0;
                list->get_Count(&count);
                // Every winna.com cookie goes with the requests (like the browser sends them):
                // connect.sid first, cf_clearance separately (it has its own field).
                QString sessionCookie;
                QStringList otherCookies;
                QStringList seen; // a name listed twice (stale copy): the first one is what the browser sends
                for (UINT i = 0; i < count; ++i) {
                    ICoreWebView2Cookie *cookie = nullptr;
                    if (FAILED(list->GetValueAtIndex(i, &cookie)) || !cookie)
                        continue;
                    LPWSTR name = nullptr;
                    LPWSTR value = nullptr;
                    cookie->get_Name(&name);
                    cookie->get_Value(&value);
                    const QString n = name ? QString::fromWCharArray(name) : QString();
                    const QString v = value ? QString::fromWCharArray(value) : QString();
                    const bool first = !n.isEmpty() && !seen.contains(n);
                    if (first)
                        seen << n;
                    if (!first || v.isEmpty()) {
                        // skip: empty or a second copy of a cookie already taken
                    } else if (n == QLatin1String("connect.sid")) {
                        sessionCookie = n + QLatin1Char('=') + v;
                    } else if (n == QLatin1String("cf_clearance")) {
                        self->m_clearance = v;
                    } else if (!n.contains(QLatin1Char(';')) && !v.contains(QLatin1Char(';'))) {
                        otherCookies << n + QLatin1Char('=') + v;
                    }
                    if (name)
                        CoTaskMemFree(name);
                    if (value)
                        CoTaskMemFree(value);
                    cookie->Release();
                }

                if (!sessionCookie.isEmpty())
                    self->m_token = (QStringList{sessionCookie} + otherCookies).join(QStringLiteral("; "));
                const bool haveData = !self->m_token.isEmpty();
                self->m_useBtn->setEnabled(haveData);
                if (haveData && loggedIn) {
                    self->finish();
                    return S_OK;
                }
                self->m_status->setText(
                    haveData ? tr("Found the \"connect.sid\" session%1 - waiting for the login check…")
                                   .arg(self->m_clearance.isEmpty() ? QString()
                                                                    : tr(" and cf_clearance"))
                             : tr("Waiting for the login…"));
                return S_OK;
            }).Get());
    if (FAILED(hr)) {
        cookieManager->Release();
        m_polling = false;
    }
}

void WinnaLoginDialog::finish()
{
    if (m_token.isEmpty())
        return;
    m_pollTimer->stop();
    m_status->setText(tr("Logged in — session and cf_clearance captured."));
    accept();
}
