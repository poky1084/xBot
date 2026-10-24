#include "bot/ConditionRunner.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr double kMinChance = BotConstants::kMinChance;
constexpr double kMaxChance = BotConstants::kMaxChance;

double floor8(double v) { return std::floor(v * 1e8) / 1e8; }
double round2(double v) { return std::round(v * 100.0) / 100.0; }

void setChance(BotConfig &cfg, double chance)
{
    chance = round2(std::clamp(chance, kMinChance, kMaxChance));
    if (cfg.game == Game::Limbo) {
        // Limbo: win chance = 99 / target, so keep the chance and move the multiplier target.
        cfg.multiplierTarget = round2(std::clamp(99.0 / chance, BotConstants::kMinLimboTarget,
                                                 BotConstants::kMaxLimboTarget));
        return;
    }
    // Under wins below the target, Over wins above it.
    cfg.target = round2(cfg.side == BetSide::Under ? chance : 100.0 - chance);
}
} // namespace

void ConditionRunner::reset(const BotConfig &base)
{
    m_base = base;
    m_bets = m_wins = m_losses = 0;
    m_winStreak = m_loseStreak = 0;
}

bool ConditionRunner::onBetResult(bool win, double profitSinceStart, double balance,
                                  double &nextBet, BotConfig &runCfg)
{
    ++m_bets;
    if (win) {
        ++m_wins;
        ++m_winStreak;
        m_loseStreak = 0;
    } else {
        ++m_losses;
        ++m_loseStreak;
        m_winStreak = 0;
    }

    for (const ConditionBlockData &b : std::as_const(m_blocks)) {
        if (!matches(b, win, profitSinceStart, balance))
            continue;
        if (apply(b, nextBet, runCfg))
            return true;
    }
    return false;
}

bool ConditionRunner::matches(const ConditionBlockData &b, bool win, double profit,
                              double balance) const
{
    const double v = b.onValue;

    if (b.type == QLatin1String("profit")) {
        double current = profit; // "profit"
        if (b.profitType == QLatin1String("loss"))
            current = -profit;   // loss is positive when losing
        else if (b.profitType == QLatin1String("balance"))
            current = balance;

        constexpr double eps = 1e-12;
        if (b.onType == QLatin1String("greaterThan")) return current > v + eps;
        if (b.onType == QLatin1String("greaterThanOrEqualTo")) return current >= v - eps;
        if (b.onType == QLatin1String("lowerThan")) return current < v - eps;
        if (b.onType == QLatin1String("lowerThanOrEqualTo")) return current <= v + eps;
        return false;
    }

    // ---- bets ---------------------------------------------------------------
    // "bet": any bet (counts all bets); "win"/"lose": only that outcome.
    qint64 count = m_bets;
    qint64 streak = m_bets;
    if (b.betType == QLatin1String("win")) {
        count = m_wins;
        streak = m_winStreak;
        if (!win && b.onType == QLatin1String("every"))
            return false; // "every N wins" is only checked on a win
    } else if (b.betType == QLatin1String("lose")) {
        count = m_losses;
        streak = m_loseStreak;
        if (win && b.onType == QLatin1String("every"))
            return false;
    }

    const qint64 n = static_cast<qint64>(std::floor(v));
    if (b.onType == QLatin1String("every"))
        return n > 0 && count > 0 && count % n == 0;
    if (b.onType == QLatin1String("everyStreakOf"))
        return n > 0 && streak > 0 && streak % n == 0;
    if (b.onType == QLatin1String("firstStreakOf"))
        return n > 0 && streak == n;
    if (b.onType == QLatin1String("streakGreaterThan"))
        return streak > n;
    if (b.onType == QLatin1String("streakLowerThan"))
        return streak < n;
    return false;
}

bool ConditionRunner::apply(const ConditionBlockData &b, double &nextBet, BotConfig &cfg) const
{
    const QString &t = b.doType;
    const double v = b.doValue;
    const double chance = cfg.game == Game::Limbo
                              ? BotConstants::limboChance(cfg.multiplierTarget)
                              : BotConstants::winChance(cfg.target, cfg.side == BetSide::Under);

    if (t == QLatin1String("increaseByPercentage"))
        nextBet = floor8(nextBet * (1.0 + v / 100.0));
    else if (t == QLatin1String("decreaseByPercentage"))
        nextBet = std::max(0.0, floor8(nextBet * (1.0 - v / 100.0)));
    else if (t == QLatin1String("addToAmount"))
        nextBet = floor8(nextBet + v);
    else if (t == QLatin1String("subtractFromAmount"))
        nextBet = std::max(0.0, floor8(nextBet - v));
    else if (t == QLatin1String("setAmount"))
        nextBet = std::max(0.0, floor8(v));
    else if (t == QLatin1String("resetAmount"))
        nextBet = m_base.baseBet;
    else if (t == QLatin1String("increaseWinChanceBy"))
        setChance(cfg, chance * (1.0 + v / 100.0));
    else if (t == QLatin1String("decreaseWinChanceBy"))
        setChance(cfg, chance * (1.0 - v / 100.0));
    else if (t == QLatin1String("addToWinChance"))
        setChance(cfg, chance + v);
    else if (t == QLatin1String("subtractFromWinChance"))
        setChance(cfg, chance - v);
    else if (t == QLatin1String("setWinChance"))
        setChance(cfg, v);
    else if (t == QLatin1String("resetWinChance")) {
        cfg.target = m_base.target;
        cfg.side = m_base.side;
        cfg.multiplierTarget = m_base.multiplierTarget;
    } else if (t == QLatin1String("switchOverUnder")) {
        // Keep the win chance, flip the side (and mirror the target). No effect in limbo.
        if (cfg.game == Game::Limbo)
            return false;
        cfg.side = cfg.side == BetSide::Under ? BetSide::Over : BetSide::Under;
        cfg.target = round2(100.0 - cfg.target);
    } else if (t == QLatin1String("stop"))
        return true;

    return false;
}
