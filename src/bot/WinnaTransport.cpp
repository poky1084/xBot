#include "bot/WinnaTransport.h"

#include "bot/NoCookieJar.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QUuid>

#include <algorithm>
#include <cmath>

namespace
{
constexpr int kRequestTimeoutMs = 15000;
// A game token is renewed this long before its "exp".
constexpr qint64 kTokenMarginSecs = 300;

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

// "name=value; other=…" -> value (when the copied text starts with `name`).
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

// Payload of a JWT (no signature check: only "exp" / "user_id" are read).
QJsonObject jwtPayload(const QString &jwt)
{
    const QStringList parts = jwt.split(QLatin1Char('.'));
    if (parts.size() < 2)
        return {};
    const QByteArray raw = QByteArray::fromBase64(parts.at(1).toLatin1(),
                                                  QByteArray::Base64UrlEncoding
                                                      | QByteArray::OmitTrailingEquals);
    return QJsonDocument::fromJson(raw).object();
}

// {"response":{"payload":[…]}} / {"payload":[…]} / […]
QJsonArray walletArray(const QByteArray &body, bool *ok)
{
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    QJsonValue v;
    if (doc.isArray()) {
        v = doc.array();
    } else {
        const QJsonObject root = doc.object();
        const QJsonObject resp = root.value(QStringLiteral("response")).toObject();
        v = resp.value(QStringLiteral("payload"));
        if (!v.isArray())
            v = root.value(QStringLiteral("payload"));
    }
    if (ok)
        *ok = v.isArray();
    return v.toArray();
}
} // namespace

// The Cookie header for Direct mode: the session (a bare "connect.sid" value, or the whole cookie
// list the login window captured) plus cf_clearance.
static QByteArray cookieHeader(const WinnaCredentials &cred)
{
    QByteArray cookie = cred.session.contains(QLatin1Char('='))
                            ? cred.session.toUtf8()
                            : "connect.sid=" + cred.session.toUtf8();
    if (!cred.clearance.isEmpty())
        cookie += "; cf_clearance=" + cred.clearance.toUtf8();
    return cookie;
}

WinnaTransport::WinnaTransport(QObject *parent)
    : IStakeTransport(parent)
    , m_sid(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    // Only the cookies from Options are sent (see NoCookieJar.h).
    m_nam.setCookieJar(new NoCookieJar(&m_nam));
}

QString WinnaTransport::defaultUserAgent()
{
    return QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                          "(KHTML, like Gecko) Chrome/156.0.0.0 Safari/537.36");
}

QString WinnaTransport::cleanSession(const QString &raw)
{
    QString t = stripQuotes(raw);
    if (t.startsWith(QLatin1String("cookie:"), Qt::CaseInsensitive))
        t = t.mid(7).trimmed();
    // A whole cookie list ("connect.sid=…; chat.jwt=…; …", what the login window captures or a
    // copied "cookie:" header): kept as it is, without cf_clearance (that has its own field).
    if (t.contains(QLatin1String("connect.sid=")) && t.contains(QLatin1Char(';'))) {
        QStringList keep;
        for (const QString &part : t.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
            const QString c = part.trimmed();
            if (c.isEmpty() || c.startsWith(QLatin1String("cf_clearance=")))
                continue;
            if (c.startsWith(QLatin1String("connect.sid=s:")))
                keep << QStringLiteral("connect.sid=s%3A") + c.mid(14);
            else
                keep << c;
        }
        return keep.join(QStringLiteral("; "));
    }
    t = stripQuotes(stripName(t, QStringLiteral("connect.sid")));
    // DevTools can show the value URL-decoded ("s:…"); the cookie itself is "s%3A…".
    if (t.startsWith(QLatin1String("s:")))
        t = QStringLiteral("s%3A") + t.mid(2);
    if (t.compare(QLatin1String("null"), Qt::CaseInsensitive) == 0)
        t.clear();
    return t;
}

QString WinnaTransport::cleanClearance(const QString &raw)
{
    return stripQuotes(stripName(stripQuotes(raw), QStringLiteral("cf_clearance")));
}

