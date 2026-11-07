#include "bot/StakeFair.h"

#include "bot/SimulatedProvider.h"
#include "bot/StakeGames.h"

#include <QCryptographicHash>
#include <QMap>
#include <QSet>
#include <QMessageAuthenticationCode>
#include <QStringList>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <functional>

namespace StakeFair
{
namespace
{
const char *const kRanks[13] = {"2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K", "A"};
const char *const kSuits[4] = {"D", "H", "S", "C"};

double round2(double v) { return std::round(v * 100.0 + 1e-9) / 100.0; }
double floor2(double v) { return std::floor(v * 100.0 + 1e-9) / 100.0; }
double round4(double v) { return std::round(v * 10000.0) / 10000.0; }
double round6(double v) { return std::round(v * 1e6) / 1e6; }

double choose(int n, int k)
{
    if (k < 0 || k > n)
        return 0.0;
    double r = 1.0;
    for (int i = 1; i <= k; ++i)
        r = r * double(n - k + i) / double(i);
    return r;
}

QString text(const QJsonValue &v, const QString &def = QString())
{
    if (v.isString())
        return v.toString();
    if (v.isDouble())
        return QString::number(v.toDouble());
    return def;
}

double number(const QJsonValue &v, double def = 0.0)
{
    if (v.isDouble())
        return v.toDouble();
    if (v.isString()) {
        bool ok = false;
        const double d = v.toString().toDouble(&ok);
        return ok ? d : def;
    }
    return def;
}

// "1,2,3" / [1,2,3] -> ints
QVector<int> intList(const QJsonValue &v)
{
    QVector<int> out;
    if (v.isArray()) {
        for (const QJsonValue &x : v.toArray())
            out << int(number(x, -1));
    } else if (v.isString()) {
        for (const QString &p : v.toString().split(QLatin1Char(','), Qt::SkipEmptyParts))
            out << p.trimmed().toInt();
    } else if (v.isDouble()) {
        out << v.toInt();
    }
    return out;
}

QStringList stringList(const QJsonValue &v)
{
    QStringList out;
    if (v.isArray()) {
        for (const QJsonValue &x : v.toArray())
            out << text(x).trimmed().toLower();
    } else if (v.isString()) {
        for (const QString &p : v.toString().split(QLatin1Char(','), Qt::SkipEmptyParts))
            out << p.trimmed().toLower();
    }
    return out;
}

// ---- payout tables ------------------------------------------------------------------------
// Keno: [risk][picks - 1] = multiplier for 0 .. picks hits (Stake's tables, ~99% RTP each).
const QVector<QVector<double>> &kenoTable(const QString &risk)
{
    static const QVector<QVector<double>> classic = {
        {0, 3.96}, {0, 1.9, 4.5}, {0, 1, 3.1, 10.4}, {0, 0.8, 1.8, 5, 22.5},
        {0, 0.25, 1.4, 4.1, 16.5, 36}, {0, 0, 1, 3.68, 7, 16.5, 40}, {0, 0, 0.47, 3, 4.5, 14, 31, 60},
        {0, 0, 0, 2.2, 4, 13, 22, 55, 70}, {0, 0, 0, 1.55, 3, 8, 15, 44, 60, 85},
        {0, 0, 0, 1.4, 2.25, 4.5, 8, 17, 50, 80, 100}};
    static const QVector<QVector<double>> low = {
        {0.7, 1.85}, {0, 2, 3.8}, {0, 1.1, 1.38, 26}, {0, 0, 2.2, 7.9, 90}, {0, 0, 1.5, 4.2, 13, 300},
        {0, 0, 1.1, 2, 6.2, 100, 700}, {0, 0, 1.1, 1.6, 3.5, 15, 225, 700},
        {0, 0, 1.1, 1.5, 2, 5.5, 39, 100, 800}, {0, 0, 1.1, 1.3, 1.7, 2.5, 7.5, 50, 250, 1000},
        {0, 0, 1.1, 1.2, 1.3, 1.8, 3.5, 13, 50, 250, 1000}};
    static const QVector<QVector<double>> medium = {
        {0.4, 2.75}, {0, 1.8, 5.1}, {0, 0, 2.8, 50}, {0, 0, 1.7, 10, 100}, {0, 0, 1.4, 4, 14, 390},
        {0, 0, 0, 3, 9, 180, 710}, {0, 0, 0, 2, 7, 30, 400, 800}, {0, 0, 0, 2, 4, 11, 67, 400, 900},
        {0, 0, 0, 2, 2.5, 5, 15, 100, 500, 1000}, {0, 0, 0, 1.6, 2, 4, 7, 26, 100, 500, 1000}};
    static const QVector<QVector<double>> high = {
        {0, 3.96}, {0, 0, 17.1}, {0, 0, 0, 81.5}, {0, 0, 0, 10, 259}, {0, 0, 0, 4.5, 48, 450},
        {0, 0, 0, 0, 11, 350, 710}, {0, 0, 0, 0, 7, 90, 400, 800}, {0, 0, 0, 0, 5, 20, 270, 600, 900},
        {0, 0, 0, 0, 4, 11, 56, 500, 800, 1000}, {0, 0, 0, 0, 3.5, 8, 13, 63, 500, 800, 1000}};
    static const QVector<QVector<double>> none;
    const QString r = risk.trimmed().toLower();
    if (r == QLatin1String("classic")) return classic;
    if (r == QLatin1String("low")) return low;
    if (r == QLatin1String("medium")) return medium;
    if (r == QLatin1String("high")) return high;
    return none;
}

// Plinko: [rows - 8][risk] = the multipliers from the edge slot to the middle one (the board is
// symmetric). Low / medium / high only: the expert table is not published.
const QVector<double> &plinkoHalf(int rows, int risk)
{
    static const QVector<QVector<QVector<double>>> t = {
        {{5.6, 2.1, 1.1, 1, 0.5}, {13, 3, 1.3, 0.7, 0.4}, {29, 4, 1.5, 0.3, 0.2}},
        {{5.6, 2, 1.6, 1, 0.7}, {18, 4, 1.7, 0.9, 0.5}, {43, 7, 2, 0.6, 0.2}},
        {{8.9, 3, 1.4, 1.1, 1, 0.5}, {22, 5, 2, 1.4, 0.6, 0.4}, {76, 10, 3, 0.9, 0.3, 0.2}},
        {{8.4, 3, 1.9, 1.3, 1, 0.7}, {24, 6, 3, 1.8, 0.7, 0.5}, {120, 14, 5.2, 1.4, 0.4, 0.2}},
        {{10, 3, 1.6, 1.4, 1.1, 1, 0.5}, {33, 11, 4, 2, 1.1, 0.6, 0.3}, {170, 24, 8.1, 2, 0.7, 0.2, 0.2}},
        {{8.1, 4, 3, 1.9, 1.2, 0.9, 0.7}, {43, 13, 6, 3, 1.3, 0.7, 0.4}, {260, 37, 11, 4, 1, 0.2, 0.2}},
        {{7.1, 4, 1.9, 1.4, 1.3, 1.1, 1, 0.5}, {58, 15, 7, 4, 1.9, 1, 0.5, 0.2},
         {420, 56, 18, 5, 1.9, 0.3, 0.2, 0.2}},
        {{15, 8, 3, 2, 1.5, 1.1, 1, 0.7}, {88, 18, 11, 5, 3, 1.3, 0.5, 0.3},
         {620, 83, 27, 8, 3, 0.5, 0.2, 0.2}},
        {{16, 9, 2, 1.4, 1.4, 1.2, 1.1, 1, 0.5}, {110, 41, 10, 5, 3, 1.5, 1, 0.5, 0.3},
         {1000, 130, 26, 9, 4, 2, 0.2, 0.2, 0.2}}};
    static const QVector<double> none;
    if (rows < 8 || rows > 16 || risk < 0 || risk > 2)
        return none;
    return t.at(rows - 8).at(risk);
}

// Plinko expert: [rows - 8] = every slot's multiplier, left to right.
const QVector<double> &plinkoExpert(int rows)
{
    static const QVector<QVector<double>> t = {
        {50, 4.6, 1.1, 0.1, 0.1, 0.1, 1.1, 4.6, 50},
        {100, 7.8, 1.5, 0.2, 0.1, 0.1, 0.2, 1.5, 7.8, 100},
        {201, 11, 2, 0.6, 0.1, 0.1, 0.1, 0.6, 2, 11, 201},
        {324, 16, 4, 1.1, 0.2, 0.1, 0.1, 0.2, 1.1, 4, 16, 324},
        {619, 30, 6, 1.5, 0.4, 0.1, 0.1, 0.1, 0.4, 1.5, 6, 30, 619},
        {1012, 52, 10, 3, 0.6, 0.1, 0.1, 0.1, 0.1, 0.6, 3, 10, 52, 1012},
        {2369, 80, 16, 3, 1.2, 0.2, 0.1, 0.1, 0.1, 0.2, 1.2, 3, 16, 80, 2369},
        {5000, 125, 23, 6, 1.8, 0.2, 0.1, 0.1, 0.1, 0.1, 0.2, 1.8, 6, 23, 125, 5000},
        {10000, 216, 26, 7, 2.5, 1.1, 0.1, 0.1, 0.1, 0.1, 0.1, 1.1, 2.5, 7, 26, 216, 10000}};
    static const QVector<double> none;
    if (rows < 8 || rows > 16)
        return none;
    return t.at(rows - 8);
}

// Snakes: [difficulty][roll 0 .. 4][dice total - 2] = the roll's factor (0 = a snake). The
// factors of the rolls played multiply.
const QVector<QVector<double>> *snakesTable(const QString &difficulty)
{
    static const QVector<double> easy1 = {2, 1.3, 1.2, 1.1, 1.01, 0, 1.01, 1.1, 1.2, 1.3, 2};
    static const QVector<double> easyN = {2, 1.3, 1.2, 1.1, 1.08, 0, 1.08, 1.1, 1.2, 1.3, 2};
    static const QVector<double> medium1 = {4, 2.5, 1.4, 1.11, 0, 0, 0, 1.11, 1.4, 2.5, 4};
    static const QVector<double> mediumN = {4, 2.5, 1.4, 1.2, 0, 0, 0, 1.2, 1.4, 2.5, 4};
    static const QVector<double> hard1 = {7.5, 3, 1.38, 0, 0, 0, 0, 0, 1.38, 3, 7.5};
    static const QVector<double> hardN = {7.5, 3, 1.5, 0, 0, 0, 0, 0, 1.5, 3, 7.5};
    static const QVector<double> expert1 = {10, 3.82, 0, 0, 0, 0, 0, 0, 0, 3.82, 10};
    static const QVector<double> expertN = {10, 4, 0, 0, 0, 0, 0, 0, 0, 4, 10};
    static const QVector<double> master1 = {17.64, 0, 0, 0, 0, 0, 0, 0, 0, 0, 17.64};
    static const QVector<double> masterN = {18, 0, 0, 0, 0, 0, 0, 0, 0, 0, 18};
    static const QVector<QVector<double>> e = {easy1, easyN, easyN, easyN, easyN};
    static const QVector<QVector<double>> m = {medium1, mediumN, mediumN, mediumN, mediumN};
    static const QVector<QVector<double>> h = {hard1, hardN, hardN, hardN, hardN};
    static const QVector<QVector<double>> x = {expert1, expertN, expertN, expertN, expertN};
    static const QVector<QVector<double>> ms = {master1, masterN, masterN, masterN, masterN};
    const QString d = difficulty.trimmed().toLower();
    if (d == QLatin1String("easy")) return &e;
    if (d == QLatin1String("medium")) return &m;
    if (d == QLatin1String("hard")) return &h;
    if (d == QLatin1String("expert")) return &x;
    if (d == QLatin1String("master")) return &ms;
    return nullptr;
}

int die(double f)
{
    return std::min(5, int(f * 6.0)) + 1;
}

// Packs: 240 cards in groups {first id, last id, cumulative start, chance of each card, multiplier};
// a float picks the first card whose cumulative end reaches it.
struct PackCard
{
    int id = 1;
    double end = 1.0;
    double multiplier = 0.0;
};

const QVector<PackCard> &packCards()
{
    static const QVector<PackCard> cards = [] {
        struct Group { int first, last; double start, step, mult; };
        static const Group groups[] = {
            {240, 211, 0.0, 0.012, 0.01}, {210, 191, 0.36, 0.011, 0.02}, {190, 161, 0.58, 0.005, 0.03},
            {160, 141, 0.73, 0.005, 0.04}, {140, 131, 0.83, 0.003, 0.10}, {130, 121, 0.86, 0.003, 0.15},
            {120, 111, 0.89, 0.003, 0.20}, {110, 101, 0.92, 0.002, 0.30}, {100, 91, 0.94, 0.002, 0.50},
            {90, 81, 0.96, 0.002, 0.80}, {80, 71, 0.98, 0.0003, 1.00}, {70, 61, 0.983, 0.0003, 1.20},
            {60, 56, 0.986, 0.0006, 2.00}, {55, 51, 0.989, 0.0006, 3.00}, {50, 46, 0.992, 0.0006, 4.00},
            {45, 41, 0.995, 0.0003, 8.00}, {40, 36, 0.9965, 0.0002, 10.00}, {35, 32, 0.9975, 0.0002, 12.00},
            {31, 31, 0.9983, 0.0002, 32.00}, {30, 26, 0.9985, 0.0002, 15.00}, {25, 21, 0.9995, 0.00006, 50.00},
            {20, 16, 0.9998, 0.000024, 75.00}, {15, 11, 0.99992, 0.00001, 150.00},
            {10, 6, 0.99997, 0.000003, 500.00}, {5, 4, 0.999985, 0.000005, 650.00},
            {3, 2, 0.999995, 0.00000245, 1000.00}, {1, 1, 0.9999999, 0.0000001, 10000.00}};
        QVector<PackCard> out;
        for (const Group &g : groups)
            for (int id = g.first, i = 0; id >= g.last; --id, ++i)
                out.append({id, g.start + g.step * (i + 1), g.mult});
        return out;
    }();
    return cards;
}

PackCard packCard(double f)
{
    for (const PackCard &c : packCards())
        if (f <= c.end)
            return c;
    return packCards().last(); // id 1
}

// ---- weighted tables (Cases, Bars, Tarot): {multiplier, chance}; a float picks the first row
// whose cumulative end (rounded to 8 decimals) reaches it.
struct WeightRow
{
    double multiplier;
    double chance;
};

double round8(double v) { return std::round(v * 1e8) / 1e8; }

double pickWeighted(double f, const QVector<WeightRow> &rows)
{
    double cumulative = 0.0;
    for (int i = 0; i < rows.size(); ++i) {
        cumulative += rows.at(i).chance;
        if (f <= round8(cumulative) || i == rows.size() - 1)
            return rows.at(i).multiplier;
    }
    return 0.0;
}

const QVector<WeightRow> *casesTable(const QString &difficulty)
{
    static const QVector<WeightRow> easy = {{0.1, 0.41}, {0.4, 0.35}, {1.09, 0.10}, {2, 0.07}, {3, 0.04},
                                           {10, 0.02}, {23, 0.01}};
    static const QVector<WeightRow> medium = {{0, 0.30}, {0.2, 0.27}, {0.4, 0.18}, {1.5, 0.13}, {2, 0.06},
                                             {3.5, 0.03}, {7.5, 0.015}, {10, 0.0085}, {15, 0.004}, {41, 0.0015},
                                             {115, 0.001}};
    static const QVector<WeightRow> hard = {{0, 0.35}, {0.2, 0.25}, {0.4, 0.12}, {0.8, 0.10}, {1.5, 0.10},
                                           {3, 0.05}, {8, 0.02}, {10, 0.004}, {15, 0.003}, {35, 0.002},
                                           {50, 0.0004}, {100, 0.0003}, {250, 0.00015}, {495, 0.0001},
                                           {1000, 0.00005}};
    static const QVector<WeightRow> expert = {{0, 0.3539}, {0.15, 0.20}, {0.3, 0.15}, {0.7, 0.15}, {1.5, 0.08},
                                             {5, 0.03}, {7, 0.02}, {10, 0.008}, {15, 0.005}, {20, 0.002},
                                             {50, 0.0004}, {75, 0.0003}, {100, 0.0002}, {250, 0.0001},
                                             {460, 0.00005}, {850, 0.00003}, {1500, 0.000012},
                                             {3500, 0.000006}, {10000, 0.000002}};
    const QString d = difficulty.trimmed().toLower();
    if (d == QLatin1String("easy")) return &easy;
    if (d == QLatin1String("medium")) return &medium;
    if (d == QLatin1String("hard")) return &hard;
    if (d == QLatin1String("expert")) return &expert;
    return nullptr;
}

const QVector<WeightRow> *barsTable(const QString &difficulty)
{
    static const QVector<WeightRow> easy = {{0.4, 0.35}, {0.6, 0.35}, {1.2, 0.15}, {1.5, 0.08}, {3, 0.05}, {9, 0.02}};
    static const QVector<WeightRow> medium = {{0.3, 0.35}, {0.6, 0.35}, {1.2, 0.15}, {1.4, 0.10}, {3, 0.03},
                                             {6, 0.015}, {33, 0.005}};
    static const QVector<WeightRow> hard = {{0.1, 0.47}, {0.3, 0.31}, {1.2, 0.12}, {2.4, 0.06}, {6, 0.03},
                                           {12, 0.008}, {75, 0.0018}, {705, 0.0002}};
    static const QVector<WeightRow> expert = {{0, 0.50}, {0.2, 0.40}, {1.5, 0.06}, {6, 0.025}, {9, 0.01},
                                             {30, 0.004}, {150, 0.0008}, {1200, 0.00015}, {3000, 0.00005}};
    const QString d = difficulty.trimmed().toLower();
    if (d == QLatin1String("easy")) return &easy;
    if (d == QLatin1String("medium")) return &medium;
    if (d == QLatin1String("hard")) return &hard;
    if (d == QLatin1String("expert")) return &expert;
    return nullptr;
}

// Bars: 30 bars; the selected ones (1 to 5) pay the average of their multipliers (in cents).
double barsMultiplier(const QVector<double> &bars, const QVector<int> &selected)
{
    if (selected.isEmpty())
        return 0.0;
    double cents = 0.0;
    for (int i : selected)
        if (i >= 0 && i < bars.size())
            cents += std::round(bars.at(i) * 100.0);
    return round8(cents / (100.0 * selected.size()));
}

struct TarotTables
{
    QVector<WeightRow> major;
    QVector<WeightRow> minor;
};

const TarotTables *tarotTables(const QString &difficulty)
{
    static const TarotTables easy = {{{1, 0.51}, {2, 0.22}, {3, 0.10}, {4, 0.10}, {5, 0.07}},
                                     {{0, 0.10}, {0.4, 0.60}, {1.1, 0.10}, {1.5, 0.10}, {2, 0.10}}};
    static const TarotTables medium = {{{1, 0.55}, {2, 0.23}, {3, 0.13}, {5, 0.06}, {10, 0.03}},
                                       {{0, 0.19}, {0.3, 0.50}, {0.6, 0.15}, {2, 0.09}, {4, 0.07}}};
    static const TarotTables hard = {{{1, 0.60}, {2, 0.26}, {4, 0.10}, {8, 0.03}, {24, 0.01}},
                                     {{0, 0.26}, {0.15, 0.40}, {0.55, 0.20}, {2, 0.09}, {7, 0.05}}};
    static const TarotTables expert = {{{1, 0.60}, {2, 0.30}, {5, 0.08}, {10, 0.015}, {50, 0.005}},
                                       {{0, 0.47}, {0.15, 0.20}, {0.35, 0.20}, {3, 0.10}, {10, 0.03}}};
    const QString d = difficulty.trimmed().toLower();
    if (d == QLatin1String("easy")) return &easy;
    if (d == QLatin1String("medium")) return &medium;
    if (d == QLatin1String("hard")) return &hard;
    if (d == QLatin1String("expert")) return &expert;
    return nullptr;
}

// Tarot: minor (left), major (centre), minor (right); the three multiply (in cents).
double tarotMultiplier(const TarotTables &t, const QVector<double> &f, double cards[3])
{
    cards[0] = pickWeighted(f.at(0), t.minor);
    cards[1] = pickWeighted(f.at(1), t.major);
    cards[2] = pickWeighted(f.at(2), t.minor);
    return std::round(cards[0] * 100.0) * std::round(cards[1] * 100.0) * std::round(cards[2] * 100.0) / 1e6;
}

// Darts: rotation float (18 sectors of 20 degrees from the top, clockwise) and distance float
// (radius = sqrt(f) / 2 on a board of radius 0.5). Rings: green bullseye, the coloured target
// band (each sector's colour), grey inside the band, black elsewhere.
struct DartsLevel
{
    double greyInner, bandInner, bandOuter, greenRadius;
    double dark, light, yellow, orange, red, green; // multipliers
    const char *sectors[18];
};

const DartsLevel *dartsLevel(const QString &difficulty)
{
    static const DartsLevel easy = {0.275, 0.375, 0.45, 0.0625, 0.5, 0.8, 1.2, 1.5, 2.7, 8.5,
        {"yellow", "orange", "yellow", "red", "yellow", "orange", "yellow", "red", "yellow", "orange", "yellow",
         "red", "yellow", "orange", "yellow", "orange", "yellow", "red"}};
    static const DartsLevel medium = {0.225, 0.35, 0.4, 0.05, 0.4, 0.6, 1.3, 3.1, 6, 16,
        {"yellow", "orange", "yellow", "red", "yellow", "orange", "yellow", "red", "yellow", "orange", "yellow",
         "red", "yellow", "orange", "yellow", "orange", "yellow", "red"}};
    static const DartsLevel hard = {0.2, 0.33, 0.375, 0.03, 0.2, 0.5, 2.5, 3.6, 8.8, 63,
        {"yellow", "orange", "yellow", "orange", "yellow", "red", "yellow", "orange", "yellow", "orange", "yellow",
         "red", "yellow", "orange", "yellow", "orange", "yellow", "red"}};
    static const DartsLevel expert = {0.25, 0.355, 0.375, 0.01, 0.1, 0.5, 4.8, 9.6, 42, 500,
        {"yellow", "orange", "yellow", "yellow", "red", "yellow", "yellow", "orange", "yellow", "yellow", "orange",
         "yellow", "yellow", "red", "yellow", "yellow", "orange", "yellow"}};
    const QString d = difficulty.trimmed().toLower();
    if (d == QLatin1String("easy")) return &easy;
    if (d == QLatin1String("medium")) return &medium;
    if (d == QLatin1String("hard")) return &hard;
    if (d == QLatin1String("expert")) return &expert;
    return nullptr;
}

double dartsResult(const DartsLevel &l, double rotation, double distance, QString *colour)
{
    QString c;
    double m = 0.0;
    if (distance <= 4 * l.greenRadius * l.greenRadius) {
        c = QStringLiteral("green");
        m = l.green;
    } else if (distance >= 4 * l.bandInner * l.bandInner && distance <= 4 * l.bandOuter * l.bandOuter) {
        c = QLatin1String(l.sectors[std::min(17, int(rotation * 18.0))]);
        m = c == QLatin1String("red") ? l.red : c == QLatin1String("orange") ? l.orange : l.yellow;
    } else if (distance >= 4 * l.greyInner * l.greyInner && distance < 4 * l.bandInner * l.bandInner) {
        c = QStringLiteral("grey");
        m = l.light;
    } else {
        c = QStringLiteral("black");
        m = l.dark;
    }
    if (colour)
        *colour = c;
    return m;
}

// ---- Tome of Life / Scarab Spin (the same reels and pay table): 5 reels, 3 rows, 1 to 20 lines.
const QVector<QStringList> &tomeReels()
{
    static const QVector<QStringList> reels = {
        QStringLiteral("five,queen,ten,three,jack,ace,four,ten,queen,two,nine,ten,scatter,queen,jack,wild,queen,ace,two,"
                "king,one,queen,five,king,four,ten,one,nine,three,ten").split(QLatin1Char(',')),
        QStringLiteral("queen,jack,three,queen,ace,wild,queen,jack,five,nine,queen,three,king,one,jack,two,ten,one,nine,"
                "scatter,ace,four,ten,king,two,jack,queen,five,jack,four").split(QLatin1Char(',')),
        QStringLiteral("king,nine,five,ten,three,king,jack,wild,queen,ten,three,nine,jack,one,ace,ten,four,ace,king,one,"
                "nine,ten,two,queen,nine,four,king,five,nine,scatter").split(QLatin1Char(',')),
        QStringLiteral("king,nine,five,ace,two,jack,one,ten,four,jack,three,queen,five,jack,ten,four,ace,queen,scatter,"
                "ten,king,three,jack,four,nine,ace,two,nine,ace,wild").split(QLatin1Char(',')),
        QStringLiteral("three,ten,jack,two,king,ten,wild,king,nine,one,ten,five,ace,jack,scatter,nine,ace,four,queen,ace,"
                "four,ten,five,queen,three,nine,wild,jack,two,queen,king,five,jack,ten,three,queen,four,nine,one,"
                "ace,nine").split(QLatin1Char(','))};
    return reels;
}

struct SymbolPay
{
    const char *symbol;
    double pay[6]; // by count 0 .. 5
};

const QVector<SymbolPay> &tomePays()
{
    // In the model's order (ties keep the first).
    static const QVector<SymbolPay> pays = {
        {"wild", {0, 0, 10, 200, 2000, 10000}}, {"one", {0, 0, 2, 25, 100, 750}}, {"two", {0, 0, 2, 25, 100, 750}},
        {"three", {0, 0, 0, 15, 100, 400}}, {"four", {0, 0, 0, 10, 75, 250}}, {"five", {0, 0, 0, 10, 50, 250}},
        {"ace", {0, 0, 0, 10, 50, 125}}, {"king", {0, 0, 0, 5, 50, 100}}, {"queen", {0, 0, 0, 5, 25, 100}},
        {"jack", {0, 0, 0, 5, 25, 100}}, {"ten", {0, 0, 0, 5, 25, 100}}, {"nine", {0, 0, 2, 5, 25, 100}}};
    return pays;
}

const int kTomeLines[20][5] = {{1,1,1,1,1},{0,0,0,0,0},{2,2,2,2,2},{0,1,2,1,0},{2,1,0,1,2},{0,0,1,2,2},{2,2,1,0,0},
    {1,0,1,2,1},{1,2,1,0,1},{0,1,1,1,2},{2,1,1,1,0},{1,0,0,1,2},{1,2,2,1,0},{1,1,0,1,2},{1,1,2,1,0},{0,0,1,2,1},
    {2,2,1,0,1},{1,0,1,2,2},{1,2,1,0,0},{0,0,0,1,2}};

struct SlotSpin
{
    double multiplier = 0.0;
    int scatters = 0;
};

SlotSpin tomeSpin(const QVector<double> &f, int lines, bool bonus)
{
    const QVector<QStringList> &reels = tomeReels();
    QString grid[5][3];
    int scatters = 0;
    for (int r = 0; r < 5; ++r) {
        const int len = reels.at(r).size();
        const int stop = int(f.at(r) * len);
        for (int row = 0; row < 3; ++row) {
            grid[r][row] = reels.at(r).at(((stop - 1 + row) % len + len) % len);
            if (grid[r][row] == QLatin1String("scatter"))
                ++scatters;
        }
    }
    double units = 0.0;
    for (int l = 0; l < lines; ++l) {
        double best = 0.0;
        for (const SymbolPay &p : tomePays()) {
            const bool isWild = qstrcmp(p.symbol, "wild") == 0;
            int count = 0;
            bool hasWild = false;
            for (int r = 0; r < 5; ++r) {
                const QString &s = grid[r][kTomeLines[l][r]];
                if (s == QLatin1String(p.symbol) || (!isWild && s == QLatin1String("wild"))) {
                    ++count;
                    hasWild = hasWild || s == QLatin1String("wild");
                } else {
                    break;
                }
            }
            double pay = p.pay[count];
            if (!isWild && hasWild)
                pay *= 2;
            if (bonus && !(isWild && count == 5))
                pay *= 3;
            if (pay > best)
                best = pay;
        }
        units += best;
    }
    static const double scatterPay[6] = {0, 0, 2, 6, 50, 500};
    const double scatterMult = scatterPay[std::min(scatters, 5)] * (bonus ? 3 : 1);
    SlotSpin out;
    out.scatters = scatters;
    out.multiplier = (units + scatterMult * lines) / lines;
    return out;
}

// A whole round: the base spin, then 15 free spins (x3) on 3+ scatters, +15 on a retrigger, at
// most 180. Returns the total multiplier; `spins` / `freeSpins` count what was played.
// `buy`: Tome of Life's bonus buy - the 15 free spins straight away (no base spin), from the
// first floats of the bet.
double tomeRound(const Seeds &s, int lines, int *freeSpins, bool buy = false)
{
    int remaining = buy ? 15 : 0;
    int awarded = remaining;
    double units = 0.0;
    int index = 0;
    for (; (index == 0 && !buy) || remaining > 0; ++index) {
        const SlotSpin sp = tomeSpin(floats(s, 5, index * 20), lines, buy || index > 0);
        if (index == 0 && !buy) {
            remaining = sp.scatters >= 3 ? 15 : 0;
            awarded = remaining;
        } else {
            --remaining;
            if (sp.scatters >= 3) {
                const int added = std::min(15, 180 - awarded);
                remaining += added;
                awarded += added;
            }
        }
        units += std::round(sp.multiplier * lines);
    }
    if (freeSpins)
        *freeSpins = buy ? index : index - 1;
    return units / lines;
}

// ---- Blue Samurai: 3-4-4-4-3 reels, 40 lines; bonus spins (10 per 3+ scatters, at most 180) and
// the special feature (a full outer column of samurai: 5 sticky-samurai spins).
const char *samuraiSymbol(double v, bool outer)
{
    struct Row { const char *s; double end; };
    static const Row outerRows[] = {{"s1", .075}, {"s2", .15}, {"s3", .23}, {"s4", .31}, {"s5", .39}, {"s6", .47},
                                    {"s7", .55}, {"s8", .63}, {"s9", .78}, {"samurai", .95}, {"wild", 1}};
    static const Row innerRows[] = {{"s1", .09}, {"s2", .18}, {"s3", .25}, {"s4", .31}, {"s5", .4}, {"s6", .48},
                                    {"s7", .56}, {"s8", .625}, {"s9", .775}, {"samurai", .895}, {"scatter", .935},
                                    {"wild", 1}};
    if (outer) {
        for (const Row &r : outerRows)
            if (v < r.end)
                return r.s;
        return "wild";
    }
    for (const Row &r : innerRows)
        if (v < r.end)
            return r.s;
    return "wild";
}

double samuraiPay(const QString &symbol, int count, bool bonus)
{
    struct Pay { const char *s; double base[6]; double bonus[6]; };
    static const Pay pays[] = {
        {"s1", {0, 0, 0, 0, 1.5, 3}, {0, 0, 0, 1, 2, 8}}, {"s2", {0, 0, 0, 0, 1.5, 3}, {0, 0, 0, 1, 2, 8}},
        {"s3", {0, 0, 0, 0, 1.5, 4}, {0, 0, 0, 1.5, 3, 10}}, {"s4", {0, 0, 0, 0, 1.5, 4}, {0, 0, 0, 1.5, 3, 10}},
        {"s5", {0, 0, 0, 0, 2, 4}, {0, 0, 0, 2, 4, 12}}, {"s6", {0, 0, 0, 0, 2, 4}, {0, 0, 0, 2, 4, 12}},
        {"s7", {0, 0, 0, 0, 2, 4}, {0, 0, 0, 2, 4, 12}}, {"s8", {0, 0, 0, 0, 1.5, 3}, {0, 0, 0, 1.5, 3, 10}},
        {"s9", {0, 0, 0, 0, 1.5, 3}, {0, 0, 0, 1.5, 3, 10}}, {"wild", {0, 0, 0, 0, 2.5, 12.5}, {0, 0, 0, 5, 10, 20}},
        {"samurai", {0, 0, 0, 0.5, 1, 1.5}, {0, 0, 0, 0.5, 2, 4}}};
    for (const Pay &p : pays)
        if (symbol == QLatin1String(p.s))
            return bonus ? p.bonus[count] : p.base[count];
    return 0.0;
}

const QStringList &samuraiOrder()
{
    static const QStringList order = {QStringLiteral("s1"), QStringLiteral("s2"), QStringLiteral("s3"),
                                      QStringLiteral("s4"), QStringLiteral("s5"), QStringLiteral("s6"),
                                      QStringLiteral("s7"), QStringLiteral("s8"), QStringLiteral("s9"),
                                      QStringLiteral("wild"), QStringLiteral("samurai")};
    return order;
}

const int kSamuraiLines[40][5] = {
    {0,0,0,0,0},{0,1,1,1,0},{1,1,1,1,1},{1,2,2,2,1},{2,2,2,2,2},{2,3,3,3,2},
    {0,0,1,0,0},{0,1,0,1,0},{1,1,2,1,1},{1,2,1,2,1},{2,2,3,2,2},{2,3,2,3,2},
    {0,0,0,1,0},{0,1,1,0,0},{1,1,1,2,1},{1,2,2,1,1},{2,2,2,3,2},{2,3,3,2,2},
    {0,0,1,1,0},{0,1,0,0,0},{1,1,2,2,1},{1,2,1,1,1},{2,2,3,3,2},{2,3,2,2,2},
    {0,1,2,1,0},{1,2,3,2,1},{1,1,0,1,1},{2,2,1,2,2},{0,1,2,2,1},{1,2,3,3,2},
    {1,1,0,0,0},{2,2,1,1,1},{0,2,1,2,1},{1,3,2,3,2},{1,0,1,0,0},{2,1,2,1,1},
    {2,1,0,1,2},{0,2,3,2,0},{1,0,0,0,1},{1,3,3,3,1}};

struct SamuraiSpin
{
    double multiplier = 0.0;
    int scatters = 0;
    bool special = false;
};

SamuraiSpin samuraiOrdinary(const QVector<double> &f, bool bonus)
{
    static const int rows[5] = {3, 4, 4, 4, 3};
    QString grid[5][4];
    int k = 0;
    SamuraiSpin out;
    for (int r = 0; r < 5; ++r)
        for (int row = 0; row < rows[r]; ++row) {
            grid[r][row] = QLatin1String(samuraiSymbol(f.at(k++), r == 0 || r == 4));
            if (grid[r][row] == QLatin1String("scatter"))
                ++out.scatters;
        }
    for (int l = 0; l < 40; ++l) {
        double best = 0.0;
        for (const QString &target : samuraiOrder()) {
            int count = 0;
            while (count < 5) {
                const QString &s = grid[count][kSamuraiLines[l][count]];
                if (s == target || (target != QLatin1String("wild") && s == QLatin1String("wild")))
                    ++count;
                else
                    break;
            }
            const double pay = samuraiPay(target, count, bonus);
            if (pay > best)
                best = pay;
        }
        out.multiplier += best;
    }
    for (int r : {0, 4}) {
        bool all = true;
        for (int row = 0; row < rows[r]; ++row)
            all = all && grid[r][row] == QLatin1String("samurai");
        out.special = out.special || all;
    }
    return out;
}

double samuraiRound(const Seeds &s, int *bonusSpins, int *specialSpins)
{
    static const double specialPay[13] = {0, 1, 1.5, 2, 2.5, 5, 8, 12, 16, 30, 100, 250, 500};
    int spins = 0, bonusRemaining = 0, bonusAwarded = 0, bonusPlayed = 0;
    int specialPending = 0, specialRemaining = 0, specialPlayed = 0, pastSpecial = 0;
    QSet<int> sticky;
    double halfUnits = 0.0;
    while (spins == 0 || bonusRemaining > 0 || specialPending > 0 || specialRemaining > 0) {
        if (specialRemaining == 0 && specialPending > 0) {
            --specialPending;
            specialRemaining = 5;
            sticky.clear();
        }
        const bool special = specialRemaining > 0;
        const bool bonus = !special && spins > 0;
        const int cursor = spins * 72 - pastSpecial * 24;
        double mult = 0.0;
        if (special) {
            const QVector<double> f = floats(s, 12, cursor);
            const double before = specialPay[sticky.size()];
            for (int i = 0; i < 12; ++i)
                if (qstrcmp(samuraiSymbol(f.at(i), false), "samurai") == 0)
                    sticky.insert(i);
            mult = specialPay[sticky.size()] - before;
            --specialRemaining;
            ++specialPlayed;
            ++pastSpecial;
        } else {
            const SamuraiSpin sp = samuraiOrdinary(floats(s, 18, cursor), bonus);
            mult = sp.multiplier;
            if (bonus) {
                --bonusRemaining;
                ++bonusPlayed;
            }
            if (sp.scatters >= 3) {
                const int added = std::min(10, 180 - bonusAwarded);
                bonusAwarded += added;
                bonusRemaining += added;
            }
            if (sp.special)
                ++specialPending;
        }
        halfUnits += std::round(mult * 2.0);
        ++spins;
    }
    if (bonusSpins)
        *bonusSpins = bonusPlayed;
    if (specialSpins)
        *specialSpins = specialPlayed;
    return halfUnits / 2.0;
}

// ---- Primedice X: a roll on 10,001 points (0.00 .. 100.00); the win chance counts the grid points
// strictly inside the range(s) (outside: also the end points), out of 10,000; pays 0.99 / chance
// (4 decimals).
struct PrimediceResult
{
    double roll = 0.0;
    double chance = 0.0; // %
    double payout = 0.0; // multiplier if it wins
    bool win = false;
};

PrimediceResult primediceModel(double f, const QString &condition, double t1, double t2, double t3, double t4)
{
    const QString cond = condition == QLatin1String("rollOutside") ? QStringLiteral("outside")
                       : condition == QLatin1String("rollBetweenTwo") ? QStringLiteral("betweenTwo")
                                                                        : QStringLiteral("between");
    auto cent = [](double v) { return int(std::llround(v * 100.0)); };
    const int minimum = cond == QLatin1String("outside") ? 1 : 2;
    int lo = std::clamp(cent(t1), 0, 10000 - minimum);
    int hi = std::clamp(cent(t2), lo + minimum, 10000);
    if (cond == QLatin1String("outside") && hi - lo == 10000)
        hi = 9999;
    int lo2 = std::clamp(cent(t3), 0, 9998);
    int hi2 = std::clamp(cent(t4), lo2 + 2, 10000);
    if (cond == QLatin1String("betweenTwo")) {
        lo = std::clamp(lo, 0, 9995);
        hi = std::clamp(hi, lo + 2, 9997);
        lo2 = std::clamp(lo2, hi + 1, 9998);
        hi2 = std::clamp(hi2, lo2 + 2, 10000);
    }
    const int count = cond == QLatin1String("outside") ? 10000 - (hi - lo)
                                                       : hi - lo - 1 + (cond == QLatin1String("betweenTwo") ? hi2 - lo2 - 1 : 0);
    const double probability = std::clamp(count, 1, 9999) / 10000.0;
    const int index = int(f * 10001.0);
    PrimediceResult r;
    r.roll = index / 100.0;
    r.chance = probability * 100.0;
    r.payout = std::round(0.99 / probability * 10000.0) / 10000.0;
    r.win = cond == QLatin1String("outside") ? (index < lo || index > hi)
                                             : ((index > lo && index < hi)
                                                || (cond == QLatin1String("betweenTwo") && index > lo2 && index < hi2));
    return r;
}

int riskIndex(const QString &risk)
{
    const QString r = risk.trimmed().toLower();
    if (r == QLatin1String("low")) return 0;
    if (r == QLatin1String("medium")) return 1;
    if (r == QLatin1String("high")) return 2;
    return -1;
}

// Wheel: [segments / 10 - 1][risk] = the multiplier of every segment.
const QVector<double> &wheelRow(int segments, int risk)
{
    static const QVector<QVector<QVector<double>>> t = {
        {{1.5, 1.2, 1.2, 1.2, 0, 1.2, 1.2, 1.2, 1.2, 0},
         {0, 1.9, 0, 1.5, 0, 2, 0, 1.5, 0, 3},
         {0, 0, 0, 0, 0, 0, 0, 0, 0, 9.9}},
        {{1.5, 1.2, 1.2, 1.2, 0, 1.2, 1.2, 1.2, 1.2, 0, 1.5, 1.2, 1.2, 1.2, 0, 1.2, 1.2, 1.2, 1.2, 0},
         {1.5, 0, 2, 0, 2, 0, 2, 0, 1.5, 0, 3, 0, 1.8, 0, 2, 0, 2, 0, 2, 0},
         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 19.8}},
        {{1.5, 1.2, 1.2, 1.2, 0, 1.2, 1.2, 1.2, 1.2, 0, 1.5, 1.2, 1.2, 1.2, 0, 1.2, 1.2, 1.2, 1.2, 0,
          1.5, 1.2, 1.2, 1.2, 0, 1.2, 1.2, 1.2, 1.2, 0},
         {1.5, 0, 1.5, 0, 2, 0, 1.5, 0, 2, 0, 2, 0, 1.5, 0, 3, 0, 1.5, 0, 2, 0,
          2, 0, 1.7, 0, 4, 0, 1.5, 0, 2, 0},
         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 29.7}},
        {{1.5, 1.2, 1.2, 1.2, 0, 1.2, 1.2, 1.2, 1.2, 0, 1.5, 1.2, 1.2, 1.2, 0, 1.2, 1.2, 1.2, 1.2, 0,
          1.5, 1.2, 1.2, 1.2, 0, 1.2, 1.2, 1.2, 1.2, 0, 1.5, 1.2, 1.2, 1.2, 0, 1.2, 1.2, 1.2, 1.2, 0},
         {2, 0, 3, 0, 2, 0, 1.5, 0, 3, 0, 1.5, 0, 1.5, 0, 2, 0, 1.5, 0, 3, 0,
          1.5, 0, 2, 0, 2, 0, 1.6, 0, 2, 0, 1.5, 0, 3, 0, 1.5, 0, 2, 0, 1.5, 0},
         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 39.6}},
        {{1.5, 1.2, 1.2, 1.2, 0, 1.2, 1.2, 1.2, 1.2, 0, 1.5, 1.2, 1.2, 1.2, 0, 1.2, 1.2, 1.2, 1.2, 0,
          1.5, 1.2, 1.2, 1.2, 0, 1.2, 1.2, 1.2, 1.2, 0, 1.5, 1.2, 1.2, 1.2, 0, 1.2, 1.2, 1.2, 1.2, 0,
          1.5, 1.2, 1.2, 1.2, 0, 1.2, 1.2, 1.2, 1.2, 0},
         {2, 0, 1.5, 0, 2, 0, 1.5, 0, 3, 0, 1.5, 0, 1.5, 0, 2, 0, 1.5, 0, 3, 0,
          1.5, 0, 2, 0, 1.5, 0, 2, 0, 2, 0, 1.5, 0, 3, 0, 1.5, 0, 2, 0, 1.5, 0,
          1.5, 0, 5, 0, 1.5, 0, 2, 0, 1.5, 0},
         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 49.5}}};
    static const QVector<double> none;
    if (segments % 10 != 0 || segments < 10 || segments > 50 || risk < 0 || risk > 2)
        return none;
    return t.at(segments / 10 - 1).at(risk);
}

