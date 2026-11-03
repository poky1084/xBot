#include "bot/StakeGames.h"

#include <QJSEngine>
#include <QJSValue>
#include <QJsonDocument>
#include <QMap>
#include <QPair>
#include <QRandomGenerator>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace
{
using StakeGames::ParamSpec;
using StakeGames::ParamType;

// The reference bot's video poker helpers (getOptimalHold, getTargetedHold, checkTargetHit,
// evaluateHand), unchanged apart from comments. Kept as one literal below MSVC's 16 KB limit.
const char kPokerHelpersJs[] =
R"JS((function (g) {
const RANK_ORDER    = ["2","3","4","5","6","7","8","9","10","J","Q","K","A"];
const HIGH_RANKS    = new Set(["J","Q","K","A"]);
const ROYAL_RANKS   = new Set(["10","J","Q","K","A"]);
const rankVal  = (r) => RANK_ORDER.indexOf(r);
const cardStr  = (c) => `${c.rank}${c.suit}`;
const handStr  = (h) => h.map(cardStr).join("  ");
function byRank(hand) {
  return hand.reduce((a, c) => { (a[c.rank] = a[c.rank] || []).push(c); return a; }, {});
}
function bySuit(hand) {
  return hand.reduce((a, c) => { (a[c.suit] = a[c.suit] || []).push(c); return a; }, {});
}
function rankDesc(rg) {
  return Object.entries(rg).sort((a, b) => b[1].length - a[1].length);
}
function sortedByRank(cards) {
  return [...cards].sort((a, b) => rankVal(a.rank) - rankVal(b.rank));
}
function countHighCards(cards) {
  return cards.filter(c => HIGH_RANKS.has(c.rank)).length;
}
function isFlushHand(hand) {
  return Object.keys(bySuit(hand)).length === 1;
}
function rankVals(cards) {
  return sortedByRank(cards).map(c => rankVal(c.rank));
}
function isStraightVals(vals) {
  if (new Set(vals).size !== vals.length) return false;
  return vals[vals.length-1] - vals[0] === vals.length - 1;
}
function isStraightHand(hand) {
  const vals = rankVals(hand);
  if (isStraightVals(vals)) return true;
  if (vals[vals.length-1] === 12) {
    const low = [vals[vals.length-1], ...vals.slice(0,-1)].map(v => v === 12 ? -1 : v).sort((a,b)=>a-b);
    return isStraightVals(low.map((v,i,a) => i===0? 0: low[i]-low[0])) ;
  }
  return false;
}
function isAceLowStraight(hand) {
  const v = rankVals(hand);
  return JSON.stringify(v) === JSON.stringify([0,1,2,3,12]);
}
function royalDraw(hand, n) {
  const sg = bySuit(hand);
  for (const cards of Object.values(sg)) {
    const royal = cards.filter(c => ROYAL_RANKS.has(c.rank));
    if (royal.length === n) return royal;
    if (royal.length > n)  return sortedByRank(royal).slice(-n);
  }
  return null;
}
function bestRoyalDraw(hand) {
  for (let n = 5; n >= 2; n--) {
    const r = royalDraw(hand, n);
    if (r) return { n, cards: r };
  }
  return null;
}
function straightFlushDraw(hand, minN, maxGap = 0) {
  const sg = bySuit(hand);
  let best = null;
  for (const cards of Object.values(sg)) {
    if (cards.length < minN) continue;
    const s = sortedByRank(cards);
    const combos = getCombinations(s, minN);
    for (const combo of combos) {
      const vals = combo.map(c => rankVal(c.rank));
      const span = vals[vals.length-1] - vals[0];
      const gaps = span - (combo.length - 1);
      if (gaps <= maxGap && span <= 4) {
        if (!best || combo.length > best.length) best = combo;
      }
    }
  }
  return best;
}
function getCombinations(arr, k) {
  if (k === 0) return [[]];
  if (arr.length < k) return [];
  const [first, ...rest] = arr;
  const withFirst    = getCombinations(rest, k - 1).map(c => [first, ...c]);
  const withoutFirst = getCombinations(rest, k);
  return [...withFirst, ...withoutFirst];
}
function straightDraw4(hand) {
  const uniq = Object.values(byRank(hand)).map(g => g[0]);
  const s    = sortedByRank(uniq);
  const combos = getCombinations(s, 4);
  let outside = null, inside = null;
  for (const combo of combos) {
    const vals = combo.map(c => rankVal(c.rank));
    const span = vals[vals.length-1] - vals[0];
    if (span === 3 && new Set(vals).size === 4) {
      const canLow  = vals[0] > 0;
      const canHigh = vals[3] < 12;
      if (canLow && canHigh) outside = outside || combo;
      else                   inside  = inside  || combo;
    } else if (span === 4 && new Set(vals).size === 4) {
      inside = inside || combo;
    }
  }
  return { outside, inside };
}
function straightFlush3(hand) {
  const pure  = straightFlushDraw(hand, 3, 0);
  const gap1  = straightFlushDraw(hand, 3, 1);
  const gap2  = straightFlushDraw(hand, 3, 2);
  return { pure, gap1, gap2 };
}
function evaluateHand(hand) {
  const rg     = byRank(hand);
  const rd     = rankDesc(rg);
  const counts = rd.map(([,c]) => c.length);
  const flush  = isFlushHand(hand);
  const vals   = rankVals(hand);
  const seq    = isStraightVals(vals);
  const aceLow = isAceLowStraight(hand);
  const str    = seq || aceLow;
  if (flush && str) return Math.max(...vals) === 12 && !aceLow ? "royalFlush" : "straightFlush";
  if (counts[0] === 4)              return "fourOfAKind";
  if (counts[0] === 3 && counts[1] === 2) return "fullHouse";
  if (flush)                        return "flush";
  if (str)                          return "straight";
  if (counts[0] === 3)              return "threeOfAKind";
  if (counts[0] === 2 && counts[1] === 2) return "twoPair";
  if (counts[0] === 2 && HIGH_RANKS.has(rd[0][0])) return "onePair";
  if (counts[0] === 2)              return "lowPair";
  return "highCard";
}
function getOptimalHold(hand) {
  const rg     = byRank(hand);
  const rd     = rankDesc(rg);
  const counts = rd.map(([,c]) => c.length);
  const flush   = isFlushHand(hand);
  const vals    = rankVals(hand);
  const straight = isStraightVals(vals) || isAceLowStraight(hand);
  if (flush && straight && Math.max(...vals) === 12 && !isAceLowStraight(hand))
    return { hold: hand, reason: "Royal Flush - hold all 5" };
  if (flush && straight)
    return { hold: hand, reason: "Straight Flush - hold all 5" };
  if (counts[0] === 4)
    return { hold: rd[0][1], reason: "Four of a Kind - hold quads" };
  const royal4 = royalDraw(hand, 4);
  if (royal4)
    return { hold: royal4, reason: "4 to Royal Flush" };
  if (counts[0] === 3 && counts[1] === 2)
    return { hold: hand, reason: "Full House - hold all 5" };
  if (flush)
    return { hold: hand, reason: "Flush - hold all 5" };
  if (counts[0] === 3)
    return { hold: rd[0][1].slice(0, 3), reason: "Three of a Kind - hold trips" };
  if (straight)
    return { hold: hand, reason: "Straight - hold all 5" };
  const sf4 = straightFlushDraw(hand, 4, 1);
  if (sf4)
    return { hold: sf4, reason: "4 to Straight Flush" };
  if (counts[0] === 2 && counts[1] === 2) {
    const bothPairs = [...rd[0][1], ...rd[1][1]];
    return { hold: bothPairs, reason: "Two Pair - hold both pairs" };
  }
  const highPair = rd.find(([r, c]) => c.length === 2 && HIGH_RANKS.has(r));
  if (highPair)
    return { hold: highPair[1], reason: `High Pair (${highPair[0]}s)` };
  const royal3 = royalDraw(hand, 3);
  if (royal3)
    return { hold: royal3, reason: "3 to Royal Flush" };
  const sg = bySuit(hand);
  const flush4 = Object.values(sg).find(c => c.length === 4);
  if (flush4)
    return { hold: flush4, reason: "4 to Flush" };
  const lowPair = rd.find(([, c]) => c.length === 2);
  if (lowPair)
    return { hold: lowPair[1], reason: `Low Pair (${lowPair[0]}s)` };
  const { outside: str4out, inside: str4in } = straightDraw4(hand);
  if (str4out)
    return { hold: str4out, reason: "4 to Outside Straight" };
  const { pure: sf3pure, gap1: sf3g1 } = straightFlush3(hand);
  if (sf3pure)
    return { hold: sf3pure, reason: "3 to Straight Flush (pure)" };
  const royal2 = royalDraw(hand, 2);
  if (royal2 && countHighCards(royal2) === 2)
    return { hold: royal2, reason: "2 to Royal Flush (2 high cards suited)" };
  if (str4in && countHighCards(str4in) >= 3)
    return { hold: str4in, reason: "4 to Inside Straight (3+ high cards)" };
  if (sf3g1)
    return { hold: sf3g1, reason: "3 to Straight Flush (1 gap)" };
  if (str4in && countHighCards(str4in) >= 2)
    return { hold: str4in, reason: "4 to Inside Straight (2 high cards)" };
  const highCards = hand.filter(c => HIGH_RANKS.has(c.rank))
                        .sort((a, b) => rankVal(b.rank) - rankVal(a.rank));
  if (highCards.length >= 2) {
    const suitedHighPairs = getSuitedPairs(highCards);
    if (suitedHighPairs)
      return { hold: suitedHighPairs, reason: "2 suited high cards" };
    return { hold: highCards.slice(0, 2), reason: "2 high cards (unsuited)" };
  }
  const { gap2: sf3g2 } = straightFlush3(hand);
  if (sf3g2)
    return { hold: sf3g2, reason: "3 to Straight Flush (2 gaps)" };
  if (highCards.length === 1)
    return { hold: [highCards[0]], reason: `Single high card (${highCards[0].rank})` };
  return { hold: [], reason: "No hold - draw all 5" };
}
function getSuitedPairs(highCards) {
  const sg = bySuit(highCards);
  for (const cards of Object.values(sg)) {
    if (cards.length >= 2)
      return sortedByRank(cards).slice(-2);
  }
  return null;
}
const PAYOUT = {
  royalFlush: 800, straightFlush: 60, fourOfAKind: 22,
  fullHouse: 9, flush: 6, straight: 4,
  threeOfAKind: 3, twoPair: 2, onePair: 1,
  lowPair: 0, highCard: 0,
};
function getTargetedHold(hand, targetCombos) {
  const rg = byRank(hand);   // { "K": [{...},{...}], "J": [{...}], ... }
  const scored = targetCombos.map(combo => {
    let score = 0;
    for (const [rank, needed] of Object.entries(combo)) {
      const have = (rg[rank] || []).length;
      score += Math.min(have, needed) * needed;
    }
    return { combo, score };
  });
  scored.sort((a, b) => b.score - a.score);
  const { combo: best, score } = scored[0];
  const ranksByPriority = Object.entries(best)
    .sort((a, b) => b[1] - a[1]);   // [["K",3],["J",2]]
  const hold = [];
  const label = ranksByPriority.map(([r, n]) => `${n}x${r}`).join(" + ");
  for (const [rank, needed] of ranksByPriority) {
    const available = rg[rank] || [];
    hold.push(...available.slice(0, needed));
  }
  const haveDesc = ranksByPriority
    .map(([r, n]) => `${(rg[r]||[]).length}/${n} ${r}`)
    .join(", ");
  let reason;
  if (hold.length === 5) {
    reason = `Target complete: ${label}`;
  } else if (score === 0) {
    const fallback = getOptimalHold(hand);
    return {
      hold: fallback.hold,
      reason: `No target cards (${label}) - EV fallback: ${fallback.reason}`,
      combo: best,
    };
  } else {
    reason = `Targeting ${label} [${haveDesc}] - hold ${hold.length}, draw ${5 - hold.length}`;
  }
  return { hold, reason, combo: best };
}
function checkTargetHit(hand, targetCombos) {
	if (!Array.isArray(targetCombos)) {
		return null;
	}
	if (targetCombos == null) {
		return null;
	}
	if (targetCombos.length == 0) {
		return null;
	}
  const rg = byRank(hand);
  for (const combo of targetCombos) {
    const hit = Object.entries(combo).every(([rank, needed]) =>
      (rg[rank] || []).length >= needed
    );
    if (hit) {
      const label = Object.entries(combo)
        .sort((a, b) => b[1] - a[1])
        .map(([r, n]) => `${n}x${r}`)
        .join(" + ");
      return label;
    }
  }
  return null;
}
    g.getOptimalHold = getOptimalHold;
    g.getTargetedHold = getTargetedHold;
    g.checkTargetHit = checkTargetHit;
    g.evaluateHand = evaluateHand;
})
)JS";