QString WinnaTransport::cleanDeviceId(const QString &raw)
{
    QString t = stripQuotes(raw);
    if (t.startsWith(QLatin1Char('{'))) {
        const QJsonObject o = QJsonDocument::fromJson(t.toUtf8()).object();
        t = o.value(QStringLiteral("id")).toString().trimmed();
    }
    t = stripName(t, QStringLiteral("x-antifraud-device-id"));
    if (t.compare(QLatin1String("null"), Qt::CaseInsensitive) == 0)
        t.clear();
    return t;
}

void WinnaTransport::setCredentials(const WinnaCredentials &cred)
{
    WinnaCredentials c = cred;
    c.session = cleanSession(c.session);
    c.clearance = cleanClearance(c.clearance);
    c.userAgent = c.userAgent.trimmed();
    c.deviceId = cleanDeviceId(c.deviceId);
    const bool changed = c.session != m_cred.session || c.clearance != m_cred.clearance
                         || c.userAgent != m_cred.userAgent || c.deviceId != m_cred.deviceId;
    m_cred = c;
    if (changed) {
        ++m_generation;
        m_gameToken.clear(); // another account / session: start a new game session
        m_gameCoin.clear();
        emit readyChanged(isReady());
    }
}

void WinnaTransport::setExtension(ExtensionTransport *ext)
{
    if (m_ext == ext)
        return;
    if (m_ext)
        disconnect(m_ext, nullptr, this, nullptr);
    m_ext = ext;
    if (m_ext)
        connect(m_ext, &IStakeTransport::readyChanged, this, [this] { emit readyChanged(isReady()); });
    ++m_generation; // answers of the other path are stale
    m_gameToken.clear();
    m_gameCoin.clear();
    emit readyChanged(isReady());
}

void WinnaTransport::setCoin(const QString &code)
{
    m_coin = code.trimmed().toLower();
}

bool WinnaTransport::isReady(QString *whyNot) const
{
    if (m_ext) {
        // The tab sends its own session cookie.
        if (!m_ext->isConnectedTo(pageHosts())) {
            if (whyNot)
                *whyNot = tr("Winna: no winna.com tab with the browser extension is open.");
            return false;
        }
        return true;
    }
    if (m_cred.session.isEmpty()) {
        if (whyNot)
            *whyNot = tr("Winna: the session is empty (log in or paste the \"connect.sid\" cookie in Options).");
        return false;
    }
    return true;
}

QNetworkRequest WinnaTransport::buildApiRequest(const QString &path, const WinnaCredentials &cred,
                                                bool viaExtension)
{
    QString p = path.trimmed();
    while (p.startsWith(QLatin1Char('/')))
        p.remove(0, 1);
    QNetworkRequest req{QUrl(apiBase() + QLatin1Char('/') + p)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("Accept", "application/json, text/plain, */*");
    req.setRawHeader("Accept-Language", "en");
    if (!cred.deviceId.isEmpty())
        req.setRawHeader("x-antifraud-device-id", cred.deviceId.toUtf8());
    if (viaExtension) {
        // The tab's own cookies (api2 is another host than the page): see ExtensionTransport.
        req.setRawHeader("X-Xbot-Credentials", "include");
    } else {
        req.setRawHeader("Cookie", cookieHeader(cred));
    }
    req.setRawHeader("Origin", "https://winna.com");
    req.setRawHeader("Referer", "https://winna.com/");
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  cred.userAgent.isEmpty() ? defaultUserAgent() : cred.userAgent);
    req.setTransferTimeout(kRequestTimeoutMs);
    manualCookies(req); // also when sent through a shared QNetworkAccessManager (currency list)
    return req;
}