// ---- game helpers ---------------------------------------------------------------------------
int rankOf(int card) { return card / 4; } // 0 = "2" .. 12 = "A"

// Hilo: A = 1 (lowest) .. K = 13 (highest).
int hiloRank(int card)
{
    const int r = rankOf(card);
    return r == 12 ? 1 : r + 2;
}

int hiloRankOfText(const QString &rank)
{
    const QString r = rank.trimmed().toUpper();
    if (r == QLatin1String("A")) return 1;
    if (r == QLatin1String("J")) return 11;
    if (r == QLatin1String("Q")) return 12;
    if (r == QLatin1String("K")) return 13;
    const int n = r.toInt();
    return n >= 2 && n <= 10 ? n : 0;
}

int blackjackValue(const QVector<int> &cards, bool *soft = nullptr)
{
    int total = 0;
    int aces = 0;
    for (int c : cards) {
        const int r = rankOf(c);
        if (r == 12) {
            ++aces;
            total += 11;
        } else if (r >= 8) {
            total += 10;
        } else {
            total += r + 2;
        }
    }
    while (total > 21 && aces > 0) {
        total -= 10;
        --aces;
    }
    if (soft)
        *soft = aces > 0;
    return total;
}

int baccaratValue(int card)
{
    const int r = rankOf(card);
    if (r == 12) return 1;   // A
    if (r >= 8) return 0;    // 10 J Q K
    return r + 2;
}

