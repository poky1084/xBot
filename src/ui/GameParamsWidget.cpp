#include "ui/GameParamsWidget.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QColor>
#include <QAbstractItemView>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontMetrics>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
using StakeGames::ParamSpec;
using StakeGames::ParamType;

QString listText(const QJsonValue &v)
{
    if (v.isString())
        return v.toString();
    QStringList parts;
    for (const QJsonValue &e : v.toArray())
        parts << (e.isDouble() ? QString::number(e.toDouble()) : e.toString());
    return parts.join(QStringLiteral(","));
}

QString cardText(const QJsonValue &v)
{
    if (v.isString())
        return v.toString();
    const QJsonObject o = v.toObject();
    return o.value(QStringLiteral("rank")).toString() + o.value(QStringLiteral("suit")).toString();
}

QString jsonText(const QJsonValue &v)
{
    if (v.isArray())
        return QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
    if (v.isObject())
        return QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
    if (v.isString())
        return v.toString();
    return {};
}

QStringList splitList(const QString &text)
{
    return text.split(QRegularExpression(QStringLiteral("[,;\\s]+")), Qt::SkipEmptyParts);
}

// Diamonds color buttons, as in the C# bot (the "orange" gem is shown as Pink).
struct GemColor
{
    const char *tag;
    const char *label;
    const char *css;
};
const GemColor kGems[] = {
    {"red", "Red", "#e53935"},      {"yellow", "Yellow", "#fdd835"}, {"green", "Green", "#43a047"},
    {"blue", "Blue", "#1e88e5"},    {"orange", "Pink", "#ec407a"},   {"purple", "Purple", "#8e24aa"},
    {"cyan", "Cyan", "#00acc1"},
};
constexpr int kMaxGems = 5;

// Drill picks, as on Stake: blue diamond = 1, yellow circle = 2, green triangle = 3.
struct DrillButton
{
    char16_t symbol;
    const char *css;
    const char *name;
};
const DrillButton kDrills[] = {
    {u'\u25C6', "#4fd1ff", "Blue"},   // ◆
    {u'\u25CF', "#ffc107", "Yellow"}, // ●
    {u'\u25B2', "#22c55e", "Green"},  // ▲
};
} // namespace

GameParamsWidget::GameParamsWidget(QWidget *parent)
    : QWidget(parent)
{
    m_form = new QFormLayout(this);
    m_form->setContentsMargins(0, 0, 0, 0);
    m_form->setHorizontalSpacing(12);
    m_form->setVerticalSpacing(6);
}

QJsonObject GameParamsWidget::valuesFor(Game game) const
{
    // Defaults, overlaid with what was entered / loaded for this game. Only the game's own
    // inputs are kept (settings from older versions with other keys are dropped).
    QJsonObject values = StakeGames::defaultParams(game);
    const QJsonObject saved = m_values.value(gameName(game));
    for (auto it = saved.begin(); it != saved.end(); ++it)
        if (values.contains(it.key()) && !m_hiddenKeys.contains(it.key())
            && !StakeGames::unsupportedChoices(m_casino, game, it.key()).contains(it.value().toString()))
            values.insert(it.key(), it.value()); // hidden inputs / choices the casino lacks: default
    // Numbers above the casino's own limit (Shuffle chicken: 17 lanes) are capped.
    for (auto it = values.begin(); it != values.end(); ++it) {
        const double cap = StakeGames::casinoMaximum(m_casino, game, it.key(), values);
        if (cap >= 0.0 && it.value().isDouble() && it.value().toDouble() > cap)
            it.value() = cap;
    }
    return values;
}

void GameParamsWidget::setGame(Game game)
{
    if (game == m_game && !m_fields.isEmpty())
        return;
    m_game = game;
    rebuild();
}

void GameParamsWidget::setCasino(const QString &casinoKey)
{
    const QString key = casinoKey.trimmed().toLower();
    if (key == m_casino)
        return;
    m_casino = key;
    rebuild();
}

void GameParamsWidget::setHiddenKeys(const QStringList &keys)
{
    m_hiddenKeys = keys;
    rebuild();
}