QString randomId(int length = 21)
{
    static const char kChars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    QString s;
    s.reserve(length);
    for (int i = 0; i < length; ++i)
        s.append(QLatin1Char(kChars[QRandomGenerator::global()->bounded(int(sizeof(kChars) - 1))]));
    return s;
}

double floor8(double v) { return std::floor(v * 1e8 + 1e-6) / 1e8; }

double toNumber(const QJsonValue &v, double def = 0.0, bool *ok = nullptr)
{
    if (ok)
        *ok = true;
    if (v.isDouble())
        return v.toDouble();
    if (v.isString()) {
        bool good = false;
        const double d = v.toString().trimmed().toDouble(&good);
        if (good)
            return d;
    }
    if (v.isBool())
        return v.toBool() ? 1.0 : 0.0;
    if (ok)
        *ok = false;
    return def;
}

int toInt(const QJsonValue &v, int def)
{
    bool ok = false;
    const double d = toNumber(v, def, &ok);
    return ok && std::isfinite(d) ? int(std::llround(d)) : def;
}

QString toText(const QJsonValue &v, const QString &def = QString())
{
    if (v.isString())
        return v.toString().trimmed();
    if (v.isDouble())
        return QString::number(v.toDouble());
    return def;
}

// [1,2,3] or "1,2,3" (also from a table whose values came through as strings).
QJsonArray toIntArray(const QJsonValue &v)
{
    QJsonArray out;
    if (v.isArray()) {
        for (const QJsonValue &e : v.toArray()) {
            bool ok = false;
            const double d = toNumber(e, 0.0, &ok);
            if (ok && std::isfinite(d))
                out.append(int(std::llround(d)));
        }
    } else if (v.isString()) {
        const QStringList parts = v.toString().split(QRegularExpression(QStringLiteral("[,;\\s]+")),
                                                     Qt::SkipEmptyParts);
        for (const QString &p : parts) {
            bool ok = false;
            const int n = p.toInt(&ok);
            if (ok)
                out.append(n);
        }
    } else if (v.isDouble()) {
        out.append(int(std::llround(v.toDouble())));
    }
    return out;
}

// 1-based positions as the user types them (1 = the first egg / hole) -> Stake's 0-based ones.
// Order and repeats are kept (one entry per floor / round); anything below 1 is an error.
bool toZeroBased(const QJsonValue &v, const QString &what, QJsonArray *out, QString *error)
{
    *out = QJsonArray(); // QJsonArray has no clear()
    for (const QJsonValue &t : toIntArray(v)) {
        const int n = t.toInt();
        if (n < 1) {
            if (error)
                *error = QStringLiteral("%1 are 1-based (1 = the first), got %2.").arg(what).arg(n);
            return false;
        }
        out->append(n - 1);
    }
    return true;
}

QJsonArray toStringArray(const QJsonValue &v)
{
    QJsonArray out;
    if (v.isArray()) {
        for (const QJsonValue &e : v.toArray()) {
            const QString s = toText(e);
            if (!s.isEmpty())
                out.append(s);
        }
    } else if (v.isString()) {
        const QStringList parts = v.toString().split(QRegularExpression(QStringLiteral("[,;\\s]+")),
                                                     Qt::SkipEmptyParts);
        for (const QString &p : parts)
            out.append(p);
    }
    return out;
}

// {"rank":"A","suit":"H"} from an object or a text such as "A", "AH", "10s".
QJsonObject toCard(const QJsonValue &v)
{
    if (v.isObject()) {
        const QJsonObject o = v.toObject();
        QJsonObject card;
        card.insert(QStringLiteral("rank"), toText(o.value(QStringLiteral("rank")), QStringLiteral("A")).toUpper());
        card.insert(QStringLiteral("suit"), toText(o.value(QStringLiteral("suit")), QStringLiteral("H")).toUpper());
        return card;
    }
    QString s = toText(v, QStringLiteral("A")).toUpper();
    QString suit = QStringLiteral("H");
    if (s.size() >= 2 && QStringLiteral("HDCS").contains(s.back())) {
        suit = s.back();
        s.chop(1);
    }
    if (s == QLatin1String("T"))
        s = QStringLiteral("10");
    QJsonObject card;
    card.insert(QStringLiteral("rank"), s.isEmpty() ? QStringLiteral("A") : s);
    card.insert(QStringLiteral("suit"), suit);
    return card;
}

QJsonObject stateOf(const QJsonObject &bet)
{
    return bet.value(QStringLiteral("state")).toObject();
}

// Any JSON value as short text (strings as they are, objects / arrays compact).
QString jsonText(const QJsonValue &v)
{
    if (v.isString())
        return v.toString();
    if (v.isDouble())
        return QString::number(v.toDouble());
    if (v.isBool())
        return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (v.isObject())
        return QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
    if (v.isArray())
        return QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
    return {};
}

QString num(double v, int decimals = 2)
{
    return QString::number(v, 'f', decimals);
}

// "A,5,K" from [{rank,suit}, ...]
QString ranksOf(const QJsonArray &cards)
{
    QStringList r;
    for (const QJsonValue &c : cards)
        r << c.toObject().value(QStringLiteral("rank")).toString();
    return r.join(QLatin1Char(','));
}

QString joinValues(const QJsonArray &a)
{
    QStringList parts;
    for (const QJsonValue &v : a)
        parts << (v.isDouble() ? QString::number(v.toDouble()) : v.toString());
    return parts.join(QLatin1Char(','));
}

ParamSpec spec(const QString &key, const QString &label, ParamType type, const QJsonValue &def,
               const QStringList &choices = {}, double min = 0.0, double max = 0.0,
               const QString &tip = {}, int decimals = 2)
{
    ParamSpec s;
    s.decimals = decimals;
    s.key = key;
    s.label = label;
    s.type = type;
    s.def = def;
    s.choices = choices;
    s.min = min;
    s.max = max;
    s.tip = tip;
    return s;
}

QJsonArray ints(std::initializer_list<int> values)
{
    QJsonArray a;
    for (int v : values)
        a.append(v);
    return a;
}

QJsonArray strings(std::initializer_list<const char *> values)
{
    QJsonArray a;
    for (const char *v : values)
        a.append(QString::fromLatin1(v));
    return a;
}

const QStringList kEasyToExpert = {QStringLiteral("easy"), QStringLiteral("medium"),
                                   QStringLiteral("hard"), QStringLiteral("expert")};
const QStringList kEasyToMaster = {QStringLiteral("easy"), QStringLiteral("medium"),
                                   QStringLiteral("hard"), QStringLiteral("expert"),
                                   QStringLiteral("master")};
const QStringList kLowToHigh = {QStringLiteral("low"), QStringLiteral("medium"), QStringLiteral("high")};

QJSEngine *pokerEngine()
{
    // Created on first use and kept for the whole run (never destroyed: a QJSEngine must not
    // outlive the application object in a static destructor).
    static QJSEngine *engine = nullptr;
    if (!engine) {
        engine = new QJSEngine;
        QJSValue setup = engine->evaluate(QString::fromLatin1(kPokerHelpersJs), QStringLiteral("poker"));
        if (setup.isCallable())
            setup.call({engine->globalObject()});
    }
    return engine;
}
} // namespace

