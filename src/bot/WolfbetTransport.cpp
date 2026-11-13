#include "bot/WolfbetTransport.h"

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
                          "(KHTML, like Gecko) Chrome/155.0.0.0 Safari/537.36");
}

QString errorMessageOf(const QJsonObject &o)
{
    const QJsonValue err = o.value(QStringLiteral("error"));
    if (err.isString() && !err.toString().isEmpty())
        return err.toString();
    if (err.isObject()) {
        const QString m = err.toObject().value(QStringLiteral("message")).toString();
        if (!m.isEmpty())
            return m;
    }
    const QJsonArray errs = o.value(QStringLiteral("errors")).toArray();
    if (!errs.isEmpty()) {
        const QJsonValue first = errs.first();
        const QString m = first.isString() ? first.toString()
                                           : first.toObject().value(QStringLiteral("message")).toString();
        if (!m.isEmpty())
            return m;
    }
    return o.value(QStringLiteral("message")).toString();
}
} // namespace

WolfbetTransport::WolfbetTransport(QObject *parent)
    : IStakeTransport(parent)
{
}

QString WolfbetTransport::cleanToken(const QString &raw)
{
    QString t = raw.trimmed();

    // A copied cookie line: "token=...; other=..." -> keep the token cookie's value.
    if (t.startsWith(QLatin1String("token="), Qt::CaseInsensitive))
        t = t.mid(6);
    const int semi = t.indexOf(QLatin1Char(';'));
    if (semi >= 0)
        t = t.left(semi).trimmed();

    // The cookie is URL-encoded JSON: %7B%22value%22:%22<jwt>%22%2C%22expire%22:...%7D
    if (t.contains(QLatin1Char('%')))
        t = QUrl::fromPercentEncoding(t.toUtf8()).trimmed();
    if (t.startsWith(QLatin1Char('{'))) {
        const QJsonDocument doc = QJsonDocument::fromJson(t.toUtf8());
        if (doc.isObject())
            t = doc.object().value(QStringLiteral("value")).toString().trimmed();
    }

    if (t.startsWith(QLatin1String("Bearer "), Qt::CaseInsensitive))
        t = t.mid(7).trimmed();
    if (t.size() >= 2 && ((t.startsWith(QLatin1Char('"')) && t.endsWith(QLatin1Char('"')))
                          || (t.startsWith(QLatin1Char('\'')) && t.endsWith(QLatin1Char('\'')))))
        t = t.mid(1, t.size() - 2).trimmed();
    return t;
}

void WolfbetTransport::setCredentials(const WolfbetCredentials &cred)
{
    WolfbetCredentials c = cred;
    c.token = cleanToken(c.token);
    const bool changed = c.token != m_cred.token;
    m_cred = c;
    if (changed) {
        ++m_generation;
        emit readyChanged(isReady());
    }
}

void WolfbetTransport::setExtension(ExtensionTransport *ext)
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

bool WolfbetTransport::isReady(QString *whyNot) const
{
    if (m_cred.token.isEmpty()) {
        if (whyNot)
            *whyNot = tr("Wolfbet: the token is empty (log in or paste the \"token\" cookie in Options).");
        return false;
    }
    if (m_ext && !m_ext->isConnectedTo(pageHosts())) {
        if (whyNot)
            *whyNot = tr("Wolfbet: no %1 tab with the browser extension is open.").arg(pageHosts().value(0));
        return false;
    }
    return true;
}

QNetworkRequest WolfbetTransport::makeRequest(const QString &path, bool json) const
{
    QNetworkRequest req(QUrl(QStringLiteral("https://%1/%2").arg(host(), path)));
    if (json)
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("Accept", "*/*");
    req.setRawHeader("Authorization", QByteArray("Bearer ") + m_cred.token.toUtf8());
    req.setRawHeader("Origin", QStringLiteral("https://%1").arg(host()).toUtf8());
    req.setRawHeader("Referer", QStringLiteral("https://%1/casino/dice").arg(host()).toUtf8());
    req.setRawHeader("x-client-type", "Web-Application");
    req.setRawHeader("x-requested-with", "XMLHttpRequest");
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  m_cred.userAgent.isEmpty() ? defaultUserAgent() : m_cred.userAgent);
    req.setTransferTimeout(kRequestTimeoutMs);
    return req;
}