QJsonArray cardsJson(const QVector<int> &cards)
{
    QJsonArray a;
    for (int c : cards)
        a.append(cardJson(c));
    return a;
}

QString cardsText(const QVector<int> &cards)
{
    QStringList out;
    for (int c : cards)
        out << cardText(c);
    return out.join(QLatin1Char(' '));
}

// Video poker hand (9/6 Jacks or Better pay table).
QString pokerHand(const QVector<int> &cards, double *mult)
{
    QVector<int> ranks;
    QMap<int, int> count;
    QSet<int> suits;
    for (int c : cards) {
        ranks << rankOf(c);
        ++count[rankOf(c)];
        suits.insert(c % 4);
    }
    std::sort(ranks.begin(), ranks.end());
    const bool flush = suits.size() == 1 && cards.size() == 5;
    bool straight = false;
    if (count.size() == 5) {
        straight = ranks.last() - ranks.first() == 4
                   || (ranks == QVector<int>{0, 1, 2, 3, 12}); // A-2-3-4-5
    }
    QList<int> groups = count.values();
    std::sort(groups.begin(), groups.end(), std::greater<int>());
    auto set = [mult](double m, const char *name) {
        if (mult)
            *mult = m;
        return QString::fromLatin1(name);
    };
    if (straight && flush && ranks.first() == 8)
        return set(800, "royalFlush");
    if (straight && flush)
        return set(60, "straightFlush");
    if (groups.value(0) == 4)
        return set(22, "fourOfAKind");
    if (groups.value(0) == 3 && groups.value(1) == 2)
        return set(9, "fullHouse");
    if (flush)
        return set(6, "flush");
    if (straight)
        return set(4, "straight");
    if (groups.value(0) == 3)
        return set(3, "threeOfAKind");
    if (groups.value(0) == 2 && groups.value(1) == 2)
        return set(2, "twoPair");
    for (auto it = count.begin(); it != count.end(); ++it)
        if (it.value() == 2 && it.key() >= 9) // J, Q, K, A
            return set(1, "pair");
    return set(0, "nothing");
}

const QStringList &gemNames()
{
    static const QStringList gems = {QStringLiteral("green"), QStringLiteral("purple"), QStringLiteral("yellow"),
                                     QStringLiteral("red"), QStringLiteral("cyan"), QStringLiteral("orange"),
                                     QStringLiteral("blue")};
    return gems;
}

// Diamonds: 5 gems; the pattern decides the payout.
QString diamondsResult(const QStringList &gems, double *mult)
{
    QMap<QString, int> count;
    for (const QString &g : gems)
        ++count[g];
    QList<int> groups = count.values();
    std::sort(groups.begin(), groups.end(), std::greater<int>());
    double m = 0.0;
    QString name = QStringLiteral("no match");
    if (groups.value(0) == 5) { m = 50; name = QStringLiteral("5 of a kind"); }
    else if (groups.value(0) == 4) { m = 5; name = QStringLiteral("4 of a kind"); }
    else if (groups.value(0) == 3 && groups.value(1) == 2) { m = 4; name = QStringLiteral("full house"); }
    else if (groups.value(0) == 3) { m = 3; name = QStringLiteral("3 of a kind"); }
    else if (groups.value(0) == 2 && groups.value(1) == 2) { m = 2; name = QStringLiteral("two pair"); }
    else if (groups.value(0) == 2) { m = 0.1; name = QStringLiteral("one pair"); }
    if (mult)
        *mult = m;
    return name;
}

struct TowerLevel
{
    int tiles = 4;
    int eggs = 3;
};

bool towerLevel(const QString &difficulty, TowerLevel *out)
{
    const QString d = difficulty.trimmed().toLower();
    TowerLevel l;
    if (d == QLatin1String("easy")) l = {4, 3};
    else if (d == QLatin1String("medium")) l = {3, 2};
    else if (d == QLatin1String("hard")) l = {2, 1};
    else if (d == QLatin1String("expert")) l = {3, 1};
    else if (d == QLatin1String("master")) l = {4, 1};
    else return false;
    if (out)
        *out = l;
    return true;
}

// Dragon Tower: the egg tiles of each of the 9 floors (a Fisher-Yates pick per floor).
QVector<QVector<int>> towerEggs(const Seeds &s, const TowerLevel &l)
{
    const QVector<double> f = floats(s, 9 * l.eggs);
    QVector<QVector<int>> floors;
    for (int i = 0; i < 9; ++i)
        floors << shuffled(f.mid(i * l.eggs, l.eggs), l.tiles, l.eggs);
    return floors;
}

double towerMultiplier(const TowerLevel &l, int floorsCleared)
{
    if (floorsCleared <= 0)
        return 0.0;
    return round6(0.98 / std::pow(double(l.eggs) / double(l.tiles), floorsCleared));
}

// Pump: 25 places, Chicken: a road of 20; 1 / 3 / 5 / 10 of them pop the balloon / end the run.
int crossingPlaces(Game game)
{
    return game == Game::Pump ? 25 : 20;
}

int crossingHazards(const QString &difficulty)
{
    QString d = difficulty.trimmed().toLower();
    if (d == QLatin1String("extreme"))
        d = QStringLiteral("expert");
    if (d == QLatin1String("easy")) return 1;
    if (d == QLatin1String("medium")) return 3;
    if (d == QLatin1String("hard")) return 5;
    if (d == QLatin1String("expert")) return 10;
    return 0;
}

// The step (1-based) at which the run ends: the first hazard of the shuffled places.
int crossingEnd(const Seeds &s, int hazards, int places)
{
    const QVector<int> spots = shuffled(floats(s, hazards), places, hazards);
    int first = places;
    for (int p : spots)
        first = std::min(first, p);
    return first + 1;
}

