#include "ui/LogWidget.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QFont>
#include <QHeaderView>
#include <QKeySequence>

#include <algorithm>

namespace
{
constexpr int kMaxRows = 500;
constexpr int kTrimBatch = 50;
} // namespace

LogWidget::LogWidget(QWidget *parent)
    : QTableWidget(0, 3, parent)
{
    setHorizontalHeaderLabels({tr("Time"), tr("Level"), tr("Message")});
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setShowGrid(false);
    setWordWrap(false);
    setAlternatingRowColors(false);
    setTextElideMode(Qt::ElideRight);

    verticalHeader()->setVisible(false);
    verticalHeader()->setDefaultSectionSize(20);
    verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);

    // Hide the header entirely
    horizontalHeader()->setVisible(false);

    QHeaderView *h = horizontalHeader();
    h->setSectionResizeMode(0, QHeaderView::Fixed);
    h->setSectionResizeMode(1, QHeaderView::Fixed);
    h->setSectionResizeMode(2, QHeaderView::Stretch);
    // h->setHighlightSections(false); // No longer needed when hidden
    setColumnWidth(0, 76);
    setColumnWidth(1, 64);

    // Default colours: text colours that read on both light and dark themes,
    // plus a faint tint so a row stands out at a glance.
    m_styles[int(Level::Info)] = {QColor(), QColor()}; // theme default
    m_styles[int(Level::Success)] = {QColor(0x3f, 0xae, 0x5a), QColor(0x3f, 0xae, 0x5a, 28)};
    m_styles[int(Level::Warning)] = {QColor(0xd9, 0xa2, 0x1b), QColor(0xd9, 0xa2, 0x1b, 34)};
    m_styles[int(Level::Error)] = {QColor(0xe0, 0x55, 0x55), QColor(0xe0, 0x55, 0x55, 40)};

    setContextMenuPolicy(Qt::ActionsContextMenu);
    auto *copyAct = new QAction(tr("Copy"), this);
    copyAct->setShortcut(QKeySequence::Copy);
    copyAct->setShortcutContext(Qt::WidgetShortcut);
    connect(copyAct, &QAction::triggered, this, &LogWidget::copySelected);
    addAction(copyAct);

    auto *clearAct = new QAction(tr("Clear log"), this);
    connect(clearAct, &QAction::triggered, this, &LogWidget::clearLog);
    addAction(clearAct);
}

void LogWidget::setLevelColors(Level level, const QColor &foreground, const QColor &rowTint)
{
    m_styles[int(level)] = {foreground, rowTint};
}

LogWidget::Level LogWidget::classify(const QString &message, bool isError)
{
    if (isError)
        return Level::Error;

    const QString m = message.toLower();
    static const char *const warn[] = {"stopped", "halted", "paused", "retrying", "hit stop",
                                       "back to simulation", "disconnected", "cleared", "reset"};
    for (const char *w : warn)
        if (m.contains(QLatin1String(w)))
            return Level::Warning;

    static const char *const ok[] = {"started", "resumed", "connected", "enabled", "logged in",
                                     "saved", "applied", "ready"};
    for (const char *w : ok)
        if (m.contains(QLatin1String(w)))
            return Level::Success;

    return Level::Info;
}

QString LogWidget::levelText(Level level)
{
    switch (level) {
    case Level::Success: return QStringLiteral("OK");
    case Level::Warning: return QStringLiteral("WARN");
    case Level::Error: return QStringLiteral("ERROR");
    case Level::Info: break;
    }
    return QStringLiteral("INFO");
}

void LogWidget::appendLog(const QString &message, bool isError)
{
    appendEntry(message, classify(message, isError));
}

void LogWidget::appendEntry(const QString &message, Level level)
{
    const Style &st = m_styles[int(level)];
    const QDateTime now = QDateTime::currentDateTime();

    auto makeItem = [&](const QString &text, const QColor &fg) {
        auto *it = new QTableWidgetItem(text);
        it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        if (fg.isValid())
            it->setForeground(fg);
        if (st.bg.isValid())
            it->setBackground(st.bg);
        it->setToolTip(QStringLiteral("%1\n%2")
                           .arg(now.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")), message));
        return it;
    };

    const int row = rowCount();
    insertRow(row);

    auto *time = makeItem(now.toString(QStringLiteral("HH:mm:ss")), QColor(0x7f, 0x86, 0x8d));

    auto *lvl = makeItem(levelText(level), st.fg);
    QFont bold = lvl->font();
    bold.setBold(true);
    lvl->setFont(bold);
    lvl->setTextAlignment(Qt::AlignCenter);

    auto *msg = makeItem(message, st.fg);

    setItem(row, 0, time);
    setItem(row, 1, lvl);
    setItem(row, 2, msg);

    trimOldRows();
    scrollToBottom();   // always scroll to the newest entry
}

void LogWidget::trimOldRows()
{
    if (rowCount() <= kMaxRows + kTrimBatch)
        return;
    setUpdatesEnabled(false);
    for (int i = 0; i < kTrimBatch; ++i)
        removeRow(0);
    setUpdatesEnabled(true);
}

void LogWidget::clearLog()
{
    setRowCount(0);
}

void LogWidget::copySelected()
{
    QList<int> rows;
    const auto selected = selectionModel()->selectedRows();
    for (const QModelIndex &idx : selected)
        rows << idx.row();
    std::sort(rows.begin(), rows.end());

    QStringList lines;
    for (int r : std::as_const(rows)) {
        QStringList cols;
        for (int c = 0; c < columnCount(); ++c)
            cols << (item(r, c) ? item(r, c)->text() : QString());
        lines << cols.join(QLatin1Char('\t'));
    }
    if (!lines.isEmpty())
        QApplication::clipboard()->setText(lines.join(QLatin1Char('\n')));
}