// Sends the request straight from the app or through the extension's wolfbet.com tab.
void WolfbetTransport::send(const QByteArray &method, const QString &path, const QByteArray &body,
                            TransportCallback cb)
{
    sendHttp(this, m_nam, m_ext, pageHosts(), method, makeRequest(path, true), body,
             [this, cb](int status, const QByteArray &data, const QString &netError) {
        cb(interpret(status, data, netError));
    });
}

// Shared by post(), get() and requestBalances(): turns the HTTP answer into a TransportReply.
TransportReply WolfbetTransport::interpret(int status, const QByteArray &data, const QString &netError) const
{
    {
        const QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isObject()) {
            TransportReply r;
            r.ok = true;
            r.status = status;
            r.json = doc.object();
            // Rate limit: report it like an API error so the engine's "slow down" retry applies.
            if (status == 429 && !r.json.contains(QStringLiteral("errors"))) {
                QJsonObject err;
                err.insert(QStringLiteral("message"), QStringLiteral("HTTP 429: slow down"));
                r.json.insert(QStringLiteral("errors"), QJsonArray{err});
            }
            return r;
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
            return r;
        } else if (status == 401) {
            return failure(tr("HTTP 401: unauthorized — the token was rejected or has expired."), status);
        } else if (status == 403 || status == 503) {
            return failure(tr("HTTP %1: blocked (Cloudflare or the token was refused). "
                          "Use the browser extension or log in again.")
                           .arg(status),
                       status);
        } else if (status > 0) {
            return failure(tr("HTTP %1: %2")
                           .arg(status)
                           .arg(QString::fromUtf8(data.left(300)).simplified()),
                       status);
        } else {
            return failure(netError.isEmpty() ? tr("Network error.") : netError, 0);
        }
    }
}

void WolfbetTransport::post(const QString &path, const QJsonObject &body, TransportCallback cb)
{
    QString why;
    if (!isReady(&why)) {
        QTimer::singleShot(0, this, [cb, why] { cb(failure(why)); });
        return;
    }
    send("POST", path, QJsonDocument(body).toJson(QJsonDocument::Compact), std::move(cb));
}

void WolfbetTransport::get(const QString &path, TransportCallback cb)
{
    QString why;
    if (!isReady(&why)) {
        QTimer::singleShot(0, this, [cb, why] { cb(failure(why)); });
        return;
    }
    send("GET", path, QByteArray(), std::move(cb));
}

void WolfbetTransport::requestBalances()
{
    QString why;
    if (!isReady(&why)) {
        QTimer::singleShot(0, this, [this, why] { emit balancesFailed(why); });
        return;
    }

    const quint32 generation = m_generation;
    send("GET", QStringLiteral("api/v2/user/balances"), QByteArray(),
         [this, generation](const TransportReply &r) {
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
        const QJsonObject &o = r.json;
        if (!o.value(QStringLiteral("userBalances")).isArray()) {
            QString msg = errorMessageOf(o);
            if (msg.isEmpty())
                msg = tr("Login failed: unexpected response (is the token valid?)");
            emit balancesFailed(msg);
            return;
        }

        // {"userBalances":[{"amount":"6.4111499180","currency":"doge","amount_usd":"0.49"}, ...]}
        QMap<QString, double> balances;
        const QJsonArray arr = o.value(QStringLiteral("userBalances")).toArray();
        for (const QJsonValue &v : arr) {
            const QJsonObject b = v.toObject();
            const QString cur = b.value(QStringLiteral("currency")).toString().toLower();
            if (!cur.isEmpty())
                balances.insert(cur, numberOf(b.value(QStringLiteral("amount"))));
        }
        emit balancesUpdated(balances);
    });
}