QString WinnaTransport::errorText(int status, const QByteArray &data)
{
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isObject()) {
        const QJsonObject o = doc.object();
        QString msg;
        for (const QString &key : {QStringLiteral("message"), QStringLiteral("error"),
                                   QStringLiteral("msg"), QStringLiteral("code")}) {
            const QJsonValue v = o.value(key);
            if (v.isString() && !v.toString().trimmed().isEmpty()) {
                msg = v.toString().trimmed();
                break;
            }
            if (v.isObject()) {
                msg = v.toObject().value(QStringLiteral("message")).toString().trimmed();
                if (!msg.isEmpty())
                    break;
            }
        }
        if (msg.isEmpty()) {
            const QJsonObject resp = o.value(QStringLiteral("response")).toObject();
            msg = resp.value(QStringLiteral("message")).toString().trimmed();
        }
        if (!msg.isEmpty())
            return status > 0 ? QStringLiteral("HTTP %1: %2").arg(status).arg(msg) : msg;
    }
    const QByteArray lower = data.left(4000).toLower();
    if ((status == 403 || status == 503)
        && (lower.contains("cloudflare") || lower.contains("challenge") || lower.contains("cf-"))) {
        return tr("HTTP %1: blocked by Cloudflare — cf_clearance is missing or expired, or the "
                  "User-Agent does not match the browser that got it. Use \"Open Winna & Log in\" "
                  "again or the browser extension.").arg(status);
    }
    if (status == 401)
        return tr("HTTP 401: unauthorized — the session (\"connect.sid\") was rejected or has expired.");
    if (status == 429)
        return tr("HTTP 429: Too Many Requests (slow down)");
    if (status > 0)
        return tr("HTTP %1: %2").arg(status).arg(QString::fromUtf8(data.left(300)).simplified());
    return QString();
}

void WinnaTransport::sendApi(const QByteArray &method, const QString &path, const QJsonObject &body,
                             RawDone done)
{
    sendHttp(this, m_nam, m_ext, pageHosts(), method, buildApiRequest(path, m_cred, m_ext != nullptr),
             method == "GET" ? QByteArray() : QJsonDocument(body).toJson(QJsonDocument::Compact),
             std::move(done));
}

QMap<QString, double> WinnaTransport::parseWallets(const QByteArray &body, bool *ok)
{
    QMap<QString, double> main;   // non-vault wallets
    QMap<QString, double> other;  // wallets without the "is_vault" flag
    for (const QJsonValue &v : walletArray(body, ok)) {
        const QJsonObject w = v.toObject();
        const QString cur = w.value(QStringLiteral("coin")).toString().trimmed().toLower();
        if (cur.isEmpty())
            continue;
        // "balance" is in 1e-8 units ("500000000" = 5); "origin_amount" is the same in coins.
        double amount = 0.0;
        if (w.contains(QStringLiteral("origin_amount")))
            amount = numberOf(w.value(QStringLiteral("origin_amount")));
        else
            amount = numberOf(w.value(QStringLiteral("balance"))) / 1e8;
        const QJsonValue vault = w.value(QStringLiteral("is_vault"));
        if (vault.isBool()) {
            if (vault.toBool())
                continue; // the vault is not the betting balance
            main.insert(cur, main.value(cur) + amount);
        } else {
            // No flag: of the duplicates, the one with a balance counts.
            other.insert(cur, std::max(other.value(cur), amount));
        }
    }
    for (auto it = other.constBegin(); it != other.constEnd(); ++it) {
        if (!main.contains(it.key()))
            main.insert(it.key(), it.value());
    }
    return main;
}

QStringList WinnaTransport::parseWalletCoins(const QByteArray &body)
{
    QStringList out;
    for (const QJsonValue &v : walletArray(body, nullptr)) {
        const QString cur = v.toObject().value(QStringLiteral("coin")).toString().trimmed().toLower();
        if (!cur.isEmpty() && !out.contains(cur))
            out << cur;
    }
    return out;
}

QStringList WinnaTransport::defaultCurrencies()
{
    return {QStringLiteral("btc"),  QStringLiteral("eth"),  QStringLiteral("ltc"),
            QStringLiteral("usdt"), QStringLiteral("usdc"), QStringLiteral("doge"),
            QStringLiteral("bnb"),  QStringLiteral("trx"),  QStringLiteral("sol"),
            QStringLiteral("xrp"),  QStringLiteral("ton"),  QStringLiteral("dai"),
            QStringLiteral("shfl")};
}

