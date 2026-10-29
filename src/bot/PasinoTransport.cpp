#include "bot/PasinoTransport.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

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

QString defaultUserAgent()
{
    return QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                          "(KHTML, like Gecko) Chrome/156.0.0.0 Safari/537.36");
}

QJsonObject rateLimitAnswer()
{
    QJsonObject json;
    json.insert(QStringLiteral("success"), false);
    json.insert(QStringLiteral("message"), QStringLiteral("HTTP 429: slow down"));
    return json;
}
} // namespace

PasinoTransport::PasinoTransport(QObject *parent)
    : IStakeTransport(parent)
{
}

QString PasinoTransport::cleanToken(const QString &raw)
{
    QString t = raw.trimmed();
    // A whole copied request body: {"token":"…","language":"en"}
    if (t.startsWith(QLatin1Char('{'))) {
        const QJsonObject o = QJsonDocument::fromJson(t.toUtf8()).object();
        if (o.contains(QStringLiteral("token")))
            t = o.value(QStringLiteral("token")).toString().trimmed();
    }
    if (t.startsWith(QLatin1String("token"), Qt::CaseInsensitive)
        && t.size() > 5 && (t.at(5) == QLatin1Char(':') || t.at(5) == QLatin1Char('='))) {
        t = t.mid(6).trimmed();
    }
    if (t.size() >= 2 && ((t.startsWith(QLatin1Char('"')) && t.endsWith(QLatin1Char('"')))
                          || (t.startsWith(QLatin1Char('\'')) && t.endsWith(QLatin1Char('\'')))))
        t = t.mid(1, t.size() - 2).trimmed();
    if (t.startsWith(QLatin1String("Bearer "), Qt::CaseInsensitive))
        t = t.mid(7).trimmed();
    if (t.compare(QLatin1String("null"), Qt::CaseInsensitive) == 0
        || t.compare(QLatin1String("undefined"), Qt::CaseInsensitive) == 0
        || t.compare(QLatin1String("false"), Qt::CaseInsensitive) == 0)
        t.clear();
    return t;
}

QByteArray PasinoTransport::makeBody(const QString &token, QJsonObject body)
{
    // Same key order as the site (token, language first) is not required by JSON, but the
    // values are: the token and "en".
    body.insert(QStringLiteral("token"), token);
    if (!body.contains(QStringLiteral("language")))
        body.insert(QStringLiteral("language"), QStringLiteral("en"));
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

void PasinoTransport::setCredentials(const PasinoCredentials &cred)
{
    PasinoCredentials c = cred;
    c.token = cleanToken(c.token);
    const bool changed = c.token != m_cred.token;
    m_cred = c;
    if (changed) {
        ++m_generation;
        emit readyChanged(isReady());
    }
}

void PasinoTransport::setExtension(ExtensionTransport *ext)
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

bool PasinoTransport::isReady(QString *whyNot) const
{
    if (m_cred.token.isEmpty()) {
        if (whyNot)
            *whyNot = tr("Pasino: the token is empty (log in or paste localStorage \"token\" in Options).");
        return false;
    }
    if (m_ext && !m_ext->isConnectedTo(pageHosts())) {
        if (whyNot)
            *whyNot = tr("Pasino: no pasino.com tab with the browser extension is open.");
        return false;
    }
    return true;
}

QNetworkRequest PasinoTransport::makeRequest(const QString &path) const
{
    QString p = path.trimmed();
    while (p.startsWith(QLatin1Char('/')))
        p.remove(0, 1);
    QNetworkRequest req{QUrl(apiBase() + QLatin1Char('/') + p)};
    // The site posts its JSON with a form content type (a "simple" request: no CORS preflight).
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/x-www-form-urlencoded"));
    // Only CORS-safelisted headers: through the browser extension the request runs in the
    // pasino.com tab, and any other header (Cache-Control, Pragma, ...) would make the browser
    // send an OPTIONS preflight, which api.pasino.com does not answer ("Failed to fetch").
    req.setRawHeader("Accept", "*/*");
    req.setRawHeader("Accept-Language", "en");
    req.setRawHeader("Origin", "https://pasino.com");
    req.setRawHeader("Referer", "https://pasino.com/");
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  m_cred.userAgent.isEmpty() ? defaultUserAgent() : m_cred.userAgent);
    req.setTransferTimeout(kRequestTimeoutMs);
    return req;
}

