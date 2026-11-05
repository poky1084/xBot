#include "bot/StakeClient.h"

#include "bot/BotTypes.h"

#include "bot/DuelTransport.h"
#include "bot/GoatedTransport.h"
#include "bot/ParadiceTransport.h"
#include "bot/PasinoTransport.h"
#include "bot/TowerTransport.h"
#include "bot/WinnaTransport.h"
#include "bot/ShuffleTransport.h"
#include "bot/StakeGames.h"
#include "bot/WolfbetTransport.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QRandomGenerator>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
double numberOf(const QJsonValue &v)
{
    if (v.isDouble())
        return v.toDouble();
    if (v.isString())
        return v.toString().toDouble();
    return 0.0;
}

QString firstErrorText(const QJsonObject &json, QString *errType)
{
    const QJsonArray errs = json.value(QStringLiteral("errors")).toArray();
    if (errs.isEmpty())
        return {};
    const QJsonObject e = errs.first().toObject();
    const QString type = e.value(QStringLiteral("errorType")).toString();
    QString msg = e.value(QStringLiteral("message")).toString();
    if (msg.isEmpty())
        msg = type;
    if (msg.isEmpty())
        msg = QStringLiteral("API error");
    if (errType)
        *errType = type;
    return msg;
}

bool containsCI(const QString &haystack, const char *needle)
{
    return haystack.contains(QLatin1String(needle), Qt::CaseInsensitive);
}

// Server answered with an error and the bet did not go through, so repeating
// it is safe. (Same rules as the C# bot's IsParallelBetError / IsRateLimitError
// / IsSeedRotateError.)
bool isRetryable(const QString &type, const QString &msg)
{
    const bool parallel = (containsCI(type, "parallelCasinoBet") && !containsCI(type, "internalError"))
                          || containsCI(msg, "only one bet at a time")   // Wolfbet
                          || containsCI(msg, "concurrent requests");     // Goated
    const bool rateLimit = containsCI(msg, "slow down") || containsCI(msg, "slowdown") // Paradice: "slowdown_please"
                           || msg.contains(QLatin1String("429"))
                           || containsCI(msg, "too many attempts"); // Wolfbet / Laravel throttle
    const bool seedRotate = containsCI(msg, "new seed pair") || containsCI(msg, "choose a new seed")
                            || containsCI(msg, "seedRotate") || containsCI(type, "seedRotate")
                            || (containsCI(msg, "seed") && containsCI(msg, "rotat"));
    return parallel || rateLimit || seedRotate;
}

// Short retry delay for errors that clear immediately; 0 for rate limits (keep the
// engine's growing backoff there) and for everything else.
int fastRetryDelayMs(const QString &type, const QString &msg)
{
    const bool rateLimit = containsCI(msg, "slow down") || containsCI(msg, "slowdown") // Paradice: "slowdown_please"
                           || msg.contains(QLatin1String("429"))
                           || containsCI(msg, "too many attempts");
    if (rateLimit)
        return 0;
    if ((containsCI(type, "parallelCasinoBet") && !containsCI(type, "internalError"))
        || containsCI(msg, "only one bet at a time") || containsCI(msg, "concurrent requests"))
        return 40;
    if (containsCI(msg, "new seed pair") || containsCI(msg, "choose a new seed")
        || containsCI(msg, "seedRotate") || containsCI(type, "seedRotate")
        || (containsCI(msg, "seed") && containsCI(msg, "rotat")))
        return 100;
    return 0;
}

// The API wraps the bet in one property ({"diceRoll": {...}}), optionally
// inside {"data": {...}}. Pick the first object that looks like a bet.
QJsonObject findBetObject(const QJsonObject &json)
{
    QJsonObject container = json;
    if (json.value(QStringLiteral("data")).isObject())
        container = json.value(QStringLiteral("data")).toObject();

    for (auto it = container.constBegin(); it != container.constEnd(); ++it) {
        if (!it.value().isObject())
            continue;
        const QJsonObject o = it.value().toObject();
        if (o.contains(QStringLiteral("payoutMultiplier")) || o.contains(QStringLiteral("state")))
            return o;
    }
    return {};
}

// The app's multiplier is 99 / win chance (1 % house edge). A casino whose dice pays
// `rtp` / chance (Duel 99.9, Tower 98.5, Pasino 95) gets the chance that pays the same
// multiplier: appChance * rtp / 99, rounded to 2 decimals (every casino's target uses 2).
// Example: 2x = 49.50 % in the app -> Tower 49.25 %, Pasino 47.50 %, Duel 49.95 %.
// Casinos with the same 1 % edge as the app (Stake, Shuffle, Wolfbet, Goated, Paradice, Winna) get the
// app's target as it is (2x = over 50.50 / under 49.50).
double chanceForRtp(double appChance, double rtp, int decimals)
{
    const double scale = std::pow(10.0, decimals);
    return std::round(appChance * rtp / 99.0 * scale) / scale;
}

// The app's chance for a dice request (target rounded to 2 decimals like the panels show it).
double appChanceOf(const DiceBetRequest &request)
{
    const double target = std::round(request.target * 100.0) / 100.0;
    return request.over ? 100.0 - target : target;
}

LiveBetResult failed(const QString &error, const QString &type = {},
                     bool retryable = false, bool unknown = false)
{
    LiveBetResult r;
    r.ok = false;
    r.error = error;
    r.errorType = type;
    r.retryable = retryable;
    r.outcomeUnknown = unknown;
    return r;
}

// ---------------------------------------------------------------- Shuffle blackjack helpers
// Shuffle numbers its cards 0..51 as rank * 4 + suit, ranks 2..A (Stake's order: 0 = 2♦,
// 1 = 2♥, 2 = 2♠, 3 = 2♣, 4 = 3♦, ..., 51 = A♣).
QJsonObject shuffleCard(int n)
{
    static const char *ranks[] = {"2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K", "A"};
    static const char *suits[] = {"D", "H", "S", "C"};
    n = std::clamp(n, 0, 51);
    QJsonObject card;
    card.insert(QStringLiteral("rank"), QLatin1String(ranks[n / 4]));
    card.insert(QStringLiteral("suit"), QLatin1String(suits[n % 4]));
    return card;
}

int blackjackValue(const QJsonArray &cards)
{
    int total = 0;
    int aces = 0;
    for (const QJsonValue &c : cards) {
        const QString r = c.toObject().value(QStringLiteral("rank")).toString();
        if (r == QLatin1String("A")) {
            ++aces;
            total += 11;
        } else if (r == QLatin1String("J") || r == QLatin1String("Q") || r == QLatin1String("K")) {
            total += 10;
        } else {
            total += r.toInt();
        }
    }
    while (total > 21 && aces-- > 0)
        total -= 10;
    return total;
}

QString shuffleToStakeAction(const QString &a)
{
    const QString u = a.toUpper();
    if (u == QLatin1String("HIT")) return QStringLiteral("hit");
    if (u == QLatin1String("STAND")) return QStringLiteral("stand");
    if (u.startsWith(QLatin1String("DOUBLE"))) return QStringLiteral("double");
    if (u == QLatin1String("SPLIT")) return QStringLiteral("split");
    if (u == QLatin1String("REJECT_INSURANCE")) return QStringLiteral("noInsurance");
    if (u == QLatin1String("BUY_INSURANCE")) return QStringLiteral("insurance");
    return a.toLower();
}

// One hand in Stake's shape: {cards, value, actions} with Stake's "deal" first and its
// "bust" / "blackjack" / "full" markers.
QJsonObject stakeHandOf(const QJsonArray &cards, const QJsonArray &shuffleActions, bool split,
                        const QStringList &extraActions = {});

QJsonObject stakeHand(const QJsonArray &shuffleCards, const QJsonArray &shuffleActions, bool split,
                      const QStringList &extraActions = {})
{
    QJsonArray cards;
    for (const QJsonValue &v : shuffleCards)
        cards.append(shuffleCard(v.toInt()));
    return stakeHandOf(cards, shuffleActions, split, extraActions);
}

// One hand from {rank, suit} cards and Shuffle / Goated action names (HIT, DOUBLE_DOWN, ...).
QJsonObject stakeHandOf(const QJsonArray &cards, const QJsonArray &shuffleActions, bool split,
                        const QStringList &extraActions)
{
    QJsonArray actions;
    actions.append(QStringLiteral("deal"));
    for (const QString &a : extraActions)
        actions.append(a);
    for (const QJsonValue &a : shuffleActions)
        actions.append(shuffleToStakeAction(a.toString()));
    const int value = blackjackValue(cards);
    if (value > 21)
        actions.append(QStringLiteral("bust"));
    else if (value == 21 && cards.size() == 2 && !split)
        actions.append(QStringLiteral("blackjack"));
    else if (value == 21)
        actions.append(QStringLiteral("full"));
    QJsonObject hand;
    hand.insert(QStringLiteral("cards"), cards);
    hand.insert(QStringLiteral("value"), value);
    hand.insert(QStringLiteral("actions"), actions);
    return hand;
}

// A Shuffle blackjack bet (BlackjackBetFields) as a Stake blackjack bet:
// {id, amount, payout, payoutMultiplier, active, currency, state: {player: [...], dealer: [...]}}.
QJsonObject shuffleBlackjackAsStake(const QJsonObject &bet)
{
    const QJsonArray steps = bet.value(QStringLiteral("shuffleOriginalActions")).toArray();
    const QJsonObject bj = steps.isEmpty()
        ? QJsonObject()
        : steps.last().toObject().value(QStringLiteral("action")).toObject()
              .value(QStringLiteral("blackjack")).toObject();
    const bool active = bet.value(QStringLiteral("completedAt")).isNull()
                        || bet.value(QStringLiteral("completedAt")).toString().isEmpty();

    const QJsonArray dealerCards = bj.value(QStringLiteral("dealerHand")).toArray();
    const QJsonArray splitCards = bj.value(QStringLiteral("splitPlayerHand")).toArray();
    const bool split = !splitCards.isEmpty();

    // Insurance: Shuffle's insuranceStatus is ELIGIBLE while it is offered, then REJECTED or
    // BOUGHT_... (the BUY_INSURANCE / REJECT_INSURANCE action is in mainPlayerActions too).
    // When it is not offered (INELIGIBLE), record a decision so an ace up-card does not ask.
    QStringList insurance;
    const bool dealerAce = !dealerCards.isEmpty() && dealerCards.first().toInt() / 4 == 12;
    const QString ins = bj.value(QStringLiteral("insuranceStatus")).toString().toUpper();
    bool decided = false;
    for (const QJsonValue &a : bj.value(QStringLiteral("mainPlayerActions")).toArray())
        decided = decided || a.toString().toUpper().endsWith(QLatin1String("_INSURANCE"));
    if (!decided) {
        if (ins.startsWith(QLatin1String("BOUGHT")))
            insurance << QStringLiteral("insurance");
        else if (dealerAce && ins != QLatin1String("ELIGIBLE"))
            insurance << QStringLiteral("noInsurance"); // INELIGIBLE / REJECTED / none
    }

    QJsonArray player;
    player.append(stakeHand(bj.value(QStringLiteral("mainPlayerHand")).toArray(),
                            bj.value(QStringLiteral("mainPlayerActions")).toArray(), split, insurance));
    if (split)
        player.append(stakeHand(splitCards, bj.value(QStringLiteral("splitPlayerActions")).toArray(), true));

    QJsonArray dealerActions;
    dealerActions.append(QStringLiteral("deal"));
    if (!active && dealerCards.size() > 1) {
        dealerActions.append(QStringLiteral("reveal"));
        for (int i = 2; i < dealerCards.size(); ++i)
            dealerActions.append(QStringLiteral("hit"));
    }
    QJsonObject dealer = stakeHand(dealerCards, {}, true);
    dealer.insert(QStringLiteral("actions"), dealerActions);

    QJsonObject state;
    state.insert(QStringLiteral("player"), player);
    state.insert(QStringLiteral("dealer"), QJsonArray{dealer});

    const double amount = numberOf(bet.value(QStringLiteral("amount")));
    const double payout = numberOf(bet.value(QStringLiteral("payout")));
    double mult = 0.0;
    if (!active) {
        const QJsonValue m = bet.value(QStringLiteral("multiplier"));
        mult = m.isNull() || m.isUndefined() ? (amount > 0.0 ? payout / amount : 0.0) : numberOf(m);
    }

    QJsonObject out;
    out.insert(QStringLiteral("id"), bet.value(QStringLiteral("id")).toString());
    out.insert(QStringLiteral("amount"), amount);
    out.insert(QStringLiteral("payout"), payout);
    out.insert(QStringLiteral("payoutMultiplier"), mult);
    out.insert(QStringLiteral("active"), active);
    out.insert(QStringLiteral("currency"), bet.value(QStringLiteral("currency")).toString().toLower());
    out.insert(QStringLiteral("game"), QStringLiteral("blackjack"));
    out.insert(QStringLiteral("state"), state);
    // Shuffle's own outcome words ("WIN", "LOSS", "PUSH", ...), for reference.
    out.insert(QStringLiteral("mainHandOutcome"), bj.value(QStringLiteral("mainHandOutcome")));
    out.insert(QStringLiteral("splitHandOutcome"), bj.value(QStringLiteral("splitHandOutcome")));
    return out;
}

// Goated card "10S" / "QH" / "AD" -> {rank, suit}.
QJsonObject goatedCard(const QString &code)
{
    const QString c = code.trimmed().toUpper();
    QJsonObject card;
    card.insert(QStringLiteral("rank"), c.left(c.size() - 1));
    card.insert(QStringLiteral("suit"), c.right(1));
    return card;
}

// A Goated blackjack bet (`data` of its answers) as a Stake blackjack bet, plus
// "allowedActions" (Stake's names) from Goated's allowed_actions for the Manual tab buttons.
QJsonObject goatedBlackjackAsStake(const QJsonObject &bet)
{
    const QJsonObject st = bet.value(QStringLiteral("state")).toObject();
    const bool active = bet.value(QStringLiteral("is_active")).toBool(false);
    const auto cardsOf = [](const QJsonValue &v) {
        QJsonArray out;
        for (const QJsonValue &c : v.toArray())
            out.append(goatedCard(c.toString()));
        return out;
    };
    const QJsonArray dealerCards = cardsOf(st.value(QStringLiteral("dealer_hand")));
    const QJsonArray splitCards = cardsOf(st.value(QStringLiteral("split_player_hand")));
    const bool split = !splitCards.isEmpty();

    QJsonArray allowed;
    bool insuranceOffered = false;
    for (const QJsonValue &a : st.value(QStringLiteral("allowed_actions")).toArray()) {
        const QString name = shuffleToStakeAction(a.toString());
        allowed.append(name);
        insuranceOffered = insuranceOffered || name == QLatin1String("insurance")
                           || name == QLatin1String("noInsurance");
    }
    const QString ins = st.value(QStringLiteral("insurance_status")).toString().toUpper();
    insuranceOffered = insuranceOffered || ins == QLatin1String("ELIGIBLE") || ins == QLatin1String("OFFERED")
                       || ins == QLatin1String("PENDING");

    QStringList insurance;
    bool decided = false;
    for (const QJsonValue &a : st.value(QStringLiteral("main_player_actions")).toArray())
        decided = decided || a.toString().toUpper().endsWith(QLatin1String("_INSURANCE"));
    const bool dealerAce = !dealerCards.isEmpty()
                           && dealerCards.first().toObject().value(QStringLiteral("rank")).toString() == QLatin1String("A");
    if (!decided) {
        if (ins.startsWith(QLatin1String("BOUGHT")))
            insurance << QStringLiteral("insurance");
        else if (dealerAce && !(active && insuranceOffered))
            insurance << QStringLiteral("noInsurance"); // INELIGIBLE / REJECTED
    }

    QJsonArray player;
    player.append(stakeHandOf(cardsOf(st.value(QStringLiteral("main_player_hand"))),
                              st.value(QStringLiteral("main_player_actions")).toArray(), split, insurance));
    if (split)
        player.append(stakeHandOf(splitCards, st.value(QStringLiteral("split_player_actions")).toArray(), true));

    QJsonArray dealerActions;
    dealerActions.append(QStringLiteral("deal"));
    if (!active && dealerCards.size() > 1) {
        dealerActions.append(QStringLiteral("reveal"));
        for (int i = 2; i < dealerCards.size(); ++i)
            dealerActions.append(QStringLiteral("hit"));
    }
    QJsonObject dealer = stakeHandOf(dealerCards, {}, true);
    dealer.insert(QStringLiteral("actions"), dealerActions);

    QJsonObject state;
    state.insert(QStringLiteral("player"), player);
    state.insert(QStringLiteral("dealer"), QJsonArray{dealer});

    // The stake: the doubled / split total when Goated reports it.
    double amount = numberOf(bet.value(QStringLiteral("amount")));
    const double updated = numberOf(bet.value(QStringLiteral("updated_bet_amount")));
    if (updated > amount)
        amount = updated;
    const double payout = numberOf(bet.value(QStringLiteral("payout")));
    double mult = 0.0;
    if (!active) {
        const QJsonValue m = bet.value(QStringLiteral("multiplier"));
        mult = m.isNull() || m.isUndefined() ? (amount > 0.0 ? payout / amount : 0.0) : numberOf(m);
    }

    QJsonObject out;
    out.insert(QStringLiteral("id"), bet.value(QStringLiteral("id")).toString());
    out.insert(QStringLiteral("amount"), amount);
    out.insert(QStringLiteral("payout"), payout);
    out.insert(QStringLiteral("payoutMultiplier"), mult);
    out.insert(QStringLiteral("active"), active);
    out.insert(QStringLiteral("currency"), bet.value(QStringLiteral("currency")).toString().toLower());
    out.insert(QStringLiteral("game"), QStringLiteral("blackjack"));
    out.insert(QStringLiteral("state"), state);
    if (active)
        out.insert(QStringLiteral("allowedActions"), allowed);
    if (bet.contains(QStringLiteral("nonce")))
        out.insert(QStringLiteral("nonce"), bet.value(QStringLiteral("nonce")));
    out.insert(QStringLiteral("mainHandOutcome"), st.value(QStringLiteral("main_hand_outcome")));
    out.insert(QStringLiteral("splitHandOutcome"), st.value(QStringLiteral("split_hand_outcome")));
    return out;
}

// A Goated hilo bet as a Stake hilo bet: card_sequence "JD,2D,10S" (the start card first),
// player_choices "SH" (one letter per drawn card: H = same-or-higher, L = same-or-lower,
// S = skip) and multipliers (one per drawn card). *lastRank = the card the next guess is about.
QJsonObject goatedHiloAsStake(const QJsonObject &bet, QString *lastRank)
{
    const QStringList cards = bet.value(QStringLiteral("card_sequence")).toString()
                                  .split(QLatin1Char(','), Qt::SkipEmptyParts);
    const QString choices = bet.value(QStringLiteral("player_choices")).toString().toUpper();
    const QJsonArray mults = bet.value(QStringLiteral("multipliers")).toArray();
    const bool active = bet.value(QStringLiteral("is_active")).toBool(false);
    const QJsonValue mv = bet.value(QStringLiteral("multiplier"));
    const double finalMult = mv.isNull() || mv.isUndefined() ? 0.0 : numberOf(mv);
    const bool lost = !active && !(finalMult > 0.0);

    QJsonObject startCard = cards.isEmpty() ? QJsonObject() : goatedCard(cards.first());
    QString rank = startCard.value(QStringLiteral("rank")).toString();
    QJsonArray rounds;
    double current = 0.0;
    for (int i = 1; i < cards.size(); ++i) {
        const QChar c = i - 1 < choices.size() ? choices.at(i - 1) : QChar();
        QString guess;
        if (c == QLatin1Char('S'))
            guess = QStringLiteral("skip");
        else if (c == QLatin1Char('H'))
            guess = rank == QLatin1String("A") ? QStringLiteral("higher")
                  : rank == QLatin1String("K") ? QStringLiteral("equal") : QStringLiteral("higherEqual");
        else if (c == QLatin1Char('L'))
            guess = rank == QLatin1String("K") ? QStringLiteral("lower")
                  : rank == QLatin1String("A") ? QStringLiteral("equal") : QStringLiteral("lowerEqual");
        if (i - 1 < mults.size())
            current = numberOf(mults.at(i - 1));
        if (lost && i == cards.size() - 1 && guess != QLatin1String("skip"))
            current = 0.0; // the losing card
        const QJsonObject card = goatedCard(cards.at(i));
        QJsonObject round;
        round.insert(QStringLiteral("card"), card);
        round.insert(QStringLiteral("guess"), guess);
        round.insert(QStringLiteral("payoutMultiplier"), current);
        rounds.append(round);
        rank = card.value(QStringLiteral("rank")).toString();
    }
    QJsonObject state;
    state.insert(QStringLiteral("startCard"), startCard);
    state.insert(QStringLiteral("rounds"), rounds);
    QJsonObject out;
    out.insert(QStringLiteral("id"), bet.value(QStringLiteral("id")).toString());
    out.insert(QStringLiteral("amount"), numberOf(bet.value(QStringLiteral("amount"))));
    out.insert(QStringLiteral("payout"), numberOf(bet.value(QStringLiteral("payout"))));
    out.insert(QStringLiteral("payoutMultiplier"), active ? 0.0 : finalMult);
    out.insert(QStringLiteral("active"), active);
    out.insert(QStringLiteral("currency"), bet.value(QStringLiteral("currency")).toString().toLower());
    out.insert(QStringLiteral("game"), QStringLiteral("hilo"));
    out.insert(QStringLiteral("state"), state);
    if (bet.contains(QStringLiteral("nonce")))
        out.insert(QStringLiteral("nonce"), bet.value(QStringLiteral("nonce")));
    if (lastRank)
        *lastRank = rank;
    return out;
}

// ---------------------------------------------------------------- Wolfbet hilo helpers
// Wolfbet numbers its cards 0-51: suit * 13 + rank, suits diamonds, hearts, spades, clubs
// (0-3) and ranks A, 2, ... 10, J, Q, K (0-12). 4 = 5 of diamonds, 23 = J of hearts, 27 = 2 of
// spades, 36 = J of spades, 48 = 10 of clubs.
const QStringList &wolfbetRanks()
{
    static const QStringList r = {QStringLiteral("A"), QStringLiteral("2"), QStringLiteral("3"),
                                  QStringLiteral("4"), QStringLiteral("5"), QStringLiteral("6"),
                                  QStringLiteral("7"), QStringLiteral("8"), QStringLiteral("9"),
                                  QStringLiteral("10"), QStringLiteral("J"), QStringLiteral("Q"),
                                  QStringLiteral("K")};
    return r;
}
const QString kWolfbetSuits = QStringLiteral("DHSC");

QJsonObject wolfbetCard(int n)
{
    QJsonObject card;
    if (n < 0 || n > 51)
        return card;
    card.insert(QStringLiteral("rank"), wolfbetRanks().at(n % 13));
    card.insert(QStringLiteral("suit"), QString(kWolfbetSuits.at(n / 13)));
    return card;
}

// {rank, suit} (Stake's start card) -> 0-51, -1 when it is not a card.
int wolfbetCardNumber(const QJsonObject &card)
{
    QString rank = card.value(QStringLiteral("rank")).toString().trimmed().toUpper();
    if (rank == QLatin1String("T"))
        rank = QStringLiteral("10");
    const int r = int(wolfbetRanks().indexOf(rank));
    const int s = int(kWolfbetSuits.indexOf(card.value(QStringLiteral("suit")).toString().trimmed().toUpper()));
    return r < 0 || s < 0 ? -1 : s * 13 + r;
}

// Ranks a "high" / "low" guess wins on, out of 13. "high" = higher or same, "low" = lower or
// same; on an ace "high" is strictly higher (and "low" the ace again), on a king "low" is
// strictly lower (and "high" the king again) - the same as Goated's H / L.
int wolfbetHiloWinningRanks(const QString &rule, int rank)
{
    if (rule == QLatin1String("high"))
        return rank == 0 ? 12 : 13 - rank;
    if (rule == QLatin1String("low"))
        return rank == 12 ? 12 : rank + 1;
    return 0;
}

// Whether a Wolfbet hilo round is still open. Known: "status" 1 = cashed out (finished), 3 = open
// with guesses made, 4 = just started (no guesses yet); a guess (not a skip) with a status other than 1 lost the round. A round
// just started (no guesses yet) may carry another status, so anything else counts as open.
bool wolfbetHiloOpen(const QJsonObject &bet)
{
    if (bet.isEmpty() || int(numberOf(bet.value(QStringLiteral("status")))) == 1)
        return false;
    for (const QJsonValue &v : bet.value(QStringLiteral("bets")).toArray()) {
        const QJsonObject sub = v.toObject();
        if (sub.value(QStringLiteral("rule")).toString().toLower() != QLatin1String("skip")
            && int(numberOf(sub.value(QStringLiteral("status")))) != 1)
            return false; // a lost guess ends the round
    }
    return true;
}

// A Wolfbet hilo bet ({"hash", "status": 3 while open, "amount", "profit", "initial_bet_value",
// "bets": [{"status": 1 = won, "multiplier" (the round's so far), "bet_value", "result_value",
// "rule", "sub_nonce"}]}) as a Stake hilo bet. *lastCard = the card the next guess is about,
// *steps = the guesses made so far.
QJsonObject wolfbetHiloAsStake(const QJsonObject &bet, int *lastCard, int *steps)
{
    const bool active = wolfbetHiloOpen(bet);
    const double amount = numberOf(bet.value(QStringLiteral("amount")));
    const double profit = numberOf(bet.value(QStringLiteral("profit")));
    const QJsonArray subs = bet.value(QStringLiteral("bets")).toArray();

    int card = int(numberOf(bet.value(QStringLiteral("initial_bet_value"))));
    const QJsonObject startCard = wolfbetCard(card);
    bool lostGuess = false;
    double current = 0.0;
    QJsonArray rounds;
    for (const QJsonValue &v : subs) {
        const QJsonObject sub = v.toObject();
        const QString rule = sub.value(QStringLiteral("rule")).toString().toLower();
        const QString rank = wolfbetCard(card).value(QStringLiteral("rank")).toString();
        QString guess = rule;
        if (rule == QLatin1String("high"))
            guess = rank == QLatin1String("A") ? QStringLiteral("higher")
                  : rank == QLatin1String("K") ? QStringLiteral("equal") : QStringLiteral("higherEqual");
        else if (rule == QLatin1String("low"))
            guess = rank == QLatin1String("K") ? QStringLiteral("lower")
                  : rank == QLatin1String("A") ? QStringLiteral("equal") : QStringLiteral("lowerEqual");
        const bool skipped = rule == QLatin1String("skip");
        const bool won = int(numberOf(sub.value(QStringLiteral("status")))) == 1;
        if (skipped) {
            // A skip only draws a new card: never a loss, the round's multiplier stays (its
            // own "multiplier" is used only when the site reports one).
            const double m = numberOf(sub.value(QStringLiteral("multiplier")));
            if (m > 0.0)
                current = m;
        } else {
            if (!won)
                lostGuess = true;
            current = won ? numberOf(sub.value(QStringLiteral("multiplier"))) : 0.0;
        }
        card = int(numberOf(sub.value(QStringLiteral("result_value"))));
        QJsonObject round;
        round.insert(QStringLiteral("card"), wolfbetCard(card));
        round.insert(QStringLiteral("guess"), guess);
        round.insert(QStringLiteral("payoutMultiplier"), current);
        rounds.append(round);
    }
    const bool lost = !active && (lostGuess || (amount > 0.0 && profit < 0.0));
    const double finalMult = active || lost ? 0.0 : (amount > 0.0 ? (amount + profit) / amount : current);

    QJsonObject state;
    state.insert(QStringLiteral("startCard"), startCard);
    state.insert(QStringLiteral("rounds"), rounds);
    QJsonObject out;
    out.insert(QStringLiteral("id"), bet.value(QStringLiteral("hash")).toString());
    out.insert(QStringLiteral("amount"), amount);
    out.insert(QStringLiteral("payout"), active || lost ? 0.0 : amount + profit);
    out.insert(QStringLiteral("payoutMultiplier"), finalMult);
    out.insert(QStringLiteral("active"), active);
    out.insert(QStringLiteral("currency"), bet.value(QStringLiteral("currency")).toString().toLower());
    out.insert(QStringLiteral("game"), QStringLiteral("hilo"));
    out.insert(QStringLiteral("state"), state);
    if (bet.contains(QStringLiteral("nonce")))
        out.insert(QStringLiteral("nonce"), bet.value(QStringLiteral("nonce")));
    if (lastCard)
        *lastCard = card;
    if (steps)
        *steps = int(subs.size());
    return out;
}

const char kShuffleBlackjackFields[] =
    "fragment BlackjackBetFields on Bet {\n"
    "  id\n  amount\n  originalAmount\n  payout\n  multiplier\n  currency\n  afterBalance\n"
    "  completedAt\n  shuffleOriginalActions {\n    id\n    action {\n      blackjack {\n"
    "        mainPlayerHand\n        mainPlayerActions\n        splitPlayerHand\n"
    "        splitPlayerActions\n        dealerHand\n        perfectPairWin\n"
    "        twentyOnePlusThreeWin\n        twentyOnePlusThreeAmount\n        perfectPairAmount\n"
    "        insuranceStatus\n        originalMainBetAmount\n        mainHandOutcome\n"
    "        splitHandOutcome\n        __typename\n      }\n      __typename\n    }\n"
    "    __typename\n  }\n  __typename\n}";

// ---------------------------------------------------------------- Shuffle hilo helpers
// Card number for Shuffle's HiloStart startCard (same numbering as blackjack).
int shuffleCardNumber(const QString &rank, const QString &suit)
{
    static const QStringList ranks = {QStringLiteral("2"), QStringLiteral("3"), QStringLiteral("4"),
                                      QStringLiteral("5"), QStringLiteral("6"), QStringLiteral("7"),
                                      QStringLiteral("8"), QStringLiteral("9"), QStringLiteral("10"),
                                      QStringLiteral("J"), QStringLiteral("Q"), QStringLiteral("K"),
                                      QStringLiteral("A")};
    static const QStringList suits = {QStringLiteral("D"), QStringLiteral("H"), QStringLiteral("S"),
                                      QStringLiteral("C")};
    const int r = std::max(0, int(ranks.indexOf(rank.toUpper())));
    const int s = std::max(0, int(suits.indexOf(suit.toUpper())));
    return r * 4 + s;
}

// Shuffle's guess on the card of rank `rank` as Stake names it: on an ace SAME_OR_ABOVE pays
// as "higher" and SAME_OR_BELOW as "equal"; on a king SAME_OR_BELOW is "lower" and
// SAME_OR_ABOVE "equal".
QString shuffleHiloGuessAsStake(const QString &guess, const QString &rank)
{
    const QString g = guess.toUpper();
    if (g == QLatin1String("SKIP"))
        return QStringLiteral("skip");
    if (g == QLatin1String("SAME_OR_ABOVE"))
        return rank == QLatin1String("A") ? QStringLiteral("higher")
             : rank == QLatin1String("K") ? QStringLiteral("equal")
                                          : QStringLiteral("higherEqual");
    if (g == QLatin1String("SAME_OR_BELOW"))
        return rank == QLatin1String("K") ? QStringLiteral("lower")
             : rank == QLatin1String("A") ? QStringLiteral("equal")
                                          : QStringLiteral("lowerEqual");
    return guess.toLower();
}

bool shuffleHiloLost(const QString &actionType)
{
    const QString t = actionType.toUpper();
    return t.contains(QLatin1String("INCORRECT")) || t.contains(QLatin1String("WRONG"))
        || t.contains(QLatin1String("LOSS")) || t.contains(QLatin1String("LOST"))
        || t.contains(QLatin1String("BUST"));
}

// A Shuffle hilo bet (HiloBetFields) as a Stake hilo bet:
// {id, amount, payout, payoutMultiplier, active, state: {startCard, rounds: [{card, guess,
// payoutMultiplier}]}}. *lastRank = the rank of the card the next guess is about.
QJsonObject shuffleHiloAsStake(const QJsonObject &bet, QString *lastRank)
{
    const QJsonArray steps = bet.value(QStringLiteral("shuffleOriginalActions")).toArray();
    QJsonObject startCard;
    QJsonArray rounds;
    QString rank;
    double current = 0.0;
    bool lost = false;
    bool cashedOut = false;
    for (int i = 0; i < steps.size(); ++i) {
        const QJsonObject h = steps.at(i).toObject().value(QStringLiteral("action")).toObject()
                                  .value(QStringLiteral("hilo")).toObject();
        const QJsonObject card = shuffleCard(h.value(QStringLiteral("card")).toInt());
        const QString type = h.value(QStringLiteral("actionType")).toString().toUpper();
        if (i == 0 || type == QLatin1String("START")) {
            startCard = card;
            rank = card.value(QStringLiteral("rank")).toString();
            continue;
        }
        if (type.contains(QLatin1String("CASH"))) { // a cash-out step (no new card)
            cashedOut = true;
            continue;
        }
        const QJsonValue wm = h.value(QStringLiteral("winMultiplier"));
        const bool roundLost = shuffleHiloLost(type);
        if (roundLost)
            current = 0.0;
        else if (!wm.isNull() && !wm.isUndefined())
            current = numberOf(wm);
        lost = lost || roundLost;
        QJsonObject round;
        round.insert(QStringLiteral("card"), card);
        round.insert(QStringLiteral("guess"), shuffleHiloGuessAsStake(h.value(QStringLiteral("guess")).toString(), rank));
        round.insert(QStringLiteral("payoutMultiplier"), current);
        rounds.append(round);
        rank = card.value(QStringLiteral("rank")).toString();
    }
    const QJsonValue completed = bet.value(QStringLiteral("completedAt"));
    const double amount = numberOf(bet.value(QStringLiteral("amount")));
    const double payout = numberOf(bet.value(QStringLiteral("payout")));
    const bool done = lost || cashedOut || payout > 0.0
                      || (completed.isString() && !completed.toString().isEmpty());
    double mult = 0.0;
    if (done && !lost)
        mult = amount > 0.0 && payout > 0.0 ? payout / amount : current;

    QJsonObject state;
    state.insert(QStringLiteral("startCard"), startCard);
    state.insert(QStringLiteral("rounds"), rounds);
    QJsonObject out;
    out.insert(QStringLiteral("id"), bet.value(QStringLiteral("id")).toString());
    out.insert(QStringLiteral("amount"), amount);
    out.insert(QStringLiteral("payout"), payout);
    out.insert(QStringLiteral("payoutMultiplier"), mult);
    out.insert(QStringLiteral("active"), !done);
    out.insert(QStringLiteral("currency"), bet.value(QStringLiteral("currency")).toString().toLower());
    out.insert(QStringLiteral("game"), QStringLiteral("hilo"));
    out.insert(QStringLiteral("state"), state);
    if (lastRank)
        *lastRank = rank;
    return out;
}

const char kShuffleHiloFields[] =
    "fragment HiloBetFields on Bet {\n"
    "  id\n  amount\n  payout\n  currency\n  afterBalance\n  completedAt\n"
    "  shuffleOriginalActions {\n    id\n    action {\n      hilo {\n        card\n        guess\n"
    "        winMultiplier\n        actionType\n        __typename\n      }\n      __typename\n"
    "    }\n    __typename\n  }\n  __typename\n}";
} // namespace