double crossingMultiplier(int hazards, int steps, int places)
{
    if (steps <= 0 || steps > places - hazards)
        return 0.0;
    if (places == 25) // pump: not rounded
        return round8(0.98 * std::round(choose(places, steps)) / std::round(choose(places - hazards, steps)));
    return round2(0.98 * choose(places, steps) / choose(places - hazards, steps));
}

// Rock Paper Scissors: what each float shows.
const QStringList &rpsSymbols()
{
    static const QStringList s = {QStringLiteral("rock"), QStringLiteral("paper"), QStringLiteral("scissors")};
    return s;
}

// +1 = `guess` beats `shown`, 0 = draw, -1 = loss.
int rpsOutcome(const QString &guess, const QString &shown)
{
    if (guess == shown)
        return 0;
    const bool win = (guess == QLatin1String("rock") && shown == QLatin1String("scissors"))
                     || (guess == QLatin1String("paper") && shown == QLatin1String("rock"))
                     || (guess == QLatin1String("scissors") && shown == QLatin1String("paper"));
    return win ? 1 : -1;
}

// Moles: the holes (0 .. 6) of `moles` moles on stage `stage` (a fresh board each stage).
QVector<int> molesOn(const Seeds &s, int moles, int stage)
{
    return shuffled(floats(s, moles, stage * moles * 4), 7, moles);
}

double molesMultiplier(int moles, int hits)
{
    if (hits <= 0)
        return 0.0;
    return round6(0.98 / std::pow(double(moles) / 7.0, hits));
}

double drillPoint(double u)
{
    if (u < 0.02)
        return 1.0;
    return floor2(std::min(0.98 / (1.0 - u), 2000000.0));
}

bool isRed(int pocket)
{
    static const QSet<int> red = {1, 3, 5, 7, 9, 12, 14, 16, 18, 19, 21, 23, 25, 27, 30, 32, 34, 36};
    return red.contains(pocket);
}

// Roulette: what a chip pays back (stake included) on `pocket`, as a multiple of its amount.
double rouletteChip(const QString &value, int pocket, bool *known)
{
    if (known)
        *known = true;
    const QString v = value.trimmed();
    if (v.startsWith(QLatin1String("number")))
        return v.mid(6).toInt() == pocket ? 36.0 : 0.0;
    if (pocket == 0) {
        if (known)
            *known = v.startsWith(QLatin1String("color")) || v.startsWith(QLatin1String("parity"))
                     || v.startsWith(QLatin1String("range")) || v.startsWith(QLatin1String("row"));
        return 0.0;
    }
    if (v == QLatin1String("colorRed")) return isRed(pocket) ? 2.0 : 0.0;
    if (v == QLatin1String("colorBlack")) return !isRed(pocket) ? 2.0 : 0.0;
    if (v == QLatin1String("parityEven")) return pocket % 2 == 0 ? 2.0 : 0.0;
    if (v == QLatin1String("parityOdd")) return pocket % 2 == 1 ? 2.0 : 0.0;
    if (v == QLatin1String("range0118")) return pocket <= 18 ? 2.0 : 0.0;
    if (v == QLatin1String("range1936")) return pocket >= 19 ? 2.0 : 0.0;
    if (v == QLatin1String("range0112")) return pocket <= 12 ? 3.0 : 0.0;
    if (v == QLatin1String("range1324")) return pocket >= 13 && pocket <= 24 ? 3.0 : 0.0;
    if (v == QLatin1String("range2536")) return pocket >= 25 ? 3.0 : 0.0;
    if (v == QLatin1String("row1")) return pocket % 3 == 1 ? 3.0 : 0.0;
    if (v == QLatin1String("row2")) return pocket % 3 == 2 ? 3.0 : 0.0;
    if (v == QLatin1String("row3")) return pocket % 3 == 0 ? 3.0 : 0.0;
    if (known)
        *known = false;
    return 0.0;
}

struct BaccaratDeal
{
    QVector<int> player;
    QVector<int> banker;
    int playerTotal = 0;
    int bankerTotal = 0;
    QString result; // "player" / "banker" / "tie"
};

// Baccarat (infinite decks): player, player, banker, banker, then the third-card rules.
BaccaratDeal baccaratDeal(const Seeds &s)
{
    const QVector<double> f = floats(s, 6);
    int next = 0;
    auto draw = [&] { return cardIndex(f.at(next++)); };
    auto total = [](const QVector<int> &cards) {
        int t = 0;
        for (int c : cards)
            t = (t + baccaratValue(c)) % 10;
        return t;
    };
    BaccaratDeal d;
    d.player << draw() << draw();
    d.banker << draw() << draw();
    int p = total(d.player);
    int b = total(d.banker);
    if (p < 8 && b < 8) {
        if (p > 5) {
            if (b <= 5)
                d.banker << draw();
        } else {
            const int third = draw();
            d.player << third;
            const int v = baccaratValue(third);
            bool bankerDraws = false;
            switch (b) {
            case 0: case 1: case 2: bankerDraws = true; break;
            case 3: bankerDraws = v != 8; break;
            case 4: bankerDraws = v >= 2 && v <= 7; break;
            case 5: bankerDraws = v >= 4 && v <= 7; break;
            case 6: bankerDraws = v == 6 || v == 7; break;
            default: break;
            }
            if (bankerDraws)
                d.banker << draw();
        }
    }
    d.playerTotal = total(d.player);
    d.bankerTotal = total(d.banker);
    d.result = d.playerTotal > d.bankerTotal ? QStringLiteral("player")
             : d.bankerTotal > d.playerTotal ? QStringLiteral("banker") : QStringLiteral("tie");
    return d;
}

QString gray(const QString &s)
{
    return QStringLiteral("<span style=\"color:#8a8f94\">%1</span>").arg(s);
}

QString num(double v, int decimals = 2)
{
    return QString::number(v, 'f', decimals);
}

QString floatsLine(const QVector<double> &f)
{
    QStringList out;
    for (double v : f)
        out << QString::number(v, 'f', 6);
    return out.join(QStringLiteral(", "));
}

QJsonValue param(const QJsonObject &p, Game game, const char *key)
{
    const QJsonValue v = p.value(QLatin1String(key));
    if (!v.isUndefined() && !v.isNull())
        return v;
    return StakeGames::defaultParams(game).value(QLatin1String(key));
}
} // namespace

// ---- public ---------------------------------------------------------------------------------

QVector<double> floats(const Seeds &seeds, int count, int cursor)
{
    QVector<double> out;
    out.reserve(count);
    int round = cursor / 32;
    int pos = cursor % 32;
    const QByteArray key = seeds.server.toUtf8();
    while (out.size() < count) {
        const QByteArray msg = QStringLiteral("%1:%2:%3").arg(seeds.client).arg(seeds.nonce).arg(round).toUtf8();
        const QByteArray mac = QMessageAuthenticationCode::hash(msg, key, QCryptographicHash::Sha256);
        for (; pos + 4 <= mac.size() && out.size() < count; pos += 4) {
            const auto b = [&mac, pos](int i) { return double(quint8(mac.at(pos + i))); };
            out << b(0) / 256.0 + b(1) / 65536.0 + b(2) / 16777216.0 + b(3) / 4294967296.0;
        }
        pos = 0;
        ++round;
    }
    return out;
}

QVector<int> shuffled(const QVector<double> &f, int pool, int picks)
{
    QVector<int> left;
    for (int i = 0; i < pool; ++i)
        left << i;
    QVector<int> out;
    for (int i = 0; i < picks && i < f.size() && !left.isEmpty(); ++i) {
        const int at = std::min(int(f.at(i) * left.size()), int(left.size()) - 1);
        out << left.takeAt(at);
    }
    return out;
}

int cardIndex(double f)
{
    return std::clamp(int(f * 52.0), 0, 51);
}

QJsonObject cardJson(int index)
{
    QJsonObject c;
    c.insert(QStringLiteral("rank"), QLatin1String(kRanks[std::clamp(index, 0, 51) / 4]));
    c.insert(QStringLiteral("suit"), QLatin1String(kSuits[std::clamp(index, 0, 51) % 4]));
    return c;
}

QString cardText(int index)
{
    static const QString suits[4] = {QString(QChar(0x2666)), QString(QChar(0x2665)), QString(QChar(0x2660)),
                                     QString(QChar(0x2663))};
    index = std::clamp(index, 0, 51);
    QString s = suits[index % 4] + QLatin1String(kRanks[index / 4]);
    if (index % 4 < 2) // hearts / diamonds in red
        s = QStringLiteral("<span style=\"color:#e53935\">%1</span>").arg(s);
    return s;
}

double kenoMultiplier(const QString &risk, int picks, int hits, bool *ok)
{
    const QVector<QVector<double>> &t = kenoTable(risk);
    const bool valid = picks >= 1 && picks <= t.size() && hits >= 0 && hits <= picks;
    if (ok)
        *ok = valid;
    return valid ? t.at(picks - 1).at(hits) : 0.0;
}

double plinkoMultiplier(int rows, const QString &risk, int slot, bool *ok)
{
    if (risk.trimmed().compare(QLatin1String("expert"), Qt::CaseInsensitive) == 0) {
        const QVector<double> &all = plinkoExpert(rows);
        const bool valid = slot >= 0 && slot < all.size();
        if (ok)
            *ok = valid;
        return valid ? all.at(slot) : 0.0;
    }
    const QVector<double> &half = plinkoHalf(rows, riskIndex(risk));
    const bool valid = !half.isEmpty() && slot >= 0 && slot <= rows;
    if (ok)
        *ok = valid;
    return valid ? half.at(std::min(slot, rows - slot)) : 0.0;
}

double wheelMultiplier(int segments, const QString &risk, int index, bool *ok)
{
    const QVector<double> &row = wheelRow(segments, riskIndex(risk));
    const bool valid = index >= 0 && index < row.size();
    if (ok)
        *ok = valid;
    return valid ? row.at(index) : 0.0;
}

double minesMultiplier(int mines, int revealed)
{
    if (revealed <= 0 || mines < 1 || mines > 24 || revealed > 25 - mines)
        return 0.0;
    return std::floor(0.99 * choose(25, revealed) / choose(25 - mines, revealed) * 1e4 + 1e-7) / 1e4;
}

bool canSimulate(Game game, QString *why)
{
    switch (game) {
    case Game::Dice: case Game::Limbo: case Game::Primedice: case Game::Keno: case Game::Mines:
    case Game::Plinko: case Game::Wheel: case Game::Roulette: case Game::Baccarat: case Game::Hilo:
    case Game::Blackjack: case Game::VideoPoker: case Game::Diamonds: case Game::DragonTower:
    case Game::Flip: case Game::Drill: case Game::Pump: case Game::Chicken: case Game::GoatRun:
    case Game::Moles: case Game::Rps: case Game::Snakes: case Game::Packs: case Game::Cases:
    case Game::Bars: case Game::Tarot: case Game::Darts: case Game::TomeOfLife: case Game::ScarabSpin:
    case Game::BlueSamurai:
        return true;
    default:
        if (why)
            *why = QStringLiteral("%1 can't be simulated: its payout table isn't published - turn on live "
                                  "betting to play it.").arg(gameDisplayName(game));
        return false;
    }
}

