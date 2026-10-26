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

bool GoatedTransport::isReady(QString *whyNot) const
{
    if (m_cred.token.isEmpty()) {
        if (whyNot)
            *whyNot = tr("Goated: the token is empty (log in or paste g3_token in Options).");
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

// Shared by post() and requestBalances(): turns the HTTP answer into a TransportReply.
void GoatedTransport::handleReply(QNetworkReply *reply, TransportCallback cb)
{
    connect(reply, &QNetworkReply::finished, this, [this, reply, cb] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray data = reply->readAll();

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
        } else if (status == 401) {
            cb(failure(tr("HTTP 401: unauthorized — the token was rejected or has expired."), status));
        } else if (status == 403 || status == 503) {
            cb(failure(tr("HTTP %1: blocked (Cloudflare or the token was refused). Direct mode "
                          "has no browser session to pass a challenge.")
                           .arg(status),
                       status));
        } else if (status > 0) {
            cb(failure(tr("HTTP %1: %2")
                           .arg(status)
                           .arg(QString::fromUtf8(data.left(300)).simplified()),
                       status));
        } else {
            cb(failure(reply->errorString(), 0));
        }
    });
}

void GoatedTransport::post(const QString &path, const QJsonObject &body, TransportCallback cb)
{
    QString why;
    if (!isReady(&why)) {
        QTimer::singleShot(0, this, [cb, why] { cb(failure(why)); });
        return;
    }
    QNetworkReply *reply = m_nam.post(makeRequest(path, true),
                                      QJsonDocument(body).toJson(QJsonDocument::Compact));
    handleReply(reply, std::move(cb));
}

void GoatedTransport::requestBalances()
{
    QString why;
    if (!isReady(&why)) {
        QTimer::singleShot(0, this, [this, why] { emit balancesFailed(why); });
        return;
    }

    const quint32 generation = m_generation;
    QNetworkReply *reply = m_nam.get(makeRequest(QStringLiteral("user/wallet/balances"), false));
    handleReply(reply, [this, generation](const TransportReply &r) {
        if (generation != m_generation)
            return; // credentials changed meanwhile: stale answer
        if (!r.ok) {
            emit balancesFailed(r.error);
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