void WinnaTransport::requestBalances()
{
    QString why;
    if (!isReady(&why)) {
        QTimer::singleShot(0, this, [this, why] { emit balancesFailed(why); });
        return;
    }
    const quint32 generation = m_generation;
    const QString path = QStringLiteral("v1/payments/wallets?_wnNoCache=%1")
                             .arg(QDateTime::currentMSecsSinceEpoch());
    sendApi("GET", path, QJsonObject(),
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
        const QMap<QString, double> balances = parseWallets(data, &ok);
        if (!ok) {
            emit balancesFailed(tr("Login failed: unexpected response (is the session valid?) %1")
                                    .arg(QString::fromUtf8(data.left(200)).simplified()));
            return;
        }
        emit balancesUpdated(balances);
    });
}

void WinnaTransport::rotateSeed(const QString &clientSeed,
                                std::function<void(const QString &, const QJsonObject &)> done)
{
    QString why;
    if (!isReady(&why)) {
        QTimer::singleShot(0, this, [done, why] { done(why, QJsonObject()); });
        return;
    }
    const quint32 generation = m_generation;
    auto failText = [](int status, const QByteArray &data, const QString &netError) {
        QString msg = errorText(status, data);
        if (msg.isEmpty())
            msg = netError.isEmpty() ? tr("unexpected answer: %1")
                                           .arg(QString::fromUtf8(data.left(200)).simplified())
                                     : netError;
        return msg;
    };

    // 1. the active seed: GET /v1/games/our-game/get-active-seed
    //    {"success":true,"response":{"id":1103124,"nonce":75,"client_seed","server_seed",…}}
    sendApi("GET", QStringLiteral("v1/games/our-game/get-active-seed"), QJsonObject(),
            [this, generation, clientSeed, done, failText](int status, const QByteArray &data,
                                                           const QString &netError) {
        if (generation != m_generation)
            return done(tr("Winna: the login changed - try again."), QJsonObject());
        const QJsonObject active = QJsonDocument::fromJson(data).object()
                                       .value(QStringLiteral("response")).toObject();
        const qint64 seedId = qint64(numberOf(active.value(QStringLiteral("id"))));
        if (status < 200 || status >= 300 || seedId <= 0)
            return done(tr("could not read the active seed (%1)").arg(failText(status, data, netError)),
                        QJsonObject());

        // 2. reveal it and start a new pair: POST /v1/games/our-game/unhash {"seedId","clientSeed"}
        QJsonObject body;
        body.insert(QStringLiteral("seedId"), seedId);
        body.insert(QStringLiteral("clientSeed"), clientSeed);
        sendApi("POST", QStringLiteral("v1/games/our-game/unhash"), body,
                [this, generation, done, failText](int status2, const QByteArray &data2,
                                                   const QString &netError2) {
            if (generation != m_generation)
                return done(tr("Winna: the login changed - try again."), QJsonObject());
            const QJsonObject newSeed = QJsonDocument::fromJson(data2).object()
                                            .value(QStringLiteral("response")).toObject()
                                            .value(QStringLiteral("new_seed")).toObject();
            if (status2 < 200 || status2 >= 300 || newSeed.isEmpty())
                return done(failText(status2, data2, netError2), QJsonObject());
            m_gameToken.clear(); // the next game session starts with the new seed pair
            done(QString(), newSeed);
        });
    });
}

void WinnaTransport::vaultDeposit(const QString &coin, double amount,
                                  std::function<void(const QString &)> done)
{
    QString why;
    if (!isReady(&why)) {
        QTimer::singleShot(0, this, [done, why] { done(why); });
        return;
    }
    // Amount as a plain decimal without trailing zeros ("1", "0.5"), like the site.
    QString text = QString::number(std::max(0.0, amount), 'f', 8);
    while (text.contains(QLatin1Char('.')) && text.endsWith(QLatin1Char('0')))
        text.chop(1);
    if (text.endsWith(QLatin1Char('.')))
        text.chop(1);

    QJsonObject body;
    body.insert(QStringLiteral("coin"), apiCurrency(coin));
    body.insert(QStringLiteral("balance"), text);
    const quint32 generation = m_generation;
    sendApi("POST", QStringLiteral("v2/vault/deposit"), body,
            [this, generation, done](int status, const QByteArray &data, const QString &netError) {
        if (generation != m_generation)
            return done(tr("Winna: the login changed - try again."));
        const QJsonObject root = QJsonDocument::fromJson(data).object();
        const QJsonValue success = root.value(QStringLiteral("success"));
        if (status >= 200 && status < 300 && (!success.isBool() || success.toBool()))
            return done(QString());
        // {"success":false,"status":400,"message":"Minimum vault deposit amount is 1 USD",
        //  "title":"Bad Request","code":"MA-000000"}: show the site's message as it is.
        QString msg = root.value(QStringLiteral("message")).toString().trimmed();
        if (msg.isEmpty())
            msg = root.value(QStringLiteral("title")).toString().trimmed();
        if (!msg.isEmpty())
            return done(tr("Winna: %1").arg(msg));
        msg = errorText(status, data);
        if (msg.isEmpty())
            msg = netError.isEmpty() ? tr("Winna vault deposit rejected (HTTP %1): %2")
                                           .arg(status).arg(QString::fromUtf8(data.left(200)).simplified())
                                     : netError;
        done(msg);
    });
}