QString verifyHtml(Game game, const Seeds &s, const QJsonObject &params)
{
    const QJsonObject &p = params;
    QStringList lines;
    auto line = [&lines](const QString &label, const QString &value) {
        lines << QStringLiteral("%1: <b>%2</b>").arg(label, value);
    };

    switch (game) {
    case Game::Dice: {
        const double f = floats(s, 1).at(0);
        line(QStringLiteral("Roll"), num(std::floor(f * 10001.0) / 100.0));
        break;
    }
    case Game::Limbo: {
        const double f = floats(s, 1).at(0);
        const double raw = (1e8 / (std::floor(f * 1e8) + 1.0)) * 0.99;
        line(QStringLiteral("Limbo"), num(std::clamp(std::floor(raw * 100.0) / 100.0, 1.0, 1e6)) + QLatin1Char('x'));
        break;
    }
    case Game::Primedice: {
        const QString cond = text(param(p, game, "condition"), QStringLiteral("rollBetween"));
        const double t1 = number(param(p, game, "target1")), t2 = number(param(p, game, "target2"));
        const double t3 = number(param(p, game, "target3")), t4 = number(param(p, game, "target4"));
        const PrimediceResult r = primediceModel(floats(s, 1).at(0), cond, t1, t2, t3, t4);
        line(QStringLiteral("Roll"), num(r.roll));
        lines << gray(QStringLiteral("%1 with your targets (%2, %3% chance): %4")
                          .arg(cond, cond == QLatin1String("rollBetweenTwo")
                                         ? QStringLiteral("%1-%2 / %3-%4").arg(t1).arg(t2).arg(t3).arg(t4)
                                         : QStringLiteral("%1-%2").arg(t1).arg(t2))
                          .arg(num(r.chance))
                          .arg(r.win ? QStringLiteral("win %1x").arg(num(r.payout, 4)) : QStringLiteral("loss")));
        break;
    }
    case Game::Keno: {
        const QVector<int> drawn = shuffled(floats(s, 10), 40, 10);
        QStringList d;
        for (int n : drawn)
            d << QString::number(n + 1);
        line(QStringLiteral("Drawn numbers"), d.join(QStringLiteral(", ")));
        QVector<int> picks = intList(p.contains(QStringLiteral("kenoTiles")) ? p.value(QStringLiteral("kenoTiles"))
                                                                             : param(p, game, "numbers"));
        int hits = 0;
        for (int n : picks)
            if (drawn.contains(n - 1))
                ++hits;
        const QString risk = text(param(p, game, "risk"), QStringLiteral("low"));
        bool ok = false;
        const double m = kenoMultiplier(risk, picks.size(), hits, &ok);
        if (ok)
            lines << gray(QStringLiteral("Your %1 numbers (%2 risk): %3 hits = %4x").arg(picks.size()).arg(risk).arg(hits).arg(num(m)));
        break;
    }
    case Game::Mines: {
        const int count = std::clamp(int(number(param(p, game, "mines"), 3)), 1, 24);
        const QVector<int> mines = shuffled(floats(s, count), 25, count);
        QStringList m;
        for (int n : mines)
            m << QString::number(n + 1);
        line(QStringLiteral("Mines (%1)").arg(count), m.join(QStringLiteral(", ")));
        QStringList grid;
        for (int row = 0; row < 5; ++row) {
            QString r;
            for (int col = 0; col < 5; ++col)
                r += mines.contains(row * 5 + col) ? QStringLiteral("&#x1F4A3;") : QStringLiteral("&#x1F48E;");
            grid << r;
        }
        lines << grid.join(QStringLiteral("<br>"));
        const QVector<int> tiles = intList(p.contains(QStringLiteral("tiles")) ? p.value(QStringLiteral("tiles"))
                                                                              : param(p, game, "fields"));
        int safe = 0;
        bool boom = false;
        for (int t : tiles) {
            if (mines.contains(t - 1)) {
                boom = true;
                break;
            }
            ++safe;
        }
        if (!tiles.isEmpty())
            lines << gray(boom ? QStringLiteral("Your tiles: a mine after %1 safe tiles").arg(safe)
                               : QStringLiteral("Your %1 tiles are safe = %2x").arg(safe).arg(num(minesMultiplier(count, safe))));
        break;
    }
    case Game::Plinko: {
        const int rows = std::clamp(int(number(param(p, game, "rows"), 8)), 8, 16);
        const QVector<double> f = floats(s, rows);
        QString path;
        int slot = 0;
        for (double v : f) {
            const bool right = int(v * 2.0) == 1;
            slot += right ? 1 : 0;
            path += right ? QLatin1Char('R') : QLatin1Char('L');
        }
        line(QStringLiteral("Path (%1 rows)").arg(rows), path);
        const QString risk = text(param(p, game, "risk"), QStringLiteral("low"));
        bool ok = false;
        const double m = plinkoMultiplier(rows, risk, slot, &ok);
        line(QStringLiteral("Slot"), QStringLiteral("%1 of %2").arg(slot + 1).arg(rows + 1)
                                         + (ok ? QStringLiteral(" = %1x (%2)").arg(num(m, 1), risk) : QString()));
        break;
    }
    case Game::Wheel: {
        const int segments = std::clamp(int(number(param(p, game, "segments"), 10)), 10, 50) / 10 * 10;
        const int idx = std::min(int(floats(s, 1).at(0) * segments), segments - 1);
        const QString risk = text(param(p, game, "risk"), QStringLiteral("low"));
        bool ok = false;
        const double m = wheelMultiplier(segments, risk, idx, &ok);
        line(QStringLiteral("Segment"), QStringLiteral("%1 of %2").arg(idx).arg(segments)
                                            + (ok ? QStringLiteral(" = %1x (%2)").arg(num(m), risk) : QString()));
        break;
    }
    case Game::Roulette: {
        const int pocket = std::min(int(floats(s, 1).at(0) * 37.0), 36);
        line(QStringLiteral("Pocket"), QStringLiteral("%1 %2").arg(pocket).arg(
                                           pocket == 0 ? QStringLiteral("(green)")
                                                       : isRed(pocket) ? QStringLiteral("(red)") : QStringLiteral("(black)")));
        break;
    }
    case Game::Baccarat: {
        const BaccaratDeal d = baccaratDeal(s);
        line(QStringLiteral("Player (%1)").arg(d.playerTotal), cardsText(d.player));
        line(QStringLiteral("Banker (%1)").arg(d.bankerTotal), cardsText(d.banker));
        line(QStringLiteral("Result"), d.result);
        break;
    }
    case Game::Blackjack: {
        const QVector<double> f = floats(s, 12);
        QVector<int> c;
        for (double v : f)
            c << cardIndex(v);
        line(QStringLiteral("Player"), cardText(c.at(0)) + QLatin1Char(' ') + cardText(c.at(2)));
        line(QStringLiteral("Dealer"), cardText(c.at(1)) + QLatin1Char(' ') + cardText(c.at(3)));
        line(QStringLiteral("Next cards"), cardsText(c.mid(4)));
        lines << gray(QStringLiteral("Dealt player, dealer, player, dealer; hits, doubles, splits and the "
                                     "dealer's draws take the next cards in order."));
        break;
    }
    case Game::Hilo: {
        const QVector<double> f = floats(s, 15);
        QVector<int> c;
        for (double v : f)
            c << cardIndex(v);
        line(QStringLiteral("Cards"), cardsText(c));
        lines << gray(QStringLiteral("After the start card; each guess or skip takes the next card."));
        break;
    }
    case Game::VideoPoker: {
        const QVector<int> deck = shuffled(floats(s, 10), 52, 10);
        line(QStringLiteral("Hand"), cardsText(deck.mid(0, 5)));
        line(QStringLiteral("Draw cards"), cardsText(deck.mid(5, 5)));
        double m = 0.0;
        const QString hand = pokerHand(deck.mid(0, 5), &m);
        lines << gray(QStringLiteral("Dealt hand: %1 (%2x)").arg(hand, num(m)));
        break;
    }
    case Game::Diamonds: {
        QStringList gems;
        for (double v : floats(s, 5))
            gems << gemNames().at(std::min(int(v * 7.0), 6));
        double m = 0.0;
        const QString res = diamondsResult(gems, &m);
        line(QStringLiteral("Gems"), gems.join(QStringLiteral(", ")));
        line(QStringLiteral("Result"), QStringLiteral("%1 = %2x").arg(res, num(m)));
        break;
    }
    case Game::DragonTower: {
        const QString diff = text(param(p, game, "difficulty"), QStringLiteral("easy"));
        TowerLevel l;
        if (!towerLevel(diff, &l)) {
            lines << QStringLiteral("Unknown difficulty \"%1\".").arg(diff);
            break;
        }
        const QVector<QVector<int>> eggs = towerEggs(s, l);
        QStringList rows;
        for (int i = eggs.size() - 1; i >= 0; --i) {
            QString row;
            for (int t = 0; t < l.tiles; ++t)
                row += eggs.at(i).contains(t) ? QStringLiteral("&#x1F95A;") : QStringLiteral("&#x1F480;");
            rows << QStringLiteral("%1 %2").arg(i + 1).arg(row);
        }
        line(QStringLiteral("Floors (%1, top first)").arg(diff), QString());
        lines << rows.join(QStringLiteral("<br>"));
        break;
    }
    case Game::Flip: {
        QStringList flips;
        for (double v : floats(s, 20))
            flips << (v <= 0.5 ? QStringLiteral("tails") : QStringLiteral("heads"));
        line(QStringLiteral("Flips"), flips.join(QStringLiteral(", ")));
        break;
    }
    case Game::Drill: {
        QStringList d;
        int i = 1;
        for (double v : floats(s, 3))
            d << QStringLiteral("%1: %2x").arg(i++).arg(num(drillPoint(v)));
        line(QStringLiteral("Drills"), d.join(QStringLiteral(", ")));
        break;
    }
    case Game::Pump:
    case Game::Chicken:
    case Game::GoatRun: {
        const QString diff = text(param(p, game, "difficulty"), QStringLiteral("easy"));
        const int hazards = crossingHazards(diff);
        if (hazards <= 0) {
            lines << QStringLiteral("Unknown difficulty \"%1\".").arg(diff);
            break;
        }
        const int places = crossingPlaces(game);
        const int end = crossingEnd(s, hazards, places);
        line(game == Game::Pump ? QStringLiteral("Pops on pump") : QStringLiteral("Hit on lane"),
             QStringLiteral("%1 (%2)").arg(end).arg(diff));
        lines << gray(QStringLiteral("Safe up to %1 = %2x").arg(end - 1)
                          .arg(num(crossingMultiplier(hazards, end - 1, places))));
        break;
    }
    case Game::Snakes: {
        const QVector<double> f = floats(s, 10);
        QStringList pairs;
        for (int i = 0; i + 1 < f.size(); i += 2)
            pairs << QStringLiteral("%1+%2=%3").arg(die(f.at(i))).arg(die(f.at(i + 1))).arg(die(f.at(i)) + die(f.at(i + 1)));
        line(QStringLiteral("Dice"), pairs.join(QStringLiteral(", ")));
        const QString diff = text(param(p, game, "difficulty"), QStringLiteral("easy"));
        const int rolls = std::clamp(int(number(param(p, game, "rolls"), 1)), 1, 5);
        if (const QVector<QVector<double>> *t = snakesTable(diff)) {
            double m = 1.0;
            int safe = 0;
            for (int i = 0; i < rolls; ++i) {
                const double factor = t->at(i).at(die(f.at(2 * i)) + die(f.at(2 * i + 1)) - 2);
                if (factor <= 0.0) {
                    m = 0.0;
                    break;
                }
                m *= factor;
                ++safe;
            }
            lines << gray(m > 0.0 ? QStringLiteral("%1 rolls on %2: %3x").arg(rolls).arg(diff).arg(num(m, 4))
                                  : QStringLiteral("%1 rolls on %2: a snake on roll %3").arg(rolls).arg(diff).arg(safe + 1));
        }
        break;
    }
    case Game::Packs: {
        QStringList cards;
        double total = 0.0;
        for (double v : floats(s, 5)) {
            const PackCard c = packCard(v);
            cards << QStringLiteral("#%1 (%2x)").arg(c.id).arg(c.multiplier);
            total += c.multiplier;
        }
        line(QStringLiteral("Cards"), cards.join(QStringLiteral(", ")));
        line(QStringLiteral("Pack"), num(total, 2) + QLatin1Char('x'));
        break;
    }
    case Game::Darts: {
        const QVector<double> f = floats(s, 2);
        line(QStringLiteral("Rotation"), QStringLiteral("%1&deg;").arg(num(f.at(0) * 360.0, 2)));
        line(QStringLiteral("Distance"), num(std::sqrt(f.at(1)) / 2.0, 4));
        const QString diff = text(param(p, game, "difficulty"), QStringLiteral("easy"));
        if (const DartsLevel *l = dartsLevel(diff)) {
            QString colour;
            const double m = dartsResult(*l, f.at(0), f.at(1), &colour);
            line(QStringLiteral("Hit (%1)").arg(diff), QStringLiteral("%1 = %2x").arg(colour, num(m)));
        }
        break;
    }
    case Game::Cases: {
        const double f = floats(s, 1).at(0);
        const QString diff = text(param(p, game, "difficulty"), QStringLiteral("easy"));
        line(QStringLiteral("Float"), QString::number(f, 'f', 8));
        if (const QVector<WeightRow> *t = casesTable(diff))
            line(QStringLiteral("Case (%1)").arg(diff), num(pickWeighted(f, *t)) + QLatin1Char('x'));
        break;
    }
    case Game::Tarot: {
        const QVector<double> f = floats(s, 3);
        const QString diff = text(param(p, game, "difficulty"), QStringLiteral("medium"));
        if (const TarotTables *t = tarotTables(diff)) {
            double cards[3];
            const double m = tarotMultiplier(*t, f, cards);
            line(QStringLiteral("Cards (%1)").arg(diff), QStringLiteral("%1x &times; %2x &times; %3x")
                                                             .arg(num(cards[0])).arg(num(cards[1])).arg(num(cards[2])));
            line(QStringLiteral("Result"), num(m, 4) + QLatin1Char('x'));
        }
        break;
    }
    case Game::Bars: {
        const QVector<double> f = floats(s, 30);
        const QString diff = text(param(p, game, "difficulty"), QStringLiteral("easy"));
        const QVector<WeightRow> *t = barsTable(diff);
        if (!t)
            break;
        QVector<double> bars;
        QStringList shown;
        for (double v : f) {
            bars << pickWeighted(v, *t);
            shown << num(bars.last());
        }
        QStringList rows;
        for (int r = 0; r < 6; ++r)
            rows << shown.mid(r * 5, 5).join(QStringLiteral(" "));
        line(QStringLiteral("Bars (%1, 5 per row)").arg(diff), QString());
        lines << rows.join(QStringLiteral("<br>"));
        const QVector<int> tiles = intList(param(p, game, "tiles"));
        if (!tiles.isEmpty())
            lines << gray(QStringLiteral("Your bars %1: %2x").arg(QStringList([&] {
                                                                     QStringList l;
                                                                     for (int t : tiles) l << QString::number(t);
                                                                     return l; }()).join(QLatin1Char(',')))
                              .arg(num(barsMultiplier(bars, tiles), 4)));
        break;
    }
    case Game::TomeOfLife:
    case Game::ScarabSpin: {
        const int lines20 = std::clamp(int(number(param(p, game, "lines"), 20)), 1, 20);
        const bool buy = StakeGames::bonusBuy(game, p);
        int free = 0;
        const double m = tomeRound(s, lines20, &free, buy);
        if (buy)
            line(QStringLiteral("Bonus buy (%1 lines)").arg(lines20),
                 QStringLiteral("%1x the bet = %2x the buy").arg(num(m, 2), num(m / StakeGames::kTomeBonusBuyCost, 4)));
        else
            line(QStringLiteral("Result (%1 lines)").arg(lines20), num(m, 2) + QLatin1Char('x'));
        if (free > 0)
            lines << gray(QStringLiteral("%1 free spins played").arg(free));
        break;
    }
    case Game::BlueSamurai: {
        int bonus = 0, special = 0;
        const double m = samuraiRound(s, &bonus, &special);
        line(QStringLiteral("Result"), num(m, 2) + QLatin1Char('x'));
        if (bonus || special)
            lines << gray(QStringLiteral("%1 bonus spins, %2 special spins").arg(bonus).arg(special));
        break;
    }
    case Game::Moles: {
        const int moles = std::clamp(int(number(param(p, game, "moles"), 3)), 1, 6);
        const QVector<int> picks = intList(p.contains(QStringLiteral("molePicks")) ? p.value(QStringLiteral("molePicks"))
                                                                                  : param(p, game, "picks"));
        const int stages = std::clamp(int(picks.size()), 1, 10);
        int hits = 0;
        bool missed = false;
        for (int st = 0; st < stages; ++st) {
            QStringList h;
            for (int n : molesOn(s, moles, st))
                h << QString::number(n + 1);
            line(QStringLiteral("Stage %1 moles").arg(st + 1), h.join(QStringLiteral(", ")));
            if (!missed && st < picks.size()) {
                if (molesOn(s, moles, st).contains(picks.at(st) - 1))
                    ++hits;
                else
                    missed = true;
            }
        }
        if (!picks.isEmpty())
            lines << gray(missed ? QStringLiteral("Your picks: a miss after %1 hits").arg(hits)
                                 : QStringLiteral("Your %1 picks all hit = %2x").arg(hits).arg(num(molesMultiplier(moles, hits))));
        break;
    }
    case Game::Rps: {
        QStringList shown;
        for (double v : floats(s, 10))
            shown << rpsSymbols().at(std::min(int(v * 3.0), 2));
        line(QStringLiteral("Opponent"), shown.join(QStringLiteral(", ")));
        lines << gray(QStringLiteral("Each float picks rock, paper or scissors (float &times; 3, in that order)."));
        break;
    }
    default: {
        int count = 5;
        if (game == Game::Cases) count = 1;
        else if (game == Game::Tarot) count = 3;
        else if (game == Game::Bars) count = 30;
        line(QStringLiteral("Floats"), floatsLine(floats(s, count)));
        lines << gray(QStringLiteral("%1 picks its result from these with its own payout table (shown on "
                                     "Stake's verify page).").arg(gameDisplayName(game)));
        break;
    }
    }
    return lines.join(QStringLiteral("<br>"));
}
} // namespace StakeFair

// ============================================================================ simulated Stake

using namespace StakeFair;

namespace
{
QJsonObject apiError(const QString &message, const QString &type = QStringLiteral("simulation"))
{
    QJsonObject e;
    e.insert(QStringLiteral("message"), message);
    e.insert(QStringLiteral("errorType"), type);
    QJsonObject o;
    o.insert(QStringLiteral("errors"), QJsonArray{e});
    return o;
}

QJsonObject wrap(const QString &key, const QJsonObject &bet)
{
    QJsonObject o;
    o.insert(key, bet);
    return o;
}

double floor8(double v)
{
    return std::floor(v * 1e8 + 1e-6) / 1e8;
}

double numberOf(const QJsonValue &v, double def = 0.0)
{
    if (v.isDouble())
        return v.toDouble();
    if (v.isString()) {
        bool ok = false;
        const double d = v.toString().toDouble(&ok);
        return ok ? d : def;
    }
    return def;
}

QJsonObject betObject(const QString &id, Game game, const Seeds &s, double amount, double mult,
                      bool active, const QJsonObject &state)
{
    QJsonObject o;
    o.insert(QStringLiteral("id"), id);
    o.insert(QStringLiteral("amount"), amount);
    o.insert(QStringLiteral("payoutMultiplier"), active ? 0.0 : mult);
    o.insert(QStringLiteral("payout"), active ? 0.0 : floor8(amount * mult));
    o.insert(QStringLiteral("active"), active);
    o.insert(QStringLiteral("currency"), QStringLiteral("sim"));
    o.insert(QStringLiteral("game"), gameName(game));
    o.insert(QStringLiteral("nonce"), double(s.nonce));
    o.insert(QStringLiteral("state"), state);
    return o;
}
} // namespace

SimulatedStakeTransport::SimulatedStakeTransport(SimulatedProvider *provider, QObject *parent)
    : IStakeTransport(parent)
    , m_provider(provider)
{
}

bool SimulatedStakeTransport::isReady(QString *whyNot) const
{
    if (!m_provider && whyNot)
        *whyNot = QStringLiteral("No simulation seed pair.");
    return m_provider != nullptr;
}

void SimulatedStakeTransport::post(const QString &path, const QJsonObject &body, TransportCallback cb)
{
    TransportReply reply;
    reply.ok = true;
    reply.status = 200;
    reply.json = handle(path, body);
    // Answered on the next event loop turn, like a network reply.
    QTimer::singleShot(0, this, [cb, reply] {
        if (cb)
            cb(reply);
    });
}

Seeds SimulatedStakeTransport::takeSeeds()
{
    Seeds s;
    s.server = m_provider->serverSeed();
    s.client = m_provider->clientSeed();
    s.nonce = m_provider->takeNonce();
    return s;
}

