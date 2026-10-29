#include "bot/DuelTransport.h"

#include <QNetworkRequest>
#include <QUrl>

#include "ui/DuelBrowser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include <QUrl>

#include <utility>

namespace
{
// The token lives 600 s; a new one is fetched this long before it runs out.
constexpr int kTokenSafetySecs = 60;

double numberOf(const QJsonValue &v, bool *ok = nullptr)
{
    if (v.isDouble()) {
        if (ok)
            *ok = true;
        return v.toDouble();
    }
    if (v.isString()) {
        bool good = false;
        const double d = v.toString().trimmed().toDouble(&good);
        if (ok)
            *ok = good;
        return good ? d : 0.0;
    }
    if (ok)
        *ok = false;
    return 0.0;
}

TransportReply failure(const QString &why, int status = 0)
{
    TransportReply r;
    r.ok = false;
    r.status = status;
    r.error = why;
    return r;
}

// "message" / "error" (string or object) / "errors" (array, or Laravel's {"field":["msg"]}).
QString errorTextOf(const QJsonObject &o)
{
    const QJsonValue err = o.value(QStringLiteral("error"));
    if (err.isString() && !err.toString().isEmpty())
        return err.toString();
    if (err.isObject()) {
        const QString m = err.toObject().value(QStringLiteral("message")).toString();
        if (!m.isEmpty())
            return m;
    }
    const QString msg = o.value(QStringLiteral("message")).toString();
    if (!msg.isEmpty())
        return msg;
    const QJsonValue errs = o.value(QStringLiteral("errors"));
    if (errs.isArray() && !errs.toArray().isEmpty()) {
        const QJsonValue first = errs.toArray().first();
        return first.isString() ? first.toString()
                                : first.toObject().value(QStringLiteral("message")).toString();
    }
    if (errs.isObject()) {
        const QJsonObject eo = errs.toObject();
        for (auto it = eo.constBegin(); it != eo.constEnd(); ++it) {
            const QJsonValue v = it.value();
            if (v.isString())
                return v.toString();
            if (v.isArray() && !v.toArray().isEmpty())
                return v.toArray().first().toString();
        }
    }
    return {};
}

} // namespace

DuelTransport::DuelTransport(QObject *parent)
    : IStakeTransport(parent)
{
}

const QStringList &DuelTransport::currencyCodes()
{
    // Order of the site's wallet list: 101 = btc, 102 = bch, 103 = eth, ...
    static const QStringList codes = {
        QStringLiteral("btc"),  QStringLiteral("bch"),  QStringLiteral("eth"),
        QStringLiteral("ltc"),  QStringLiteral("usdt"), QStringLiteral("usdc"),
        QStringLiteral("bnb"),  QStringLiteral("trx"),  QStringLiteral("sol"),
        QStringLiteral("xrp"),  QStringLiteral("doge"), QStringLiteral("ada"),
        QStringLiteral("link"), QStringLiteral("avax"), QStringLiteral("gram"),
        QStringLiteral("hbar")};
    return codes;
}

namespace
{
// Currencies seen in metadata answers (id -> lowercase code), e.g. ones added to the site later.
QMap<int, QString> &learnedCodes()
{
    static QMap<int, QString> codes;
    return codes;
}
} // namespace

int DuelTransport::currencyId(const QString &code)
{
    const QString c = code.trimmed().toLower();
    const int idx = currencyCodes().indexOf(c);
    if (idx >= 0)
        return 101 + idx;
    for (auto it = learnedCodes().constBegin(); it != learnedCodes().constEnd(); ++it)
        if (it.value() == c)
            return it.key();
    return 0;
}

QString DuelTransport::currencyCode(int id)
{
    const int idx = id - 101;
    if (idx >= 0 && idx < currencyCodes().size())
        return currencyCodes().at(idx);
    return learnedCodes().value(id);
}