StakeClient::StakeClient(QObject *parent)
    : QObject(parent)
{
}

void StakeClient::setTransport(IStakeTransport *transport)
{
    for (const QMetaObject::Connection &c : std::as_const(m_shuffleConnections))
        disconnect(c);
    m_shuffleConnections.clear();

    m_transport = transport;

    if (auto *shuffle = qobject_cast<ShuffleTransport *>(transport)) {
        shuffle->setWantedCurrency(m_currency);
        // Balances are pushed by Shuffle's websocket; hand them to the same signals the
        // Stake GraphQL query uses.
        m_shuffleConnections << connect(shuffle, &ShuffleTransport::balancesUpdated, this,
                                        &StakeClient::balancesReady);
        m_shuffleConnections << connect(shuffle, &ShuffleTransport::balancesFailed, this,
                                        &StakeClient::balancesFailed);
    }
    if (auto *goated = qobject_cast<GoatedTransport *>(transport)) {
        // Goated: balances are read over HTTPS; same signals as the other casinos.
        m_shuffleConnections << connect(goated, &GoatedTransport::balancesUpdated, this,
                                        &StakeClient::balancesReady);
        m_shuffleConnections << connect(goated, &GoatedTransport::balancesFailed, this,
                                        &StakeClient::balancesFailed);
    }
    if (auto *wolfbet = qobject_cast<WolfbetTransport *>(transport)) {
        // Wolfbet: balances are read over HTTPS; same signals as the other casinos.
        m_shuffleConnections << connect(wolfbet, &WolfbetTransport::balancesUpdated, this,
                                        &StakeClient::balancesReady);
        m_shuffleConnections << connect(wolfbet, &WolfbetTransport::balancesFailed, this,
                                        &StakeClient::balancesFailed);
    }
    if (auto *paradice = qobject_cast<ParadiceTransport *>(transport)) {
        // Paradice: balances are read over HTTPS; same signals as the other casinos.
        m_shuffleConnections << connect(paradice, &ParadiceTransport::balancesUpdated, this,
                                        &StakeClient::balancesReady);
        m_shuffleConnections << connect(paradice, &ParadiceTransport::balancesFailed, this,
                                        &StakeClient::balancesFailed);
    }
    if (auto *pasino = qobject_cast<PasinoTransport *>(transport)) {
        // Pasino: balances are read over HTTPS; same signals as the other casinos.
        m_shuffleConnections << connect(pasino, &PasinoTransport::balancesUpdated, this,
                                        &StakeClient::balancesReady);
        m_shuffleConnections << connect(pasino, &PasinoTransport::balancesFailed, this,
                                        &StakeClient::balancesFailed);
    }
    if (auto *tower = qobject_cast<TowerTransport *>(transport)) {
        // Tower: balances are read over HTTPS; same signals as the other casinos.
        m_shuffleConnections << connect(tower, &TowerTransport::balancesUpdated, this,
                                        &StakeClient::balancesReady);
        m_shuffleConnections << connect(tower, &TowerTransport::balancesFailed, this,
                                        &StakeClient::balancesFailed);
    }
    if (auto *winna = qobject_cast<WinnaTransport *>(transport)) {
        // Winna: balances are read over HTTPS; same signals as the other casinos.
        winna->setCoin(m_currency);
        m_shuffleConnections << connect(winna, &WinnaTransport::balancesUpdated, this,
                                        &StakeClient::balancesReady);
        m_shuffleConnections << connect(winna, &WinnaTransport::balancesFailed, this,
                                        &StakeClient::balancesFailed);
    }
    if (auto *duel = qobject_cast<DuelTransport *>(transport)) {
        // Duel: balances are read over HTTPS; same signals as the other casinos.
        m_shuffleConnections << connect(duel, &DuelTransport::balancesUpdated, this,
                                        &StakeClient::balancesReady);
        m_shuffleConnections << connect(duel, &DuelTransport::balancesFailed, this,
                                        &StakeClient::balancesFailed);
    }
}

bool StakeClient::isShuffle() const
{
    return qobject_cast<ShuffleTransport *>(m_transport) != nullptr;
}

bool StakeClient::isGoated() const
{
    return qobject_cast<GoatedTransport *>(m_transport) != nullptr;
}

bool StakeClient::isWolfbet() const
{
    return qobject_cast<WolfbetTransport *>(m_transport) != nullptr;
}

bool StakeClient::isDuel() const
{
    return qobject_cast<DuelTransport *>(m_transport) != nullptr;
}

bool StakeClient::isParadice() const
{
    return qobject_cast<ParadiceTransport *>(m_transport) != nullptr;
}

bool StakeClient::isPasino() const
{
    return qobject_cast<PasinoTransport *>(m_transport) != nullptr;
}

bool StakeClient::isTower() const
{
    return qobject_cast<TowerTransport *>(m_transport) != nullptr;
}

bool StakeClient::isWinna() const
{
    return qobject_cast<WinnaTransport *>(m_transport) != nullptr;
}

bool StakeClient::isStake() const
{
    return m_transport && !isShuffle() && !isGoated() && !isWolfbet() && !isDuel() && !isParadice()
           && !isPasino() && !isTower() && !isWinna();
}

void StakeClient::setCurrency(const QString &currency)
{
    m_currency = currency.trimmed().toLower();
    if (auto *shuffle = qobject_cast<ShuffleTransport *>(m_transport))
        shuffle->setWantedCurrency(m_currency);
    if (auto *winna = qobject_cast<WinnaTransport *>(m_transport))
        winna->setCoin(m_currency); // the game session is started for this coin
}

QString StakeClient::randomIdentifier()
{
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    QString id;
    id.reserve(21);
    for (int i = 0; i < 21; ++i)
        id += QLatin1Char(alphabet[QRandomGenerator::global()->bounded(62)]);
    return id;
}

QString StakeClient::randomClientSeed(int length)
{
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    QString seed;
    seed.reserve(length);
    for (int i = 0; i < length; ++i)
        seed += QLatin1Char(alphabet[QRandomGenerator::system()->bounded(62)]);
    return seed;
}

void StakeClient::rotateSeedPair(const QString &clientSeed)
{
    if (isShuffle()) {
        rotateShuffleSeed(clientSeed);
        return;
    }
    if (isGoated()) {
        rotateGoatedSeed(clientSeed);
        return;
    }
    if (isWolfbet()) {
        rotateWolfbetSeed(); // the client seed cannot be chosen on Wolfbet
        return;
    }
    if (isDuel()) {
        rotateDuelSeed(clientSeed);
        return;
    }
    if (isParadice()) {
        rotateParadiceSeed(); // the client seed cannot be chosen on Paradice
        return;
    }
    if (isPasino()) {
        rotatePasinoSeed(clientSeed); // sent with every bet: nothing to call on the server
        return;
    }
    if (isTower()) {
        rotateTowerSeed(clientSeed);
        return;
    }
    if (isWinna()) {
        rotateWinnaSeed(clientSeed);
        return;
    }

    QString why;
    if (!m_transport || !m_transport->isReady(&why)) {
        const QString msg = m_transport ? why : tr("No transport configured.");
        QTimer::singleShot(0, this, [this, msg] { emit seedRotateFailed(msg); });
        return;
    }

    QJsonObject variables;
    variables.insert(QStringLiteral("seed"), clientSeed);

    QJsonObject body;
    body.insert(QStringLiteral("operationName"), QStringLiteral("RotateSeedPair"));
    body.insert(QStringLiteral("variables"), variables);
    body.insert(QStringLiteral("query"), QStringLiteral(
        "mutation RotateSeedPair($seed: String!) {\n"
        "  rotateSeedPair(seed: $seed) {\n"
        "    clientSeed {\n"
        "      user {\n"
        "        id\n"
        "        activeClientSeed {\n"
        "          id\n"
        "          seed\n"
        "        }\n"
        "        activeServerSeed {\n"
        "          id\n"
        "          nonce\n"
        "          seedHash\n"
        "          nextSeedHash\n"
        "        }\n"
        "      }\n"
        "    }\n"
        "  }\n"
        "}"));

    m_transport->post(QStringLiteral("_api/graphql"), body, [this, clientSeed](const TransportReply &reply) {
        if (!reply.ok) {
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit seedRotateFailed(reply.error);
            return;
        }
        QString type;
        const QString apiError = firstErrorText(reply.json, &type);
        if (!apiError.isEmpty()) {
            emit seedRotateFailed(apiError);
            return;
        }

        const QJsonObject user = reply.json.value(QStringLiteral("data")).toObject()
                                     .value(QStringLiteral("rotateSeedPair")).toObject()
                                     .value(QStringLiteral("clientSeed")).toObject()
                                     .value(QStringLiteral("user")).toObject();
        if (user.isEmpty()) {
            emit seedRotateFailed(tr("Unexpected seed rotation response."));
            return;
        }

        const QString activeSeed = user.value(QStringLiteral("activeClientSeed")).toObject()
                                       .value(QStringLiteral("seed")).toString();
        const QJsonObject server = user.value(QStringLiteral("activeServerSeed")).toObject();
        emit seedRotated(activeSeed.isEmpty() ? clientSeed : activeSeed,
                         qint64(numberOf(server.value(QStringLiteral("nonce")))),
                         server.value(QStringLiteral("seedHash")).toString(),
                         server.value(QStringLiteral("nextSeedHash")).toString());
    });
}

// Shuffle: ChangeGameSeed mutation (gameSeedChangeAndReveal) - reveals the old server
// seed and starts a new seed pair with `clientSeed` (POST https://<site>/main-api/graphql/api/graphql).
// The reply is a list of seeds: the ACTIVE one (new client seed, nonce 0), the NEXT one
// and the revealed old one.
void StakeClient::rotateShuffleSeed(const QString &clientSeed)
{
    QString why;
    if (!m_transport || !m_transport->isReady(&why)) {
        const QString msg = m_transport ? why : tr("No transport configured.");
        QTimer::singleShot(0, this, [this, msg] { emit seedRotateFailed(msg); });
        return;
    }

    QJsonObject variables;
    variables.insert(QStringLiteral("clientSeed"), clientSeed);

    QJsonObject body;
    body.insert(QStringLiteral("operationName"), QStringLiteral("ChangeGameSeed"));
    body.insert(QStringLiteral("variables"), variables);
    QJsonObject clientLibrary;
    clientLibrary.insert(QStringLiteral("name"), QStringLiteral("@apollo/client"));
    clientLibrary.insert(QStringLiteral("version"), QStringLiteral("4.2.9"));
    QJsonObject extensions;
    extensions.insert(QStringLiteral("clientLibrary"), clientLibrary);
    body.insert(QStringLiteral("extensions"), extensions);
    body.insert(QStringLiteral("query"), QStringLiteral(
        "mutation ChangeGameSeed($clientSeed: String!) {\n"
        "  gameSeedChangeAndReveal(newClientSeed: $clientSeed) {\n"
        "    ...GameSeedFields\n"
        "    __typename\n"
        "  }\n"
        "}\n"
        "\n"
        "fragment GameSeedFields on GameSeed {\n"
        "  id\n"
        "  clientSeed\n"
        "  seed\n"
        "  hashedSeed\n"
        "  status\n"
        "  currentNonce\n"
        "  createdAt\n"
        "  __typename\n"
        "}"));

    m_transport->post(QStringLiteral("main-api/graphql/api/graphql"), body,
                      [this, clientSeed](const TransportReply &reply) {
        if (!reply.ok) {
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit seedRotateFailed(reply.error);
            return;
        }
        QString type;
        const QString apiError = firstErrorText(reply.json, &type);
        if (!apiError.isEmpty()) {
            emit seedRotateFailed(apiError);
            return;
        }
        const QJsonValue result = reply.json.value(QStringLiteral("data")).toObject()
                                      .value(QStringLiteral("gameSeedChangeAndReveal"));
        QJsonArray seeds;
        if (result.isArray())
            seeds = result.toArray();
        else if (result.isObject())
            seeds.append(result);
        if (seeds.isEmpty()) {
            emit seedRotateFailed(tr("Unexpected seed rotation response."));
            return;
        }

        QString activeClient = clientSeed;
        QString activeHash;
        QString nextHash;
        qint64 nonce = 0;
        for (const QJsonValue &v : std::as_const(seeds)) {
            const QJsonObject o = v.toObject();
            const QString status = o.value(QStringLiteral("status")).toString();
            if (status == QLatin1String("ACTIVE")) {
                const QString cs = o.value(QStringLiteral("clientSeed")).toString();
                if (!cs.isEmpty())
                    activeClient = cs;
                activeHash = o.value(QStringLiteral("hashedSeed")).toString();
                nonce = qint64(numberOf(o.value(QStringLiteral("currentNonce"))));
            } else if (status == QLatin1String("NEXT")) {
                nextHash = o.value(QStringLiteral("hashedSeed")).toString();
            }
        }
        emit seedRotated(activeClient, nonce, activeHash, nextHash);
    });
}

// Shuffle: VaultDeposit mutation (POST https://<site>/main-api/graphql/api/graphql).
void StakeClient::vaultDeposit(double amount)
{
    if (isGoated()) {
        goatedVaultDeposit(amount);
        return;
    }
    if (isWolfbet()) {
        QTimer::singleShot(0, this, [this] { emit vaultFailed(tr("Wolfbet: vault deposits are not supported.")); });
        return;
    }
    if (isDuel()) {
        QTimer::singleShot(0, this, [this] { emit vaultFailed(tr("Duel: vault deposits are not supported.")); });
        return;
    }
    if (isParadice()) {
        QTimer::singleShot(0, this, [this] { emit vaultFailed(tr("Paradice: vault deposits are not supported.")); });
        return;
    }
    if (isPasino()) {
        QTimer::singleShot(0, this, [this] { emit vaultFailed(tr("Pasino: vault deposits are not supported.")); });
        return;
    }
    if (isTower()) {
        QTimer::singleShot(0, this, [this] { emit vaultFailed(tr("Tower: vault deposits are not supported.")); });
        return;
    }
    if (isWinna()) {
        // POST api2.winna.com/v2/vault/deposit {"coin":"USDC","balance":"<amount>"} -> {"success":true}
        auto *winna = qobject_cast<WinnaTransport *>(m_transport);
        if (!(amount > 0.0)) {
            QTimer::singleShot(0, this, [this] { emit vaultFailed(tr("Please specify vault amount.")); });
            return;
        }
        if (m_currency.isEmpty()) {
            QTimer::singleShot(0, this, [this] {
                emit vaultFailed(tr("No currency selected — log in and pick one in Options."));
            });
            return;
        }
        const QString currency = m_currency.toUpper();
        const double rounded = std::round(amount * 1e8) / 1e8;
        winna->vaultDeposit(m_currency, rounded, [this, rounded, currency](const QString &error) {
            if (!error.isEmpty())
                emit vaultFailed(error);
            else
                emit vaultDeposited(rounded, currency);
        });
        return;
    }
    if (!isShuffle()) {
        stakeVaultDeposit(amount);
        return;
    }
    QString why;
    if (!m_transport || !m_transport->isReady(&why)) {
        const QString msg = m_transport ? why : tr("No transport configured.");
        QTimer::singleShot(0, this, [this, msg] { emit vaultFailed(msg); });
        return;
    }
    if (!(amount > 0.0)) {
        QTimer::singleShot(0, this, [this] { emit vaultFailed(tr("Please specify vault amount.")); });
        return;
    }

    QJsonObject data;
    data.insert(QStringLiteral("currency"), ShuffleTransport::apiCurrency(m_currency));
    data.insert(QStringLiteral("amount"), QString::number(amount, 'f', 8));

    QJsonObject variables;
    variables.insert(QStringLiteral("data"), data);

    QJsonObject body;
    body.insert(QStringLiteral("operationName"), QStringLiteral("VaultDeposit"));
    body.insert(QStringLiteral("variables"), variables);
    QJsonObject clientLibrary;
    clientLibrary.insert(QStringLiteral("name"), QStringLiteral("@apollo/client"));
    clientLibrary.insert(QStringLiteral("version"), QStringLiteral("4.2.9"));
    QJsonObject extensions;
    extensions.insert(QStringLiteral("clientLibrary"), clientLibrary);
    body.insert(QStringLiteral("extensions"), extensions);
    body.insert(QStringLiteral("query"), QStringLiteral(
        "mutation VaultDeposit($data: VaultDepositInput!) {\n"
        "  vaultDeposit(data: $data) {\n"
        "    id\n"
        "    type\n"
        "    currency\n"
        "    amount\n"
        "    createdAt\n"
        "    afterVaultBalance\n"
        "    __typename\n"
        "  }\n"
        "}"));

    m_transport->post(QStringLiteral("main-api/graphql/api/graphql"), body, [this, amount](const TransportReply &reply) {
        if (!reply.ok) {
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit vaultFailed(reply.error);
            return;
        }
        QString type;
        const QString apiError = firstErrorText(reply.json, &type);
        if (!apiError.isEmpty()) {
            emit vaultFailed(apiError);
            return;
        }
        const QJsonObject dep = reply.json.value(QStringLiteral("data")).toObject()
                                    .value(QStringLiteral("vaultDeposit")).toObject();
        if (dep.isEmpty()) {
            emit vaultFailed(tr("Unexpected vault response."));
            return;
        }
        const double done = dep.contains(QStringLiteral("amount"))
                                ? numberOf(dep.value(QStringLiteral("amount"))) : amount;
        emit vaultDeposited(done, dep.value(QStringLiteral("currency")).toString());
    });
}

// Stake: CreateVaultDeposit mutation (POST https://<site>/_api/v1/graphql). The amount is
// a Float and the currency a lowercase code ("usdc").
void StakeClient::stakeVaultDeposit(double amount)
{
    QString why;
    if (!m_transport || !m_transport->isReady(&why)) {
        const QString msg = m_transport ? why : tr("No transport configured.");
        QTimer::singleShot(0, this, [this, msg] { emit vaultFailed(msg); });
        return;
    }
    if (!(amount > 0.0)) {
        QTimer::singleShot(0, this, [this] { emit vaultFailed(tr("Please specify vault amount.")); });
        return;
    }

    QJsonObject variables;
    variables.insert(QStringLiteral("currency"), m_currency.toLower());
    variables.insert(QStringLiteral("amount"), amount);

    QJsonObject body;
    body.insert(QStringLiteral("operationName"), QStringLiteral("CreateVaultDeposit"));
    body.insert(QStringLiteral("variables"), variables);
    body.insert(QStringLiteral("query"), QStringLiteral(
        "mutation CreateVaultDeposit($currency: CurrencyEnum!, $amount: Float!) {\n"
        "  createVaultDeposit(currency: $currency, amount: $amount) {\n"
        "    id\n"
        "    amount\n"
        "    currency\n"
        "    __typename\n"
        "  }\n"
        "}"));

    m_transport->post(QStringLiteral("_api/v1/graphql"), body, [this, amount](const TransportReply &reply) {
        if (!reply.ok) {
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit vaultFailed(reply.error);
            return;
        }
        QString type;
        const QString apiError = firstErrorText(reply.json, &type);
        if (!apiError.isEmpty()) {
            emit vaultFailed(apiError);
            return;
        }
        const QJsonObject dep = reply.json.value(QStringLiteral("data")).toObject()
                                    .value(QStringLiteral("createVaultDeposit")).toObject();
        if (dep.isEmpty()) {
            emit vaultFailed(tr("Unexpected vault response."));
            return;
        }
        const double done = dep.contains(QStringLiteral("amount"))
                                ? numberOf(dep.value(QStringLiteral("amount"))) : amount;
        emit vaultDeposited(done, dep.value(QStringLiteral("currency")).toString().toUpper());
    });
}

void StakeClient::fetchBalances()
{
    QString why;
    if (!m_transport || !m_transport->isReady(&why)) {
        const QString msg = m_transport ? why : tr("No transport configured.");
        QTimer::singleShot(0, this, [this, msg] { emit balancesFailed(msg); });
        return;
    }

    // Shuffle: connect / reuse the balance websocket; results arrive through the
    // transport's signals (see setTransport()).
    if (auto *shuffle = qobject_cast<ShuffleTransport *>(m_transport)) {
        shuffle->requestBalances();
        return;
    }
    // Goated: GET /user/wallet/balances; results arrive through the transport's signals.
    if (auto *goated = qobject_cast<GoatedTransport *>(m_transport)) {
        goated->requestBalances();
        return;
    }
    // Wolfbet: GET /api/v2/user/balances; results arrive through the transport's signals.
    if (auto *wolfbet = qobject_cast<WolfbetTransport *>(m_transport)) {
        wolfbet->requestBalances();
        return;
    }
    // Duel: GET /api/v2/metadata (user.balances); results arrive through the transport's signals.
    if (auto *duel = qobject_cast<DuelTransport *>(m_transport)) {
        duel->requestBalances();
        return;
    }
    // Paradice: GraphQL "me { wallets }"; results arrive through the transport's signals.
    if (auto *paradice = qobject_cast<ParadiceTransport *>(m_transport)) {
        paradice->requestBalances();
        return;
    }
    // Pasino: POST /coin/get-balances; results arrive through the transport's signals.
    if (auto *pasino = qobject_cast<PasinoTransport *>(m_transport)) {
        pasino->requestBalances();
        return;
    }
    // Tower: GET /api/accounts; results arrive through the transport's signals.
    if (auto *tower = qobject_cast<TowerTransport *>(m_transport)) {
        tower->requestBalances();
        return;
    }
    // Winna: GET api2.winna.com/v1/payments/wallets; results arrive through the transport's signals.
    if (auto *winna = qobject_cast<WinnaTransport *>(m_transport)) {
        winna->requestBalances();
        return;
    }

    postBalanceQuery(m_transport, false);
}

void StakeClient::fetchBalancesVia(IStakeTransport *via)
{
    if (!via || via == m_transport || !via->isReady())
        return; // only for a Stake transport that is not the active one
    postBalanceQuery(via, true);
}

void StakeClient::postBalanceQuery(IStakeTransport *via, bool background)
{
    QJsonObject body;
    body.insert(QStringLiteral("operationName"), QStringLiteral("UserBalances"));
    body.insert(QStringLiteral("variables"), QJsonObject());
    body.insert(QStringLiteral("query"),
                QStringLiteral("query UserBalances{user{balances{available{amount currency}}}}"));

    auto fail = [this, background](const QString &error) {
        if (background)
            emit backgroundBalancesFailed(error);
        else
            emit balancesFailed(error);
    };

    via->post(QStringLiteral("_api/graphql"), body,
              [this, via, background, fail](const TransportReply &reply) {
        // Active fetch: the casino / transport was switched meanwhile -> stale answer.
        // Background fetch: the transport became the active one meanwhile -> its own
        // fetchBalances() covers it.
        if (background ? (via == m_transport) : (via != m_transport))
            return;
        if (!reply.ok) {
            if (!background && reply.status == 0) {
                emit sessionLost(reply.error);
            }
            fail(reply.error);
            return;
        }
        QString type;
        const QString apiError = firstErrorText(reply.json, &type);
        if (!apiError.isEmpty()) {
            fail(apiError);
            return;
        }

        const QJsonValue balsVal = reply.json.value(QStringLiteral("data")).toObject()
                                       .value(QStringLiteral("user")).toObject()
                                       .value(QStringLiteral("balances"));
        if (!balsVal.isArray()) {
            fail(tr("Login failed: unexpected response (is the API key valid?)"));
            return;
        }

        QMap<QString, double> balances;
        const QJsonArray arr = balsVal.toArray();
        for (const QJsonValue &v : arr) {
            const QJsonObject avail = v.toObject().value(QStringLiteral("available")).toObject();
            const QString cur = avail.value(QStringLiteral("currency")).toString().toLower();
            if (!cur.isEmpty())
                balances.insert(cur, numberOf(avail.value(QStringLiteral("amount"))));
        }
        if (background)
            emit backgroundBalancesReady(balances);
        else
            emit balancesReady(balances);
    });
}

void StakeClient::lookupBetIid(const QString &betId, std::function<void(const QString &)> done)
{
    QString why;
    if (betId.isEmpty() || !m_transport || isShuffle() || isGoated() || isWolfbet() || isDuel() || isParadice() || isPasino() || isTower() || isWinna() || !m_transport->isReady(&why)) {
        QTimer::singleShot(0, this, [done] { done(QString()); });
        return;
    }

    QJsonObject vars;
    vars.insert(QStringLiteral("betId"), betId);

    QJsonObject body;
    body.insert(QStringLiteral("operationName"), QStringLiteral("BetLookup"));
    body.insert(QStringLiteral("variables"), vars);
    body.insert(QStringLiteral("query"),
                QStringLiteral("query BetLookup($betId:String){bet(betId:$betId){iid}}"));

    m_transport->post(QStringLiteral("_api/graphql"), body, [done](const TransportReply &reply) {
        if (!reply.ok) {
            done(QString());
            return;
        }
        const QString iid = reply.json.value(QStringLiteral("data")).toObject()
                                .value(QStringLiteral("bet")).toObject()
                                .value(QStringLiteral("iid")).toString();
        done(iid);
    });
}

void StakeClient::placeDice(const DiceBetRequest &request)
{
    m_lastDiceTarget = -1.0; // set below by the casinos whose target differs from the app's
    QString why;
    if (!m_transport || !m_transport->isReady(&why)) {
        const LiveBetResult r = failed(m_transport ? why : tr("No transport configured."));
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }
    if (m_currency.isEmpty()) {
        const LiveBetResult r = failed(tr("No currency selected — log in and pick one in Options."));
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }

    if (isShuffle()) {
        placeShuffleDice(request);
        return;
    }
    if (isGoated()) {
        placeGoatedDice(request);
        return;
    }
    if (isWolfbet()) {
        placeWolfbetDice(request);
        return;
    }
    if (isDuel()) {
        placeDuelDice(request);
        return;
    }
    if (isParadice()) {
        placeParadiceDice(request);
        return;
    }
    if (isPasino()) {
        placePasinoDice(request);
        return;
    }
    if (isTower()) {
        placeTowerDice(request);
        return;
    }
    if (isWinna()) {
        placeWinnaDice(request);
        return;
    }

    const double target = std::round(request.target * 100.0) / 100.0;

    QJsonObject body;
    body.insert(QStringLiteral("target"), target);
    body.insert(QStringLiteral("condition"),
                request.over ? QStringLiteral("above") : QStringLiteral("below"));
    body.insert(QStringLiteral("identifier"), randomIdentifier());
    body.insert(QStringLiteral("amount"), request.amount);
    body.insert(QStringLiteral("currency"), m_currency);

    postBet(QStringLiteral("_api/casino/dice/roll"), body, request.amount);
}

void StakeClient::placeLimbo(const LimboBetRequest &request)
{
    m_lastDiceTarget = -1.0;
    QString why;
    if (!m_transport || !m_transport->isReady(&why)) {
        const LiveBetResult r = failed(m_transport ? why : tr("No transport configured."));
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }
    if (m_currency.isEmpty()) {
        const LiveBetResult r = failed(tr("No currency selected — log in and pick one in Options."));
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }

    if (isShuffle()) {
        placeShuffleLimbo(request);
        return;
    }
    if (isGoated()) {
        placeGoatedLimbo(request);
        return;
    }
    if (isWolfbet()) {
        placeWolfbetLimbo(request);
        return;
    }
    if (isDuel()) {
        placeDuelLimbo(request);
        return;
    }
    if (isParadice()) {
        const LiveBetResult r = failed(tr("Paradice: Limbo is not supported (dice only)."));
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }
    if (isPasino()) {
        placePasinoLimbo(request);
        return;
    }
    if (isTower()) {
        placeTowerLimbo(request);
        return;
    }
    if (isWinna()) {
        placeWinnaLimbo(request);
        return;
    }

    const double target = std::round(request.multiplierTarget * 100.0) / 100.0;

    QJsonObject body;
    body.insert(QStringLiteral("multiplierTarget"), target);
    body.insert(QStringLiteral("identifier"), randomIdentifier());
    body.insert(QStringLiteral("amount"), request.amount);
    body.insert(QStringLiteral("currency"), m_currency);

    postBet(QStringLiteral("_api/casino/limbo/bet"), body, request.amount);
}

// ---------------------------------------------------------------- Stake's other games
// Keno, mines, plinko, hilo, blackjack, ... - the same requests as the reference JavaScript
// bot (bodies built by StakeGames::buildBetRequest()). REST answers look like
// {"kenoBet": {...}}, the GraphQL ones (plinko, flip) like {"data": {"plinkoBet": {...}}}.
void StakeClient::placeGame(const GameBetRequest &request)
{
    m_lastDiceTarget = -1.0;
    auto failLater = [this](const LiveBetResult &r) {
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
    };
    QString why;
    if (!m_transport || !m_transport->isReady(&why)) {
        failLater(failed(m_transport ? why : tr("No transport configured.")));
        return;
    }
    if (m_currency.isEmpty()) {
        failLater(failed(tr("No currency selected — log in and pick one in Options.")));
        return;
    }
    if (isShuffle() && request.game == Game::Blackjack) {
        placeShuffleBlackjack(request);
        return;
    }
    if (isGoated() && request.game == Game::Blackjack) {
        placeGoatedBlackjack(request);
        return;
    }
    if (isGoated() && request.game == Game::Hilo) {
        placeGoatedHilo(request);
        return;
    }
    if (isWolfbet() && request.game == Game::Hilo) {
        placeWolfbetHilo(request);
        return;
    }
    if (isWolfbet() && request.game == Game::Keno) {
        placeWolfbetKeno(request);
        return;
    }
    if (isWolfbet() && request.game == Game::Plinko) {
        placeWolfbetPlinko(request);
        return;
    }
    if (isGoated() && request.game == Game::Keno) {
        placeGoatedKeno(request);
        return;
    }
    if (isGoated() && request.game == Game::Mines) {
        placeGoatedMines(request);
        return;
    }
    if (isGoated() && request.game == Game::Plinko) {
        placeGoatedPlinko(request);
        return;
    }
    if (isGoated() && request.game == Game::Wheel) {
        placeGoatedWheel(request);
        return;
    }
    if (isGoated() && request.game == Game::Diamonds) {
        placeGoatedDiamonds(request);
        return;
    }
    if (isGoated() && request.game == Game::DragonTower) {
        placeGoatedTower(request);
        return;
    }
    if (isGoated() && request.game == Game::GoatRun) {
        placeGoatedGoatRun(request);
        return;
    }
    if (request.game == Game::GoatRun) {
        failLater(failed(tr("Goat Run is only available on Goated - switch the casino to Goated or pick "
                            "another game.")));
        return;
    }
    if (isShuffle() && request.game == Game::Hilo) {
        placeShuffleHilo(request);
        return;
    }
    if (isShuffle() && request.game == Game::Keno) {
        placeShuffleKeno(request);
        return;
    }
    if (isShuffle() && request.game == Game::Mines) {
        placeShuffleMines(request);
        return;
    }
    if (isShuffle() && request.game == Game::Plinko) {
        placeShufflePlinko(request);
        return;
    }
    if (isShuffle() && request.game == Game::Wheel) {
        placeShuffleWheel(request);
        return;
    }
    if (isShuffle() && request.game == Game::DragonTower) {
        placeShuffleTower(request);
        return;
    }
    if (isShuffle() && request.game == Game::Chicken) {
        placeShuffleChicken(request);
        return;
    }
    if (!isStake()) {
        failLater(failed(tr("%1 is only available on Stake - switch the casino to Stake or pick "
                            "another game.").arg(gameDisplayName(request.game))));
        return;
    }

    QString endpoint;
    QJsonObject body;
    QString err;
    if (!StakeGames::buildBetRequest(request.game, request.amount, m_currency, request.params,
                                     &endpoint, &body, &err)) {
        failLater(failed(err));
        return;
    }
    postGameBet(request.game, endpoint, body, request.params, request.amount, true);
}

void StakeClient::sendGameAction(Game game, const QString &endpoint, const QJsonObject &body,
                                 const QJsonObject &params, double requestedAmount)
{
    QString why;
    if (!m_transport || !m_transport->isReady(&why)) {
        const LiveBetResult r = failed(m_transport ? why : tr("No transport configured."), {}, true, false);
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }
    if (isShuffle() && game == Game::Blackjack) {
        sendShuffleBlackjack(body.value(QStringLiteral("action")).toString(), params, requestedAmount);
        return;
    }
    if (isGoated() && game == Game::Blackjack) {
        sendGoatedBlackjack(body.value(QStringLiteral("action")).toString(), params, requestedAmount);
        return;
    }
    if (isGoated() && game == Game::Hilo) {
        sendGoatedHilo(endpoint, body, params, requestedAmount);
        return;
    }
    if (isShuffle() && game == Game::Hilo) {
        sendShuffleHilo(endpoint, body, params, requestedAmount);
        return;
    }
    if (isWolfbet() && game == Game::Hilo) {
        sendWolfbetHilo(endpoint, body, params, requestedAmount);
        return;
    }
    postGameBet(game, endpoint, body, params, requestedAmount, false);
}

LiveBetResult StakeClient::gameResult(Game game, const QJsonObject &bet, const QJsonObject &params,
                                      double requestedAmount) const
{
    LiveBetResult r;
    r.ok = true;
    r.bet = bet;
    r.amount = bet.contains(QStringLiteral("amount")) ? numberOf(bet.value(QStringLiteral("amount")))
                                                      : requestedAmount;
    r.multiplier = numberOf(bet.value(QStringLiteral("payoutMultiplier")));
    r.payout = numberOf(bet.value(QStringLiteral("payout")));
    r.betId = bet.value(QStringLiteral("id")).toString();
    r.nonce = bet.contains(QStringLiteral("nonce")) ? qint64(numberOf(bet.value(QStringLiteral("nonce")))) : -1;
    r.active = StakeGames::isUnfinished(game, bet);
    const StakeGames::Summary sum = StakeGames::summarize(game, bet, params);
    r.roll = sum.roll;
    r.targetText = sum.targetText;
    r.rollText = sum.rollText;
    r.win = !r.active && BotConstants::isWinMultiplier(r.multiplier);
    return r;
}

void StakeClient::postGameBet(Game game, const QString &endpoint, const QJsonObject &body,
                              const QJsonObject &params, double requestedAmount, bool firstRequest)
{
    m_transport->post(endpoint, body, [this, game, params, requestedAmount, firstRequest](const TransportReply &reply) {
        if (!reply.ok) {
            // Same policy as dice: a timeout is retried, a 5xx may have gone through.
            const bool isTimeout = (reply.status == 0);
            const bool unknown = (reply.status >= 500);
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit diceFinished(failed(reply.error, {}, isTimeout, unknown));
            return;
        }

        QString type;
        const QString apiError = firstErrorText(reply.json, &type);
        if (!apiError.isEmpty()) {
            // A game of this kind is still open (left over from an earlier run or a lost
            // answer): fetch it so the engine can finish it first.
            if (firstRequest && (containsCI(type, "existingGame") || containsCI(apiError, "existing game"))
                && !StakeGames::activeBetSlug(game).isEmpty()) {
                fetchActiveGame(game, params, requestedAmount);
                return;
            }
            LiveBetResult r = failed(apiError, type, isRetryable(type, apiError), false);
            r.retryDelayMs = fastRetryDelayMs(type, apiError);
            emit diceFinished(r);
            return;
        }

        const QJsonObject bet = findBetObject(reply.json);
        if (bet.isEmpty()) {
            const QString raw = QString::fromUtf8(QJsonDocument(reply.json).toJson(QJsonDocument::Compact)).left(400);
            emit diceFinished(failed(tr("Unexpected %1 response (no bet object): %2")
                                         .arg(gameDisplayName(game), raw),
                                     {}, false, true));
            return;
        }
        emit diceFinished(gameResult(game, bet, params, requestedAmount));
    });
}

