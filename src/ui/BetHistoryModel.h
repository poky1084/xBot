#pragma once

#include "bot/BotTypes.h"

#include <QAbstractTableModel>
#include <QHash>

class BetHistoryModel : public QAbstractTableModel
{
    Q_OBJECT

public:
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
        ColBetId,       // last column: "View" link -> click resolves to the public iid
        ColumnCount
    };

    enum Filter { All, Wins, Losses, BigWins };

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

    // ---- Bet id column ----------------------------------------------------
    // Rows with a server bet id show a blue "View" link. Once the id has been
    // resolved (BetLookup) the cell shows the iid instead, like the C# bot.
    QString betIdAt(int visualRow) const;
    QString resolvedIid(const QString &betId) const { return m_resolved.value(betId); }
    bool isResolved(const QString &betId) const { return m_resolved.contains(betId); }
    void setResolvedIid(const QString &betId, const QString &iid);

    int totalBets() const { return m_rows.size(); }
    const BetRecord &recordAt(int visualRow) const { return m_rows.at(m_visible.at(visualRow)); }

    static QString formatAmount(double value, int decimals = 8);

private:
    bool passes(const BetRecord &b) const;
    void rebuildVisible();

    QVector<BetRecord> m_rows;   // everything, newest appended last
    QHash<QString, QString> m_resolved; // numeric betId -> iid ("casino:...")
    QVector<int> m_visible;      // indices into m_rows after filter+search
    Filter m_filter = All;
    QString m_search;
};
