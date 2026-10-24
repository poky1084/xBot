#include "bot/SimulatedProvider.h"

#include <QCryptographicHash>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>

#include "bot/BotTypes.h"

#include <algorithm>
#include <cmath>

SimulatedProvider::SimulatedProvider()
{
    m_serverSeed = randomHex(32);
    m_serverHash = sha256Hex(m_serverSeed);
    m_clientSeed = randomHex(8);
    // No seed pair has been rotated yet, so we are still on the first one (bet 1 is next).
    m_nonce = 1;
}

QString SimulatedProvider::randomHex(int bytes)
{
    QString out;
    out.reserve(bytes * 2);
    for (int i = 0; i < bytes; ++i) {
        const quint32 b = QRandomGenerator::global()->bounded(256u);
        out += QStringLiteral("%1").arg(b, 2, 16, QLatin1Char('0'));
    }
    return out;
}

QString SimulatedProvider::sha256Hex(const QString &input)
{
    const QByteArray digest =
        QCryptographicHash::hash(input.toUtf8(), QCryptographicHash::Sha256);
    return QString::fromLatin1(digest.toHex());
}

double SimulatedProvider::floatFromSeeds(const QString &serverSeed,
                                         const QString &clientSeed, qint64 nonce)
{
    const QByteArray message =
        QStringLiteral("%1:%2:0").arg(clientSeed).arg(nonce).toUtf8();
    const QByteArray mac = QMessageAuthenticationCode::hash(
        message, serverSeed.toUtf8(), QCryptographicHash::Sha256);

    // First 4 bytes -> float in [0, 1), exactly like the reference implementation.
    const auto byteAt = [&mac](int i) { return i < mac.size() ? quint8(mac.at(i)) : 0u; };
    const double f = byteAt(0) / 256.0 + byteAt(1) / 65536.0 + byteAt(2) / 16777216.0
                     + byteAt(3) / 4294967296.0;
    return f;
}

double SimulatedProvider::rollFromSeeds(const QString &serverSeed,
                                        const QString &clientSeed, qint64 nonce)
{
    return std::floor(floatFromSeeds(serverSeed, clientSeed, nonce) * 10001.0) / 100.0;
}

// Limbo: 1% house edge, result = 0.99 / (1 - f) style crash point, 2 decimals.
double SimulatedProvider::limboFromSeeds(const QString &serverSeed,
                                         const QString &clientSeed, qint64 nonce)
{
    const double f = floatFromSeeds(serverSeed, clientSeed, nonce);
    const double raw = (1e8 / (std::floor(f * 1e8) + 1.0)) * 0.99;
    const double result = std::floor(raw * 100.0) / 100.0;
    return std::clamp(result, 1.0, BotConstants::kMaxLimboTarget);
}

LimboSimResult SimulatedProvider::simulateLimbo(const QString &serverSeed,
                                               const QString &clientSeed, qint64 startNonce,
                                               qint64 rounds, double target,
                                               const std::function<bool(qint64)> &progress)
{
    LimboSimResult r;
    r.target = target;
    r.expectedRate = BotConstants::limboChance(target);

    qint64 lossRun = 0;
    for (qint64 i = 0; i < rounds; ++i) {
        if (progress && (i % 5000) == 0 && !progress(i)) {
            r.cancelled = true;
            break;
        }
        const double result = limboFromSeeds(serverSeed, clientSeed, startNonce + i);
        ++r.rounds;
        r.maxResult = std::max(r.maxResult, result);
        if (result >= target - 1e-9) {
            ++r.wins;
            lossRun = 0;
        } else {
            r.longestLossStreak = std::max(r.longestLossStreak, ++lossRun);
        }
    }
    if (r.rounds > 0) {
        r.hitRate = 100.0 * double(r.wins) / double(r.rounds);
        r.rtp = 100.0 * double(r.wins) * target / double(r.rounds);
    }
    return r;
}

bool SimulatedProvider::nextRoll(double *rollOut, QString *errorOut)
{
    Q_UNUSED(errorOut);
    *rollOut = rollFromSeeds(m_serverSeed, m_clientSeed, m_nonce);
    ++m_nonce;
    return true;
}

bool SimulatedProvider::nextLimbo(double *resultOut, QString *errorOut)
{
    Q_UNUSED(errorOut);
    *resultOut = limboFromSeeds(m_serverSeed, m_clientSeed, m_nonce);
    ++m_nonce;
    return true;
}

void SimulatedProvider::setClientSeed(const QString &seed)
{
    if (!seed.isEmpty())
        m_clientSeed = seed;
}

void SimulatedProvider::setServerSeed(const QString &seed)
{
    if (seed.isEmpty())
        return;
    m_serverSeed = seed;
    m_serverHash = sha256Hex(m_serverSeed);
}

void SimulatedProvider::setNonce(qint64 nonce)
{
    if (nonce >= 1)
        m_nonce = nonce;
}

void SimulatedProvider::randomizeSeeds()
{
    m_serverSeed = randomHex(32);
    m_serverHash = sha256Hex(m_serverSeed);
    m_clientSeed = randomHex(8);
    m_nonce = 1;
}

RevealedSeeds SimulatedProvider::rotateSeeds()
{
    const RevealedSeeds revealed{m_serverSeed, m_serverHash, m_clientSeed, int(m_nonce)};
    m_serverSeed = randomHex(32);
    m_serverHash = sha256Hex(m_serverSeed);
    // Nonces are 1-based: the first bet of every seed pair is nonce 1, like Stake.
    m_nonce = 1;
    return revealed;
}