void StakeClient::fetchActiveGame(Game game, const QJsonObject &params, double requestedAmount)
{
    if (isShuffle() && game == Game::Blackjack) {
        fetchShuffleBlackjack(params, requestedAmount);
        return;
    }
    if (isShuffle() && game == Game::Hilo) {
        fetchShuffleHilo(params, requestedAmount);
        return;
    }
    if (isGoated() && game == Game::Blackjack) {
        fetchGoatedBlackjack(params, requestedAmount);
        return;
    }
    if (isGoated() && game == Game::Hilo) {
        fetchGoatedHilo(params, requestedAmount);
        return;
    }
    if (isWolfbet() && game == Game::Hilo) {
        fetchWolfbetHilo(params, requestedAmount);
        return;
    }
    if (isWolfbet()) {
        LiveBetResult r = failed(tr("%1: no open game found on Wolfbet.").arg(gameDisplayName(game)),
                                 QStringLiteral("existingGame"), false, false);
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }
    if (isGoated()) {
        LiveBetResult r = failed(tr("%1: no open game found on Goated.").arg(gameDisplayName(game)),
                                 QStringLiteral("existingGame"), false, false);
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }
    const QString slug = StakeGames::activeBetSlug(game);
    m_transport->post(QStringLiteral("_api/casino/active-bet/%1").arg(slug), QJsonObject(),
                      [this, game, params, requestedAmount](const TransportReply &reply) {
        const QJsonObject bet = reply.json.value(QStringLiteral("user")).toObject()
                                    .value(QStringLiteral("activeCasinoBet")).toObject();
        if (!reply.ok || bet.isEmpty()) {
            // Nothing open after all (it may have just finished): place the bet again.
            LiveBetResult r = failed(tr("%1: an earlier game was still open; retrying.")
                                         .arg(gameDisplayName(game)),
                                     QStringLiteral("existingGame"), true, false);
            r.retryDelayMs = 500;
            emit diceFinished(r);
            return;
        }
        LiveBetResult r = gameResult(game, bet, params, requestedAmount);
        r.active = true; // continue it with the next move
        r.win = false;
        emit diceFinished(r);
    });
}

// Shared by dice and limbo: both answer with {"<game>Bet"/"diceRoll": {...}}.
void StakeClient::postBet(const QString &endpoint, const QJsonObject &body, double requestedAmount)
{
    m_transport->post(endpoint, body, [this, requestedAmount](const TransportReply &reply) {
        if (!reply.ok) {
            // Treat timeouts (status 0) as retryable so the bot doesn't halt.
            // We only mark 5xx server errors as "unknown" to prevent halting on network blips.
            const bool isTimeout = (reply.status == 0);
            const bool unknown = (reply.status >= 500);
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit diceFinished(failed(reply.error, {}, isTimeout, unknown));
            return;
        }

        QString type;
        const QString apiError = firstErrorText(reply.json, &type);
        if (!apiError.isEmpty()) {
            LiveBetResult r = failed(apiError, type, isRetryable(type, apiError), false);
            r.retryDelayMs = fastRetryDelayMs(type, apiError);
            emit diceFinished(r);
            return;
        }

        const QJsonObject bet = findBetObject(reply.json);
        if (bet.isEmpty()) {
            emit diceFinished(failed(tr("Unexpected bet response (no bet object)."), {}, true, false));
            return;
        }

        LiveBetResult r;
        r.ok = true;
        r.amount = bet.contains(QStringLiteral("amount")) ? numberOf(bet.value(QStringLiteral("amount")))
                                                          : requestedAmount;
        r.multiplier = numberOf(bet.value(QStringLiteral("payoutMultiplier")));
        r.payout = numberOf(bet.value(QStringLiteral("payout")));
        r.roll = numberOf(bet.value(QStringLiteral("state")).toObject().value(QStringLiteral("result")));
        r.betId = bet.value(QStringLiteral("id")).toString();
        r.nonce = bet.contains(QStringLiteral("nonce"))
                      ? qint64(numberOf(bet.value(QStringLiteral("nonce")))) : -1;
        const bool active = bet.value(QStringLiteral("active")).toBool(false);
        r.win = !active && r.multiplier >= 1.0;
        emit diceFinished(r);
    });
}

// Shuffle dice: the DicePlay GraphQL mutation (same query the JavaScript bot uses).
// amount / bet are strings with 8 / 2 decimals, direction is BELOW / ABOVE.
// ---------------------------------------------------------------- Shuffle blackjack
QString StakeClient::shuffleWindowId()
{
    // The site sends a random id of its browser window with every blackjack request.
    if (m_shuffleWindowId.isEmpty()) {
        static const char chars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
        for (int i = 0; i < 10; ++i)
            m_shuffleWindowId += QLatin1Char(chars[QRandomGenerator::global()->bounded(36)]);
    }
    return m_shuffleWindowId;
}

void StakeClient::placeShuffleBlackjack(const GameBetRequest &request)
{
    QJsonObject data;
    data.insert(QStringLiteral("amount"), QString::number(request.amount, 'f', 8));
    data.insert(QStringLiteral("currency"), ShuffleTransport::apiCurrency(m_currency));
    // Side bets are not played.
    data.insert(QStringLiteral("perfectPairAmount"), QStringLiteral("0.00000000"));
    data.insert(QStringLiteral("twentyOnePlusThreeAmount"), QStringLiteral("0.00000000"));
    data.insert(QStringLiteral("windowId"), shuffleWindowId());
    data.insert(QStringLiteral("usdAmount"), 0);
    postShuffleBlackjack(QStringLiteral("BlackjackStart"),
                         QStringLiteral("mutation BlackjackStart($data: BlackjackStartInput!) {\n"
                                        "  blackjackStart(data: $data) {\n    ...BlackjackBetFields\n"
                                        "    __typename\n  }\n}\n\n")
                             + QLatin1String(kShuffleBlackjackFields),
                         QJsonObject{{QStringLiteral("data"), data}}, QStringLiteral("blackjackStart"),
                         request.params, request.amount, true);
}

void StakeClient::sendShuffleBlackjack(const QString &stakeAction, const QJsonObject &params,
                                       double requestedAmount)
{
    // Stake's action names -> Shuffle's.
    QString action;
    if (stakeAction == QLatin1String("hit")) action = QStringLiteral("HIT");
    else if (stakeAction == QLatin1String("stand")) action = QStringLiteral("STAND");
    else if (stakeAction == QLatin1String("double")) action = QStringLiteral("DOUBLE_DOWN");
    else if (stakeAction == QLatin1String("split")) action = QStringLiteral("SPLIT");
    else if (stakeAction == QLatin1String("insurance")) action = QStringLiteral("BUY_INSURANCE");
    else if (stakeAction == QLatin1String("noInsurance")) action = QStringLiteral("REJECT_INSURANCE");
    if (action.isEmpty() || m_shuffleBjBetId.isEmpty()) {
        const LiveBetResult r = failed(action.isEmpty()
                                           ? tr("Shuffle blackjack: unknown action \"%1\".").arg(stakeAction)
                                           : tr("Shuffle blackjack: no open hand."));
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }
    QJsonObject data;
    data.insert(QStringLiteral("action"), action);
    data.insert(QStringLiteral("windowId"), shuffleWindowId());
    data.insert(QStringLiteral("betId"), m_shuffleBjBetId);
    data.insert(QStringLiteral("cardCount"), m_shuffleBjSteps); // steps reported so far
    postShuffleBlackjack(QStringLiteral("BlackjackNext"),
                         QStringLiteral("mutation BlackjackNext($data: BlackjackNextInput!) {\n"
                                        "  blackjackNext(data: $data) {\n    ...BlackjackBetFields\n"
                                        "    __typename\n  }\n}\n\n")
                             + QLatin1String(kShuffleBlackjackFields),
                         QJsonObject{{QStringLiteral("data"), data}}, QStringLiteral("blackjackNext"),
                         params, requestedAmount, false);
}

void StakeClient::fetchShuffleBlackjack(const QJsonObject &params, double requestedAmount)
{
    postShuffleBlackjack(QStringLiteral("BlackjackActiveBet"),
                         QStringLiteral("query BlackjackActiveBet {\n  blackjackActiveBet {\n"
                                        "    ...BlackjackBetFields\n    __typename\n  }\n}\n\n")
                             + QLatin1String(kShuffleBlackjackFields),
                         QJsonObject(), QStringLiteral("blackjackActiveBet"), params, requestedAmount, false);
}

void StakeClient::postShuffleBlackjack(const QString &operation, const QString &query,
                                       const QJsonObject &variables, const QString &resultKey,
                                       const QJsonObject &params, double requestedAmount, bool firstRequest)
{
    QJsonObject body;
    body.insert(QStringLiteral("operationName"), operation);
    body.insert(QStringLiteral("variables"), variables);
    body.insert(QStringLiteral("query"), query);
    const bool activeQuery = resultKey == QLatin1String("blackjackActiveBet");
    m_transport->post(QStringLiteral("main-api/graphql/api/graphql"), body,
                      [this, resultKey, params, requestedAmount, firstRequest, activeQuery](const TransportReply &reply) {
        if (!reply.ok) {
            const bool isTimeout = (reply.status == 0);
            const bool unknown = (reply.status >= 500);
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit diceFinished(failed(reply.error, {}, isTimeout, unknown));
            return;
        }
        QString type;
        const QString apiError = firstErrorText(reply.json, &type);
        if (!apiError.isEmpty()) {
            // A hand is still open (an earlier run, the website): fetch it to finish it first.
            if (firstRequest && (containsCI(apiError, "active") || containsCI(apiError, "existing")
                                 || containsCI(apiError, "already") || containsCI(apiError, "progress")
                                 || containsCI(type, "existing") || containsCI(type, "active"))) {
                fetchShuffleBlackjack(params, requestedAmount);
                return;
            }
            LiveBetResult r = failed(apiError, type, isRetryable(type, apiError), false);
            r.retryDelayMs = fastRetryDelayMs(type, apiError);
            emit diceFinished(r);
            return;
        }
        const QJsonObject raw = reply.json.value(QStringLiteral("data")).toObject()
                                    .value(resultKey).toObject();
        if (raw.isEmpty()) {
            if (activeQuery) {
                // Nothing open (same answer as Stake's active-bet fetch).
                LiveBetResult r = failed(tr("Blackjack: no open hand."), QStringLiteral("existingGame"),
                                         true, false);
                r.retryDelayMs = 500;
                emit diceFinished(r);
                return;
            }
            emit diceFinished(failed(tr("Unexpected Shuffle blackjack response (no bet)."), {}, false, true));
            return;
        }
        const QJsonObject bet = shuffleBlackjackAsStake(raw);
        m_shuffleBjBetId = raw.value(QStringLiteral("id")).toString();
        m_shuffleBjSteps = int(raw.value(QStringLiteral("shuffleOriginalActions")).toArray().size());
        LiveBetResult r = gameResult(Game::Blackjack, bet, params, requestedAmount);
        if (activeQuery) {
            r.active = true; // continue it with the next move
            r.win = false;
        }
        if (!r.active) {
            m_shuffleBjBetId.clear();
            // The balance after the settled hand (payout included).
            const QJsonValue after = raw.value(QStringLiteral("afterBalance"));
            if (after.isString() || after.isDouble())
                r.balanceAfter = numberOf(after);
        }
        emit diceFinished(r);
    });
}

// ---------------------------------------------------------------- Shuffle keno
void StakeClient::placeShuffleKeno(const GameBetRequest &request)
{
    // Numbers and risk as Stake's request would have them (the tab's 1..40 tiles or a script's
    // numbers). Stake takes them 0-based; Shuffle numbers its keno board 1..40
    // (KENO_SELECTED_BOUNDS for a 0), so they go back up by one.
    QString endpoint;
    QJsonObject stakeBody;
    QString err;
    if (!StakeGames::buildBetRequest(Game::Keno, request.amount, m_currency, request.params,
                                     &endpoint, &stakeBody, &err)) {
        const LiveBetResult r = failed(err);
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }
    QJsonObject data;
    data.insert(QStringLiteral("amount"), QString::number(request.amount, 'f', 8));
    data.insert(QStringLiteral("currency"), ShuffleTransport::apiCurrency(m_currency));
    // Shuffle's risk names: CLASSIC, LOW_RISK, MEDIUM_RISK, HIGH_RISK.
    const QString risk = stakeBody.value(QStringLiteral("risk")).toString().toUpper();
    data.insert(QStringLiteral("risk"), risk == QLatin1String("CLASSIC") ? risk : risk + QStringLiteral("_RISK"));
    QJsonArray selected;
    for (const QJsonValue &n : stakeBody.value(QStringLiteral("numbers")).toArray())
        selected.append(n.toInt() + 1);
    data.insert(QStringLiteral("selected"), selected);
    data.insert(QStringLiteral("windowId"), shuffleWindowId());
    data.insert(QStringLiteral("usdAmount"), 0);

    QJsonObject body;
    body.insert(QStringLiteral("operationName"), QStringLiteral("KenoPlay"));
    body.insert(QStringLiteral("variables"), QJsonObject{{QStringLiteral("data"), data}});
    body.insert(QStringLiteral("query"), QStringLiteral(
        "mutation KenoPlay($data: KenoPlayInput!) {\n  kenoPlay(data: $data) {\n    id\n    amount\n"
        "    multiplier\n    currency\n    jackpot\n    payout\n    afterBalance\n"
        "    shuffleOriginalActions {\n      id\n      action {\n        keno {\n          results\n"
        "          risk\n          multiplier\n          selected\n          __typename\n        }\n"
        "        __typename\n      }\n      __typename\n    }\n    __typename\n  }\n}"));

    const QJsonObject params = request.params;
    const double requestedAmount = request.amount;
    m_transport->post(QStringLiteral("main-api/graphql/api/graphql"), body,
                      [this, params, requestedAmount](const TransportReply &reply) {
        if (!reply.ok) {
            const bool isTimeout = (reply.status == 0);
            const bool unknown = (reply.status >= 500);
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit diceFinished(failed(reply.error, {}, isTimeout, unknown));
            return;
        }
        QString type;
        const QString apiError = firstErrorText(reply.json, &type);
        if (!apiError.isEmpty()) {
            LiveBetResult r = failed(apiError, type, isRetryable(type, apiError), false);
            r.retryDelayMs = fastRetryDelayMs(type, apiError);
            emit diceFinished(r);
            return;
        }
        const QJsonObject play = reply.json.value(QStringLiteral("data")).toObject()
                                     .value(QStringLiteral("kenoPlay")).toObject();
        const QJsonArray steps = play.value(QStringLiteral("shuffleOriginalActions")).toArray();
        const QJsonObject keno = steps.isEmpty()
            ? QJsonObject()
            : steps.first().toObject().value(QStringLiteral("action")).toObject()
                  .value(QStringLiteral("keno")).toObject();
        if (play.isEmpty() || keno.isEmpty()) {
            emit diceFinished(failed(tr("Unexpected Shuffle keno response (no kenoPlay result)."), {}, true, false));
            return;
        }
        // Stake's keno shape: state {risk, selectedNumbers, drawnNumbers}, 0-based like Stake's
        // (Shuffle answers 1..40).
        const auto zeroBased = [](const QJsonValue &v) {
            QJsonArray out;
            for (const QJsonValue &n : v.toArray())
                out.append(n.toInt() - 1);
            return out;
        };
        QJsonObject state;
        QString risk = keno.value(QStringLiteral("risk")).toString().toLower(); // "low_risk" -> "low"
        if (risk.endsWith(QLatin1String("_risk")))
            risk.chop(5);
        state.insert(QStringLiteral("risk"), risk);
        state.insert(QStringLiteral("selectedNumbers"), zeroBased(keno.value(QStringLiteral("selected"))));
        state.insert(QStringLiteral("drawnNumbers"), zeroBased(keno.value(QStringLiteral("results"))));
        const QJsonValue m = play.value(QStringLiteral("multiplier"));
        const double mult = (m.isNull() || m.isUndefined()) ? numberOf(keno.value(QStringLiteral("multiplier")))
                                                            : numberOf(m);
        QJsonObject bet;
        bet.insert(QStringLiteral("id"), play.value(QStringLiteral("id")).toString());
        bet.insert(QStringLiteral("amount"), numberOf(play.value(QStringLiteral("amount"))));
        bet.insert(QStringLiteral("payout"), numberOf(play.value(QStringLiteral("payout"))));
        bet.insert(QStringLiteral("payoutMultiplier"), mult);
        bet.insert(QStringLiteral("active"), false);
        bet.insert(QStringLiteral("currency"), play.value(QStringLiteral("currency")).toString().toLower());
        bet.insert(QStringLiteral("game"), QStringLiteral("keno"));
        bet.insert(QStringLiteral("state"), state);
        LiveBetResult r = gameResult(Game::Keno, bet, params, requestedAmount);
        const QJsonValue after = play.value(QStringLiteral("afterBalance"));
        if (after.isString() || after.isDouble())
            r.balanceAfter = numberOf(after);
        emit diceFinished(r);
    });
}

// ---------------------------------------------------------------- Shuffle auto-bet info
QJsonObject StakeClient::shuffleAutoBetInfo()
{
    if (m_shuffleAutoSessionId.isEmpty()) {
        static const char chars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
        for (int i = 0; i < 10; ++i)
            m_shuffleAutoSessionId += QLatin1Char(chars[QRandomGenerator::global()->bounded(36)]);
    }
    QJsonObject info;
    info.insert(QStringLiteral("sessionId"), m_shuffleAutoSessionId);
    info.insert(QStringLiteral("totalPnl"), QStringLiteral("0"));
    info.insert(QStringLiteral("currentBetCount"), ++m_shuffleAutoBetCount);
    info.insert(QStringLiteral("metadata"), QStringLiteral(
        "{\"config\":{\"betCount\":\"0\",\"onWinMode\":\"reset\",\"onLossMode\":\"reset\","
        "\"onWinPercentage\":\"0.00\",\"onLossPercentage\":\"0.00\",\"stopOnProfit\":\"0.00\","
        "\"stopOnLoss\":\"0.00\"}}"));
    return info;
}

// ---------------------------------------------------------------- Shuffle chicken
void StakeClient::placeShuffleChicken(const GameBetRequest &request)
{
    // Difficulty and steps as Stake's chicken request would have them; the steps are the lane
    // to reach (Shuffle's selectedLane).
    QString endpoint;
    QJsonObject stakeBody;
    QString err;
    if (!StakeGames::buildBetRequest(Game::Chicken, request.amount, m_currency, request.params,
                                     &endpoint, &stakeBody, &err)) {
        const LiveBetResult r = failed(err);
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }
    // Shuffle's lanes per difficulty: easy 1..19, medium 1..17, hard 1..15, expert 1..10.
    const int lane = stakeBody.value(QStringLiteral("round")).toInt(1);
    const QString difficulty = stakeBody.value(QStringLiteral("difficulty")).toString().toLower();
    const int maxLane = int(StakeGames::casinoMaximum(QStringLiteral("shuffle"), Game::Chicken,
                                                      QStringLiteral("steps"),
                                                      QJsonObject{{QStringLiteral("difficulty"), difficulty}}));
    if (lane < 1 || lane > maxLane) {
        const LiveBetResult r = failed(tr("Shuffle chicken: steps must be 1 to %1 on %2 difficulty.")
                                           .arg(maxLane).arg(difficulty));
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }
    QJsonObject data;
    data.insert(QStringLiteral("selectedLane"), lane);
    data.insert(QStringLiteral("amount"), QString::number(request.amount, 'f', 8));
    data.insert(QStringLiteral("currency"), ShuffleTransport::apiCurrency(m_currency));
    data.insert(QStringLiteral("difficulty"), stakeBody.value(QStringLiteral("difficulty")).toString().toUpper());
    data.insert(QStringLiteral("autoBetInfo"), shuffleAutoBetInfo());

    QJsonObject body;
    body.insert(QStringLiteral("operationName"), QStringLiteral("ChickenAuto"));
    body.insert(QStringLiteral("variables"), QJsonObject{{QStringLiteral("data"), data}});
    body.insert(QStringLiteral("query"), QStringLiteral(
        "mutation ChickenAuto($data: ChickenBetInput!) {\n  chickenAuto(data: $data) {\n"
        "    ...ChickenBetFields\n    __typename\n  }\n}\n\nfragment ChickenBetFields on Bet {\n  id\n"
        "  amount\n  payout\n  currency\n  multiplier\n  afterBalance\n  shuffleOriginalActions {\n"
        "    id\n    action {\n      chicken {\n        chickenResult\n        chickenDifficulty\n"
        "        winMultiplier\n        selectedLane\n        __typename\n      }\n      __typename\n"
        "    }\n    __typename\n  }\n  __typename\n}"));

    const QJsonObject params = request.params;
    const double requestedAmount = request.amount;
    m_transport->post(QStringLiteral("main-api/graphql/api/graphql"), body,
                      [this, params, requestedAmount](const TransportReply &reply) {
        if (!reply.ok) {
            const bool isTimeout = (reply.status == 0);
            const bool unknown = (reply.status >= 500);
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit diceFinished(failed(reply.error, {}, isTimeout, unknown));
            return;
        }
        QString type;
        const QString apiError = firstErrorText(reply.json, &type);
        if (!apiError.isEmpty()) {
            LiveBetResult r = failed(apiError, type, isRetryable(type, apiError), false);
            r.retryDelayMs = fastRetryDelayMs(type, apiError);
            emit diceFinished(r);
            return;
        }
        const QJsonObject play = reply.json.value(QStringLiteral("data")).toObject()
                                     .value(QStringLiteral("chickenAuto")).toObject();
        const QJsonArray steps = play.value(QStringLiteral("shuffleOriginalActions")).toArray();
        const QJsonObject chicken = steps.isEmpty()
            ? QJsonObject()
            : steps.last().toObject().value(QStringLiteral("action")).toObject()
                  .value(QStringLiteral("chicken")).toObject();
        if (play.isEmpty() || chicken.isEmpty()) {
            emit diceFinished(failed(tr("Unexpected Shuffle chicken response (no chickenAuto result)."), {}, true, false));
            return;
        }
        // Stake's chicken shape: state {difficulty, round (lanes to cross), _deathPoint (the first
        // lane with a car, the lowest of Shuffle's chickenResult)}.
        const QJsonArray cars = chicken.value(QStringLiteral("chickenResult")).toArray();
        int death = -1;
        for (const QJsonValue &c : cars)
            if (death < 0 || c.toInt() < death)
                death = c.toInt();
        QJsonObject state;
        state.insert(QStringLiteral("difficulty"), chicken.value(QStringLiteral("chickenDifficulty")).toString().toLower());
        state.insert(QStringLiteral("round"), chicken.value(QStringLiteral("selectedLane")).toInt());
        state.insert(QStringLiteral("cars"), cars);
        if (death >= 0)
            state.insert(QStringLiteral("_deathPoint"), death);
        const QJsonValue m = play.value(QStringLiteral("multiplier"));
        const double mult = (m.isNull() || m.isUndefined()) ? numberOf(chicken.value(QStringLiteral("winMultiplier")))
                                                            : numberOf(m);
        QJsonObject bet;
        bet.insert(QStringLiteral("id"), play.value(QStringLiteral("id")).toString());
        bet.insert(QStringLiteral("amount"), numberOf(play.value(QStringLiteral("amount"))));
        bet.insert(QStringLiteral("payout"), numberOf(play.value(QStringLiteral("payout"))));
        bet.insert(QStringLiteral("payoutMultiplier"), mult);
        bet.insert(QStringLiteral("active"), false);
        bet.insert(QStringLiteral("currency"), play.value(QStringLiteral("currency")).toString().toLower());
        bet.insert(QStringLiteral("game"), QStringLiteral("chicken"));
        bet.insert(QStringLiteral("state"), state);
        LiveBetResult r = gameResult(Game::Chicken, bet, params, requestedAmount);
        const QJsonValue after = play.value(QStringLiteral("afterBalance"));
        if (after.isString() || after.isDouble())
            r.balanceAfter = numberOf(after);
        emit diceFinished(r);
    });
}

// ---------------------------------------------------------------- Shuffle tower
void StakeClient::placeShuffleTower(const GameBetRequest &request)
{
    // Picks and difficulty as Stake's (Dragon) Tower request would have them (1-based picks sent
    // 0-based: Shuffle numbers the tiles of a floor from 0 too).
    QString endpoint;
    QJsonObject stakeBody;
    QString err;
    if (!StakeGames::buildBetRequest(Game::DragonTower, request.amount, m_currency, request.params,
                                     &endpoint, &stakeBody, &err)) {
        const LiveBetResult r = failed(err);
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }
    QJsonObject data;
    data.insert(QStringLiteral("selected"), stakeBody.value(QStringLiteral("eggs")).toArray());
    data.insert(QStringLiteral("amount"), QString::number(request.amount, 'f', 8));
    data.insert(QStringLiteral("currency"), ShuffleTransport::apiCurrency(m_currency));
    // EASY (4 tiles, 3 safe), MEDIUM (3/2), HARD (2/1), EXPERT (3/1), MASTER (4/1).
    data.insert(QStringLiteral("difficulty"), stakeBody.value(QStringLiteral("difficulty")).toString().toUpper());
    data.insert(QStringLiteral("autoBetInfo"), shuffleAutoBetInfo());

    QJsonObject body;
    body.insert(QStringLiteral("operationName"), QStringLiteral("TowerAutoBet"));
    body.insert(QStringLiteral("variables"), QJsonObject{{QStringLiteral("data"), data}});
    body.insert(QStringLiteral("query"), QStringLiteral(
        "mutation TowerAutoBet($data: TowerBetInput!) {\n  towerAuto(data: $data) {\n    ...TowerBetFields\n"
        "    __typename\n  }\n}\n\nfragment TowerBetFields on Bet {\n  id\n  amount\n  payout\n  currency\n"
        "  multiplier\n  afterBalance\n  shuffleOriginalActions {\n    id\n    action {\n      tower {\n"
        "        towerResult\n        towerDifficulty\n        winMultiplier\n        selected\n"
        "        __typename\n      }\n      __typename\n    }\n    __typename\n  }\n  __typename\n}"));

    const QJsonObject params = request.params;
    const double requestedAmount = request.amount;
    m_transport->post(QStringLiteral("main-api/graphql/api/graphql"), body,
                      [this, params, requestedAmount](const TransportReply &reply) {
        if (!reply.ok) {
            const bool isTimeout = (reply.status == 0);
            const bool unknown = (reply.status >= 500);
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit diceFinished(failed(reply.error, {}, isTimeout, unknown));
            return;
        }
        QString type;
        const QString apiError = firstErrorText(reply.json, &type);
        if (!apiError.isEmpty()) {
            LiveBetResult r = failed(apiError, type, isRetryable(type, apiError), false);
            r.retryDelayMs = fastRetryDelayMs(type, apiError);
            emit diceFinished(r);
            return;
        }
        const QJsonObject play = reply.json.value(QStringLiteral("data")).toObject()
                                     .value(QStringLiteral("towerAuto")).toObject();
        const QJsonArray steps = play.value(QStringLiteral("shuffleOriginalActions")).toArray();
        const QJsonObject tower = steps.isEmpty()
            ? QJsonObject()
            : steps.last().toObject().value(QStringLiteral("action")).toObject()
                  .value(QStringLiteral("tower")).toObject();
        if (play.isEmpty() || tower.isEmpty()) {
            emit diceFinished(failed(tr("Unexpected Shuffle tower response (no towerAuto result)."), {}, true, false));
            return;
        }
        // Stake's (Dragon) Tower shape: state {difficulty, currentRound (floors cleared),
        // rounds (the safe tiles of each floor), playedRounds (the picks)}.
        const QJsonArray floors = tower.value(QStringLiteral("towerResult")).toArray();
        const QJsonArray picks = tower.value(QStringLiteral("selected")).toArray();
        int cleared = 0;
        QJsonArray played;
        for (int i = 0; i < picks.size(); ++i) {
            played.append(QJsonArray{picks.at(i)});
            if (cleared == i && i < floors.size() && floors.at(i).toArray().contains(picks.at(i)))
                ++cleared;
        }
        QJsonObject state;
        state.insert(QStringLiteral("difficulty"), tower.value(QStringLiteral("towerDifficulty")).toString().toLower());
        state.insert(QStringLiteral("currentRound"), cleared);
        state.insert(QStringLiteral("rounds"), floors);
        state.insert(QStringLiteral("playedRounds"), played);
        const QJsonValue m = play.value(QStringLiteral("multiplier"));
        const double mult = (m.isNull() || m.isUndefined()) ? numberOf(tower.value(QStringLiteral("winMultiplier")))
                                                            : numberOf(m);
        QJsonObject bet;
        bet.insert(QStringLiteral("id"), play.value(QStringLiteral("id")).toString());
        bet.insert(QStringLiteral("amount"), numberOf(play.value(QStringLiteral("amount"))));
        bet.insert(QStringLiteral("payout"), numberOf(play.value(QStringLiteral("payout"))));
        bet.insert(QStringLiteral("payoutMultiplier"), mult);
        bet.insert(QStringLiteral("active"), false);
        bet.insert(QStringLiteral("currency"), play.value(QStringLiteral("currency")).toString().toLower());
        bet.insert(QStringLiteral("game"), QStringLiteral("dragonTower"));
        bet.insert(QStringLiteral("state"), state);
        LiveBetResult r = gameResult(Game::DragonTower, bet, params, requestedAmount);
        const QJsonValue after = play.value(QStringLiteral("afterBalance"));
        if (after.isString() || after.isDouble())
            r.balanceAfter = numberOf(after);
        emit diceFinished(r);
    });
}

// ---------------------------------------------------------------- Shuffle wheel
void StakeClient::placeShuffleWheel(const GameBetRequest &request)
{
    QString endpoint;
    QJsonObject stakeBody;
    QString err;
    if (!StakeGames::buildBetRequest(Game::Wheel, request.amount, m_currency, request.params,
                                     &endpoint, &stakeBody, &err)) {
        const LiveBetResult r = failed(err);
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }
    // Shuffle writes the segment count as a word (TEN .. FIFTY) and the risk in capitals.
    static const QMap<int, QString> words = {{10, QStringLiteral("TEN")}, {20, QStringLiteral("TWENTY")},
                                             {30, QStringLiteral("THIRTY")}, {40, QStringLiteral("FORTY")},
                                             {50, QStringLiteral("FIFTY")}};
    const int segments = stakeBody.value(QStringLiteral("segments")).toInt(10);
    if (!words.contains(segments)) {
        const LiveBetResult r = failed(tr("Wheel: segments must be 10, 20, 30, 40 or 50."));
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }
    QJsonObject data;
    data.insert(QStringLiteral("amount"), QString::number(request.amount, 'f', 8));
    data.insert(QStringLiteral("currency"), ShuffleTransport::apiCurrency(m_currency));
    data.insert(QStringLiteral("risk"), stakeBody.value(QStringLiteral("risk")).toString().toUpper());
    data.insert(QStringLiteral("segments"), words.value(segments));
    data.insert(QStringLiteral("windowId"), shuffleWindowId());
    data.insert(QStringLiteral("autoBetInfo"), shuffleAutoBetInfo());
    data.insert(QStringLiteral("usdAmount"), 0);

    QJsonObject body;
    body.insert(QStringLiteral("operationName"), QStringLiteral("WheelPlay"));
    body.insert(QStringLiteral("variables"), QJsonObject{{QStringLiteral("data"), data}});
    body.insert(QStringLiteral("query"), QStringLiteral(
        "mutation WheelPlay($data: WheelPlayInput!) {\n  wheelPlay(data: $data) {\n    id\n    currency\n"
        "    amount\n    payout\n    createdAt\n    shuffleOriginalActions {\n      id\n      action {\n"
        "        wheel {\n          resultRaw\n          resultSegment\n          risk\n          segments\n"
        "          __typename\n        }\n        __typename\n      }\n      __typename\n    }\n"
        "    afterBalance\n    multiplier\n    __typename\n  }\n}"));

    const QJsonObject params = request.params;
    const double requestedAmount = request.amount;
    m_transport->post(QStringLiteral("main-api/graphql/api/graphql"), body,
                      [this, params, requestedAmount](const TransportReply &reply) {
        if (!reply.ok) {
            const bool isTimeout = (reply.status == 0);
            const bool unknown = (reply.status >= 500);
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit diceFinished(failed(reply.error, {}, isTimeout, unknown));
            return;
        }
        QString type;
        const QString apiError = firstErrorText(reply.json, &type);
        if (!apiError.isEmpty()) {
            LiveBetResult r = failed(apiError, type, isRetryable(type, apiError), false);
            r.retryDelayMs = fastRetryDelayMs(type, apiError);
            emit diceFinished(r);
            return;
        }
        const QJsonObject play = reply.json.value(QStringLiteral("data")).toObject()
                                     .value(QStringLiteral("wheelPlay")).toObject();
        const QJsonArray steps = play.value(QStringLiteral("shuffleOriginalActions")).toArray();
        const QJsonObject wheel = steps.isEmpty()
            ? QJsonObject()
            : steps.last().toObject().value(QStringLiteral("action")).toObject()
                  .value(QStringLiteral("wheel")).toObject();
        if (play.isEmpty() || wheel.isEmpty()) {
            emit diceFinished(failed(tr("Unexpected Shuffle wheel response (no wheelPlay result)."), {}, true, false));
            return;
        }
        // Stake's wheel shape: state {risk, segments (number), result (segment index)}.
        static const QMap<QString, int> numbers = {{QStringLiteral("TEN"), 10}, {QStringLiteral("TWENTY"), 20},
                                                   {QStringLiteral("THIRTY"), 30}, {QStringLiteral("FORTY"), 40},
                                                   {QStringLiteral("FIFTY"), 50}};
        QJsonObject state;
        state.insert(QStringLiteral("risk"), wheel.value(QStringLiteral("risk")).toString().toLower());
        state.insert(QStringLiteral("segments"),
                     numbers.value(wheel.value(QStringLiteral("segments")).toString().toUpper(), 0));
        state.insert(QStringLiteral("result"), wheel.value(QStringLiteral("resultSegment")).toInt());
        const double mult = numberOf(play.value(QStringLiteral("multiplier")));
        QJsonObject bet;
        bet.insert(QStringLiteral("id"), play.value(QStringLiteral("id")).toString());
        bet.insert(QStringLiteral("amount"), numberOf(play.value(QStringLiteral("amount"))));
        bet.insert(QStringLiteral("payout"), numberOf(play.value(QStringLiteral("payout"))));
        bet.insert(QStringLiteral("payoutMultiplier"), mult);
        bet.insert(QStringLiteral("active"), false);
        bet.insert(QStringLiteral("currency"), play.value(QStringLiteral("currency")).toString().toLower());
        bet.insert(QStringLiteral("game"), QStringLiteral("wheel"));
        bet.insert(QStringLiteral("state"), state);
        LiveBetResult r = gameResult(Game::Wheel, bet, params, requestedAmount);
        const QJsonValue after = play.value(QStringLiteral("afterBalance"));
        if (after.isString() || after.isDouble())
            r.balanceAfter = numberOf(after);
        emit diceFinished(r);
    });
}

