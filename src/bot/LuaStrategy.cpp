#include "bot/LuaStrategy.h"

#include "bot/BeepSound.h"
#include "bot/StakeGames.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

#include <QByteArray>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QLocale>
#include <QString>

#include <algorithm>
#include <cmath>
#include <ctime>

namespace
{
constexpr int kCallTimeLimitMs = 2000;          // one dobet() / the script body
constexpr size_t kMemoryLimit = 128u * 1024u * 1024u;
constexpr double kMinChance = BotConstants::kMinChance;
constexpr double kMaxChance = BotConstants::kMaxChance;

char g_selfKey = 0; // address used as registry key for the LuaStrategy pointer

// ---- source translation helpers ------------------------------------------------

// src[i] == '[': returns the level of a long bracket opening ([[ = 0, [=[ = 1 ...), or -1.
int longBracketLevel(const QByteArray &src, int i)
{
    const int n = src.size();
    int j = i + 1;
    int level = 0;
    while (j < n && src[j] == '=') {
        ++level;
        ++j;
    }
    return (j < n && src[j] == '[') ? level : -1;
}

// Index just past the long string / block comment that opens at i.
int longBracketEnd(const QByteArray &src, int i, int level)
{
    const QByteArray close = "]" + QByteArray(level, '=') + "]";
    const int end = src.indexOf(close, i + level + 2);
    return end < 0 ? int(src.size()) : end + int(close.size());
}

bool isIdentChar(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

// Words that cannot continue an expression: they end the right-hand side of "x += ...".
bool isStatementWord(const QByteArray &w)
{
    static const char *const words[] = {"end",   "else", "elseif", "until", "then",  "do",
                                        "local", "return", "if",   "while", "for",   "repeat",
                                        "break", "goto"};
    for (const char *k : words)
        if (w == k)
            return true;
    return false;
}

// Finds where the right-hand side of a compound assignment that starts at `from`
// ends: at a newline or ';' (outside brackets), at a comment, at a closing bracket
// that is not ours, before a statement keyword (end, else, local, ...), or before an
// identifier that follows a complete operand ("a += 1 b += 2").
int findRhsEnd(const QByteArray &src, int from)
{
    const int n = src.size();
    int i = from;
    int depth = 0;
    bool operand = false; // the previous token completes an operand

    while (i < n) {
        const char c = src[i];

        if (c == '-' && i + 1 < n && src[i + 1] == '-')
            return i; // comment
        if (depth == 0 && (c == '\n' || c == ';'))
            return i;
        if (isSpace(c)) {
            ++i;
            continue;
        }
        if (c == '"' || c == '\'') {
            ++i;
            while (i < n && src[i] != c && src[i] != '\n') {
                if (src[i] == '\\' && i + 1 < n)
                    ++i;
                ++i;
            }
            if (i < n && src[i] == c)
                ++i;
            operand = true;
            continue;
        }
        if (c == '[') {
            const int level = longBracketLevel(src, i);
            if (level >= 0) {
                i = longBracketEnd(src, i, level);
                operand = true;
                continue;
            }
            ++depth;
            operand = false;
            ++i;
            continue;
        }
        if (c == '(' || c == '{') {
            ++depth;
            operand = false;
            ++i;
            continue;
        }
        if (c == ')' || c == '}' || c == ']') {
            if (--depth < 0)
                return i;
            operand = true;
            ++i;
            continue;
        }
        if (isIdentChar(c)) {
            int j = i;
            while (j < n && isIdentChar(src[j]))
                ++j;
            const QByteArray word = src.mid(i, j - i);
            const bool number = word[0] >= '0' && word[0] <= '9';
            if (!number && depth == 0) {
                if (isStatementWord(word))
                    return i;
                if (operand && word != "and" && word != "or")
                    return i; // a new statement starts here
            }
            // Exponent sign of a number like 1e-5 / 0x1p+3 belongs to the number.
            if (number && j < n && (src[j] == '-' || src[j] == '+')) {
                const char last = word.back();
                const bool hex = word.size() > 1 && (word[1] == 'x' || word[1] == 'X');
                if ((!hex && (last == 'e' || last == 'E')) || (hex && (last == 'p' || last == 'P')))
                    ++j;
                while (j < n && isIdentChar(src[j]))
                    ++j;
            }
            operand = number || !(word == "and" || word == "or" || word == "not" || word == "function");
            i = j;
            continue;
        }
        // operators, '.', ':', ',' ...
        operand = false;
        ++i;
    }
    return n;
}

// Finds the start of the assignable expression that ends at out[end) (name, a.b, a[i].c ...).
// Returns -1 if there is none.
int findLhsStart(const QByteArray &out, int end)
{
    int i = end;
    while (true) {
        // optional index suffixes: ...[expr]
        while (i > 0 && out[i - 1] == ']') {
            int depth = 0;
            int k = i - 1;
            for (; k >= 0; --k) {
                if (out[k] == ']')
                    ++depth;
                else if (out[k] == '[' && --depth == 0)
                    break;
            }
            if (k < 0)
                return -1;
            i = k;
        }
        const int nameEnd = i;
        while (i > 0 && isIdentChar(out[i - 1]))
            --i;
        if (i == nameEnd)
            return -1;
        if (out[i] >= '0' && out[i] <= '9')
            return -1;
        // continue with "<prefix>.name" (but not with the ".." operator)
        if (i > 0 && out[i - 1] == '.' && !(i > 1 && out[i - 2] == '.')) {
            --i;
            continue;
        }
        return i;
    }
}

// Lets scripts write "!x" instead of "not x" (and "!=" instead of "~="), and use the
// compound assignments  +=  -=  *=  /=  //=  %=  ^=  ..=  ("x += y" becomes
// "x = x + (y)"). Done as a text pass over the source before it is compiled; strings,
// long strings and comments are copied untouched. Replacements stay on the same line
// (the right-hand side keeps its own newlines), so line numbers in Lua error messages
// stay correct. The left-hand side is evaluated twice, so keep it free of side effects
// (t[f()] += 1 calls f twice).
QByteArray translateBang(const QByteArray &src)
{
    QByteArray out;
    out.reserve(src.size() + 16);
    const int n = src.size();

    int i = 0;
    while (i < n) {
        const char c = src[i];

        if (c == '-' && i + 1 < n && src[i + 1] == '-') { // comment
            out.append("--");
            i += 2;
            if (i < n && src[i] == '[') {
                const int level = longBracketLevel(src, i);
                if (level >= 0) {
                    const int end = longBracketEnd(src, i, level);
                    out.append(src.mid(i, end - i));
                    i = end;
                    continue;
                }
            }
            while (i < n && src[i] != '\n')
                out.append(src[i++]);
            continue;
        }
        if (c == '[') { // long string
            const int level = longBracketLevel(src, i);
            if (level >= 0) {
                const int end = longBracketEnd(src, i, level);
                out.append(src.mid(i, end - i));
                i = end;
                continue;
            }
        }
        if (c == '"' || c == '\'') { // quoted string
            out.append(c);
            ++i;
            while (i < n && src[i] != c && src[i] != '\n') {
                if (src[i] == '\\' && i + 1 < n)
                    out.append(src[i++]);
                out.append(src[i++]);
            }
            if (i < n && src[i] == c)
                out.append(src[i++]);
            continue;
        }

        // compound assignment: + - * / // % ^ ..  followed by '='
        {
            const char *op = nullptr;
            int opLen = 0;
            if (c == '/' && i + 2 < n && src[i + 1] == '/' && src[i + 2] == '=') {
                op = "//"; opLen = 3;
            } else if (c == '.' && i + 2 < n && src[i + 1] == '.' && src[i + 2] == '=') {
                op = ".."; opLen = 3;
            } else if ((c == '+' || c == '-' || c == '*' || c == '/' || c == '%' || c == '^')
                       && i + 1 < n && src[i + 1] == '='
                       && !(i + 2 < n && src[i + 2] == '=')) {
                static char single[2] = {0, 0};
                single[0] = c;
                op = single; opLen = 2;
            }
            if (op) {
                int e = out.size();
                while (e > 0 && (out[e - 1] == ' ' || out[e - 1] == '\t'))
                    --e;
                const int start = findLhsStart(out, e);
                if (start >= 0) {
                    const QByteArray lhs = out.mid(start, e - start);
                    const int rhsStart = i + opLen;
                    int rhsEnd = findRhsEnd(src, rhsStart);
                    int trimmed = rhsEnd;
                    while (trimmed > rhsStart && isSpace(src[trimmed - 1]))
                        --trimmed;
                    const QByteArray rhs = translateBang(src.mid(rhsStart, trimmed - rhsStart));
                    out.append("= ");
                    out.append(lhs);
                    out.append(' ');
                    out.append(op);
                    out.append(" (");
                    out.append(rhs);
                    out.append(')');
                    out.append(src.mid(trimmed, rhsEnd - trimmed)); // whitespace after the expression
                    i = rhsEnd;
                    continue;
                }
            }
        }

        if (c == '!') {
            if (i + 1 < n && src[i + 1] == '=') {
                out.append("~=");
                i += 2;
            } else {
                out.append(" not ");
                ++i;
            }
            continue;
        }
        out.append(c);
        ++i;
    }
    return out;
}

struct Allocator
{
    size_t used = 0;
};

void *limitedAlloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
    auto *a = static_cast<Allocator *>(ud);
    if (!ptr)
        osize = 0;
    if (nsize == 0) {
        a->used -= std::min(osize, a->used);
        std::free(ptr);
        return nullptr;
    }
    if (a->used - std::min(osize, a->used) + nsize > kMemoryLimit)
        return nullptr; // Lua turns this into a "not enough memory" error
    void *np = std::realloc(ptr, nsize);
    if (np)
        a->used = a->used - std::min(osize, a->used) + nsize;
    return np;
}

LuaStrategy *self(lua_State *L)
{
    lua_rawgetp(L, LUA_REGISTRYINDEX, &g_selfKey);
    auto *s = static_cast<LuaStrategy *>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return s;
}

QJsonValue luaToJson(lua_State *L, int idx, int depth); // defined below

// A number in full positional notation (0.0000001, not 1e-07), shortest exact form.
QString fullNumber(double v)
{
    if (!std::isfinite(v))
        return std::isnan(v) ? QStringLiteral("nan") : (v > 0 ? QStringLiteral("inf") : QStringLiteral("-inf"));
    return QString::number(v, 'f', QLocale::FloatingPointShortest);
}

// A table's contents as JSON-like text ({"a":1,"b":[1,2]}) with every number in full.
QString jsonFullText(const QJsonValue &v)
{
    switch (v.type()) {
    case QJsonValue::Double:
        return fullNumber(v.toDouble());
    case QJsonValue::Bool:
        return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QJsonValue::String: {
        QJsonArray wrap;
        wrap.append(v);
        const QByteArray j = QJsonDocument(wrap).toJson(QJsonDocument::Compact); // ["..."]
        return QString::fromUtf8(j.mid(1, j.size() - 2));
    }
    case QJsonValue::Array: {
        QStringList items;
        for (const QJsonValue &e : v.toArray())
            items << jsonFullText(e);
        return QLatin1Char('{') + items.join(QLatin1Char(',')) + QLatin1Char('}');
    }
    case QJsonValue::Object: {
        QStringList items;
        const QJsonObject o = v.toObject();
        for (auto it = o.begin(); it != o.end(); ++it)
            items << it.key() + QLatin1Char('=') + jsonFullText(it.value());
        return QLatin1Char('{') + items.join(QLatin1Char(',')) + QLatin1Char('}');
    }
    default:
        return QStringLiteral("nil");
    }
}

// Joins the arguments like print() does (tab separated, using __tostring).
QString joinArgs(lua_State *L)
{
    QStringList parts;
    const int n = lua_gettop(L);
    for (int i = 1; i <= n; ++i) {
        // Floats are written in full positional notation (0.00000226) instead of
        // Lua's default scientific notation (2.26e-06).
        if (lua_type(L, i) == LUA_TNUMBER && !lua_isinteger(L, i)) {
            const double v = lua_tonumber(L, i);
            if (std::isfinite(v)) {
                QString txt = fullNumber(v);
                if (!txt.contains(QLatin1Char('.')))
                    txt += QStringLiteral(".0"); // keep Lua's "5.0" for whole floats
                parts << txt;
                continue;
            }
        }
        // Tables: their contents ({1,2,3} / {player=0.001,...}) instead of "table: 0x...",
        // unless the table has its own __tostring.
        if (lua_type(L, i) == LUA_TTABLE) {
            if (luaL_getmetafield(L, i, "__tostring") == LUA_TNIL) {
                parts << jsonFullText(luaToJson(L, i, 0));
                continue;
            }
            lua_pop(L, 1); // the __tostring field
        }
        size_t len = 0;
        const char *s = luaL_tolstring(L, i, &len);
        parts << QString::fromUtf8(s, int(len));
        lua_pop(L, 1);
    }
    return parts.join(QLatin1Char('\t'));
}

int l_print(lua_State *L)
{
    if (LuaStrategy *s = self(L))
        s->log(joinArgs(L), false);
    return 0;
}

int l_stop(lua_State *L)
{
    QString reason;
    if (lua_gettop(L) >= 1 && !lua_isnil(L, 1)) {
        size_t len = 0;
        const char *s = luaL_tolstring(L, 1, &len);
        reason = QString::fromUtf8(s, int(len));
        lua_pop(L, 1);
    }
    if (LuaStrategy *s = self(L))
        s->requestStop(reason);
    return 0;
}

// resetstats(): resets the session statistics.
int l_resetstats(lua_State *L)
{
    if (LuaStrategy *s = self(L))
        s->requestResetStats();
    return 0;
}

// resetseed([clientseed]): rotates the seed pair; the optional argument is the new
// client seed (anything else than nil is converted to a string).
int l_resetseed(lua_State *L)
{
    QString seed;
    if (lua_gettop(L) >= 1 && !lua_isnil(L, 1)) {
        size_t len = 0;
        const char *s = luaL_tolstring(L, 1, &len);
        seed = QString::fromUtf8(s, int(len));
        lua_pop(L, 1);
    }
    if (LuaStrategy *s = self(L))
        s->requestResetSeed(seed);
    return 0;
}

// vault(amount): deposits `amount` of the current currency into the vault (Shuffle, live).
int l_vault(lua_State *L)
{
    LuaStrategy *s = self(L);
    if (!s)
        return 0;
    if (lua_gettop(L) < 1 || !lua_isnumber(L, 1)) {
        s->log(QStringLiteral("Please specify vault amount."), true);
        return 0;
    }
    s->requestVault(static_cast<double>(lua_tonumber(L, 1)));
    return 0;
}

int l_ostime(lua_State *L)
{
    lua_pushinteger(L, static_cast<lua_Integer>(std::time(nullptr)));
    return 1;
}

int l_osclock(lua_State *L)
{
    lua_pushnumber(L, double(std::clock()) / double(CLOCKS_PER_SEC));
    return 1;
}

// ---- JSON <-> Lua ------------------------------------------------------------------
// Arrays become 1-based sequences, objects tables with string keys (like the reference
// bot's jsToLua()); whole numbers are pushed as integers so they print as "5", not "5.0".
void pushJson(lua_State *L, const QJsonValue &v, int depth = 0)
{
    if (depth > 32) {
        lua_pushnil(L);
        return;
    }
    switch (v.type()) {
    case QJsonValue::Bool:
        lua_pushboolean(L, v.toBool() ? 1 : 0);
        break;
    case QJsonValue::Double: {
        const double d = v.toDouble();
        if (std::isfinite(d) && d == std::floor(d) && std::fabs(d) < 9.0e15)
            lua_pushinteger(L, static_cast<lua_Integer>(d));
        else
            lua_pushnumber(L, d);
        break;
    }
    case QJsonValue::String: {
        const QByteArray b = v.toString().toUtf8();
        lua_pushlstring(L, b.constData(), size_t(b.size()));
        break;
    }
    case QJsonValue::Array: {
        const QJsonArray a = v.toArray();
        lua_createtable(L, int(a.size()), 0);
        for (int i = 0; i < a.size(); ++i) {
            pushJson(L, a.at(i), depth + 1);
            lua_rawseti(L, -2, i + 1);
        }
        break;
    }
    case QJsonValue::Object: {
        const QJsonObject o = v.toObject();
        lua_createtable(L, 0, int(o.size()));
        for (auto it = o.begin(); it != o.end(); ++it) {
            pushJson(L, it.value(), depth + 1);
            const QByteArray k = it.key().toUtf8();
            lua_setfield(L, -2, k.constData());
        }
        break;
    }
    default:
        lua_pushnil(L);
        break;
    }
}

QJsonValue luaToJson(lua_State *L, int idx, int depth = 0)
{
    idx = lua_absindex(L, idx);
    switch (lua_type(L, idx)) {
    case LUA_TBOOLEAN:
        return lua_toboolean(L, idx) != 0;
    case LUA_TNUMBER:
        return double(lua_tonumber(L, idx));
    case LUA_TSTRING:
        return QString::fromUtf8(lua_tostring(L, idx));
    case LUA_TTABLE: {
        if (depth > 32)
            return QJsonValue(QJsonValue::Null);
        // A sequence 1..n (or an empty table) is an array, anything else an object.
        const lua_Integer n = static_cast<lua_Integer>(lua_rawlen(L, idx));
        lua_Integer count = 0;
        bool sequence = true;
        lua_pushnil(L);
        while (lua_next(L, idx) != 0) {
            ++count;
            if (!lua_isinteger(L, -2)) {
                sequence = false;
            } else {
                const lua_Integer k = lua_tointeger(L, -2);
                if (k < 1 || k > n)
                    sequence = false;
            }
            lua_pop(L, 1);
        }
        if (sequence && count == n) {
            QJsonArray a;
            for (lua_Integer i = 1; i <= n; ++i) {
                lua_rawgeti(L, idx, i);
                a.append(luaToJson(L, -1, depth + 1));
                lua_pop(L, 1);
            }
            return a;
        }
        QJsonObject o;
        lua_pushnil(L);
        while (lua_next(L, idx) != 0) {
            QString key;
            if (lua_type(L, -2) == LUA_TSTRING) {
                key = QString::fromUtf8(lua_tostring(L, -2));
            } else if (lua_isinteger(L, -2)) {
                key = QString::number(lua_tointeger(L, -2)); // {[10] = 2} -> "10"
            } else if (lua_type(L, -2) == LUA_TNUMBER) {
                key = QString::number(double(lua_tonumber(L, -2)));
            }
            if (!key.isEmpty())
                o.insert(key, luaToJson(L, -1, depth + 1));
            lua_pop(L, 1);
        }
        return o;
    }
    default:
        return QJsonValue(QJsonValue::Null);
    }
}

// checkTargetHit(hand, combos): video poker helper of the reference bot. Returns the label of
// the first combo ({A = 3, K = 2}) the hand holds, or nil.
int l_checkTargetHit(lua_State *L)
{
    const QJsonValue hand = lua_gettop(L) >= 1 ? luaToJson(L, 1) : QJsonValue();
    const QJsonValue combos = lua_gettop(L) >= 2 ? luaToJson(L, 2) : QJsonValue();
    const QString hit = StakeGames::checkTargetHit(hand.toArray(), combos);
    if (hit.isEmpty()) {
        lua_pushnil(L);
    } else {
        const QByteArray b = hit.toUtf8();
        lua_pushstring(L, b.constData());
    }
    return 1;
}

// beep() / ding(): play the reference bot's bell tone (e.g. to signal a hit).
int l_beep(lua_State *)
{
    BeepSound::play();
    return 0;
}

// Aborts runaway scripts (while true do end).
void hook(lua_State *L, lua_Debug *)
{
    LuaStrategy *s = self(L);
    if (s && s->timeExceeded())
        luaL_error(L, "script took longer than %d ms and was aborted", kCallTimeLimitMs);
}

double floor8(double v) { return std::floor(v * 1e8) / 1e8; }
double round2(double v) { return std::round(v * 100.0) / 100.0; }
} // namespace

