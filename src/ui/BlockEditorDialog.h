#pragma once

#include "ui/ConditionBlock.h"

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QStackedWidget;

// "Add / Edit Condition Block" popup (port of BlockEditorForm.cs).
class BlockEditorDialog : public QDialog
{
    Q_OBJECT

public:
    explicit BlockEditorDialog(const ConditionBlockData *existing = nullptr,
                               QWidget *parent = nullptr);

    ConditionBlockData result() const { return m_result; }

private slots:
    void updateOnSection();

private:
    void populate(const ConditionBlockData &b);
    void accept() override;

    QComboBox *m_type = nullptr;
    QComboBox *m_onType = nullptr;
    QComboBox *m_betType = nullptr;
    QComboBox *m_profitType = nullptr;
    QComboBox *m_doType = nullptr;
    QDoubleSpinBox *m_onValue = nullptr;
    QDoubleSpinBox *m_doValue = nullptr;
    QLabel *m_subLabel = nullptr;
    QStackedWidget *m_subStack = nullptr;

    ConditionBlockData m_result;
    QString m_keepId;
};
