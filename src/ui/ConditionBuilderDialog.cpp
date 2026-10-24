#include "ConditionBuilderDialog.h"

#include "ui/BlockEditorDialog.h"

#include <QAction>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace
{
// 1 -> "1", 100.5 -> "100.5" (no trailing zeros).
QString num(double v)
{
    QString s = QLocale::c().toString(v, 'f', 8);
    if (s.contains(QLatin1Char('.'))) {
        while (s.endsWith(QLatin1Char('0')))
            s.chop(1);
        if (s.endsWith(QLatin1Char('.')))
            s.chop(1);
    }
    return s;
}

const char *kJsonFilter = "JSON files (*.json);;All files (*)";
} // namespace

ConditionBuilderDialog::ConditionBuilderDialog(const BlockList &existing,
                                               const StrategyMap &namedStrategies,
                                               const QString &activeStrategyName,
                                               QWidget *parent)
    : QDialog(parent)
    , m_blocks(existing)
    , m_strategies(namedStrategies)
{
    setWindowTitle(tr("Condition Builder"));
    setModal(true);
    resize(700, 460);

    buildUi(activeStrategyName);
    refreshList();
    refreshStrategyCombo(activeStrategyName);
}

// ═══════════════════════════════════════════════════════════════ UI

void ConditionBuilderDialog::buildUi(const QString &activeName)
{
    // Row 1: Strategy name | Load | Save Delete New
    m_name = new QLineEdit(activeName);

    m_strategyCombo = new QComboBox;
    m_strategyCombo->setMinimumWidth(170);
    connect(m_strategyCombo, &QComboBox::currentIndexChanged, this,
            &ConditionBuilderDialog::onStrategySelected);

    auto *btnSave = new QPushButton(tr("Save"));
    auto *btnDelete = new QPushButton(tr("Delete"));
    auto *btnNew = new QPushButton(tr("New"));
    connect(btnSave, &QPushButton::clicked, this, &ConditionBuilderDialog::saveStrategy);
    connect(btnDelete, &QPushButton::clicked, this, &ConditionBuilderDialog::deleteStrategy);
    connect(btnNew, &QPushButton::clicked, this, &ConditionBuilderDialog::newStrategy);

    auto *row1 = new QHBoxLayout;
    row1->addWidget(new QLabel(tr("Strategy:")));
    row1->addWidget(m_name, 1);
    row1->addSpacing(8);
    row1->addWidget(new QLabel(tr("Load:")));
    row1->addWidget(m_strategyCombo);
    row1->addWidget(btnSave);
    row1->addWidget(btnDelete);
    row1->addWidget(btnNew);

    // Row 2: Export (menu) | Import | Delete All
    m_btnExport = new QPushButton(tr("Export"));
    auto *exportMenu = new QMenu(m_btnExport);
    QAction *actCurrent = exportMenu->addAction(tr("EXPORT SELECTED"));
    m_exportAllAction = exportMenu->addAction(tr("EXPORT ALL"));
    connect(actCurrent, &QAction::triggered, this, &ConditionBuilderDialog::exportCurrent);
    connect(m_exportAllAction, &QAction::triggered, this, &ConditionBuilderDialog::exportAll);
    connect(exportMenu, &QMenu::aboutToShow, this,
            [this] { m_exportAllAction->setEnabled(!m_strategies.isEmpty()); });
    m_btnExport->setMenu(exportMenu);

    auto *btnImport = new QPushButton(tr("Import"));
    auto *btnDeleteAll = new QPushButton(tr("Delete All"));
    btnDeleteAll->setStyleSheet(QStringLiteral("QPushButton { color: #8b0000; }"));
    connect(btnImport, &QPushButton::clicked, this, &ConditionBuilderDialog::importStrategies);
    connect(btnDeleteAll, &QPushButton::clicked, this,
            &ConditionBuilderDialog::deleteAllStrategies);

    auto *row2 = new QHBoxLayout;
    row2->addWidget(m_btnExport);
    row2->addWidget(btnImport);
    row2->addWidget(btnDeleteAll);
    row2->addStretch(1);

    auto *line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);

    // Block list
    m_table = new QTableWidget(0, 4);
    m_table->setHorizontalHeaderLabels({tr("#"), tr("Type"), tr("When"), tr("Then")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setShowGrid(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    connect(m_table, &QTableWidget::cellDoubleClicked, this,
            [this](int, int) { editSelected(); });

    // Side buttons
    auto *btnAdd = new QPushButton(tr("Add"));
    auto *btnEdit = new QPushButton(tr("Edit"));
    auto *btnRemove = new QPushButton(tr("Remove"));
    auto *btnUp = new QPushButton(tr("▲ Up"));
    auto *btnDown = new QPushButton(tr("▼ Down"));
    auto *btnOk = new QPushButton(tr("OK"));
    auto *btnCancel = new QPushButton(tr("Cancel"));
    btnOk->setDefault(true);
    connect(btnAdd, &QPushButton::clicked, this, &ConditionBuilderDialog::addBlock);
    connect(btnEdit, &QPushButton::clicked, this, &ConditionBuilderDialog::editSelected);
    connect(btnRemove, &QPushButton::clicked, this, &ConditionBuilderDialog::removeSelected);
    connect(btnUp, &QPushButton::clicked, this, [this] { moveSelected(-1); });
    connect(btnDown, &QPushButton::clicked, this, [this] { moveSelected(1); });
    connect(btnOk, &QPushButton::clicked, this, &ConditionBuilderDialog::accept);
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);

    auto *side = new QVBoxLayout;
    for (QPushButton *b : {btnAdd, btnEdit, btnRemove, btnUp, btnDown}) {
        b->setMinimumWidth(100);
        side->addWidget(b);
    }
    side->addSpacing(8);
    btnOk->setMinimumWidth(100);
    btnCancel->setMinimumWidth(100);
    side->addWidget(btnOk);
    side->addWidget(btnCancel);
    side->addStretch(1);

    auto *body = new QHBoxLayout;
    body->addWidget(m_table, 1);
    body->addLayout(side);

    auto *root = new QVBoxLayout(this);
    root->addLayout(row1);
    root->addLayout(row2);
    root->addWidget(line);
    root->addLayout(body, 1);
}

// ═══════════════════════════════════════════════════════════════ Block list

void ConditionBuilderDialog::refreshList()
{
    m_table->setRowCount(0);
    m_table->setRowCount(m_blocks.size());
    for (int i = 0; i < m_blocks.size(); ++i) {
        const ConditionBlockData &b = m_blocks.at(i);
        m_table->setItem(i, 0, new QTableWidgetItem(QString::number(i + 1)));
        m_table->setItem(i, 1, new QTableWidgetItem(b.type));
        m_table->setItem(i, 2, new QTableWidgetItem(describeOn(b)));
        m_table->setItem(i, 3, new QTableWidgetItem(describeDo(b)));
    }
}

QString ConditionBuilderDialog::describeOn(const ConditionBlockData &b)
{
    if (b.type == QLatin1String("bets"))
        return QStringLiteral("%1 %2 streak of %3").arg(b.onType, b.betType, num(b.onValue));
    return QStringLiteral("%1 %2 %3").arg(b.profitType, b.onType, num(b.onValue));
}

QString ConditionBuilderDialog::describeDo(const ConditionBlockData &b)
{
    const QString v = num(b.doValue);
    const QString &t = b.doType;
    if (t == QLatin1String("increaseByPercentage")) return QStringLiteral("Increase bet by %1%").arg(v);
    if (t == QLatin1String("decreaseByPercentage")) return QStringLiteral("Decrease bet by %1%").arg(v);
    if (t == QLatin1String("increaseWinChanceBy")) return QStringLiteral("Increase chance by %1%").arg(v);
    if (t == QLatin1String("decreaseWinChanceBy")) return QStringLiteral("Decrease chance by %1%").arg(v);
    if (t == QLatin1String("addToAmount")) return QStringLiteral("Add %1 to bet").arg(v);
    if (t == QLatin1String("subtractFromAmount")) return QStringLiteral("Subtract %1 from bet").arg(v);
    if (t == QLatin1String("addToWinChance")) return QStringLiteral("Add %1 to chance").arg(v);
    if (t == QLatin1String("subtractFromWinChance")) return QStringLiteral("Subtract %1 from chance").arg(v);
    if (t == QLatin1String("setAmount")) return QStringLiteral("Set bet to %1").arg(v);
    if (t == QLatin1String("setWinChance")) return QStringLiteral("Set chance to %1").arg(v);
    if (t == QLatin1String("switchOverUnder")) return QStringLiteral("Switch Over/Under");
    if (t == QLatin1String("resetAmount")) return QStringLiteral("Reset bet to base");
    if (t == QLatin1String("resetWinChance")) return QStringLiteral("Reset chance to base");
    if (t == QLatin1String("stop")) return QStringLiteral("Stop bot");
    return t;
}

void ConditionBuilderDialog::addBlock()
{
    BlockEditorDialog dlg(nullptr, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    m_blocks.append(dlg.result());
    refreshList();
    m_table->selectRow(m_blocks.size() - 1);
}

void ConditionBuilderDialog::editSelected()
{
    const int idx = m_table->currentRow();
    if (idx < 0 || idx >= m_blocks.size())
        return;
    const ConditionBlockData current = m_blocks.at(idx);
    BlockEditorDialog dlg(&current, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    m_blocks[idx] = dlg.result();
    refreshList();
    m_table->selectRow(idx);
}

void ConditionBuilderDialog::removeSelected()
{
    const int idx = m_table->currentRow();
    if (idx < 0 || idx >= m_blocks.size())
        return;
    m_blocks.removeAt(idx);
    refreshList();
}

void ConditionBuilderDialog::moveSelected(int dir)
{
    const int idx = m_table->currentRow();
    const int newIdx = idx + dir;
    if (idx < 0 || newIdx < 0 || newIdx >= m_blocks.size())
        return;
    m_blocks.swapItemsAt(idx, newIdx);
    refreshList();
    m_table->selectRow(newIdx);
}

void ConditionBuilderDialog::accept()
{
    m_resultBlocks = m_blocks;
    m_resultStrategies = m_strategies;
    m_resultName = m_name->text().trimmed();
    QDialog::accept();
}

// ═══════════════════════════════════════════════════════════════ Strategies

void ConditionBuilderDialog::refreshStrategyCombo(const QString &selectName)
{
    const QSignalBlocker blocker(m_strategyCombo);
    m_strategyCombo->clear();
    m_strategyCombo->addItems(m_strategies.keys()); // QMap keys are sorted
    const int i = selectName.isEmpty() ? -1 : m_strategyCombo->findText(selectName);
    m_strategyCombo->setCurrentIndex(i);
}

void ConditionBuilderDialog::onStrategySelected(int index)
{
    if (index < 0)
        return;
    const QString name = m_strategyCombo->itemText(index);
    m_blocks = m_strategies.value(name);
    m_name->setText(name);
    refreshList();
}

void ConditionBuilderDialog::saveStrategy()
{
    const QString name = m_name->text().trimmed();
    if (name.isEmpty()) {
        QMessageBox::warning(this, tr("Save Strategy"), tr("Please enter a strategy name."));
        m_name->setFocus();
        return;
    }

    m_strategies[name] = m_blocks;
    refreshStrategyCombo(name);
    autoSaveStrategies();

    // Clear the active block list and name, ready for a new strategy.
    m_blocks.clear();
    m_name->clear();
    refreshList();
}

void ConditionBuilderDialog::deleteStrategy()
{
    if (m_strategyCombo->currentIndex() < 0)
        return;
    const QString name = m_strategyCombo->currentText();
    m_strategies.remove(name);
    if (m_name->text().trimmed() == name)
        m_name->clear();
    refreshStrategyCombo();
    autoSaveStrategies();
}

void ConditionBuilderDialog::deleteAllStrategies()
{
    if (m_strategies.isEmpty())
        return;
    const auto ans = QMessageBox::warning(
        this, tr("Delete All Strategies"),
        tr("Delete all %1 saved strategies? This cannot be undone.").arg(m_strategies.size()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ans != QMessageBox::Yes)
        return;
    m_strategies.clear();
    refreshStrategyCombo();
    autoSaveStrategies();
}

void ConditionBuilderDialog::newStrategy()
{
    m_blocks.clear();
    m_name->clear();
    {
        const QSignalBlocker blocker(m_strategyCombo);
        m_strategyCombo->setCurrentIndex(-1);
    }
    refreshList();
}

// ═══════════════════════════════════════════════════════════════ Export / import

bool ConditionBuilderDialog::writeFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(data) < 0) {
        QMessageBox::warning(this, tr("Export Error"),
                             tr("Export failed:\n%1").arg(f.errorString()));
        return false;
    }
    return true;
}

void ConditionBuilderDialog::exportCurrent()
{
    const QString label = m_name->text().trimmed();
    QString path = QFileDialog::getSaveFileName(
        this, tr("Export Current Strategy"), label.isEmpty() ? QStringLiteral("strategy") : label,
        tr(kJsonFilter));
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().isEmpty())
        path += QStringLiteral(".json");

    StrategyMap single;
    single.insert(label.isEmpty() ? QStringLiteral("(unnamed)") : label, m_blocks);
    writeFile(path, ConditionIo::serializeCollection(single));
}

void ConditionBuilderDialog::exportAll()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Export All Strategies"),
                                                QStringLiteral("strategies"), tr(kJsonFilter));
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().isEmpty())
        path += QStringLiteral(".json");
    writeFile(path, ConditionIo::serializeCollection(m_strategies));
}

