#pragma once

#include "bot/BotTypes.h"

#include <QWidget>

// The dice range bar: red/green zones for the current side and target,
// plus a marker showing where the last roll landed.
class TargetBar : public QWidget
{
    Q_OBJECT

public:
    explicit TargetBar(QWidget *parent = nullptr);

    void setGame(double target, BetSide side);
    void setLastRoll(double roll);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    double m_target = 50.5;
    BetSide m_side = BetSide::Under;
    double m_lastRoll = -1.0;
};
