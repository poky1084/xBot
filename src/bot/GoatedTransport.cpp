#include "bot/GoatedTransport.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QTimer>
#include <QUrl>
#include <QUuid>

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

QString randomId(int length)
{
    static const char chars[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    QString out;
    out.reserve(length);
    for (int i = 0; i < length; ++i)
        out += QLatin1Char(chars[QRandomGenerator::global()->bounded(int(sizeof(chars)) - 1)]);
    return out;
}

QString defaultUserAgent()
{
    return QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                          "(KHTML, like Gecko) Chrome/155.0.0.0 Safari/537.36");
}
} // namespace

GoatedTransport::GoatedTransport(QObject *parent)
    : IStakeTransport(parent)
    , m_deviceId(randomId(16))
    , m_sessionId(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
}

QString GoatedTransport::cleanToken(const QString &raw)
{
    QString t = raw.trimmed();
    const int semi = t.indexOf(QLatin1Char(';'));
    if (semi >= 0)
        t = t.left(semi).trimmed();
    if (t.startsWith(QLatin1String("Bearer "), Qt::CaseInsensitive))
        t = t.mid(7).trimmed();
    if (t.size() >= 2 && ((t.startsWith(QLatin1Char('"')) && t.endsWith(QLatin1Char('"')))
                          || (t.startsWith(QLatin1Char('\'')) && t.endsWith(QLatin1Char('\'')))))
        t = t.mid(1, t.size() - 2).trimmed();
    return t;
}

void GoatedTransport::setCredentials(const GoatedCredentials &cred)
{
    GoatedCredentials c = cred;
    c.token = cleanToken(c.token);
    const bool changed = c.token != m_cred.token;
    m_cred = c;
    if (changed) {
        ++m_generation;
        emit readyChanged(isReady());
    }
}

void GoatedTransport::setExtension(ExtensionTransport *ext)
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

bool GoatedTransport::isReady(QString *whyNot) const
{
    if (m_cred.token.isEmpty()) {
        if (whyNot)
            *whyNot = tr("Goated: the token is empty (log in or paste g3_token in Options).");
        return false;
    }
    if (m_ext && !m_ext->isConnectedTo(pageHosts())) {
        if (whyNot)
            *whyNot = tr("Goated: no goated.com tab with the browser extension is open.");
        return false;
    }
    return true;
}

QNetworkRequest GoatedTransport::makeRequest(const QString &path, bool json) const
{
    QNetworkRequest req(QUrl(QStringLiteral("https://%1/%2").arg(apiHost(), path)));
    if (json)
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("Accept", "application/json, text/plain, */*");
    req.setRawHeader("Authorization", QByteArray("Bearer ") + m_cred.token.toUtf8());
    req.setRawHeader("Origin", QStringLiteral("https://%1").arg(webHost()).toUtf8());
    req.setRawHeader("Referer", QStringLiteral("https://%1/").arg(webHost()).toUtf8());
    req.setRawHeader("x-device-id", m_deviceId.toUtf8());
    req.setRawHeader("x-analytics-session-id", m_sessionId.toUtf8());
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  m_cred.userAgent.isEmpty() ? defaultUserAgent() : m_cred.userAgent);
    req.setTransferTimeout(kRequestTimeoutMs);
    return req;
}

// Sends the request straight from the app or through the extension's goated.com tab.
void GoatedTransport::send(const QByteArray &method, const QString &path, const QByteArray &body,
                           bool json, TransportCallback cb)
{
    sendHttp(this, m_nam, m_ext, pageHosts(), method, makeRequest(path, json), body,
             [this, cb](int status, const QByteArray &data, const QString &netError) {
        cb(interpret(status, data, netError));
    });
}

// Shared by post() and requestBalances(): turns the HTTP answer into a TransportReply.
TransportReply GoatedTransport::interpret(int status, const QByteArray &data, const QString &netError) const
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

void GoatedTransport::post(const QString &path, const QJsonObject &body, TransportCallback cb)
{
    QString why;
    if (!isReady(&why)) {
        QTimer::singleShot(0, this, [cb, why] { cb(failure(why)); });
        return;
    }
    send("POST", path, QJsonDocument(body).toJson(QJsonDocument::Compact), true, std::move(cb));
}

void GoatedTransport::requestBalances()
{
    QString why;
    if (!isReady(&why)) {
        QTimer::singleShot(0, this, [this, why] { emit balancesFailed(why); });
        return;
    }

    const quint32 generation = m_generation;
    send("GET", QStringLiteral("user/wallet/balances"), QByteArray(), false,
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
        if (o.contains(QStringLiteral("success")) && !o.value(QStringLiteral("success")).toBool()) {
            QString msg = o.value(QStringLiteral("message")).toString();
            if (msg.isEmpty())
                msg = o.value(QStringLiteral("error")).toString();
            if (msg.isEmpty())
                msg = tr("Login failed: the balance request was refused (is the token valid?)");
            emit balancesFailed(msg);
            return;
        }
        if (!o.value(QStringLiteral("data")).isObject()) {
            emit balancesFailed(tr("Login failed: unexpected response (is the token valid?)"));
            return;
        }

        // {"success":true,"data":{"USDC":0.99324}}
        const QJsonObject data = o.value(QStringLiteral("data")).toObject();
        QMap<QString, double> balances;
        for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
            if (it.value().isDouble() || it.value().isString())
                balances.insert(it.key().toLower(), numberOf(it.value()));
        }
        emit balancesUpdated(balances);
    });
}