void GameParamsWidget::rebuild()
{
    m_building = true;
    while (m_form->rowCount() > 0)
        m_form->removeRow(0); // deletes the label and editor widgets
    m_fields.clear();

    const QJsonObject values = valuesFor(m_game);
    for (const ParamSpec &spec : StakeGames::paramSpecs(m_game)) {
        if (m_hiddenKeys.contains(spec.key) || StakeGames::casinoHidesParam(m_casino, m_game, spec.key))
            continue;
        const QJsonValue v = values.value(spec.key);
        QWidget *editor = nullptr;   // the widget the value is read from
        QWidget *rowWidget = nullptr; // what goes into the form (editor or a composite)
        bool spanRow = false;         // check boxes and notes use the whole row
        switch (spec.type) {
        case ParamType::Int: {
            int top = int(spec.max > spec.min ? spec.max : 1000000);
            const double cap = StakeGames::casinoMaximum(m_casino, m_game, spec.key, values);
            if (cap >= 0.0)
                top = std::min(top, int(cap)); // the casino's own limit (Shuffle chicken lanes)
            if (spec.dropdown) {
                // A drop-down list of every allowed value (steps: 1 .. the casino's maximum).
                auto *combo = new QComboBox;
                const int want = v.isDouble() ? int(std::llround(v.toDouble())) : int(spec.def.toDouble());
                for (int n = int(spec.min); n <= top; ++n)
                    combo->addItem(QString::number(n), n);
                const int idx = combo->findData(std::clamp(want, int(spec.min), std::max(int(spec.min), top)));
                combo->setCurrentIndex(idx >= 0 ? idx : 0);
                combo->setMaxVisibleItems(12);
                connect(combo, &QComboBox::currentIndexChanged, this, [this] { storeCurrent(); });
                editor = combo;
                break;
            }
            auto *spin = new QSpinBox;
            spin->setRange(int(spec.min), top);
            spin->setValue(v.isDouble() ? int(std::llround(v.toDouble())) : int(spec.def.toDouble()));
            spin->setKeyboardTracking(false);
            connect(spin, &QSpinBox::valueChanged, this, [this] { storeCurrent(); });
            editor = spin;
            break;
        }
        case ParamType::Number: {
            auto *spin = new QDoubleSpinBox;
            spin->setDecimals(spec.decimals);
            spin->setRange(spec.min, spec.max > spec.min ? spec.max : 1e9);
            spin->setSingleStep(spec.decimals >= 8 ? 0.00000001 : 1.0);
            spin->setValue(v.isDouble() ? v.toDouble() : spec.def.toDouble());
            spin->setKeyboardTracking(false);
            connect(spin, &QDoubleSpinBox::valueChanged, this, [this] { storeCurrent(); });
            editor = spin;
            break;
        }
        case ParamType::Choice: {
            auto *combo = new QComboBox; // fixed choices, like the C# panels
            // Each item shows its label (e.g. "♥ Hearts") and carries its value ("H").
            const QStringList lacking = StakeGames::unsupportedChoices(m_casino, m_game, spec.key);
            for (int i = 0; i < spec.choices.size(); ++i) {
                if (lacking.contains(spec.choices.at(i)))
                    continue; // not offered by the selected casino
                const QString shown = i < spec.choiceLabels.size() ? spec.choiceLabels.at(i) : spec.choices.at(i);
                combo->addItem(shown, spec.choices.at(i));
                const QString color = i < spec.choiceColors.size() ? spec.choiceColors.at(i) : QString();
                if (!color.isEmpty())
                    combo->setItemData(combo->count() - 1, QColor(color), Qt::ForegroundRole);
            }
            // A number saved earlier (wheel segments used to be a spin box) picks its item too.
            const QString want = v.isString() ? v.toString()
                                 : v.isDouble() ? QString::number(v.toDouble())
                                                : spec.def.toString();
            int idx = -1;
            for (int i = 0; i < combo->count(); ++i)
                if (combo->itemData(i).toString().compare(want, Qt::CaseInsensitive) == 0)
                    idx = i;
            // The closed box shows the selected item in its color too.
            const QStringList colors = spec.choiceColors;
            const auto paint = [combo, colors] {
                const int i = combo->currentIndex();
                const QString c = i >= 0 && i < colors.size() ? colors.at(i) : QString();
                combo->setStyleSheet(c.isEmpty() ? QString() : QStringLiteral("QComboBox { color: %1; }").arg(c));
            };
            combo->setCurrentIndex(idx >= 0 ? idx : 0);
            // Never cut items short ("♦ Diamonds"): the box is as wide as its longest
            // item and the drop-down list at least as wide as that too.
            combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
            {
                QAbstractItemView *view = combo->view();
                view->setTextElideMode(Qt::ElideNone); // "Diamonds", never "Di...onds"
                // The item width as the style draws it (stylesheet item padding included),
                // or the bare text width plus room, whichever is larger.
                int widest = 0;
                const QFontMetrics fm(view->font());
                for (int i = 0; i < combo->count(); ++i)
                    widest = qMax(widest, fm.horizontalAdvance(combo->itemText(i)));
                const int styled = view->sizeHintForColumn(0);
                view->setMinimumWidth(qMax(styled, widest + 48) + 24); // + scroll bar / frame
            }
            if (!colors.isEmpty())
                paint();
            connect(combo, &QComboBox::currentIndexChanged, this, [this, paint, colors] {
                if (!colors.isEmpty())
                    paint();
                storeCurrent();
            });
            editor = combo;
            break;
        }
        case ParamType::Bool: {
            auto *check = new QCheckBox(spec.label);
            check->setChecked(v.isBool() ? v.toBool() : spec.def.toBool());
            connect(check, &QCheckBox::toggled, this, [this] { storeCurrent(); });
            editor = check;
            spanRow = true;
            break;
        }
        case ParamType::Note: {
            auto *note = new QLabel(spec.label);
            note->setWordWrap(true);
            note->setStyleSheet(QStringLiteral("color:#737373;"));
            rowWidget = note;
            spanRow = true;
            break;
        }
        case ParamType::Colors: {
            // Up to 5 gems picked in order (buttons), shown in an editable box.
            auto *host = new QWidget;
            auto *col = new QVBoxLayout(host);
            col->setContentsMargins(0, 0, 0, 0);
            col->setSpacing(4);
            auto *buttons = new QGridLayout;
            buttons->setSpacing(4);
            auto *edit = new QLineEdit(v.isString() ? v.toString() : QString());
            auto *count = new QLabel;
            auto updateCount = [edit, count] {
                count->setText(QStringLiteral("%1 / %2 selected")
                                   .arg(splitList(edit->text()).size()).arg(kMaxGems));
            };
            int i = 0;
            for (const GemColor &gem : kGems) {
                auto *b = new QPushButton(QString::fromLatin1(gem.label));
                b->setObjectName(QStringLiteral("amountBtn")); // compact padding (app style)
                b->setStyleSheet(QStringLiteral("QPushButton { background:%1; color:white; font-weight:bold; }")
                                     .arg(QLatin1String(gem.css)));
                const QString tag = QString::fromLatin1(gem.tag);
                connect(b, &QPushButton::clicked, this, [this, edit, tag, updateCount] {
                    QStringList picked = splitList(edit->text());
                    if (picked.size() >= kMaxGems)
                        return;
                    picked << tag;
                    edit->setText(picked.join(QLatin1Char(',')));
                    updateCount();
                    storeCurrent();
                });
                buttons->addWidget(b, i / 4, i % 4);
                ++i;
            }
            auto *clear = new QPushButton(tr("Clear"));
            clear->setObjectName(QStringLiteral("amountBtn"));
            connect(clear, &QPushButton::clicked, this, [this, edit, updateCount] {
                edit->clear();
                updateCount();
                storeCurrent();
            });
            buttons->addWidget(clear, i / 4, i % 4);
            col->addLayout(buttons);
            auto *row = new QHBoxLayout;
            row->setSpacing(6);
            row->addWidget(edit, 1);
            row->addWidget(count);
            col->addLayout(row);
            connect(edit, &QLineEdit::editingFinished, this, [this, updateCount] {
                updateCount();
                storeCurrent();
            });
            updateCount();
            editor = edit;
            rowWidget = host;
            break;
        }
        case ParamType::DrillPick: {
            // One button per drill; the selected one is highlighted. The value (1, 2, 3) is
            // kept in the host's "pick" property.
            auto *host = new QWidget;
            host->setObjectName(QStringLiteral("drillPick"));
            host->setStyleSheet(QStringLiteral("QWidget#drillPick { background:#1a2c38; border-radius:6px; }"));
            auto *row = new QHBoxLayout(host);
            row->setContentsMargins(4, 4, 4, 4);
            row->setSpacing(4);
            auto *group = new QButtonGroup(host);
            group->setExclusive(true);
            int pick = v.isDouble() ? int(std::llround(v.toDouble())) : int(spec.def.toDouble());
            if (pick < 1 || pick > 3)
                pick = 1;
            host->setProperty("pick", pick);
            int id = 1;
            for (const DrillButton &d : kDrills) {
                auto *b = new QPushButton(QString(QChar(d.symbol)));
                b->setCheckable(true);
                b->setChecked(id == pick);
                b->setToolTip(tr("%1 = pick %2").arg(QLatin1String(d.name)).arg(id));
                b->setMinimumHeight(30);
                b->setStyleSheet(QStringLiteral(
                    "QPushButton { color:%1; font-size:18px; background:transparent; border:none;"
                    " border-radius:4px; padding:2px 10px; }"
                    "QPushButton:checked { background:#3d5366; }"
                    "QPushButton:hover:!checked { background:#2a3d4c; }").arg(QLatin1String(d.css)));
                group->addButton(b, id);
                row->addWidget(b, 1);
                ++id;
            }
            connect(group, &QButtonGroup::idClicked, this, [this, host](int picked) {
                host->setProperty("pick", picked);
                storeCurrent();
            });
            editor = host;
            break;
        }
        case ParamType::IntList:
        case ParamType::StringList:
        case ParamType::Card:
        case ParamType::Json: {
            auto *edit = new QLineEdit;
            if (spec.type == ParamType::Card)
                edit->setText(cardText(v));
            else if (spec.type == ParamType::Json)
                edit->setText(jsonText(v));
            else
                edit->setText(listText(v));
            connect(edit, &QLineEdit::editingFinished, this, [this] { storeCurrent(); });
            editor = edit;
            break;
        }
        }
        if (!rowWidget)
            rowWidget = editor;
        if (!spec.tip.isEmpty())
            rowWidget->setToolTip(spec.tip);
        QLabel *label = nullptr;
        // Next to another input on its row ("Start card rank: [A]  Suit: [C]").
        if (!spec.sameRowAs.isEmpty() && !spanRow) {
            Field *host = nullptr;
            for (Field &f : m_fields)
                if (f.spec.key == spec.sameRowAs)
                    host = &f;
            int row = -1;
            QFormLayout::ItemRole role = QFormLayout::FieldRole;
            if (host && host->rowWidget)
                m_form->getWidgetPosition(host->rowWidget, &row, &role);
            if (row >= 0 && role == QFormLayout::FieldRole) {
                auto *pair = new QWidget;
                auto *line = new QHBoxLayout(pair);
                line->setContentsMargins(0, 0, 0, 0);
                line->setSpacing(6);
                m_form->removeWidget(host->rowWidget); // the host's field leaves the form ...
                line->addWidget(host->rowWidget, 1);   // ... and joins the shared row
                label = new QLabel(spec.label);
                if (!spec.tip.isEmpty())
                    label->setToolTip(spec.tip);
                line->addWidget(label);
                line->addWidget(rowWidget); // its full width; the host takes the rest
                m_form->setWidget(row, QFormLayout::FieldRole, pair);
                host->rowWidget = pair;
                if (editor)
                    m_fields.append({spec, editor, rowWidget, label});
                continue;
            }
        }
        if (spanRow) {
            m_form->addRow(rowWidget);
        } else {
            label = new QLabel(spec.label);
            if (!spec.tip.isEmpty())
                label->setToolTip(spec.tip);
            m_form->addRow(label, rowWidget);
        }
        if (editor)
            m_fields.append({spec, editor, rowWidget, label});
    }
    m_hasRows = m_form->rowCount() > 0;
    m_building = false;
    updateVisibility();
}

