#include "ui/BetHistoryModel.h"

#include <QColor>
#include <QFont>

BetHistoryModel::BetHistoryModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int BetHistoryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_visible.size();
}

int BetHistoryModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QString BetHistoryModel::formatAmount(double value, int decimals)
{
    // Plain fixed-point like the reference app (no thousands separators).
    return QString::number(value, 'f', decimals);
}

QVariant BetHistoryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_visible.size())
        return {};

    const BetRecord &rec = source().at(m_visible.at(index.row()));
    const bool win = rec.win;

    if (role == Qt::BackgroundRole) {
        if (m_stripe.isValid() && (rec.index & 1))
            return m_stripe;
        return {};
    }

    if (role == Qt::ForegroundRole) {
        // Bet ID: blue "View" link until resolved, then the row's green / red like the rest.
        if (index.column() == ColBetId && !rec.betId.isEmpty() && !m_resolved.contains(rec.betId))
            return QColor(0x25, 0x63, 0xeb);
        // Roll keeps its own colours (green win / orange loss); every other column is
        // green on a win and grey on a loss (a darker grey in the light theme so it reads on white).
        if (index.column() == ColRoll)
            return win ? QColor(0x2e, 0x9e, 0x46) : QColor(0xd4, 0x7a, 0x2c);
        if (win)
            return QColor(0x2e, 0x9e, 0x46);
        return m_dark ? QColor(0x9a, 0xa0, 0xa6) : QColor(0x6a, 0x70, 0x76);
    }

    if (role == Qt::TextAlignmentRole)
        return int(Qt::AlignLeft | Qt::AlignVCenter);

    // Every column in bold.
    if (role == Qt::FontRole)
        return m_boldFont;

    if (role != Qt::DisplayRole && role != Qt::ToolTipRole)
        return {};

    switch (index.column()) {
    case ColBetId:
        if (rec.betId.isEmpty())
            return {};
        return m_resolved.contains(rec.betId) ? m_resolved.value(rec.betId)
                                              : QStringLiteral("View");
    case ColCasino:
        if (rec.casino.isEmpty())
            return {};
        return rec.casino.at(0).toUpper() + rec.casino.mid(1);
    case ColGame:
        return scriptGameName(rec.game); // the road-crossing games as "cross"
    case ColIndex:
        // Newest row on top carries the total bet count, counting down downwards.
        return QString::number(int(rec.index));
    case ColNonce:
        return QString::number(rec.nonce);
    case ColTime:
        return rec.time.toString(QStringLiteral("HH:mm:ss"));
    case ColAmount:
        return formatAmount(rec.bet);
    case ColTarget:
        if (!isDiceOrLimbo(rec.game))
            return rec.targetText; // "medium|8", "low|10", cards, ...
        if (rec.game == Game::Limbo)
            return QStringLiteral("≥ %1x").arg(rec.target, 0, 'f', 2);
        // The casino's own target when it differs from the app's (house edge / roll range).
        return QStringLiteral("%1 %2")
            .arg(rec.side == BetSide::Under ? QLatin1Char('<') : QLatin1Char('>'))
            .arg(rec.casinoTarget >= 0.0 ? rec.casinoTarget : rec.target, 0, 'f', 2);
    case ColRoll:
        if (!isDiceOrLimbo(rec.game) && !rec.rollText.isEmpty())
            return rec.rollText; // "3 hits", "fourOfAKind", ...
        return QString::number(rec.roll, 'f', 2);
    case ColMultiplier:
        return QStringLiteral("%1x").arg(rec.mult, 0, 'f', 4);
    case ColPayout:
        return formatAmount(rec.payout);
    case ColProfit:
        return (rec.profit > 0 ? QStringLiteral("+") : QString()) + formatAmount(rec.profit);
    default:
        return {};
    }
}

QVariant BetHistoryModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case ColBetId:
        return tr("Bet ID");
    case ColCasino:
        return tr("Casino");
    case ColGame:
        return tr("Game");
    case ColIndex:
        return QStringLiteral("#");
    case ColNonce:
        return tr("Nonce");
    case ColTime:
        return tr("Time");
    case ColAmount:
        return tr("Amount");
    case ColTarget:
        return tr("Target");
    case ColRoll:
        return tr("Roll");
    case ColMultiplier:
        return tr("Multiplier");
    case ColPayout:
        return tr("Payout");
    case ColProfit:
        return tr("Profit");
    default:
        return {};
    }
}

bool BetHistoryModel::betIdInTop(const QString &betId) const
{
    if (betId.isEmpty())
        return false;
    for (const BetRecord &r : m_top)
        if (r.betId == betId)
            return true;
    return false;
}

