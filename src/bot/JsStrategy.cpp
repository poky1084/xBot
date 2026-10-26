#include "bot/JsStrategy.h"

#include <QJSEngine>
#include <QJSValue>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace
{
constexpr int kCallTimeLimitMs = 2000;          // one dobet() / the script body
constexpr double kMinChance = BotConstants::kMinChance;
constexpr double kMaxChance = BotConstants::kMaxChance;

double floor8(double v) { return std::floor(v * 1e8) / 1e8; }
double round2(double v) { return std::round(v * 100.0) / 100.0; }

// Interrupts the engine when a call runs too long (the equivalent of the Lua
// instruction-count hook). QJSEngine::setInterrupted() is meant to be called from
// another thread, which is exactly what a runaway `while (true) {}` needs.
class Watchdog
{
public:
    Watchdog(QJSEngine *engine, int limitMs)
        : m_engine(engine)
    {
        m_thread = std::thread([this, limitMs] {
            std::unique_lock<std::mutex> lock(m_mutex);
            if (!m_cv.wait_for(lock, std::chrono::milliseconds(limitMs), [this] { return m_done; })) {
                m_timedOut = true;
                m_engine->setInterrupted(true);
            }
        });
    }
    ~Watchdog() { finish(); }

    // Stops the timer thread; returns true if the limit was hit (the interrupt
    // flag is cleared again so the engine can be used afterwards).
    bool finish()
    {
        if (m_thread.joinable()) {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_done = true;
            }
            m_cv.notify_all();
            m_thread.join();
            if (m_timedOut)
                m_engine->setInterrupted(false);
        }
        return m_timedOut;
    }

private:
    QJSEngine *m_engine;
    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_done = false;
    bool m_timedOut = false;
};

// Defines print / log / stop / console in the script's global scope. They forward to
// the JsHost object; objects are printed as JSON.
const char kPrelude[] = R"JS(
(function (host, g) {
    function num(v) {
        var s = String(v);
        if (!isFinite(v) || s.indexOf("e") < 0) return s;
        // Expand exponent notation (2.26e-6 -> 0.00000226).
        var m = s.match(/^(-?)(\d+)(?:\.(\d+))?e([+-]\d+)$/);
        if (!m) return s;
        var digits = m[2] + (m[3] || ""), exp = parseInt(m[4], 10) + m[2].length;
        if (exp <= 0) return m[1] + "0." + new Array(1 - exp).join("0") + digits;
        if (exp >= digits.length) return m[1] + digits + new Array(exp - digits.length + 1).join("0");
        return m[1] + digits.slice(0, exp) + "." + digits.slice(exp);
    }
    function fmt(v) {
        if (typeof v === "number") return num(v);
        if (typeof v === "string") return v;
        if (v === null) return "null";
        if (v === undefined) return "undefined";
        if (typeof v === "function") return "[function]";
        if (typeof v === "object") {
            try { return JSON.stringify(v); } catch (e) { return String(v); }
        }
        return String(v);
    }
    function join(args) {
        var parts = [];
        for (var i = 0; i < args.length; i++) parts.push(fmt(args[i]));
        return parts.join("\t");
    }
    var out = function () { host.print(join(arguments)); };
    g.print = out;
    g.log = out;
    g.stop = function (reason) {
        host.stop(reason === undefined || reason === null ? "" : String(reason));
    };
    g.resetstats = function () { host.resetStats(); };
    g.resetseed = function (seed) {
        host.resetSeed(seed === undefined || seed === null ? "" : String(seed));
    };
    g.vault = function (amount) {
        host.vault(amount === undefined || amount === null ? NaN : Number(amount));
    };
    g.console = {
        log: out, info: out, debug: out, warn: out,
        error: function () { host.error(join(arguments)); }
    };
})
)JS";

QString describeError(const QJSValue &err)
{
    QString msg = err.property(QStringLiteral("message")).toString();
    const QString name = err.property(QStringLiteral("name")).toString();
    if (msg.isEmpty())
        msg = err.toString();
    else if (!name.isEmpty() && name != QLatin1String("Error"))
        msg = QStringLiteral("%1: %2").arg(name, msg);
    const QJSValue line = err.property(QStringLiteral("lineNumber"));
    if (line.isNumber() && line.toInt() > 0)
        msg += QStringLiteral(" (line %1)").arg(line.toInt());
    return msg;
}
} // namespace

JsStrategy::JsStrategy() = default;

JsStrategy::~JsStrategy()
{
    close();
}

void JsStrategy::close()
{
    m_engine.reset(); // before the host object its wrapper points to
    m_host.reset();
}

void JsStrategy::publish(const char *name, double value)
{
    m_engine->globalObject().setProperty(QString::fromLatin1(name), value);
}

void JsStrategy::publishBool(const char *name, bool value)
{
    m_engine->globalObject().setProperty(QString::fromLatin1(name), value);
}