void WinnaTransport::ensureGameToken(const QString &game, std::function<void(const QString &)> then)
{
    const QString coin = m_coin;
    if (coin.isEmpty()) {
        then(tr("Winna: no currency selected."));
        return;
    }
    const bool fresh = !m_gameToken.isEmpty() && m_gameCoin == coin && m_gameName == game
                       && (!m_gameExpiry.isValid()
                           || QDateTime::currentDateTimeUtc().secsTo(m_gameExpiry) > kTokenMarginSecs);
    if (fresh) {
        then(QString());
        return;
    }
    m_tokenWaiters.append(std::move(then));
    if (m_tokenBusy)
        return; // the running fetch answers every waiter
    m_tokenBusy = true;

    const quint32 generation = m_generation;
    auto finish = [this](const QString &error) {
        m_tokenBusy = false;
        const auto waiters = std::move(m_tokenWaiters);
        m_tokenWaiters.clear();
        for (const auto &w : waiters)
            w(error);
    };

    // 1. the wallet the games use -> the bet currency
    QJsonObject coinBody;
    coinBody.insert(QStringLiteral("coin"), apiCurrency(coin));
    sendApi("POST", QStringLiteral("v1/user/change-wallet-coin"), coinBody,
            [this, generation, coin, game, finish](int status, const QByteArray &data, const QString &netError) {
        if (generation != m_generation)
            return finish(tr("Winna: the login changed - try again."));
        if (status < 200 || status >= 300) {
            QString msg = errorText(status, data);
            if (msg.isEmpty())
                msg = netError.isEmpty() ? tr("Network error.") : netError;
            return finish(tr("Winna: could not switch the wallet to %1 (%2).").arg(coin.toUpper(), msg));
        }
        // 2. a game session for the game with that wallet
        QJsonObject startBody;
        startBody.insert(QStringLiteral("game"), game);
        startBody.insert(QStringLiteral("platform"), QStringLiteral("GPL_DESKTOP"));
        sendApi("POST", QStringLiteral("v1/games/our-game/start"), startBody,
                [this, generation, coin, game, finish](int status2, const QByteArray &data2, const QString &netError2) {
            if (generation != m_generation)
                return finish(tr("Winna: the login changed - try again."));
            if (status2 < 200 || status2 >= 300) {
                QString msg = errorText(status2, data2);
                if (msg.isEmpty())
                    msg = netError2.isEmpty() ? tr("Network error.") : netError2;
                return finish(tr("Winna: could not start the %1 game (%2).").arg(game, msg));
            }
            const QJsonObject result = QJsonDocument::fromJson(data2).object()
                                           .value(QStringLiteral("response")).toObject()
                                           .value(QStringLiteral("result")).toObject();
            const QString jwt = result.value(QStringLiteral("jwt")).toString().trimmed();
            if (jwt.isEmpty())
                return finish(tr("Winna: no game token in the answer: %1")
                                  .arg(QString::fromUtf8(data2.left(200)).simplified()));
            const QJsonObject payload = jwtPayload(jwt);
            // user_id "87344-USDC-USDC-name": the coin the session plays with.
            const QString userId = payload.value(QStringLiteral("user_id")).toString();
            const QString tokenCoin = userId.section(QLatin1Char('-'), 1, 1).trimmed().toLower();
            if (!tokenCoin.isEmpty() && tokenCoin != coin)
                return finish(tr("Winna: the game session is for %1, not %2 (the wallet did not switch).")
                                  .arg(tokenCoin.toUpper(), coin.toUpper()));
            m_gameToken = jwt;
            m_gameCoin = coin;
            m_gameName = game;
            const qint64 exp = qint64(numberOf(payload.value(QStringLiteral("exp"))));
            m_gameExpiry = exp > 0 ? QDateTime::fromSecsSinceEpoch(exp).toUTC() : QDateTime();
            m_sid = QUuid::createUuid().toString(QUuid::WithoutBraces);
            emit notice(tr("Winna: %1 session started (%2).").arg(game, coin.toUpper()));
            finish(QString());
        });
    });
}