void GameParamsWidget::updateVisibility()
{
    for (const Field &f : std::as_const(m_fields)) {
        if (f.spec.showWhenKey.isEmpty())
            continue;
        bool show = false;
        for (const Field &ctl : std::as_const(m_fields))
            if (ctl.spec.key == f.spec.showWhenKey) {
                const QString v = readEditor(ctl).toVariant().toString();
                for (const QString &want : f.spec.showWhenValues)
                    if (v.compare(want, Qt::CaseInsensitive) == 0)
                        show = true;
            }
        if (f.rowWidget)
            f.rowWidget->setVisible(show);
        if (f.label)
            f.label->setVisible(show);
    }
}

QJsonValue GameParamsWidget::readEditor(const Field &field) const
{
    const ParamSpec &spec = field.spec;
    switch (spec.type) {
    case ParamType::Int:
        if (const auto *combo = qobject_cast<const QComboBox *>(field.editor))
            return combo->currentData().toInt(); // drop-down Int
        return static_cast<QSpinBox *>(field.editor)->value();
    case ParamType::Number:
        return static_cast<QDoubleSpinBox *>(field.editor)->value();
    case ParamType::Choice: {
        const auto *combo = static_cast<QComboBox *>(field.editor);
        const QString t = combo->currentData().toString().trimmed(); // the value, not the label
        return t.isEmpty() ? spec.def : QJsonValue(t);
    }
    case ParamType::Bool:
        return static_cast<QCheckBox *>(field.editor)->isChecked();
    case ParamType::DrillPick:
        return field.editor->property("pick").toInt();
    case ParamType::Colors:
        return splitList(static_cast<QLineEdit *>(field.editor)->text().toLower()).join(QLatin1Char(','));
    case ParamType::IntList: {
        QJsonArray a;
        for (const QString &p : splitList(static_cast<QLineEdit *>(field.editor)->text())) {
            bool ok = false;
            const int n = p.toInt(&ok);
            if (ok)
                a.append(n);
        }
        return a;
    }
    case ParamType::StringList: {
        QJsonArray a;
        for (const QString &p : splitList(static_cast<QLineEdit *>(field.editor)->text()))
            a.append(p);
        return a;
    }
    case ParamType::Card: {
        const QString t = static_cast<QLineEdit *>(field.editor)->text().trimmed();
        return t.isEmpty() ? spec.def : QJsonValue(t);
    }
    case ParamType::Json: {
        const QString t = static_cast<QLineEdit *>(field.editor)->text().trimmed();
        if (t.isEmpty())
            return QJsonValue(QJsonValue::Null);
        const QJsonDocument doc = QJsonDocument::fromJson(t.toUtf8());
        if (doc.isArray())
            return doc.array();
        if (doc.isObject())
            return QJsonArray{doc.object()};
        return spec.def; // not valid JSON: keep the default
    }
    case ParamType::Note:
        break;
    }
    return spec.def;
}

