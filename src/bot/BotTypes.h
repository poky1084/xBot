#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>

#include <algorithm>
#include <cmath>

// Stake-style dice rules: roll is uniform in [0, 100], 1% house edge.
// "Under" wins when roll < target, "Over" wins when roll > target.
// Win chance = (under ? target : 100 - target), multiplier = floor(0.99*100/chance, 4dp).

namespace BotConstants
{
inline constexpr double kHouseEdge = 0.01;
inline constexpr double kStartBalance = 1000.0;

// Allowed win chance (and therefore roll target) range, in percent.
// Applies to manual, auto (condition blocks) and Lua mode.
inline constexpr double kMinChance = 0.01;
inline constexpr double kMaxChance = 99.99;

// Limbo: the bet wins when the result is >= the multiplier target; the payout
// multiplier equals the target, win chance = 99 / target.
inline constexpr double kMinLimboTarget = 1.01;
inline constexpr double kMaxLimboTarget = 1000000.0;

inline double limboChance(double multiplierTarget)
{
    return multiplierTarget > 0.0 ? 99.0 / multiplierTarget : 0.0;
}

inline double winChance(double target, bool isUnder)
{
    return isUnder ? target : 100.0 - target;
}

inline double payoutMultiplier(double target, bool isUnder)
{
    const double chance = winChance(target, isUnder);
    if (chance <= 0.0)
        return 0.0;
    const double raw = (100.0 * (1.0 - kHouseEdge)) / chance;
    return std::floor(raw * 10000.0) / 10000.0;
}

// A bet only counts as a WIN when its payout multiplier is >= 1x (payout >= stake).
// A partial payout (e.g. 0.5x) is a loss even though the casino paid something back.
// The tiny epsilon absorbs 8-decimal rounding of payout / amount (0.99999999 -> 1x).
inline bool isWinMultiplier(double payoutMult)
{
    return payoutMult >= 1.0 - 1e-9;
}

// Payout multiplier of a settled bet: payout / amount when there was a stake, otherwise
// (0 bet, payout is always 0) the multiplier the result reported, or 0 for a loss.
inline double settledMultiplier(double amount, double payout, double reportedMult, bool reportedWin)
{
    if (amount > 0.0)
        return payout / amount;
    return reportedWin ? reportedMult : 0.0;
}
} // namespace BotConstants

// Cuts a roll / result to 2 decimals without rounding (49.999 -> 49.99). The small
// epsilon keeps values such as 49.53, stored as 49.5299999..., from losing a cent.
inline double truncate2(double v)
{
    return std::floor(v * 100.0 + 1e-6) / 100.0;
}

enum class BetSide { Under, Over };

// Dice and Limbo are played on every casino that supports them; the rest are Stake's own
// games (see bot/StakeGames.h for which casino offers which game and how they are sent).
// Crash and Slide (multiplayer) are not supported.
enum class Game {
    Dice,
    Limbo,
    Keno,
    Mines,
    Plinko,
    Wheel,
    Hilo,
    Blackjack,
    Baccarat,
    Roulette,
    DragonTower,
    Flip,
    Rps,
    Pump,
    Snakes,
    Cases,
    Darts,
    Diamonds,
    VideoPoker,
    BlueSamurai,
    ScarabSpin,
    TomeOfLife,
    Bars,
    Chicken,
    Tarot,
    Drill,
    Packs,
    Moles,
    Primedice,
    GoatRun // Goated's Goat Run (not on Stake)
};

// Every game, in Game menu order.
inline const QList<Game> &allGames()
{
    static const QList<Game> games = {
        Game::Dice,       Game::Limbo,       Game::Baccarat,  Game::Bars,       Game::Blackjack,
        Game::BlueSamurai, Game::Cases,      Game::Chicken,   Game::Darts,      Game::Diamonds,
        Game::Drill,      Game::Flip,        Game::GoatRun,   Game::Hilo,      Game::Keno,
        Game::Mines,      Game::Moles,       Game::Packs,     Game::Plinko,     Game::Primedice,
        Game::Pump,       Game::Rps,         Game::Roulette,  Game::ScarabSpin, Game::Snakes,
        Game::Tarot,      Game::TomeOfLife,  Game::DragonTower, Game::VideoPoker, Game::Wheel};
    // (Game::DragonTower is "Tower": listed in alphabetical order by that name.)
    return games;
}