LuaStrategy::LuaStrategy() = default;

LuaStrategy::~LuaStrategy()
{
    close();
}

void LuaStrategy::close()
{
    if (m_L) {
        void *ud = nullptr;
        lua_getallocf(m_L, &ud);
        lua_close(m_L);
        delete static_cast<Allocator *>(ud);
        m_L = nullptr;
    }
}

void LuaStrategy::startTimer()
{
    m_deadlineMs = QDateTime::currentMSecsSinceEpoch() + kCallTimeLimitMs;
}

bool LuaStrategy::timeExceeded() const
{
    return m_deadlineMs != 0 && QDateTime::currentMSecsSinceEpoch() > m_deadlineMs;
}

void LuaStrategy::publish(const char *name, double value)
{
    lua_pushnumber(m_L, value);
    lua_setglobal(m_L, name);
}

void LuaStrategy::publishLastBet(const BetInfo &info)
{
    lua_newtable(m_L);
    const struct { const char *key; double value; } fields[] = {
        {"amount", info.bet}, {"profit", info.profit},
        {"payout", info.payout}, {"payoutMultiplier", info.multiplier}};
    for (const auto &f : fields) {
        lua_pushnumber(m_L, f.value);
        lua_setfield(m_L, -2, f.key);
    }
    // lastBet.roll / lastBet.Roll (the reference bot's spelling); video poker: the hand's
    // outcome ("twoPair", "onePair", "royalFlush", ...).
    // Mines: the mine positions, keno: the drawn numbers (1-based tables).
    const QJsonValue special = info.betJson.isEmpty() ? QJsonValue(QJsonValue::Undefined)
                                                      : StakeGames::scriptRoll(info.game, info.betJson);
    for (const char *key : {"roll", "Roll"}) {
        if (!special.isUndefined()) {
            pushJson(m_L, special);
        } else {
            lua_pushnumber(m_L, info.roll);
        }
        lua_setfield(m_L, -2, key);
    }
    lua_pushboolean(m_L, info.win ? 1 : 0);
    lua_setfield(m_L, -2, "win");
    // The game's own result (baccarat "banker", roulette's number, the dice roll, ...).
    pushJson(m_L, StakeGames::scriptResult(info.game, info.betJson, info.roll));
    lua_setfield(m_L, -2, "result");
    const QByteArray id = info.betId.toUtf8();
    lua_pushstring(m_L, id.constData());
    lua_setfield(m_L, -2, "id");
    const QByteArray name = info.betJson.value(QStringLiteral("user")).toObject()
                                .value(QStringLiteral("name")).toString().toUtf8();
    lua_pushstring(m_L, name.constData());
    lua_setfield(m_L, -2, "name");
    lua_setglobal(m_L, "lastBet");
}