TransportReply PasinoTransport::interpret(int status, const QByteArray &data, const QString &netError) const
{
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (status == 429) {
        TransportReply r;
        r.ok = true;
        r.status = status;
        r.json = doc.isObject() ? doc.object() : QJsonObject();
        // Report it like an API error so the engine's "slow down" retry applies.
        const QJsonObject rl = rateLimitAnswer();
        r.json.insert(QStringLiteral("success"), false);
        if (r.json.value(QStringLiteral("message")).toString().isEmpty())
            r.json.insert(QStringLiteral("message"), rl.value(QStringLiteral("message")));
        else
            r.json.insert(QStringLiteral("message"),
                          QStringLiteral("HTTP 429: slow down (%1)")
                              .arg(r.json.value(QStringLiteral("message")).toString()));
        return r;
    }
    if (doc.isObject()) {
        TransportReply r;
        r.ok = true;
        r.status = status;
        r.json = doc.object();
        return r;
    }
    if (status == 401)
        return failure(tr("HTTP 401: unauthorized — the token was rejected or has expired."), status);
    if (status == 403 || status == 503)
        return failure(tr("HTTP %1: blocked (Cloudflare or the token was refused). "
                          "Use the browser extension or log in again.").arg(status),
                       status);
    if (status > 0)
        return failure(tr("HTTP %1: %2").arg(status).arg(QString::fromUtf8(data.left(300)).simplified()),
                       status);
    return failure(netError.isEmpty() ? tr("Network error.") : netError, 0);
}

void PasinoTransport::post(const QString &path, const QJsonObject &body, TransportCallback cb)
{
    QString why;
    if (!isReady(&why)) {
        QTimer::singleShot(0, this, [cb, why] { cb(failure(why)); });
        return;
    }
    sendHttp(this, m_nam, m_ext, pageHosts(), "POST", makeRequest(path), makeBody(m_cred.token, body),
             [this, cb](int status, const QByteArray &data, const QString &netError) {
        cb(interpret(status, data, netError));
    });
}

QString PasinoTransport::errorText(const QJsonObject &json)
{
    const QJsonValue success = json.value(QStringLiteral("success"));
    const bool failed = (success.isBool() && !success.toBool())
                        || (success.isDouble() && success.toInt() == 0);
    if (!failed)
        return QString();
    QString msg = json.value(QStringLiteral("message")).toString().trimmed();
    if (msg.isEmpty())
        msg = json.value(QStringLiteral("error")).toString().trimmed();
    return msg.isEmpty() ? QStringLiteral("Pasino: request refused") : msg;
}

QMap<QString, double> PasinoTransport::parseCoins(const QJsonObject &json, bool *ok)
{
    QMap<QString, double> balances;
    const QJsonValue coins = json.value(QStringLiteral("coins"));
    if (ok)
        *ok = coins.isArray();
    for (const QJsonValue &v : coins.toArray()) {
        const QJsonObject c = v.toObject();
        const QString cur = c.value(QStringLiteral("coin")).toString().trimmed().toLower();
        if (!cur.isEmpty())
            balances.insert(cur, numberOf(c.value(QStringLiteral("balance"))));
    }
    return balances;
}

void PasinoTransport::requestBalances()
{
    QString why;
    if (!isReady(&why)) {
        QTimer::singleShot(0, this, [this, why] { emit balancesFailed(why); });
        return;
    }

    const quint32 generation = m_generation;
    post(balancesPath(), QJsonObject(), [this, generation](const TransportReply &r) {
        if (generation != m_generation)
            return; // credentials changed meanwhile: stale answer
        if (!r.ok) {
            emit balancesFailed(r.error);
            return;
        }
        if (r.status == 429) {
            emit balancesFailed(tr("HTTP 429: Too Many Requests"));
            return;
        }
        const QString err = errorText(r.json);
        bool ok = false;
        const QMap<QString, double> balances = parseCoins(r.json, &ok);
        if (!ok || !err.isEmpty()) {
            QString msg = err;
            if (msg.isEmpty())
                msg = tr("Login failed: unexpected response (is the token valid?)");
            emit balancesFailed(msg);
            return;
        }
        emit balancesUpdated(balances);
    });
}