// ---------------------------------------------------------------- Shuffle plinko
void StakeClient::placeShufflePlinko(const GameBetRequest &request)
{
    // Rows and risk as Stake's request would have them.
    QString endpoint;
    QJsonObject stakeBody;
    QString err;
    if (!StakeGames::buildBetRequest(Game::Plinko, request.amount, m_currency, request.params,
                                     &endpoint, &stakeBody, &err)) {
        const LiveBetResult r = failed(err);
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }
    const QJsonObject vars = stakeBody.value(QStringLiteral("variables")).toObject();
    if (vars.value(QStringLiteral("risk")).toString().compare(QLatin1String("expert"), Qt::CaseInsensitive) == 0) {
        const LiveBetResult r = failed(tr("Shuffle plinko has no expert risk - use low, medium or high."));
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }
    QJsonObject data;
    data.insert(QStringLiteral("amount"), QString::number(request.amount, 'f', 8));
    data.insert(QStringLiteral("currency"), ShuffleTransport::apiCurrency(m_currency));
    // Shuffle's risk names: LOW_RISK, MEDIUM_RISK, HIGH_RISK (it has no expert risk).
    data.insert(QStringLiteral("risk"), vars.value(QStringLiteral("risk")).toString().toUpper()
                                            + QStringLiteral("_RISK"));
    data.insert(QStringLiteral("rows"), vars.value(QStringLiteral("rows")).toInt(8));
    data.insert(QStringLiteral("autoBetInfo"), shuffleAutoBetInfo());
    data.insert(QStringLiteral("windowId"), shuffleWindowId());
    data.insert(QStringLiteral("usdAmount"), 0);

    QJsonObject body;
    body.insert(QStringLiteral("operationName"), QStringLiteral("PlinkoPlay"));
    body.insert(QStringLiteral("variables"), QJsonObject{{QStringLiteral("data"), data}});
    body.insert(QStringLiteral("query"), QStringLiteral(
        "mutation PlinkoPlay($data: PlinkoPlayInput!) {\n  plinkoPlay(data: $data) {\n    id\n    currency\n"
        "    amount\n    multiplier\n    payout\n    shuffleOriginalActions {\n      id\n      action {\n"
        "        plinko {\n          risk\n          results\n          rows\n          multiplier\n"
        "          __typename\n        }\n        __typename\n      }\n      updatedAt\n      createdAt\n"
        "      __typename\n    }\n    afterBalance\n    __typename\n  }\n}"));

    const QJsonObject params = request.params;
    const double requestedAmount = request.amount;
    m_transport->post(QStringLiteral("main-api/graphql/api/graphql"), body,
                      [this, params, requestedAmount](const TransportReply &reply) {
        if (!reply.ok) {
            const bool isTimeout = (reply.status == 0);
            const bool unknown = (reply.status >= 500);
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit diceFinished(failed(reply.error, {}, isTimeout, unknown));
            return;
        }
        QString type;
        const QString apiError = firstErrorText(reply.json, &type);
        if (!apiError.isEmpty()) {
            LiveBetResult r = failed(apiError, type, isRetryable(type, apiError), false);
            r.retryDelayMs = fastRetryDelayMs(type, apiError);
            emit diceFinished(r);
            return;
        }
        const QJsonObject play = reply.json.value(QStringLiteral("data")).toObject()
                                     .value(QStringLiteral("plinkoPlay")).toObject();
        const QJsonArray steps = play.value(QStringLiteral("shuffleOriginalActions")).toArray();
        const QJsonObject plinko = steps.isEmpty()
            ? QJsonObject()
            : steps.last().toObject().value(QStringLiteral("action")).toObject()
                  .value(QStringLiteral("plinko")).toObject();
        if (play.isEmpty() || plinko.isEmpty()) {
            emit diceFinished(failed(tr("Unexpected Shuffle plinko response (no plinkoPlay result)."), {}, true, false));
            return;
        }
        // Stake's plinko shape: state {risk, rows, path} (path: Shuffle's 0 = left / 1 = right
        // per row, as "L" / "R").
        QString risk = plinko.value(QStringLiteral("risk")).toString().toLower();
        if (risk.endsWith(QLatin1String("_risk")))
            risk.chop(5);
        QJsonArray path;
        for (const QJsonValue &v : plinko.value(QStringLiteral("results")).toArray())
            path.append(v.toInt() ? QStringLiteral("R") : QStringLiteral("L"));
        QJsonObject state;
        state.insert(QStringLiteral("risk"), risk);
        state.insert(QStringLiteral("rows"), plinko.value(QStringLiteral("rows")).toInt());
        state.insert(QStringLiteral("path"), path);
        const QJsonValue m = play.value(QStringLiteral("multiplier"));
        const double mult = (m.isNull() || m.isUndefined()) ? numberOf(plinko.value(QStringLiteral("multiplier")))
                                                            : numberOf(m);
        QJsonObject bet;
        bet.insert(QStringLiteral("id"), play.value(QStringLiteral("id")).toString());
        bet.insert(QStringLiteral("amount"), numberOf(play.value(QStringLiteral("amount"))));
        bet.insert(QStringLiteral("payout"), numberOf(play.value(QStringLiteral("payout"))));
        bet.insert(QStringLiteral("payoutMultiplier"), mult);
        bet.insert(QStringLiteral("active"), false);
        bet.insert(QStringLiteral("currency"), play.value(QStringLiteral("currency")).toString().toLower());
        bet.insert(QStringLiteral("game"), QStringLiteral("plinko"));
        bet.insert(QStringLiteral("state"), state);
        LiveBetResult r = gameResult(Game::Plinko, bet, params, requestedAmount);
        const QJsonValue after = play.value(QStringLiteral("afterBalance"));
        if (after.isString() || after.isDouble())
            r.balanceAfter = numberOf(after);
        emit diceFinished(r);
    });
}

// ---------------------------------------------------------------- Shuffle mines
void StakeClient::placeShuffleMines(const GameBetRequest &request)
{
    // Tiles and mines count as Stake's request would have them (1..25 tiles sent 0-based:
    // Shuffle numbers the board 0 (top-left) .. 24 too).
    QString endpoint;
    QJsonObject stakeBody;
    QString err;
    if (!StakeGames::buildBetRequest(Game::Mines, request.amount, m_currency, request.params,
                                     &endpoint, &stakeBody, &err)) {
        const LiveBetResult r = failed(err);
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }
    // The site sends its auto-bet bookkeeping with every MinesAutoBet; the same defaults here.
    const QJsonObject autoInfo = shuffleAutoBetInfo();

    QJsonObject data;
    data.insert(QStringLiteral("selected"), stakeBody.value(QStringLiteral("fields")).toArray());
    data.insert(QStringLiteral("amount"), QString::number(request.amount, 'f', 8));
    data.insert(QStringLiteral("currency"), ShuffleTransport::apiCurrency(m_currency));
    data.insert(QStringLiteral("minesCount"), stakeBody.value(QStringLiteral("minesCount")).toInt(1));
    data.insert(QStringLiteral("autoBetInfo"), autoInfo);

    QJsonObject body;
    body.insert(QStringLiteral("operationName"), QStringLiteral("MinesAutoBet"));
    body.insert(QStringLiteral("variables"), QJsonObject{{QStringLiteral("data"), data}});
    body.insert(QStringLiteral("query"), QStringLiteral(
        "mutation MinesAutoBet($data: MinesBetInput!) {\n  minesAuto(data: $data) {\n"
        "    ...MinesBetFields\n    __typename\n  }\n}\n\n"
        "fragment MinesBetFields on Bet {\n  id\n  amount\n  payout\n  currency\n  multiplier\n"
        "  game {\n    id\n    name\n    slug\n    edge\n    __typename\n  }\n"
        "  shuffleOriginalActions {\n    id\n    updatedAt\n    createdAt\n    action {\n"
        "      mines {\n        minesResult\n        minesCount\n        winMultiplier\n"
        "        selected\n        __typename\n      }\n      __typename\n    }\n    __typename\n  }\n"
        "  gameSeedNonce\n  afterBalance\n  __typename\n}"));

    const QJsonObject params = request.params;
    const double requestedAmount = request.amount;
    m_transport->post(QStringLiteral("main-api/graphql/api/graphql"), body,
                      [this, params, requestedAmount](const TransportReply &reply) {
        if (!reply.ok) {
            const bool isTimeout = (reply.status == 0);
            const bool unknown = (reply.status >= 500);
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit diceFinished(failed(reply.error, {}, isTimeout, unknown));
            return;
        }
        QString type;
        const QString apiError = firstErrorText(reply.json, &type);
        if (!apiError.isEmpty()) {
            LiveBetResult r = failed(apiError, type, isRetryable(type, apiError), false);
            r.retryDelayMs = fastRetryDelayMs(type, apiError);
            emit diceFinished(r);
            return;
        }
        const QJsonObject play = reply.json.value(QStringLiteral("data")).toObject()
                                     .value(QStringLiteral("minesAuto")).toObject();
        const QJsonArray steps = play.value(QStringLiteral("shuffleOriginalActions")).toArray();
        const QJsonObject mines = steps.isEmpty()
            ? QJsonObject()
            : steps.last().toObject().value(QStringLiteral("action")).toObject()
                  .value(QStringLiteral("mines")).toObject();
        if (play.isEmpty() || mines.isEmpty()) {
            emit diceFinished(failed(tr("Unexpected Shuffle mines response (no minesAuto result)."), {}, true, false));
            return;
        }
        // Stake's mines shape: state {mines, minesCount, rounds: [{field, payoutMultiplier}]}.
        const QJsonValue m = play.value(QStringLiteral("multiplier"));
        const double mult = (m.isNull() || m.isUndefined()) ? numberOf(mines.value(QStringLiteral("winMultiplier")))
                                                            : numberOf(m);
        QJsonArray rounds;
        for (const QJsonValue &f : mines.value(QStringLiteral("selected")).toArray()) {
            QJsonObject round;
            round.insert(QStringLiteral("field"), f.toInt());
            round.insert(QStringLiteral("payoutMultiplier"), mult);
            rounds.append(round);
        }
        QJsonObject state;
        state.insert(QStringLiteral("mines"), mines.value(QStringLiteral("minesResult")).toArray());
        state.insert(QStringLiteral("minesCount"), mines.value(QStringLiteral("minesCount")).toInt());
        state.insert(QStringLiteral("rounds"), rounds);
        QJsonObject bet;
        bet.insert(QStringLiteral("id"), play.value(QStringLiteral("id")).toString());
        bet.insert(QStringLiteral("amount"), numberOf(play.value(QStringLiteral("amount"))));
        bet.insert(QStringLiteral("payout"), numberOf(play.value(QStringLiteral("payout"))));
        bet.insert(QStringLiteral("payoutMultiplier"), mult);
        bet.insert(QStringLiteral("active"), false);
        bet.insert(QStringLiteral("currency"), play.value(QStringLiteral("currency")).toString().toLower());
        bet.insert(QStringLiteral("game"), QStringLiteral("mines"));
        bet.insert(QStringLiteral("state"), state);
        LiveBetResult r = gameResult(Game::Mines, bet, params, requestedAmount);
        if (play.contains(QStringLiteral("gameSeedNonce")))
            r.nonce = qint64(numberOf(play.value(QStringLiteral("gameSeedNonce"))));
        const QJsonValue after = play.value(QStringLiteral("afterBalance"));
        if (after.isString() || after.isDouble())
            r.balanceAfter = numberOf(after);
        emit diceFinished(r);
    });
}

// ---------------------------------------------------------------- Shuffle hilo
void StakeClient::placeShuffleHilo(const GameBetRequest &request)
{
    // The start card as Stake's request would have it (Manual / Auto tab rank + suit, or a
    // script's startcard), then as Shuffle's card number.
    QString endpoint;
    QJsonObject stakeBody;
    QString err;
    if (!StakeGames::buildBetRequest(Game::Hilo, request.amount, m_currency, request.params,
                                     &endpoint, &stakeBody, &err)) {
        const LiveBetResult r = failed(err);
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }
    const QJsonObject card = stakeBody.value(QStringLiteral("startCard")).toObject();
    QJsonObject data;
    data.insert(QStringLiteral("amount"), QString::number(request.amount, 'f', 8));
    data.insert(QStringLiteral("currency"), ShuffleTransport::apiCurrency(m_currency));
    data.insert(QStringLiteral("startCard"),
                shuffleCardNumber(card.value(QStringLiteral("rank")).toString(),
                                  card.value(QStringLiteral("suit")).toString()));
    data.insert(QStringLiteral("usdAmount"), 0);
    postShuffleHilo(QStringLiteral("HiloStart"),
                    QStringLiteral("mutation HiloStart($data: HiloStartInput!) {\n  hiloStart(data: $data) {\n"
                                   "    ...HiloBetFields\n    __typename\n  }\n}\n\n")
                        + QLatin1String(kShuffleHiloFields),
                    QJsonObject{{QStringLiteral("data"), data}}, QStringLiteral("hiloStart"),
                    request.params, request.amount, true);
}

void StakeClient::sendShuffleHilo(const QString &endpoint, const QJsonObject &stakeBody,
                                  const QJsonObject &params, double requestedAmount)
{
    const auto failNow = [this](const QString &msg) {
        const LiveBetResult r = failed(msg);
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
    };
    if (m_shuffleHiloBetId.isEmpty()) {
        failNow(tr("Shuffle hilo: no open round."));
        return;
    }
    QJsonObject data;
    data.insert(QStringLiteral("betId"), m_shuffleHiloBetId);
    if (endpoint.contains(QLatin1String("cashout"))) {
        // HiloCashout takes the bet id directly (no data input); the answer ends with a
        // CASHOUT step that has no card.
        postShuffleHilo(QStringLiteral("HiloCashout"),
                        QStringLiteral("mutation HiloCashout($betId: String) {\n"
                                       "  hiloCashout(betId: $betId) {\n    ...HiloBetFields\n"
                                       "    __typename\n  }\n}\n\n")
                            + QLatin1String(kShuffleHiloFields),
                        QJsonObject{{QStringLiteral("betId"), m_shuffleHiloBetId}},
                        QStringLiteral("hiloCashout"), params, requestedAmount, false);
        return;
    }
    // Stake's guesses -> Shuffle's (it only has same-or-above / same-or-below / skip; "equal"
    // is same-or-below on an ace and same-or-above on a king).
    const QString g = stakeBody.value(QStringLiteral("guess")).toString();
    const QString rank = m_shuffleHiloRank;
    QString guess;
    if (g == QLatin1String("skip"))
        guess = QStringLiteral("SKIP");
    else if (g == QLatin1String("higher") || g == QLatin1String("higherEqual"))
        guess = QStringLiteral("SAME_OR_ABOVE");
    else if (g == QLatin1String("lower") || g == QLatin1String("lowerEqual"))
        guess = QStringLiteral("SAME_OR_BELOW");
    else if (g == QLatin1String("equal")) {
        if (rank == QLatin1String("A"))
            guess = QStringLiteral("SAME_OR_BELOW");
        else if (rank == QLatin1String("K"))
            guess = QStringLiteral("SAME_OR_ABOVE");
        else {
            failNow(tr("Shuffle hilo: Equal is only possible on an Ace or a King."));
            return;
        }
    }
    if (guess.isEmpty()) {
        failNow(tr("Shuffle hilo: unknown guess \"%1\".").arg(g));
        return;
    }
    data.insert(QStringLiteral("guess"), guess);
    data.insert(QStringLiteral("cardCount"), m_shuffleHiloSteps); // steps reported so far
    postShuffleHilo(QStringLiteral("HiloNext"),
                    QStringLiteral("mutation HiloNext($data: HiloNextInput!) {\n  hiloNext(data: $data) {\n"
                                   "    ...HiloBetFields\n    __typename\n  }\n}\n\n")
                        + QLatin1String(kShuffleHiloFields),
                    QJsonObject{{QStringLiteral("data"), data}}, QStringLiteral("hiloNext"),
                    params, requestedAmount, false);
}

void StakeClient::fetchShuffleHilo(const QJsonObject &params, double requestedAmount)
{
    postShuffleHilo(QStringLiteral("HiloActiveBet"),
                    QStringLiteral("query HiloActiveBet {\n  hiloActiveBet {\n    ...HiloBetFields\n"
                                   "    __typename\n  }\n}\n\n")
                        + QLatin1String(kShuffleHiloFields),
                    QJsonObject(), QStringLiteral("hiloActiveBet"), params, requestedAmount, false);
}

void StakeClient::postShuffleHilo(const QString &operation, const QString &query, const QJsonObject &variables,
                                  const QString &resultKey, const QJsonObject &params, double requestedAmount,
                                  bool firstRequest)
{
    QJsonObject body;
    body.insert(QStringLiteral("operationName"), operation);
    body.insert(QStringLiteral("variables"), variables);
    body.insert(QStringLiteral("query"), query);
    const bool activeQuery = resultKey == QLatin1String("hiloActiveBet");
    m_transport->post(QStringLiteral("main-api/graphql/api/graphql"), body,
                      [this, resultKey, params, requestedAmount, firstRequest, activeQuery](const TransportReply &reply) {
        if (!reply.ok) {
            const bool isTimeout = (reply.status == 0);
            const bool unknown = (reply.status >= 500);
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit diceFinished(failed(reply.error, {}, isTimeout, unknown));
            return;
        }
        QString type;
        const QString apiError = firstErrorText(reply.json, &type);
        if (!apiError.isEmpty()) {
            // A round is still open (an earlier run, the website): fetch it to finish it first.
            if (firstRequest && (containsCI(apiError, "active") || containsCI(apiError, "existing")
                                 || containsCI(apiError, "already") || containsCI(apiError, "progress")
                                 || containsCI(type, "existing") || containsCI(type, "active"))) {
                fetchShuffleHilo(params, requestedAmount);
                return;
            }
            LiveBetResult r = failed(apiError, type, isRetryable(type, apiError), false);
            r.retryDelayMs = fastRetryDelayMs(type, apiError);
            emit diceFinished(r);
            return;
        }
        const QJsonObject raw = reply.json.value(QStringLiteral("data")).toObject().value(resultKey).toObject();
        if (raw.isEmpty()) {
            if (activeQuery) {
                LiveBetResult r = failed(tr("Hilo: no open round."), QStringLiteral("existingGame"), true, false);
                r.retryDelayMs = 500;
                emit diceFinished(r);
                return;
            }
            emit diceFinished(failed(tr("Unexpected Shuffle hilo response (no bet)."), {}, false, true));
            return;
        }
        const QJsonObject bet = shuffleHiloAsStake(raw, &m_shuffleHiloRank);
        m_shuffleHiloBetId = raw.value(QStringLiteral("id")).toString();
        m_shuffleHiloSteps = int(raw.value(QStringLiteral("shuffleOriginalActions")).toArray().size());
        LiveBetResult r = gameResult(Game::Hilo, bet, params, requestedAmount);
        if (activeQuery) {
            r.active = true;
            r.win = false;
        }
        if (!r.active) {
            m_shuffleHiloBetId.clear();
            const QJsonValue after = raw.value(QStringLiteral("afterBalance"));
            if (after.isString() || after.isDouble())
                r.balanceAfter = numberOf(after);
        }
        emit diceFinished(r);
    });
}

void StakeClient::placeShuffleDice(const DiceBetRequest &request)
{
    const double target = std::round(request.target * 100.0) / 100.0;

    QJsonObject data;
    data.insert(QStringLiteral("amount"), QString::number(request.amount, 'f', 8));
    data.insert(QStringLiteral("bet"), QString::number(target, 'f', 2));
    data.insert(QStringLiteral("currency"), ShuffleTransport::apiCurrency(m_currency));
    data.insert(QStringLiteral("direction"),
                request.over ? QStringLiteral("ABOVE") : QStringLiteral("BELOW"));

    QJsonObject variables;
    variables.insert(QStringLiteral("data"), data);

    QJsonObject body;
    body.insert(QStringLiteral("operationName"), QStringLiteral("DicePlay"));
    body.insert(QStringLiteral("variables"), variables);
    body.insert(QStringLiteral("query"), QStringLiteral(
        "mutation DicePlay($data: DicePlayInput!) {\n"
        "  dicePlay(data: $data) {\n"
        "    id\n"
        "    currency\n"
        "    amount\n"
        "    payout\n"
        "    afterBalance\n"
        "    shuffleOriginalActions {\n"
        "      id\n"
        "      action {\n"
        "        dice {\n"
        "          ...DiceFields\n"
        "          __typename\n"
        "        }\n"
        "        __typename\n"
        "      }\n"
        "      __typename\n"
        "    }\n"
        "    updatedAt\n"
        "    createdAt\n"
        "    __typename\n"
        "  }\n"
        "}\n"
        "\n"
        "fragment DiceFields on DiceActionModel {\n"
        "  userDiceDirection\n"
        "  userValue\n"
        "  resultValue\n"
        "  resultRaw\n"
        "  __typename\n"
        "}"));

    const double requestedAmount = request.amount;
    m_transport->post(QStringLiteral("main-api/graphql/api/graphql"), body,
                      [this, requestedAmount](const TransportReply &reply) {
        if (!reply.ok) {
            // Same policy as the Stake bets: a timeout is retried, a 5xx may have gone through.
            const bool isTimeout = (reply.status == 0);
            const bool unknown = (reply.status >= 500);
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit diceFinished(failed(reply.error, {}, isTimeout, unknown));
            return;
        }

        QString type;
        const QString apiError = firstErrorText(reply.json, &type);
        if (!apiError.isEmpty()) {
            LiveBetResult r = failed(apiError, type, isRetryable(type, apiError), false);
            r.retryDelayMs = fastRetryDelayMs(type, apiError);
            emit diceFinished(r);
            return;
        }

        const QJsonObject play = reply.json.value(QStringLiteral("data")).toObject()
                                     .value(QStringLiteral("dicePlay")).toObject();
        const QJsonArray actions = play.value(QStringLiteral("shuffleOriginalActions")).toArray();
        const QJsonObject dice = actions.isEmpty()
            ? QJsonObject()
            : actions.first().toObject().value(QStringLiteral("action")).toObject()
                  .value(QStringLiteral("dice")).toObject();
        if (play.isEmpty() || dice.isEmpty()) {
            emit diceFinished(failed(tr("Unexpected bet response (no dicePlay result)."), {}, true, false));
            return;
        }

        LiveBetResult r;
        r.ok = true;
        r.amount = play.contains(QStringLiteral("amount")) ? numberOf(play.value(QStringLiteral("amount")))
                                                           : requestedAmount;
        r.payout = numberOf(play.value(QStringLiteral("payout")));
        r.roll = numberOf(dice.value(QStringLiteral("resultValue")));
        r.betId = play.value(QStringLiteral("id")).toString();
        r.nonce = -1; // the dice mutation does not return the seed nonce

        const double userValue = numberOf(dice.value(QStringLiteral("userValue")));
        const bool above = dice.value(QStringLiteral("userDiceDirection")).toString()
                               .compare(QLatin1String("ABOVE"), Qt::CaseInsensitive) == 0;

        // The server's payout is the truth; for a 0 bet fall back to comparing the roll.
        // Win only when the payout multiplier is >= 1x (a partial payout is a loss).
        r.win = r.amount > 0.0 ? BotConstants::isWinMultiplier(r.payout / r.amount)
                               : (above ? r.roll > userValue : r.roll < userValue);

        if (r.win) {
            const double chance = above ? 100.0 - userValue : userValue;
            r.multiplier = (r.amount > 0.0 && r.payout > 0.0)
                               ? r.payout / r.amount
                               : (chance > 0.0 ? 99.0 / chance : 0.0);
        }
        emit diceFinished(r);
    });
}

// ---------------------------------------------------------------- Goated dice
namespace
{
// Looks `names` up in `o` and then, breadth-first, in nested objects (depth <= 3), because the
// bet may come wrapped ({"data": {"bet": {...}}}). Returns true and fills `out` when found.
bool findGoatedValue(const QJsonObject &o, const QStringList &names, QJsonValue *out, int depth = 0)
{
    for (const QString &n : names) {
        const QJsonValue v = o.value(n);
        if (!v.isUndefined() && !v.isNull() && !v.isObject() && !v.isArray()) {
            *out = v;
            return true;
        }
    }
    if (depth >= 3)
        return false;
    for (auto it = o.constBegin(); it != o.constEnd(); ++it) {
        if (it.value().isObject() && findGoatedValue(it.value().toObject(), names, out, depth + 1))
            return true;
    }
    return false;
}

bool goatedNumber(const QJsonObject &o, const QStringList &names, double *out)
{
    QJsonValue v;
    if (!findGoatedValue(o, names, &v))
        return false;
    if (v.isDouble()) {
        *out = v.toDouble();
        return true;
    }
    if (v.isString()) {
        bool ok = false;
        const double d = v.toString().toDouble(&ok);
        if (ok) {
            *out = d;
            return true;
        }
    }
    return false;
}

bool goatedBool(const QJsonObject &o, const QStringList &names, bool *out)
{
    QJsonValue v;
    if (!findGoatedValue(o, names, &v))
        return false;
    if (v.isBool()) {
        *out = v.toBool();
        return true;
    }
    if (v.isString()) {
        const QString s = v.toString().trimmed().toLower();
        if (s == QLatin1String("win") || s == QLatin1String("won") || s == QLatin1String("true")) {
            *out = true;
            return true;
        }
        if (s == QLatin1String("loss") || s == QLatin1String("lose") || s == QLatin1String("lost")
            || s == QLatin1String("false")) {
            *out = false;
            return true;
        }
    }
    return false;
}

// Error text of a Goated answer: "message" / "error" (string or object) / "errors":[...].
QString goatedErrorText(const QJsonObject &json)
{
    const QJsonValue err = json.value(QStringLiteral("error"));
    if (err.isString() && !err.toString().isEmpty())
        return err.toString();
    if (err.isObject()) {
        const QString m = err.toObject().value(QStringLiteral("message")).toString();
        if (!m.isEmpty())
            return m;
    }
    const QJsonArray errs = json.value(QStringLiteral("errors")).toArray();
    if (!errs.isEmpty()) {
        const QJsonValue first = errs.first();
        const QString m = first.isString() ? first.toString()
                                           : first.toObject().value(QStringLiteral("message")).toString();
        if (!m.isEmpty())
            return m;
    }
    return json.value(QStringLiteral("message")).toString();
}
} // namespace

// Goated dice: POST https://apis.goated.com/casino/dice/v2
//   {"user_value": 51, "roll_type": "OVER", "amount": 0.1, "currency": "USDC", "is_fiat": false}
// Goated's dice has a 1 % house edge like the app (payout = 99 / chance: "UNDER 49" pays 2.0204x),
// so the app's target is sent as it is: 2x = "OVER 50.5" / "UNDER 49.5".
// The answer's layout was not captured, so it is read defensively (several field names, and
// the bet may be wrapped in "data"). If no roll can be found the raw answer is logged.
// ---------------------------------------------------------------- Goated blackjack
void StakeClient::placeGoatedBlackjack(const GameBetRequest &request)
{
    QJsonObject body;
    body.insert(QStringLiteral("amount"), std::round(request.amount * 1e8) / 1e8);
    body.insert(QStringLiteral("currency"), m_currency.toUpper());
    body.insert(QStringLiteral("perfect_pair_amount"), 0); // side bets are not played
    body.insert(QStringLiteral("twenty_one_amount"), 0);
    body.insert(QStringLiteral("is_fiat"), false);
    postGoatedBlackjack(QStringLiteral("casino/blackjack/create"), body, request.params, request.amount, true);
}

void StakeClient::sendGoatedBlackjack(const QString &stakeAction, const QJsonObject &params,
                                      double requestedAmount)
{
    static const QMap<QString, QString> endpoints = {
        {QStringLiteral("hit"), QStringLiteral("casino/blackjack/hit")},
        {QStringLiteral("stand"), QStringLiteral("casino/blackjack/stand")},
        {QStringLiteral("double"), QStringLiteral("casino/blackjack/double-down")},
        {QStringLiteral("split"), QStringLiteral("casino/blackjack/split")},
        {QStringLiteral("insurance"), QStringLiteral("casino/blackjack/buy-insurance")},
        {QStringLiteral("noInsurance"), QStringLiteral("casino/blackjack/reject-insurance")}};
    const QString endpoint = endpoints.value(stakeAction);
    if (endpoint.isEmpty()) {
        const LiveBetResult r = failed(tr("Goated blackjack: unknown action \"%1\".").arg(stakeAction));
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }
    postGoatedBlackjack(endpoint, QJsonObject(), params, requestedAmount); // body: {}
}

void StakeClient::postGoatedBlackjack(const QString &endpoint, const QJsonObject &body,
                                      const QJsonObject &params, double requestedAmount, bool firstRequest)
{
    m_transport->post(endpoint, body, [this, params, requestedAmount, firstRequest](const TransportReply &reply) {
        handleGoatedBlackjackReply(reply, params, requestedAmount, firstRequest, false);
    });
}

void StakeClient::fetchGoatedBlackjack(const QJsonObject &params, double requestedAmount)
{
    auto *goated = qobject_cast<GoatedTransport *>(m_transport);
    if (!goated) {
        const LiveBetResult r = failed(tr("Blackjack: no open hand."), QStringLiteral("existingGame"), true, false);
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }
    goated->get(QStringLiteral("casino/blackjack/active"), [this, params, requestedAmount](const TransportReply &reply) {
        handleGoatedBlackjackReply(reply, params, requestedAmount, false, true);
    });
}

void StakeClient::handleGoatedBlackjackReply(const TransportReply &reply, const QJsonObject &params,
                                             double requestedAmount, bool firstRequest, bool activeQuery)
{
    const auto nothingOpen = [this] {
        // Same answer as Stake's active-bet fetch when nothing is open.
        LiveBetResult r = failed(tr("Blackjack: no open hand."), QStringLiteral("existingGame"), true, false);
        r.retryDelayMs = 500;
        emit diceFinished(r);
    };
    if (!reply.ok) {
        if (activeQuery && reply.status == 404) {
            nothingOpen();
            return;
        }
        const bool isTimeout = (reply.status == 0);
        const bool unknown = (reply.status >= 500);
        if (reply.status == 0)
            emit sessionLost(reply.error);
        emit diceFinished(failed(reply.error, {}, isTimeout, unknown));
        return;
    }
    const QJsonObject &json = reply.json;
    const QString errText = goatedErrorText(json);
    const bool successFalse = json.contains(QStringLiteral("success"))
                              && !json.value(QStringLiteral("success")).toBool();
    if (successFalse || reply.status >= 400) {
        if (activeQuery) {
            nothingOpen();
            return;
        }
        const QString msg = errText.isEmpty()
            ? tr("Goated blackjack request rejected (HTTP %1).").arg(reply.status) : errText;
        // A hand is still open (an earlier run, the website): load it to finish it first.
        if (firstRequest && (containsCI(msg, "active") || containsCI(msg, "existing")
                             || containsCI(msg, "already") || containsCI(msg, "progress")
                             || containsCI(msg, "unfinished") || containsCI(msg, "ongoing"))) {
            fetchGoatedBlackjack(params, requestedAmount);
            return;
        }
        LiveBetResult r = failed(msg, {}, isRetryable({}, msg), false);
        r.retryDelayMs = fastRetryDelayMs({}, msg);
        emit diceFinished(r);
        return;
    }
    // {"success": true, "data": {...}} (buy-insurance may answer with the bet itself).
    const QJsonValue dataValue = json.value(QStringLiteral("data"));
    const QJsonObject raw = dataValue.isObject() ? dataValue.toObject()
                          : (json.contains(QStringLiteral("state")) ? json : QJsonObject());
    if (raw.isEmpty() || !raw.contains(QStringLiteral("state"))) {
        if (activeQuery) { // "data": null - nothing open
            nothingOpen();
            return;
        }
        const QString text = QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)).left(400);
        emit diceFinished(failed(tr("Unexpected Goated blackjack response: %1").arg(text), {}, false, true));
        return;
    }
    const QJsonObject bet = goatedBlackjackAsStake(raw);
    if (activeQuery && !bet.value(QStringLiteral("active")).toBool()) {
        nothingOpen();
        return;
    }
    LiveBetResult r = gameResult(Game::Blackjack, bet, params, requestedAmount);
    if (activeQuery) {
        r.active = true; // continue it with the next move
        r.win = false;
    }
    if (!r.active) {
        // The balance after the settled hand.
        const QJsonObject balances = raw.value(QStringLiteral("updated_balance")).toObject();
        const QString cur = m_currency.toUpper();
        if (balances.contains(cur))
            r.balanceAfter = numberOf(balances.value(cur));
    }
    emit diceFinished(r);
}

QString StakeClient::goatedAutobetSessionId()
{
    if (m_goatedAutobetSessionId.isEmpty()) {
        static const char chars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
        for (int i = 0; i < 10; ++i)
            m_goatedAutobetSessionId += QLatin1Char(chars[QRandomGenerator::global()->bounded(36)]);
    }
    return m_goatedAutobetSessionId;
}