void JsStrategy::publishLastBet(double amount, double roll, double profit, double payout)
{
    QJSValue o = m_engine->newObject();
    o.setProperty(QStringLiteral("amount"), amount);
    o.setProperty(QStringLiteral("roll"), roll);
    o.setProperty(QStringLiteral("profit"), profit);
    o.setProperty(QStringLiteral("payout"), payout);
    m_engine->globalObject().setProperty(QStringLiteral("lastBet"), o);
}

template <typename Fn>
bool JsStrategy::runGuarded(const char *what, Fn &&fn)
{
    QJSValue result;
    bool timedOut = false;
    {
        Watchdog watchdog(m_engine.get(), kCallTimeLimitMs);
        result = fn();
        timedOut = watchdog.finish();
    }
    // A timeout that fired just after the call returned normally is not an error.
    if (result.isError()) {
        m_error = timedOut
            ? QStringLiteral("%1: script took longer than %2 ms and was aborted")
                  .arg(QString::fromUtf8(what))
                  .arg(kCallTimeLimitMs)
            : QStringLiteral("%1: %2").arg(QString::fromUtf8(what), describeError(result));
        return false;
    }
    return true;
}

// Global lookup. `var x`, `x = ...` and function declarations are properties of the
// global object; a top-level `let` / `const` is looked up by name as a fallback.
QJSValue JsStrategy::readGlobal(const char *name)
{
    QJSValue v = m_engine->globalObject().property(QString::fromLatin1(name));
    if (v.isUndefined()) {
        const QJSValue byName = m_engine->evaluate(
            QStringLiteral("typeof %1 === 'undefined' ? undefined : %1").arg(QString::fromLatin1(name)));
        if (!byName.isError())
            v = byName;
    }
    return v;
}

bool JsStrategy::readNumber(const char *name, double &out)
{
    const QJSValue v = readGlobal(name);
    if (v.isNumber()) {
        out = v.toNumber();
        return true;
    }
    if (v.isString()) { // "0.5" is accepted, like a numeric string in Lua
        bool ok = false;
        out = v.toString().toDouble(&ok);
        return ok;
    }
    return false;
}

// resetstats(): the run counters start over and the globals follow at once, so the
// script sees the new values right after the call.
void JsStrategy::onStatsReset(double balance)
{
    m_bets = m_wins = m_losses = m_streak = 0;
    m_wagered = 0.0;
    if (!m_engine)
        return;
    publish("startbalance", balance);
    publish("balance", balance);
    publish("profit", 0.0);
    publish("bets", 0);
    publish("wins", 0);
    publish("losses", 0);
    publish("wagered", 0.0);
    publish("currentstreak", 0);
}

