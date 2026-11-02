#pragma once

#include "bot/BotTypes.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>

// Stake's games besides dice and limbo (the same set and request bodies as the reference
// JavaScript bot, without its multiplayer Crash and Slide), plus which casino offers which
// game. Pure JSON helpers: StakeClient sends what is built here, BotEngine drives the
// multi-step games (hilo, blackjack, video poker, ...) with nextAction().
namespace StakeGames
{
// ---- which site offers which game ------------------------------------------------------
// `casinoKey` is LiveSettings::nameOf() ("stake", "shuffle", ...). Stake has every game,
// Paradice only dice, the other casinos dice and limbo.
QList<Game> gamesForCasino(const QString &casinoKey);
bool casinoSupports(const QString &casinoKey, Game game);

// ---- settings of a game -----------------------------------------------------------------
enum class ParamType {
    Int,        // whole number (spin box)
    Number,     // decimal number
    Choice,     // one of `choices` (editable: other values can be typed)
    IntList,    // "1,2,3"  -> [1,2,3]
    StringList, // "heads,tails" -> ["heads","tails"]
    Card,       // "A" / "AH" / "10S" -> {"rank":"A","suit":"H"}
    Json,       // free JSON (roulette chips), empty = null
    Bool,       // check box ("Stop on pattern")
    Colors,     // diamonds: up to 5 colors picked in order -> "red,blue,..."
    DrillPick,  // drill: the blue / yellow / green drill buttons -> 1 / 2 / 3
    Note        // text only, no value ("No extra parameters — base bet only.")
};

struct ParamSpec
{
    QString key;      // script variable name and BotConfig::gameParams key
    QString label;    // Manual Bet tab label
    ParamType type = ParamType::Int;
    QJsonValue def;   // default value (already in the JSON form the request uses)
    QStringList choices;
    double min = 0.0;
    double max = 0.0;
    QString tip;
    int decimals = 2; // Number
    // Shown only while the input `showWhenKey` has one of `showWhenValues` (primedice X:
    // T3 / T4 only for rollBetweenTwo). Empty = always shown.
    QString showWhenKey;
    QStringList showWhenValues;
    // Placed on the same row as the input `sameRowAs`, after it, with its label in front of it
    // (hilo: "Start card rank: [A]  Suit: [C]"). Empty = its own row.
    QString sameRowAs;
    // Choice: what each item shows (same order as `choices`, which stay the values sent) and
    // its text color ("#rrggbb", empty = the theme's). Empty lists = the values as they are.
    QStringList choiceLabels;
    QStringList choiceColors;
};

// The Manual / Auto Bet tab inputs for `game`, as in the C# bot's game panels (empty for
// dice / limbo). Keys differ from the script variables where the C# panel works differently:
// mines "tiles" (1-based), hilo "hiloPattern" (C# codes) + "rank" / "suit" / "suitMode",
// baccarat "banker" / "player" / "tie" amounts, video poker "strategy".
QList<ParamSpec> paramSpecs(Game game);
QJsonObject defaultParams(Game game);

// Script variables (besides nextbet) a Lua / JavaScript strategy sets for `game`; they are
// read back after the script body and after every dobet(). Names as in the reference bot's
// README: keno {numbers, risk}, mines {fields, mines}, plinko {rows, risk}, ...
QStringList scriptVariables(Game game);

// Checks the values a script set for `game` and works out the amount actually bet:
//   baccarat  nextbet may be {player=, banker=, tie=} (each side's stake; total = sum) or a
//             number (all of it on params "side", default player),
//   roulette  the amounts of the `chips` ({value=, amount=}); nextbet is not needed,
//   others    nextbet.
// `params` holds the variables read (scriptVariables()); nextbet's per-side amounts are
// added to it. Returns false with `error` when something required is missing.
bool finishScriptParams(Game game, const QJsonValue &nextbet, QJsonObject &params,
                        double &totalBet, QString &error);

// Code mode has no defaults: the script must set every variable its game uses. Returns the
// names still missing (empty = ready to bet). `params` = the variables read (unset ones are
// absent), `hasRound` = the script defines round(). Hilo: startcard + round();
// blackjack: round(); primedice X: target3 / target4 only for rollBetweenTwo; baccarat: side
// only when nextbet is a number; video poker: target is optional (nil / null = optimal hold).
QStringList missingScriptVariables(Game game, const QJsonObject &params, const QJsonValue &nextbet,
                                   bool hasRound);

// Games whose settings fix the stake: baccarat with banker / player / tie amounts, roulette
// with chips that carry amounts. Returns that total and sets `fixed`; the base bet / nextbet
// is not used for them (like the C# bot).
double fixedStake(Game game, const QJsonObject &params, bool *fixed);

// A game setting that stops the run after this settled bet (C# bot): diamonds "Stop on
// pattern", hilo "Stop on". Empty = keep going.
QString stopReason(Game game, const QJsonObject &bet, const QJsonObject &params);

// ---- requests --------------------------------------------------------------------------
// The first request of a bet: endpoint (relative to the Stake mirror) + JSON body.
bool buildBetRequest(Game game, double amount, const QString &currency, const QJsonObject &params,
                     QString *endpoint, QJsonObject *body, QString *error);

// Games that can be left unfinished (an "existingGame" error then names the open one):
// the active-bet query slug ("hilo", "blackjack", "videoPoker", "mines"), else empty.
QString activeBetSlug(Game game);

// True while `bet` still needs another request before it is settled (hilo / blackjack /
// video poker / mines still active, blue samurai with special or bonus spins left).
bool isUnfinished(Game game, const QJsonObject &bet);

// Hilo and blackjack ask the script's round() function for the next move.
bool usesRoundFunction(Game game);

struct Action
{
    QString endpoint;
    QJsonObject body;
    QString description; // short text for the log ("hilo: higher", "blackjack: stand")
};

// The next request for an unfinished bet.
//   scriptChoice   what round() returned (undefined = no round() / no script):
//                  hilo:      HILO_BET_HIGH (5) / HILO_BET_LOW (4) / HILO_BET_EQUAL (2) /
//                             HILO_SKIP (7) / HILO_CASHOUT (3), or the guess as a string
//                  blackjack: "BLACKJACK_HIT" / "_STAND" / "_DOUBLE" / "_SPLIT" /
//                             "_INSURANCE" / "_NOINSURANCE" (or "hit", "stand", ...)
//                  Without a choice: hilo plays the `pattern` setting (then cashes out),
//                  blackjack a simple strategy (no insurance, hit below 17).
//   fallbackLevel  blackjack after "invalid action": 1 = noInsurance, 2 = stand.
bool nextAction(Game game, const QJsonObject &bet, const QJsonObject &params,
                const QJsonValue &scriptChoice, int fallbackLevel, Action *out, QString *error);

// ---- results ---------------------------------------------------------------------------
// Code mode: what lastBet.roll / lastBet.Roll hold for `game` when it is not the numeric roll:
// video poker the hand's outcome ("twoPair"), mines the mine positions and keno the drawn
// numbers (both 1-based, like the inputs). Undefined = use the numeric roll.
QJsonValue scriptRoll(Game game, const QJsonObject &bet);

// Code mode: lastBet.result = the game's own result as Stake reports it (bet.state.result:
// baccarat "banker" / "player" / "tie", roulette's number, the dice / primedice roll, the limbo
// multiplier, ...). Games without one: scriptRoll(), else `roll` (also in simulation).
QJsonValue scriptResult(Game game, const QJsonObject &bet, double roll);

struct Summary
{
    double roll = 0.0;   // numeric "roll" (scripts' lastBet.Roll / roll)
    QString targetText;  // bet list Target column
    QString rollText;    // bet list Roll column
};
Summary summarize(Game game, const QJsonObject &bet, const QJsonObject &params);

// ---- video poker -----------------------------------------------------------------------
// Cards to hold from `hand` ([{rank, suit}, ...]): the reference bot's targeted hold when
// `target` is a non-empty array of combos ({"A":3,"K":2}), else its 9/6 Jacks-or-Better
// strategy ("optimal"). `reason` gets the strategy's explanation.
QJsonArray videoPokerHold(const QJsonArray &hand, const QJsonValue &target, QString *reason);
// The label of the first combo of `combos` the hand contains ("3xA + 2xK"), else empty.
QString checkTargetHit(const QJsonArray &hand, const QJsonValue &combos);
// JavaScript source of the poker helpers: evaluates to a function(globalObject) that defines
// getOptimalHold, getTargetedHold, checkTargetHit and evaluateHand on it.
const char *pokerHelpersJs();
} // namespace StakeGames