void LuaStrategy::publishJson(const char *name, const QJsonValue &value)
{
    pushJson(m_L, value);
    lua_setglobal(m_L, name);
}

QJsonValue LuaStrategy::readJson(const char *name)
{
    lua_getglobal(m_L, name);
    const QJsonValue v = lua_isnil(m_L, -1) ? QJsonValue(QJsonValue::Undefined) : luaToJson(m_L, -1);
    lua_pop(m_L, 1);
    return v;
}

void LuaStrategy::publishInt(const char *name, qint64 value)
{
    lua_pushinteger(m_L, static_cast<lua_Integer>(value));
    lua_setglobal(m_L, name);
}

void LuaStrategy::publishBool(const char *name, bool value)
{
    lua_pushboolean(m_L, value ? 1 : 0);
    lua_setglobal(m_L, name);
}

bool LuaStrategy::callProtected(int nargs, const char *what, int nresults)
{
    startTimer();
    const int rc = lua_pcall(m_L, nargs, nresults, 0);
    m_deadlineMs = 0;
    if (rc != LUA_OK) {
        const char *msg = lua_tostring(m_L, -1);
        m_error = QStringLiteral("%1: %2").arg(QString::fromUtf8(what),
                                               msg ? QString::fromUtf8(msg) : QStringLiteral("error"));
        lua_pop(m_L, 1);
        return false;
    }
    return true;
}