namespace StakeGames
{
const char *pokerHelpersJs()
{
    return kPokerHelpersJs;
}

QList<Game> gamesForCasino(const QString &casinoKey)
{
    const QString k = casinoKey.trimmed().toLower();
    if (k.isEmpty() || k == QLatin1String("stake"))
        return allGames();
    if (k == QLatin1String("paradice"))
        return {Game::Dice};
    if (k == QLatin1String("shuffle"))
        return {Game::Dice, Game::Limbo, Game::Keno, Game::Mines, Game::Plinko, Game::Wheel, Game::Hilo,
                Game::Blackjack};
    return {Game::Dice, Game::Limbo}; // Goated, Wolfbet, Duel, Pasino, Tower, Winna
}

QStringList unsupportedChoices(const QString &casinoKey, Game game, const QString &key)
{
    if (casinoKey.trimmed().compare(QLatin1String("shuffle"), Qt::CaseInsensitive) == 0
        && game == Game::Plinko && key == QLatin1String("risk"))
        return {QStringLiteral("expert")};
    return {};
}

bool casinoSupports(const QString &casinoKey, Game game)
{
    return gamesForCasino(casinoKey).contains(game);
}

// The Manual / Auto Bet inputs of each game: the same fields, choices, ranges and defaults as
// the C# bot's game panels.
QList<ParamSpec> paramSpecs(Game game)
{
    using T = ParamType;
    const QString baseOnly = QStringLiteral("No extra parameters — base bet only.");
    switch (game) {
    case Game::Keno:
        return {spec(QStringLiteral("kenoTiles"), QStringLiteral("Numbers (1-40, comma-sep):"), T::IntList,
                     ints({1, 2, 3, 4, 5}), {}, 0, 0,
                     QStringLiteral("1 = the first tile (sent to Stake as 0) .. 40 = the last (39)")),
                spec(QStringLiteral("risk"), QStringLiteral("Risk:"), T::Choice, QStringLiteral("low"),
                     {QStringLiteral("low"), QStringLiteral("classic"), QStringLiteral("medium"), QStringLiteral("high")})};
    case Game::Mines:
        return {spec(QStringLiteral("mines"), QStringLiteral("Mines count:"), T::Int, 1, {}, 1, 24),
                spec(QStringLiteral("tiles"), QStringLiteral("Tiles to reveal (1-based, comma-sep):"), T::IntList,
                     ints({1, 2, 3}), {}, 0, 0,
                     QStringLiteral("Grid: 1=top-left, 25=bottom-right (5×5, row by row)"))};
    case Game::Plinko:
        return {spec(QStringLiteral("rows"), QStringLiteral("Rows:"), T::Int, 8, {}, 8, 16),
                spec(QStringLiteral("risk"), QStringLiteral("Risk:"), T::Choice, QStringLiteral("low"),
                     {QStringLiteral("low"), QStringLiteral("medium"), QStringLiteral("high"), QStringLiteral("expert")})};
    case Game::Wheel:
        return {spec(QStringLiteral("segments"), QStringLiteral("Segments:"), T::Choice, QStringLiteral("10"),
                     {QStringLiteral("10"), QStringLiteral("20"), QStringLiteral("30"), QStringLiteral("40"),
                      QStringLiteral("50")}),
                spec(QStringLiteral("risk"), QStringLiteral("Risk:"), T::Choice, QStringLiteral("low"), kLowToHigh)};
    case Game::Hilo:
        return {spec(QStringLiteral("hiloPattern"), QStringLiteral("Pattern (comma-sep):"), T::IntList,
                     ints({5, 5, 5}), {}, 0, 0,
                     QStringLiteral("0=Low  1=High  2=Equal  3=Random\n4=Lowest Odds (best payout)  "
                                    "5=Highest Odds (safest)  7=Skip\nCashes out after the last step.")),
                spec(QStringLiteral("rank"), QStringLiteral("Start card rank:"), T::Choice, QStringLiteral("A"),
                     {QStringLiteral("A"), QStringLiteral("2"), QStringLiteral("3"), QStringLiteral("4"),
                      QStringLiteral("5"), QStringLiteral("6"), QStringLiteral("7"), QStringLiteral("8"),
                      QStringLiteral("9"), QStringLiteral("10"), QStringLiteral("J"), QStringLiteral("Q"),
                      QStringLiteral("K")}),
                [] {
                    ParamSpec suit = spec(QStringLiteral("suit"), QStringLiteral("Suit:"), T::Choice, QStringLiteral("C"),
                                          {QStringLiteral("C"), QStringLiteral("H"), QStringLiteral("D"), QStringLiteral("S")});
                    suit.sameRowAs = QStringLiteral("rank"); // next to the start card rank
                    // Card suit symbols; hearts and diamonds in red.
                    suit.choiceLabels = {QString(QChar(0x2663)) + QStringLiteral(" Clubs"),
                                         QString(QChar(0x2665)) + QStringLiteral(" Hearts"),
                                         QString(QChar(0x2666)) + QStringLiteral(" Diamonds"),
                                         QString(QChar(0x2660)) + QStringLiteral(" Spades")};
                    suit.choiceColors = {QString(), QStringLiteral("#e53935"), QStringLiteral("#e53935"), QString()};
                    return suit;
                }(),
                spec(QStringLiteral("suitMode"), QStringLiteral("Stop on:"), T::Choice, QStringLiteral("None"),
                     {QStringLiteral("None"), QStringLiteral("All same color"), QStringLiteral("All same suit"),
                      QStringLiteral("Pattern")}, 0, 0,
                     QStringLiteral("All same color / suit: cash out early when a card breaks it, and stop "
                                    "when every card of the pattern kept it.\nPattern: stop when the whole "
                                    "pattern was played and cashed out."))};
    case Game::Baccarat:
        return {spec(QStringLiteral("banker"), QStringLiteral("Banker:"), T::Number, 0.0, {}, 0, 999999, {}, 8),
                spec(QStringLiteral("player"), QStringLiteral("Player:"), T::Number, 0.0, {}, 0, 999999, {}, 8),
                spec(QStringLiteral("tie"), QStringLiteral("Tie:"), T::Number, 0.0, {}, 0, 999999, {}, 8)};
    case Game::Roulette: {
        QJsonArray chips;
        chips.append(QJsonObject{{QStringLiteral("value"), QStringLiteral("colorBlack")},
                                 {QStringLiteral("amount"), 0.0001}});
        return {spec(QStringLiteral("chips"), QStringLiteral("Chips JSON:"), T::Json, chips, {}, 0, 0,
                     QStringLiteral("e.g. [{\"value\":\"colorRed\",\"amount\":0.0001},{\"value\":\"number0\",\"amount\":0.0001}]\n"
                                    "values: colorRed/Black, number0-36, parityEven/Odd, range0112, range1324, "
                                    "range2536, range0118, range1936, row1-3"))};
    }
    case Game::Pump:
        return {spec(QStringLiteral("pumps"), QStringLiteral("Pumps:"), T::Int, 1, {}, 1, 20),
                spec(QStringLiteral("difficulty"), QStringLiteral("Difficulty:"), T::Choice, QStringLiteral("easy"), kEasyToExpert)};
    case Game::DragonTower:
        return {spec(QStringLiteral("difficulty"), QStringLiteral("Difficulty:"), T::Choice, QStringLiteral("easy"), kEasyToMaster),
                spec(QStringLiteral("eggTiles"), QStringLiteral("Eggs (1-based, one per floor):"), T::IntList, ints({1}),
                     {}, 0, 0, QStringLiteral("1 = the first egg of the floor (sent to Stake as 0)"))};
    case Game::Bars:
        return {spec(QStringLiteral("difficulty"), QStringLiteral("Difficulty:"), T::Choice, QStringLiteral("easy"), kEasyToExpert),
                spec(QStringLiteral("tiles"), QStringLiteral("Tiles:"), T::IntList, ints({2}))};
    case Game::ScarabSpin:
    case Game::TomeOfLife:
        return {spec(QStringLiteral("lines"), QStringLiteral("Lines:"), T::Int, 1, {}, 1, 20)};
    case Game::Diamonds:
        return {spec(QStringLiteral("colors"), QStringLiteral("Colors:"), T::Colors, QString(), {}, 0, 0,
                     QStringLiteral("Select up to 5 colors in order")),
                spec(QStringLiteral("stopOnPattern"), QStringLiteral("Stop on pattern"), T::Bool, false),
                spec(QStringLiteral("anyOrder"), QStringLiteral("Any order"), T::Bool, false)};
    case Game::Cases:
    case Game::Darts:
    case Game::Tarot:
        return {spec(QStringLiteral("difficulty"), QStringLiteral("Difficulty:"), T::Choice, QStringLiteral("easy"), kEasyToExpert)};
    case Game::Rps:
        return {spec(QStringLiteral("guesses"), QStringLiteral("Guesses (rock,paper,scissors):"), T::StringList,
                     strings({"rock"}))};
    case Game::Flip:
        return {spec(QStringLiteral("guesses"), QStringLiteral("Guesses (heads,tails):"), T::StringList,
                     strings({"heads"}))};
    case Game::Snakes:
        return {spec(QStringLiteral("difficulty"), QStringLiteral("Difficulty:"), T::Choice, QStringLiteral("easy"), kEasyToMaster),
                spec(QStringLiteral("rolls"), QStringLiteral("Rolls:"), T::Int, 1, {}, 1, 20)};
    case Game::Packs:
    case Game::BlueSamurai:
        return {spec(QStringLiteral("note"), baseOnly, T::Note, QJsonValue())};
    case Game::Moles:
        return {spec(QStringLiteral("moles"), QStringLiteral("Moles:"), T::Int, 3, {}, 1, 10),
                spec(QStringLiteral("molePicks"), QStringLiteral("Picks (1-based):"), T::IntList, ints({1}),
                     {}, 0, 0, QStringLiteral("1 = the first hole (sent to Stake as 0)"))};
    case Game::Chicken:
        return {spec(QStringLiteral("difficulty"), QStringLiteral("Difficulty:"), T::Choice, QStringLiteral("easy"), kEasyToExpert),
                spec(QStringLiteral("steps"), QStringLiteral("Steps:"), T::Int, 1, {}, 1, 20)};
    case Game::Drill:
        return {spec(QStringLiteral("target"), QStringLiteral("Target Multi:"), T::Number, 1.01, {}, 1.01, 999),
                spec(QStringLiteral("pick"), QStringLiteral("Pick:"), T::DrillPick, 1, {}, 1, 3,
                     QStringLiteral("Blue = drill 1, yellow = drill 2, green = drill 3"))};
    case Game::Primedice: {
        QList<ParamSpec> specs = {spec(QStringLiteral("condition"), QStringLiteral("Condition:"), T::Choice, QStringLiteral("rollBetweenTwo"),
                     {QStringLiteral("rollBetween"), QStringLiteral("rollOutside"), QStringLiteral("rollBetweenTwo")}),
                spec(QStringLiteral("target1"), QStringLiteral("T1:"), T::Number, 2.0, {}, 0, 100),
                spec(QStringLiteral("target2"), QStringLiteral("T2:"), T::Number, 24.0, {}, 0, 100),
                spec(QStringLiteral("target3"), QStringLiteral("T3:"), T::Number, 34.0, {}, 0, 100),
                spec(QStringLiteral("target4"), QStringLiteral("T4:"), T::Number, 68.0, {}, 0, 100)};
        // T3 / T4 only exist for rollBetweenTwo (rollBetween / rollOutside send null).
        for (ParamSpec &ps : specs)
            if (ps.key == QLatin1String("target3") || ps.key == QLatin1String("target4")) {
                ps.showWhenKey = QStringLiteral("condition");
                ps.showWhenValues = {QStringLiteral("rollBetweenTwo")};
            }
        return specs;
    }
    case Game::VideoPoker:
        return {spec(QStringLiteral("strategy"), QStringLiteral("Hold strategy:"), T::Choice, QStringLiteral("optimal"),
                     {QStringLiteral("optimal")}, 0, 0,
                     QStringLiteral("optimal: 9/6 Jacks-or-Better expected-value table"))};
    case Game::Blackjack:
        return {spec(QStringLiteral("note"), QStringLiteral("Uses base bet only. Actions handled by bot logic."),
                     T::Note, QJsonValue())};
    case Game::Dice:
    case Game::Limbo:
        return {};
    }
    return {};
}

QJsonObject defaultParams(Game game)
{
    QJsonObject o;
    for (const ParamSpec &s : paramSpecs(game))
        if (s.type != ParamType::Note)
            o.insert(s.key, s.def);
    return o;
}

// The reference bot's README variables (Lua / JavaScript strategies).
QStringList scriptVariables(Game game)
{
    auto L = [](std::initializer_list<const char *> names) {
        QStringList out;
        for (const char *n : names)
            out << QString::fromLatin1(n);
        return out;
    };
    switch (game) {
    case Game::Keno:        return L({"numbers", "risk"});
    case Game::Mines:       return L({"fields", "mines"});
    case Game::Plinko:      return L({"rows", "risk"});
    case Game::Wheel:       return L({"risk", "segments"});
    case Game::Hilo:        return L({"startcard"}); // moves come from round()
    case Game::Baccarat:    return L({"side"}); // amounts come from nextbet = {player=, banker=, tie=}
    case Game::Roulette:    return L({"chips"});
    case Game::DragonTower: return L({"difficulty", "eggs"});
    case Game::Flip:
    case Game::Rps:         return L({"guesses"});
    case Game::Pump:        return L({"difficulty", "pumps"});
    case Game::Snakes:      return L({"difficulty", "rolls"});
    case Game::Cases:
    case Game::Darts:
    case Game::Tarot:       return L({"difficulty"});
    case Game::VideoPoker:  return L({"target"});
    case Game::ScarabSpin:
    case Game::TomeOfLife:  return L({"lines"});
    case Game::Bars:        return L({"difficulty", "tiles"});
    case Game::Chicken:     return L({"difficulty", "steps"});
    case Game::Drill:       return L({"target", "pick"});
    case Game::Moles:       return L({"moles", "picks"});
    case Game::Primedice:   return L({"condition", "target1", "target2", "target3", "target4"});
    default:                return {};
    }
}

QStringList missingScriptVariables(Game game, const QJsonObject &params, const QJsonValue &nextbet,
                                   bool hasRound)
{
    QStringList missing;
    auto need = [&](const char *name) {
        const QString n = QString::fromLatin1(name);
        if (!params.contains(n))
            missing << n;
    };
    switch (game) {
    case Game::Hilo:
        need("startcard");
        if (!hasRound)
            missing << QStringLiteral("round()");
        break;
    case Game::Blackjack:
        if (!hasRound)
            missing << QStringLiteral("round()");
        break;
    case Game::Baccarat:
        // nextbet = {player, banker, tie} needs nothing else; a plain number needs `side`.
        if (!nextbet.isObject())
            need("side");
        break;
    case Game::VideoPoker:
        break; // target nil / null = the optimal hold
    case Game::Primedice:
        need("condition");
        need("target1");
        need("target2");
        if (params.value(QStringLiteral("condition")).toString() == QLatin1String("rollBetweenTwo")) {
            need("target3");
            need("target4");
        }
        break;
    default:
        for (const QString &n : scriptVariables(game))
            if (!params.contains(n))
                missing << n;
        break;
    }
    return missing;
}

double fixedStake(Game game, const QJsonObject &params, bool *fixed)
{
    if (fixed)
        *fixed = false;
    if (game == Game::Baccarat && (params.contains(QStringLiteral("player")) || params.contains(QStringLiteral("banker"))
                                   || params.contains(QStringLiteral("tie")))) {
        if (fixed)
            *fixed = true;
        return floor8(floor8(toNumber(params.value(QStringLiteral("player"))))
                      + floor8(toNumber(params.value(QStringLiteral("banker"))))
                      + floor8(toNumber(params.value(QStringLiteral("tie")))));
    }
    if (game == Game::Roulette) {
        const QJsonArray chips = params.value(QStringLiteral("chips")).toArray();
        bool allObjects = !chips.isEmpty();
        double total = 0.0;
        for (const QJsonValue &c : chips) {
            if (!c.isObject()) {
                allObjects = false;
                break;
            }
            total += floor8(toNumber(c.toObject().value(QStringLiteral("amount"))));
        }
        if (allObjects) {
            if (fixed)
                *fixed = true;
            return floor8(total);
        }
    }
    return 0.0;
}

bool finishScriptParams(Game game, const QJsonValue &nextbet, QJsonObject &params,
                        double &totalBet, QString &error)
{
    const QString name = gameName(game);
    if (game == Game::Baccarat) {
        if (nextbet.isObject()) {
            const QJsonObject o = nextbet.toObject();
            double total = 0.0;
            for (const char *side : {"player", "banker", "tie"}) {
                const double v = floor8(toNumber(o.value(QLatin1String(side)), 0.0));
                if (!std::isfinite(v) || v < 0.0) {
                    error = QStringLiteral("nextbet.%1 must be a number >= 0 (baccarat)").arg(QLatin1String(side));
                    return false;
                }
                params.insert(QLatin1String(side), v);
                total += v;
            }
            totalBet = floor8(total);
            return true;
        }
        bool ok = false;
        const double v = toNumber(nextbet, 0.0, &ok);
        if (!ok || !std::isfinite(v) || v < 0.0) {
            error = QStringLiteral("nextbet must be {player = .., banker = .., tie = ..} or a number (baccarat)");
            return false;
        }
        if (!params.contains(QStringLiteral("side")))
            params.insert(QStringLiteral("side"), QStringLiteral("player"));
        totalBet = floor8(v);
        return true;
    }

    if (game == Game::Roulette) {
        const QJsonArray chips = params.value(QStringLiteral("chips")).toArray();
        if (chips.isEmpty()) {
            error = QStringLiteral("chips must be a list such as {{value = \"colorRed\", amount = 0.0001}} (roulette)");
            return false;
        }
        double total = 0.0;
        bool allObjects = true;
        for (const QJsonValue &c : chips) {
            if (!c.isObject()) {
                allObjects = false;
                break;
            }
            const QJsonObject o = c.toObject();
            const double a = toNumber(o.value(QStringLiteral("amount")), -1.0);
            if (o.value(QStringLiteral("value")).toString().isEmpty() || !std::isfinite(a) || a < 0.0) {
                error = QStringLiteral("every chip needs a value (\"colorRed\", \"number0\", ...) and an amount >= 0 (roulette)");
                return false;
            }
            total += floor8(a);
        }
        if (allObjects) {
            totalBet = floor8(total);
            return true;
        }
        // A list of chip names: nextbet is split between them.
        bool ok = false;
        const double v = toNumber(nextbet, 0.0, &ok);
        if (!ok || !std::isfinite(v) || v < 0.0) {
            error = QStringLiteral("chips without amounts need nextbet (the total, split between them) (roulette)");
            return false;
        }
        totalBet = floor8(v);
        return true;
    }

    bool ok = false;
    const double v = toNumber(nextbet, 0.0, &ok);
    if (!ok || nextbet.isString()) {
        error = QStringLiteral("nextbet must be set to a number by the script (%1)").arg(name);
        return false;
    }
    if (!std::isfinite(v) || v < 0.0) {
        error = QStringLiteral("nextbet must be a finite number >= 0 (got %1)").arg(v);
        return false;
    }
    totalBet = floor8(v);
    return true;
}

bool buildBetRequest(Game game, double amount, const QString &currency, const QJsonObject &params,
                     QString *endpoint, QJsonObject *body, QString *error)
{
    const QJsonObject p = params.isEmpty() ? defaultParams(game) : params;
    auto val = [&p, game](const char *key) {
        const QJsonValue v = p.value(QLatin1String(key));
        if (v.isUndefined() || v.isNull())
            return defaultParams(game).value(QLatin1String(key));
        return v;
    };
    auto fail = [error](const QString &msg) {
        if (error)
            *error = msg;
        return false;
    };

    QJsonObject b;
    b.insert(QStringLiteral("currency"), currency);
    b.insert(QStringLiteral("amount"), amount);
    b.insert(QStringLiteral("identifier"), randomId());
    QString ep;

    switch (game) {
    case Game::Keno: {
        // 1-based everywhere: Manual / Auto Bet tab "kenoTiles" and scripts' "numbers" are
        // 1 .. 40, sent to Stake as 0 .. 39.
        QJsonArray numbers;
        const QJsonValue picked = p.contains(QStringLiteral("kenoTiles")) ? p.value(QStringLiteral("kenoTiles"))
                                                                         : val("numbers");
        for (const QJsonValue &t : toIntArray(picked))
            if (t.toInt() >= 1 && t.toInt() <= 40 && !numbers.contains(t.toInt() - 1))
                numbers.append(t.toInt() - 1);
        if (numbers.isEmpty() || numbers.size() > 10)
            return fail(QStringLiteral("Keno: pick 1 to 10 numbers from 1 to 40."));
        b.insert(QStringLiteral("risk"), toText(val("risk"), QStringLiteral("low")));
        b.insert(QStringLiteral("numbers"), numbers);
        ep = QStringLiteral("_api/casino/keno/bet");
        break;
    }
    case Game::Mines: {
        // 1-based everywhere: Manual / Auto Bet tab "tiles" and scripts' "fields" are 1 (top-left)
        // .. 25 (bottom-right), sent to Stake as 0 .. 24.
        QJsonArray fields;
        const QJsonValue tiles = p.contains(QStringLiteral("tiles")) ? p.value(QStringLiteral("tiles"))
                                                                    : val("fields");
        for (const QJsonValue &t : toIntArray(tiles))
            if (t.toInt() >= 1 && t.toInt() <= 25 && !fields.contains(t.toInt() - 1))
                fields.append(t.toInt() - 1);
        if (fields.isEmpty())
            return fail(QStringLiteral("Mines: pick the tiles to reveal from 1 to 25."));
        b.insert(QStringLiteral("minesCount"), toInt(val("mines"), 3));
        b.insert(QStringLiteral("fields"), fields);
        ep = QStringLiteral("_api/casino/mines/bet");
        break;
    }
    case Game::Plinko: {
        // GraphQL mutation, exactly as the reference bot sends it.
        QJsonObject vars;
        vars.insert(QStringLiteral("currency"), currency);
        vars.insert(QStringLiteral("amount"), amount);
        vars.insert(QStringLiteral("rows"), toInt(val("rows"), 8));
        vars.insert(QStringLiteral("risk"), toText(val("risk"), QStringLiteral("low")));
        vars.insert(QStringLiteral("identifier"), randomId());
        b = QJsonObject();
        b.insert(QStringLiteral("query"), QStringLiteral(
            "mutation PlinkoBet($amount: Float!, $currency: CurrencyEnum!, $risk: CasinoGamePlinkoRiskEnum!, "
            "$rows: Int!, $identifier: String!) {\n  plinkoBet(\n    amount: $amount\n    currency: $currency\n"
            "    risk: $risk\n    rows: $rows\n    identifier: $identifier\n  ) {\n    ...CasinoBet\n    state {\n"
            "      ...CasinoGamePlinko\n    }\n  }\n}\n\nfragment CasinoGamePlinko on CasinoGamePlinko {\n  risk\n"
            "  rows\n  point\n  path\n}\n\nfragment CasinoBet on CasinoBet {\n  id\n  active\n  payoutMultiplier\n"
            "  amountMultiplier\n  amount\n  payout\n  updatedAt\n  currency\n  game\n  user {\n    id\n    name\n"
            "  }\n}"));
        b.insert(QStringLiteral("variables"), vars);
        ep = QStringLiteral("_api/graphql");
        break;
    }
    case Game::Wheel:
        b.insert(QStringLiteral("risk"), toText(val("risk"), QStringLiteral("low")));
        b.insert(QStringLiteral("segments"), toInt(val("segments"), 10));
        ep = QStringLiteral("_api/casino/wheel/spin");
        break;
    case Game::Hilo:
        if (p.contains(QStringLiteral("hiloPattern"))) {
            // Manual / Auto Bet tab: rank + suit pickers and the C# bot's pattern.
            if (toIntArray(p.value(QStringLiteral("hiloPattern"))).isEmpty())
                return fail(QStringLiteral("Hilo pattern is empty."));
            QJsonObject card;
            card.insert(QStringLiteral("rank"), toText(p.value(QStringLiteral("rank")), QStringLiteral("A")).toUpper());
            card.insert(QStringLiteral("suit"), toText(p.value(QStringLiteral("suit")), QStringLiteral("C")).toUpper());
            b.insert(QStringLiteral("startCard"), card);
        } else {
            b.insert(QStringLiteral("startCard"), toCard(val("startcard")));
        }
        ep = QStringLiteral("_api/casino/hilo/bet");
        break;
    case Game::Blackjack:
        ep = QStringLiteral("_api/casino/blackjack/bet");
        break;
    case Game::Baccarat: {
        b.remove(QStringLiteral("amount"));
        double player = 0.0, banker = 0.0, tie = 0.0;
        if (p.contains(QStringLiteral("player")) || p.contains(QStringLiteral("banker"))
            || p.contains(QStringLiteral("tie"))) {
            // A script's nextbet = {player=, banker=, tie=}
            player = floor8(toNumber(p.value(QStringLiteral("player"))));
            banker = floor8(toNumber(p.value(QStringLiteral("banker"))));
            tie = floor8(toNumber(p.value(QStringLiteral("tie"))));
        } else {
            const QString side = toText(val("side"), QStringLiteral("player")).toLower();
            if (side == QLatin1String("banker"))
                banker = amount;
            else if (side == QLatin1String("tie"))
                tie = amount;
            else
                player = amount;
        }
        b.insert(QStringLiteral("tie"), tie);
        b.insert(QStringLiteral("player"), player);
        b.insert(QStringLiteral("banker"), banker);
        ep = QStringLiteral("_api/casino/baccarat/bet");
        break;
    }
    case Game::Roulette: {
        b.remove(QStringLiteral("amount"));
        const QJsonArray chips = val("chips").isArray() ? val("chips").toArray() : toStringArray(val("chips"));
        if (chips.isEmpty())
            return fail(QStringLiteral("Roulette: no chips placed."));
        int named = 0;
        for (const QJsonValue &c : chips)
            if (!c.isObject())
                ++named;
        const double share = named > 0 ? floor8(amount / named) : 0.0;
        QJsonArray rows, parities, ranges, colors, numbers;
        for (const QJsonValue &c : chips) {
            QJsonObject chip;
            if (c.isObject()) {
                chip.insert(QStringLiteral("value"), c.toObject().value(QStringLiteral("value")).toString());
                chip.insert(QStringLiteral("amount"), floor8(toNumber(c.toObject().value(QStringLiteral("amount")))));
            } else {
                chip.insert(QStringLiteral("value"), toText(c));
                chip.insert(QStringLiteral("amount"), share);
            }
            const QString v = chip.value(QStringLiteral("value")).toString();
            if (v.contains(QLatin1String("row")))
                rows.append(chip);
            else if (v.contains(QLatin1String("parity")))
                parities.append(chip);
            else if (v.contains(QLatin1String("range")))
                ranges.append(chip);
            else if (v.contains(QLatin1String("color")))
                colors.append(chip);
            else if (v.contains(QLatin1String("number")))
                numbers.append(chip);
            else
                return fail(QStringLiteral("Roulette: unknown chip \"%1\" (use colorRed, number0, parityEven, "
                                           "range0112, row1, ...).").arg(v));
        }
        b.insert(QStringLiteral("rows"), rows);
        b.insert(QStringLiteral("parities"), parities);
        b.insert(QStringLiteral("ranges"), ranges);
        b.insert(QStringLiteral("colors"), colors);
        b.insert(QStringLiteral("numbers"), numbers);
        ep = QStringLiteral("_api/casino/roulette/bet");
        break;
    }
    case Game::DragonTower: {
        // 1-based everywhere: Manual / Auto Bet tab "eggTiles" and scripts' "eggs" (1 = the first
        // egg of a floor), sent to Stake as 0-based.
        QJsonArray eggs;
        QString posError;
        if (!toZeroBased(p.contains(QStringLiteral("eggTiles")) ? p.value(QStringLiteral("eggTiles")) : val("eggs"),
                         QStringLiteral("Dragon Tower: eggs"), &eggs, &posError))
            return fail(posError);
        if (eggs.isEmpty())
            return fail(QStringLiteral("Dragon Tower: eggs must list the egg picked on each floor."));
        b.insert(QStringLiteral("difficulty"), toText(val("difficulty"), QStringLiteral("easy")));
        b.insert(QStringLiteral("eggs"), eggs);
        ep = QStringLiteral("_api/casino/dragon-tower/bet");
        break;
    }
    case Game::Flip: {
        const QJsonArray guesses = toStringArray(val("guesses"));
        if (guesses.isEmpty())
            return fail(QStringLiteral("Flip: guesses must list heads / tails."));
        QJsonObject vars;
        vars.insert(QStringLiteral("amount"), amount);
        vars.insert(QStringLiteral("currency"), currency);
        vars.insert(QStringLiteral("identifier"), randomId());
        vars.insert(QStringLiteral("guesses"), guesses);
        b = QJsonObject();
        b.insert(QStringLiteral("query"), QStringLiteral(
            "mutation FlipBet($amount: Float!, $currency: CurrencyEnum!, $identifier: String, "
            "$guesses: [FlipConditionEnum!]!) {\n  flipBet(amount: $amount, currency: $currency, "
            "identifier: $identifier, guesses: $guesses) {\n    ...CasinoBet\n    state { ...CasinoGameFlip }\n"
            "  }\n}\nfragment CasinoBet on CasinoBet { id active payoutMultiplier amountMultiplier amount payout "
            "updatedAt currency game user { id name } }\nfragment CasinoGameFlip on CasinoGameFlip { currentRound "
            "payoutMultiplier playedRounds flips }"));
        b.insert(QStringLiteral("variables"), vars);
        ep = QStringLiteral("_api/graphql");
        break;
    }
    case Game::Rps: {
        const QJsonArray guesses = toStringArray(val("guesses"));
        if (guesses.isEmpty())
            return fail(QStringLiteral("Rock Paper Scissors: guesses must list rock / paper / scissors."));
        b.insert(QStringLiteral("guesses"), guesses);
        ep = QStringLiteral("_api/casino/rock-paper-scissors/bet");
        break;
    }
    case Game::Pump:
        b.insert(QStringLiteral("round"), toInt(val("pumps"), 1));
        b.insert(QStringLiteral("difficulty"), toText(val("difficulty"), QStringLiteral("easy")));
        ep = QStringLiteral("_api/casino/pump/bet");
        break;
    case Game::Snakes:
        b.insert(QStringLiteral("difficulty"), toText(val("difficulty"), QStringLiteral("easy")));
        b.insert(QStringLiteral("rollCount"), toInt(val("rolls"), 1));
        ep = QStringLiteral("_api/casino/snakes/bet");
        break;
    case Game::Cases:
        b.insert(QStringLiteral("difficulty"), toText(val("difficulty"), QStringLiteral("easy")));
        ep = QStringLiteral("_api/casino/cases/bet");
        break;
    case Game::Darts:
        b.insert(QStringLiteral("difficulty"), toText(val("difficulty"), QStringLiteral("easy")));
        ep = QStringLiteral("_api/casino/darts/bet");
        break;
    case Game::Diamonds:
        ep = QStringLiteral("_api/casino/diamonds/bet");
        break;
    case Game::VideoPoker:
        b.remove(QStringLiteral("identifier")); // the reference bot sends only currency + amount
        ep = QStringLiteral("_api/casino/video-poker/bet");
        break;
    case Game::BlueSamurai:
        ep = QStringLiteral("_api/casino/slots-samurai/bet");
        break;
    case Game::ScarabSpin:
        b.insert(QStringLiteral("lines"), toInt(val("lines"), 1));
        ep = QStringLiteral("_api/casino/slots/bet");
        break;
    case Game::TomeOfLife:
        b.insert(QStringLiteral("lines"), toInt(val("lines"), 1));
        ep = QStringLiteral("_api/casino/slots-tome-of-life/bet");
        break;
    case Game::Bars: {
        const QJsonArray tiles = toIntArray(val("tiles"));
        if (tiles.isEmpty())
            return fail(QStringLiteral("Bars: tiles must list at least one tile."));
        b.insert(QStringLiteral("difficulty"), toText(val("difficulty"), QStringLiteral("easy")));
        b.insert(QStringLiteral("tiles"), tiles);
        ep = QStringLiteral("_api/casino/bars/bet");
        break;
    }
    case Game::Chicken:
        b.insert(QStringLiteral("difficulty"), toText(val("difficulty"), QStringLiteral("medium")));
        b.insert(QStringLiteral("round"), toInt(val("steps"), 1));
        ep = QStringLiteral("_api/casino/chicken/bet");
        break;
    case Game::Tarot:
        b.insert(QStringLiteral("difficulty"), toText(val("difficulty"), QStringLiteral("medium")));
        ep = QStringLiteral("_api/casino/tarot/bet");
        break;
    case Game::Drill:
        b.insert(QStringLiteral("target"), toNumber(val("target"), 2.0));
        b.insert(QStringLiteral("pick"), std::clamp(toInt(val("pick"), 1), 1, 3)); // 3 drills
        ep = QStringLiteral("_api/casino/drill/bet");
        break;
    case Game::Packs:
        ep = QStringLiteral("_api/casino/packs/bet");
        break;
    case Game::Moles: {
        // 1-based everywhere: Manual / Auto Bet tab "molePicks" and scripts' "picks" (1 = the
        // first hole), sent to Stake as 0-based.
        QJsonArray picks;
        QString posError;
        if (!toZeroBased(p.contains(QStringLiteral("molePicks")) ? p.value(QStringLiteral("molePicks")) : val("picks"),
                         QStringLiteral("Moles: picks"), &picks, &posError))
            return fail(posError);
        if (picks.isEmpty())
            return fail(QStringLiteral("Moles: picks must list at least one pick."));
        b.insert(QStringLiteral("molesCount"), toInt(val("moles"), 6));
        b.insert(QStringLiteral("picks"), picks);
        ep = QStringLiteral("_api/casino/moles/autobet");
        break;
    }
    case Game::Primedice: {
        const QString cond = toText(val("condition"), QStringLiteral("rollOutside"));
        b.insert(QStringLiteral("condition"), cond);
        b.insert(QStringLiteral("target1"), toNumber(val("target1")));
        b.insert(QStringLiteral("target2"), toNumber(val("target2")));
        if (cond == QLatin1String("rollBetween") || cond == QLatin1String("rollOutside")) {
            b.insert(QStringLiteral("target3"), QJsonValue::Null);
            b.insert(QStringLiteral("target4"), QJsonValue::Null);
        } else {
            b.insert(QStringLiteral("target3"), toNumber(val("target3")));
            b.insert(QStringLiteral("target4"), toNumber(val("target4")));
        }
        ep = QStringLiteral("_api/casino/primedice-x/bet");
        break;
    }
    case Game::Dice:
    case Game::Limbo:
        return fail(QStringLiteral("Dice and limbo are placed through placeDice() / placeLimbo()."));
    }

    if (endpoint)
        *endpoint = ep;
    if (body)
        *body = b;
    return true;
}

QString activeBetSlug(Game game)
{
    switch (game) {
    case Game::Hilo:       return QStringLiteral("hilo");
    case Game::Blackjack:  return QStringLiteral("blackjack");
    case Game::VideoPoker: return QStringLiteral("videoPoker");
    case Game::Mines:      return QStringLiteral("mines");
    default:               return {};
    }
}

bool isUnfinished(Game game, const QJsonObject &bet)
{
    if (game == Game::BlueSamurai) {
        const QString next = stateOf(bet).value(QStringLiteral("nextSpinType")).toString();
        if (next == QLatin1String("special") || next == QLatin1String("bonus"))
            return true;
    }
    return bet.value(QStringLiteral("active")).toBool(false);
}

bool usesRoundFunction(Game game)
{
    return game == Game::Hilo || game == Game::Blackjack;
}

namespace
{
// The reference bot's mapping of its HILO_* codes to a guess for the current card.
QString hiloGuess(int code, const QString &rank)
{
    switch (code) {
    case 2: return QStringLiteral("equal");
    case 7: return QStringLiteral("skip");
    case 5:
        if (rank == QLatin1String("A")) return QStringLiteral("higher");
        if (rank == QLatin1String("K")) return QStringLiteral("equal");
        return QStringLiteral("higherEqual");
    case 4:
        if (rank == QLatin1String("A")) return QStringLiteral("equal");
        if (rank == QLatin1String("K")) return QStringLiteral("lower");
        return QStringLiteral("lowerEqual");
    default:
        return {};
    }
}

// The C# bot's pattern codes (Manual / Auto Bet tab): 0 = Low, 1 = High, 2 = Equal, 3 = Random,
// 4 = the side with the highest odds (safest), 5 = the side with the lowest odds (best payout),
// 7 = Skip. Ace / King edge cases as in its HiloGuess().
QString csHiloGuess(int p, const QString &rank)
{
    const QString r = rank.toUpper();
    if (p == 7)
        return QStringLiteral("skip");
    if (p == 1) {
        if (r == QLatin1String("A")) return QStringLiteral("higher");
        if (r == QLatin1String("K")) return QStringLiteral("equal");
        return QStringLiteral("higherEqual");
    }
    if (p == 0) {
        if (r == QLatin1String("K")) return QStringLiteral("lower");
        if (r == QLatin1String("A")) return QStringLiteral("equal");
        return QStringLiteral("lowerEqual");
    }
    if (p == 2)
        return QStringLiteral("equal");
    if (p == 4 && (r == QLatin1String("A") || r == QLatin1String("K")))
        return QStringLiteral("equal");
    if (r == QLatin1String("A"))
        return QStringLiteral("higher");
    if (r == QLatin1String("K"))
        return QStringLiteral("lower");
    static const QStringList highRanks = {QStringLiteral("Q"), QStringLiteral("J"), QStringLiteral("10"),
                                          QStringLiteral("9"), QStringLiteral("8")};
    const bool isHigh = highRanks.contains(r);
    switch (p) {
    case 3: return QRandomGenerator::global()->bounded(2) == 0 ? QStringLiteral("higherEqual")
                                                               : QStringLiteral("lowerEqual");
    case 4: return isHigh ? QStringLiteral("higherEqual") : QStringLiteral("lowerEqual");
    case 5: return isHigh ? QStringLiteral("lowerEqual") : QStringLiteral("higherEqual");
    default: return QStringLiteral("higher");
    }
}

QString suitColor(const QString &suit)
{
    const QString s = suit.toUpper();
    if (s == QLatin1String("H") || s == QLatin1String("D"))
        return QStringLiteral("red");
    if (s == QLatin1String("C") || s == QLatin1String("S"))
        return QStringLiteral("black");
    return {};
}

// "All same suit" / "All same color": true when `suit` breaks the start card's suit / color.
bool hiloSuitBroken(const QString &mode, const QString &startSuit, const QString &suit)
{
    if (startSuit.isEmpty() || suit.isEmpty())
        return false;
    if (mode == QLatin1String("All same suit"))
        return suit.compare(startSuit, Qt::CaseInsensitive) != 0;
    if (mode == QLatin1String("All same color"))
        return !suitColor(startSuit).isEmpty() && suitColor(suit) != suitColor(startSuit);
    return false;
}

QString canonicalHiloGuess(const QString &s)
{
    static const QStringList guesses = {QStringLiteral("higher"), QStringLiteral("lower"),
                                        QStringLiteral("equal"), QStringLiteral("higherEqual"),
                                        QStringLiteral("lowerEqual"), QStringLiteral("skip")};
    for (const QString &g : guesses)
        if (g.compare(s, Qt::CaseInsensitive) == 0)
            return g;
    return {};
}

QString blackjackAction(const QString &raw)
{
    QString s = raw.trimmed();
    if (s.startsWith(QLatin1String("BLACKJACK_"), Qt::CaseInsensitive))
        s = s.mid(10);
    s = s.toLower();
    s.remove(QLatin1Char('_'));
    if (s == QLatin1String("hit") || s == QLatin1String("stand") || s == QLatin1String("double")
        || s == QLatin1String("split") || s == QLatin1String("insurance"))
        return s;
    if (s == QLatin1String("noinsurance"))
        return QStringLiteral("noInsurance");
    return {};
}

bool hasAction(const QJsonObject &hand, const char *action)
{
    for (const QJsonValue &a : hand.value(QStringLiteral("actions")).toArray())
        if (a.toString() == QLatin1String(action))
            return true;
    return false;
}

// No round(): never take insurance, hit below 17, otherwise stand.
QString builtInBlackjack(const QJsonObject &state)
{
    const QJsonArray dealer = state.value(QStringLiteral("dealer")).toArray();
    const QJsonArray hands = state.value(QStringLiteral("player")).toArray();
    QString upCard;
    if (!dealer.isEmpty()) {
        const QJsonArray cards = dealer.first().toObject().value(QStringLiteral("cards")).toArray();
        if (!cards.isEmpty())
            upCard = cards.first().toObject().value(QStringLiteral("rank")).toString();
    }
    if (hands.isEmpty())
        return QStringLiteral("stand");
    const QJsonObject first = hands.first().toObject();
    if (upCard == QLatin1String("A") && !hasAction(first, "insurance") && !hasAction(first, "noInsurance"))
        return QStringLiteral("noInsurance");
    QJsonObject hand = hands.last().toObject();
    for (const QJsonValue &h : hands) {
        const QJsonObject o = h.toObject();
        if (!hasAction(o, "stand") && !hasAction(o, "bust") && !hasAction(o, "full")
            && !hasAction(o, "double") && !hasAction(o, "blackjack")) {
            hand = o;
            break;
        }
    }
    return toNumber(hand.value(QStringLiteral("value")), 21.0) < 17.0 ? QStringLiteral("hit")
                                                                       : QStringLiteral("stand");
}
} // namespace

bool nextAction(Game game, const QJsonObject &bet, const QJsonObject &params,
                const QJsonValue &scriptChoice, int fallbackLevel, Action *out, QString *error)
{
    auto fail = [error](const QString &msg) {
        if (error)
            *error = msg;
        return false;
    };
    const QJsonObject state = stateOf(bet);
    Action a;

    switch (game) {
    case Game::Hilo: {
        const QJsonArray rounds = state.value(QStringLiteral("rounds")).toArray();
        QString rank = rounds.isEmpty() ? QString()
                                        : rounds.last().toObject().value(QStringLiteral("card")).toObject()
                                              .value(QStringLiteral("rank")).toString();
        if (rank.isEmpty())
            rank = state.value(QStringLiteral("startCard")).toObject().value(QStringLiteral("rank")).toString();

        QString guess;
        bool cashout = false;
        if (scriptChoice.isUndefined() && params.contains(QStringLiteral("hiloPattern"))) {
            // Manual / Auto Bet tab (C# bot behaviour): play the pattern, cash out after its
            // last step, or early when a card breaks the "All same suit / color" filter.
            const QJsonArray pattern = toIntArray(params.value(QStringLiteral("hiloPattern")));
            const QString mode = toText(params.value(QStringLiteral("suitMode")), QStringLiteral("None"));
            const QString startSuit = state.value(QStringLiteral("startCard")).toObject()
                                          .value(QStringLiteral("suit")).toString();
            const QString lastSuit = rounds.isEmpty() ? QString()
                : rounds.last().toObject().value(QStringLiteral("card")).toObject()
                      .value(QStringLiteral("suit")).toString();
            if (!rounds.isEmpty() && hiloSuitBroken(mode, startSuit, lastSuit)) {
                cashout = true;
                a.description = QStringLiteral("hilo: %1 broken - cash out").arg(mode);
            } else if (rounds.size() < pattern.size()) {
                guess = csHiloGuess(pattern.at(rounds.size()).toInt(), rank);
            } else {
                cashout = true;
            }
        } else if (scriptChoice.isString()) {
            const QString s = scriptChoice.toString().trimmed();
            if (s.compare(QLatin1String("cashout"), Qt::CaseInsensitive) == 0)
                cashout = true;
            else if ((guess = canonicalHiloGuess(s)).isEmpty())
                return fail(QStringLiteral("round() returned \"%1\" - use HILO_BET_HIGH, HILO_BET_LOW, "
                                           "HILO_BET_EQUAL, HILO_SKIP or HILO_CASHOUT").arg(s));
        } else {
            int code = 3;
            if (scriptChoice.isDouble()) {
                code = int(std::llround(scriptChoice.toDouble()));
            } else {
                const QJsonValue pv = params.value(QStringLiteral("pattern"));
                // No pattern either: the reference bot's README example, high twice then cash out.
                const QJsonArray pattern = pv.isUndefined() || pv.isNull() ? ints({5, 5}) : toIntArray(pv);
                code = rounds.size() < pattern.size() ? pattern.at(rounds.size()).toInt() : 3;
            }
            if (code == 3) {
                cashout = true;
            } else {
                guess = hiloGuess(code, rank);
                if (guess.isEmpty())
                    return fail(QStringLiteral("hilo: unknown guess code %1 (5 = high, 4 = low, 2 = equal, "
                                               "7 = skip, 3 = cash out)").arg(code));
            }
        }
        if (cashout) {
            a.endpoint = QStringLiteral("_api/casino/hilo/cashout");
            a.body.insert(QStringLiteral("identifier"), randomId());
            if (a.description.isEmpty())
                a.description = QStringLiteral("hilo: cash out");
        } else {
            a.endpoint = QStringLiteral("_api/casino/hilo/next");
            a.body.insert(QStringLiteral("guess"), guess);
            a.description = QStringLiteral("hilo: %1 on %2").arg(guess, rank);
        }
        break;
    }
    case Game::Blackjack: {
        QString action;
        if (fallbackLevel >= 2)
            action = QStringLiteral("stand");
        else if (fallbackLevel == 1)
            action = QStringLiteral("noInsurance");
        else if (scriptChoice.isString() && !scriptChoice.toString().trimmed().isEmpty()) {
            action = blackjackAction(scriptChoice.toString());
            if (action.isEmpty())
                return fail(QStringLiteral("round() returned \"%1\" - use BLACKJACK_HIT, BLACKJACK_STAND, "
                                           "BLACKJACK_DOUBLE, BLACKJACK_SPLIT, BLACKJACK_INSURANCE or "
                                           "BLACKJACK_NOINSURANCE").arg(scriptChoice.toString()));
        } else {
            action = builtInBlackjack(state);
        }
        a.endpoint = QStringLiteral("_api/casino/blackjack/next");
        a.body.insert(QStringLiteral("action"), action);
        a.body.insert(QStringLiteral("identifier"), randomId());
        a.description = QStringLiteral("blackjack: %1").arg(action);
        break;
    }
    case Game::VideoPoker: {
        QJsonArray hand = state.value(QStringLiteral("initialHand")).toArray();
        if (hand.isEmpty())
            hand = state.value(QStringLiteral("playerHand")).toArray();
        QString reason;
        const QJsonArray held = hand.isEmpty()
            ? QJsonArray()
            : videoPokerHold(hand, params.value(QStringLiteral("target")), &reason);
        a.endpoint = QStringLiteral("_api/casino/video-poker/next");
        a.body.insert(QStringLiteral("identifier"), randomId());
        a.body.insert(QStringLiteral("held"), held);
        a.description = QStringLiteral("video poker: hold %1 (%2)")
                            .arg(held.isEmpty() ? QStringLiteral("nothing") : ranksOf(held), reason);
        break;
    }
    case Game::BlueSamurai:
        a.endpoint = QStringLiteral("_api/casino/slots-samurai/next");
        a.body.insert(QStringLiteral("identifier"), randomId());
        a.description = QStringLiteral("blue samurai: %1 spin")
                            .arg(state.value(QStringLiteral("nextSpinType")).toString());
        break;
    case Game::Mines:
        a.endpoint = QStringLiteral("_api/casino/mines/cashout");
        a.body.insert(QStringLiteral("identifier"), randomId());
        a.description = QStringLiteral("mines: cash out");
        break;
    default:
        return fail(QStringLiteral("%1: the bet is still open, but this game has no next step.")
                        .arg(gameDisplayName(game)));
    }
    if (out)
        *out = a;
    return true;
}

QJsonValue scriptRoll(Game game, const QJsonObject &bet)
{
    const QJsonObject st = stateOf(bet);
    auto oneBased = [](const QJsonValue &v) {
        QJsonArray out;
        for (const QJsonValue &n : v.toArray())
            out.append(n.toInt() + 1);
        return out;
    };
    switch (game) {
    case Game::VideoPoker:
        return st.value(QStringLiteral("handResult")).toString();
    case Game::Mines:
        return oneBased(st.value(QStringLiteral("mines")));        // 1 (top-left) .. 25
    case Game::Keno:
        return oneBased(st.value(QStringLiteral("drawnNumbers"))); // 1 .. 40
    default:
        return QJsonValue(QJsonValue::Undefined);
    }
}

QJsonValue scriptResult(Game game, const QJsonObject &bet, double roll)
{
    const QJsonValue result = stateOf(bet).value(QStringLiteral("result"));
    if (!result.isUndefined() && !result.isNull())
        return result;
    const QJsonValue special = bet.isEmpty() ? QJsonValue(QJsonValue::Undefined) : scriptRoll(game, bet);
    if (!special.isUndefined())
        return special;
    return roll;
}

Summary summarize(Game game, const QJsonObject &bet, const QJsonObject &params)
{
    Summary s;
    const QJsonObject st = stateOf(bet);
    const double mult = toNumber(bet.value(QStringLiteral("payoutMultiplier")));
    s.roll = mult;
    auto text = [&st](const char *key) { return toText(st.value(QLatin1String(key))); };

    switch (game) {
    case Game::Primedice: {
        s.roll = toNumber(st.value(QStringLiteral("result")));
        QStringList t;
        for (const char *k : {"target1", "target2", "target3", "target4"}) {
            const QJsonValue v = st.value(QLatin1String(k));
            if (!v.isNull() && !v.isUndefined())
                t << QString::number(toNumber(v));
        }
        s.targetText = QStringLiteral("%1 %2").arg(text("condition"), t.join(QLatin1Char('|')));
        s.rollText = num(s.roll);
        break;
    }
    case Game::Flip: {
        const QJsonArray guesses = toStringArray(params.value(QStringLiteral("guesses")));
        const QJsonValue flips = st.value(QStringLiteral("flips"));
        s.targetText = joinValues(guesses);
        if (flips.isArray()) {
            s.rollText = joinValues(flips.toArray());
        } else {
            s.rollText = QStringLiteral("%1 rounds").arg(toInt(st.value(QStringLiteral("playedRounds")), 0));
        }
        break;
    }
    case Game::Plinko:
        s.targetText = QStringLiteral("%1|%2").arg(text("risk"), text("rows"));
        s.rollText = num(mult, 4);
        break;
    case Game::Moles: {
        const QJsonArray rounds = st.value(QStringLiteral("rounds")).toArray();
        int lastHit = -1;
        for (int i = 0; i < rounds.size(); ++i)
            if (rounds.at(i).toObject().value(QStringLiteral("hit")).toBool())
                lastHit = i;
        s.roll = lastHit + 1;
        s.targetText = QStringLiteral("%1|%2").arg(text("molesCount"))
                           .arg(toIntArray(params.contains(QStringLiteral("molePicks"))
                                               ? params.value(QStringLiteral("molePicks"))
                                               : params.value(QStringLiteral("picks"))).size());
        s.rollText = QString::number(lastHit + 1);
        break;
    }
    case Game::Drill: {
        const int pick = toInt(st.value(QStringLiteral("pick")), 1);
        const QJsonArray results = st.value(QStringLiteral("drillResults")).toArray();
        s.targetText = QStringLiteral("%1/%2").arg(pick).arg(num(toNumber(st.value(QStringLiteral("target"))), 2));
        if (pick >= 1 && pick <= results.size()) {
            s.roll = toNumber(results.at(pick - 1).toObject().value(QStringLiteral("multiplier")));
            s.rollText = num(s.roll);
        }
        break;
    }
    case Game::Tarot:
    case Game::Cases:
    case Game::Darts:
        s.targetText = text("difficulty");
        if (game == Game::Cases && !st.value(QStringLiteral("result")).isUndefined()) {
            s.roll = toNumber(st.value(QStringLiteral("result")), mult);
            s.rollText = toText(st.value(QStringLiteral("result")));
        }
        break;
    case Game::Chicken:
        s.targetText = QStringLiteral("%1|%2").arg(text("difficulty"))
                           .arg(toInt(params.value(QStringLiteral("steps")), toInt(st.value(QStringLiteral("round")), 0)));
        if (!st.value(QStringLiteral("_deathPoint")).isUndefined()) {
            s.roll = toNumber(st.value(QStringLiteral("_deathPoint")));
            s.rollText = toText(st.value(QStringLiteral("_deathPoint")));
        }
        break;
    case Game::Bars:
        s.targetText = QStringLiteral("%1|%2").arg(text("difficulty"))
                           .arg(st.value(QStringLiteral("tiles")).toArray().size());
        break;
    case Game::Pump:
        s.targetText = QString::number(toInt(params.value(QStringLiteral("pumps")), toInt(st.value(QStringLiteral("round")), 0)));
        if (!st.value(QStringLiteral("_popPoint")).isUndefined()) {
            s.roll = toNumber(st.value(QStringLiteral("_popPoint")));
            s.rollText = toText(st.value(QStringLiteral("_popPoint")));
        }
        break;
    case Game::Baccarat: {
        QStringList sides;
        if (toNumber(st.value(QStringLiteral("tie"))) > 0) sides << QStringLiteral("t");
        if (toNumber(st.value(QStringLiteral("banker"))) > 0) sides << QStringLiteral("b");
        if (toNumber(st.value(QStringLiteral("player"))) > 0) sides << QStringLiteral("p");
        s.targetText = sides.join(QLatin1Char('|'));
        s.rollText = jsonText(st.value(QStringLiteral("result")));
        break;
    }
    case Game::DragonTower:
        s.targetText = QStringLiteral("%1|%2").arg(text("difficulty"))
                           .arg(toIntArray(params.contains(QStringLiteral("eggTiles"))
                                               ? params.value(QStringLiteral("eggTiles"))
                                               : params.value(QStringLiteral("eggs"))).size());
        s.roll = toNumber(st.value(QStringLiteral("currentRound")), mult);
        s.rollText = QStringLiteral("floor %1").arg(toInt(st.value(QStringLiteral("currentRound")), 0));
        break;
    case Game::Roulette:
        s.roll = toNumber(st.value(QStringLiteral("result")), 0.0);
        s.targetText = QStringLiteral("%1 chips").arg(
            st.value(QStringLiteral("numbers")).toArray().size() + st.value(QStringLiteral("colors")).toArray().size()
            + st.value(QStringLiteral("parities")).toArray().size() + st.value(QStringLiteral("ranges")).toArray().size()
            + st.value(QStringLiteral("rows")).toArray().size());
        s.rollText = toText(st.value(QStringLiteral("result")));
        break;
    case Game::Wheel:
        s.roll = toNumber(st.value(QStringLiteral("result")), mult);
        s.targetText = QStringLiteral("%1|%2").arg(text("risk"), text("segments"));
        s.rollText = toText(st.value(QStringLiteral("result")));
        break;
    case Game::Keno: {
        const QJsonArray picked = st.value(QStringLiteral("selectedNumbers")).toArray();
        const QJsonArray drawn = st.value(QStringLiteral("drawnNumbers")).toArray();
        int hits = 0;
        for (const QJsonValue &n : picked)
            if (drawn.contains(n))
                ++hits;
        s.roll = hits;
        s.targetText = QStringLiteral("%1|%2").arg(text("risk")).arg(picked.size());
        s.rollText = QStringLiteral("%1/%2 hits").arg(hits).arg(picked.size());
        break;
    }
    case Game::Mines: {
        const QJsonArray mines = st.value(QStringLiteral("mines")).toArray();
        const QJsonArray rounds = st.value(QStringLiteral("rounds")).toArray();
        QStringList hit;
        for (const QJsonValue &r : rounds) {
            const QJsonValue f = r.toObject().value(QStringLiteral("field"));
            if (mines.contains(f))
                hit << QString::number(f.toInt() + 1); // shown 1-based like the inputs
        }
        s.targetText = QStringLiteral("%1|%2").arg(toInt(st.value(QStringLiteral("minesCount")), mines.size()))
                           .arg(rounds.size());
        s.rollText = hit.isEmpty() ? QStringLiteral("safe") : QStringLiteral("mine %1").arg(hit.join(QLatin1Char(',')));
        break;
    }
    case Game::Snakes: {
        const QJsonValue rounds = st.value(QStringLiteral("rounds"));
        int good = 0;
        const QJsonArray arr = rounds.isArray() ? rounds.toArray() : QJsonArray();
        if (rounds.isObject()) {
            const QJsonObject o = rounds.toObject();
            for (auto it = o.begin(); it != o.end(); ++it)
                if (toNumber(it.value().toObject().value(QStringLiteral("payoutMultiplier"))) >= 1.0)
                    ++good;
        }
        for (const QJsonValue &r : arr)
            if (toNumber(r.toObject().value(QStringLiteral("payoutMultiplier"))) >= 1.0)
                ++good;
        s.roll = good;
        s.targetText = QStringLiteral("%1|%2").arg(text("difficulty"))
                           .arg(toInt(params.value(QStringLiteral("rolls")), 1));
        s.rollText = QString::number(good);
        break;
    }
    case Game::Rps:
        s.targetText = QString::number(toStringArray(params.value(QStringLiteral("guesses"))).size());
        break;
    case Game::ScarabSpin:
    case Game::TomeOfLife:
        s.targetText = QStringLiteral("%1 lines").arg(toInt(params.value(QStringLiteral("lines")), 1));
        break;
    case Game::BlueSamurai:
    case Game::Diamonds:
    case Game::Packs:
        break;
    case Game::Blackjack: {
        const QJsonArray hands = st.value(QStringLiteral("player")).toArray();
        const QJsonArray dealer = st.value(QStringLiteral("dealer")).toArray();
        QStringList pv;
        for (const QJsonValue &h : hands)
            pv << toText(h.toObject().value(QStringLiteral("value")));
        if (!hands.isEmpty())
            s.targetText = QStringLiteral("P %1").arg(pv.join(QLatin1Char('/')));
        if (!dealer.isEmpty())
            s.rollText = QStringLiteral("D %1").arg(toText(dealer.first().toObject().value(QStringLiteral("value"))));
        break;
    }
    case Game::Hilo: {
        QJsonArray cards;
        cards.append(st.value(QStringLiteral("startCard")));
        for (const QJsonValue &r : st.value(QStringLiteral("rounds")).toArray())
            cards.append(r.toObject().value(QStringLiteral("card")));
        s.targetText = ranksOf(cards);
        break;
    }
    case Game::VideoPoker:
        s.targetText = ranksOf(st.value(QStringLiteral("playerHand")).toArray());
        s.rollText = text("handResult");
        break;
    case Game::Dice:
    case Game::Limbo:
        s.roll = toNumber(st.value(QStringLiteral("result")));
        break;
    }
    if (s.rollText.isEmpty())
        s.rollText = num(mult, 4) + QLatin1Char('x');
    return s;
}

QJsonArray videoPokerHold(const QJsonArray &hand, const QJsonValue &target, QString *reason)
{
    QJSEngine *e = pokerEngine();
    QJSValue fn = e->evaluate(QStringLiteral(
        "(function (hs, ts) {"
        "  var h = JSON.parse(hs), t = JSON.parse(ts);"
        "  var r = (Array.isArray(t) && t.length > 0) ? getTargetedHold(h, t) : getOptimalHold(h);"
        "  return JSON.stringify({hold: r.hold, reason: r.reason});"
        "})"));
    const QString hs = QString::fromUtf8(QJsonDocument(hand).toJson(QJsonDocument::Compact));
    QString ts = QStringLiteral("null");
    if (target.isArray())
        ts = QString::fromUtf8(QJsonDocument(target.toArray()).toJson(QJsonDocument::Compact));
    else if (target.isObject())
        ts = QStringLiteral("[%1]").arg(QString::fromUtf8(QJsonDocument(target.toObject()).toJson(QJsonDocument::Compact)));
    const QJSValue res = fn.isCallable() ? fn.call({QJSValue(hs), QJSValue(ts)}) : fn;
    if (res.isError() || !res.isString()) {
        if (reason)
            *reason = QStringLiteral("strategy error: %1 - drawing all 5").arg(res.toString());
        return {};
    }
    const QJsonObject o = QJsonDocument::fromJson(res.toString().toUtf8()).object();
    if (reason)
        *reason = o.value(QStringLiteral("reason")).toString();
    QJsonArray held;
    for (const QJsonValue &c : o.value(QStringLiteral("hold")).toArray()) {
        // Send the cards exactly as the casino dealt them ({rank, suit}).
        const QJsonObject card = c.toObject();
        QJsonObject clean;
        clean.insert(QStringLiteral("rank"), card.value(QStringLiteral("rank")));
        clean.insert(QStringLiteral("suit"), card.value(QStringLiteral("suit")));
        held.append(clean);
    }
    return held;
}

namespace
{
// Diamonds result colors: state.hand / state.result / state.diamonds, as strings or {color}.
QStringList diamondColors(const QJsonObject &state)
{
    QJsonArray hand = state.value(QStringLiteral("hand")).toArray();
    if (hand.isEmpty())
        hand = state.value(QStringLiteral("result")).toArray();
    if (hand.isEmpty())
        hand = state.value(QStringLiteral("diamonds")).toArray();
    QStringList out;
    for (const QJsonValue &v : hand) {
        QString c = v.isString() ? v.toString()
                                 : toText(v.toObject().value(QStringLiteral("color")),
                                          toText(v.toObject().value(QStringLiteral("value"))));
        c = c.trimmed().toLower();
        if (!c.isEmpty())
            out << c;
    }
    return out;
}

// The C# bot's DiamondPatternMatches(): in order from the first gem, or (anyOrder) every
// selected color present as often as selected.
bool diamondPatternMatches(const QString &selected, const QStringList &result, bool anyOrder)
{
    QStringList sel;
    for (const QString &p : selected.toLower().split(QLatin1Char(',')))
        if (!p.trimmed().isEmpty())
            sel << p.trimmed();
    if (sel.isEmpty() || result.size() < sel.size())
        return false;
    if (anyOrder) {
        for (const QString &c : sel)
            if (result.count(c) < sel.count(c))
                return false;
        return true;
    }
    for (int i = 0; i < sel.size(); ++i)
        if (sel.at(i) != result.at(i))
            return false;
    return true;
}
} // namespace

QString stopReason(Game game, const QJsonObject &bet, const QJsonObject &params)
{
    const QJsonObject st = stateOf(bet);
    switch (game) {
    case Game::Diamonds: {
        if (!params.value(QStringLiteral("stopOnPattern")).toBool())
            return {};
        const QString selected = toText(params.value(QStringLiteral("colors")));
        const QStringList colors = diamondColors(st);
        const bool anyOrder = params.value(QStringLiteral("anyOrder")).toBool();
        if (diamondPatternMatches(selected, colors, anyOrder))
            return QStringLiteral("Diamonds pattern [%1] matched (%2)").arg(selected, colors.join(QLatin1Char(',')));
        if (colors.isEmpty() && BotConstants::isWinMultiplier(toNumber(bet.value(QStringLiteral("payoutMultiplier")))))
            return QStringLiteral("Diamonds pattern [%1] - win (colors unavailable)").arg(selected);
        return {};
    }
    case Game::Hilo: {
        // Manual / Auto Bet tab: with "Stop on" set, stop once the whole pattern was played
        // and cashed out (and, for the suit / color filters, every card kept it).
        if (!params.contains(QStringLiteral("hiloPattern")))
            return {};
        const QString mode = toText(params.value(QStringLiteral("suitMode")), QStringLiteral("None"));
        if (mode == QLatin1String("None"))
            return {};
        const QJsonArray pattern = toIntArray(params.value(QStringLiteral("hiloPattern")));
        const QJsonArray rounds = st.value(QStringLiteral("rounds")).toArray();
        if (pattern.isEmpty() || rounds.size() < pattern.size()
            || !(toNumber(bet.value(QStringLiteral("payoutMultiplier"))) > 0.0))
            return {};
        const QString startSuit = st.value(QStringLiteral("startCard")).toObject()
                                      .value(QStringLiteral("suit")).toString();
        for (const QJsonValue &r : rounds)
            if (hiloSuitBroken(mode, startSuit,
                               r.toObject().value(QStringLiteral("card")).toObject()
                                   .value(QStringLiteral("suit")).toString()))
                return {};
        return QStringLiteral("HiLo card-filter matched all cards — pattern complete.");
    }
    default:
        return {};
    }
}

QString checkTargetHit(const QJsonArray &hand, const QJsonValue &combos)
{
    if (!combos.isArray())
        return {};
    QMap<QString, int> have;
    for (const QJsonValue &c : hand)
        ++have[c.toObject().value(QStringLiteral("rank")).toString()];
    for (const QJsonValue &cv : combos.toArray()) {
        const QJsonObject combo = cv.toObject();
        if (combo.isEmpty())
            continue;
        bool hit = true;
        QList<QPair<int, QString>> parts;
        for (auto it = combo.begin(); it != combo.end(); ++it) {
            const int needed = toInt(it.value(), 0);
            if (have.value(it.key()) < needed)
                hit = false;
            parts.append({needed, it.key()});
        }
        if (!hit)
            continue;
        std::stable_sort(parts.begin(), parts.end(),
                         [](const QPair<int, QString> &a, const QPair<int, QString> &b) { return a.first > b.first; });
        QStringList label;
        for (const auto &p : parts)
            label << QStringLiteral("%1x%2").arg(p.first).arg(p.second);
        return label.join(QStringLiteral(" + "));
    }
    return {};
}
} // namespace StakeGames
