#include "ui/DuelBrowser.h"

#include "ui/WebViewSession.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QResizeEvent>
#include <QTimer>

#include <windows.h>
#include <wrl.h>
#include <WebView2.h>

using namespace Microsoft::WRL;

namespace
{
// Defined once per document: fetch() with the site's own cookies, 15 s timeout.
const wchar_t *kHelperScript = LR"JS(
if (window.top === window) {
    Object.defineProperty(window, '__xbotDuel', {
        configurable: true, enumerable: false, writable: true,
        value: async (method, url, body, uuid, balancesOnly) => {
            try {
                if (!uuid) {
                    try { uuid = String(localStorage.getItem('security:uuid') || '').replace(/^"+|"+$/g, ''); } catch (e) {}
                }
                const headers = { 'Accept': 'application/json, text/plain, */*' };
                if (uuid)
                    headers['x-duel-device-identifier'] = uuid;
                if (body)
                    headers['Content-Type'] = 'application/json';
                const ctl = new AbortController();
                const timer = setTimeout(() => ctl.abort(), 15000);
                let r;
                try {
                    r = await fetch(url, { method: method, credentials: 'include', headers: headers,
                                           body: body ? body : undefined, signal: ctl.signal,
                                           cache: 'no-store' });
                } finally {
                    clearTimeout(timer);
                }
                let text = await r.text();
                if (balancesOnly) {
                    try {
                        const j = JSON.parse(text);
                        const u = j && j.user;
                        text = JSON.stringify({ user: u ? { id: u.id, balances: u.balances || [] } : null,
                                                message: j && j.message });
                    } catch (e) {}
                }
                return { status: r.status, text: text };
            } catch (e) {
                return { status: 0, text: (e && e.name === 'AbortError') ? 'timeout (15 s)' : String(e) };
            }
        }
    });
}
)JS";

QString jsString(const QString &s)
{
    const QByteArray arr = QJsonDocument(QJsonArray{s}).toJson(QJsonDocument::Compact); // ["..."]
    return QString::fromUtf8(arr.mid(1, arr.size() - 2));
}
} // namespace

DuelBrowser::DuelBrowser(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle(QStringLiteral("Duel (hidden browser)"));
    resize(1000, 720);
    QTimer::singleShot(0, this, [this] { initializeWebView(); });
}

DuelBrowser::~DuelBrowser()
{
    if (m_controller) {
        m_controller->Close();
        m_controller->Release();
    }
    if (m_webview)
        m_webview->Release();
    if (m_env)
        m_env->Release();
}

bool DuelBrowser::isAvailable()
{
    LPWSTR versionInfo = nullptr;
    const HRESULT hr = GetAvailableCoreWebView2BrowserVersionString(nullptr, &versionInfo);
    if (SUCCEEDED(hr) && versionInfo) {
        CoTaskMemFree(versionInfo);
        return true;
    }
    return false;
}

void DuelBrowser::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_controller)
        m_controller->put_Bounds({0, 0, width(), height()});
}

void DuelBrowser::initializeWebView()
{
    HWND hwnd = reinterpret_cast<HWND>(winId());
    QPointer<DuelBrowser> self(this);

    // Same browser arguments as every other WebView2 environment in this process.
    if (qEnvironmentVariableIsEmpty("WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS")) {
        qputenv("WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS",
                "--disable-background-timer-throttling --disable-renderer-backgrounding "
                "--disable-backgrounding-occluded-windows "
                "--disable-features=CalculateNativeWinOcclusion");
    }

    const std::wstring userDataFolder = WebViewSession::folderW(this);
    CreateCoreWebView2EnvironmentWithOptions(
        nullptr, userDataFolder.empty() ? nullptr : userDataFolder.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [self, hwnd](HRESULT result, ICoreWebView2Environment *env) -> HRESULT {
                if (!self || FAILED(result) || !env)
                    return S_OK;
                self->m_env = env;
                self->m_env->AddRef();
                self->m_env->CreateCoreWebView2Controller(
                    hwnd,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [self](HRESULT result, ICoreWebView2Controller *controller) -> HRESULT {
                            if (!self || FAILED(result) || !controller)
                                return S_OK;
                            self->m_controller = controller;
                            self->m_controller->AddRef();
                            self->m_controller->put_Bounds({0, 0, self->width(), self->height()});
                            self->m_controller->put_IsVisible(TRUE);
                            self->m_controller->get_CoreWebView2(&self->m_webview);

                            self->m_webview->AddScriptToExecuteOnDocumentCreated(
                                LR"(Object.defineProperty(navigator, 'webdriver', { get: () => undefined });)",
                                nullptr);
                            self->m_webview->AddScriptToExecuteOnDocumentCreated(kHelperScript, nullptr);

                            EventRegistrationToken navTok;
                            self->m_webview->add_NavigationCompleted(
                                Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                    [self](ICoreWebView2 *, ICoreWebView2NavigationCompletedEventArgs *args) -> HRESULT {
                                        if (!self)
                                            return S_OK;
                                        BOOL ok = FALSE;
                                        args->get_IsSuccess(&ok);
                                        if (ok && !self->m_pageLoaded) {
                                            self->m_pageLoaded = true;
                                            emit self->pageLoaded();
                                        } else if (!ok && !self->m_pageLoaded) {
                                            // e.g. a Cloudflare check that redirects: try again shortly.
                                            QTimer::singleShot(5000, self.data(), [self] {
                                                if (self && self->m_webview && !self->m_pageLoaded)
                                                    self->navigateToSite();
                                            });
                                        }
                                        return S_OK;
                                    }).Get(),
                                &navTok);

                            self->applySessionCookie();
                            self->navigateToSite();
                            return S_OK;
                        }).Get());
                return S_OK;
            }).Get());
}