// resetstats(): the run counters start over and the globals follow at once, so the
// script sees the new values right after the call.
void LuaStrategy::onStatsReset(double balance)
{
    m_bets = m_wins = m_losses = m_streak = 0;
    m_wagered = 0.0;
    if (!m_L)
        return;
    publish("startbalance", balance);
    publish("balance", balance);
    publish("profit", 0.0);
    publishInt("bets", 0);
    publishInt("wins", 0);
    publishInt("losses", 0);
    publish("wagered", 0.0);
    publishInt("currentstreak", 0);
    publishInt("winstreak", 0);
    publishInt("losestreak", 0);
}

LuaStrategy::Status LuaStrategy::init(const QString &source, double balance,
                                      double &nextBet, BotConfig &runCfg)
{
    close();
    m_error.clear();
    m_stopReason.clear();
    m_stop = false;
    m_bets = m_wins = m_losses = m_streak = 0;
    m_wagered = 0.0;

    auto *alloc = new Allocator;
    m_L = lua_newstate(limitedAlloc, alloc);
    if (!m_L) {
        delete alloc;
        m_error = QStringLiteral("could not create the Lua state");
        return Status::Error;
    }

    // Registry pointer back to this object (for print/stop/hook).
    lua_pushlightuserdata(m_L, this);
    lua_rawsetp(m_L, LUA_REGISTRYINDEX, &g_selfKey);

    // Safe libraries only: no io, no os.execute, no package/require, no debug.
    const luaL_Reg libs[] = {
        {LUA_GNAME, luaopen_base},     {LUA_TABLIBNAME, luaopen_table},
        {LUA_STRLIBNAME, luaopen_string}, {LUA_MATHLIBNAME, luaopen_math},
        {LUA_UTF8LIBNAME, luaopen_utf8},  {LUA_COLIBNAME, luaopen_coroutine},
    };
    for (const luaL_Reg &l : libs) {
        luaL_requiref(m_L, l.name, l.func, 1);
        lua_pop(m_L, 1);
    }
    for (const char *g : {"dofile", "loadfile", "collectgarbage"}) {
        lua_pushnil(m_L);
        lua_setglobal(m_L, g);
    }

    lua_pushcfunction(m_L, l_print);
    lua_setglobal(m_L, "print");
    lua_pushcfunction(m_L, l_print);
    lua_setglobal(m_L, "log");
    lua_pushcfunction(m_L, l_stop);
    lua_setglobal(m_L, "stop");
    lua_pushcfunction(m_L, l_resetstats);
    lua_setglobal(m_L, "resetstats");
    lua_pushcfunction(m_L, l_resetseed);
    lua_setglobal(m_L, "resetseed");
    lua_pushcfunction(m_L, l_vault);
    lua_setglobal(m_L, "vault");
    lua_pushcfunction(m_L, l_checkTargetHit);
    lua_setglobal(m_L, "checkTargetHit");
    lua_pushcfunction(m_L, l_beep); // ding() plays the same tone as beep()
    lua_setglobal(m_L, "ding");
    lua_pushcfunction(m_L, l_beep);
    lua_setglobal(m_L, "beep");
    // Hilo moves returned by round() (same values as the reference bot).
    publishInt("HILO_BET_EQUAL", 2);
    publishInt("HILO_SKIP", 7);
    publishInt("HILO_BET_HIGH", 5);
    publishInt("HILO_BET_LOW", 4);
    publishInt("HILO_CASHOUT", 3);

    lua_newtable(m_L); // minimal os table
    lua_pushcfunction(m_L, l_ostime);
    lua_setfield(m_L, -2, "time");
    lua_pushcfunction(m_L, l_osclock);
    lua_setfield(m_L, -2, "clock");
    lua_setglobal(m_L, "os");

    lua_sethook(m_L, hook, LUA_MASKCOUNT, 10000);

    // No bet settings are injected except the game: nextbet and chance (dice, and
    // optionally bethigh, default = roll under) or target (limbo) must be set by
    // the script itself.
    {
        const QByteArray g = gameName(runCfg.game).toUtf8();
        lua_pushstring(m_L, g.constData());
        lua_setglobal(m_L, "game");
    }
    if (!runCfg.casino.isEmpty()) {
        const QByteArray k = runCfg.casino.toUtf8();
        lua_pushstring(m_L, k.constData());
        lua_setglobal(m_L, "casino");
    }
    if (!runCfg.currency.isEmpty()) {
        const QByteArray c = runCfg.currency.toUtf8();
        lua_pushstring(m_L, c.constData());
        lua_setglobal(m_L, "currency");
    }
    publish("startbalance", balance);
    publishBool("win", false);
    publish("previousbet", 0.0);
    publishLastBet(BetInfo());
    publishJson("bet", QJsonObject());
    publish("lastprofit", 0.0);
    publish("currentprofit", 0.0);
    publishInt("winstreak", 0);
    publishInt("losestreak", 0);
    publish("roll", 0.0);
    publish("balance", balance);
    publish("profit", 0.0);
    publishInt("bets", 0);
    publishInt("wins", 0);
    publishInt("losses", 0);
    publish("wagered", 0.0);
    publishInt("currentstreak", 0);

    const QByteArray code = translateBang(source.toUtf8());
    if (luaL_loadbufferx(m_L, code.constData(), size_t(code.size()), "=strategy", "t") != LUA_OK) {
        m_error = QStringLiteral("syntax error: %1").arg(QString::fromUtf8(lua_tostring(m_L, -1)));
        lua_pop(m_L, 1);
        close();
        return Status::Error;
    }
    if (!callProtected(0, "script")) {
        close();
        return Status::Error;
    }
    if (m_stop)
        return Status::Stop;

    if (!readOutputs(nextBet, runCfg)) {
        close();
        return Status::Error;
    }
    return Status::Ok;
}