QJsonObject SimulatedStakeTransport::handle(const QString &path, const QJsonObject &body)
{
    if (!m_provider)
        return apiError(QStringLiteral("No simulation seed pair."));
    QString p = path;
    if (p.startsWith(QLatin1Char('/')))
        p.remove(0, 1);

    if (p == QLatin1String("_api/graphql")) {
        const QString query = body.value(QStringLiteral("query")).toString();
        const QJsonObject vars = body.value(QStringLiteral("variables")).toObject();
        if (query.contains(QLatin1String("plinkoBet")))
            return plinko(vars);
        if (query.contains(QLatin1String("flipBet")))
            return flip(vars);
        return apiError(QStringLiteral("This request can't be simulated."));
    }
    if (p.startsWith(QLatin1String("_api/casino/active-bet/")))
        return activeBet(p.mid(QStringLiteral("_api/casino/active-bet/").size()));

    if (p == QLatin1String("_api/casino/keno/bet")) return keno(body);
    if (p == QLatin1String("_api/casino/mines/bet")) return minesBet(body);
    if (p == QLatin1String("_api/casino/mines/cashout")) return minesCashout();
    if (p == QLatin1String("_api/casino/wheel/spin")) return wheel(body);
    if (p == QLatin1String("_api/casino/roulette/bet")) return roulette(body);
    if (p == QLatin1String("_api/casino/baccarat/bet")) return baccarat(body);
    if (p == QLatin1String("_api/casino/dragon-tower/bet")) return dragonTower(body);
    if (p == QLatin1String("_api/casino/diamonds/bet")) return diamonds(body);
    if (p == QLatin1String("_api/casino/drill/bet")) return drill(body);
    if (p == QLatin1String("_api/casino/primedice-x/bet")) return primedice(body);
    if (p == QLatin1String("_api/casino/pump/bet")) return crossing(Game::Pump, body);
    if (p == QLatin1String("_api/casino/chicken/bet")) return crossing(Game::Chicken, body);
    if (p == QLatin1String("_api/casino/moles/autobet")) return moles(body);
    if (p == QLatin1String("_api/casino/snakes/bet")) return snakes(body);
    if (p == QLatin1String("_api/casino/packs/bet")) return packs(body);
    if (p == QLatin1String("_api/casino/cases/bet")) return weighted(Game::Cases, body);
    if (p == QLatin1String("_api/casino/tarot/bet")) return weighted(Game::Tarot, body);
    if (p == QLatin1String("_api/casino/darts/bet")) return weighted(Game::Darts, body);
    if (p == QLatin1String("_api/casino/bars/bet")) return weighted(Game::Bars, body);
    if (p == QLatin1String("_api/casino/slots/bet")) return slotGame(Game::ScarabSpin, body);
    if (p == QLatin1String("_api/casino/slots-tome-of-life/bet")) return slotGame(Game::TomeOfLife, body);
    if (p == QLatin1String("_api/casino/slots-tome-of-life/bonus")) {
        QJsonObject b = body;
        b.insert(QStringLiteral("_bonusBuy"), true);
        return slotGame(Game::TomeOfLife, b);
    }
    if (p == QLatin1String("_api/casino/slots-samurai/bet")) return slotGame(Game::BlueSamurai, body);
    if (p == QLatin1String("_api/casino/rock-paper-scissors/bet")) return rps(body);
    if (p == QLatin1String("_api/casino/hilo/bet")) return hiloBet(body);
    if (p == QLatin1String("_api/casino/hilo/next")) return hiloNext(body);
    if (p == QLatin1String("_api/casino/hilo/cashout")) return hiloCashout();
    if (p == QLatin1String("_api/casino/video-poker/bet")) return videoPokerBet(body);
    if (p == QLatin1String("_api/casino/video-poker/next")) return videoPokerNext(body);
    if (p == QLatin1String("_api/casino/blackjack/bet")) return blackjackBet(body);
    if (p == QLatin1String("_api/casino/blackjack/next")) return blackjackNext(body);

    return apiError(QStringLiteral("This game can't be simulated: its payout table isn't published - turn on "
                                   "live betting to play it."));
}

// ---- single-request games ----------------------------------------------------------------

QJsonObject SimulatedStakeTransport::keno(const QJsonObject &b)
{
    const QString risk = b.value(QStringLiteral("risk")).toString(QStringLiteral("low"));
    QJsonArray picked = b.value(QStringLiteral("numbers")).toArray();
    bool ok = false;
    kenoMultiplier(risk, picked.size(), 0, &ok);
    if (!ok)
        return apiError(QStringLiteral("Keno: pick 1 to 10 numbers and a risk of classic, low, medium or high."));
    const double amount = numberOf(b.value(QStringLiteral("amount")));
    const Seeds s = takeSeeds();
    const QVector<int> drawn = shuffled(floats(s, 10), 40, 10);
    int hits = 0;
    QJsonArray drawnJson;
    for (int n : drawn)
        drawnJson.append(n);
    for (const QJsonValue &n : picked)
        if (drawn.contains(n.toInt()))
            ++hits;
    const double mult = kenoMultiplier(risk, picked.size(), hits);
    QJsonObject st;
    st.insert(QStringLiteral("risk"), risk);
    st.insert(QStringLiteral("selectedNumbers"), picked);
    st.insert(QStringLiteral("drawnNumbers"), drawnJson);
    return wrap(QStringLiteral("kenoBet"), betObject(QString::number(++m_betId), Game::Keno, s, amount, mult, false, st));
}

QJsonObject SimulatedStakeTransport::minesBet(const QJsonObject &b)
{
    if (m_open.contains(QStringLiteral("mines")))
        return apiError(QStringLiteral("You already have an active mines game."), QStringLiteral("existingGame"));
    const int count = int(numberOf(b.value(QStringLiteral("minesCount")), 3));
    const QJsonArray fields = b.value(QStringLiteral("fields")).toArray();
    if (count < 1 || count > 24)
        return apiError(QStringLiteral("Mines: 1 to 24 mines."));
    if (fields.isEmpty() || fields.size() > 25 - count)
        return apiError(QStringLiteral("Mines: reveal 1 to %1 tiles with %2 mines.").arg(25 - count).arg(count));
    const double amount = numberOf(b.value(QStringLiteral("amount")));
    Round r;
    r.game = Game::Mines;
    r.seeds = takeSeeds();
    r.amount = amount;
    r.id = QString::number(++m_betId);
    r.mines = shuffled(floats(r.seeds, count), 25, count);
    bool boom = false;
    int safe = 0;
    for (const QJsonValue &f : fields) {
        const int field = f.toInt();
        QJsonObject round;
        round.insert(QStringLiteral("field"), field);
        if (r.mines.contains(field)) {
            boom = true;
            round.insert(QStringLiteral("payoutMultiplier"), 0.0);
            r.minesRounds.append(round);
            break;
        }
        ++safe;
        round.insert(QStringLiteral("payoutMultiplier"), minesMultiplier(count, safe));
        r.minesRounds.append(round);
    }
    QJsonObject st;
    st.insert(QStringLiteral("minesCount"), count);
    st.insert(QStringLiteral("rounds"), r.minesRounds);
    if (boom) {
        QJsonArray mines;
        for (int m : r.mines)
            mines.append(m);
        st.insert(QStringLiteral("mines"), mines);
        return wrap(QStringLiteral("minesBet"), betObject(r.id, Game::Mines, r.seeds, amount, 0.0, false, st));
    }
    m_open.insert(QStringLiteral("mines"), r);
    return wrap(QStringLiteral("minesBet"), betObject(r.id, Game::Mines, r.seeds, amount, 0.0, true, st));
}

QJsonObject SimulatedStakeTransport::minesCashout()
{
    if (!m_open.contains(QStringLiteral("mines")))
        return apiError(QStringLiteral("No active mines game found."), QStringLiteral("notFound"));
    const Round r = m_open.take(QStringLiteral("mines"));
    const double mult = minesMultiplier(r.mines.size(), r.minesRounds.size());
    QJsonArray mines;
    for (int m : r.mines)
        mines.append(m);
    QJsonObject st;
    st.insert(QStringLiteral("minesCount"), int(r.mines.size()));
    st.insert(QStringLiteral("rounds"), r.minesRounds);
    st.insert(QStringLiteral("mines"), mines);
    return wrap(QStringLiteral("minesCashout"), betObject(r.id, Game::Mines, r.seeds, r.amount, mult, false, st));
}

QJsonObject SimulatedStakeTransport::plinko(const QJsonObject &vars)
{
    const int rows = int(numberOf(vars.value(QStringLiteral("rows")), 8));
    const QString risk = vars.value(QStringLiteral("risk")).toString(QStringLiteral("low"));
    bool ok = false;
    plinkoMultiplier(rows, risk, 0, &ok);
    if (!ok)
        return apiError(risk.compare(QLatin1String("expert"), Qt::CaseInsensitive) == 0
                            ? QStringLiteral("Plinko: 8 to 16 rows on the expert risk.")
                            : QStringLiteral("Plinko: 8 to 16 rows, risk low, medium, high or expert."));
    const double amount = numberOf(vars.value(QStringLiteral("amount")));
    const Seeds s = takeSeeds();
    QJsonArray path;
    int slot = 0;
    for (double v : floats(s, rows)) {
        const bool right = int(v * 2.0) == 1;
        slot += right ? 1 : 0;
        path.append(right ? QStringLiteral("R") : QStringLiteral("L"));
    }
    const double mult = plinkoMultiplier(rows, risk, slot);
    QJsonObject st;
    st.insert(QStringLiteral("risk"), risk);
    st.insert(QStringLiteral("rows"), rows);
    st.insert(QStringLiteral("point"), slot);
    st.insert(QStringLiteral("path"), path);
    QJsonObject data;
    data.insert(QStringLiteral("plinkoBet"), betObject(QString::number(++m_betId), Game::Plinko, s, amount, mult, false, st));
    return wrap(QStringLiteral("data"), data);
}

QJsonObject SimulatedStakeTransport::flip(const QJsonObject &vars)
{
    QStringList guesses;
    for (const QJsonValue &g : vars.value(QStringLiteral("guesses")).toArray())
        guesses << g.toString().trimmed().toLower();
    if (guesses.isEmpty() || guesses.size() > 20)
        return apiError(QStringLiteral("Flip: 1 to 20 guesses (heads / tails)."));
    const double amount = numberOf(vars.value(QStringLiteral("amount")));
    const Seeds s = takeSeeds();
    const QVector<double> f = floats(s, 20);
    QJsonArray flips;
    bool lost = false;
    for (int i = 0; i < guesses.size(); ++i) {
        const QString side = f.at(i) <= 0.5 ? QStringLiteral("tails") : QStringLiteral("heads");
        flips.append(side);
        if (side != guesses.at(i)) {
            lost = true;
            break;
        }
    }
    const double mult = lost ? 0.0 : round2(0.98 * std::pow(2.0, guesses.size()));
    QJsonObject st;
    st.insert(QStringLiteral("flips"), flips);
    st.insert(QStringLiteral("playedRounds"), int(flips.size()));
    st.insert(QStringLiteral("currentRound"), int(flips.size()));
    st.insert(QStringLiteral("payoutMultiplier"), mult);
    QJsonObject data;
    data.insert(QStringLiteral("flipBet"), betObject(QString::number(++m_betId), Game::Flip, s, amount, mult, false, st));
    return wrap(QStringLiteral("data"), data);
}

QJsonObject SimulatedStakeTransport::wheel(const QJsonObject &b)
{
    const int segments = int(numberOf(b.value(QStringLiteral("segments")), 10));
    const QString risk = b.value(QStringLiteral("risk")).toString(QStringLiteral("low"));
    bool ok = false;
    wheelMultiplier(segments, risk, 0, &ok);
    if (!ok)
        return apiError(QStringLiteral("Wheel: segments 10, 20, 30, 40 or 50 and risk low, medium or high."));
    const double amount = numberOf(b.value(QStringLiteral("amount")));
    const Seeds s = takeSeeds();
    const int idx = std::min(int(floats(s, 1).at(0) * segments), segments - 1);
    QJsonObject st;
    st.insert(QStringLiteral("result"), idx);
    st.insert(QStringLiteral("segments"), segments);
    st.insert(QStringLiteral("risk"), risk);
    return wrap(QStringLiteral("wheelBet"),
                betObject(QString::number(++m_betId), Game::Wheel, s, amount, wheelMultiplier(segments, risk, idx), false, st));
}

QJsonObject SimulatedStakeTransport::roulette(const QJsonObject &b)
{
    double total = 0.0;
    QList<QPair<QString, double>> chips;
    for (const char *group : {"rows", "parities", "ranges", "colors", "numbers"}) {
        for (const QJsonValue &c : b.value(QLatin1String(group)).toArray()) {
            const QJsonObject o = c.toObject();
            const double a = numberOf(o.value(QStringLiteral("amount")));
            chips.append({o.value(QStringLiteral("value")).toString(), a});
            total += a;
        }
    }
    if (chips.isEmpty())
        return apiError(QStringLiteral("Roulette: no chips placed."));
    const Seeds s = takeSeeds();
    const int pocket = std::min(int(floats(s, 1).at(0) * 37.0), 36);
    double paid = 0.0;
    double units = 0.0; // a 0 bet: the chips' own multipliers, weighted equally
    for (const auto &c : chips) {
        bool known = true;
        const double m = rouletteChip(c.first, pocket, &known);
        if (!known)
            return apiError(QStringLiteral("Roulette: unknown chip \"%1\".").arg(c.first));
        paid += c.second * m;
        units += m;
    }
    total = floor8(total);
    const double mult = total > 0.0 ? paid / total : units / chips.size();
    QJsonObject st;
    for (const char *group : {"rows", "parities", "ranges", "colors", "numbers"})
        st.insert(QLatin1String(group), b.value(QLatin1String(group)));
    st.insert(QStringLiteral("result"), pocket);
    return wrap(QStringLiteral("rouletteBet"),
                betObject(QString::number(++m_betId), Game::Roulette, s, total, mult, false, st));
}

QJsonObject SimulatedStakeTransport::baccarat(const QJsonObject &b)
{
    const double player = numberOf(b.value(QStringLiteral("player")));
    const double banker = numberOf(b.value(QStringLiteral("banker")));
    const double tie = numberOf(b.value(QStringLiteral("tie")));
    if (player < 0 || banker < 0 || tie < 0)
        return apiError(QStringLiteral("Baccarat: amounts must be 0 or more."));
    const Seeds s = takeSeeds();
    const BaccaratDeal d = baccaratDeal(s);
    double paid = 0.0;
    if (d.result == QLatin1String("player"))
        paid = player * 2.0;
    else if (d.result == QLatin1String("banker"))
        paid = banker * 1.95;
    else
        paid = tie * 9.0 + player + banker; // a tie returns the player / banker stakes
    const double total = floor8(player + banker + tie);
    double mult = total > 0.0 ? paid / total : 0.0;
    if (!(total > 0.0)) // a 0 bet: the side it would have been on
        mult = d.result == QLatin1String("tie") ? 9.0 : 0.0;
    QJsonObject st;
    st.insert(QStringLiteral("player"), player);
    st.insert(QStringLiteral("banker"), banker);
    st.insert(QStringLiteral("tie"), tie);
    st.insert(QStringLiteral("playerCards"), cardsJson(d.player));
    st.insert(QStringLiteral("bankerCards"), cardsJson(d.banker));
    st.insert(QStringLiteral("result"), d.result);
    return wrap(QStringLiteral("baccaratBet"), betObject(QString::number(++m_betId), Game::Baccarat, s, total, mult, false, st));
}

QJsonObject SimulatedStakeTransport::dragonTower(const QJsonObject &b)
{
    const QString diff = b.value(QStringLiteral("difficulty")).toString(QStringLiteral("easy"));
    TowerLevel l;
    if (!towerLevel(diff, &l))
        return apiError(QStringLiteral("Tower: difficulty easy, medium, hard, expert or master."));
    const QJsonArray picks = b.value(QStringLiteral("eggs")).toArray();
    if (picks.isEmpty() || picks.size() > 9)
        return apiError(QStringLiteral("Tower: pick an egg on 1 to 9 floors."));
    for (const QJsonValue &e : picks)
        if (e.toInt() < 0 || e.toInt() >= l.tiles)
            return apiError(QStringLiteral("Tower: eggs are 1 to %1 on %2.").arg(l.tiles).arg(diff));
    const double amount = numberOf(b.value(QStringLiteral("amount")));
    const Seeds s = takeSeeds();
    const QVector<QVector<int>> eggs = towerEggs(s, l);
    int cleared = 0;
    bool lost = false;
    QJsonArray played;
    for (int i = 0; i < picks.size(); ++i) {
        played.append(picks.at(i).toInt());
        if (!eggs.at(i).contains(picks.at(i).toInt())) {
            lost = true;
            break;
        }
        ++cleared;
    }
    QJsonArray layout;
    for (const QVector<int> &floor : eggs) {
        QJsonArray f;
        for (int t : floor)
            f.append(t);
        layout.append(f);
    }
    QJsonObject st;
    st.insert(QStringLiteral("difficulty"), diff);
    st.insert(QStringLiteral("currentRound"), cleared);
    st.insert(QStringLiteral("playedRounds"), played);
    st.insert(QStringLiteral("rounds"), layout);
    const double mult = lost ? 0.0 : towerMultiplier(l, cleared);
    return wrap(QStringLiteral("dragonTowerBet"),
                betObject(QString::number(++m_betId), Game::DragonTower, s, amount, mult, false, st));
}

QJsonObject SimulatedStakeTransport::diamonds(const QJsonObject &b)
{
    const double amount = numberOf(b.value(QStringLiteral("amount")));
    const Seeds s = takeSeeds();
    QStringList gems;
    QJsonArray hand;
    for (double v : floats(s, 5)) {
        gems << gemNames().at(std::min(int(v * 7.0), 6));
        hand.append(gems.last());
    }
    double mult = 0.0;
    diamondsResult(gems, &mult);
    QJsonObject st;
    st.insert(QStringLiteral("hand"), hand);
    return wrap(QStringLiteral("diamondsBet"), betObject(QString::number(++m_betId), Game::Diamonds, s, amount, mult, false, st));
}

