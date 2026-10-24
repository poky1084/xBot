#include "ui/TargetBar.h"

#include <QPainter>
#include <QPaintEvent>

TargetBar::TargetBar(QWidget *parent)
    : QWidget(parent)
{
    setFixedHeight(36);
}

void TargetBar::setGame(double target, BetSide side)
{
    m_target = target;
    m_side = side;
    update();
}

void TargetBar::setLastRoll(double roll)
{
    m_lastRoll = roll;
    update();
}

void TargetBar::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QColor win(0x2e, 0x9e, 0x46);
    const QColor loss(0xc9, 0x37, 0x2a);
    const QColor winFill = win, lossFill = loss;
    // Top 12px are reserved for the last-roll marker label; bar sits below.
    const QRect r(0, 12, width(), height() - 12);

    // Zones: "under" wins below the target (green left), "over" wins above.
    const int splitX = int(m_target / 100.0 * width());
    QColor left = (m_side == BetSide::Under) ? winFill : lossFill;
    QColor right = (m_side == BetSide::Under) ? lossFill : winFill;
    left.setAlpha(140);
    right.setAlpha(140);
    p.fillRect(QRect(r.left(), r.top(), splitX, r.height()), left);
    p.fillRect(QRect(splitX, r.top(), r.right() - splitX + 1, r.height()), right);
    p.setPen(QColor(0xbd, 0xbd, 0xbd));
    p.drawRect(r);
    p.drawLine(QPoint(splitX, r.top()), QPoint(splitX, r.bottom()));

    // Last roll marker
    if (m_lastRoll >= 0.0) {
        const int x = int(m_lastRoll / 100.0 * width());
        p.setPen(QPen(QColor(0x20, 0x20, 0x20), 2));
        p.drawLine(QPoint(x, r.top() - 2), QPoint(x, r.bottom() + 2));

        const QString label = QString::number(m_lastRoll, 'f', 2);
        const QFontMetrics fm(p.font());
        const int tw = fm.horizontalAdvance(label) + 6;
        QRect tag(x - tw / 2, 0, tw, 12);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x20, 0x20, 0x20));
        p.drawRoundedRect(tag, 2, 2);
        p.setPen(Qt::white);
        p.drawText(tag, Qt::AlignCenter, label);
    }
}
