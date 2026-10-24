#include "BlockEditorDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QLabel>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace
{
const QStringList &betOnTypes()
{
    static const QStringList l{"every", "everyStreakOf", "firstStreakOf", "streakGreaterThan",
                               "streakLowerThan"};
    return l;
}
const QStringList &profitOnTypes()
{
    static const QStringList l{"greaterThan", "greaterThanOrEqualTo", "lowerThan",
                               "lowerThanOrEqualTo"};
    return l;
}
const QStringList &betTypes()
{
    static const QStringList l{"bet", "win", "lose"};
    return l;
}
const QStringList &profitTypes()
{
    static const QStringList l{"profit", "loss", "balance"};
    return l;
}
const QStringList &doTypes()
{
    static const QStringList l{"increaseByPercentage", "decreaseByPercentage",
                               "increaseWinChanceBy",  "decreaseWinChanceBy",
                               "addToAmount",          "subtractFromAmount",
                               "addToWinChance",       "subtractFromWinChance",
                               "setAmount",            "setWinChance",
                               "switchOverUnder",      "resetAmount",
                               "resetWinChance",       "stop"};
    return l;
}

QComboBox *makeCombo(const QStringList &items)
{
    auto *cb = new QComboBox;
    cb->addItems(items);
    return cb;
}

QDoubleSpinBox *makeSpin(int decimals)
{
    auto *s = new QDoubleSpinBox;
    s->setRange(-99999999.0, 99999999.0);
    s->setDecimals(decimals);
    s->setGroupSeparatorShown(false);
    return s;
}

void setCombo(QComboBox *cb, const QString &value)
{
    const int i = cb->findText(value);
    if (i >= 0)
        cb->setCurrentIndex(i);
}
} // namespace

BlockEditorDialog::BlockEditorDialog(const ConditionBlockData *existing, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(existing ? tr("Edit Condition Block") : tr("Add Condition Block"));
    setModal(true);

    m_type = makeCombo({"bets", "profit"});
    m_onType = makeCombo(betOnTypes());
    m_betType = makeCombo(betTypes());
    m_profitType = makeCombo(profitTypes());
    m_doType = makeCombo(doTypes());
    m_onValue = makeSpin(8);
    m_doValue = makeSpin(4);

    // "Bet type" (bets) and "Profit of" (profit) share one row.
    m_subLabel = new QLabel(tr("Bet type:"));
    m_subStack = new QStackedWidget;
    m_subStack->addWidget(m_betType);
    m_subStack->addWidget(m_profitType);

    auto *form = new QFormLayout;
    form->addRow(tr("Condition type:"), m_type);
    form->addRow(tr("When:"), m_onType);
    form->addRow(m_subLabel, m_subStack);
    form->addRow(tr("Value:"), m_onValue);

    auto *sep = new QFrame;
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);

    auto *form2 = new QFormLayout;
    form2->addRow(tr("Then:"), m_doType);
    form2->addRow(tr("With value:"), m_doValue);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &BlockEditorDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addWidget(sep);
    root->addLayout(form2);
    root->addWidget(buttons);

    connect(m_type, &QComboBox::currentIndexChanged, this, &BlockEditorDialog::updateOnSection);

    if (existing) {
        m_keepId = existing->id;
        populate(*existing);
    }
    updateOnSection();
    setFixedSize(sizeHint().expandedTo(QSize(420, 0)));
}

void BlockEditorDialog::populate(const ConditionBlockData &b)
{
    setCombo(m_type, b.type); // also swaps the "When" items via updateOnSection
    updateOnSection();
    setCombo(m_onType, b.onType);
    setCombo(m_betType, b.betType);
    setCombo(m_profitType, b.profitType);
    setCombo(m_doType, b.doType);
    m_onValue->setValue(b.onValue);
    m_doValue->setValue(b.doValue);
}

void BlockEditorDialog::updateOnSection()
{
    const bool isBets = m_type->currentText() == QLatin1String("bets");
    m_subLabel->setText(isBets ? tr("Bet type:") : tr("Profit of:"));
    m_subStack->setCurrentIndex(isBets ? 0 : 1);

    const QString cur = m_onType->currentText();
    m_onType->clear();
    m_onType->addItems(isBets ? betOnTypes() : profitOnTypes());
    const int i = m_onType->findText(cur);
    m_onType->setCurrentIndex(i >= 0 ? i : 0);
}

void BlockEditorDialog::accept()
{
    const QString doType = m_doType->currentText();
    const bool doNeedsValue = doType != QLatin1String("switchOverUnder")
                              && doType != QLatin1String("resetAmount")
                              && doType != QLatin1String("resetWinChance")
                              && doType != QLatin1String("stop");

    ConditionBlockData r;
    if (!m_keepId.isEmpty())
        r.id = m_keepId;
    r.type = m_type->currentText();
    r.onType = m_onType->currentText();
    r.onValue = m_onValue->value();
    r.betType = m_betType->currentText();
    r.profitType = m_profitType->currentText();
    r.doType = doType;
    r.doValue = doNeedsValue ? m_doValue->value() : 0.0;
    m_result = r;
    QDialog::accept();
}