QStringList DuelTransport::allCurrencyCodes()
{
    QMap<int, QString> byId;
    for (int i = 0; i < currencyCodes().size(); ++i)
        byId.insert(101 + i, currencyCodes().at(i));
    for (auto it = learnedCodes().constBegin(); it != learnedCodes().constEnd(); ++it)
        if (!byId.contains(it.key()) && !currencyCodes().contains(it.value()))
            byId.insert(it.key(), it.value());
    return byId.values();
}

QString DuelTransport::metadataPath(const QString &uuid)
{
    return QStringLiteral("api/v2/metadata?uuid=%1")
        .arg(QString::fromLatin1(QUrl::toPercentEncoding(cleanUuid(uuid))));
}

// {"user":{"balances":[{"balance_type":101,"balance":"0.0000","balance_type_name":"BTC",
//   "balance_coins":"0",...},{"balance_type":106,"balance":"1.0156","balance_type_name":"USDC",...}]}}
// "balance" is in the currency itself ("balance_coins" is its USD value).
bool DuelTransport::parseMetadataBalances(const QJsonObject &json, QMap<QString, double> *balances,
                                          QString *error)
{
    const QJsonValue userVal = json.value(QStringLiteral("user"));
    if (!userVal.isObject() || userVal.toObject().isEmpty()) {
        if (error) {
            const QString msg = errorTextOf(json);
            *error = msg.isEmpty() ? tr("Duel: not logged in - the \"duel\" cookie is invalid or "
                                        "expired (log in again in Options).")
                                   : tr("Duel: %1").arg(msg);
        }
        return false;
    }
    const QJsonArray arr = userVal.toObject().value(QStringLiteral("balances")).toArray();
    for (const QJsonValue &v : arr) {
        const QJsonObject b = v.toObject();
        const int id = b.value(QStringLiteral("balance_type")).toVariant().toInt();
        QString code = b.value(QStringLiteral("balance_type_name")).toString().trimmed().toLower();
        if (code.isEmpty())
            code = currencyCode(id);
        if (code.isEmpty())
            continue;
        if (id > 0 && currencyCode(id).isEmpty())
            learnedCodes().insert(id, code);
        if (balances)
            balances->insert(code, numberOf(b.value(QStringLiteral("balance"))));
    }
    return true;
}

QString DuelTransport::cleanCookie(const QString &raw, const QString &name)
{
    QString t = raw.trimmed();
    // A copied cookie line ("a=1; duel=xyz; b=2"): keep the wanted cookie.
    const QString prefix = name + QLatin1Char('=');
    if (t.contains(QLatin1Char(';')) || t.startsWith(prefix, Qt::CaseInsensitive)) {
        const QStringList parts = t.split(QLatin1Char(';'));
        bool picked = false;
        for (const QString &p : parts) {
            const QString part = p.trimmed();
            if (part.startsWith(prefix, Qt::CaseInsensitive)) {
                t = part.mid(prefix.size()).trimmed();
                picked = true;
                break;
            }
        }
        if (!picked)
            t = parts.first().trimmed();
    }
    if (t.size() >= 2 && ((t.startsWith(QLatin1Char('"')) && t.endsWith(QLatin1Char('"')))
                          || (t.startsWith(QLatin1Char('\'')) && t.endsWith(QLatin1Char('\'')))))
        t = t.mid(1, t.size() - 2).trimmed();
    return t;
}

QString DuelTransport::cleanUuid(const QString &raw)
{
    QString t = raw.trimmed();
    if (t.size() >= 2 && ((t.startsWith(QLatin1Char('"')) && t.endsWith(QLatin1Char('"')))
                          || (t.startsWith(QLatin1Char('\'')) && t.endsWith(QLatin1Char('\'')))))
        t = t.mid(1, t.size() - 2).trimmed();
    return t;
}