LuaStrategy::Status LuaStrategy::onBet(const BetInfo &info, double &nextBet, BotConfig &runCfg)
{
    if (!m_L)
        return Status::Error;

    ++m_bets;
    m_wagered += info.bet;
    if (info.win) {
        ++m_wins;
        m_streak = m_streak > 0 ? m_streak + 1 : 1;
    } else {
        ++m_losses;
        m_streak = m_streak < 0 ? m_streak - 1 : -1;
    }

    publishBool("win", info.win);
    publish("previousbet", info.bet);
    publishLastBet(info);
    publishJson("bet", info.betJson);
    publish("lastprofit", info.profit);
    publish("currentprofit", info.profit);
    publishInt("winstreak", m_streak > 0 ? m_streak : 0);
    publishInt("losestreak", m_streak < 0 ? -m_streak : 0);
    publish("roll", info.roll);
    publish("balance", info.balance);
    publish("profit", info.runProfit);
    publishInt("bets", m_bets);
    publishInt("wins", m_wins);
    publishInt("losses", m_losses);
    publish("wagered", m_wagered);
    publishInt("currentstreak", m_streak);

    lua_getglobal(m_L, "dobet");
    if (lua_isfunction(m_L, -1)) {
        if (!callProtected(0, "dobet()"))
            return Status::Error;
    } else {
        lua_pop(m_L, 1); // no dobet(): keep the values the script set
    }

    if (m_stop)
        return Status::Stop;
    return readOutputs(nextBet, runCfg) ? Status::Ok : Status::Error;
}