// ---------------------------------------------------------------- Goated Goat Run
void StakeClient::placeGoatedGoatRun(const GameBetRequest &request)
{
    QString endpoint;
    QJsonObject stakeBody;
    QString err;
    if (!StakeGames::buildBetRequest(Game::GoatRun, request.amount, m_currency, request.params,
                                     &endpoint, &stakeBody, &err)) {
        const LiveBetResult r = failed(err);
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }
    // Lanes per difficulty: easy 1..24, medium 1..22, hard 1..20, expert 1..15.
    const QString difficulty = stakeBody.value(QStringLiteral("difficulty")).toString().toLower();
    const int lane = stakeBody.value(QStringLiteral("round")).toInt(1);
    const int maxLane = int(StakeGames::casinoMaximum(QStringLiteral("goated"), Game::GoatRun, QStringLiteral("steps"),
                                                      QJsonObject{{QStringLiteral("difficulty"), difficulty}}));
    if (lane < 1 || lane > maxLane) {
        const LiveBetResult r = failed(tr("Goat Run: steps must be 1 to %1 on %2 difficulty.").arg(maxLane).arg(difficulty));
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }
    QJsonObject body;
    body.insert(QStringLiteral("level"), difficulty.toUpper()); // EASY / MEDIUM / HARD / EXPERT
    body.insert(QStringLiteral("amount"), std::round(request.amount * 1e8) / 1e8);
    body.insert(QStringLiteral("currency"), m_currency.toUpper());
    body.insert(QStringLiteral("hop_to_lane_number"), lane);
    body.insert(QStringLiteral("is_fiat"), false);
    body.insert(QStringLiteral("autobet_session_id"), goatedAutobetSessionId());

    const QJsonObject params = request.params;
    const double requestedAmount = request.amount;
    m_transport->post(QStringLiteral("casino/goat-run/multi-hop-bet"), body,
                      [this, params, requestedAmount](const TransportReply &reply) {
        if (!reply.ok) {
            const bool isTimeout = (reply.status == 0);
            const bool unknown = (reply.status >= 500);
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit diceFinished(failed(reply.error, {}, isTimeout, unknown));
            return;
        }
        const QJsonObject &json = reply.json;
        const QString errText = goatedErrorText(json);
        const bool successFalse = json.contains(QStringLiteral("success"))
                                  && !json.value(QStringLiteral("success")).toBool();
        if (successFalse || reply.status >= 400) {
            const QString msg = errText.isEmpty()
                ? tr("Goated Goat Run bet rejected (HTTP %1).").arg(reply.status) : errText;
            LiveBetResult r = failed(msg, {}, isRetryable({}, msg), false);
            r.retryDelayMs = fastRetryDelayMs({}, msg);
            emit diceFinished(r);
            return;
        }
        const QJsonObject raw = json.value(QStringLiteral("data")).isObject()
                                    ? json.value(QStringLiteral("data")).toObject() : json;
        if (!raw.contains(QStringLiteral("accidental_lanes"))) {
            const QString text = QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)).left(400);
            emit diceFinished(failed(tr("Unexpected Goated Goat Run response: %1").arg(text), {}, false, true));
            return;
        }
        // Like Stake's chicken: state {difficulty, round (the lane hopped to), _deathPoint (the
        // first lane with an accident), lanes (all accident lanes)}.
        const QJsonArray accidents = raw.value(QStringLiteral("accidental_lanes")).toArray();
        int death = -1;
        for (const QJsonValue &a : accidents)
            if (death < 0 || a.toInt() < death)
                death = a.toInt();
        QJsonObject state;
        state.insert(QStringLiteral("difficulty"), raw.value(QStringLiteral("level")).toString().toLower());
        state.insert(QStringLiteral("round"), raw.value(QStringLiteral("active_lane")).toInt());
        state.insert(QStringLiteral("lanes"), accidents);
        if (death >= 0)
            state.insert(QStringLiteral("_deathPoint"), death);
        QJsonObject bet;
        bet.insert(QStringLiteral("id"), raw.value(QStringLiteral("id")).toString());
        bet.insert(QStringLiteral("amount"), numberOf(raw.value(QStringLiteral("amount"))));
        bet.insert(QStringLiteral("payout"), numberOf(raw.value(QStringLiteral("payout"))));
        bet.insert(QStringLiteral("payoutMultiplier"), numberOf(raw.value(QStringLiteral("multiplier"))));
        bet.insert(QStringLiteral("active"), false);
        bet.insert(QStringLiteral("currency"), raw.value(QStringLiteral("currency")).toString().toLower());
        bet.insert(QStringLiteral("game"), QStringLiteral("goatRun"));
        bet.insert(QStringLiteral("state"), state);
        if (raw.contains(QStringLiteral("nonce")))
            bet.insert(QStringLiteral("nonce"), raw.value(QStringLiteral("nonce")));
        LiveBetResult r = gameResult(Game::GoatRun, bet, params, requestedAmount);
        const QJsonObject balances = raw.value(QStringLiteral("updated_balance")).toObject();
        const QString cur = m_currency.toUpper();
        if (balances.contains(cur))
            r.balanceAfter = numberOf(balances.value(cur));
        emit diceFinished(r);
    });
}

// ---------------------------------------------------------------- Goated tower
void StakeClient::placeGoatedTower(const GameBetRequest &request)
{
    // Picks (1-based in the tab, sent 0-based: Goated numbers a floor's tiles from 0) and
    // difficulty as Stake's Tower request would have them.
    QString endpoint;
    QJsonObject stakeBody;
    QString err;
    if (!StakeGames::buildBetRequest(Game::DragonTower, request.amount, m_currency, request.params,
                                     &endpoint, &stakeBody, &err)) {
        const LiveBetResult r = failed(err);
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }
    QJsonObject body;
    body.insert(QStringLiteral("risk"), stakeBody.value(QStringLiteral("difficulty")).toString().toUpper()); // EASY .. MASTER
    body.insert(QStringLiteral("amount"), std::round(request.amount * 1e8) / 1e8);
    body.insert(QStringLiteral("currency"), m_currency.toUpper());
    body.insert(QStringLiteral("locations"), stakeBody.value(QStringLiteral("eggs")).toArray());
    body.insert(QStringLiteral("is_fiat"), false);
    body.insert(QStringLiteral("autobet_session_id"), goatedAutobetSessionId());

    const QJsonObject params = request.params;
    const double requestedAmount = request.amount;
    m_transport->post(QStringLiteral("casino/tower/multi-select-bet"), body,
                      [this, params, requestedAmount](const TransportReply &reply) {
        if (!reply.ok) {
            const bool isTimeout = (reply.status == 0);
            const bool unknown = (reply.status >= 500);
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit diceFinished(failed(reply.error, {}, isTimeout, unknown));
            return;
        }
        const QJsonObject &json = reply.json;
        const QString errText = goatedErrorText(json);
        const bool successFalse = json.contains(QStringLiteral("success"))
                                  && !json.value(QStringLiteral("success")).toBool();
        if (successFalse || reply.status >= 400) {
            const QString msg = errText.isEmpty()
                ? tr("Goated tower bet rejected (HTTP %1).").arg(reply.status) : errText;
            LiveBetResult r = failed(msg, {}, isRetryable({}, msg), false);
            r.retryDelayMs = fastRetryDelayMs({}, msg);
            emit diceFinished(r);
            return;
        }
        const QJsonObject raw = json.value(QStringLiteral("data")).isObject()
                                    ? json.value(QStringLiteral("data")).toObject() : json;
        if (!raw.contains(QStringLiteral("safe_positions"))) {
            const QString text = QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)).left(400);
            emit diceFinished(failed(tr("Unexpected Goated tower response: %1").arg(text), {}, false, true));
            return;
        }
        // Stake's Tower shape: state {difficulty, currentRound (floors cleared), rounds (the safe
        // tiles of each floor), playedRounds (the picks)}.
        QJsonArray floors;
        for (const QJsonValue &v : raw.value(QStringLiteral("safe_positions")).toArray()) {
            const QJsonObject level = v.toObject();
            const int i = level.value(QStringLiteral("level")).toInt(int(floors.size()));
            while (floors.size() <= i)
                floors.append(QJsonArray());
            floors[i] = level.value(QStringLiteral("positions")).toArray();
        }
        const QJsonArray picks = raw.value(QStringLiteral("selected_positions")).toArray();
        int cleared = 0;
        QJsonArray played;
        for (int i = 0; i < picks.size(); ++i) {
            played.append(QJsonArray{picks.at(i)});
            if (cleared == i && i < floors.size() && floors.at(i).toArray().contains(picks.at(i)))
                ++cleared;
        }
        QJsonObject state;
        state.insert(QStringLiteral("difficulty"), raw.value(QStringLiteral("risk")).toString().toLower());
        state.insert(QStringLiteral("currentRound"), cleared);
        state.insert(QStringLiteral("rounds"), floors);
        state.insert(QStringLiteral("playedRounds"), played);
        QJsonObject bet;
        bet.insert(QStringLiteral("id"), raw.value(QStringLiteral("id")).toString());
        bet.insert(QStringLiteral("amount"), numberOf(raw.value(QStringLiteral("amount"))));
        bet.insert(QStringLiteral("payout"), numberOf(raw.value(QStringLiteral("payout"))));
        bet.insert(QStringLiteral("payoutMultiplier"), numberOf(raw.value(QStringLiteral("multiplier"))));
        bet.insert(QStringLiteral("active"), false);
        bet.insert(QStringLiteral("currency"), raw.value(QStringLiteral("currency")).toString().toLower());
        bet.insert(QStringLiteral("game"), QStringLiteral("dragonTower"));
        bet.insert(QStringLiteral("state"), state);
        if (raw.contains(QStringLiteral("nonce")))
            bet.insert(QStringLiteral("nonce"), raw.value(QStringLiteral("nonce")));
        LiveBetResult r = gameResult(Game::DragonTower, bet, params, requestedAmount);
        const QJsonObject balances = raw.value(QStringLiteral("updated_balance")).toObject();
        const QString cur = m_currency.toUpper();
        if (balances.contains(cur))
            r.balanceAfter = numberOf(balances.value(cur));
        emit diceFinished(r);
    });
}

// ---------------------------------------------------------------- Goated diamonds
void StakeClient::placeGoatedDiamonds(const GameBetRequest &request)
{
    QJsonObject body;
    body.insert(QStringLiteral("amount"), std::round(request.amount * 1e8) / 1e8);
    body.insert(QStringLiteral("currency"), m_currency.toUpper());
    body.insert(QStringLiteral("is_fiat"), false);
    body.insert(QStringLiteral("autobet_session_id"), goatedAutobetSessionId());

    const QJsonObject params = request.params;
    const double requestedAmount = request.amount;
    m_transport->post(QStringLiteral("casino/diamonds"), body, [this, params, requestedAmount](const TransportReply &reply) {
        if (!reply.ok) {
            const bool isTimeout = (reply.status == 0);
            const bool unknown = (reply.status >= 500);
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit diceFinished(failed(reply.error, {}, isTimeout, unknown));
            return;
        }
        const QJsonObject &json = reply.json;
        const QString errText = goatedErrorText(json);
        const bool successFalse = json.contains(QStringLiteral("success"))
                                  && !json.value(QStringLiteral("success")).toBool();
        if (successFalse || reply.status >= 400) {
            const QString msg = errText.isEmpty()
                ? tr("Goated diamonds bet rejected (HTTP %1).").arg(reply.status) : errText;
            LiveBetResult r = failed(msg, {}, isRetryable({}, msg), false);
            r.retryDelayMs = fastRetryDelayMs({}, msg);
            emit diceFinished(r);
            return;
        }
        const QJsonObject raw = json.value(QStringLiteral("data")).isObject()
                                    ? json.value(QStringLiteral("data")).toObject() : json;
        if (!raw.contains(QStringLiteral("sequence"))) {
            const QString text = QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)).left(400);
            emit diceFinished(failed(tr("Unexpected Goated diamonds response: %1").arg(text), {}, false, true));
            return;
        }
        // Goated numbers its 5 gems (0..6); which number is which color is not known yet, so the
        // numbers are kept as "gems" (Stake's "hand" of color names stays empty: "Stop on
        // pattern" then stops on any win, as for Stake when the colors are missing).
        QJsonObject state;
        state.insert(QStringLiteral("gems"), raw.value(QStringLiteral("sequence")).toArray());
        QJsonObject bet;
        bet.insert(QStringLiteral("id"), raw.value(QStringLiteral("id")).toString());
        bet.insert(QStringLiteral("amount"), numberOf(raw.value(QStringLiteral("amount"))));
        bet.insert(QStringLiteral("payout"), numberOf(raw.value(QStringLiteral("payout"))));
        bet.insert(QStringLiteral("payoutMultiplier"), numberOf(raw.value(QStringLiteral("multiplier"))));
        bet.insert(QStringLiteral("active"), false);
        bet.insert(QStringLiteral("currency"), raw.value(QStringLiteral("currency")).toString().toLower());
        bet.insert(QStringLiteral("game"), QStringLiteral("diamonds"));
        bet.insert(QStringLiteral("state"), state);
        if (raw.contains(QStringLiteral("nonce")))
            bet.insert(QStringLiteral("nonce"), raw.value(QStringLiteral("nonce")));
        LiveBetResult r = gameResult(Game::Diamonds, bet, params, requestedAmount);
        const QJsonObject balances = raw.value(QStringLiteral("updated_balance")).toObject();
        const QString cur = m_currency.toUpper();
        if (balances.contains(cur))
            r.balanceAfter = numberOf(balances.value(cur));
        emit diceFinished(r);
    });
}

// ---------------------------------------------------------------- Goated wheel
void StakeClient::placeGoatedWheel(const GameBetRequest &request)
{
    QString endpoint;
    QJsonObject stakeBody;
    QString err;
    if (!StakeGames::buildBetRequest(Game::Wheel, request.amount, m_currency, request.params,
                                     &endpoint, &stakeBody, &err)) {
        const LiveBetResult r = failed(err);
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }
    const int segments = stakeBody.value(QStringLiteral("segments")).toInt(10);
    if (segments % 10 != 0 || segments < 10 || segments > 50) {
        const LiveBetResult r = failed(tr("Wheel: segments must be 10, 20, 30, 40 or 50."));
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }
    QJsonObject body;
    body.insert(QStringLiteral("risk"), stakeBody.value(QStringLiteral("risk")).toString().toUpper()); // LOW / MEDIUM / HIGH
    body.insert(QStringLiteral("segments"), segments);
    body.insert(QStringLiteral("amount"), std::round(request.amount * 1e8) / 1e8);
    body.insert(QStringLiteral("currency"), m_currency.toUpper());
    body.insert(QStringLiteral("is_fiat"), false);
    body.insert(QStringLiteral("autobet_session_id"), goatedAutobetSessionId());

    const QJsonObject params = request.params;
    const double requestedAmount = request.amount;
    m_transport->post(QStringLiteral("casino/wheel"), body, [this, params, requestedAmount](const TransportReply &reply) {
        if (!reply.ok) {
            const bool isTimeout = (reply.status == 0);
            const bool unknown = (reply.status >= 500);
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit diceFinished(failed(reply.error, {}, isTimeout, unknown));
            return;
        }
        const QJsonObject &json = reply.json;
        const QString errText = goatedErrorText(json);
        const bool successFalse = json.contains(QStringLiteral("success"))
                                  && !json.value(QStringLiteral("success")).toBool();
        if (successFalse || reply.status >= 400) {
            const QString msg = errText.isEmpty()
                ? tr("Goated wheel bet rejected (HTTP %1).").arg(reply.status) : errText;
            LiveBetResult r = failed(msg, {}, isRetryable({}, msg), false);
            r.retryDelayMs = fastRetryDelayMs({}, msg);
            emit diceFinished(r);
            return;
        }
        const QJsonObject raw = json.value(QStringLiteral("data")).isObject()
                                    ? json.value(QStringLiteral("data")).toObject() : json;
        if (!raw.contains(QStringLiteral("spin_segment"))) {
            const QString text = QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)).left(400);
            emit diceFinished(failed(tr("Unexpected Goated wheel response: %1").arg(text), {}, false, true));
            return;
        }
        // Stake's wheel shape: state {risk, segments, result (the segment the wheel stopped on)}.
        QJsonObject state;
        state.insert(QStringLiteral("risk"), raw.value(QStringLiteral("risk")).toString().toLower());
        state.insert(QStringLiteral("segments"), raw.value(QStringLiteral("segments")).toInt());
        state.insert(QStringLiteral("result"), raw.value(QStringLiteral("spin_segment")).toInt());
        QJsonObject bet;
        bet.insert(QStringLiteral("id"), raw.value(QStringLiteral("id")).toString());
        bet.insert(QStringLiteral("amount"), numberOf(raw.value(QStringLiteral("amount"))));
        bet.insert(QStringLiteral("payout"), numberOf(raw.value(QStringLiteral("payout"))));
        bet.insert(QStringLiteral("payoutMultiplier"), numberOf(raw.value(QStringLiteral("multiplier"))));
        bet.insert(QStringLiteral("active"), false);
        bet.insert(QStringLiteral("currency"), raw.value(QStringLiteral("currency")).toString().toLower());
        bet.insert(QStringLiteral("game"), QStringLiteral("wheel"));
        bet.insert(QStringLiteral("state"), state);
        if (raw.contains(QStringLiteral("nonce")))
            bet.insert(QStringLiteral("nonce"), raw.value(QStringLiteral("nonce")));
        LiveBetResult r = gameResult(Game::Wheel, bet, params, requestedAmount);
        const QJsonObject balances = raw.value(QStringLiteral("updated_balance")).toObject();
        const QString cur = m_currency.toUpper();
        if (balances.contains(cur))
            r.balanceAfter = numberOf(balances.value(cur));
        emit diceFinished(r);
    });
}

// ---------------------------------------------------------------- Goated plinko
void StakeClient::placeGoatedPlinko(const GameBetRequest &request)
{
    QString endpoint;
    QJsonObject stakeBody;
    QString err;
    if (!StakeGames::buildBetRequest(Game::Plinko, request.amount, m_currency, request.params,
                                     &endpoint, &stakeBody, &err)) {
        const LiveBetResult r = failed(err);
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }
    const QJsonObject vars = stakeBody.value(QStringLiteral("variables")).toObject();
    const QString risk = vars.value(QStringLiteral("risk")).toString().toUpper();
    if (risk == QLatin1String("EXPERT")) {
        const LiveBetResult r = failed(tr("Goated plinko has no expert risk - use low, medium or high."));
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }
    QJsonObject body;
    body.insert(QStringLiteral("risk"), risk); // LOW / MEDIUM / HIGH
    body.insert(QStringLiteral("rows"), vars.value(QStringLiteral("rows")).toInt(8));
    body.insert(QStringLiteral("amount"), std::round(request.amount * 1e8) / 1e8);
    body.insert(QStringLiteral("currency"), m_currency.toUpper());
    body.insert(QStringLiteral("is_fiat"), false);
    body.insert(QStringLiteral("autobet_session_id"), goatedAutobetSessionId());

    const QJsonObject params = request.params;
    const double requestedAmount = request.amount;
    m_transport->post(QStringLiteral("casino/plinko"), body, [this, params, requestedAmount](const TransportReply &reply) {
        if (!reply.ok) {
            const bool isTimeout = (reply.status == 0);
            const bool unknown = (reply.status >= 500);
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit diceFinished(failed(reply.error, {}, isTimeout, unknown));
            return;
        }
        const QJsonObject &json = reply.json;
        const QString errText = goatedErrorText(json);
        const bool successFalse = json.contains(QStringLiteral("success"))
                                  && !json.value(QStringLiteral("success")).toBool();
        if (successFalse || reply.status >= 400) {
            const QString msg = errText.isEmpty()
                ? tr("Goated plinko bet rejected (HTTP %1).").arg(reply.status) : errText;
            LiveBetResult r = failed(msg, {}, isRetryable({}, msg), false);
            r.retryDelayMs = fastRetryDelayMs({}, msg);
            emit diceFinished(r);
            return;
        }
        const QJsonObject raw = json.value(QStringLiteral("data")).isObject()
                                    ? json.value(QStringLiteral("data")).toObject() : json;
        if (!raw.contains(QStringLiteral("multiplier"))) {
            const QString text = QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)).left(400);
            emit diceFinished(failed(tr("Unexpected Goated plinko response: %1").arg(text), {}, false, true));
            return;
        }
        // Stake's plinko shape: state {risk, rows, point (the bucket the ball fell into)}.
        // Goated's "path" is the drawing data of the animation, not kept.
        QJsonObject state;
        state.insert(QStringLiteral("risk"), raw.value(QStringLiteral("risk")).toString().toLower());
        state.insert(QStringLiteral("rows"), raw.value(QStringLiteral("rows")).toInt());
        if (raw.contains(QStringLiteral("bucket")))
            state.insert(QStringLiteral("point"), raw.value(QStringLiteral("bucket")).toInt());
        QJsonObject bet;
        bet.insert(QStringLiteral("id"), raw.value(QStringLiteral("id")).toString());
        bet.insert(QStringLiteral("amount"), numberOf(raw.value(QStringLiteral("amount"))));
        bet.insert(QStringLiteral("payout"), numberOf(raw.value(QStringLiteral("payout"))));
        bet.insert(QStringLiteral("payoutMultiplier"), numberOf(raw.value(QStringLiteral("multiplier"))));
        bet.insert(QStringLiteral("active"), false);
        bet.insert(QStringLiteral("currency"), raw.value(QStringLiteral("currency")).toString().toLower());
        bet.insert(QStringLiteral("game"), QStringLiteral("plinko"));
        bet.insert(QStringLiteral("state"), state);
        if (raw.contains(QStringLiteral("nonce")))
            bet.insert(QStringLiteral("nonce"), raw.value(QStringLiteral("nonce")));
        LiveBetResult r = gameResult(Game::Plinko, bet, params, requestedAmount);
        const QJsonObject balances = raw.value(QStringLiteral("updated_balance")).toObject();
        const QString cur = m_currency.toUpper();
        if (balances.contains(cur))
            r.balanceAfter = numberOf(balances.value(cur));
        emit diceFinished(r);
    });
}

// ---------------------------------------------------------------- Goated mines
void StakeClient::placeGoatedMines(const GameBetRequest &request)
{
    // Tiles and mines count as Stake's request would have them (1..25 tiles sent 0-based:
    // Goated numbers the board 0 (top-left) .. 24 like Stake).
    QString endpoint;
    QJsonObject stakeBody;
    QString err;
    if (!StakeGames::buildBetRequest(Game::Mines, request.amount, m_currency, request.params,
                                     &endpoint, &stakeBody, &err)) {
        const LiveBetResult r = failed(err);
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }
    QJsonObject body;
    body.insert(QStringLiteral("num_mines"), stakeBody.value(QStringLiteral("minesCount")).toInt(1));
    body.insert(QStringLiteral("amount"), std::round(request.amount * 1e8) / 1e8);
    body.insert(QStringLiteral("currency"), m_currency.toUpper());
    body.insert(QStringLiteral("locations"), stakeBody.value(QStringLiteral("fields")).toArray());
    body.insert(QStringLiteral("is_fiat"), false);
    body.insert(QStringLiteral("autobet_session_id"), goatedAutobetSessionId());

    const QJsonObject params = request.params;
    const double requestedAmount = request.amount;
    m_transport->post(QStringLiteral("casino/mines/multi-select-bet"), body,
                      [this, params, requestedAmount](const TransportReply &reply) {
        if (!reply.ok) {
            const bool isTimeout = (reply.status == 0);
            const bool unknown = (reply.status >= 500);
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit diceFinished(failed(reply.error, {}, isTimeout, unknown));
            return;
        }
        const QJsonObject &json = reply.json;
        const QString errText = goatedErrorText(json);
        const bool successFalse = json.contains(QStringLiteral("success"))
                                  && !json.value(QStringLiteral("success")).toBool();
        if (successFalse || reply.status >= 400) {
            const QString msg = errText.isEmpty()
                ? tr("Goated mines bet rejected (HTTP %1).").arg(reply.status) : errText;
            LiveBetResult r = failed(msg, {}, isRetryable({}, msg), false);
            r.retryDelayMs = fastRetryDelayMs({}, msg);
            emit diceFinished(r);
            return;
        }
        const QJsonObject raw = json.value(QStringLiteral("data")).isObject()
                                    ? json.value(QStringLiteral("data")).toObject() : json;
        if (!raw.contains(QStringLiteral("mine_locations"))) {
            const QString text = QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)).left(400);
            emit diceFinished(failed(tr("Unexpected Goated mines response: %1").arg(text), {}, false, true));
            return;
        }
        // Stake's mines shape: state {mines, minesCount, rounds: [{field, payoutMultiplier}]}.
        const double mult = numberOf(raw.value(QStringLiteral("multiplier")));
        QJsonArray rounds;
        for (const QJsonValue &f : raw.value(QStringLiteral("picked_locations")).toArray()) {
            QJsonObject round;
            round.insert(QStringLiteral("field"), f.toInt());
            round.insert(QStringLiteral("payoutMultiplier"), mult);
            rounds.append(round);
        }
        QJsonObject state;
        state.insert(QStringLiteral("mines"), raw.value(QStringLiteral("mine_locations")).toArray());
        state.insert(QStringLiteral("minesCount"), raw.value(QStringLiteral("num_mines")).toInt());
        state.insert(QStringLiteral("rounds"), rounds);
        QJsonObject bet;
        bet.insert(QStringLiteral("id"), raw.value(QStringLiteral("id")).toString());
        bet.insert(QStringLiteral("amount"), numberOf(raw.value(QStringLiteral("amount"))));
        bet.insert(QStringLiteral("payout"), numberOf(raw.value(QStringLiteral("payout"))));
        bet.insert(QStringLiteral("payoutMultiplier"), mult);
        bet.insert(QStringLiteral("active"), false);
        bet.insert(QStringLiteral("currency"), raw.value(QStringLiteral("currency")).toString().toLower());
        bet.insert(QStringLiteral("game"), QStringLiteral("mines"));
        bet.insert(QStringLiteral("state"), state);
        if (raw.contains(QStringLiteral("nonce")))
            bet.insert(QStringLiteral("nonce"), raw.value(QStringLiteral("nonce")));
        LiveBetResult r = gameResult(Game::Mines, bet, params, requestedAmount);
        const QJsonObject balances = raw.value(QStringLiteral("updated_balance")).toObject();
        const QString cur = m_currency.toUpper();
        if (balances.contains(cur))
            r.balanceAfter = numberOf(balances.value(cur));
        emit diceFinished(r);
    });
}

// ---------------------------------------------------------------- Goated keno
void StakeClient::placeGoatedKeno(const GameBetRequest &request)
{
    // Numbers and risk as Stake's request would have them; Stake takes the numbers 0-based,
    // Goated's board is numbered 1..40 like the tab, so they go back up by one.
    QString endpoint;
    QJsonObject stakeBody;
    QString err;
    if (!StakeGames::buildBetRequest(Game::Keno, request.amount, m_currency, request.params,
                                     &endpoint, &stakeBody, &err)) {
        const LiveBetResult r = failed(err);
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }
    QJsonArray selections;
    for (const QJsonValue &n : stakeBody.value(QStringLiteral("numbers")).toArray())
        selections.append(n.toInt() + 1);
    QJsonObject body;
    body.insert(QStringLiteral("risk"), stakeBody.value(QStringLiteral("risk")).toString().toUpper()); // LOW .. HIGH, CLASSIC
    body.insert(QStringLiteral("selections"), selections);
    body.insert(QStringLiteral("amount"), std::round(request.amount * 1e8) / 1e8);
    body.insert(QStringLiteral("currency"), m_currency.toUpper());
    body.insert(QStringLiteral("is_fiat"), false);
    body.insert(QStringLiteral("autobet_session_id"), goatedAutobetSessionId());

    const QJsonObject params = request.params;
    const double requestedAmount = request.amount;
    m_transport->post(QStringLiteral("casino/keno/bet"), body, [this, params, requestedAmount](const TransportReply &reply) {
        if (!reply.ok) {
            const bool isTimeout = (reply.status == 0);
            const bool unknown = (reply.status >= 500);
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit diceFinished(failed(reply.error, {}, isTimeout, unknown));
            return;
        }
        const QJsonObject &json = reply.json;
        const QString errText = goatedErrorText(json);
        const bool successFalse = json.contains(QStringLiteral("success"))
                                  && !json.value(QStringLiteral("success")).toBool();
        if (successFalse || reply.status >= 400) {
            const QString msg = errText.isEmpty()
                ? tr("Goated keno bet rejected (HTTP %1).").arg(reply.status) : errText;
            LiveBetResult r = failed(msg, {}, isRetryable({}, msg), false);
            r.retryDelayMs = fastRetryDelayMs({}, msg);
            emit diceFinished(r);
            return;
        }
        const QJsonObject raw = json.value(QStringLiteral("data")).isObject()
                                    ? json.value(QStringLiteral("data")).toObject() : json;
        if (!raw.contains(QStringLiteral("correct_selections"))) {
            const QString text = QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)).left(400);
            emit diceFinished(failed(tr("Unexpected Goated keno response: %1").arg(text), {}, false, true));
            return;
        }
        // Stake's keno shape (0-based numbers): state {risk, selectedNumbers, drawnNumbers}.
        const auto zeroBased = [](const QJsonValue &v) {
            QJsonArray out;
            for (const QJsonValue &n : v.toArray())
                out.append(n.toInt() - 1);
            return out;
        };
        QJsonObject state;
        state.insert(QStringLiteral("risk"), raw.value(QStringLiteral("risk")).toString().toLower());
        state.insert(QStringLiteral("selectedNumbers"), zeroBased(raw.value(QStringLiteral("user_selections"))));
        state.insert(QStringLiteral("drawnNumbers"), zeroBased(raw.value(QStringLiteral("correct_selections"))));
        QJsonObject bet;
        bet.insert(QStringLiteral("id"), raw.value(QStringLiteral("id")).toString());
        bet.insert(QStringLiteral("amount"), numberOf(raw.value(QStringLiteral("amount"))));
        bet.insert(QStringLiteral("payout"), numberOf(raw.value(QStringLiteral("payout"))));
        bet.insert(QStringLiteral("payoutMultiplier"), numberOf(raw.value(QStringLiteral("multiplier"))));
        bet.insert(QStringLiteral("active"), false);
        bet.insert(QStringLiteral("currency"), raw.value(QStringLiteral("currency")).toString().toLower());
        bet.insert(QStringLiteral("game"), QStringLiteral("keno"));
        bet.insert(QStringLiteral("state"), state);
        if (raw.contains(QStringLiteral("nonce")))
            bet.insert(QStringLiteral("nonce"), raw.value(QStringLiteral("nonce")));
        LiveBetResult r = gameResult(Game::Keno, bet, params, requestedAmount);
        const QJsonObject balances = raw.value(QStringLiteral("updated_balance")).toObject();
        const QString cur = m_currency.toUpper();
        if (balances.contains(cur))
            r.balanceAfter = numberOf(balances.value(cur));
        emit diceFinished(r);
    });
}

// ---------------------------------------------------------------- Goated hilo
void StakeClient::placeGoatedHilo(const GameBetRequest &request)
{
    // The start card as Stake's request would have it ({rank, suit}) -> Goated's "AC".
    QString endpoint;
    QJsonObject stakeBody;
    QString err;
    if (!StakeGames::buildBetRequest(Game::Hilo, request.amount, m_currency, request.params,
                                     &endpoint, &stakeBody, &err)) {
        const LiveBetResult r = failed(err);
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }
    const QJsonObject card = stakeBody.value(QStringLiteral("startCard")).toObject();
    QJsonObject body;
    body.insert(QStringLiteral("first_card"), card.value(QStringLiteral("rank")).toString().toUpper()
                                                  + card.value(QStringLiteral("suit")).toString().toUpper());
    body.insert(QStringLiteral("amount"), std::round(request.amount * 1e8) / 1e8);
    body.insert(QStringLiteral("currency"), m_currency.toUpper());
    body.insert(QStringLiteral("is_fiat"), false);
    const QJsonObject params = request.params;
    const double amount = request.amount;
    m_transport->post(QStringLiteral("casino/hilo/bet"), body, [this, params, amount](const TransportReply &reply) {
        handleGoatedHiloReply(reply, params, amount, true, false);
    });
}

void StakeClient::sendGoatedHilo(const QString &endpoint, const QJsonObject &stakeBody, const QJsonObject &params,
                                 double requestedAmount)
{
    const auto failNow = [this](const QString &msg) {
        const LiveBetResult r = failed(msg);
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
    };
    if (endpoint.contains(QLatin1String("cashout"))) {
        m_transport->post(QStringLiteral("casino/hilo/cashout"), QJsonObject(),
                          [this, params, requestedAmount](const TransportReply &reply) {
            handleGoatedHiloReply(reply, params, requestedAmount, false, false);
        });
        return;
    }
    // Stake's guesses -> Goated's letters: H = same or higher, L = same or lower, S = skip;
    // "equal" is H on a king and L on an ace.
    const QString g = stakeBody.value(QStringLiteral("guess")).toString();
    QString choice;
    if (g == QLatin1String("skip"))
        choice = QStringLiteral("S");
    else if (g == QLatin1String("higher") || g == QLatin1String("higherEqual"))
        choice = QStringLiteral("H");
    else if (g == QLatin1String("lower") || g == QLatin1String("lowerEqual"))
        choice = QStringLiteral("L");
    else if (g == QLatin1String("equal")) {
        if (m_goatedHiloRank == QLatin1String("K"))
            choice = QStringLiteral("H");
        else if (m_goatedHiloRank == QLatin1String("A"))
            choice = QStringLiteral("L");
        else {
            failNow(tr("Goated hilo: Equal is only possible on an Ace or a King."));
            return;
        }
    }
    if (choice.isEmpty()) {
        failNow(tr("Goated hilo: unknown guess \"%1\".").arg(g));
        return;
    }
    auto *goated = qobject_cast<GoatedTransport *>(m_transport);
    if (!goated) {
        failNow(tr("Goated hilo: no Goated connection."));
        return;
    }
    goated->patch(QStringLiteral("casino/hilo/select"), QJsonObject{{QStringLiteral("choice"), choice}},
                  [this, params, requestedAmount](const TransportReply &reply) {
        handleGoatedHiloReply(reply, params, requestedAmount, false, false);
    });
}

void StakeClient::fetchGoatedHilo(const QJsonObject &params, double requestedAmount)
{
    auto *goated = qobject_cast<GoatedTransport *>(m_transport);
    if (!goated) {
        const LiveBetResult r = failed(tr("Hilo: no open round."), QStringLiteral("existingGame"), true, false);
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }
    // Same address pattern as blackjack's open hand.
    goated->get(QStringLiteral("casino/hilo/active"), [this, params, requestedAmount](const TransportReply &reply) {
        handleGoatedHiloReply(reply, params, requestedAmount, false, true);
    });
}

void StakeClient::handleGoatedHiloReply(const TransportReply &reply, const QJsonObject &params,
                                        double requestedAmount, bool firstRequest, bool activeQuery)
{
    const auto nothingOpen = [this] {
        LiveBetResult r = failed(tr("Hilo: no open round."), QStringLiteral("existingGame"), true, false);
        r.retryDelayMs = 500;
        emit diceFinished(r);
    };
    if (!reply.ok) {
        if (activeQuery) {
            nothingOpen();
            return;
        }
        const bool isTimeout = (reply.status == 0);
        const bool unknown = (reply.status >= 500);
        if (reply.status == 0)
            emit sessionLost(reply.error);
        emit diceFinished(failed(reply.error, {}, isTimeout, unknown));
        return;
    }
    const QJsonObject &json = reply.json;
    const QString errText = goatedErrorText(json);
    const bool successFalse = json.contains(QStringLiteral("success"))
                              && !json.value(QStringLiteral("success")).toBool();
    if (successFalse || reply.status >= 400) {
        if (activeQuery) {
            nothingOpen();
            return;
        }
        const QString msg = errText.isEmpty()
            ? tr("Goated hilo request rejected (HTTP %1).").arg(reply.status) : errText;
        if (firstRequest && (containsCI(msg, "active") || containsCI(msg, "existing")
                             || containsCI(msg, "already") || containsCI(msg, "progress")
                             || containsCI(msg, "unfinished") || containsCI(msg, "ongoing"))) {
            fetchGoatedHilo(params, requestedAmount);
            return;
        }
        LiveBetResult r = failed(msg, {}, isRetryable({}, msg), false);
        r.retryDelayMs = fastRetryDelayMs({}, msg);
        emit diceFinished(r);
        return;
    }
    // {"success": true, "data": {...}} (cashout answers with the bet itself).
    const QJsonValue dataValue = json.value(QStringLiteral("data"));
    const QJsonObject raw = dataValue.isObject() ? dataValue.toObject()
                          : (json.contains(QStringLiteral("card_sequence")) ? json : QJsonObject());
    if (raw.isEmpty() || !raw.contains(QStringLiteral("card_sequence"))) {
        if (activeQuery) {
            nothingOpen();
            return;
        }
        const QString text = QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)).left(400);
        emit diceFinished(failed(tr("Unexpected Goated hilo response: %1").arg(text), {}, false, true));
        return;
    }
    const QJsonObject bet = goatedHiloAsStake(raw, &m_goatedHiloRank);
    if (activeQuery && !bet.value(QStringLiteral("active")).toBool()) {
        nothingOpen();
        return;
    }
    LiveBetResult r = gameResult(Game::Hilo, bet, params, requestedAmount);
    if (activeQuery) {
        r.active = true;
        r.win = false;
    }
    if (!r.active) {
        const QJsonObject balances = raw.value(QStringLiteral("updated_balance")).toObject();
        const QString cur = m_currency.toUpper();
        if (balances.contains(cur))
            r.balanceAfter = numberOf(balances.value(cur));
    }
    emit diceFinished(r);
}

