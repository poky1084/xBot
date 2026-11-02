#pragma once

#include "bot/ScriptStrategy.h"

#include <QString>

struct lua_State;

// Runs a user-written Lua script as a betting strategy.
//
// Script contract (same idea as the usual dice-bot scripts):
//
//   * The script body runs ONCE when the run starts. Use it to set the first bet:
//         game    = "dice"          -- optional: "dice" or "limbo" (default: the game
//                                   -- selected in the Game menu); may be changed in dobet().
//                                   -- On Stake also "keno", "mines", "plinko", "wheel", "hilo",
//                                   -- "blackjack", "baccarat", "roulette", "dragontower",
//                                   -- "flip", "rps", "pump", "snakes", "cases", "darts",
//                                   -- "diamonds", "videopoker", "bluesamurai", "scarabspin",
//                                   -- "tomeoflife", "bars", "chicken", "tarot", "drill",
//                                   -- "packs", "moles", "primedice" with the reference bot's
//                                   -- variables (keno: numbers = {1,2,3} (1 .. 40), risk = "low";
//                                   -- mines: fields = {1,2} (1 .. 25, 1 = top-left), mines = 3;
//                                   -- dragontower eggs / moles picks 1-based too; baccarat: nextbet =
//                                   -- {player = 0, banker = 0.004, tie = 0}; roulette: chips =
//                                   -- {{value = "colorRed", amount = 0.001}}; hilo / blackjack:
//                                   -- function round() return HILO_BET_HIGH end, with
//                                   -- currentBet = the open bet). See StakeGames::scriptVariables().
//         currency = "btc"          -- optional: currency code to bet in (default: the
//                                   -- current bet currency); may be changed in dobet().
//                                   -- Live: the account needs a balance in it; the session
//                                   -- statistics restart in the new currency
//         nextbet = 0.00000100      -- required
//       dice:
//         chance  = 49.5            -- required
//         bethigh = true            -- true = roll over, false = roll under
//       limbo:
//         target  = 2               -- required, multiplier target (1.01 .. 1000000);
//                                   -- chance and bethigh are not used
//
//   * If the script defines a global function dobet(), it is called after every
//     finished bet. Change nextbet / chance / bethigh in it to steer the next bet.
//
//   * Read-only globals refreshed before every dobet() call:
//         win            true if the last bet won
//         previousbet    amount of the last bet
//         lastBet        table with the last bet's details:
//                          lastBet.amount, lastBet.roll, lastBet.profit, lastBet.payout
//         lastprofit     profit of the last bet (negative on a loss)
//         roll           roll of the last bet (limbo: the result multiplier)
//         balance        current balance
//         profit         profit of this run so far
//         bets, wins, losses, wagered   counters of this run
//         currentstreak  > 0 = consecutive wins, < 0 = consecutive losses
//         startbalance   balance when the run started
//         currentprofit, winstreak, losestreak, lastBet.Roll / payoutMultiplier / win / id,
//         bet            Stake's bet object of the last bet (bet.state.playerHand, ...)
//         HILO_BET_HIGH (5), HILO_BET_LOW (4), HILO_BET_EQUAL (2), HILO_SKIP (7), HILO_CASHOUT (3)
//         checkTargetHit(hand, combos)  video poker: label of the first combo the hand holds
//         beep()         plays the reference bot's bell tone
//
//   * Functions: stop([reason]), print(...) / log(...), os.time(), os.clock(),
//         resetstats()   resets the session statistics (bets, wins, losses, wagered,
//                        profit, streaks, ...); the balance is kept and startbalance
//                        becomes the current balance
//         resetseed([clientseed])
//                        rotates the seed pair. clientseed is optional (a string,
//                        e.g. resetseed("123xcv123")); without it a random
//                        10-character alphanumeric client seed is used. Safe to call
//                        at any time; in live mode the rotation waits for a bet that is
//                        in flight and the next bet waits until the server confirmed it
//         vault(amount)  deposits amount of the current currency into the vault (live
//                        mode), e.g. vault(0.001)
//
// Syntax extras (translated to plain Lua before compiling): "!x" = "not x", "!=" = "~=",
// and the compound assignments += -= *= /= //= %= ^= ..= ("nextbet *= 2").
//
// The interpreter is sandboxed: no io, no os.execute, no require/dofile, a
// per-call time limit and a memory limit.
class LuaStrategy : public ScriptStrategy
{
public:
    LuaStrategy();
    ~LuaStrategy() override;
    LuaStrategy(const LuaStrategy &) = delete;
    LuaStrategy &operator=(const LuaStrategy &) = delete;

    // Compiles and runs the script body. `nextBet` and `runCfg` (target / side)
    // are taken from what the script set; nothing comes from the Manual / Auto
    // Bet tabs, so the script must set nextbet and chance itself.
    Status init(const QString &source, double balance, double &nextBet,
                BotConfig &runCfg) override;

    // Call after every bet. Updates nextBet / runCfg from the script's globals.
    Status onBet(const BetInfo &info, double &nextBet, BotConfig &runCfg) override;
    Status callRound(const QJsonObject &currentBet, QJsonValue &result) override;

    bool isLoaded() const override { return m_L != nullptr; }
    void setGlobalString(const char *name, const QString &value) override;
    void close() override;

    // --- used by the Lua C callbacks (not part of the public contract) -----
    bool timeExceeded() const;

protected:
    void onStatsReset(double balance) override;

private:
    bool readOutputs(double &nextBet, BotConfig &runCfg);
    void publish(const char *name, double value);
    void publishInt(const char *name, qint64 value);
    void publishBool(const char *name, bool value);
    // Sets the global table lastBet = {amount=, roll=, Roll=, profit=, payout=, ...}.
    void publishLastBet(const BetInfo &info);
    void publishJson(const char *name, const QJsonValue &value);
    // The value of global `name` as JSON (undefined when nil).
    QJsonValue readJson(const char *name);
    bool callProtected(int nargs, const char *what, int nresults = 0);
    void startTimer();

    lua_State *m_L = nullptr;
    qint64 m_deadlineMs = 0;   // 0 = no limit active

    // Run counters
    qint64 m_bets = 0;
    qint64 m_wins = 0;
    qint64 m_losses = 0;
    qint64 m_streak = 0;
    double m_wagered = 0.0;
};