LuaStrategy::Status LuaStrategy::callRound(const QJsonObject &currentBet, QJsonValue &result)
{
    result = QJsonValue(QJsonValue::Undefined);
    if (!m_L)
        return Status::Error;
    publishJson("currentBet", currentBet);
    lua_getglobal(m_L, "round");
    if (!lua_isfunction(m_L, -1)) {
        lua_pop(m_L, 1);
        return Status::Ok; // no round(): the game's built-in play is used
    }
    if (!callProtected(0, "round()", 1))
        return Status::Error;
    if (lua_type(m_L, -1) == LUA_TNUMBER)
        result = double(lua_tonumber(m_L, -1));
    else if (lua_type(m_L, -1) == LUA_TSTRING)
        result = QString::fromUtf8(lua_tostring(m_L, -1));
    lua_pop(m_L, 1);
    return Status::Ok;
}

// Reads game / nextbet and chance + bethigh (dice) or target (limbo) back from the script.
bool LuaStrategy::readOutputs(double &nextBet, BotConfig &runCfg)
{
    // ---- game
    lua_getglobal(m_L, "game");
    if (lua_isnil(m_L, -1)) {
        // keep the current game
    } else if (lua_type(m_L, -1) == LUA_TSTRING) {
        const QString g = QString::fromUtf8(lua_tostring(m_L, -1)).trimmed().toLower();
        Game parsed = Game::Dice;
        if (gameFromName(g, &parsed)) {
            runCfg.game = parsed;
        } else {
            lua_pop(m_L, 1);
            QStringList names;
            for (Game known : allGames())
                names << gameName(known);
            m_error = QStringLiteral("game \"%1\" is not supported (crash and slide are not available); "
                                     "use one of: %2").arg(g, names.join(QStringLiteral(", ")));
            return false;
        }
    } else {
        lua_pop(m_L, 1);
        m_error = QStringLiteral("game must be a string such as \"dice\", \"limbo\" or \"keno\"");
        return false;
    }
    lua_pop(m_L, 1);

    // ---- casino (optional): "stake", "shuffle", "goated", "wolfbet", "duel", "paradice", "pasino", "tower" or "winna"
    lua_getglobal(m_L, "casino");
    if (lua_isnil(m_L, -1)) {
        // keep the current casino
    } else if (lua_type(m_L, -1) == LUA_TSTRING) {
        const QString k = QString::fromUtf8(lua_tostring(m_L, -1)).trimmed().toLower();
        if (k == QLatin1String("stake") || k == QLatin1String("shuffle") || k == QLatin1String("goated")
            || k == QLatin1String("wolfbet") || k == QLatin1String("duel")
            || k == QLatin1String("paradice") || k == QLatin1String("pasino")
            || k == QLatin1String("tower") || k == QLatin1String("winna")) {
            runCfg.casino = k;
        } else if (!k.isEmpty()) {
            lua_pop(m_L, 1);
            m_error = QStringLiteral("casino must be \"stake\", \"shuffle\", \"goated\", \"wolfbet\", \"duel\", \"paradice\", \"pasino\", \"tower\" or \"winna\" (got \"%1\")").arg(k);
            return false;
        }
    } else {
        lua_pop(m_L, 1);
        m_error = QStringLiteral("casino must be the string \"stake\", \"shuffle\", \"goated\", \"wolfbet\", \"duel\", \"paradice\", \"pasino\", \"tower\" or \"winna\"");
        return false;
    }
    lua_pop(m_L, 1);

    // ---- currency (optional): the currency code to bet in, e.g. "btc" / "usdt"
    lua_getglobal(m_L, "currency");
    if (lua_isnil(m_L, -1)) {
        // keep the current currency
    } else if (lua_type(m_L, -1) == LUA_TSTRING) {
        const QString c = QString::fromUtf8(lua_tostring(m_L, -1)).trimmed().toLower();
        if (!c.isEmpty())
            runCfg.currency = c;
    } else {
        lua_pop(m_L, 1);
        m_error = QStringLiteral("currency must be a string such as \"btc\" or \"usdt\"");
        return false;
    }
    lua_pop(m_L, 1);

    // ---- Stake's other games: their settings (README variables) and nextbet
    if (!isDiceOrLimbo(runCfg.game)) {
        QJsonObject params;
        for (const QString &name : StakeGames::scriptVariables(runCfg.game)) {
            const QByteArray key = name.toLatin1();
            const QJsonValue v = readJson(key.constData());
            if (!v.isUndefined() && !v.isNull())
                params.insert(name, v);
        }
        // Code mode has no defaults: every variable of the game must be set by the script.
        lua_getglobal(m_L, "round");
        const bool hasRound = lua_isfunction(m_L, -1);
        lua_pop(m_L, 1);
        const QStringList missing = StakeGames::missingScriptVariables(runCfg.game, params,
                                                                       readJson("nextbet"), hasRound);
        if (!missing.isEmpty()) {
            m_error = QStringLiteral("%1 needs these variables set in the script: %2")
                          .arg(gameName(runCfg.game), missing.join(QStringLiteral(", ")));
            return false;
        }
        double total = 0.0;
        if (!StakeGames::finishScriptParams(runCfg.game, readJson("nextbet"), params, total, m_error))
            return false;
        runCfg.gameParams = params;
        nextBet = floor8(total);
        return true;
    }

    // ---- nextbet
    lua_getglobal(m_L, "nextbet");
    if (!lua_isnumber(m_L, -1)) {
        lua_pop(m_L, 1);
        m_error = QStringLiteral("nextbet must be set to a number by the script");
        return false;
    }
    const double bet = lua_tonumber(m_L, -1);
    lua_pop(m_L, 1);
    if (!std::isfinite(bet) || bet < 0.0) {
        m_error = QStringLiteral("nextbet must be a finite number >= 0 (got %1)").arg(bet);
        return false;
    }

    if (runCfg.game == Game::Limbo) {
        // Limbo: only `target` is used; chance / bethigh are ignored.
        lua_getglobal(m_L, "target");
        if (!lua_isnumber(m_L, -1)) {
            lua_pop(m_L, 1);
            m_error = QStringLiteral("target must be set to a number by the script (limbo)");
            return false;
        }
        const double target = lua_tonumber(m_L, -1);
        lua_pop(m_L, 1);
        if (!std::isfinite(target)) {
            m_error = QStringLiteral("target must be a finite number");
            return false;
        }
        runCfg.multiplierTarget = round2(std::clamp(target, BotConstants::kMinLimboTarget,
                                                    BotConstants::kMaxLimboTarget));
        nextBet = floor8(bet);
        return true;
    }

    lua_getglobal(m_L, "chance");
    if (!lua_isnumber(m_L, -1)) {
        lua_pop(m_L, 1);
        m_error = QStringLiteral("chance must be set to a number by the script");
        return false;
    }
    double chance = lua_tonumber(m_L, -1);
    lua_pop(m_L, 1);
    if (!std::isfinite(chance)) {
        m_error = QStringLiteral("chance must be a finite number");
        return false;
    }

    lua_getglobal(m_L, "bethigh");
    const bool high = lua_toboolean(m_L, -1) != 0;
    lua_pop(m_L, 1);

    chance = round2(std::clamp(chance, kMinChance, kMaxChance));
    runCfg.side = high ? BetSide::Over : BetSide::Under;
    runCfg.target = round2(high ? 100.0 - chance : chance);
    nextBet = floor8(bet);
    return true;
}

void LuaStrategy::setGlobalString(const char *name, const QString &value)
{
    if (!m_L)
        return;
    const QByteArray v = value.toUtf8();
    lua_pushstring(m_L, v.constData());
    lua_setglobal(m_L, name);
}
