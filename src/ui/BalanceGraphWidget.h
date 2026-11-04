#pragma once

#include <QVector>
#include <QWidget>

class QTimer;

// Profit curve of the whole session: every bet from the first to the last is kept and drawn
// (no sliding window). Above zero the curve and its shaded fill are green, below zero red.
//
// Fast at any size: adding a bet is O(1) and only schedules a repaint (at most ~30 per
// second, however fast the bets come), and a repaint reads per-block min / max summaries,
// so it costs about the chart's width in pixels, not the number of bets.
class BalanceGraphWidget : public QWidget
{
    Q_OBJECT

public:
    explicit BalanceGraphWidget(QWidget *parent = nullptr);

    void addSample(double profit);
    void clearSamples();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    struct Range
    {
        double lo;
        double hi;
    };

    QRect plotRect() const;
    int sampleIndexAt(const QPoint &pos) const;
    // Lowest / highest profit of samples [from, to) (to > from).
    Range rangeMinMax(qsizetype from, qsizetype to) const;
    void scheduleRepaint();

    // Point 0 is the start (0 profit, before the first bet), point n the profit after bet n.
    QVector<double> m_samples;
    // Min / max of each full block of kBlock1 samples, and of each full block of kBlock2.
    static constexpr qsizetype kBlock1 = 256;
    static constexpr qsizetype kBlock2 = kBlock1 * 256;
    QVector<Range> m_blocks1;
    QVector<Range> m_blocks2;
    double m_lo = 0.0; // whole session
    double m_hi = 0.0;

    int m_hoverX = -1; // mouse x over the chart, -1 = none (the sample under it is
                       // looked up at paint time, so it follows the growing curve)
    QTimer *m_repaintTimer = nullptr;
};
