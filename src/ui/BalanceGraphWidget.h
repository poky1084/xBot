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

private:
    QVector<double> m_samples;
    static constexpr int kMaxSamples = 600;
};