JsStrategy::Status JsStrategy::init(const QString &source, double balance, double &nextBet,
                                    BotConfig &runCfg)
{
    close();
    m_error.clear();
    m_stopReason.clear();
    m_stop = false;
    m_bets = m_wins = m_losses = m_streak = 0;
    m_wagered = 0.0;

    m_engine = std::make_unique<QJSEngine>();
    m_host = std::make_unique<JsHost>(this);
    QJSEngine::setObjectOwnership(m_host.get(), QJSEngine::CppOwnership);

    // No engine extensions are installed (no console/gc/Qt objects); the script only
    // gets the ECMAScript built-ins plus print / log / stop / console below.
    QJSValue prelude = m_engine->evaluate(QString::fromLatin1(kPrelude),
                                                QStringLiteral("prelude"));
    if (prelude.isError() || !prelude.isCallable()) {
        m_error = QStringLiteral("could not set up the JavaScript engine: %1")
                      .arg(prelude.isError() ? describeError(prelude) : prelude.toString());
        close();
        return Status::Error;
    }
    const QJSValue setup = prelude.call({m_engine->newQObject(m_host.get()), m_engine->globalObject()});
    if (setup.isError()) {
        m_error = QStringLiteral("could not set up the JavaScript engine: %1").arg(describeError(setup));
        close();
        return Status::Error;
    }

    m_engine->globalObject().setProperty(QStringLiteral("game"), gameName(runCfg.game));
    if (!runCfg.casino.isEmpty())
        m_engine->globalObject().setProperty(QStringLiteral("casino"), runCfg.casino);
    if (!runCfg.currency.isEmpty())
        m_engine->globalObject().setProperty(QStringLiteral("currency"), runCfg.currency);
    publish("startbalance", balance);
    publishBool("win", false);
    publish("previousbet", 0.0);
    publishLastBet(0.0, 0.0, 0.0, 0.0);
    publish("lastprofit", 0.0);
    publish("roll", 0.0);
    publish("balance", balance);
    publish("profit", 0.0);
    publish("bets", 0);
    publish("wins", 0);
    publish("losses", 0);
    publish("wagered", 0.0);
    publish("currentstreak", 0);

    if (!runGuarded("script", [&] {
            return m_engine->evaluate(source, QStringLiteral("strategy"), 1);
        })) {
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

JsStrategy::Status JsStrategy::onBet(const BetInfo &info, double &nextBet, BotConfig &runCfg)
{
    if (!m_engine)
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
    publishLastBet(info.bet, info.roll, info.profit, info.payout);
    publish("lastprofit", info.profit);
    publish("roll", info.roll);
    publish("balance", info.balance);
    publish("profit", info.runProfit);
    publish("bets", double(m_bets));
    publish("wins", double(m_wins));
    publish("losses", double(m_losses));
    publish("wagered", m_wagered);
    publish("currentstreak", double(m_streak));

    QJSValue dobet = readGlobal("dobet"); // non-const: QJSValue::call() is non-const in Qt 5
    if (dobet.isCallable()) {
        if (!runGuarded("dobet()", [&] { return dobet.call(); }))
            return Status::Error;
    } // no dobet(): keep the values the script set

    if (m_stop)
        return Status::Stop;
    return readOutputs(nextBet, runCfg) ? Status::Ok : Status::Error;
}

// Reads game / nextbet and chance + bethigh (dice) or target (limbo) back from the script.
bool JsStrategy::readOutputs(double &nextBet, BotConfig &runCfg)
{
    // ---- game
    const QJSValue game = readGlobal("game");
    if (game.isUndefined() || game.isNull()) {
        // keep the current game
    } else if (game.isString()) {
        const QString g = game.toString().trimmed().toLower();
        if (g == QLatin1String("dice")) {
            runCfg.game = Game::Dice;
        } else if (g == QLatin1String("limbo")) {
            runCfg.game = Game::Limbo;
        } else {
            m_error = QStringLiteral("game must be \"dice\" or \"limbo\" (got \"%1\")").arg(g);
            return false;
        }
    } else {
        m_error = QStringLiteral("game must be the string \"dice\" or \"limbo\"");
        return false;
    }

    // ---- casino (optional): "stake", "shuffle", "goated" or "wolfbet"
    const QJSValue casino = readGlobal("casino");
    if (casino.isUndefined() || casino.isNull()) {
        // keep the current casino
    } else if (casino.isString()) {
        const QString k = casino.toString().trimmed().toLower();
        if (k == QLatin1String("stake") || k == QLatin1String("shuffle") || k == QLatin1String("goated")
            || k == QLatin1String("wolfbet")) {
            runCfg.casino = k;
        } else if (!k.isEmpty()) {
            m_error = QStringLiteral("casino must be \"stake\", \"shuffle\", \"goated\" or \"wolfbet\" (got \"%1\")").arg(k);
            return false;
        }
    } else {
        m_error = QStringLiteral("casino must be the string \"stake\", \"shuffle\", \"goated\" or \"wolfbet\"");
        return false;
    }

    // ---- currency (optional): the currency code to bet in, e.g. "btc" / "usdt"
    const QJSValue currency = readGlobal("currency");
    if (currency.isUndefined() || currency.isNull()) {
        // keep the current currency
    } else if (currency.isString()) {
        const QString c = currency.toString().trimmed().toLower();
        if (!c.isEmpty())
            runCfg.currency = c;
    } else {
        m_error = QStringLiteral("currency must be a string such as \"btc\" or \"usdt\"");
        return false;
    }

    static const char *kDeclHint =
        " (assign it as a plain global, e.g. `nextbet = 0.0001;` or `var nextbet = 0.0001;` - "
        "top-level let / const are not visible to the bot)";

    // ---- nextbet
    double bet = 0.0;
    if (!readNumber("nextbet", bet)) {
        m_error = QStringLiteral("nextbet must be set to a number by the script%1")
                      .arg(QString::fromLatin1(kDeclHint));
        return false;
    }
    if (!std::isfinite(bet) || bet < 0.0) {
        m_error = QStringLiteral("nextbet must be a finite number >= 0 (got %1)").arg(bet);
        return false;
    }

    if (runCfg.game == Game::Limbo) {
        // Limbo: only `target` is used; chance / bethigh are ignored.
        double target = 0.0;
        if (!readNumber("target", target)) {
            m_error = QStringLiteral("target must be set to a number by the script (limbo)");
            return false;
        }
        if (!std::isfinite(target)) {
            m_error = QStringLiteral("target must be a finite number");
            return false;
        }
        runCfg.multiplierTarget = round2(std::clamp(target, BotConstants::kMinLimboTarget,
                                                    BotConstants::kMaxLimboTarget));
        nextBet = floor8(bet);
        return true;
    }

    double chance = 0.0;
    if (!readNumber("chance", chance)) {
        m_error = QStringLiteral("chance must be set to a number by the script");
        return false;
    }
    if (!std::isfinite(chance)) {
        m_error = QStringLiteral("chance must be a finite number");
        return false;
    }

    const bool high = readGlobal("bethigh").toBool();

    chance = round2(std::clamp(chance, kMinChance, kMaxChance));
    runCfg.side = high ? BetSide::Over : BetSide::Under;
    runCfg.target = round2(high ? 100.0 - chance : chance);
    nextBet = floor8(bet);
    return true;
}
