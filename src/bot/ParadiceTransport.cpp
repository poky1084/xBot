#include "bot/ParadiceTransport.h"

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

QString graphQlError(const QJsonObject &o)
{
    const QJsonArray errs = o.value(QStringLiteral("errors")).toArray();
    if (!errs.isEmpty()) {
        const QJsonValue first = errs.first();
        const QString m = first.isString() ? first.toString()
                                           : first.toObject().value(QStringLiteral("message")).toString();
        if (!m.isEmpty())
            return m;
    }
    const QJsonValue err = o.value(QStringLiteral("error"));
    if (err.isString())
        return err.toString();
    return o.value(QStringLiteral("message")).toString();
}
} // namespace

ParadiceTransport::ParadiceTransport(QObject *parent)
    : IStakeTransport(parent)
{
}

QString ParadiceTransport::cleanToken(const QString &raw)
{
    QString t = raw.trimmed();
    if (t.startsWith(QLatin1String("x-access-token"), Qt::CaseInsensitive)) {
        t = t.mid(14).trimmed();
        if (t.startsWith(QLatin1Char(':')) || t.startsWith(QLatin1Char('=')))
            t = t.mid(1).trimmed();
    }
    if (t.size() >= 2 && ((t.startsWith(QLatin1Char('"')) && t.endsWith(QLatin1Char('"')))
                          || (t.startsWith(QLatin1Char('\'')) && t.endsWith(QLatin1Char('\'')))))
        t = t.mid(1, t.size() - 2).trimmed();
    if (t.startsWith(QLatin1String("Bearer "), Qt::CaseInsensitive))
        t = t.mid(7).trimmed();
    if (t.compare(QLatin1String("null"), Qt::CaseInsensitive) == 0
        || t.compare(QLatin1String("undefined"), Qt::CaseInsensitive) == 0)
        t.clear();
    return t;
}

void ParadiceTransport::setCredentials(const ParadiceCredentials &cred)
{
    ParadiceCredentials c = cred;
    c.token = cleanToken(c.token);
    const bool changed = c.token != m_cred.token;
    m_cred = c;
    if (changed) {
        ++m_generation;
        emit readyChanged(isReady());
    }
}

void ParadiceTransport::setExtension(ExtensionTransport *ext)
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

bool ParadiceTransport::isReady(QString *whyNot) const
{
    if (m_cred.token.isEmpty()) {
        if (whyNot)
            *whyNot = tr("Paradice: the token is empty (log in or paste localStorage \"token\" in Options).");
        return false;
    }
    if (m_ext && !m_ext->isConnectedTo(pageHosts())) {
        if (whyNot)
            *whyNot = tr("Paradice: no paradice.in tab with the browser extension is open.");
        return false;
    }
    return true;
}

QNetworkRequest ParadiceTransport::makeRequest() const
{
    QNetworkRequest req{QUrl(apiUrl())};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("Accept", "*/*");
    req.setRawHeader("Accept-Language", "EN");
    req.setRawHeader("x-access-token", m_cred.token.toUtf8());
    req.setRawHeader("Origin", "https://paradice.in");
    req.setRawHeader("Referer", "https://paradice.in/");
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  m_cred.userAgent.isEmpty() ? defaultUserAgent() : m_cred.userAgent);
    req.setTransferTimeout(kRequestTimeoutMs);
    return req;
}

TransportReply ParadiceTransport::interpret(int status, const QByteArray &data, const QString &netError) const
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

void ParadiceTransport::post(const QString &path, const QJsonObject &body, TransportCallback cb)
{
    Q_UNUSED(path); // single GraphQL endpoint
    QString why;
    if (!isReady(&why)) {
        QTimer::singleShot(0, this, [cb, why] { cb(failure(why)); });
        return;
    }
    sendHttp(this, m_nam, m_ext, pageHosts(), "POST", makeRequest(),
             QJsonDocument(body).toJson(QJsonDocument::Compact),
             [this, cb](int status, const QByteArray &data, const QString &netError) {
        cb(interpret(status, data, netError));
    });
}

QString ParadiceTransport::balancesQuery()
{
    return QStringLiteral("{\n  me {\n    id\n    login\n    wallets {\n      balance\n"
                          "      currency\n      __typename\n    }\n    __typename\n  }\n}\n");
}

QMap<QString, double> ParadiceTransport::parseWallets(const QJsonObject &json, bool *ok)
{
    QMap<QString, double> balances;
    const QJsonValue wallets = json.value(QStringLiteral("data")).toObject()
                                   .value(QStringLiteral("me")).toObject()
                                   .value(QStringLiteral("wallets"));
    if (ok)
        *ok = wallets.isArray();
    for (const QJsonValue &v : wallets.toArray()) {
        const QJsonObject w = v.toObject();
        const QString cur = w.value(QStringLiteral("currency")).toString().trimmed().toLower();
        if (!cur.isEmpty())
            balances.insert(cur, numberOf(w.value(QStringLiteral("balance"))));
    }
    return balances;
}

void ParadiceTransport::requestBalances()
{
    QString why;
    if (!isReady(&why)) {
        QTimer::singleShot(0, this, [this, why] { emit balancesFailed(why); });
        return;
    }

    QJsonObject body;
    body.insert(QStringLiteral("variables"), QJsonObject());
    body.insert(QStringLiteral("query"), balancesQuery());

    const quint32 generation = m_generation;
    post(QString(), body, [this, generation](const TransportReply &r) {
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
        bool ok = false;
        const QMap<QString, double> balances = parseWallets(r.json, &ok);
        if (!ok) {
            QString msg = graphQlError(r.json);
            if (msg.isEmpty())
                msg = tr("Login failed: unexpected response (is the token valid?)");
            emit balancesFailed(msg);
            return;
        }
        emit balancesUpdated(balances);
    });
}
