#include "ConditionBlock.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QUuid>

QString ConditionBlockData::newId()
{
    return QUuid::createUuid().toString(QUuid::Id128).left(8);
}

namespace ConditionIo
{

QJsonArray blocksToJson(const BlockList &blocks)
{
    QJsonArray arr;
    for (const ConditionBlockData &b : blocks) {
        QJsonObject on;
        on[QStringLiteral("type")] = b.onType;
        on[QStringLiteral("value")] = b.onValue;
        on[QStringLiteral("betType")] = b.betType;
        on[QStringLiteral("profitType")] = b.profitType;

        QJsonObject doObj;
        doObj[QStringLiteral("type")] = b.doType;
        doObj[QStringLiteral("value")] = b.doValue;

        QJsonObject o;
        o[QStringLiteral("id")] = b.id;
        o[QStringLiteral("type")] = b.type;
        o[QStringLiteral("on")] = on;
        o[QStringLiteral("do")] = doObj;
        arr.append(o);
    }
    return arr;
}

static double numberOr(const QJsonValue &v, double fallback)
{
    if (v.isDouble())
        return v.toDouble();
    if (v.isString()) {
        bool ok = false;
        const double d = v.toString().toDouble(&ok);
        return ok ? d : fallback;
    }
    return fallback;
}

BlockList blocksFromJson(const QJsonArray &array)
{
    BlockList list;
    for (const QJsonValue &item : array) {
        if (!item.isObject())
            continue;
        const QJsonObject block = item.toObject();
        const QJsonObject on = block.value(QStringLiteral("on")).toObject();
        const QJsonObject doObj = block.value(QStringLiteral("do")).toObject();

        ConditionBlockData b;
        const QString id = block.value(QStringLiteral("id")).toVariant().toString();
        if (!id.isEmpty())
            b.id = id;
        b.type = block.value(QStringLiteral("type")).toString(QStringLiteral("bets"));
        b.onType = on.value(QStringLiteral("type")).toString(QStringLiteral("every"));
        b.onValue = numberOr(on.value(QStringLiteral("value")), 1.0);
        b.betType = on.value(QStringLiteral("betType")).toString(QStringLiteral("bet"));
        b.profitType = on.value(QStringLiteral("profitType")).toString(QStringLiteral("profit"));
        b.doType = doObj.value(QStringLiteral("type")).toString(QStringLiteral("stop"));
        b.doValue = numberOr(doObj.value(QStringLiteral("value")), 0.0);
        list.append(b);
    }
    return list;
}

QByteArray serializeCollection(const StrategyMap &strategies)
{
    QJsonArray arr;
    for (auto it = strategies.constBegin(); it != strategies.constEnd(); ++it) {
        QJsonObject o;
        o[QStringLiteral("label")] = it.key();
        o[QStringLiteral("blocks")] = blocksToJson(it.value());
        o[QStringLiteral("isDefault")] = false;
        arr.append(o);
    }
    return QJsonDocument(arr).toJson(QJsonDocument::Indented);
}

static ParsedStrategy parseSingle(const QJsonObject &root)
{
    ParsedStrategy s;
    s.label = root.value(QStringLiteral("label")).toString(QStringLiteral("(unnamed)"));
    s.blocks = blocksFromJson(root.value(QStringLiteral("blocks")).toArray());
    return s;
}

QList<ParsedStrategy> parseStrategyFile(const QByteArray &json, QString *error)
{
    QList<ParsedStrategy> result;
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &perr);
    if (doc.isNull()) {
        if (error)
            *error = perr.errorString();
        return result;
    }
    if (doc.isArray()) {
        for (const QJsonValue &v : doc.array())
            if (v.isObject())
                result.append(parseSingle(v.toObject()));
    } else if (doc.isObject()) {
        result.append(parseSingle(doc.object()));
    }
    return result;
}

QString strategiesPath()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("strategies.json"));
}

bool saveStrategies(const StrategyMap &strategies)
{
    QFile f(strategiesPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return f.write(serializeCollection(strategies)) >= 0;
}

StrategyMap loadStrategies()
{
    StrategyMap map;
    QFile f(strategiesPath());
    if (!f.open(QIODevice::ReadOnly))
        return map;
    const QList<ParsedStrategy> parsed = parseStrategyFile(f.readAll());
    for (const ParsedStrategy &p : parsed)
        map.insert(p.label, p.blocks);
    return map;
}

} // namespace ConditionIo
