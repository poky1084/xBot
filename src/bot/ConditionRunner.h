#pragma once

#include "bot/BotTypes.h"
#include "ui/ConditionBlock.h"

// Executes the Condition Builder rules during an auto-bet run.
//
// After every bet the engine calls onBetResult(). Each block is evaluated in
// list order against the run so far; a block whose "When" matches performs its
// "Then" action on the next bet amount / win chance / over-under side. When the
// list is empty the engine falls back to the simple on-win / on-loss rules.
class ConditionRunner
{
public:
    void setBlocks(const BlockList &blocks) { m_blocks = blocks; }
    bool active() const { return !m_blocks.isEmpty(); }
    int blockCount() const { return m_blocks.size(); }

    // Start of an auto-bet run: clears streaks/counters, remembers the base config.
    void reset(const BotConfig &base);

    // Returns true when a "stop" action fired.
    //   win             result of the bet that just finished
    //   profitSinceStart  profit of this run (negative = losing)
    //   balance         current balance
    //   nextBet / runCfg  modified in place (runCfg.target / runCfg.side)
    bool onBetResult(bool win, double profitSinceStart, double balance, double &nextBet,
                     BotConfig &runCfg);

    // The game was switched mid-run: "reset win chance" now goes back to these bet
    // settings (game, target, side, limbo multiplier target).
    void setBetBase(const BotConfig &cfg)
    {
        m_base.game = cfg.game;
        m_base.target = cfg.target;
        m_base.side = cfg.side;
        m_base.multiplierTarget = cfg.multiplierTarget;
    }

private:
    bool matches(const ConditionBlockData &b, bool win, double profit, double balance) const;
    bool apply(const ConditionBlockData &b, double &nextBet, BotConfig &cfg) const;

    BlockList m_blocks;
    BotConfig m_base;
    qint64 m_bets = 0;
    qint64 m_wins = 0;
    qint64 m_losses = 0;
    qint64 m_winStreak = 0;
    qint64 m_loseStreak = 0;
};
