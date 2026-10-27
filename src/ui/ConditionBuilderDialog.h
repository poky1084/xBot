#pragma once

#include "ui/ConditionBlock.h"

#include <QDialog>

class QComboBox;
class QLineEdit;
class QMenu;
class QPushButton;
class QTableWidget;

// "Condition Builder" popup (port of ConditionBuilderForm.cs).
class ConditionBuilderDialog : public QDialog
{
    Q_OBJECT

public:
    ConditionBuilderDialog(const BlockList &existing,
                           const StrategyMap &namedStrategies = {},
                           const QString &activeStrategyName = QString(),
                           QWidget *parent = nullptr);

    BlockList result() const { return m_resultBlocks; }
    StrategyMap resultStrategies() const { return m_resultStrategies; }
    QString resultStrategyName() const { return m_resultName; }

    // Window slot whose strategy file the autosave writes (see ConditionIo::strategiesPath()).
    void setStrategiesSlot(int slot) { m_strategiesSlot = slot; }

private slots:
    void onStrategySelected(int index);
    void saveStrategy();
    void deleteStrategy();
    void newStrategy();
    void deleteAllStrategies();
    void exportCurrent();
    void exportAll();
    void importStrategies();

    void addBlock();
    void editSelected();
    void removeSelected();
    void moveSelected(int dir);

private:
    void accept() override;

    void buildUi(const QString &activeName);
    void refreshList();
    void refreshStrategyCombo(const QString &selectName = QString());
    void autoSaveStrategies();
    void importSingle(const QString &label, const BlockList &blocks);
    void importMultiple(const QList<ConditionIo::ParsedStrategy> &parsed);
    QString pickStrategy(const QStringList &names, const QString &prompt);
    bool writeFile(const QString &path, const QByteArray &bytes);

    static QString describeOn(const ConditionBlockData &b);
    static QString describeDo(const ConditionBlockData &b);

    BlockList m_blocks;
    StrategyMap m_strategies;
    int m_strategiesSlot = 0;

    QLineEdit *m_name = nullptr;
    QComboBox *m_strategyCombo = nullptr;
    QPushButton *m_btnExport = nullptr;
    QAction *m_exportAllAction = nullptr;
    QTableWidget *m_table = nullptr;

    BlockList m_resultBlocks;
    StrategyMap m_resultStrategies;
    QString m_resultName;
};