bool BetHistoryModel::addBigWin(const BetRecord &record)
{
    if (!record.win)
        return false;
    // Above the ones already there on an equal multiplier: the latest hit goes on top, and
    // when the list is full the oldest win of the lowest multiplier is the one that drops out.
    int pos = 0;
    while (pos < m_top.size() && m_top.at(pos).mult > record.mult)
        ++pos;
    if (pos >= kMaxBigWins)
        return false;
    m_top.insert(pos, record);
    while (m_top.size() > kMaxBigWins)
        m_top.removeLast();
    return true;
}

void BetHistoryModel::addBet(const BetRecord &record)
{
    const bool topChanged = addBigWin(record);
    if (m_filter == BigWins) {
        // Only 20 rows, re-sorted by multiplier: rebuild them when the list changed.
        m_rows.prepend(record);
        if (m_rows.size() > kMaxRows) {
            if (!betIdInTop(m_rows.last().betId))
                m_resolved.remove(m_rows.last().betId);
            m_rows.removeLast();
        }
        if (topChanged) {
            beginResetModel();
            rebuildVisible();
            endResetModel();
        }
        return;
    }

    // Incremental insert (no model reset): a reset snaps the view back to the
    // top and drops the selection on every bet, which made the list jump while
    // the user was scrolling or had a row selected.
    const bool visible = passes(record);
    if (visible)
        beginInsertRows({}, 0, 0);
    m_rows.prepend(record); // newest first, like the reference app
    for (int &i : m_visible)
        ++i;                // every existing row moved down by one
    if (visible)
        m_visible.prepend(0);
    if (visible)
        endInsertRows();

    // Keep the list bounded; the oldest rows fall off the bottom.
    while (m_rows.size() > kMaxRows) {
        const int last = m_rows.size() - 1;
        const bool wasVisible = !m_visible.isEmpty() && m_visible.last() == last;
        if (wasVisible) {
            emit rowAboutToBeDropped(m_visible.size() - 1);
            beginRemoveRows({}, m_visible.size() - 1, m_visible.size() - 1);
        }
        if (!betIdInTop(m_rows.last().betId))
            m_resolved.remove(m_rows.last().betId);
        m_rows.removeLast();
        if (wasVisible) {
            m_visible.removeLast();
            endRemoveRows();
        }
    }
}

void BetHistoryModel::clear()
{
    beginResetModel();
    m_rows.clear();
    m_top.clear();
    m_resolved.clear();
    rebuildVisible();
    endResetModel();
}

QString BetHistoryModel::betIdAt(int visualRow) const
{
    if (visualRow < 0 || visualRow >= m_visible.size())
        return {};
    return source().at(m_visible.at(visualRow)).betId;
}

void BetHistoryModel::setResolvedIid(const QString &betId, const QString &iid)
{
    if (betId.isEmpty() || iid.isEmpty())
        return;
    m_resolved.insert(betId, iid);
    if (!m_visible.isEmpty())
        emit dataChanged(index(0, ColBetId), index(m_visible.size() - 1, ColBetId));
}

void BetHistoryModel::setStripeColor(const QColor &color)
{
    if (m_stripe == color)
        return;
    m_stripe = color;
    if (!m_visible.isEmpty())
        emit dataChanged(index(0, 0), index(m_visible.size() - 1, ColumnCount - 1),
                         {Qt::BackgroundRole});
}

void BetHistoryModel::setDarkTheme(bool dark)
{
    if (m_dark == dark)
        return;
    m_dark = dark;
    if (!m_visible.isEmpty())
        emit dataChanged(index(0, 0), index(m_visible.size() - 1, ColumnCount - 1),
                         {Qt::ForegroundRole});
}

void BetHistoryModel::setFilter(Filter filter)
{
    if (m_filter == filter)
        return;
    beginResetModel();
    m_filter = filter;
    rebuildVisible();
    endResetModel();
}

void BetHistoryModel::setSearchText(const QString &text)
{
    if (m_search == text)
        return;
    beginResetModel();
    m_search = text;
    rebuildVisible();
    endResetModel();
}

bool BetHistoryModel::passes(const BetRecord &b) const
{
    const bool passesFilter =
        m_filter == All
        || (m_filter == Wins && b.win)
        || (m_filter == Losses && !b.win)
        || m_filter == BigWins; // m_top only holds big wins
    const bool passesSearch =
        m_search.isEmpty()
        || QString::number(b.nonce).contains(m_search, Qt::CaseInsensitive)
        || QString::number(b.roll, 'f', 2).contains(m_search, Qt::CaseInsensitive)
        || b.rollText.contains(m_search, Qt::CaseInsensitive)
        || scriptGameName(b.game).contains(m_search, Qt::CaseInsensitive);
    return passesFilter && passesSearch;
}

void BetHistoryModel::rebuildVisible()
{
    const QVector<BetRecord> &rows = source();
    m_visible.clear();
    m_visible.reserve(rows.size());
    for (int i = 0; i < rows.size(); ++i) {
        if (passes(rows.at(i)))
            m_visible.append(i);
    }
}