QJsonObject SimulatedStakeTransport::drill(const QJsonObject &b)
{
    const double target = numberOf(b.value(QStringLiteral("target")), 2.0);
    const int pick = std::clamp(int(numberOf(b.value(QStringLiteral("pick")), 1)), 1, 3);
    if (target < 1.01)
        return apiError(QStringLiteral("Drill: the target must be 1.01x or more."));
    const double amount = numberOf(b.value(QStringLiteral("amount")));
    const Seeds s = takeSeeds();
    QJsonArray results;
    QVector<double> points;
    for (double v : floats(s, 3)) {
        points << drillPoint(v);
        QJsonObject r;
        r.insert(QStringLiteral("multiplier"), points.last());
        results.append(r);
    }
    const double mult = points.at(pick - 1) >= target ? target : 0.0;
    QJsonObject st;
    st.insert(QStringLiteral("pick"), pick);
    st.insert(QStringLiteral("target"), target);
    st.insert(QStringLiteral("drillResults"), results);
    return wrap(QStringLiteral("drillBet"), betObject(QString::number(++m_betId), Game::Drill, s, amount, mult, false, st));
}

QJsonObject SimulatedStakeTransport::primedice(const QJsonObject &b)
{
    const QString cond = b.value(QStringLiteral("condition")).toString(QStringLiteral("rollBetween"));
    const double t1 = numberOf(b.value(QStringLiteral("target1")));
    const double t2 = numberOf(b.value(QStringLiteral("target2")));
    const double t3 = numberOf(b.value(QStringLiteral("target3")));
    const double t4 = numberOf(b.value(QStringLiteral("target4")));
    const double amount = numberOf(b.value(QStringLiteral("amount")));
    const Seeds s = takeSeeds();
    const PrimediceResult r = primediceModel(floats(s, 1).at(0), cond, t1, t2, t3, t4);
    const double roll = r.roll;
    const double mult = r.win ? r.payout : 0.0;
    QJsonObject st;
    st.insert(QStringLiteral("condition"), cond);
    st.insert(QStringLiteral("target1"), b.value(QStringLiteral("target1")));
    st.insert(QStringLiteral("target2"), b.value(QStringLiteral("target2")));
    st.insert(QStringLiteral("target3"), b.value(QStringLiteral("target3")));
    st.insert(QStringLiteral("target4"), b.value(QStringLiteral("target4")));
    st.insert(QStringLiteral("result"), roll);
    return wrap(QStringLiteral("primediceXBet"), betObject(QString::number(++m_betId), Game::Primedice, s, amount, mult, false, st));
}

QJsonObject SimulatedStakeTransport::crossing(Game game, const QJsonObject &b)
{
    const QString diff = b.value(QStringLiteral("difficulty")).toString(QStringLiteral("easy"));
    const int hazards = crossingHazards(diff);
    const int places = crossingPlaces(game);
    const int steps = int(numberOf(b.value(QStringLiteral("round")), 1));
    const QString what = game == Game::Pump ? QStringLiteral("Pump: pumps") : QStringLiteral("Chicken: steps");
    if (hazards <= 0)
        return apiError(QStringLiteral("%1 - difficulty easy, medium, hard or expert.").arg(what));
    if (steps < 1 || steps > places - hazards)
        return apiError(QStringLiteral("%1 must be 1 to %2 on %3.").arg(what).arg(places - hazards).arg(diff));
    const double amount = numberOf(b.value(QStringLiteral("amount")));
    const Seeds s = takeSeeds();
    const int end = crossingEnd(s, hazards, places);
    const double mult = steps < end ? crossingMultiplier(hazards, steps, places) : 0.0;
    QJsonObject st;
    st.insert(QStringLiteral("difficulty"), diff);
    st.insert(QStringLiteral("round"), steps);
    st.insert(game == Game::Pump ? QStringLiteral("_popPoint") : QStringLiteral("_deathPoint"), end);
    return wrap(game == Game::Pump ? QStringLiteral("pumpBet") : QStringLiteral("chickenBet"),
                betObject(QString::number(++m_betId), game, s, amount, mult, false, st));
}

QJsonObject SimulatedStakeTransport::moles(const QJsonObject &b)
{
    const int count = int(numberOf(b.value(QStringLiteral("molesCount")), 3));
    const QJsonArray picks = b.value(QStringLiteral("picks")).toArray();
    if (count < 1 || count > 6)
        return apiError(QStringLiteral("Moles: 1 to 6 moles."));
    if (picks.isEmpty() || picks.size() > (count == 1 ? 8 : 9))
        return apiError(QStringLiteral("Moles: pick a hole on 1 to %1 stages.").arg(count == 1 ? 8 : 9));
    for (const QJsonValue &v : picks)
        if (v.toInt() < 0 || v.toInt() > 6)
            return apiError(QStringLiteral("Moles: holes are 1 to 7."));
    const double amount = numberOf(b.value(QStringLiteral("amount")));
    const Seeds s = takeSeeds();
    QJsonArray rounds;
    int hits = 0;
    bool missed = false;
    for (int st = 0; st < picks.size() && !missed; ++st) {
        const QVector<int> holes = molesOn(s, count, st);
        QJsonArray molesJson;
        for (int h : holes)
            molesJson.append(h);
        const bool hit = holes.contains(picks.at(st).toInt());
        QJsonObject r;
        r.insert(QStringLiteral("pick"), picks.at(st).toInt());
        r.insert(QStringLiteral("moles"), molesJson);
        r.insert(QStringLiteral("hit"), hit);
        rounds.append(r);
        if (hit)
            ++hits;
        else
            missed = true;
    }
    QJsonObject st;
    st.insert(QStringLiteral("molesCount"), count);
    st.insert(QStringLiteral("rounds"), rounds);
    const double mult = missed ? 0.0 : molesMultiplier(count, hits);
    return wrap(QStringLiteral("molesBet"), betObject(QString::number(++m_betId), Game::Moles, s, amount, mult, false, st));
}

QJsonObject SimulatedStakeTransport::snakes(const QJsonObject &b)
{
    const QString diff = b.value(QStringLiteral("difficulty")).toString(QStringLiteral("easy"));
    const QVector<QVector<double>> *t = snakesTable(diff);
    const int rolls = int(numberOf(b.value(QStringLiteral("rollCount")), 1));
    if (!t)
        return apiError(QStringLiteral("Snakes: difficulty easy, medium, hard, expert or master."));
    if (rolls < 1 || rolls > 5)
        return apiError(QStringLiteral("Snakes: 1 to 5 rolls."));
    const double amount = numberOf(b.value(QStringLiteral("amount")));
    const Seeds s = takeSeeds();
    const QVector<double> f = floats(s, 10);
    double mult = 1.0;
    QJsonArray rounds;
    for (int i = 0; i < rolls; ++i) {
        const int a = die(f.at(2 * i));
        const int d = die(f.at(2 * i + 1));
        const double factor = t->at(i).at(a + d - 2);
        mult = factor > 0.0 ? mult * factor : 0.0;
        QJsonObject r;
        r.insert(QStringLiteral("dice"), QJsonArray{a, d});
        r.insert(QStringLiteral("payoutMultiplier"), mult);
        rounds.append(r);
        if (factor <= 0.0)
            break;
    }
    mult = std::round(mult * 1e8) / 1e8;
    QJsonObject st;
    st.insert(QStringLiteral("difficulty"), diff);
    st.insert(QStringLiteral("rounds"), rounds);
    return wrap(QStringLiteral("snakesBet"), betObject(QString::number(++m_betId), Game::Snakes, s, amount, mult, false, st));
}

QJsonObject SimulatedStakeTransport::packs(const QJsonObject &b)
{
    const double amount = numberOf(b.value(QStringLiteral("amount")));
    const Seeds s = takeSeeds();
    double total = 0.0;
    QJsonArray cards;
    for (double v : floats(s, 5)) {
        const PackCard c = packCard(v);
        QJsonObject card;
        card.insert(QStringLiteral("id"), c.id);
        card.insert(QStringLiteral("multiplier"), c.multiplier);
        cards.append(card);
        total += c.multiplier;
    }
    QJsonObject st;
    st.insert(QStringLiteral("cards"), cards);
    return wrap(QStringLiteral("packsBet"),
                betObject(QString::number(++m_betId), Game::Packs, s, amount, round6(total), false, st));
}

QJsonObject SimulatedStakeTransport::weighted(Game game, const QJsonObject &b)
{
    const QString diff = b.value(QStringLiteral("difficulty")).toString(QStringLiteral("easy")).toLower();
    const double amount = numberOf(b.value(QStringLiteral("amount")));
    QJsonObject st;
    st.insert(QStringLiteral("difficulty"), diff);
    double mult = 0.0;
    QString key;
    if (game == Game::Cases) {
        const QVector<WeightRow> *t = casesTable(diff);
        if (!t)
            return apiError(QStringLiteral("Cases: difficulty easy, medium, hard or expert."));
        const Seeds s = takeSeeds();
        mult = pickWeighted(floats(s, 1).at(0), *t);
        st.insert(QStringLiteral("result"), mult);
        return wrap(QStringLiteral("casesBet"), betObject(QString::number(++m_betId), game, s, amount, mult, false, st));
    }
    if (game == Game::Tarot) {
        const TarotTables *t = tarotTables(diff);
        if (!t)
            return apiError(QStringLiteral("Tarot: difficulty easy, medium, hard or expert."));
        const Seeds s = takeSeeds();
        double cards[3];
        mult = tarotMultiplier(*t, floats(s, 3), cards);
        st.insert(QStringLiteral("cards"), QJsonArray{cards[0], cards[1], cards[2]});
        return wrap(QStringLiteral("tarotBet"), betObject(QString::number(++m_betId), game, s, amount, mult, false, st));
    }
    if (game == Game::Darts) {
        const DartsLevel *l = dartsLevel(diff);
        if (!l)
            return apiError(QStringLiteral("Darts: difficulty easy, medium, hard or expert."));
        const Seeds s = takeSeeds();
        const QVector<double> f = floats(s, 2);
        QString colour;
        mult = dartsResult(*l, f.at(0), f.at(1), &colour);
        st.insert(QStringLiteral("colour"), colour);
        st.insert(QStringLiteral("rotation"), f.at(0));
        st.insert(QStringLiteral("distance"), f.at(1));
        return wrap(QStringLiteral("dartsBet"), betObject(QString::number(++m_betId), game, s, amount, mult, false, st));
    }
    // Bars: 30 bars; "tiles" = the selected bars (0 .. 29), 1 to 5 of them.
    const QVector<WeightRow> *t = barsTable(diff);
    if (!t)
        return apiError(QStringLiteral("Bars: difficulty easy, medium, hard or expert."));
    QVector<int> tiles;
    for (const QJsonValue &v : b.value(QStringLiteral("tiles")).toArray()) {
        const int i = v.toInt(-1);
        if (i < 0 || i > 29 || tiles.contains(i))
            return apiError(QStringLiteral("Bars: pick 1 to 5 different bars (0 to 29)."));
        tiles << i;
    }
    if (tiles.isEmpty() || tiles.size() > 5)
        return apiError(QStringLiteral("Bars: pick 1 to 5 different bars (0 to 29)."));
    const Seeds s = takeSeeds();
    QVector<double> bars;
    QJsonArray barsJson;
    for (double v : floats(s, 30)) {
        bars << pickWeighted(v, *t);
        barsJson.append(bars.last());
    }
    mult = barsMultiplier(bars, tiles);
    QJsonArray tilesJson;
    for (int i : tiles)
        tilesJson.append(i);
    st.insert(QStringLiteral("tiles"), tilesJson);
    st.insert(QStringLiteral("bars"), barsJson);
    return wrap(QStringLiteral("barsBet"), betObject(QString::number(++m_betId), game, s, amount, mult, false, st));
}

QJsonObject SimulatedStakeTransport::slotGame(Game game, const QJsonObject &b)
{
    const double amount = numberOf(b.value(QStringLiteral("amount")));
    QJsonObject st;
    double mult = 0.0;
    Seeds s;
    if (game == Game::BlueSamurai) {
        s = takeSeeds();
        int bonus = 0, special = 0;
        mult = samuraiRound(s, &bonus, &special);
        st.insert(QStringLiteral("bonusSpins"), bonus);
        st.insert(QStringLiteral("specialSpins"), special);
        st.insert(QStringLiteral("nextSpinType"), QStringLiteral("none")); // the whole round in one bet
    } else {
        const int lines = int(numberOf(b.value(QStringLiteral("lines")), 20));
        if (lines < 1 || lines > 20)
            return apiError(QStringLiteral("%1: 1 to 20 lines.").arg(gameDisplayName(game)));
        s = takeSeeds();
        int free = 0;
        const bool buy = b.value(QStringLiteral("_bonusBuy")).toBool();
        mult = tomeRound(s, lines, &free, buy);
        st.insert(QStringLiteral("lines"), lines);
        st.insert(QStringLiteral("freeSpins"), free);
        if (buy) {
            // Bought: Stake charges 37x the amount; the payout is the spins' total x the amount.
            const double cost = floor8(amount * StakeGames::kTomeBonusBuyCost);
            QJsonObject bet = betObject(QString::number(++m_betId), game, s, cost,
                                        mult / StakeGames::kTomeBonusBuyCost, false, st);
            bet.insert(QStringLiteral("payout"), floor8(amount * mult));
            return wrap(QStringLiteral("slotsTomeOfLifeBonusBuyInBet"), bet);
        }
    }
    return wrap(QStringLiteral("slotsBet"), betObject(QString::number(++m_betId), game, s, amount, mult, false, st));
}

QJsonObject SimulatedStakeTransport::rps(const QJsonObject &b)
{
    QStringList guesses;
    for (const QJsonValue &g : b.value(QStringLiteral("guesses")).toArray())
        guesses << g.toString().trimmed().toLower();
    if (guesses.isEmpty() || guesses.size() > 20)
        return apiError(QStringLiteral("Rock Paper Scissors: 1 to 20 guesses."));
    for (const QString &g : guesses)
        if (!rpsSymbols().contains(g))
            return apiError(QStringLiteral("Rock Paper Scissors: guesses are rock, paper or scissors."));
    const double amount = numberOf(b.value(QStringLiteral("amount")));
    const Seeds s = takeSeeds();
    const QVector<double> f = floats(s, guesses.size());
    QJsonArray rounds;
    int wins = 0;
    bool lost = false;
    for (int i = 0; i < guesses.size(); ++i) {
        const QString shown = rpsSymbols().at(std::min(int(f.at(i) * 3.0), 2));
        const int o = rpsOutcome(guesses.at(i), shown);
        QJsonObject r;
        r.insert(QStringLiteral("guess"), guesses.at(i));
        r.insert(QStringLiteral("result"), shown);
        r.insert(QStringLiteral("outcome"), o > 0 ? QStringLiteral("win") : o == 0 ? QStringLiteral("draw")
                                                                                  : QStringLiteral("lose"));
        rounds.append(r);
        if (o < 0) {
            lost = true;
            break;
        }
        if (o > 0)
            ++wins;
    }
    // 0.98 x 2^wins; draws only keep the chain alive (all draws = 0.98x).
    const double mult = lost ? 0.0 : round2(0.98 * std::pow(2.0, wins));
    QJsonObject st;
    st.insert(QStringLiteral("rounds"), rounds);
    st.insert(QStringLiteral("wins"), wins);
    return wrap(QStringLiteral("rockPaperScissorsBet"),
                betObject(QString::number(++m_betId), Game::Rps, s, amount, mult, false, st));
}

// ---- hilo ------------------------------------------------------------------------------------

QJsonObject SimulatedStakeTransport::hiloJson(const Round &r, bool active, double mult) const
{
    QJsonObject st;
    st.insert(QStringLiteral("startCard"), r.startCard);
    st.insert(QStringLiteral("rounds"), r.rounds);
    return betObject(r.id, Game::Hilo, r.seeds, r.amount, mult, active, st);
}

QJsonObject SimulatedStakeTransport::hiloBet(const QJsonObject &b)
{
    if (m_open.contains(QStringLiteral("hilo")))
        return apiError(QStringLiteral("You already have an active hilo game."), QStringLiteral("existingGame"));
    const QJsonObject card = b.value(QStringLiteral("startCard")).toObject();
    const int rank = hiloRankOfText(card.value(QStringLiteral("rank")).toString());
    const QString suit = card.value(QStringLiteral("suit")).toString().toUpper();
    if (rank == 0 || !QStringLiteral("DHSC").contains(suit) || suit.size() != 1)
        return apiError(QStringLiteral("Hilo: the start card must be a rank (A, 2 .. 10, J, Q, K) and a suit (C, D, H, S)."));
    Round r;
    r.game = Game::Hilo;
    r.seeds = takeSeeds();
    r.amount = numberOf(b.value(QStringLiteral("amount")));
    r.id = QString::number(++m_betId);
    r.startCard = card;
    r.lastRank = rank;
    r.multiplier = 1.0;
    m_open.insert(QStringLiteral("hilo"), r);
    return wrap(QStringLiteral("hiloBet"), hiloJson(r, true, 0.0));
}