// ---------------------------------------------------------------- Wolfbet keno
// POST https://wolfbet.com/api/v2/keno/manual/play
//   {"currency":"doge","game":"keno","amount":"0.0001","multiplier":"1000","risk":"low",
//    "selected":[25,36,...] (1-40),"auto":1}
// "multiplier" is the top payout of the risk's paytable for that many numbers (10 numbers, low
// risk: 1000). Answer: {"bet":{"hash","nonce","currency","amount","profit" (net),"multiplier"
// (the payout multiplier),"risk","selected","result_value" (the 10 drawn numbers, 1-40),
// "state":"win"|"loss"},"userBalance":{"amount","currency"}}.
namespace
{
// Highest payout of each paytable (Stake's tables, which Wolfbet uses), by risk and by the
// count of numbers picked (index 0 = 1 number .. 9 = 10 numbers).
double wolfbetKenoTopMultiplier(const QString &risk, int picks)
{
    static const double low[10] = {1.85, 3.8, 26, 90, 300, 700, 700, 800, 1000, 1000};
    static const double medium[10] = {2.75, 5.1, 50, 100, 390, 710, 800, 900, 1000, 1000};
    static const double high[10] = {3.96, 17.1, 81.5, 259, 450, 710, 800, 900, 1000, 1000};
    if (picks < 1 || picks > 10)
        return -1.0;
    if (risk == QLatin1String("low"))
        return low[picks - 1];
    if (risk == QLatin1String("medium"))
        return medium[picks - 1];
    if (risk == QLatin1String("high"))
        return high[picks - 1];
    return -1.0;
}
} // namespace

void StakeClient::placeWolfbetKeno(const GameBetRequest &request)
{
    const auto failNow = [this](const QString &msg) {
        const LiveBetResult r = failed(msg);
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
    };
    // Numbers and risk as Stake's request would have them (0-based) -> Wolfbet's 1-40.
    QString endpoint;
    QJsonObject stakeBody;
    QString err;
    if (!StakeGames::buildBetRequest(Game::Keno, request.amount, m_currency, request.params,
                                     &endpoint, &stakeBody, &err)) {
        failNow(err);
        return;
    }
    const QString risk = stakeBody.value(QStringLiteral("risk")).toString().toLower();
    QJsonArray selected;
    for (const QJsonValue &n : stakeBody.value(QStringLiteral("numbers")).toArray())
        selected.append(n.toInt() + 1);
    const double top = wolfbetKenoTopMultiplier(risk, int(selected.size()));
    if (top < 0.0) {
        failNow(tr("Wolfbet keno: risk must be low, medium or high (not \"%1\").").arg(risk));
        return;
    }
    // Plain numbers like the site sends them: "1000", "81.5", "3.96".
    QString topText = QString::number(top, 'f', 2);
    while (topText.endsWith(QLatin1Char('0')))
        topText.chop(1);
    if (topText.endsWith(QLatin1Char('.')))
        topText.chop(1);

    QJsonObject body;
    body.insert(QStringLiteral("currency"), m_currency.toLower());
    body.insert(QStringLiteral("game"), QStringLiteral("keno"));
    body.insert(QStringLiteral("amount"), QString::number(request.amount, 'f', 8));
    body.insert(QStringLiteral("multiplier"), topText);
    body.insert(QStringLiteral("risk"), risk);
    body.insert(QStringLiteral("selected"), selected);
    body.insert(QStringLiteral("auto"), 1);

    const QJsonObject params = request.params;
    const double requestedAmount = request.amount;
    m_transport->post(QStringLiteral("api/v2/keno/manual/play"), body,
                      [this, params, requestedAmount](const TransportReply &reply) {
        if (!reply.ok) {
            const bool isTimeout = (reply.status == 0);
            const bool unknown = (reply.status >= 500);
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit diceFinished(failed(reply.error, {}, isTimeout, unknown));
            return;
        }
        const QJsonObject &json = reply.json;
        const QJsonObject raw = json.value(QStringLiteral("bet")).toObject();
        if (raw.isEmpty()) {
            // No bet in the answer: an API error (or rate limit notice) - the bet was not placed.
            QString errText = goatedErrorText(json);
            if (errText.isEmpty())
                errText = tr("Wolfbet keno bet rejected (HTTP %1).").arg(reply.status);
            if (reply.status >= 400 || isRetryable({}, errText)) {
                LiveBetResult r = failed(errText, {}, isRetryable({}, errText), false);
                r.retryDelayMs = fastRetryDelayMs({}, errText);
                emit diceFinished(r);
                return;
            }
            const QString text = QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)).left(400);
            emit diceFinished(failed(tr("Unexpected Wolfbet keno response: %1").arg(text), {}, false, true));
            return;
        }
        if (!raw.value(QStringLiteral("result_value")).isArray()) {
            const QString text = QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)).left(400);
            emit diceFinished(failed(tr("Unexpected Wolfbet keno response (no drawn numbers): %1").arg(text),
                                     {}, false, true));
            return;
        }
        // Stake's keno shape (0-based numbers): state {risk, selectedNumbers, drawnNumbers}.
        const auto zeroBased = [](const QJsonValue &v) {
            QJsonArray out;
            for (const QJsonValue &n : v.toArray())
                out.append(int(numberOf(n)) - 1);
            return out;
        };
        const double amount = raw.contains(QStringLiteral("amount")) ? numberOf(raw.value(QStringLiteral("amount")))
                                                                     : requestedAmount;
        const double profit = numberOf(raw.value(QStringLiteral("profit")));
        double mult = numberOf(raw.value(QStringLiteral("multiplier")));
        if (!(mult > 0.0) && amount > 0.0 && raw.contains(QStringLiteral("profit")))
            mult = std::max(0.0, (amount + profit) / amount);
        QJsonObject state;
        state.insert(QStringLiteral("risk"), raw.value(QStringLiteral("risk")).toString().toLower());
        state.insert(QStringLiteral("selectedNumbers"), zeroBased(raw.value(QStringLiteral("selected"))));
        state.insert(QStringLiteral("drawnNumbers"), zeroBased(raw.value(QStringLiteral("result_value"))));
        QJsonObject bet;
        bet.insert(QStringLiteral("id"), raw.value(QStringLiteral("hash")).toString());
        bet.insert(QStringLiteral("amount"), amount);
        bet.insert(QStringLiteral("payout"), amount * mult);
        bet.insert(QStringLiteral("payoutMultiplier"), mult);
        bet.insert(QStringLiteral("active"), false);
        bet.insert(QStringLiteral("currency"), raw.value(QStringLiteral("currency")).toString().toLower());
        bet.insert(QStringLiteral("game"), QStringLiteral("keno"));
        bet.insert(QStringLiteral("state"), state);
        if (raw.contains(QStringLiteral("nonce")))
            bet.insert(QStringLiteral("nonce"), raw.value(QStringLiteral("nonce")));
        LiveBetResult r = gameResult(Game::Keno, bet, params, requestedAmount);
        const QJsonObject balance = json.value(QStringLiteral("userBalance")).toObject();
        if (balance.value(QStringLiteral("currency")).toString().compare(m_currency, Qt::CaseInsensitive) == 0
            && balance.contains(QStringLiteral("amount")))
            r.balanceAfter = numberOf(balance.value(QStringLiteral("amount")));
        emit diceFinished(r);
    });
}

// ---------------------------------------------------------------- Wolfbet plinko
// POST https://wolfbet.com/api/v2/plinko/manual/play
//   {"currency":"doge","game":"plinko","amount":"0.001","risk":"high","rows":8,"auto":1}
// Answer: {"bet":{"hash","nonce","currency","amount","profit" (net),"multiplier" (payout
// multiplier),"risk","rows","result_value" (the bucket, 0 = leftmost),"path","state"},
// "userBalance":{"amount","currency"}}.
void StakeClient::placeWolfbetPlinko(const GameBetRequest &request)
{
    const auto failNow = [this](const QString &msg) {
        const LiveBetResult r = failed(msg);
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
    };
    QString endpoint;
    QJsonObject stakeBody;
    QString err;
    if (!StakeGames::buildBetRequest(Game::Plinko, request.amount, m_currency, request.params,
                                     &endpoint, &stakeBody, &err)) {
        failNow(err);
        return;
    }
    const QJsonObject vars = stakeBody.value(QStringLiteral("variables")).toObject();
    const QString risk = vars.value(QStringLiteral("risk")).toString().toLower();
    if (risk != QLatin1String("low") && risk != QLatin1String("medium") && risk != QLatin1String("high")) {
        failNow(tr("Wolfbet plinko: risk must be low, medium or high (not \"%1\").").arg(risk));
        return;
    }
    QJsonObject body;
    body.insert(QStringLiteral("currency"), m_currency.toLower());
    body.insert(QStringLiteral("game"), QStringLiteral("plinko"));
    body.insert(QStringLiteral("amount"), QString::number(request.amount, 'f', 8));
    body.insert(QStringLiteral("risk"), risk);
    body.insert(QStringLiteral("rows"), vars.value(QStringLiteral("rows")).toInt(8));
    body.insert(QStringLiteral("auto"), 1);

    const QJsonObject params = request.params;
    const double requestedAmount = request.amount;
    m_transport->post(QStringLiteral("api/v2/plinko/manual/play"), body,
                      [this, params, requestedAmount](const TransportReply &reply) {
        if (!reply.ok) {
            const bool isTimeout = (reply.status == 0);
            const bool unknown = (reply.status >= 500);
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit diceFinished(failed(reply.error, {}, isTimeout, unknown));
            return;
        }
        const QJsonObject &json = reply.json;
        const QJsonObject raw = json.value(QStringLiteral("bet")).toObject();
        if (raw.isEmpty()) {
            // No bet in the answer: an API error (or rate limit notice) - the bet was not placed.
            QString errText = goatedErrorText(json);
            if (errText.isEmpty())
                errText = tr("Wolfbet plinko bet rejected (HTTP %1).").arg(reply.status);
            if (reply.status >= 400 || isRetryable({}, errText)) {
                LiveBetResult r = failed(errText, {}, isRetryable({}, errText), false);
                r.retryDelayMs = fastRetryDelayMs({}, errText);
                emit diceFinished(r);
                return;
            }
            const QString text = QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)).left(400);
            emit diceFinished(failed(tr("Unexpected Wolfbet plinko response: %1").arg(text), {}, false, true));
            return;
        }
        if (!raw.contains(QStringLiteral("multiplier")) && !raw.contains(QStringLiteral("profit"))) {
            const QString text = QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)).left(400);
            emit diceFinished(failed(tr("Unexpected Wolfbet plinko response (no result): %1").arg(text),
                                     {}, false, true));
            return;
        }
        const double amount = raw.contains(QStringLiteral("amount")) ? numberOf(raw.value(QStringLiteral("amount")))
                                                                     : requestedAmount;
        double mult = numberOf(raw.value(QStringLiteral("multiplier")));
        if (!raw.contains(QStringLiteral("multiplier")) && amount > 0.0)
            mult = std::max(0.0, (amount + numberOf(raw.value(QStringLiteral("profit")))) / amount);
        // Stake's plinko shape: state {risk, rows, point (the bucket the ball fell into)}.
        QJsonObject state;
        state.insert(QStringLiteral("risk"), raw.value(QStringLiteral("risk")).toString().toLower());
        state.insert(QStringLiteral("rows"), int(numberOf(raw.value(QStringLiteral("rows")))));
        if (raw.contains(QStringLiteral("result_value")))
            state.insert(QStringLiteral("point"), int(numberOf(raw.value(QStringLiteral("result_value")))));
        QJsonObject bet;
        bet.insert(QStringLiteral("id"), raw.value(QStringLiteral("hash")).toString());
        bet.insert(QStringLiteral("amount"), amount);
        bet.insert(QStringLiteral("payout"), amount * mult);
        bet.insert(QStringLiteral("payoutMultiplier"), mult);
        bet.insert(QStringLiteral("active"), false);
        bet.insert(QStringLiteral("currency"), raw.value(QStringLiteral("currency")).toString().toLower());
        bet.insert(QStringLiteral("game"), QStringLiteral("plinko"));
        bet.insert(QStringLiteral("state"), state);
        if (raw.contains(QStringLiteral("nonce")))
            bet.insert(QStringLiteral("nonce"), raw.value(QStringLiteral("nonce")));
        LiveBetResult r = gameResult(Game::Plinko, bet, params, requestedAmount);
        const QJsonObject balance = json.value(QStringLiteral("userBalance")).toObject();
        if (balance.value(QStringLiteral("currency")).toString().compare(m_currency, Qt::CaseInsensitive) == 0
            && balance.contains(QStringLiteral("amount")))
            r.balanceAfter = numberOf(balance.value(QStringLiteral("amount")));
        emit diceFinished(r);
    });
}

// ---------------------------------------------------------------- Wolfbet hilo
// POST https://wolfbet.com/api/v1/user/hilo/play
//   start: {"rule":"start","bet_value":<start card 0-51>,"currency":"doge","amount":"1",
//           "win_chance":0,"multiplier":0,"sub_nonce":0}
//   guess: {"rule":"high"|"low","bet_value":<current card>,"currency","amount" (the round's stake),
//           "win_chance":84.62,"multiplier":1.1699,"sub_nonce":<moves made so far, skips included>}
//   skip:  {"rule":"skip", ..., "win_chance":0,"multiplier":0,"sub_nonce":<moves so far>}
// GET https://wolfbet.com/api/v1/user/hilo/finish: cash out.
// GET https://wolfbet.com/api/v1/user/hilo/status: the open round (same "bet" object).
// Every answer: {"bet": {... see wolfbetHiloAsStake()}}, the cash out also "userBalance".
void StakeClient::placeWolfbetHilo(const GameBetRequest &request)
{
    const auto failNow = [this](const QString &msg) {
        const LiveBetResult r = failed(msg);
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
    };
    QString endpoint;
    QJsonObject stakeBody;
    QString err;
    if (!StakeGames::buildBetRequest(Game::Hilo, request.amount, m_currency, request.params,
                                     &endpoint, &stakeBody, &err)) {
        failNow(err);
        return;
    }
    const int start = wolfbetCardNumber(stakeBody.value(QStringLiteral("startCard")).toObject());
    if (start < 0) {
        failNow(tr("Wolfbet hilo: invalid start card."));
        return;
    }
    m_wolfbetHiloAmount = QString::number(request.amount, 'f', 8);
    m_wolfbetHiloCard = start;
    m_wolfbetHiloSteps = 0;
    m_wolfbetHiloNonce = -1;

    QJsonObject body;
    body.insert(QStringLiteral("rule"), QStringLiteral("start"));
    body.insert(QStringLiteral("bet_value"), start);
    body.insert(QStringLiteral("currency"), m_currency.toLower());
    body.insert(QStringLiteral("amount"), m_wolfbetHiloAmount);
    body.insert(QStringLiteral("win_chance"), 0);
    body.insert(QStringLiteral("multiplier"), 0);
    body.insert(QStringLiteral("sub_nonce"), 0);
    const QJsonObject params = request.params;
    const double amount = request.amount;
    m_transport->post(QStringLiteral("api/v1/user/hilo/play"), body,
                      [this, params, amount](const TransportReply &reply) {
        handleWolfbetHiloReply(reply, params, amount, true);
    });
}

void StakeClient::sendWolfbetHilo(const QString &endpoint, const QJsonObject &stakeBody, const QJsonObject &params,
                                  double requestedAmount)
{
    const auto failNow = [this](const QString &msg) {
        const LiveBetResult r = failed(msg);
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
    };
    auto *wolfbet = qobject_cast<WolfbetTransport *>(m_transport);
    if (!wolfbet) {
        failNow(tr("Wolfbet hilo: no Wolfbet connection."));
        return;
    }
    if (endpoint.contains(QLatin1String("cashout"))) {
        wolfbet->get(QStringLiteral("api/v1/user/hilo/finish"), [this, params, requestedAmount](const TransportReply &reply) {
            handleWolfbetHiloReply(reply, params, requestedAmount, false);
        });
        return;
    }
    if (m_wolfbetHiloCard < 0 || m_wolfbetHiloAmount.isEmpty()) {
        failNow(tr("Wolfbet hilo: no open round."));
        return;
    }
    // Stake's guesses -> Wolfbet's rules ("high" = higher or same, "low" = lower or same;
    // "equal" is "high" on a king and "low" on an ace).
    const int rank = m_wolfbetHiloCard % 13;
    const QString g = stakeBody.value(QStringLiteral("guess")).toString();
    QString rule;
    if (g == QLatin1String("higher") || g == QLatin1String("higherEqual"))
        rule = QStringLiteral("high");
    else if (g == QLatin1String("lower") || g == QLatin1String("lowerEqual"))
        rule = QStringLiteral("low");
    else if (g == QLatin1String("equal")) {
        if (rank == 12)
            rule = QStringLiteral("high");
        else if (rank == 0)
            rule = QStringLiteral("low");
        else {
            failNow(tr("Wolfbet hilo: Equal is only possible on an Ace or a King."));
            return;
        }
    } else if (g == QLatin1String("skip")) {
        rule = QStringLiteral("skip"); // win_chance and multiplier 0, like the start
    }
    if (rule.isEmpty()) {
        failNow(tr("Wolfbet hilo: unknown guess \"%1\".").arg(g));
        return;
    }
    // The site sends the win chance (2 decimals) and the multiplier (99 / chance, 4 decimals)
    // with every guess and rejects values that do not match its own.
    const int winning = wolfbetHiloWinningRanks(rule, rank);
    const double chance = winning > 0 ? std::round(winning * 10000.0 / 13.0) / 100.0 : 0.0;
    const double mult = chance > 0.0 ? std::round(99.0 / chance * 10000.0) / 10000.0 : 0.0;

    QJsonObject body;
    body.insert(QStringLiteral("rule"), rule);
    body.insert(QStringLiteral("bet_value"), m_wolfbetHiloCard);
    body.insert(QStringLiteral("currency"), m_currency.toLower());
    body.insert(QStringLiteral("amount"), m_wolfbetHiloAmount);
    body.insert(QStringLiteral("win_chance"), chance);
    body.insert(QStringLiteral("multiplier"), mult);
    body.insert(QStringLiteral("sub_nonce"), m_wolfbetHiloSteps);
    m_transport->post(QStringLiteral("api/v1/user/hilo/play"), body,
                      [this, params, requestedAmount](const TransportReply &reply) {
        handleWolfbetHiloReply(reply, params, requestedAmount, false);
    });
}

void StakeClient::fetchWolfbetHilo(const QJsonObject &params, double requestedAmount, const QString &startError)
{
    auto *wolfbet = qobject_cast<WolfbetTransport *>(m_transport);
    if (!wolfbet) {
        const LiveBetResult r = failed(tr("Hilo: no open round."), QStringLiteral("existingGame"), true, false);
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }
    wolfbet->get(QStringLiteral("api/v1/user/hilo/status"),
                 [this, params, requestedAmount, startError](const TransportReply &reply) {
        handleWolfbetHiloReply(reply, params, requestedAmount, false, true, startError);
    });
}

void StakeClient::handleWolfbetHiloReply(const TransportReply &reply, const QJsonObject &params,
                                         double requestedAmount, bool firstRequest, bool activeQuery,
                                         const QString &startError)
{
    // Status request with nothing open (same answer as Goated's / Shuffle's).
    const auto nothingOpen = [this, startError, &reply] {
        m_wolfbetHiloCard = -1;
        m_wolfbetHiloSteps = 0;
        m_wolfbetHiloAmount.clear();
        if (!startError.isEmpty()) {
            // The new round was refused, but the status request shows no open round: report the
            // site's own message with the status answer (to see why it was not taken as open),
            // and stop there (no endless start / status loop).
            const QString status = reply.ok
                ? QString::fromUtf8(QJsonDocument(reply.json).toJson(QJsonDocument::Compact)).left(600)
                : tr("HTTP %1 %2").arg(reply.status).arg(reply.error);
            LiveBetResult r = failed(tr("Wolfbet hilo: %1 (hilo/status answered: %2)").arg(startError, status),
                                     {}, isRetryable({}, startError), false);
            r.retryDelayMs = fastRetryDelayMs({}, startError);
            emit diceFinished(r);
            return;
        }
        LiveBetResult r = failed(tr("Hilo: no open round."), QStringLiteral("existingGame"), true, false);
        r.retryDelayMs = 500;
        emit diceFinished(r);
    };
    if (!reply.ok && activeQuery) {
        nothingOpen();
        return;
    }
    if (!reply.ok) {
        const bool isTimeout = (reply.status == 0);
        const bool unknown = (reply.status >= 500);
        if (reply.status == 0)
            emit sessionLost(reply.error);
        emit diceFinished(failed(reply.error, {}, isTimeout, unknown));
        return;
    }
    const QJsonObject &json = reply.json;
    QJsonObject raw = json.value(QStringLiteral("bet")).toObject();
    if (activeQuery && !wolfbetHiloOpen(raw)) {
        nothingOpen();
        return;
    }
    if (raw.isEmpty()) {
        // No bet in the answer: an API error (or rate limit notice) - nothing was played.
        QString errText = goatedErrorText(json);
        if (errText.isEmpty())
            errText = tr("Wolfbet hilo request rejected (HTTP %1).").arg(reply.status);
        if (firstRequest && (containsCI(errText, "active") || containsCI(errText, "unfinished")
                             || containsCI(errText, "already") || containsCI(errText, "progress")
                             || containsCI(errText, "existing") || containsCI(errText, "finish"))) {
            // A round is still open (left over from the website or a lost answer): load it so
            // it is finished first.
            fetchWolfbetHilo(params, requestedAmount, errText);
            return;
        }
        if (reply.status >= 400 || isRetryable({}, errText)) {
            LiveBetResult r = failed(errText, {}, isRetryable({}, errText), false);
            r.retryDelayMs = fastRetryDelayMs({}, errText);
            emit diceFinished(r);
            return;
        }
        const QString text = QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)).left(400);
        emit diceFinished(failed(tr("Unexpected Wolfbet hilo response: %1").arg(text), {}, false, true));
        return;
    }
    // The round's nonce comes with the start (and the status) answer; the cash out answer has
    // none, so the remembered one is used for the bet list row.
    if (raw.contains(QStringLiteral("nonce")) && !raw.value(QStringLiteral("nonce")).isNull())
        m_wolfbetHiloNonce = qint64(numberOf(raw.value(QStringLiteral("nonce"))));
    else if (m_wolfbetHiloNonce >= 0)
        raw.insert(QStringLiteral("nonce"), double(m_wolfbetHiloNonce));
    const QJsonObject bet = wolfbetHiloAsStake(raw, &m_wolfbetHiloCard, &m_wolfbetHiloSteps);
    // Every guess repeats the round's stake exactly as the site reports it ("0.01100000"), also
    // for a round loaded with the status request.
    const QJsonValue stake = raw.value(QStringLiteral("amount"));
    if (stake.isString() && !stake.toString().isEmpty())
        m_wolfbetHiloAmount = stake.toString();
    else if (stake.isDouble())
        m_wolfbetHiloAmount = QString::number(stake.toDouble(), 'f', 8);
    LiveBetResult r = gameResult(Game::Hilo, bet, params, requestedAmount);
    if (activeQuery) {
        r.active = true;
        r.win = false;
    }
    if (!r.active) {
        m_wolfbetHiloCard = -1;
        m_wolfbetHiloSteps = 0;
        m_wolfbetHiloAmount.clear();
        m_wolfbetHiloNonce = -1;
        const QJsonObject balance = json.value(QStringLiteral("userBalance")).toObject();
        if (balance.value(QStringLiteral("currency")).toString().compare(m_currency, Qt::CaseInsensitive) == 0
            && balance.contains(QStringLiteral("amount")))
            r.balanceAfter = numberOf(balance.value(QStringLiteral("amount")));
    }
    emit diceFinished(r);
}

void StakeClient::placeGoatedDice(const DiceBetRequest &request)
{
    const double target = std::round(request.target * 100.0) / 100.0; // 2 decimals
    if (!(target > 0.0) || !(target < 100.0)) {
        const LiveBetResult r = failed(tr("Goated: invalid target %1.").arg(target));
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }

    QJsonObject body;
    body.insert(QStringLiteral("user_value"), target);
    body.insert(QStringLiteral("roll_type"),
                request.over ? QStringLiteral("OVER") : QStringLiteral("UNDER"));
    body.insert(QStringLiteral("amount"), std::round(request.amount * 1e8) / 1e8);
    body.insert(QStringLiteral("currency"), m_currency.toUpper());
    body.insert(QStringLiteral("is_fiat"), false);

    const double requestedAmount = request.amount;
    const bool over = request.over;
    m_transport->post(QStringLiteral("casino/dice/v2"), body,
                      [this, requestedAmount, target, over](const TransportReply &reply) {
        if (!reply.ok) {
            const bool isTimeout = (reply.status == 0);
            const bool unknown = (reply.status >= 500);
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit diceFinished(failed(reply.error, {}, isTimeout, unknown));
            return;
        }

        const QJsonObject &json = reply.json;
        const QString errText = goatedErrorText(json);
        const bool successFalse = json.contains(QStringLiteral("success"))
                                  && !json.value(QStringLiteral("success")).toBool();
        if (successFalse || reply.status >= 400) {
            const QString msg = errText.isEmpty()
                ? tr("Goated bet rejected (HTTP %1).").arg(reply.status) : errText;
            LiveBetResult r = failed(msg, {}, isRetryable({}, msg), false);
            r.retryDelayMs = fastRetryDelayMs({}, msg);
            emit diceFinished(r);
            return;
        }

        // Rate limit notice injected by the transport (HTTP 429 with a non-JSON body).
        if (!errText.isEmpty() && isRetryable({}, errText)) {
            LiveBetResult r = failed(errText, {}, true, false);
            r.retryDelayMs = fastRetryDelayMs({}, errText);
            emit diceFinished(r);
            return;
        }

        double roll = 0.0;
        const bool haveRoll = goatedNumber(json,
            {QStringLiteral("roll"), QStringLiteral("roll_result"), QStringLiteral("roll_value"),
             QStringLiteral("rolled"), QStringLiteral("result_value"), QStringLiteral("result"),
             QStringLiteral("number"), QStringLiteral("outcome")}, &roll);
        if (!haveRoll) {
            const QString raw = QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)).left(400);
            // The bet may or may not have been placed: do not retry blindly.
            emit diceFinished(failed(errText.isEmpty()
                                         ? tr("Unexpected Goated bet response (no roll found): %1").arg(raw)
                                         : errText,
                                     {}, false, true));
            return;
        }

        double amount = requestedAmount;
        goatedNumber(json, {QStringLiteral("amount"), QStringLiteral("bet_amount"),
                            QStringLiteral("wager")}, &amount);
        double payout = 0.0;
        const bool havePayout = goatedNumber(json,
            {QStringLiteral("payout"), QStringLiteral("win_amount"), QStringLiteral("payout_amount"),
             QStringLiteral("winnings"), QStringLiteral("profit_amount")}, &payout);
        double multiplier = 0.0;
        const bool haveMult = goatedNumber(json,
            {QStringLiteral("multiplier"), QStringLiteral("payout_multiplier"),
             QStringLiteral("payoutMultiplier")}, &multiplier);

        bool win = false;
        if (!goatedBool(json, {QStringLiteral("win"), QStringLiteral("won"), QStringLiteral("is_win"),
                               QStringLiteral("isWin"), QStringLiteral("is_winner"),
                               QStringLiteral("result_type"), QStringLiteral("outcome")}, &win)) {
            if (amount > 0.0 && havePayout)
                win = BotConstants::isWinMultiplier(payout / amount);
            else
                win = over ? roll > target : roll < target;
        }

        // A server "win" flag on a partial payout still is a loss: only >= 1x counts.
        if (win && amount > 0.0 && havePayout)
            win = BotConstants::isWinMultiplier(payout / amount);

        LiveBetResult r;
        r.ok = true;
        r.amount = amount;
        r.roll = roll;
        r.win = win;
        r.payout = havePayout ? payout : 0.0;
        if (win) {
            const double chance = over ? 100.0 - target : target;
            r.multiplier = haveMult ? multiplier
                           : (amount > 0.0 && havePayout && payout > 0.0)
                                 ? payout / amount
                                 : (chance > 0.0 ? 99.0 / chance : 0.0); // Goated: 1 % edge
            if (!havePayout)
                r.payout = amount * r.multiplier;
        }

        QJsonValue idv;
        if (findGoatedValue(json, {QStringLiteral("id"), QStringLiteral("bet_id"), QStringLiteral("betId"),
                                   QStringLiteral("_id")}, &idv))
            r.betId = idv.isString() ? idv.toString() : QString::number(qint64(idv.toDouble()));
        double nonce = -1;
        if (goatedNumber(json, {QStringLiteral("nonce")}, &nonce))
            r.nonce = qint64(nonce);
        emit diceFinished(r);
    });
}

// Wolfbet: GET https://wolfbet.com/api/v1/game/seed/refresh
// Answer: {"server_seed_hashed":"<hash of the new server seed>"}. The request has no client seed
// parameter, so only the server seed is rotated.
void StakeClient::rotateWolfbetSeed()
{
    auto *wolfbet = qobject_cast<WolfbetTransport *>(m_transport);
    QString why;
    if (!wolfbet || !wolfbet->isReady(&why)) {
        const QString msg = wolfbet ? why : tr("No transport configured.");
        QTimer::singleShot(0, this, [this, msg] { emit seedRotateFailed(msg); });
        return;
    }

    wolfbet->get(QStringLiteral("api/v1/game/seed/refresh"), [this](const TransportReply &reply) {
        if (!reply.ok) {
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit seedRotateFailed(reply.error);
            return;
        }
        const QString hash = reply.json.value(QStringLiteral("server_seed_hashed")).toString();
        if (hash.isEmpty()) {
            QString msg = goatedErrorText(reply.json);
            if (msg.isEmpty())
                msg = tr("Wolfbet seed rotation rejected (HTTP %1).").arg(reply.status);
            emit seedRotateFailed(msg);
            return;
        }
        emit seedRotated(QString(), -1, hash, QString()); // client seed stays as it is
    });
}

// Wolfbet dice: POST https://wolfbet.com/api/v1/dice/manual/play
//   {"currency":"doge","game":"dice","amount":"0.00000001","multiplier":"1.98","rule":"under",
//    "bet_value":"50","auto":1}
// Answer: {"bet":{"hash","nonce","currency","amount","profit" (net, negative on a loss),"multiplier",
//   "bet_value","rule","result_value" (the roll),"state":"win"|"loss",...},"userBalance":{...}}
// Errors are read defensively (the error layout was not captured).
void StakeClient::placeWolfbetDice(const DiceBetRequest &request)
{
    const double target = std::round(request.target * 100.0) / 100.0;
    const bool over = request.over;
    // Win chance in percent, as the script / panel means it (any value, 2 decimals).
    const double chance = std::round((over ? 100.0 - target : target) * 100.0) / 100.0;
    if (!(chance > 0.0) || chance >= 100.0) {
        const LiveBetResult r = failed(tr("Wolfbet: invalid win chance %1.").arg(chance));
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }

    // Wolfbet rolls 0.00 - 99.99: "under X" wins with X %, "over X" with 99.99 - X %. So the
    // value sent for "over" is 99.99 - chance (not 100 - chance), or the site answers
    // "Incorrect win chance given".
    const double betValue = over ? std::round((99.99 - chance) * 100.0) / 100.0 : chance;
    m_lastDiceTarget = betValue;

    // Fair multiplier at a 1 % house edge (50 % -> 1.98), cut (never rounded up) to 4 decimals,
    // shown with at least 2 decimals like the site does.
    const double mult = std::floor(99.0 / chance * 10000.0 + 1e-9) / 10000.0;
    QString multText = QString::number(mult, 'f', 4);
    while (multText.endsWith(QLatin1Char('0')) && multText.size() - multText.indexOf(QLatin1Char('.')) > 3)
        multText.chop(1);
    // bet_value: no trailing zeros ("50", "50.49").
    QString targetText = QString::number(betValue, 'f', 2);
    while (targetText.endsWith(QLatin1Char('0')))
        targetText.chop(1);
    if (targetText.endsWith(QLatin1Char('.')))
        targetText.chop(1);

    QJsonObject body;
    body.insert(QStringLiteral("currency"), m_currency.toLower());
    body.insert(QStringLiteral("game"), QStringLiteral("dice"));
    body.insert(QStringLiteral("amount"), QString::number(request.amount, 'f', 8));
    body.insert(QStringLiteral("multiplier"), multText);
    body.insert(QStringLiteral("rule"), over ? QStringLiteral("over") : QStringLiteral("under"));
    body.insert(QStringLiteral("bet_value"), targetText);
    body.insert(QStringLiteral("auto"), 1);

    const double requestedAmount = request.amount;
    m_transport->post(QStringLiteral("api/v1/dice/manual/play"), body,
                      [this, requestedAmount, betValue, over, mult](const TransportReply &reply) {
        if (!reply.ok) {
            const bool isTimeout = (reply.status == 0);
            const bool unknown = (reply.status >= 500);
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit diceFinished(failed(reply.error, {}, isTimeout, unknown));
            return;
        }

        const QJsonObject &json = reply.json;
        const QJsonObject bet = json.value(QStringLiteral("bet")).toObject();
        if (bet.isEmpty()) {
            // No bet in the answer: an API error (or rate limit notice) - the bet was not placed.
            QString errText = goatedErrorText(json);
            if (errText.isEmpty())
                errText = tr("Wolfbet bet rejected (HTTP %1).").arg(reply.status);
            if (reply.status >= 400 || isRetryable({}, errText)) {
                LiveBetResult r = failed(errText, {}, isRetryable({}, errText), false);
                r.retryDelayMs = fastRetryDelayMs({}, errText);
                emit diceFinished(r);
                return;
            }
            const QString raw = QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)).left(400);
            // The bet may or may not have been placed: do not retry blindly.
            emit diceFinished(failed(tr("Unexpected Wolfbet bet response (no bet found): %1").arg(raw),
                                     {}, false, true));
            return;
        }

        if (!bet.contains(QStringLiteral("result_value"))) {
            const QString raw = QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)).left(400);
            emit diceFinished(failed(tr("Unexpected Wolfbet bet response (no roll found): %1").arg(raw),
                                     {}, false, true));
            return;
        }

        LiveBetResult r;
        r.ok = true;
        r.amount = bet.contains(QStringLiteral("amount")) ? numberOf(bet.value(QStringLiteral("amount")))
                                                          : requestedAmount;
        r.roll = numberOf(bet.value(QStringLiteral("result_value")));

        const QString state = bet.value(QStringLiteral("state")).toString().trimmed().toLower();
        const double profit = numberOf(bet.value(QStringLiteral("profit")));
        // Win only when the payout multiplier is >= 1x: with a stake, net profit >= 0;
        // the server's "state" is only used for a 0 bet (where profit is always 0).
        if (r.amount > 0.0 && bet.contains(QStringLiteral("profit")))
            r.win = BotConstants::isWinMultiplier((r.amount + profit) / r.amount);
        else if (!state.isEmpty())
            r.win = (state == QLatin1String("win"));
        else
            r.win = over ? r.roll > betValue : r.roll < betValue;

        if (r.win) {
            const double betMult = numberOf(bet.value(QStringLiteral("multiplier")));
            r.multiplier = betMult > 0.0 ? betMult : mult;
            // "profit" is the net win; the payout (stake included) is what the other casinos report.
            r.payout = r.amount * r.multiplier;
        }
        // The server's net profit is the truth for the payout (stake included), also for a
        // 1x push or a partial payout that the >= 1x rule above classed as a loss.
        if (r.amount > 0.0 && bet.contains(QStringLiteral("profit")))
            r.payout = std::max(0.0, r.amount + profit);

        r.betId = bet.value(QStringLiteral("hash")).toString();
        r.nonce = bet.contains(QStringLiteral("nonce")) ? qint64(numberOf(bet.value(QStringLiteral("nonce")))) : -1;
        emit diceFinished(r);
    });
}