void ConditionBuilderDialog::importStrategies()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Import Strategy"), QString(),
                                                      tr(kJsonFilter));
    if (path.isEmpty())
        return;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, tr("Import Error"),
                              tr("Import failed:\n%1").arg(f.errorString()));
        return;
    }

    QString error;
    const auto parsed = ConditionIo::parseStrategyFile(f.readAll(), &error);
    if (!error.isEmpty()) {
        QMessageBox::critical(this, tr("Import Error"), tr("Import failed:\n%1").arg(error));
        return;
    }
    if (parsed.isEmpty()) {
        QMessageBox::warning(this, tr("Import"), tr("No valid strategies found in the file."));
        return;
    }

    if (parsed.size() == 1)
        importSingle(parsed.first().label, parsed.first().blocks);
    else
        importMultiple(parsed);
}

void ConditionBuilderDialog::importSingle(const QString &label, const BlockList &blocks)
{
    // Add to the saved list so it appears in the Load combo, and load it as active.
    m_strategies[label] = blocks;
    refreshStrategyCombo(label);
    autoSaveStrategies();

    m_blocks = blocks;
    m_name->setText(label);
    refreshList();
}

void ConditionBuilderDialog::importMultiple(const QList<ConditionIo::ParsedStrategy> &parsed)
{
    QStringList names;
    for (const auto &p : parsed)
        names << p.label;

    const QString chosen = pickStrategy(
        names, tr("File contains %1 strategies.\nSelect one to load, or click \"Import All\" to "
                  "add all to the Load dropdown.")
                   .arg(parsed.size()));

    if (!chosen.isEmpty()) {
        for (const auto &p : parsed) {
            if (p.label != chosen)
                continue;
            m_strategies[p.label] = p.blocks;
            refreshStrategyCombo(p.label);
            autoSaveStrategies();
            m_blocks = p.blocks;
            m_name->setText(p.label);
            refreshList();
            break;
        }
    } else {
        for (const auto &p : parsed)
            m_strategies[p.label] = p.blocks;
        refreshStrategyCombo();
        autoSaveStrategies();
    }
}