QJsonObject SimulatedStakeTransport::hiloNext(const QJsonObject &b)
{
    if (!m_open.contains(QStringLiteral("hilo")))
        return apiError(QStringLiteral("No active hilo game found."), QStringLiteral("notFound"));
    Round &r = m_open[QStringLiteral("hilo")];
    const QString guess = b.value(QStringLiteral("guess")).toString();
    const int cur = r.lastRank;
    double chance = 0.0;
    if (guess == QLatin1String("higher")) chance = (13 - cur) / 13.0;
    else if (guess == QLatin1String("lower")) chance = (cur - 1) / 13.0;
    else if (guess == QLatin1String("higherEqual")) chance = (14 - cur) / 13.0;
    else if (guess == QLatin1String("lowerEqual")) chance = cur / 13.0;
    else if (guess == QLatin1String("equal")) chance = 1.0 / 13.0;
    else if (guess != QLatin1String("skip"))
        return apiError(QStringLiteral("Hilo: unknown guess \"%1\".").arg(guess), QStringLiteral("invalidAction"));
    if (guess != QLatin1String("skip") && !(chance > 0.0))
        return apiError(QStringLiteral("Hilo: \"%1\" can't win on this card.").arg(guess), QStringLiteral("invalidAction"));
    if (r.next >= 52)
        return apiError(QStringLiteral("Hilo: no cards left - cash out."), QStringLiteral("invalidAction"));

    const int card = cardIndex(floats(r.seeds, 1, r.next * 4).at(0));
    ++r.next;
    const int rank = hiloRank(card);
    bool won = true;
    if (guess == QLatin1String("higher")) won = rank > cur;
    else if (guess == QLatin1String("lower")) won = rank < cur;
    else if (guess == QLatin1String("higherEqual")) won = rank >= cur;
    else if (guess == QLatin1String("lowerEqual")) won = rank <= cur;
    else if (guess == QLatin1String("equal")) won = rank == cur;
    if (guess != QLatin1String("skip")) {
        ++r.guesses;
        // The first correct guess pays 0.99 / chance (the edge, once), the next ones 1 / chance;
        // rounded to 6 decimals after each step.
        r.multiplier = won ? round6(r.multiplier * (r.guesses == 1 ? 0.99 : 1.0) / chance) : 0.0;
    }
    r.lastRank = rank;
    QJsonObject round;
    round.insert(QStringLiteral("card"), cardJson(card));
    round.insert(QStringLiteral("guess"), guess);
    round.insert(QStringLiteral("payoutMultiplier"), r.guesses > 0 ? r.multiplier : 1.0);
    r.rounds.append(round);
    if (!won) {
        const Round done = m_open.take(QStringLiteral("hilo"));
        return wrap(QStringLiteral("hiloNext"), hiloJson(done, false, 0.0));
    }
    return wrap(QStringLiteral("hiloNext"), hiloJson(r, true, 0.0));
}

QJsonObject SimulatedStakeTransport::hiloCashout()
{
    if (!m_open.contains(QStringLiteral("hilo")))
        return apiError(QStringLiteral("No active hilo game found."), QStringLiteral("notFound"));
    if (m_open.value(QStringLiteral("hilo")).guesses == 0)
        return apiError(QStringLiteral("Hilo: make a guess before cashing out."), QStringLiteral("invalidAction"));
    const Round r = m_open.take(QStringLiteral("hilo"));
    return wrap(QStringLiteral("hiloCashOut"), hiloJson(r, false, r.multiplier));
}

// ---- video poker ----------------------------------------------------------------------------

QJsonObject SimulatedStakeTransport::videoPokerBet(const QJsonObject &b)
{
    if (m_open.contains(QStringLiteral("videoPoker")))
        return apiError(QStringLiteral("You already have an active video poker game."), QStringLiteral("existingGame"));
    Round r;
    r.game = Game::VideoPoker;
    r.seeds = takeSeeds();
    r.amount = numberOf(b.value(QStringLiteral("amount")));
    r.id = QString::number(++m_betId);
    r.deck = shuffled(floats(r.seeds, 10), 52, 10);
    m_open.insert(QStringLiteral("videoPoker"), r);
    QJsonObject st;
    st.insert(QStringLiteral("initialHand"), cardsJson(r.deck.mid(0, 5)));
    st.insert(QStringLiteral("playerHand"), cardsJson(r.deck.mid(0, 5)));
    return wrap(QStringLiteral("videoPokerBet"), betObject(r.id, Game::VideoPoker, r.seeds, r.amount, 0.0, true, st));
}

QJsonObject SimulatedStakeTransport::videoPokerNext(const QJsonObject &b)
{
    if (!m_open.contains(QStringLiteral("videoPoker")))
        return apiError(QStringLiteral("No active video poker game found."), QStringLiteral("notFound"));
    const Round r = m_open.take(QStringLiteral("videoPoker"));
    const QVector<int> hand = r.deck.mid(0, 5);
    QVector<bool> keep(5, false);
    for (const QJsonValue &h : b.value(QStringLiteral("held")).toArray()) {
        const QJsonObject c = h.toObject();
        for (int i = 0; i < 5; ++i) {
            const QJsonObject dealt = cardJson(hand.at(i));
            if (!keep.at(i) && dealt.value(QStringLiteral("rank")) == c.value(QStringLiteral("rank"))
                && dealt.value(QStringLiteral("suit")) == c.value(QStringLiteral("suit"))) {
                keep[i] = true;
                break;
            }
        }
    }
    QVector<int> final = hand;
    int draw = 5;
    for (int i = 0; i < 5; ++i)
        if (!keep.at(i))
            final[i] = r.deck.at(draw++);
    double mult = 0.0;
    const QString result = pokerHand(final, &mult);
    QJsonObject st;
    st.insert(QStringLiteral("initialHand"), cardsJson(hand));
    st.insert(QStringLiteral("playerHand"), cardsJson(final));
    st.insert(QStringLiteral("handResult"), result);
    return wrap(QStringLiteral("videoPokerNext"), betObject(r.id, Game::VideoPoker, r.seeds, r.amount, mult, false, st));
}

// ---- blackjack ------------------------------------------------------------------------------
// Dealt player, dealer, player, dealer (the hole card); then every hit / double / split card and
// the dealer's draws take the next cards in order. Blackjack pays 3:2, insurance 2:1, the dealer
// stands on all 17s and checks for blackjack; one split (two cards of the same value; split aces
// get one card each), double on any two cards.

namespace
{
int nextCard(SimulatedStakeTransport::Round &r)
{
    return cardIndex(floats(r.seeds, 1, (r.next++) * 4).at(0));
}

bool isBlackjack(const QVector<int> &cards)
{
    return cards.size() == 2 && blackjackValue(cards) == 21;
}

bool hasAction(const QJsonArray &actions, const char *a)
{
    for (const QJsonValue &v : actions)
        if (v.toString() == QLatin1String(a))
            return true;
    return false;
}

void markHand(SimulatedStakeTransport::Hand &h, bool split)
{
    const int v = blackjackValue(h.cards);
    if (v > 21) {
        if (!hasAction(h.actions, "bust"))
            h.actions.append(QStringLiteral("bust"));
        h.done = true;
    } else if (v == 21) {
        const char *mark = (h.cards.size() == 2 && !split) ? "blackjack" : "full";
        if (!hasAction(h.actions, mark))
            h.actions.append(QLatin1String(mark));
        h.done = true;
    }
}
} // namespace

QJsonObject SimulatedStakeTransport::blackjackJson(const Round &r) const
{
    const bool active = m_open.contains(QStringLiteral("blackjack")) && m_open.value(QStringLiteral("blackjack")).id == r.id;
    QJsonArray player;
    for (const Hand &h : r.hands) {
        QJsonObject o;
        o.insert(QStringLiteral("cards"), cardsJson(h.cards));
        o.insert(QStringLiteral("value"), blackjackValue(h.cards));
        o.insert(QStringLiteral("actions"), h.actions);
        player.append(o);
    }
    QJsonObject dealer;
    const QVector<int> shown = active ? r.dealer.mid(0, 1) : r.dealer;
    QJsonArray dActions{QStringLiteral("deal")};
    if (!active) {
        dActions.append(QStringLiteral("reveal"));
        for (int i = 2; i < r.dealer.size(); ++i)
            dActions.append(QStringLiteral("hit"));
        const int dv = blackjackValue(r.dealer);
        if (dv > 21)
            dActions.append(QStringLiteral("bust"));
        else if (isBlackjack(r.dealer))
            dActions.append(QStringLiteral("blackjack"));
        else if (dv == 21)
            dActions.append(QStringLiteral("full"));
    }
    dealer.insert(QStringLiteral("cards"), cardsJson(shown));
    dealer.insert(QStringLiteral("value"), blackjackValue(shown));
    dealer.insert(QStringLiteral("actions"), dActions);
    QJsonObject st;
    st.insert(QStringLiteral("player"), player);
    st.insert(QStringLiteral("dealer"), QJsonArray{dealer});
    return st;
}

QJsonObject SimulatedStakeTransport::finishBlackjack(Round r)
{
    m_open.remove(QStringLiteral("blackjack"));
    // Everything in units of one hand's stake.
    double staked = 0.0;
    double paid = 0.0;
    const bool dealerBj = isBlackjack(r.dealer);
    const int dv = blackjackValue(r.dealer);
    const bool split = r.hands.size() > 1;
    for (const Hand &h : r.hands) {
        const double w = h.doubled ? 2.0 : 1.0;
        staked += w;
        const int pv = blackjackValue(h.cards);
        if (pv > 21)
            continue;
        if (!split && isBlackjack(h.cards)) {
            paid += dealerBj ? 1.0 : 2.5;
            continue;
        }
        if (dealerBj)
            continue;
        if (dv > 21 || pv > dv)
            paid += 2.0 * w;
        else if (pv == dv)
            paid += w;
    }
    if (r.insured) {
        staked += 0.5;
        if (dealerBj)
            paid += 1.5;
    }
    const double amount = floor8(r.amount * staked);
    const double mult = staked > 0.0 ? paid / staked : 0.0;
    QJsonObject bet = betObject(r.id, Game::Blackjack, r.seeds, amount, mult, false, blackjackJson(r));
    bet.insert(QStringLiteral("payout"), floor8(r.amount * paid));
    return wrap(QStringLiteral("blackjackNext"), bet);
}

// After a move: the next hand to play, or the dealer's turn and the result.
QJsonObject SimulatedStakeTransport::blackjackAdvance(Round &r)
{
    while (r.current < r.hands.size() && r.hands.at(r.current).done)
        ++r.current;
    if (r.current < r.hands.size()) {
        double staked = 0.0;
        for (const Hand &h : r.hands)
            staked += h.doubled ? 2.0 : 1.0;
        return wrap(QStringLiteral("blackjackNext"),
                    betObject(r.id, Game::Blackjack, r.seeds, floor8(r.amount * (staked + (r.insured ? 0.5 : 0.0))),
                              0.0, true, blackjackJson(r)));
    }
    bool anyLive = false;
    for (const Hand &h : r.hands)
        anyLive = anyLive || blackjackValue(h.cards) <= 21;
    if (anyLive)
        while (blackjackValue(r.dealer) < 17)
            r.dealer << nextCard(r);
    return finishBlackjack(r);
}

QJsonObject SimulatedStakeTransport::blackjackBet(const QJsonObject &b)
{
    if (m_open.contains(QStringLiteral("blackjack")))
        return apiError(QStringLiteral("You already have an active blackjack game."), QStringLiteral("existingGame"));
    Round r;
    r.game = Game::Blackjack;
    r.seeds = takeSeeds();
    r.amount = numberOf(b.value(QStringLiteral("amount")));
    r.id = QString::number(++m_betId);
    Hand h;
    h.actions.append(QStringLiteral("deal"));
    h.cards << nextCard(r);
    r.dealer << nextCard(r);
    h.cards << nextCard(r);
    r.dealer << nextCard(r); // the hole card
    r.hands << h;
    markHand(r.hands[0], false);
    m_open.insert(QStringLiteral("blackjack"), r);
    Round &open = m_open[QStringLiteral("blackjack")];
    if (rankOf(open.dealer.at(0)) == 12) {
        // Ace up: insurance is offered first.
        open.insurancePending = true;
        open.hands[0].done = false;
        QJsonObject bet = betObject(open.id, Game::Blackjack, open.seeds, open.amount, 0.0, true, blackjackJson(open));
        return wrap(QStringLiteral("blackjackBet"), bet);
    }
    if (isBlackjack(open.dealer) || isBlackjack(open.hands.at(0).cards)) {
        QJsonObject done = finishBlackjack(open);
        return wrap(QStringLiteral("blackjackBet"), done.value(QStringLiteral("blackjackNext")).toObject());
    }
    QJsonObject next = blackjackAdvance(open);
    return wrap(QStringLiteral("blackjackBet"), next.value(QStringLiteral("blackjackNext")).toObject());
}

QJsonObject SimulatedStakeTransport::blackjackNext(const QJsonObject &b)
{
    if (!m_open.contains(QStringLiteral("blackjack")))
        return apiError(QStringLiteral("No active blackjack game found."), QStringLiteral("notFound"));
    Round &r = m_open[QStringLiteral("blackjack")];
    const QString action = b.value(QStringLiteral("action")).toString();
    auto invalid = [&action] {
        return apiError(QStringLiteral("Blackjack: \"%1\" is not possible now (invalid action).").arg(action),
                        QStringLiteral("blackjackInvalidAction"));
    };

    if (r.insurancePending) {
        if (action != QLatin1String("insurance") && action != QLatin1String("noInsurance"))
            return invalid();
        r.insurancePending = false;
        r.insured = action == QLatin1String("insurance");
        r.hands[0].actions.append(action);
        if (isBlackjack(r.dealer) || isBlackjack(r.hands.at(0).cards))
            return finishBlackjack(r);
        return blackjackAdvance(r);
    }
    if (action == QLatin1String("insurance") || action == QLatin1String("noInsurance"))
        return invalid();
    if (r.current >= r.hands.size())
        return invalid();

    const bool split = r.hands.size() > 1;
    Hand &h = r.hands[r.current];
    if (action == QLatin1String("hit")) {
        h.actions.append(action);
        h.cards << nextCard(r);
        markHand(h, split);
    } else if (action == QLatin1String("stand")) {
        h.actions.append(action);
        h.done = true;
    } else if (action == QLatin1String("double")) {
        if (h.cards.size() != 2)
            return invalid();
        h.actions.append(action);
        h.doubled = true;
        h.cards << nextCard(r);
        markHand(h, split);
        h.done = true;
    } else if (action == QLatin1String("split")) {
        // Two cards of the same value (10, J, Q and K count alike).
        if (split || h.cards.size() != 2
            || blackjackValue(h.cards.mid(0, 1)) != blackjackValue(h.cards.mid(1, 1)))
            return invalid();
        Hand second;
        second.actions.append(QStringLiteral("deal"));
        second.cards << h.cards.takeLast();
        h.actions.append(action);
        second.actions.append(action);
        h.cards << nextCard(r);
        second.cards << nextCard(r);
        const bool aces = rankOf(second.cards.at(0)) == 12;
        r.hands << second;
        markHand(r.hands[0], true);
        markHand(r.hands[1], true);
        if (aces) { // split aces get one card each and no more moves
            r.hands[0].done = true;
            r.hands[1].done = true;
        }
    } else {
        return invalid();
    }
    return blackjackAdvance(r);
}

QJsonObject SimulatedStakeTransport::activeBet(const QString &slug)
{
    QJsonObject user;
    QJsonValue bet = QJsonValue::Null;
    if (slug == QLatin1String("hilo") && m_open.contains(slug)) {
        const Round &r = m_open[slug];
        bet = hiloJson(r, true, 0.0);
    } else if (slug == QLatin1String("blackjack") && m_open.contains(slug)) {
        Round &r = m_open[slug];
        bet = betObject(r.id, Game::Blackjack, r.seeds, r.amount, 0.0, true, blackjackJson(r));
    } else if (slug == QLatin1String("videoPoker") && m_open.contains(slug)) {
        const Round &r = m_open[slug];
        QJsonObject st;
        st.insert(QStringLiteral("initialHand"), cardsJson(r.deck.mid(0, 5)));
        st.insert(QStringLiteral("playerHand"), cardsJson(r.deck.mid(0, 5)));
        bet = betObject(r.id, Game::VideoPoker, r.seeds, r.amount, 0.0, true, st);
    } else if (slug == QLatin1String("mines") && m_open.contains(slug)) {
        const Round &r = m_open[slug];
        QJsonObject st;
        st.insert(QStringLiteral("minesCount"), int(r.mines.size()));
        st.insert(QStringLiteral("rounds"), r.minesRounds);
        bet = betObject(r.id, Game::Mines, r.seeds, r.amount, 0.0, true, st);
    }
    user.insert(QStringLiteral("activeCasinoBet"), bet);
    return wrap(QStringLiteral("user"), user);
}