void DuelTransport::setCredentials(const DuelCredentials &cred)
{
    DuelCredentials c = cred;
    c.token = cleanCookie(c.token, QStringLiteral("duel"));
    c.uuid = cleanUuid(c.uuid);
    c.clearance = cleanCookie(c.clearance, QStringLiteral("cf_clearance"));
    c.userAgent = c.userAgent.trimmed();
    c.site = c.site.trimmed().isEmpty() ? QStringLiteral("duel.com") : c.site.trimmed().toLower();
    const bool changed = c.token != m_cred.token || c.uuid != m_cred.uuid
                         || c.clearance != m_cred.clearance || c.userAgent != m_cred.userAgent
                         || c.site != m_cred.site;
    m_cred = c;
    if (m_browser)
        m_browser->setSessionCookie(m_cred.token);
    if (changed) {
        ++m_generation;
        m_tabUuid.clear();
        m_securityToken.clear();
        m_securityExpiry = QDateTime();
        // Waiters of a token request made with the old credentials get an error.
        const QList<SecurityTokenCallback> waiters = std::exchange(m_tokenWaiters, {});
        m_fetchingToken = false;
        for (const SecurityTokenCallback &w : waiters)
            w(QString(), tr("Duel: the login data changed."), 0);
        emit readyChanged(isReady());
    }
}

void DuelTransport::setExtension(ExtensionTransport *ext)
{
    if (m_ext == ext)
        return;
    if (m_ext)
        disconnect(m_ext, nullptr, this, nullptr);
    m_ext = ext;
    m_tabUuid.clear();
    if (m_ext)
        connect(m_ext, &IStakeTransport::readyChanged, this, [this] {
            if (!m_ext->isConnectedTo(pageHosts()) && !m_tabUuid.isEmpty()) {
                m_tabUuid.clear(); // the next tab may be another session
                m_securityToken.clear();
                m_securityExpiry = QDateTime();
            }
            emit readyChanged(isReady());
        });
    ++m_generation; // answers of the other path are stale
    m_securityToken.clear(); // bound to the session it was fetched with
    m_securityExpiry = QDateTime();
    emit readyChanged(isReady());
}

bool DuelTransport::isReady(QString *whyNot) const
{
    if (m_ext) { // the extension's tab is logged in by itself and has its own UUID
        if (!m_ext->isConnectedTo(pageHosts())) {
            if (whyNot)
                *whyNot = tr("Duel: no %1 tab with the browser extension is open.").arg(m_cred.site);
            return false;
        }
        return true;
    }
    if (m_cred.token.isEmpty()) {
        if (whyNot)
            *whyNot = tr("Duel: the token (\"duel\" cookie) is empty (log in or paste it in Options).");
        return false;
    }
    if (m_cred.uuid.isEmpty()) {
        if (whyNot)
            *whyNot = tr("Duel: the UUID is empty (log in or paste localStorage \"security:uuid\" in Options).");
        return false;
    }
    if (!m_browser) {
        if (whyNot)
            *whyNot = tr("Duel: the browser is not initialized (WebView2 Runtime missing?).");
        return false;
    }
    if (!m_browser->isPageLoaded()) {
        if (whyNot)
            *whyNot = tr("Duel: the browser page is still loading.");
        return false;
    }
    return true;
}

void DuelTransport::setBrowser(DuelBrowser *browser)
{
    if (m_browser == browser)
        return;
    if (m_browser)
        disconnect(m_browser, nullptr, this, nullptr);
    m_browser = browser;
    if (m_browser) {
        m_browser->setSessionCookie(m_cred.token);
        connect(m_browser, &DuelBrowser::pageLoaded, this, [this] { emit readyChanged(isReady()); });
    }
    emit readyChanged(isReady());
}

