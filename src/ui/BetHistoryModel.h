#pragma once

#include "bot/BotTypes.h"

#include <QAbstractTableModel>
#include <QFont>
#include <QColor>
#include <QHash>

class BetHistoryModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    // Font of the Roll / Multiplier / Payout / Profit columns (bold; the others use the view's).
    void setBoldFont(const QFont &font) { m_boldFont = font; }
    enum Column {
        ColGame = 0,
        ColIndex,
        ColNonce,
        ColTime,
        ColAmount,
        ColTarget,
        ColRoll,
        ColMultiplier,
        ColPayout,
        ColProfit,
        ColBetId,       // "View" link -> click resolves to the public iid
        ColCasino,      // last column: casino the bet was played on
        ColumnCount
    };

    enum Filter { All, Wins, Losses, BigWins };

    // "Big wins": the highest-multiplier wins of the session, highest on top. Kept apart
    // from the 1000-row list so a big win stays here after it falls off the bet list.
    static constexpr int kMaxBigWins = 20;
    int bigWinCount() const { return m_top.size(); }

    // Rows kept in the bet list; older bets drop off the bottom.
    static constexpr int kMaxRows = 1000;

    explicit BetHistoryModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    void addBet(const BetRecord &record);
    void clear();
    void setFilter(Filter filter);
    Filter filter() const { return m_filter; }
    void setSearchText(const QString &text);

    // Zebra striping is tied to the bet itself (its running #), not to the row
    // position, so the stripes stay with their bets instead of flipping every
    // time a new bet is inserted at the top.
    void setStripeColor(const QColor &color);

    // Text colour of losing rows: grey, a lighter shade in the dark theme, darker in the light one.
    void setDarkTheme(bool dark);

    // ---- Bet id column ----------------------------------------------------
    // Rows with a server bet id show a blue "View" link. Once the id has been
    // resolved (BetLookup) the cell shows the iid instead, like the C# bot.
    QString betIdAt(int visualRow) const;
    QString resolvedIid(const QString &betId) const { return m_resolved.value(betId); }
    bool isResolved(const QString &betId) const { return m_resolved.contains(betId); }
    void setResolvedIid(const QString &betId, const QString &iid);

    int totalBets() const { return m_rows.size(); }
    const BetRecord &recordAt(int visualRow) const { return source().at(m_visible.at(visualRow)); }

    static QString formatAmount(double value, int decimals = 8);

signals:
    // Emitted just before the oldest visible row falls off the bottom (kMaxRows),
    // so the view can drop its selection instead of moving it to a neighbour row.
    void rowAboutToBeDropped(int visualRow);

private:
    bool passes(const BetRecord &b) const;
    bool betIdInTop(const QString &betId) const; // keeps its resolved iid when it leaves the list
    bool addBigWin(const BetRecord &record); // true when the record entered the top list
    // Rows m_visible indexes into: the big-win list in that filter, else every bet.
    const QVector<BetRecord> &source() const { return m_filter == BigWins ? m_top : m_rows; }
    void rebuildVisible();

    QVector<BetRecord> m_rows;   // everything, newest appended last
    QVector<BetRecord> m_top;    // big wins, highest multiplier first (kMaxBigWins)
    QHash<QString, QString> m_resolved; // numeric betId -> iid ("casino:...")
    QVector<int> m_visible;      // indices into m_rows after filter+search
    Filter m_filter = All;
    QString m_search;
    QColor m_stripe;             // background of every other bet (invalid = none)
    QFont m_boldFont;
    bool m_dark = false;         // dark theme: lighter grey for losing rows
};
