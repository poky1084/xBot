#pragma once

#include <QWidget>

// Small profit-curve graph, styled after the qBittorrent speed graph.
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
    int sampleIndexAt(const QPoint &pos) const;

    int m_hoverIndex = -1; // sample under the mouse, -1 = none
    QVector<double> m_samples;
    static constexpr int kMaxSamples = 100;
};
