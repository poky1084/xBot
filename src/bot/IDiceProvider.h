#pragma once

#include <QString>

// A dice roll source. The engine only needs a uniform roll in [0, 100];
// everything else (bet sizing, stop conditions) lives in BotEngine.
class IDiceProvider
{
public:
    virtual ~IDiceProvider() = default;

    virtual QString name() const = 0;
    virtual QString description() const { return {}; }

    // Called once when the session starts.
    virtual bool open(QString *errorOut = nullptr)
    {
        Q_UNUSED(errorOut);
        return true;
    }

    // Produce the next roll in [0, 100]. Returns false on hard failure.
    virtual bool nextRoll(double *rollOut, QString *errorOut = nullptr) = 0;

    // Produce the next limbo result (a multiplier >= 1.00). Providers that
    // cannot do limbo keep the default, which reports an error.
    virtual bool nextLimbo(double *resultOut, QString *errorOut = nullptr)
    {
        Q_UNUSED(resultOut);
        if (errorOut)
            *errorOut = QStringLiteral("This provider does not support limbo.");
        return false;
    }

    // Nonce of the next roll for provably-fair verification (-1 if not applicable).
    virtual qint64 currentNonce() const { return -1; }
};