void DuelBrowser::navigateToSite()
{
    if (m_webview)
        m_webview->Navigate((L"https://" + m_site.toStdWString() + L"/dice").c_str());
}

void DuelBrowser::setSite(const QString &site)
{
    const QString s = site.trimmed().isEmpty() ? QStringLiteral("duel.com") : site.trimmed();
    if (s == m_site)
        return;
    m_site = s;
    if (!m_webview)
        return; // the first navigation (initializeWebView) uses the new site
    // Requests wait until the page on the new site has loaded (pageLoaded is emitted again).
    m_pageLoaded = false;
    applySessionCookie();
    navigateToSite();
}

void DuelBrowser::setSessionCookie(const QString &token)
{
    const QString t = token.trimmed();
    if (t.isEmpty() || t == m_sessionCookie)
        return;
    m_sessionCookie = t;
    applySessionCookie();
}

void DuelBrowser::applySessionCookie()
{
    if (!m_webview || m_sessionCookie.isEmpty())
        return;
    ICoreWebView2_2 *webview2 = nullptr;
    ICoreWebView2CookieManager *manager = nullptr;
    if (SUCCEEDED(m_webview->QueryInterface(IID_ICoreWebView2_2, reinterpret_cast<void **>(&webview2)))
        && webview2) {
        webview2->get_CookieManager(&manager);
        webview2->Release();
    }
    if (!manager)
        return;
    ICoreWebView2Cookie *cookie = nullptr;
    const std::wstring value = m_sessionCookie.toStdWString();
    const std::wstring domain = m_site.toStdWString();
    if (SUCCEEDED(manager->CreateCookie(L"duel", value.c_str(), domain.c_str(), L"/", &cookie)) && cookie) {
        cookie->put_IsHttpOnly(TRUE);
        cookie->put_IsSecure(TRUE);
        cookie->put_SameSite(COREWEBVIEW2_COOKIE_SAME_SITE_KIND_LAX);
        manager->AddOrUpdateCookie(cookie);
        cookie->Release();
    }
    manager->Release();
}

void DuelBrowser::fetch(const QString &method, const QString &path, const QByteArray &body,
                        const QString &uuid, FetchCallback cb, bool balancesOnly)
{
    if (!m_webview || !m_pageLoaded) {
        QTimer::singleShot(0, this, [cb] {
            cb(0, QByteArray(), QStringLiteral("Duel: the browser page is still loading."));
        });
        return;
    }

    const QString url = QStringLiteral("https://%1/").arg(m_site) + path;
    const QString js = QStringLiteral(
        "(typeof window.__xbotDuel === 'function')"
        " ? window.__xbotDuel(%1, %2, %3, %4, %5)"
        " : Promise.resolve({status: 0, text: 'page not ready (helper missing)'})")
        .arg(jsString(method), jsString(url), jsString(QString::fromUtf8(body)), jsString(uuid),
             balancesOnly ? QStringLiteral("true") : QStringLiteral("false"));

    QJsonObject args;
    args.insert(QStringLiteral("expression"), js);
    args.insert(QStringLiteral("awaitPromise"), true);
    args.insert(QStringLiteral("returnByValue"), true);
    const std::wstring argsW =
        QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact)).toStdWString();

    QPointer<DuelBrowser> self(this);
    const HRESULT hr = m_webview->CallDevToolsProtocolMethod(
        L"Runtime.evaluate", argsW.c_str(),
        Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler>(
            [self, cb](HRESULT result, LPCWSTR jsonResult) -> HRESULT {
                if (!self)
                    return S_OK;
                if (FAILED(result) || !jsonResult) {
                    cb(0, QByteArray(), QStringLiteral("Duel: the browser call failed."));
                    return S_OK;
                }
                const QJsonObject outer =
                    QJsonDocument::fromJson(QString::fromWCharArray(jsonResult).toUtf8()).object();
                const QJsonObject v = outer.value(QStringLiteral("result")).toObject()
                                          .value(QStringLiteral("value")).toObject();
                if (v.isEmpty()) {
                    cb(0, QByteArray(), QStringLiteral("Duel: invalid browser answer."));
                    return S_OK;
                }
                const int status = v.value(QStringLiteral("status")).toInt();
                const QByteArray text = v.value(QStringLiteral("text")).toString().toUtf8();
                cb(status, text, status == 0 ? QString::fromUtf8(text) : QString());
                return S_OK;
            }).Get());
    if (FAILED(hr))
        QTimer::singleShot(0, this, [cb] {
            cb(0, QByteArray(), QStringLiteral("Duel: the browser call could not be started."));
        });
}