// Duel: POST https://duel.com/api/v2/client-seed/rotate {"client_seed":"R7mWIxg9Qf30VSJY"}
// Answer: {"success":true,"data":{"client_seed":"R7mWIxg9Qf30VSJY","server_seed_hashed":"031f...",
//   "next_server_seed_hash":"53b1...","nonce":0}}
void StakeClient::rotateDuelSeed(const QString &clientSeed)
{
    QString why;
    if (!m_transport || !m_transport->isReady(&why)) {
        const QString msg = m_transport ? why : tr("No transport configured.");
        QTimer::singleShot(0, this, [this, msg] { emit seedRotateFailed(msg); });
        return;
    }

    QJsonObject body;
    body.insert(QStringLiteral("client_seed"), clientSeed);

    m_transport->post(QStringLiteral("api/v2/client-seed/rotate"), body,
                      [this, clientSeed](const TransportReply &reply) {
        if (!reply.ok) {
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit seedRotateFailed(reply.error);
            return;
        }
        const QJsonObject &json = reply.json;
        QJsonObject data = json.value(QStringLiteral("data")).toObject();
        if (data.isEmpty())
            data = json; // also accept the fields at the top level
        const bool success = !json.contains(QStringLiteral("success"))
                             || json.value(QStringLiteral("success")).toBool();
        const QString hash = data.value(QStringLiteral("server_seed_hashed")).toString();
        if (!success || reply.status >= 400 || (hash.isEmpty() && !data.contains(QStringLiteral("client_seed")))) {
            QString msg = goatedErrorText(json);
            if (msg.isEmpty())
                msg = tr("Duel seed rotation rejected (HTTP %1).").arg(reply.status);
            emit seedRotateFailed(msg);
            return;
        }
        QString activeClient = data.value(QStringLiteral("client_seed")).toString();
        if (activeClient.isEmpty())
            activeClient = clientSeed;
        const qint64 nonce = data.contains(QStringLiteral("nonce"))
                                 ? qint64(numberOf(data.value(QStringLiteral("nonce")))) : 0;
        emit seedRotated(activeClient, nonce, hash,
                         data.value(QStringLiteral("next_server_seed_hash")).toString());
    });
}

// Tower.bet: POST https://tower.bet/api/randomize  (Bearer token; cf_clearance in Direct mode)
//   {"clientSeed":"MFlqIA2PkSGpnZBDa"}
// Answer (HTTP 201): {"new":{"serverSeedHash","clientSeed"} (the NEXT pair, with a random client
//   seed the site picked), "current":{"serverSeedHash","clientSeed" (ours),"nonce":0} (the pair
//   now active)}; errors: Laravel {"message",...} with HTTP 4xx.
void StakeClient::rotateTowerSeed(const QString &clientSeed)
{
    QString why;
    if (!m_transport || !m_transport->isReady(&why)) {
        const QString msg = m_transport ? why : tr("No transport configured.");
        QTimer::singleShot(0, this, [this, msg] { emit seedRotateFailed(msg); });
        return;
    }
    const QString seed = clientSeed.trimmed().isEmpty() ? randomClientSeed(17) : clientSeed.trimmed();

    QJsonObject body;
    body.insert(QStringLiteral("clientSeed"), seed);

    m_transport->post(QStringLiteral("randomize"), body, [this, seed](const TransportReply &reply) {
        if (!reply.ok) {
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit seedRotateFailed(reply.error);
            return;
        }
        const QJsonObject current = reply.json.value(QStringLiteral("current")).toObject();
        const QString hash = current.value(QStringLiteral("serverSeedHash")).toString();
        if (reply.status >= 400 || current.isEmpty() || hash.isEmpty()) {
            QString msg = firstErrorText(reply.json, nullptr);
            if (msg.isEmpty())
                msg = reply.json.value(QStringLiteral("message")).toString();
            if (msg.isEmpty())
                msg = tr("Tower seed rotation rejected (HTTP %1).").arg(reply.status);
            emit seedRotateFailed(msg);
            return;
        }
        const QString active = current.value(QStringLiteral("clientSeed")).toString();
        const qint64 nonce = current.contains(QStringLiteral("nonce"))
                                 ? qint64(numberOf(current.value(QStringLiteral("nonce")))) : 0;
        const QString nextHash = reply.json.value(QStringLiteral("new")).toObject()
                                     .value(QStringLiteral("serverSeedHash")).toString();
        emit seedRotated(active.isEmpty() ? seed : active, nonce, hash, nextHash);
    });
}

// Paradice: mutation updateServerSeed (no arguments: only the server seed is renewed).
// Answer: {"data":{"updateServerSeed":{"id","serverSeed" (hash of the new active seed),
//   "serverSeedNonce" (0),"clientSeed" (unchanged),"serverSeedNext" (hash of the following one)}}}
void StakeClient::rotateParadiceSeed()
{
    QString why;
    if (!m_transport || !m_transport->isReady(&why)) {
        const QString msg = m_transport ? why : tr("No transport configured.");
        QTimer::singleShot(0, this, [this, msg] { emit seedRotateFailed(msg); });
        return;
    }

    QJsonObject body;
    body.insert(QStringLiteral("operationName"), QStringLiteral("updateServerSeed"));
    body.insert(QStringLiteral("variables"), QJsonObject());
    body.insert(QStringLiteral("query"), QStringLiteral(
        "mutation updateServerSeed {\n  updateServerSeed {\n    id\n    serverSeed\n"
        "    serverSeedNonce\n    clientSeed\n    serverSeedNext\n    __typename\n  }\n}\n"));

    m_transport->post(QString(), body, [this](const TransportReply &reply) {
        if (!reply.ok) {
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit seedRotateFailed(reply.error);
            return;
        }
        const QJsonObject data = reply.json.value(QStringLiteral("data")).toObject()
                                     .value(QStringLiteral("updateServerSeed")).toObject();
        const QString hash = data.value(QStringLiteral("serverSeed")).toString();
        if (data.isEmpty() || hash.isEmpty()) {
            QString msg = firstErrorText(reply.json, nullptr);
            if (msg.isEmpty())
                msg = tr("Paradice seed rotation rejected (HTTP %1).").arg(reply.status);
            emit seedRotateFailed(msg);
            return;
        }
        const qint64 nonce = data.contains(QStringLiteral("serverSeedNonce"))
                                 ? qint64(numberOf(data.value(QStringLiteral("serverSeedNonce")))) : 0;
        emit seedRotated(data.value(QStringLiteral("clientSeed")).toString(), nonce, hash,
                         data.value(QStringLiteral("serverSeedNext")).toString());
    });
}

// Paradice dice: POST https://api.paradice.in/api.php (GraphQL)
//   mutation rollDice($number: Float!, $betAmount: Float!, $side: RollSideEnum!, $currency: CurrencyEnum!)
//   variables: {"betAmount":"0.00000001","number":"50.00","side":"ABOVE"|"BELOW","currency":"BTC"}
// The roll runs 0.00 - 100.00; "ABOVE 50" wins with 50 % (same meaning as the app's target).
// Answer: {"data":{"rollDice":{"id","number","roll" (string),"rollSide","win" (bool),"betAmount",
//   "winAmount","currency","multiplier","chance",...}}}; errors come as {"errors":[{"message"}]}.
void StakeClient::placeParadiceDice(const DiceBetRequest &request)
{
    const double target = std::round(request.target * 100.0) / 100.0;
    const bool over = request.over;
    const double chance = over ? 100.0 - target : target;
    if (!(chance > 0.0) || chance >= 100.0) {
        const LiveBetResult r = failed(tr("Paradice: invalid target %1.").arg(target));
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }

    QJsonObject vars;
    vars.insert(QStringLiteral("betAmount"), QString::number(request.amount, 'f', 8));
    vars.insert(QStringLiteral("number"), QString::number(target, 'f', 2));
    vars.insert(QStringLiteral("side"), over ? QStringLiteral("ABOVE") : QStringLiteral("BELOW"));
    vars.insert(QStringLiteral("currency"), ParadiceTransport::apiCurrency(m_currency));

    QJsonObject body;
    body.insert(QStringLiteral("operationName"), QStringLiteral("rollDice"));
    body.insert(QStringLiteral("variables"), vars);
    body.insert(QStringLiteral("query"), QStringLiteral(
        "mutation rollDice($number: Float!, $betAmount: Float!, $side: RollSideEnum!, "
        "$currency: CurrencyEnum!) {\n"
        "  rollDice(\n    number: $number\n    betAmount: $betAmount\n    side: $side\n"
        "    currency: $currency\n  ) {\n"
        "    id\n    number\n    roll\n    rollSide\n    win\n    betAmount\n    winAmount\n"
        "    currency\n    multiplier\n    chance\n    game\n    __typename\n  }\n}\n"));

    const double requestedAmount = request.amount;
    m_transport->post(QString(), body,
                      [this, requestedAmount, target, over, chance](const TransportReply &reply) {
        if (!reply.ok) {
            const bool isTimeout = (reply.status == 0);
            const bool unknown = (reply.status >= 500);
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit diceFinished(failed(reply.error, {}, isTimeout, unknown));
            return;
        }

        const QJsonObject &json = reply.json;
        const QJsonObject bet = json.value(QStringLiteral("data")).toObject()
                                    .value(QStringLiteral("rollDice")).toObject();
        if (bet.isEmpty()) {
            // No bet in the answer: a GraphQL error (or rate limit notice) - the bet was not placed.
            QString type;
            QString errText = firstErrorText(json, &type);
            if (errText.isEmpty())
                errText = tr("Paradice bet rejected (HTTP %1).").arg(reply.status);
            if (reply.status >= 400 || json.contains(QStringLiteral("errors")) || isRetryable(type, errText)) {
                LiveBetResult r = failed(errText, type, isRetryable(type, errText), false);
                r.retryDelayMs = fastRetryDelayMs(type, errText);
                // "slowdown_please": the bet was not placed - keep betting without the 5 s wait.
                r.retryNow = containsCI(errText, "slowdown");
                emit diceFinished(r);
                return;
            }
            const QString raw = QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)).left(400);
            emit diceFinished(failed(tr("Unexpected Paradice bet response (no bet found): %1").arg(raw),
                                     {}, false, true));
            return;
        }
        if (!bet.contains(QStringLiteral("roll"))) {
            const QString raw = QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)).left(400);
            emit diceFinished(failed(tr("Unexpected Paradice bet response (no roll found): %1").arg(raw),
                                     {}, false, true));
            return;
        }

        LiveBetResult r;
        r.ok = true;
        r.amount = bet.contains(QStringLiteral("betAmount")) ? numberOf(bet.value(QStringLiteral("betAmount")))
                                                             : requestedAmount;
        r.roll = numberOf(bet.value(QStringLiteral("roll")));
        if (bet.value(QStringLiteral("win")).isBool())
            r.win = bet.value(QStringLiteral("win")).toBool();
        else
            r.win = over ? r.roll > target : r.roll < target;

        double mult = numberOf(bet.value(QStringLiteral("multiplier")));
        if (!(mult > 0.0))
            mult = std::floor(99.0 / chance * 10000.0 + 1e-9) / 10000.0; // 1 % house edge
        if (r.win) {
            r.multiplier = mult;
            r.payout = r.amount * mult;
            // "winAmount" may be the payout (stake included) or the net profit: take whichever
            // it is closer to, so the server's figure decides.
            const double winAmount = numberOf(bet.value(QStringLiteral("winAmount")));
            if (r.amount > 0.0 && winAmount > 0.0) {
                const double profit = r.amount * (mult - 1.0);
                r.payout = std::abs(winAmount - r.payout) <= std::abs(winAmount - profit)
                               ? winAmount : r.amount + winAmount;
            }
        }

        r.betId = bet.value(QStringLiteral("id")).toString();
        r.nonce = -1; // not part of the answer
        emit diceFinished(r);
    });
}

// The client seed Pasino gets with every bet (a random 64-character one until one is set).
static void ensurePasinoSeed(QString &seed)
{
    if (!seed.isEmpty())
        return;
    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    for (int i = 0; i < 64; ++i)
        seed += QLatin1Char(alphabet[QRandomGenerator::system()->bounded(36)]);
}

// Pasino: the client seed travels with every bet, so "rotating" only stores the new one here.
// The server seed is not touched (no seed endpoint is used).
void StakeClient::rotatePasinoSeed(const QString &clientSeed)
{
    const QString seed = clientSeed.trimmed();
    if (seed.isEmpty()) {
        QTimer::singleShot(0, this, [this] { emit seedRotateFailed(tr("Pasino: the client seed is empty.")); });
        return;
    }
    m_pasinoClientSeed = seed;
    QTimer::singleShot(0, this, [this, seed] { emit seedRotated(seed, 0, QString(), QString()); });
}

// Pasino dice: POST https://api.pasino.com/dice/play  (JSON sent as x-www-form-urlencoded)
//   {"token","language":"en","bet_amt":"0.10000000","coin":"GEM","type":"2","payout":"1.88904",
//    "client_seed":"<64 chars>"}
// type: 1 = low (roll under), 2 = high (roll over). The game is chosen by the payout, with a
// 5 % house edge: win chance = 95 / payout (2 decimals), payout = 95 / chance cut to 5 decimals
// (50.29 % -> 1.88904). The app's chance (1 % edge) is converted first, see chanceForRtp().
// The roll runs 0000 - 9999 (hundredths); 2x = 47.50 % = "High > 5249" / "Low < 4750".
// Answer: {"message":"","win":0|1,"payout":0,"roll_number":1790 (roll in hundredths: 17.90),
//   "coin":"GEM","balance":"5198.81240700","profit":"-0.10000000","bet_id":"34126921394",
//   "success":true}; errors: {"success":false,"message":"…"}.
void StakeClient::placePasinoDice(const DiceBetRequest &request)
{
    // Pasino's dice has a 5 % house edge (payout = 95 / chance): the chance is converted so the
    // multiplier set in the app is paid (2x: 49.50 % in the app -> 47.50 % on Pasino).
    const bool over = request.over;
    const double chance = chanceForRtp(appChanceOf(request), 95.0, 2);
    // The site's own targets on its 0000 - 9999 roll: High > (9999 - chance * 100), Low < chance * 100
    // (47.50 %: High > 52.49, Low < 47.50). Used only if the answer has no "win" field.
    const double target = over ? (9999.0 - std::round(chance * 100.0)) / 100.0 : chance;
    m_lastDiceTarget = target;
    if (!(chance > 0.0) || chance >= 100.0) {
        const LiveBetResult r = failed(tr("Pasino: invalid target %1.").arg(request.target, 0, 'f', 2));
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }
    // Pasino's own formula (bcdiv(95, chance, 5)): cut, never rounded up.
    const double payout = std::floor(95.0 / chance * 100000.0 + 1e-7) / 100000.0;

    ensurePasinoSeed(m_pasinoClientSeed);

    QJsonObject body;
    body.insert(QStringLiteral("bet_amt"), QString::number(request.amount, 'f', 8));
    body.insert(QStringLiteral("coin"), PasinoTransport::apiCurrency(m_currency));
    body.insert(QStringLiteral("type"), over ? QStringLiteral("2") : QStringLiteral("1"));
    body.insert(QStringLiteral("payout"), QString::number(payout, 'f', 5));
    body.insert(QStringLiteral("client_seed"), m_pasinoClientSeed);

    const double requestedAmount = request.amount;
    m_transport->post(QStringLiteral("dice/play"), body,
                      [this, requestedAmount, target, over, payout](const TransportReply &reply) {
        if (!reply.ok) {
            const bool isTimeout = (reply.status == 0);
            const bool unknown = (reply.status >= 500);
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit diceFinished(failed(reply.error, {}, isTimeout, unknown));
            return;
        }

        const QJsonObject &json = reply.json;
        const QString errText = PasinoTransport::errorText(json);
        if (!errText.isEmpty()) {
            // {"success":false,...}: the bet was not placed.
            LiveBetResult r = failed(errText, {}, isRetryable({}, errText), false);
            r.retryDelayMs = fastRetryDelayMs({}, errText);
            emit diceFinished(r);
            return;
        }
        if (!json.contains(QStringLiteral("roll_number"))) {
            const QString raw = QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)).left(400);
            emit diceFinished(failed(tr("Unexpected Pasino bet response (no roll found): %1").arg(raw),
                                     {}, false, true));
            return;
        }

        LiveBetResult r;
        r.ok = true;
        r.amount = requestedAmount;
        r.roll = numberOf(json.value(QStringLiteral("roll_number"))) / 100.0;
        const QJsonValue win = json.value(QStringLiteral("win"));
        if (win.isBool())
            r.win = win.toBool();
        else if (win.isDouble() || win.isString())
            r.win = numberOf(win) > 0.0;
        else
            r.win = over ? r.roll > target : r.roll < target;

        if (r.win) {
            // "profit" is the server's net figure; the stake comes back on top of it.
            if (json.contains(QStringLiteral("profit"))) {
                r.payout = r.amount + numberOf(json.value(QStringLiteral("profit")));
                r.multiplier = r.amount > 0.0 ? r.payout / r.amount : payout;
            } else {
                r.multiplier = payout;
                r.payout = r.amount * payout;
            }
        }

        const QJsonValue id = json.value(QStringLiteral("bet_id"));
        r.betId = id.isString() ? id.toString() : QString::number(qint64(numberOf(id)));
        r.nonce = -1; // not part of the answer
        if (json.contains(QStringLiteral("balance"))) // the account balance after this bet
            r.balanceAfter = numberOf(json.value(QStringLiteral("balance")));
        emit diceFinished(r);
    });
}

// Pasino limbo: POST https://api.pasino.com/limbo/play  (JSON sent as x-www-form-urlencoded)
//   {"token","language":"en","client_seed":"<64 chars>","bet_amt":"0.01000000","coin":"GEM",
//    "target_payout":"2.00"}
// Answer: {"message":"","win":0|1,"payout":0,"actual_payout":"1.48" (the multiplier rolled),
//   "coin":"GEM","balance":"…","profit":"-0.01000000","bet_id":"34126988486","success":true};
// errors: {"success":false,"message":"…"}. A win is actual_payout >= target_payout.
void StakeClient::placePasinoLimbo(const LimboBetRequest &request)
{
    const double target = std::round(request.multiplierTarget * 100.0) / 100.0;
    if (!(target > 1.0)) {
        const LiveBetResult r = failed(tr("Pasino: invalid limbo target %1.").arg(target));
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }
    ensurePasinoSeed(m_pasinoClientSeed);

    QJsonObject body;
    body.insert(QStringLiteral("client_seed"), m_pasinoClientSeed);
    body.insert(QStringLiteral("bet_amt"), QString::number(request.amount, 'f', 8));
    body.insert(QStringLiteral("coin"), PasinoTransport::apiCurrency(m_currency));
    body.insert(QStringLiteral("target_payout"), QString::number(target, 'f', 2));

    const double requestedAmount = request.amount;
    m_transport->post(QStringLiteral("limbo/play"), body,
                      [this, requestedAmount, target](const TransportReply &reply) {
        if (!reply.ok) {
            const bool isTimeout = (reply.status == 0);
            const bool unknown = (reply.status >= 500);
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit diceFinished(failed(reply.error, {}, isTimeout, unknown));
            return;
        }

        const QJsonObject &json = reply.json;
        const QString errText = PasinoTransport::errorText(json);
        if (!errText.isEmpty()) {
            // {"success":false,...}: the bet was not placed.
            LiveBetResult r = failed(errText, {}, isRetryable({}, errText), false);
            r.retryDelayMs = fastRetryDelayMs({}, errText);
            emit diceFinished(r);
            return;
        }
        if (!json.contains(QStringLiteral("actual_payout"))) {
            const QString raw = QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)).left(400);
            emit diceFinished(failed(tr("Unexpected Pasino limbo response (no result found): %1").arg(raw),
                                     {}, false, true));
            return;
        }

        LiveBetResult r;
        r.ok = true;
        r.amount = requestedAmount;
        r.roll = numberOf(json.value(QStringLiteral("actual_payout"))); // the multiplier rolled
        const QJsonValue win = json.value(QStringLiteral("win"));
        if (win.isBool())
            r.win = win.toBool();
        else if (win.isDouble() || win.isString())
            r.win = numberOf(win) > 0.0;
        else
            r.win = r.roll >= target;

        if (r.win) {
            // "profit" is the server's net figure; the stake comes back on top of it.
            if (json.contains(QStringLiteral("profit"))) {
                r.payout = r.amount + numberOf(json.value(QStringLiteral("profit")));
                r.multiplier = r.amount > 0.0 ? r.payout / r.amount : target;
            } else {
                r.multiplier = target;
                r.payout = r.amount * target;
            }
        }

        const QJsonValue id = json.value(QStringLiteral("bet_id"));
        r.betId = id.isString() ? id.toString() : QString::number(qint64(numberOf(id)));
        r.nonce = -1; // not part of the answer
        if (json.contains(QStringLiteral("balance"))) // the account balance after this bet
            r.balanceAfter = numberOf(json.value(QStringLiteral("balance")));
        emit diceFinished(r);
    });
}

// Tower.bet dice: POST https://tower.bet/api/dice/bet  (Bearer token; cf_clearance in Direct mode)
//   {"symbol":"USDC","isHigh":true,"number":50000,"amount":"0.01000404"}
// The roll runs 0 - 99999 (shown as 0.000 - 99.999), so the target is sent in thousandths:
// "over 50.75" -> isHigh true, number 50750 (choice ">50750"); the target keeps 2 decimals. The chance is converted from the
// app's 1 % edge to Tower's 1.5 % one first (see below), so the multiplier set in the app is paid.
// Answer (HTTP 201): {"hash" (bet id),"game":"dice","number":38492 (the roll),"isWin":false,
//   "choice":">50000","symbol","betAmount","payout" (multiplier, "0" on a loss),"winAmount",
//   "profit":"-0.01000404","nonce":1,"userSeed":{"serverSeedHash","clientSeed","nonce"},
//   "account":{"hash","newBalance":"0.47999192"}}; errors: Laravel {"message",...} with HTTP 4xx.
void StakeClient::placeTowerDice(const DiceBetRequest &request)
{
    // Tower's dice has a 1.5 % house edge (payout = 98.5 / chance: ">50000" = 49.999 % pays
    // 1.97x, "<51000" = 51 % pays 1.93x), the app's targets a 1 % one (multiplier = 99 / chance).
    // Keep the multiplier the user set: the chance sent to Tower is scaled by 98.5 / 99, so 2x
    // (49.50 % in the app) becomes 49.25 %, i.e. "over 50.75" / "under 49.25" (number 50750 /
    // 49250). The site takes the target in thousandths.
    constexpr double kTowerRtp = 98.5;
    const double appTarget = std::round(request.target * 100.0) / 100.0;
    const bool over = request.over;
    const double appChance = appChanceOf(request);
    // Target with 2 decimals like every other casino, sent in thousandths. Tower cuts its payout
    // (98.5 / chance) to 2 decimals, so the chance is rounded DOWN, never up: 1.5x = 66 % in the
    // app -> 65.6666 % -> 65.66 % ("over 34.34") pays 1.50015 -> 1.50x, where 65.67 % would pay
    // 1.49992 -> 1.49x. (2x: 49.25 % exactly -> 2.00x.)
    const double chance = std::floor(appChance * kTowerRtp / 99.0 * 100.0 + 1e-6) / 100.0;
    const double target = std::round((over ? 100.0 - chance : chance) * 100.0) / 100.0;
    const qint64 number = qint64(std::llround(target * 100.0)) * 10;
    m_lastDiceTarget = target;
    if (!(appChance > 0.0) || number <= 0 || number >= 100000) {
        const LiveBetResult r = failed(tr("Tower: invalid target %1.").arg(appTarget));
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }

    QJsonObject body;
    body.insert(QStringLiteral("symbol"), TowerTransport::apiCurrency(m_currency));
    body.insert(QStringLiteral("isHigh"), over);
    body.insert(QStringLiteral("number"), number);
    body.insert(QStringLiteral("amount"), QString::number(request.amount, 'f', 8));

    const double requestedAmount = request.amount;
    m_transport->post(QStringLiteral("dice/bet"), body,
                      [this, requestedAmount, target, over](const TransportReply &reply) {
        if (!reply.ok) {
            const bool isTimeout = (reply.status == 0);
            const bool unknown = (reply.status >= 500 && reply.status != 503);
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit diceFinished(failed(reply.error, {}, isTimeout, unknown));
            return;
        }

        const QJsonObject &json = reply.json;
        if (reply.status >= 400 || json.contains(QStringLiteral("errors"))) {
            // Refused by the server (validation, balance, rate limit): the bet was not placed.
            QString errText = firstErrorText(json, nullptr);
            if (errText.isEmpty())
                errText = json.value(QStringLiteral("message")).toString();
            if (errText.isEmpty())
                errText = tr("Tower bet rejected (HTTP %1).").arg(reply.status);
            LiveBetResult r = failed(errText, {}, isRetryable({}, errText), false);
            r.retryDelayMs = fastRetryDelayMs({}, errText);
            emit diceFinished(r);
            return;
        }
        if (!json.contains(QStringLiteral("number"))) {
            const QString raw = QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)).left(400);
            emit diceFinished(failed(tr("Unexpected Tower bet response (no roll found): %1").arg(raw),
                                     {}, false, true));
            return;
        }

        LiveBetResult r;
        r.ok = true;
        r.amount = json.contains(QStringLiteral("betAmount"))
                       ? numberOf(json.value(QStringLiteral("betAmount"))) : requestedAmount;
        r.roll = numberOf(json.value(QStringLiteral("number"))) / 1000.0;
        const QJsonValue win = json.value(QStringLiteral("isWin"));
        if (win.isBool())
            r.win = win.toBool();
        else
            r.win = over ? r.roll > target : r.roll < target;

        if (r.win) {
            // "profit" is the server's net figure; the stake comes back on top of it.
            const double mult = numberOf(json.value(QStringLiteral("payout")));
            if (json.contains(QStringLiteral("profit"))) {
                r.payout = r.amount + numberOf(json.value(QStringLiteral("profit")));
                r.multiplier = r.amount > 0.0 ? r.payout / r.amount : mult;
            } else {
                r.payout = numberOf(json.value(QStringLiteral("winAmount")));
                r.multiplier = mult;
            }
            if (!(r.multiplier > 0.0))
                r.multiplier = mult;
        }

        r.betId = json.value(QStringLiteral("hash")).toString();
        r.nonce = json.contains(QStringLiteral("nonce"))
                      ? qint64(numberOf(json.value(QStringLiteral("nonce")))) : -1;
        // "account.newBalance": the account balance right after this bet.
        const QJsonObject account = json.value(QStringLiteral("account")).toObject();
        if (account.contains(QStringLiteral("newBalance")))
            r.balanceAfter = numberOf(account.value(QStringLiteral("newBalance")));
        emit diceFinished(r);
    });
}

// Tower.bet limbo: POST https://tower.bet/api/limbo/bet  (Bearer token; cf_clearance in Direct mode)
//   {"symbol":"USDC","amount":"0.01000404","choice":"2"}   (choice = the target multiplier)
// Answer (HTTP 201): {"hash" (bet id),"game":"limbo","number":539368,"result":"2.03" (the
//   multiplier rolled),"isWin":true,"choice":"2","betAmount","payout":"2" (multiplier paid, "0" on
//   a loss),"winAmount","profit","nonce":86,"userSeed":{…},"account":{"hash","newBalance"}}.
// Limbo pays exactly the target multiplier, so no house-edge conversion is needed.
void StakeClient::placeTowerLimbo(const LimboBetRequest &request)
{
    const double target = std::round(request.multiplierTarget * 100.0) / 100.0;
    if (!(target > 1.0)) {
        const LiveBetResult r = failed(tr("Tower: invalid limbo target %1.").arg(target));
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }
    // Like the site: "2", "2.5", "1.01" (no trailing zeros).
    QString choice = QString::number(target, 'f', 2);
    while (choice.endsWith(QLatin1Char('0')))
        choice.chop(1);
    if (choice.endsWith(QLatin1Char('.')))
        choice.chop(1);

    QJsonObject body;
    body.insert(QStringLiteral("symbol"), TowerTransport::apiCurrency(m_currency));
    body.insert(QStringLiteral("amount"), QString::number(request.amount, 'f', 8));
    body.insert(QStringLiteral("choice"), choice);

    const double requestedAmount = request.amount;
    m_transport->post(QStringLiteral("limbo/bet"), body,
                      [this, requestedAmount, target](const TransportReply &reply) {
        if (!reply.ok) {
            const bool isTimeout = (reply.status == 0);
            const bool unknown = (reply.status >= 500 && reply.status != 503);
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit diceFinished(failed(reply.error, {}, isTimeout, unknown));
            return;
        }

        const QJsonObject &json = reply.json;
        if (reply.status >= 400 || json.contains(QStringLiteral("errors"))) {
            // Refused by the server (validation, balance, rate limit): the bet was not placed.
            QString errText = firstErrorText(json, nullptr);
            if (errText.isEmpty())
                errText = json.value(QStringLiteral("message")).toString();
            if (errText.isEmpty())
                errText = tr("Tower limbo bet rejected (HTTP %1).").arg(reply.status);
            LiveBetResult r = failed(errText, {}, isRetryable({}, errText), false);
            r.retryDelayMs = fastRetryDelayMs({}, errText);
            emit diceFinished(r);
            return;
        }
        if (!json.contains(QStringLiteral("result"))) {
            const QString raw = QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)).left(400);
            emit diceFinished(failed(tr("Unexpected Tower limbo response (no result found): %1").arg(raw),
                                     {}, false, true));
            return;
        }

        LiveBetResult r;
        r.ok = true;
        r.amount = json.contains(QStringLiteral("betAmount"))
                       ? numberOf(json.value(QStringLiteral("betAmount"))) : requestedAmount;
        r.roll = numberOf(json.value(QStringLiteral("result"))); // the multiplier rolled
        const QJsonValue win = json.value(QStringLiteral("isWin"));
        r.win = win.isBool() ? win.toBool() : r.roll >= target;

        if (r.win) {
            // "profit" is the server's net figure; the stake comes back on top of it.
            const double mult = numberOf(json.value(QStringLiteral("payout")));
            if (json.contains(QStringLiteral("profit"))) {
                r.payout = r.amount + numberOf(json.value(QStringLiteral("profit")));
                r.multiplier = r.amount > 0.0 ? r.payout / r.amount : mult;
            } else {
                r.payout = numberOf(json.value(QStringLiteral("winAmount")));
                r.multiplier = mult;
            }
            if (!(r.multiplier > 0.0))
                r.multiplier = mult > 0.0 ? mult : target;
        }

        r.betId = json.value(QStringLiteral("hash")).toString();
        r.nonce = json.contains(QStringLiteral("nonce"))
                      ? qint64(numberOf(json.value(QStringLiteral("nonce")))) : -1;
        // "account.newBalance": the account balance right after this bet.
        const QJsonObject account = json.value(QStringLiteral("account")).toObject();
        if (account.contains(QStringLiteral("newBalance")))
            r.balanceAfter = numberOf(account.value(QStringLiteral("newBalance")));
        emit diceFinished(r);
    });
}

// Winna dice: POST https://originals.winna.com/dice/play  (headers "token: <game jwt>", "sid")
//   {"bet":"0.01","turbo":true,"data":{"mode":"classic","target":"lower","value":49.5}}
// "higher" + value 50.5 = roll over 50.50 (2x); "lower" + value 49.5 = roll under 49.50 (range
// 0 - 49.5). The roll runs 0.00 - 100.00 with a 1 % house edge (49.5 % pays 2x), the same as the app, so the target is sent as it is.
// The game token belongs to the bet currency: WinnaTransport switches the wallet coin and starts
// a dice session first (change-wallet-coin + our-game/start).
// Answer: {"coin":"USDC","status":"win","id":1540147593,"multiplier":2,"wager":0.01,"profit":0.02,
//   "balance":5,"data":{"number":1.48 (the roll),"win":true,"multiplier":2,"target","value",
//   "ranges":[{"low":0,"high":49.5}]},"betUuid",…}
void StakeClient::placeWinnaDice(const DiceBetRequest &request)
{
    const double target = std::round(request.target * 100.0) / 100.0;
    if (!(target > 0.0) || !(target < 100.0)) {
        const LiveBetResult r = failed(tr("Winna: invalid target %1.").arg(target));
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }
    if (auto *winna = qobject_cast<WinnaTransport *>(m_transport))
        winna->setCoin(m_currency);
    sendWinnaDice(request.amount, target, request.over, m_winnaOverWord);
}