TransportReply WinnaTransport::interpret(int status, const QByteArray &data, const QString &netError) const
{
    if (status >= 200 && status < 300) {
        TransportReply r;
        r.ok = true;
        r.status = status;
        r.json = QJsonDocument::fromJson(data).object();
        return r;
    }
    if (status == 403 || status == 503) {
        const QByteArray lower = data.left(4000).toLower();
        if (lower.contains("cloudflare") || lower.contains("challenge"))
            return failure(errorText(status, data), status);
    }
    if (status > 0) {
        // Refused by the game server (validation, balance, rate limit, token): an API error in
        // "errors" so the callers' retry rules apply; the bet was not placed.
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

void WinnaTransport::post(const QString &path, const QJsonObject &body, TransportCallback cb)
{
    QString why;
    if (!isReady(&why)) {
        QTimer::singleShot(0, this, [cb, why] { cb(failure(why)); });
        return;
    }
    sendGame(path, body, std::move(cb), false);
}

void WinnaTransport::sendGame(const QString &path, const QJsonObject &body, TransportCallback cb,
                              bool retried)
{
    // "limbo/play" -> "limbo": the game session is started for that game.
    QString game = path.trimmed();
    while (game.startsWith(QLatin1Char('/')))
        game.remove(0, 1);
    game = game.section(QLatin1Char('/'), 0, 0).toLower();
    if (game.isEmpty())
        game = QStringLiteral("dice");
    ensureGameToken(game, [this, path, body, cb, retried](const QString &error) {
        if (!error.isEmpty()) {
            // No game session: nothing was sent, so the bet was not placed.
            TransportReply r;
            r.ok = true;
            r.status = 0;
            QJsonObject err;
            err.insert(QStringLiteral("message"), error);
            r.json.insert(QStringLiteral("errors"), QJsonArray{err});
            cb(r);
            return;
        }
        QString p = path.trimmed();
        while (p.startsWith(QLatin1Char('/')))
            p.remove(0, 1);
        QNetworkRequest req{QUrl(gameBase() + QLatin1Char('/') + p)};
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        req.setRawHeader("Accept", "application/json");
        req.setRawHeader("Accept-Language", "en-US,en;q=0.9");
        req.setRawHeader("token", m_gameToken.toUtf8());
        req.setRawHeader("sid", m_sid.toUtf8());
        if (m_ext) {
            req.setRawHeader("X-Xbot-Credentials", "include");
        } else {
            req.setRawHeader("Cookie", cookieHeader(m_cred));
        }
        req.setRawHeader("Origin", "https://games-prod.winna.com");
        req.setRawHeader("Referer", "https://games-prod.winna.com/");
        req.setHeader(QNetworkRequest::UserAgentHeader,
                      m_cred.userAgent.isEmpty() ? defaultUserAgent() : m_cred.userAgent);
        req.setTransferTimeout(kRequestTimeoutMs);
        manualCookies(req);

        sendHttp(this, m_nam, m_ext, pageHosts(), "POST", req,
                 QJsonDocument(body).toJson(QJsonDocument::Compact),
                 [this, path, body, cb, retried](int status, const QByteArray &data, const QString &netError) {
            // The game token was refused (expired / other session): start a new game session and
            // send the bet once more (a refused token means the bet was not placed).
            if ((status == 401 || status == 419) && !retried) {
                m_gameToken.clear();
                sendGame(path, body, cb, true);
                return;
            }
            cb(interpret(status, data, netError));
        });
    });
}