// Script name of a game (the value of `game = "..."` in Lua / JavaScript, also saved in the
// settings and shown in the bet list). Same names as the reference bot's README.
inline QString gameName(Game g)
{
    switch (g) {
    case Game::Dice:        return QStringLiteral("dice");
    case Game::Limbo:       return QStringLiteral("limbo");
    case Game::Keno:        return QStringLiteral("keno");
    case Game::Mines:       return QStringLiteral("mines");
    case Game::Plinko:      return QStringLiteral("plinko");
    case Game::Wheel:       return QStringLiteral("wheel");
    case Game::Hilo:        return QStringLiteral("hilo");
    case Game::Blackjack:   return QStringLiteral("blackjack");
    case Game::Baccarat:    return QStringLiteral("baccarat");
    case Game::Roulette:    return QStringLiteral("roulette");
    case Game::DragonTower: return QStringLiteral("tower"); // Stake's Dragon Tower / Shuffle's Tower
    case Game::Flip:        return QStringLiteral("flip");
    case Game::Rps:         return QStringLiteral("rps");
    case Game::Pump:        return QStringLiteral("pump");
    case Game::Snakes:      return QStringLiteral("snakes");
    case Game::Cases:       return QStringLiteral("cases");
    case Game::Darts:       return QStringLiteral("darts");
    case Game::Diamonds:    return QStringLiteral("diamonds");
    case Game::VideoPoker:  return QStringLiteral("videopoker");
    case Game::BlueSamurai: return QStringLiteral("bluesamurai");
    case Game::ScarabSpin:  return QStringLiteral("scarabspin");
    case Game::TomeOfLife:  return QStringLiteral("tomeoflife");
    case Game::Bars:        return QStringLiteral("bars");
    case Game::Chicken:     return QStringLiteral("chicken");
    case Game::GoatRun:     return QStringLiteral("goatrun");
    case Game::Tarot:       return QStringLiteral("tarot");
    case Game::Drill:       return QStringLiteral("drill");
    case Game::Packs:       return QStringLiteral("packs");
    case Game::Moles:       return QStringLiteral("moles");
    case Game::Primedice:   return QStringLiteral("primedice");
    }
    return QStringLiteral("dice");
}

// Menu / window title name.
inline QString gameDisplayName(Game g)
{
    switch (g) {
    case Game::Dice:        return QStringLiteral("Dice");
    case Game::Limbo:       return QStringLiteral("Limbo");
    case Game::Keno:        return QStringLiteral("Keno");
    case Game::Mines:       return QStringLiteral("Mines");
    case Game::Plinko:      return QStringLiteral("Plinko");
    case Game::Wheel:       return QStringLiteral("Wheel");
    case Game::Hilo:        return QStringLiteral("Hilo");
    case Game::Blackjack:   return QStringLiteral("Blackjack");
    case Game::Baccarat:    return QStringLiteral("Baccarat");
    case Game::Roulette:    return QStringLiteral("Roulette");
    case Game::DragonTower: return QStringLiteral("Tower");
    case Game::Flip:        return QStringLiteral("Flip");
    case Game::Rps:         return QStringLiteral("Rock Paper Scissors");
    case Game::Pump:        return QStringLiteral("Pump");
    case Game::Snakes:      return QStringLiteral("Snakes");
    case Game::Cases:       return QStringLiteral("Cases");
    case Game::Darts:       return QStringLiteral("Darts");
    case Game::Diamonds:    return QStringLiteral("Diamonds");
    case Game::VideoPoker:  return QStringLiteral("Video Poker");
    case Game::BlueSamurai: return QStringLiteral("Blue Samurai");
    case Game::ScarabSpin:  return QStringLiteral("Scarab Spin");
    case Game::TomeOfLife:  return QStringLiteral("Tome of Life");
    case Game::Bars:        return QStringLiteral("Bars");
    case Game::Chicken:     return QStringLiteral("Chicken");
    case Game::GoatRun:     return QStringLiteral("Goat Run");
    case Game::Tarot:       return QStringLiteral("Tarot");
    case Game::Drill:       return QStringLiteral("Drill");
    case Game::Packs:       return QStringLiteral("Packs");
    case Game::Moles:       return QStringLiteral("Moles");
    case Game::Primedice:   return QStringLiteral("Primedice X");
    }
    return QStringLiteral("Dice");
}

// Parses a script / settings game name ("keno", "Video Poker" and "video-poker" also work;
// "dragontower", the old name of "tower", too).
inline bool gameFromName(const QString &name, Game *out)
{
    QString n = name.trimmed().toLower();
    n.remove(QLatin1Char(' '));
    n.remove(QLatin1Char('-'));
    n.remove(QLatin1Char('_'));
    if (n == QLatin1String("primedicex"))
        n = QStringLiteral("primedice");
    else if (n == QLatin1String("rockpaperscissors"))
        n = QStringLiteral("rps");
    else if (n == QLatin1String("samurai") || n == QLatin1String("slotssamurai"))
        n = QStringLiteral("bluesamurai");
    else if (n == QLatin1String("dragontower")) // the old name (saved settings, older scripts)
        n = QStringLiteral("tower");
    for (Game g : allGames()) {
        if (gameName(g) == n) {
            if (out)
                *out = g;
            return true;
        }
    }
    return false;
}

// Dice and Limbo use the target / side / multiplier settings; every other game uses
// BotConfig::gameParams.
inline bool isDiceOrLimbo(Game g)
{
    return g == Game::Dice || g == Game::Limbo;
}

