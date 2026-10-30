#include "bot/TowerTransport.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <algorithm>

namespace
{
constexpr int kRequestTimeoutMs = 15000;

double numberOf(const QJsonValue &v)
{
    if (v.isDouble())
        return v.toDouble();
    if (v.isString())
        return v.toString().toDouble();
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

QString stripQuotes(QString t)
{
    t = t.trimmed();
    if (t.size() >= 2 && ((t.startsWith(QLatin1Char('"')) && t.endsWith(QLatin1Char('"')))
                          || (t.startsWith(QLatin1Char('\'')) && t.endsWith(QLatin1Char('\'')))))
        t = t.mid(1, t.size() - 2).trimmed();
    return t;
}

// "name=value; other=…" or "name: value" -> value (when the copied text starts with `name`).
QString stripName(QString t, const QString &name)
{
    t = t.trimmed();
    if (t.startsWith(name, Qt::CaseInsensitive) && t.size() > name.size()
        && (t.at(name.size()) == QLatin1Char('=') || t.at(name.size()) == QLatin1Char(':'))) {
        t = t.mid(name.size() + 1).trimmed();
        const int semi = t.indexOf(QLatin1Char(';'));
        if (semi >= 0)
            t = t.left(semi).trimmed();
    }
    return t;
}
} // namespace

TowerTransport::TowerTransport(QObject *parent)
    : IStakeTransport(parent)
{
}

QString TowerTransport::defaultUserAgent()
{
    return QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                          "(KHTML, like Gecko) Chrome/156.0.0.0 Safari/537.36");
}

QString TowerTransport::cleanToken(const QString &raw)
{
    QString t = stripQuotes(raw);
    if (t.startsWith(QLatin1String("authorization"), Qt::CaseInsensitive))
        t = stripName(t, QStringLiteral("authorization"));
    t = stripName(t, QStringLiteral("__tt"));
    t = stripQuotes(t);
    if (t.startsWith(QLatin1String("Bearer "), Qt::CaseInsensitive))
        t = t.mid(7).trimmed();
    if (t.compare(QLatin1String("null"), Qt::CaseInsensitive) == 0
        || t.compare(QLatin1String("undefined"), Qt::CaseInsensitive) == 0)
        t.clear();
    return t;
}

QString TowerTransport::cleanClearance(const QString &raw)
{
    return stripQuotes(stripName(stripQuotes(raw), QStringLiteral("cf_clearance")));
}

void TowerTransport::setCredentials(const TowerCredentials &cred)
{
    TowerCredentials c = cred;
    c.token = cleanToken(c.token);
    c.clearance = cleanClearance(c.clearance);
    c.userAgent = c.userAgent.trimmed();
    const bool changed = c.token != m_cred.token || c.clearance != m_cred.clearance
                         || c.userAgent != m_cred.userAgent;
    m_cred = c;
    if (changed) {
        ++m_generation;
        emit readyChanged(isReady());
    }
}

void TowerTransport::setExtension(ExtensionTransport *ext)
{
    if (m_ext == ext)
        return;
    if (m_ext)
        disconnect(m_ext, nullptr, this, nullptr);
    m_ext = ext;
    if (m_ext)
        connect(m_ext, &IStakeTransport::readyChanged, this, [this] { emit readyChanged(isReady()); });
    ++m_generation; // answers of the other path are stale
    emit readyChanged(isReady());
}

bool TowerTransport::isReady(QString *whyNot) const
{
    if (m_cred.token.isEmpty()) {
        if (whyNot)
            *whyNot = tr("Tower: the token is empty (log in or paste the \"__tt\" cookie in Options).");
        return false;
    }
    if (m_ext && !m_ext->isConnectedTo(pageHosts())) {
        if (whyNot)
            *whyNot = tr("Tower: no tower.bet tab with the browser extension is open.");
        return false;
    }
    return true;
}

QNetworkRequest TowerTransport::buildRequest(const QString &path, const QString &token,
                                             const QString &clearance, const QString &userAgent)
{
    QString p = path.trimmed();
    while (p.startsWith(QLatin1Char('/')))
        p.remove(0, 1);
    QNetworkRequest req{QUrl(apiBase() + QLatin1Char('/') + p)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("Accept-Language", "en-US,en;q=0.9");
    if (!token.isEmpty())
        req.setRawHeader("Authorization", QByteArray("Bearer ") + token.toUtf8());
    // Same cookies the site has: the session token and (when Cloudflare challenged) cf_clearance.
    QByteArray cookie = "NEXT_LOCALE=en";
    if (!token.isEmpty())
        cookie += "; __tt=" + token.toUtf8();
    if (!clearance.isEmpty())
        cookie += "; cf_clearance=" + clearance.toUtf8();
    req.setRawHeader("Cookie", cookie);
    req.setRawHeader("Origin", "https://tower.bet");
    req.setRawHeader("Referer", "https://tower.bet/dice");
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  userAgent.trimmed().isEmpty() ? defaultUserAgent() : userAgent.trimmed());
    req.setTransferTimeout(kRequestTimeoutMs);
    return req;
}