// Runs the request in the hidden browser and turns the answer into a TransportReply
// (JSON object -> ok, anything else -> failure).
void DuelTransport::send(const QString &method, const QString &path, const QByteArray &body,
                         TransportCallback cb, bool balancesOnly)
{
    QString why;
    if (!isReady(&why)) {
        QTimer::singleShot(0, this, [cb, why] { cb(failure(why)); });
        return;
    }
    auto handle = [cb](int status, const QByteArray &data, const QString &error) {
        if (status == 0) {
            cb(failure(error.isEmpty() ? tr("Duel: network error.") : tr("Duel: %1").arg(error), 0));
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isObject()) {
            TransportReply r;
            r.ok = true;
            r.status = status;
            r.json = doc.object();
            // Rate limit: report it like an API error so the engine's "slow down" retry applies.
            if (status == 429) {
                QJsonObject err;
                err.insert(QStringLiteral("message"), QStringLiteral("HTTP 429: slow down"));
                r.json.insert(QStringLiteral("errors"), QJsonArray{err});
            }
            cb(r);
            return;
        }
        if (status == 429) {
            QJsonObject err;
            err.insert(QStringLiteral("message"), QStringLiteral("HTTP 429: slow down"));
            QJsonObject json;
            json.insert(QStringLiteral("errors"), QJsonArray{err});
            TransportReply r;
            r.ok = true;
            r.status = status;
            r.json = json;
            cb(r);
        } else if (status == 401 || status == 419) {
            cb(failure(tr("HTTP %1: unauthorized — not logged in to Duel (use \"Open Duel.com & Log in\" "
                          "in Options).")
                           .arg(status),
                       status));
        } else if (status == 403 || status == 503) {
            cb(failure(tr("HTTP %1: blocked by Cloudflare — open Duel.com & log in again in Options.")
                           .arg(status),
                       status));
        } else {
            cb(failure(tr("HTTP %1: %2")
                           .arg(status)
                           .arg(QString::fromUtf8(data.left(300)).simplified()),
                       status));
        }
    };

    if (m_ext) {
        // Same request as the hidden browser's fetch(), sent from the extension's Duel tab.
        QNetworkRequest req(QUrl(QStringLiteral("https://%1/").arg(m_cred.site) + path));
        req.setRawHeader("Accept", "application/json, text/plain, */*");
        if (!uuid().isEmpty())
            req.setRawHeader("x-duel-device-identifier", uuid().toUtf8());
        if (!body.isEmpty())
            req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        req.setTransferTimeout(15000);
        m_ext->fetch(pageHosts(), method.toLatin1(), req, body, handle);
        return;
    }
    m_browser->fetch(method, path, body, m_cred.uuid, handle, balancesOnly);
}

void DuelTransport::post(const QString &path, const QJsonObject &body, TransportCallback cb)
{
    send(QStringLiteral("POST"), path, QJsonDocument(body).toJson(QJsonDocument::Compact), std::move(cb));
}

void DuelTransport::get(const QString &path, TransportCallback cb)
{
    send(QStringLiteral("GET"), path, QByteArray(), std::move(cb));
}

// ------------------------------------------------------------------ security token

void DuelTransport::invalidateSecurityToken()
{
    m_securityToken.clear();
    m_securityExpiry = QDateTime();
}

void DuelTransport::withSecurityToken(SecurityTokenCallback cb)
{
    QString why;
    if (!isReady(&why)) {
        QTimer::singleShot(0, this, [cb, why] { cb(QString(), why, 0); });
        return;
    }
    if (!m_securityToken.isEmpty() && m_securityExpiry.isValid()
        && QDateTime::currentDateTimeUtc() < m_securityExpiry) {
        cb(m_securityToken, QString(), 200);
        return;
    }
    m_tokenWaiters << std::move(cb);
    if (!m_fetchingToken)
        fetchSecurityToken();
}