// Auto-bet progression rules, same model as the reference web app.
struct OnResultRule
{
    bool increase = false; // false = reset to base, true = increase by pct%
    double pct = 0.0;

    double nextBet(double base, double current) const
    {
        if (!increase)
            return base;
        // Kept exact: the placed bet is cut to 8 decimals (BotEngine), but the progression
        // itself must not be, or small bets never grow (1e-8 * 1.5 floored is 1e-8 again).
        return std::max(0.0, current * (1.0 + pct / 100.0)); // pct may be negative
    }
};

struct BotConfig
{
    Game game = Game::Dice;
    BetSide side = BetSide::Under;
    double target = 50.5;      // dice: roll target, 0.01 .. 99.99
    double multiplierTarget = 2.0; // limbo: target multiplier, 1.01 .. 1,000,000
    double baseBet = 0.00000000;
    double startBalance = BotConstants::kStartBalance;
    int stopAfterBets = 0;     // 0 = unlimited
    QString casino;            // scripts: "stake" / "shuffle"; empty = unchanged
    QString currency;          // scripts: lowercase currency code to bet in ("btc", "usdt", ...); empty = unchanged
    // Settings of the Stake games other than dice / limbo, keyed by the script variable names
    // of the reference bot (keno: {"risk", "numbers"}, mines: {"mines", "fields"}, ...).
    // See StakeGames::buildBetRequest() for what each game reads.
    QJsonObject gameParams;

    OnResultRule onWin{false, 0.0};
    OnResultRule onLoss{false, 0.0};

    double stopProfit = 0.0;   // 0 = off
    double stopLoss = 0.0;     // 0 = off

    double chance() const
    {
        return game == Game::Limbo ? BotConstants::limboChance(multiplierTarget)
                                   : BotConstants::winChance(target, side == BetSide::Under);
    }
    double multiplier() const
    {
        return game == Game::Limbo ? multiplierTarget
                                   : BotConstants::payoutMultiplier(target, side == BetSide::Under);
    }
};

struct BetRecord
{
    qint64 index = 0;
    qint64 nonce = 0;
    QString betId;            // server bet id (live bets only; empty in simulation)
    QString casino;           // casino selected when the bet was made ("stake" / "shuffle")
    QDateTime time;
    Game game = Game::Dice;
    double bet = 0.0;
    double target = 0.0;      // dice: roll target, limbo: multiplier target
    // Dice: the target the casino itself got (Pasino 52.49, Tower 50.75, Duel 50.05,
    // Wolfbet 50.49 for the app's 2x / over 50.50), shown in the bet list. < 0 = same as `target`.
    double casinoTarget = -1.0;
    BetSide side = BetSide::Under;
    double roll = 0.0;        // 2 decimals, cut (bet list / UI)
    double rawRoll = 0.0;     // the full value as the casino reported it (scripts' roll / lastBet.roll)
    // Games other than dice / limbo: what the Target and Roll columns show ("medium|8",
    // "3 hits", "fourOfAKind", ...). Empty = the numeric target / roll.
    QString targetText;
    QString rollText;
    double mult = 0.0;
    bool win = false;
    double payout = 0.0;
    double profit = 0.0;
    double balance = 0.0;
};

struct SessionStats
{
    qint64 bets = 0;
    qint64 wins = 0;
    qint64 losses = 0;
    qint64 bigWins = 0;        // wins with multiplier >= 10x
    double balance = 0.0;
    double startBalance = 0.0;
    double profit = 0.0;
    double wagered = 0.0;

    // Session extremes / streaks
    double highestBet = 0.0;
    double highestProfit = 0.0;   // peak running session profit (never below 0)
    double lowestProfit = 0.0;    // deepest running session profit (never above 0)
    qint64 highestStreak = 0;     // longest win streak
    qint64 lowestStreak = 0;      // longest loss streak, stored as a negative number
    qint64 currentStreak = 0;     // > 0 = consecutive wins, < 0 = consecutive losses

    // Call after profit/wagered have been updated for the bet.
    void trackBet(double betSize, bool win)
    {
        highestBet = qMax(highestBet, betSize);
        highestProfit = qMax(highestProfit, profit);
        lowestProfit = qMin(lowestProfit, profit);
        if (win)
            currentStreak = currentStreak > 0 ? currentStreak + 1 : 1;
        else
            currentStreak = currentStreak < 0 ? currentStreak - 1 : -1;
        highestStreak = qMax(highestStreak, currentStreak);
        lowestStreak = qMin(lowestStreak, currentStreak);
    }

    double winRate() const { return bets > 0 ? 100.0 * double(wins) / double(bets) : 0.0; }
    // Return to player so far: what came back (stakes + profit) per amount wagered, in %.
    double rtp() const { return wagered > 0.0 ? 100.0 * (wagered + profit) / wagered : 0.0; }

    void reset(double initialBalance)
    {
        *this = SessionStats{};
        balance = startBalance = initialBalance;
    }
};
