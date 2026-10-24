#pragma once

#include "bot/IDiceProvider.h"

#include <QVector>

#include <functional>

// A revealed (server, client) seed pair from a finished nonce range.
struct RevealedSeeds
{
    QString serverSeed;
    QString serverHash;
    QString clientSeed;
    int nonces = 0;
};

// Result of a Limbo Monte-Carlo run over a range of nonces (see simulateLimbo).
struct LimboSimResult
{
    qint64 rounds = 0;            // rounds actually played
    qint64 wins = 0;              // rounds with result >= target
    double target = 0.0;
    double hitRate = 0.0;         // observed win chance, %
    double expectedRate = 0.0;    // theoretical win chance, % (99 / target)
    double rtp = 0.0;             // observed return to player, % (wins * target / rounds)
    double maxResult = 0.0;       // highest multiplier seen
    qint64 longestLossStreak = 0;
    bool cancelled = false;
};

// Provably-fair dice provider, identical algorithm to Stake:
//   HMAC_SHA256(serverSeed, "clientSeed:nonce:0"), first 4 bytes -> float -> roll.
// The server seed is hidden behind its SHA-256 hash until it is rotated,
// so every result can be verified after the fact.
class SimulatedProvider final : public IDiceProvider
{
public:
    SimulatedProvider();

    QString name() const override { return QStringLiteral("Simulated"); }
    QString description() const override { return QStringLiteral("Provably fair — play money only"); }

    bool nextRoll(double *rollOut, QString *errorOut = nullptr) override;
    bool nextLimbo(double *resultOut, QString *errorOut = nullptr) override;
    qint64 currentNonce() const override { return m_nonce; }

    // --- Provably fair interface (used by the Fairness tab) ---
    QString serverSeed() const { return m_serverSeed; }
    QString serverHash() const { return m_serverHash; }
    QString clientSeed() const { return m_clientSeed; }
    void setClientSeed(const QString &seed);
    int nonce() const { return m_nonce; }

    // Overrides the running simulation with a fixed seed pair (used by the
    // Fairness tab's "Use these seeds for simulation" checkbox).
    void setServerSeed(const QString &seed);   // updates hash, no reveal logged
    void setNonce(qint64 nonce);               // 1-based

    // Resets to fresh random seeds (leaves the revealed-seeds list alone).
    void randomizeSeeds();

    // Reveals the current server seed (returned) and starts a fresh seed round.
    RevealedSeeds rotateSeeds();

    // Verification helpers (also used by the Fairness tab).
    static double rollFromSeeds(const QString &serverSeed, const QString &clientSeed, qint64 nonce);
    static double limboFromSeeds(const QString &serverSeed, const QString &clientSeed, qint64 nonce);
    // Plays `rounds` consecutive nonces (startNonce, startNonce+1, ...) of a fixed seed
    // pair at a fixed Limbo target. Pure function: does not touch the provider's state.
    // `progress(i)` is called every few thousand rounds; return false to cancel.
    static LimboSimResult simulateLimbo(const QString &serverSeed, const QString &clientSeed,
                                        qint64 startNonce, qint64 rounds, double target,
                                        const std::function<bool(qint64)> &progress = {});
    static double floatFromSeeds(const QString &serverSeed, const QString &clientSeed, qint64 nonce);
    static QString sha256Hex(const QString &input);
    static QString randomHex(int bytes);

private:
    QString m_serverSeed;
    QString m_serverHash;
    QString m_clientSeed;
    // 1-based: the first bet of every seed pair is nonce 1 (Stake convention).
    qint64 m_nonce = 1;
};