void GameParamsWidget::storeCurrent()
{
    if (m_building)
        return;
    updateVisibility(); // e.g. primedice X: T3 / T4 follow the condition
    QJsonObject values;
    for (const Field &f : std::as_const(m_fields))
        values.insert(f.spec.key, readEditor(f));
    // A casino limit that depends on another input (Shuffle chicken: lanes per difficulty).
    m_building = true; // a lowered maximum moves the value: no nested store
    for (const Field &f : std::as_const(m_fields)) {
        if (f.spec.type != StakeGames::ParamType::Int)
            continue;
        const double cap = StakeGames::casinoMaximum(m_casino, m_game, f.spec.key, values);
        if (cap < 0.0)
            continue;
        const int top = std::min(int(f.spec.max > f.spec.min ? f.spec.max : 1000000), int(cap));
        if (auto *combo = qobject_cast<QComboBox *>(f.editor)) {
            // Drop-down Int: refill it with min .. the new maximum, keeping the value if it fits.
            const int count = std::max(0, top - int(f.spec.min) + 1);
            if (combo->count() != count) {
                const int keep = std::min(combo->currentData().toInt(), top);
                combo->clear();
                for (int n = int(f.spec.min); n <= top; ++n)
                    combo->addItem(QString::number(n), n);
                const int idx = combo->findData(std::max(int(f.spec.min), keep));
                combo->setCurrentIndex(idx >= 0 ? idx : 0);
                values.insert(f.spec.key, combo->currentData().toInt());
            }
            continue;
        }
        auto *spin = qobject_cast<QSpinBox *>(f.editor);
        if (!spin)
            continue;
        if (spin->maximum() != top) {
            spin->setMaximum(top);
            values.insert(f.spec.key, spin->value());
        }
    }
    m_building = false;
    m_values.insert(gameName(m_game), values);
    emit paramsChanged();
}

QJsonObject GameParamsWidget::params() const
{
    if (isDiceOrLimbo(m_game))
        return {};
    return valuesFor(m_game);
}

QJsonObject GameParamsWidget::allParams() const
{
    QJsonObject all;
    for (auto it = m_values.begin(); it != m_values.end(); ++it)
        all.insert(it.key(), it.value());
    return all;
}

void GameParamsWidget::setAllParams(const QJsonObject &all)
{
    m_values.clear();
    for (auto it = all.begin(); it != all.end(); ++it) {
        Game g = Game::Dice;
        if (it.value().isObject() && gameFromName(it.key(), &g))
            m_values.insert(gameName(g), it.value().toObject());
    }
    rebuild();
}