QString ConditionBuilderDialog::pickStrategy(const QStringList &names, const QString &prompt)
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Select Strategy"));
    dlg.resize(380, 420);

    auto *label = new QLabel(prompt);
    label->setWordWrap(true);

    auto *list = new QListWidget;
    list->addItems(names);
    if (list->count() > 0)
        list->setCurrentRow(0);

    auto *btnLoad = new QPushButton(tr("Load Selected"));
    auto *btnAll = new QPushButton(tr("Import All"));
    btnLoad->setDefault(true);

    QString chosen;
    auto pick = [&] {
        if (!list->currentItem())
            return;
        chosen = list->currentItem()->text();
        dlg.accept();
    };
    connect(btnLoad, &QPushButton::clicked, &dlg, pick);
    connect(list, &QListWidget::itemDoubleClicked, &dlg, pick);
    connect(btnAll, &QPushButton::clicked, &dlg, &QDialog::reject);

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(btnLoad);
    buttons->addWidget(btnAll);

    auto *root = new QVBoxLayout(&dlg);
    root->addWidget(label);
    root->addWidget(list, 1);
    root->addLayout(buttons);

    dlg.exec();
    return chosen;
}

void ConditionBuilderDialog::autoSaveStrategies()
{
    ConditionIo::saveStrategies(m_strategies); // never interrupt the UI for a background save
}