// POST /api/v2/user/security/token {"uuid":"<uuid>","code":"0000","type":"standard"}
// -> {"success":true,"token":"...","token_type":"standard","expires_in":600}
void DuelTransport::fetchSecurityToken()
{
    m_fetchingToken = true;
    const quint32 generation = m_generation;
    withTabUuid([this, generation](const QString &uuidError) {
    if (generation != m_generation)
        return;
    if (!uuidError.isEmpty()) {
        m_fetchingToken = false;
        const QList<SecurityTokenCallback> waiters = std::exchange(m_tokenWaiters, {});
        for (const SecurityTokenCallback &w : waiters)
            w(QString(), uuidError, 0);
        return;
    }

    QJsonObject body;
    body.insert(QStringLiteral("uuid"), uuid());
    body.insert(QStringLiteral("code"), QStringLiteral("0000"));
    body.insert(QStringLiteral("type"), QStringLiteral("standard"));

    post(QStringLiteral("api/v2/user/security/token"), body,
         [this, generation](const TransportReply &r) {
        if (generation != m_generation)
            return; // credentials changed meanwhile: the waiters were already answered
        m_fetchingToken = false;

        QString token;
        QString error;
        if (!r.ok) {
            error = r.error;
        } else {
            token = r.json.value(QStringLiteral("token")).toString().trimmed();
            const bool success = !r.json.contains(QStringLiteral("success"))
                                 || r.json.value(QStringLiteral("success")).toBool();
            if (token.isEmpty() || !success) {
                token.clear();
                error = errorTextOf(r.json);
                if (error.isEmpty())
                    error = tr("the security token request was refused (HTTP %1) - are the token "
                               "and UUID valid?").arg(r.status);
                error = tr("Duel: %1").arg(error);
            }
        }

        if (!token.isEmpty()) {
            int life = r.json.value(QStringLiteral("expires_in")).toInt(600);
            if (life <= 0)
                life = 600;
            m_securityToken = token;
            m_securityExpiry = QDateTime::currentDateTimeUtc().addSecs(
                qMax(10, life - kTokenSafetySecs));
        }

        const QList<SecurityTokenCallback> waiters = std::exchange(m_tokenWaiters, {});
        for (const SecurityTokenCallback &w : waiters)
            w(token, error, r.status);
    });
    });
}

// ------------------------------------------------------------------ balances

// GET /api/v2/metadata?uuid=<uuid>: the site's own startup request; user.balances holds every
// balance the account has (currencies it never used are not listed = 0).
void DuelTransport::requestBalances()
{
    QString why;
    if (!isReady(&why)) {
        QTimer::singleShot(0, this, [this, why] { emit balancesFailed(why); });
        return;
    }

    const quint32 generation = m_generation;
    withTabUuid([this, generation](const QString &uuidError) {
    if (generation != m_generation)
        return;
    if (!uuidError.isEmpty()) {
        emit balancesFailed(uuidError);
        return;
    }
    send(QStringLiteral("GET"), metadataPath(uuid()), QByteArray(), [this, generation](const TransportReply &r) {
        if (generation != m_generation)
            return; // credentials changed meanwhile: stale answer
        if (!r.ok) {
            emit balancesFailed(r.error);
            return;
        }
        if (r.status == 429) { // rate limited: not a login problem
            emit balancesFailed(tr("HTTP 429: Too Many Requests"));
            return;
        }
        QMap<QString, double> balances;
        QString error;
        if (!parseMetadataBalances(r.json, &balances, &error)) {
            if (m_ext) {
                m_tabUuid.clear(); // read again next time (it changes when you log in)
                error = tr("Duel: the %1 tab in your browser is not logged in - log in to Duel "
                           "there (the extension uses that tab's session).")
                            .arg(m_cred.site);
            }
            emit balancesFailed(error);
            return;
        }
        emit balancesUpdated(balances);
    }, true);
    });
}

void DuelTransport::withTabUuid(std::function<void(const QString &error)> next)
{
    if (!m_ext || !m_tabUuid.isEmpty()) {
        next(QString());
        return;
    }
    m_tabUuidWaiters << std::move(next);
    if (m_readingTabUuid)
        return;
    m_readingTabUuid = true;
    const quint32 generation = m_generation;
    m_ext->readStorage(pageHosts(), QStringLiteral("security:uuid"),
                       [this, generation](bool ok, const QString &value, const QString &error) {
        m_readingTabUuid = false;
        QString why;
        if (generation == m_generation && ok)
            m_tabUuid = cleanUuid(value);
        if (!ok)
            why = tr("Duel: %1").arg(error);
        else if (uuid().isEmpty())
            why = tr("Duel: the %1 tab has no UUID yet - open Duel in that tab and log in.")
                      .arg(m_cred.site);
        const auto waiters = std::exchange(m_tabUuidWaiters, {});
        for (const auto &w : waiters)
            w(why);
    });
}
