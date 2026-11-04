#pragma once

#include <QVector>
#include <QWidget>

class QTimer;

// Profit curve of the last kMaxSamples bets (a sliding window). Above zero the curve and its
// shaded fill are green, below zero red. Adding a bet only schedules a repaint (at most ~30
// per second, however fast the bets come).
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
    QRect plotRect() const;
    int sampleIndexAt(const QPoint &pos) const;
    void scheduleRepaint();

    static constexpr int kMaxSamples = 100;
    QVector<double> m_samples; // the last kMaxSamples profits (plus the 0 start at first)
    qint64 m_firstBet = 0;     // bet number of m_samples[0] (0 = the start, before bet 1)

    int m_hoverX = -1; // mouse x over the chart, -1 = none (the sample under it is
                       // looked up at paint time, so it follows the moving curve)
    QTimer *m_repaintTimer = nullptr;
};