QString TowerTransport::errorText(int status, const QByteArray &data)
{
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isObject()) {
        const QJsonObject o = doc.object();
        QString msg = o.value(QStringLiteral("message")).toString().trimmed();
        // Laravel validation: {"errors":{"amount":["…"]}} -> the first text.
        const QJsonObject errs = o.value(QStringLiteral("errors")).toObject();
        for (auto it = errs.constBegin(); it != errs.constEnd(); ++it) {
            const QJsonValue v = it.value();
            const QJsonArray list = v.toArray();
            const QString first = v.isArray() ? (list.isEmpty() ? QString() : list.at(0).toString())
                                              : v.toString();
            if (!first.isEmpty()) {
                if (msg.isEmpty() || msg.contains(QLatin1String("given data was invalid"), Qt::CaseInsensitive))
                    msg = first;
                break;
            }
        }
        if (msg.isEmpty())
            msg = o.value(QStringLiteral("error")).toString().trimmed();
        if (!msg.isEmpty())
            return status > 0 ? QStringLiteral("HTTP %1: %2").arg(status).arg(msg) : msg;
    }
    const QByteArray lower = data.left(4000).toLower();
    if ((status == 403 || status == 503)
        && (lower.contains("cloudflare") || lower.contains("challenge") || lower.contains("cf-"))) {
        return tr("HTTP %1: blocked by Cloudflare — cf_clearance is missing or expired, or the "
                  "User-Agent does not match the browser that got it. Use \"Open Tower & Log in\" "
                  "again or the browser extension.").arg(status);
    }
    if (status == 401)
        return tr("HTTP 401: unauthorized — the token (\"__tt\") was rejected or has expired.");
    if (status == 429)
        return tr("HTTP 429: Too Many Requests (slow down)");
    if (status > 0)
        return tr("HTTP %1: %2").arg(status).arg(QString::fromUtf8(data.left(300)).simplified());
    return QString();
}

TransportReply TowerTransport::interpret(int status, const QByteArray &data, const QString &netError) const
{
    if (status >= 200 && status < 300) {
        const QJsonDocument doc = QJsonDocument::fromJson(data);
        TransportReply r;
        r.ok = true;
        r.status = status;
        if (doc.isObject())
            r.json = doc.object();
        return r;
    }
    // Login / Cloudflare problems are not API errors: fail the request.
    if (status == 401 || status == 403 || status == 503)
        return failure(errorText(status, data), status);
    if (status > 0) {
        // The server refused (rate limit, validation, balance, …): reported as an API error in
        // "errors" so the callers' retry rules ("429", "slow down", "too many attempts") apply.
        TransportReply r;
        r.ok = true;
        r.status = status;
        QJsonObject err;
        err.insert(QStringLiteral("message"), errorText(status, data));
        r.json.insert(QStringLiteral("errors"), QJsonArray{err});
        return r;
    }
    return failure(netError.isEmpty() ? tr("Network error.") : netError, 0);
}

void TowerTransport::post(const QString &path, const QJsonObject &body, TransportCallback cb)
{
    QString why;
    if (!isReady(&why)) {
        QTimer::singleShot(0, this, [cb, why] { cb(failure(why)); });
        return;
    }
    sendHttp(this, m_nam, m_ext, pageHosts(), "POST",
             buildRequest(path, m_cred.token, m_cred.clearance, m_cred.userAgent),
             QJsonDocument(body).toJson(QJsonDocument::Compact),
             [this, cb](int status, const QByteArray &data, const QString &netError) {
        cb(interpret(status, data, netError));
    });
}

