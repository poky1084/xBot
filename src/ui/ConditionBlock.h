#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QList>
#include <QMap>
#include <QString>

// One "When ... Then ..." rule. Mirrors ConditionBlockData.cs.
struct ConditionBlockData
{
    static QString newId();

    QString id = newId();
    QString type = QStringLiteral("bets"); // "bets" | "profit"

    // ON side
    // every, everyStreakOf, firstStreakOf, streakGreaterThan, streakLowerThan,
    // greaterThan, greaterThanOrEqualTo, lowerThan, lowerThanOrEqualTo
    QString onType = QStringLiteral("everyStreakOf");
    double onValue = 1;
    QString betType = QStringLiteral("lose");      // bet | win | lose
    QString profitType = QStringLiteral("profit"); // balance | loss | profit

    // DO side
    QString doType = QStringLiteral("increaseByPercentage");
    double doValue = 100;
};

using BlockList = QList<ConditionBlockData>;
using StrategyMap = QMap<QString, BlockList>;

namespace ConditionIo
{
struct ParsedStrategy
{
    QString label;
    BlockList blocks;
};

QJsonArray blocksToJson(const BlockList &blocks);
BlockList blocksFromJson(const QJsonArray &array);

// Always produces: [ { "label": "...", "blocks": [...], "isDefault": false }, ... ]
QByteArray serializeCollection(const StrategyMap &strategies);

// Accepts both the [] array format and a single {} strategy object.
QList<ParsedStrategy> parseStrategyFile(const QByteArray &json, QString *error = nullptr);

// Strategy file next to the executable (same place as the C# app), one per window:
// slot 0 (main window) = strategies.json, slot 1 (window "xBot #2", the first one opened
// with "+") = strategies_2.json, slot 2 = strategies_3.json, …
QString strategiesPath(int slot = 0);
bool saveStrategies(const StrategyMap &strategies, int slot = 0);
// A window that has no file of its own yet starts from the main window's strategies.json
// (its first save then creates its own file).
StrategyMap loadStrategies(int slot = 0);
} // namespace ConditionIo