void StakeClient::sendWinnaDice(double amount, double target, bool over, int overWord)
{
    // "lower" / "higher" are the site's words for roll under / roll over ({"target":"higher",
    // "value":50.5} = 2x). The other words are only a fallback if the server ever refuses "higher".
    static const QStringList kOverWords{QStringLiteral("higher"), QStringLiteral("upper"),
                                        QStringLiteral("over"), QStringLiteral("above")};
    const int wordIndex = std::clamp(overWord, 0, int(kOverWords.size()) - 1);

    // Amount as a plain decimal without trailing zeros ("0.01"), like the site.
    QString betText = QString::number(std::max(0.0, amount), 'f', 8);
    while (betText.contains(QLatin1Char('.')) && betText.endsWith(QLatin1Char('0')))
        betText.chop(1);
    if (betText.endsWith(QLatin1Char('.')))
        betText.chop(1);

    QJsonObject data;
    data.insert(QStringLiteral("mode"), QStringLiteral("classic"));
    data.insert(QStringLiteral("target"), over ? kOverWords.at(wordIndex) : QStringLiteral("lower"));
    data.insert(QStringLiteral("value"), target);
    QJsonObject body;
    body.insert(QStringLiteral("bet"), betText);
    body.insert(QStringLiteral("turbo"), true);
    body.insert(QStringLiteral("data"), data);

    m_transport->post(QStringLiteral("dice/play"), body,
                      [this, amount, target, over, wordIndex](const TransportReply &reply) {
        if (!reply.ok) {
            const bool isTimeout = (reply.status == 0);
            const bool unknown = (reply.status >= 500 && reply.status != 503);
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit diceFinished(failed(reply.error, {}, isTimeout, unknown));
            return;
        }

        const QJsonObject &json = reply.json;
        if (reply.status >= 400 || reply.status == 0 || json.contains(QStringLiteral("errors"))) {
            QString errText = firstErrorText(json, nullptr);
            if (errText.isEmpty())
                errText = tr("Winna bet rejected (HTTP %1).").arg(reply.status);
            // A roll-over bet refused because of its "target" word: try the next word for "over"
            // (a refused bet was not placed). Other refusals (minimum bet, balance, …) are not retried.
            const bool targetRejected = containsCI(errText, "target") || containsCI(errText, "one of")
                                        || containsCI(errText, "enum") || containsCI(errText, "validation");
            if (over && targetRejected && (reply.status == 400 || reply.status == 422)
                && wordIndex + 1 < kOverWords.size()) {
                sendWinnaDice(amount, target, over, wordIndex + 1);
                return;
            }
            LiveBetResult r = failed(errText, {}, isRetryable({}, errText), false);
            r.retryDelayMs = fastRetryDelayMs({}, errText);
            emit diceFinished(r);
            return;
        }
        const QJsonObject d = json.value(QStringLiteral("data")).toObject();
        if (!d.contains(QStringLiteral("number"))) {
            const QString raw = QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)).left(400);
            emit diceFinished(failed(tr("Unexpected Winna bet response (no roll found): %1").arg(raw),
                                     {}, false, true));
            return;
        }
        if (over)
            m_winnaOverWord = wordIndex; // this word works: keep it

        LiveBetResult r;
        r.ok = true;
        r.amount = json.contains(QStringLiteral("wager")) ? numberOf(json.value(QStringLiteral("wager")))
                                                          : amount;
        r.roll = numberOf(d.value(QStringLiteral("number")));
        if (d.value(QStringLiteral("win")).isBool())
            r.win = d.value(QStringLiteral("win")).toBool();
        else if (json.value(QStringLiteral("status")).isString())
            r.win = json.value(QStringLiteral("status")).toString().compare(QLatin1String("win"),
                                                                            Qt::CaseInsensitive) == 0;
        else
            r.win = over ? r.roll > target : r.roll < target;

        if (r.win) {
            double mult = numberOf(d.value(QStringLiteral("multiplier")));
            if (!(mult > 0.0))
                mult = numberOf(json.value(QStringLiteral("multiplier")));
            if (!(mult > 0.0)) {
                const double chance = over ? 100.0 - target : target;
                mult = chance > 0.0 ? 99.0 / chance : 0.0;
            }
            r.multiplier = mult;
            r.payout = r.amount * mult; // "profit" is the amount paid back (0.02 for 0.01 at 2x)
        }

        const QJsonValue id = json.value(QStringLiteral("id"));
        r.betId = id.isString() ? id.toString() : QString::number(qint64(numberOf(id)));
        if (r.betId.isEmpty() || r.betId == QLatin1String("0"))
            r.betId = json.value(QStringLiteral("betUuid")).toString();
        r.nonce = -1;
        emit diceFinished(r);
    });
}

// Winna: POST https://api2.winna.com/v1/games/our-game/unhash {"seedId":1102869,"clientSeed":"…"}
// Answer: {"success":true,"response":{"new_seed":{"id":1103124,"nonce":0,"client_seed",
//   "server_seed" (hash of the new active seed),"next_server_seed","is_active":true},
//   "old_seed":{… "server_seed" revealed …}}}
void StakeClient::rotateWinnaSeed(const QString &clientSeed)
{
    auto *winna = qobject_cast<WinnaTransport *>(m_transport);
    if (!winna) {
        QTimer::singleShot(0, this, [this] { emit seedRotateFailed(tr("No transport configured.")); });
        return;
    }
    const QString seed = clientSeed.trimmed().isEmpty() ? randomClientSeed(10) : clientSeed.trimmed();
    winna->rotateSeed(seed, [this](const QString &error, const QJsonObject &newSeed) {
        if (!error.isEmpty()) {
            emit seedRotateFailed(error);
            return;
        }
        const qint64 nonce = newSeed.contains(QStringLiteral("nonce"))
                                 ? qint64(numberOf(newSeed.value(QStringLiteral("nonce")))) : 0;
        emit seedRotated(newSeed.value(QStringLiteral("client_seed")).toString(), nonce,
                         newSeed.value(QStringLiteral("server_seed")).toString(),
                         newSeed.value(QStringLiteral("next_server_seed")).toString());
    });
}

// Winna limbo: POST https://originals.winna.com/limbo/play  (headers "token: <game jwt>", "sid")
//   {"bet":"0.01","turbo":false,"data":{"target":2}}   (target = the multiplier to reach)
// Answer: {"coin":"USDC","status":"win","id":1540217560,"multiplier":2,"wager":0.01,
//   "profit":0.02 (the amount paid back),"balance":3.70,"data":{"target":2,"number":2.75 (the
//   multiplier rolled)},"betUuid",…}. Limbo pays the target exactly: no house-edge conversion.
void StakeClient::placeWinnaLimbo(const LimboBetRequest &request)
{
    const double target = std::round(request.multiplierTarget * 100.0) / 100.0;
    if (!(target > 1.0)) {
        const LiveBetResult r = failed(tr("Winna: invalid limbo target %1.").arg(target));
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }
    if (auto *winna = qobject_cast<WinnaTransport *>(m_transport))
        winna->setCoin(m_currency);

    // Amount as a plain decimal without trailing zeros ("0.01"), like the site.
    QString betText = QString::number(std::max(0.0, request.amount), 'f', 8);
    while (betText.contains(QLatin1Char('.')) && betText.endsWith(QLatin1Char('0')))
        betText.chop(1);
    if (betText.endsWith(QLatin1Char('.')))
        betText.chop(1);

    QJsonObject data;
    data.insert(QStringLiteral("target"), target);
    QJsonObject body;
    body.insert(QStringLiteral("bet"), betText);
    body.insert(QStringLiteral("turbo"), false);
    body.insert(QStringLiteral("data"), data);

    const double amount = request.amount;
    m_transport->post(QStringLiteral("limbo/play"), body, [this, amount, target](const TransportReply &reply) {
        if (!reply.ok) {
            const bool isTimeout = (reply.status == 0);
            const bool unknown = (reply.status >= 500 && reply.status != 503);
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit diceFinished(failed(reply.error, {}, isTimeout, unknown));
            return;
        }

        const QJsonObject &json = reply.json;
        if (reply.status >= 400 || reply.status == 0 || json.contains(QStringLiteral("errors"))) {
            // Refused (no game session, validation, balance, rate limit): the bet was not placed.
            QString errText = firstErrorText(json, nullptr);
            if (errText.isEmpty())
                errText = tr("Winna limbo bet rejected (HTTP %1).").arg(reply.status);
            LiveBetResult r = failed(errText, {}, isRetryable({}, errText), false);
            r.retryDelayMs = fastRetryDelayMs({}, errText);
            emit diceFinished(r);
            return;
        }
        const QJsonObject d = json.value(QStringLiteral("data")).toObject();
        if (!d.contains(QStringLiteral("number"))) {
            const QString raw = QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)).left(400);
            emit diceFinished(failed(tr("Unexpected Winna limbo response (no result found): %1").arg(raw),
                                     {}, false, true));
            return;
        }

        LiveBetResult r;
        r.ok = true;
        r.amount = json.contains(QStringLiteral("wager")) ? numberOf(json.value(QStringLiteral("wager")))
                                                          : amount;
        r.roll = numberOf(d.value(QStringLiteral("number"))); // the multiplier rolled
        if (d.value(QStringLiteral("win")).isBool())
            r.win = d.value(QStringLiteral("win")).toBool();
        else if (json.value(QStringLiteral("status")).isString())
            r.win = json.value(QStringLiteral("status")).toString().compare(QLatin1String("win"),
                                                                            Qt::CaseInsensitive) == 0;
        else
            r.win = r.roll >= target;

        if (r.win) {
            double mult = numberOf(json.value(QStringLiteral("multiplier")));
            if (!(mult > 0.0))
                mult = target;
            r.multiplier = mult;
            r.payout = r.amount * mult; // "profit" is the amount paid back (0.02 for 0.01 at 2x)
        }

        const QJsonValue id = json.value(QStringLiteral("id"));
        r.betId = id.isString() ? id.toString() : QString::number(qint64(numberOf(id)));
        if (r.betId.isEmpty() || r.betId == QLatin1String("0"))
            r.betId = json.value(QStringLiteral("betUuid")).toString();
        r.nonce = -1;
        emit diceFinished(r);
    });
}

// Duel dice: POST https://duel.com/api/v2/dice/bet
//   {"amount":"0","bet_type":"over","currency":102,"security_token":"<token>","target":"5005"}
// The roll runs 0 - 10000 (shown as 0.00 - 100.00), so the target is sent in hundredths:
// "over 50.05" -> "5005". The currency is Duel's number (101 = btc, 102 = bch, ...).
// Answer: {"data":{"round":{"id","result" (the roll, in hundredths),"is_win","amount_currency"
//   (the stake),"amount_won" (the payout, stake included, 0 on a loss),"bet_type","target",
//   "win_chance","multiplier","nonce","client_seed",...}}} (also accepted without "data").
// The security token expires every 600 s: the transport renews it before that, and a bet the
// server rejects for an invalid / expired token is sent once more with a fresh one.
void StakeClient::placeDuelDice(const DiceBetRequest &request)
{
    const int currencyId = DuelTransport::currencyId(m_currency);
    if (currencyId == 0) {
        const LiveBetResult r = failed(tr("Duel: unknown currency \"%1\" (use one of: %2).")
                                           .arg(m_currency,
                                                DuelTransport::currencyCodes().join(QStringLiteral(", "))));
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }

    // Duel's dice has a 0.1 % house edge (payout = 99.9 / chance): the chance is converted so
    // the multiplier set in the app is paid (2x: 49.50 % in the app -> 49.95 % on Duel).
    const double chance = chanceForRtp(appChanceOf(request), 99.9, 2);
    const qint64 target = qint64(std::llround((request.over ? 100.0 - chance : chance) * 100.0));
    m_lastDiceTarget = double(target) / 100.0; // "5005" -> 50.05
    if (!(chance > 0.0) || target <= 0 || target >= 10000) {
        const LiveBetResult r = failed(tr("Duel: invalid target %1.").arg(request.target, 0, 'f', 2));
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }

    // Amount as a plain decimal string without trailing zeros ("0", "0.00012"), like the site.
    QString amountText = QString::number(std::max(0.0, request.amount), 'f', 8);
    while (amountText.contains(QLatin1Char('.')) && amountText.endsWith(QLatin1Char('0')))
        amountText.chop(1);
    if (amountText.endsWith(QLatin1Char('.')))
        amountText.chop(1);

    QJsonObject fields;
    fields.insert(QStringLiteral("amount"), amountText);
    fields.insert(QStringLiteral("bet_type"), request.over ? QStringLiteral("over") : QStringLiteral("under"));
    fields.insert(QStringLiteral("currency"), currencyId);
    fields.insert(QStringLiteral("target"), QString::number(target));
    sendDuelBet(QStringLiteral("api/v2/dice/bet"), fields, request.amount, false,
                double(target) / 100.0, request.over, false);
}

// Duel limbo: POST https://duel.com/api/v2/limbo/bet
//   {"amount":"0.0114738","target":"2.00","currency":106,"security_token":"<token>","instant":false}
// Answer: {"success":true,"data":{"round":{"id","result":"5.388335" (the multiplier rolled),"is_win",
//   "amount_currency" (the stake),"amount_won" (the payout, stake included),"target","win_chance",
//   "multiplier","nonce",...}}}
void StakeClient::placeDuelLimbo(const LimboBetRequest &request)
{
    const int currencyId = DuelTransport::currencyId(m_currency);
    if (currencyId == 0) {
        const LiveBetResult r = failed(tr("Duel: unknown currency \"%1\" (use one of: %2).")
                                           .arg(m_currency,
                                                DuelTransport::currencyCodes().join(QStringLiteral(", "))));
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }
    const double target = std::round(request.multiplierTarget * 100.0) / 100.0;
    if (!(target > 1.0)) {
        const LiveBetResult r = failed(tr("Duel: invalid Limbo multiplier %1.").arg(target));
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }

    QString amountText = QString::number(std::max(0.0, request.amount), 'f', 8);
    while (amountText.contains(QLatin1Char('.')) && amountText.endsWith(QLatin1Char('0')))
        amountText.chop(1);
    if (amountText.endsWith(QLatin1Char('.')))
        amountText.chop(1);

    QJsonObject fields;
    fields.insert(QStringLiteral("amount"), amountText);
    fields.insert(QStringLiteral("target"), QString::number(target, 'f', 2)); // "2.00", like the site
    fields.insert(QStringLiteral("currency"), currencyId);
    fields.insert(QStringLiteral("instant"), false);
    sendDuelBet(QStringLiteral("api/v2/limbo/bet"), fields, request.amount, true, target, false, false);
}

namespace
{
// The bet was refused because the security token is missing, invalid or expired.
bool isDuelTokenError(int status, const QString &text)
{
    if (status == 419)
        return true;
    const bool mentionsToken = containsCI(text, "security") || containsCI(text, "token");
    const bool tokenProblem = containsCI(text, "expired") || containsCI(text, "invalid")
                              || containsCI(text, "required") || containsCI(text, "missing")
                              || containsCI(text, "not valid") || containsCI(text, "incorrect");
    return mentionsToken && (tokenProblem || status == 401 || status == 403 || status == 422);
}
} // namespace

void StakeClient::sendDuelBet(const QString &path, const QJsonObject &fields, double requestedAmount,
                              bool limbo, double target, bool over, bool tokenRetried)
{
    auto *duel = qobject_cast<DuelTransport *>(m_transport);
    if (!duel) {
        const LiveBetResult r = failed(tr("No transport configured."));
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }
    QPointer<DuelTransport> guard(duel);

    duel->withSecurityToken([this, guard, path, fields, requestedAmount, limbo, target, over,
                             tokenRetried](const QString &token, const QString &tokenError, int tokenStatus) {
        if (!guard || guard.data() != m_transport) {
            emit diceFinished(failed(tr("Duel: the casino was switched before the bet was sent.")));
            return;
        }
        if (token.isEmpty()) {
            // No token, so no bet was sent: safe to retry when it was a network / rate limit problem.
            const bool retry = tokenStatus == 0 || tokenStatus == 429 || tokenStatus >= 500
                               || isRetryable({}, tokenError);
            if (tokenStatus == 0)
                emit sessionLost(tokenError);
            LiveBetResult r = failed(tr("Duel: could not get a security token: %1").arg(tokenError),
                                     {}, retry, false);
            emit diceFinished(r);
            return;
        }

        QJsonObject body = fields;
        body.insert(QStringLiteral("security_token"), token);

        guard->post(path, body,
                    [this, guard, path, fields, requestedAmount, limbo, target, over,
                     tokenRetried](const TransportReply &reply) {
            // Security token expired / rejected: the bet was not placed. Get a new token and
            // send the same bet once more.
            const QString replyText = reply.ok ? goatedErrorText(reply.json) : reply.error;
            // {"data":{"round":{...}}} (also accepted: {"round":{...}}).
            QJsonObject round = reply.json.value(QStringLiteral("round")).toObject();
            if (round.isEmpty())
                round = reply.json.value(QStringLiteral("data")).toObject()
                            .value(QStringLiteral("round")).toObject();
            const bool hasRound = reply.ok && !round.isEmpty();
            if (!hasRound && !tokenRetried && guard && isDuelTokenError(reply.status, replyText)) {
                guard->invalidateSecurityToken();
                sendDuelBet(path, fields, requestedAmount, limbo, target, over, true);
                return;
            }

            if (!reply.ok) {
                const bool isTimeout = (reply.status == 0);
                const bool unknown = (reply.status >= 500);
                if (reply.status == 0)
                    emit sessionLost(reply.error);
                emit diceFinished(failed(reply.error, {}, isTimeout, unknown));
                return;
            }

            const QJsonObject &json = reply.json;
            if (round.isEmpty()) {
                // No round in the answer: an API error (or rate limit notice) - the bet was not placed.
                QString errText = replyText;
                if (errText.isEmpty())
                    errText = tr("Duel bet rejected (HTTP %1).").arg(reply.status);
                if (reply.status >= 400 || isRetryable({}, errText)) {
                    LiveBetResult r = failed(errText, {}, isRetryable({}, errText), false);
                    r.retryDelayMs = fastRetryDelayMs({}, errText);
                    emit diceFinished(r);
                    return;
                }
                const QString raw = QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)).left(400);
                // The bet may or may not have been placed: do not retry blindly.
                emit diceFinished(failed(tr("Unexpected Duel bet response (no round found): %1").arg(raw),
                                         {}, false, true));
                return;
            }
            if (!round.contains(QStringLiteral("result"))) {
                const QString raw = QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)).left(400);
                emit diceFinished(failed(tr("Unexpected Duel bet response (no roll found): %1").arg(raw),
                                         {}, false, true));
                return;
            }

            LiveBetResult r;
            r.ok = true;
            // "amount_currency" is the stake the server took (same unit as the balance).
            r.amount = round.contains(QStringLiteral("amount_currency"))
                           ? numberOf(round.value(QStringLiteral("amount_currency")))
                           : requestedAmount;
            // Dice: the roll in hundredths (5005 -> 50.05). Limbo: the multiplier rolled ("5.388335").
            r.roll = limbo ? numberOf(round.value(QStringLiteral("result")))
                           : numberOf(round.value(QStringLiteral("result"))) / 100.0;

            if (round.value(QStringLiteral("is_win")).isBool())
                r.win = round.value(QStringLiteral("is_win")).toBool();
            else if (limbo)
                r.win = r.roll >= target;
            else
                r.win = over ? r.roll > target : r.roll < target;

            if (r.win) {
                double mult = numberOf(round.value(QStringLiteral("multiplier")));
                if (!(mult > 0.0) && limbo)
                    mult = target;
                if (!(mult > 0.0)) {
                    // Fallback: 0.1 % edge (what the server answered with).
                    const double chance = numberOf(round.value(QStringLiteral("win_chance")));
                    mult = chance > 0.0 ? 0.999 / chance : 0.0;
                }
                r.multiplier = mult;
                r.payout = r.amount * r.multiplier;
                // The server's payout (stake included) is the truth when it is there.
                const double won = numberOf(round.value(QStringLiteral("amount_won")));
                if (won > 0.0)
                    r.payout = won;
            }

            const QJsonValue idv = round.value(QStringLiteral("id"));
            r.betId = idv.isDouble() ? QString::number(qint64(idv.toDouble())) : idv.toString();
            r.nonce = round.contains(QStringLiteral("nonce"))
                          ? qint64(numberOf(round.value(QStringLiteral("nonce")))) : -1;
            emit diceFinished(r);
        });
    });
}

// Wolfbet limbo: POST https://wolfbet.com/api/v2/limbo/manual/play
//   {"currency":"usdt","game":"limbo","amount":"0.00000001","multiplier":"2","auto":1}
// Answer: {"bet":{"hash","nonce","amount","profit" (net),"multiplier","result_value" (the result),
//   "state":"win"|"loss",...},"userBalance":{...}}
void StakeClient::placeWolfbetLimbo(const LimboBetRequest &request)
{
    const double target = std::round(request.multiplierTarget * 100.0) / 100.0;
    if (!(target > 1.0)) {
        const LiveBetResult r = failed(tr("Wolfbet: invalid Limbo multiplier %1.").arg(target));
        QTimer::singleShot(0, this, [this, r] { emit diceFinished(r); });
        return;
    }

    // "2", "2.5", "1.01": no trailing zeros, like the site.
    QString multText = QString::number(target, 'f', 2);
    while (multText.endsWith(QLatin1Char('0')))
        multText.chop(1);
    if (multText.endsWith(QLatin1Char('.')))
        multText.chop(1);

    QJsonObject body;
    body.insert(QStringLiteral("currency"), m_currency.toLower());
    body.insert(QStringLiteral("game"), QStringLiteral("limbo"));
    body.insert(QStringLiteral("amount"), QString::number(request.amount, 'f', 8));
    body.insert(QStringLiteral("multiplier"), multText);
    body.insert(QStringLiteral("auto"), 1);

    const double requestedAmount = request.amount;
    m_transport->post(QStringLiteral("api/v2/limbo/manual/play"), body,
                      [this, requestedAmount, target](const TransportReply &reply) {
        if (!reply.ok) {
            const bool isTimeout = (reply.status == 0);
            const bool unknown = (reply.status >= 500);
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit diceFinished(failed(reply.error, {}, isTimeout, unknown));
            return;
        }

        const QJsonObject &json = reply.json;
        const QJsonObject bet = json.value(QStringLiteral("bet")).toObject();
        if (bet.isEmpty()) {
            QString errText = goatedErrorText(json);
            if (errText.isEmpty())
                errText = tr("Wolfbet bet rejected (HTTP %1).").arg(reply.status);
            if (reply.status >= 400 || isRetryable({}, errText)) {
                LiveBetResult r = failed(errText, {}, isRetryable({}, errText), false);
                r.retryDelayMs = fastRetryDelayMs({}, errText);
                emit diceFinished(r);
                return;
            }
            const QString raw = QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)).left(400);
            emit diceFinished(failed(tr("Unexpected Wolfbet bet response (no bet found): %1").arg(raw),
                                     {}, false, true));
            return;
        }
        if (!bet.contains(QStringLiteral("result_value"))) {
            const QString raw = QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)).left(400);
            emit diceFinished(failed(tr("Unexpected Wolfbet bet response (no result found): %1").arg(raw),
                                     {}, false, true));
            return;
        }

        LiveBetResult r;
        r.ok = true;
        r.amount = bet.contains(QStringLiteral("amount")) ? numberOf(bet.value(QStringLiteral("amount")))
                                                          : requestedAmount;
        r.roll = numberOf(bet.value(QStringLiteral("result_value")));

        const QString state = bet.value(QStringLiteral("state")).toString().trimmed().toLower();
        const double profit = numberOf(bet.value(QStringLiteral("profit")));
        // Win only when the payout multiplier is >= 1x: with a stake, net profit >= 0;
        // the server's "state" is only used for a 0 bet (where profit is always 0).
        if (r.amount > 0.0 && bet.contains(QStringLiteral("profit")))
            r.win = BotConstants::isWinMultiplier((r.amount + profit) / r.amount);
        else if (!state.isEmpty())
            r.win = (state == QLatin1String("win"));
        else
            r.win = r.roll >= target;

        if (r.win) {
            const double betMult = numberOf(bet.value(QStringLiteral("multiplier")));
            r.multiplier = betMult > 0.0 ? betMult : target;
            r.payout = r.amount * r.multiplier;
        }
        // The server's net profit is the truth for the payout (stake included), also for a
        // 1x push or a partial payout that the >= 1x rule above classed as a loss.
        if (r.amount > 0.0 && bet.contains(QStringLiteral("profit")))
            r.payout = std::max(0.0, r.amount + profit);

        r.betId = bet.value(QStringLiteral("hash")).toString();
        r.nonce = bet.contains(QStringLiteral("nonce")) ? qint64(numberOf(bet.value(QStringLiteral("nonce")))) : -1;
        emit diceFinished(r);
    });
}

// Goated limbo: POST https://apis.goated.com/casino/limbo/bet
//   {"multiplier": 2, "amount": 0.001, "currency": "USDC", "is_fiat": false}
// Answer: {"success":true,"data":{"id","multiplier_user":2,"multiplier_output":2.21 (the result),
//   "multiplier":2 (paid multiplier),"payout":0.002,"amount":0.001,"nonce":978,...}}
void StakeClient::placeGoatedLimbo(const LimboBetRequest &request)
{
    const double target = std::round(request.multiplierTarget * 100.0) / 100.0;

    QJsonObject body;
    body.insert(QStringLiteral("multiplier"), target);
    body.insert(QStringLiteral("amount"), std::round(request.amount * 1e8) / 1e8);
    body.insert(QStringLiteral("currency"), m_currency.toUpper());
    body.insert(QStringLiteral("is_fiat"), false);

    const double requestedAmount = request.amount;
    m_transport->post(QStringLiteral("casino/limbo/bet"), body,
                      [this, requestedAmount, target](const TransportReply &reply) {
        if (!reply.ok) {
            const bool isTimeout = (reply.status == 0);
            const bool unknown = (reply.status >= 500);
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit diceFinished(failed(reply.error, {}, isTimeout, unknown));
            return;
        }

        const QJsonObject &json = reply.json;
        const QString errText = goatedErrorText(json);
        const bool successFalse = json.contains(QStringLiteral("success"))
                                  && !json.value(QStringLiteral("success")).toBool();
        if (successFalse || reply.status >= 400 || (!errText.isEmpty() && isRetryable({}, errText))) {
            const QString msg = errText.isEmpty()
                ? tr("Goated bet rejected (HTTP %1).").arg(reply.status) : errText;
            LiveBetResult r = failed(msg, {}, isRetryable({}, msg), false);
            r.retryDelayMs = fastRetryDelayMs({}, msg);
            emit diceFinished(r);
            return;
        }

        double result = 0.0;
        if (!goatedNumber(json, {QStringLiteral("multiplier_output"), QStringLiteral("result"),
                                 QStringLiteral("roll_value")}, &result)) {
            const QString raw = QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)).left(400);
            emit diceFinished(failed(errText.isEmpty()
                                         ? tr("Unexpected Goated bet response (no result found): %1").arg(raw)
                                         : errText,
                                     {}, false, true));
            return;
        }

        double amount = requestedAmount;
        goatedNumber(json, {QStringLiteral("amount")}, &amount);
        double payout = 0.0;
        const bool havePayout = goatedNumber(json, {QStringLiteral("payout")}, &payout);

        LiveBetResult r;
        r.ok = true;
        r.amount = amount;
        r.roll = result;
        r.payout = havePayout ? payout : 0.0;
        // The server's payout is the truth; for a 0 bet fall back to comparing the result.
        r.win = (amount > 0.0 && havePayout) ? BotConstants::isWinMultiplier(payout / amount)
                                             : result >= target;
        if (r.win) {
            double mult = 0.0;
            r.multiplier = (amount > 0.0 && payout > 0.0) ? payout / amount
                           : goatedNumber(json, {QStringLiteral("multiplier"),
                                                 QStringLiteral("multiplier_user")}, &mult) && mult > 0.0
                                 ? mult : target;
            if (!havePayout)
                r.payout = amount * r.multiplier;
        }

        QJsonValue idv;
        if (findGoatedValue(json, {QStringLiteral("id"), QStringLiteral("bet_id")}, &idv))
            r.betId = idv.isString() ? idv.toString() : QString::number(qint64(idv.toDouble()));
        double nonce = -1;
        if (goatedNumber(json, {QStringLiteral("nonce")}, &nonce))
            r.nonce = qint64(nonce);
        emit diceFinished(r);
    });
}

// Goated: POST https://apis.goated.com/casino/provably-fair/rotate-seed {"client_seed": "..."}
// Answer: {"success":true,"data":{"server_seed_hash","next_server_seed_hash","client_seed","nonce":0,...}}
void StakeClient::rotateGoatedSeed(const QString &clientSeed)
{
    QString why;
    if (!m_transport || !m_transport->isReady(&why)) {
        const QString msg = m_transport ? why : tr("No transport configured.");
        QTimer::singleShot(0, this, [this, msg] { emit seedRotateFailed(msg); });
        return;
    }

    QJsonObject body;
    body.insert(QStringLiteral("client_seed"), clientSeed);

    m_transport->post(QStringLiteral("casino/provably-fair/rotate-seed"), body,
                      [this, clientSeed](const TransportReply &reply) {
        if (!reply.ok) {
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit seedRotateFailed(reply.error);
            return;
        }
        const QJsonObject &json = reply.json;
        const bool successFalse = json.contains(QStringLiteral("success"))
                                  && !json.value(QStringLiteral("success")).toBool();
        if (successFalse || reply.status >= 400) {
            const QString msg = goatedErrorText(json);
            emit seedRotateFailed(msg.isEmpty() ? tr("Goated seed rotation rejected (HTTP %1).").arg(reply.status)
                                                : msg);
            return;
        }
        const QJsonObject data = json.value(QStringLiteral("data")).toObject();
        if (data.isEmpty()) {
            emit seedRotateFailed(tr("Unexpected seed rotation response."));
            return;
        }
        const QString active = data.value(QStringLiteral("client_seed")).toString();
        emit seedRotated(active.isEmpty() ? clientSeed : active,
                         qint64(numberOf(data.value(QStringLiteral("nonce")))),
                         data.value(QStringLiteral("server_seed_hash")).toString(),
                         data.value(QStringLiteral("next_server_seed_hash")).toString());
    });
}

// Goated: POST https://apis.goated.com/user/vault/transfer/in {"currency": "USDC", "amount": 0.001}
// Answer: {"success":true,"data":{"vault_balance":{...},"wallet_balance":{...}}} - the amount is
// not echoed, so the requested one is reported once the server accepted the transfer.
void StakeClient::goatedVaultDeposit(double amount)
{
    QString why;
    if (!m_transport || !m_transport->isReady(&why)) {
        const QString msg = m_transport ? why : tr("No transport configured.");
        QTimer::singleShot(0, this, [this, msg] { emit vaultFailed(msg); });
        return;
    }
    if (!(amount > 0.0)) {
        QTimer::singleShot(0, this, [this] { emit vaultFailed(tr("Please specify vault amount.")); });
        return;
    }
    if (m_currency.isEmpty()) {
        QTimer::singleShot(0, this, [this] {
            emit vaultFailed(tr("No currency selected — log in and pick one in Options."));
        });
        return;
    }

    const QString currency = m_currency.toUpper();
    const double rounded = std::round(amount * 1e8) / 1e8;
    QJsonObject body;
    body.insert(QStringLiteral("currency"), currency);
    body.insert(QStringLiteral("amount"), rounded);

    m_transport->post(QStringLiteral("user/vault/transfer/in"), body,
                      [this, currency, rounded](const TransportReply &reply) {
        if (!reply.ok) {
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit vaultFailed(reply.error);
            return;
        }
        const QJsonObject &json = reply.json;
        const bool successFalse = json.contains(QStringLiteral("success"))
                                  && !json.value(QStringLiteral("success")).toBool();
        if (successFalse || reply.status >= 400) {
            const QString msg = goatedErrorText(json);
            emit vaultFailed(msg.isEmpty() ? tr("Goated vault transfer rejected (HTTP %1).").arg(reply.status)
                                           : msg);
            return;
        }
        if (!json.value(QStringLiteral("data")).isObject()) {
            emit vaultFailed(tr("Unexpected vault response."));
            return;
        }
        emit vaultDeposited(rounded, currency);
    });
}

// Shuffle limbo: the LimboPlay GraphQL mutation (same query the JavaScript bot uses).
// amount is a string with 8 decimals, bet the multiplier target with 2 decimals.
void StakeClient::placeShuffleLimbo(const LimboBetRequest &request)
{
    const double target = std::round(request.multiplierTarget * 100.0) / 100.0;

    QJsonObject data;
    data.insert(QStringLiteral("amount"), QString::number(request.amount, 'f', 8));
    data.insert(QStringLiteral("bet"), QString::number(target, 'f', 2));
    data.insert(QStringLiteral("currency"), ShuffleTransport::apiCurrency(m_currency));
    data.insert(QStringLiteral("windowId"), randomClientSeed(10));

    QJsonObject variables;
    variables.insert(QStringLiteral("data"), data);

    QJsonObject body;
    body.insert(QStringLiteral("operationName"), QStringLiteral("LimboPlay"));
    body.insert(QStringLiteral("variables"), variables);
    body.insert(QStringLiteral("query"), QStringLiteral(
        "mutation LimboPlay($data: LimboPlayInput!) {\n"
        "  limboPlay(data: $data) {\n"
        "    id\n"
        "    currency\n"
        "    amount\n"
        "    payout\n"
        "    afterBalance\n"
        "    shuffleOriginalActions {\n"
        "      id\n"
        "      action {\n"
        "        limbo {\n"
        "          resultRaw\n"
        "          resultValue\n"
        "          userValue\n"
        "          __typename\n"
        "        }\n"
        "        __typename\n"
        "      }\n"
        "      __typename\n"
        "    }\n"
        "    __typename\n"
        "  }\n"
        "}"));

    const double requestedAmount = request.amount;
    m_transport->post(QStringLiteral("main-api/graphql/api/graphql"), body,
                      [this, requestedAmount](const TransportReply &reply) {
        if (!reply.ok) {
            const bool isTimeout = (reply.status == 0);
            const bool unknown = (reply.status >= 500);
            if (reply.status == 0)
                emit sessionLost(reply.error);
            emit diceFinished(failed(reply.error, {}, isTimeout, unknown));
            return;
        }

        QString type;
        const QString apiError = firstErrorText(reply.json, &type);
        if (!apiError.isEmpty()) {
            LiveBetResult r = failed(apiError, type, isRetryable(type, apiError), false);
            r.retryDelayMs = fastRetryDelayMs(type, apiError);
            emit diceFinished(r);
            return;
        }

        const QJsonObject play = reply.json.value(QStringLiteral("data")).toObject()
                                     .value(QStringLiteral("limboPlay")).toObject();
        const QJsonArray actions = play.value(QStringLiteral("shuffleOriginalActions")).toArray();
        const QJsonObject limbo = actions.isEmpty()
            ? QJsonObject()
            : actions.first().toObject().value(QStringLiteral("action")).toObject()
                  .value(QStringLiteral("limbo")).toObject();
        if (play.isEmpty() || limbo.isEmpty()) {
            emit diceFinished(failed(tr("Unexpected bet response (no limboPlay result)."), {}, true, false));
            return;
        }

        LiveBetResult r;
        r.ok = true;
        r.amount = play.contains(QStringLiteral("amount")) ? numberOf(play.value(QStringLiteral("amount")))
                                                           : requestedAmount;
        r.payout = numberOf(play.value(QStringLiteral("payout")));
        r.roll = numberOf(limbo.value(QStringLiteral("resultValue")));
        r.betId = play.value(QStringLiteral("id")).toString();
        r.nonce = -1; // the mutation does not return the seed nonce

        const double userValue = numberOf(limbo.value(QStringLiteral("userValue")));

        // The server's payout is the truth; for a 0 bet fall back to comparing the result.
        r.win = r.amount > 0.0 ? BotConstants::isWinMultiplier(r.payout / r.amount)
                               : r.roll >= userValue;
        if (r.win) {
            r.multiplier = (r.amount > 0.0 && r.payout > 0.0)
                               ? r.payout / r.amount
                               : userValue;
        }
        emit diceFinished(r);
    });
}