QMap<QString, double> TowerTransport::parseAccounts(const QByteArray &body, bool *ok)
{
    QMap<QString, double> balances;
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    // Usually a bare array; tolerate {"data":[…]} as well.
    QJsonArray arr = doc.array();
    if (doc.isObject())
        arr = doc.object().value(QStringLiteral("data")).toArray();
    if (ok)
        *ok = doc.isArray() || (doc.isObject() && doc.object().value(QStringLiteral("data")).isArray());
    for (const QJsonValue &v : std::as_const(arr)) {
        const QJsonObject a = v.toObject();
        const QString type = a.value(QStringLiteral("type")).toString();
        if (!type.isEmpty() && type.compare(QLatin1String("main"), Qt::CaseInsensitive) != 0)
            continue; // only the main (betting) wallet
        const QString cur = a.value(QStringLiteral("symbol")).toString().trimmed().toLower();
        if (!cur.isEmpty())
            balances.insert(cur, balances.value(cur) + numberOf(a.value(QStringLiteral("balance"))));
    }
    return balances;
}

QStringList TowerTransport::parseCurrencies(const QByteArray &body)
{
    struct Entry { int order; QString code; };
    QList<Entry> entries;
    const QJsonArray arr = QJsonDocument::fromJson(body).array();
    for (const QJsonValue &v : arr) {
        const QJsonObject c = v.toObject();
        const QString code = c.value(QStringLiteral("symbol")).toString().trimmed().toLower();
        if (code.isEmpty())
            continue;
        entries.append({c.value(QStringLiteral("order")).toInt(1000), code});
    }
    std::stable_sort(entries.begin(), entries.end(),
                     [](const Entry &a, const Entry &b) { return a.order < b.order; });
    QStringList out;
    for (const Entry &e : std::as_const(entries)) {
        if (!out.contains(e.code))
            out << e.code;
    }
    return out;
}

QStringList TowerTransport::defaultCurrencies()
{
    // /api/currencies, in the site's order.
    return {QStringLiteral("usdt"), QStringLiteral("decoy"), QStringLiteral("sol"),
            QStringLiteral("btc"),  QStringLiteral("ltc"),   QStringLiteral("usdc"),
            QStringLiteral("eth"),  QStringLiteral("doge"),  QStringLiteral("trx"),
            QStringLiteral("xrp"),  QStringLiteral("xlm"),   QStringLiteral("pol"),
            QStringLiteral("bnb"),  QStringLiteral("cad"),   QStringLiteral("xmr"),
            QStringLiteral("shib"), QStringLiteral("zec"),   QStringLiteral("bch"),
            QStringLiteral("avax"), QStringLiteral("dash"),  QStringLiteral("trump"),
            QStringLiteral("pepe"), QStringLiteral("rvn"),   QStringLiteral("bttc"),
            QStringLiteral("link"), QStringLiteral("pump"),  QStringLiteral("etc"),
            QStringLiteral("near"), QStringLiteral("uni"),   QStringLiteral("wbtc"),
            QStringLiteral("usd1"), QStringLiteral("ada"),   QStringLiteral("fdusd"),
            QStringLiteral("tusd"), QStringLiteral("ena"),   QStringLiteral("dot"),
            QStringLiteral("xaut"), QStringLiteral("atom"),  QStringLiteral("aave"),
            QStringLiteral("zen"),  QStringLiteral("inr"),   QStringLiteral("bdt"),
            QStringLiteral("uzs"),  QStringLiteral("azn"),   QStringLiteral("kzt"),
            QStringLiteral("kgs"),  QStringLiteral("twg")};
}

void TowerTransport::requestBalances()
{
    QString why;
    if (!isReady(&why)) {
        QTimer::singleShot(0, this, [this, why] { emit balancesFailed(why); });
        return;
    }

    const quint32 generation = m_generation;
    sendHttp(this, m_nam, m_ext, pageHosts(), "GET",
             buildRequest(QStringLiteral("accounts"), m_cred.token, m_cred.clearance, m_cred.userAgent),
             QByteArray(),
             [this, generation](int status, const QByteArray &data, const QString &netError) {
        if (generation != m_generation)
            return; // credentials changed meanwhile: stale answer
        if (status < 200 || status >= 300) {
            QString msg = errorText(status, data);
            if (msg.isEmpty())
                msg = netError.isEmpty() ? tr("Network error.") : netError;
            emit balancesFailed(msg);
            return;
        }
        bool ok = false;
        const QMap<QString, double> balances = parseAccounts(data, &ok);
        if (!ok) {
            emit balancesFailed(tr("Login failed: unexpected response (is the token valid?) %1")
                                    .arg(QString::fromUtf8(data.left(200)).simplified()));
            return;
        }
        emit balancesUpdated(balances);
    });
}
