#include "ui/BalanceGraphWidget.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QTimer>

#include <algorithm>

namespace
{
constexpr int kRepaintMs = 33; // at most ~30 repaints a second, however fast the bets come
}

BalanceGraphWidget::BalanceGraphWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(120);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent); // the whole widget is painted every time

    m_repaintTimer = new QTimer(this);
    m_repaintTimer->setSingleShot(true);
    m_repaintTimer->setInterval(kRepaintMs);
    connect(m_repaintTimer, &QTimer::timeout, this, [this] { update(); });
}

QRect BalanceGraphWidget::plotRect() const
{
    return rect().adjusted(6, 10, -6, -10);
}

void BalanceGraphWidget::scheduleRepaint()
{
    if (!m_repaintTimer->isActive())
        m_repaintTimer->start();
}

void BalanceGraphWidget::addSample(double profit)
{
    if (m_samples.isEmpty()) {
        // The curve starts at 0 (before the first bet).
        m_samples.reserve(kMaxSamples + 1);
        m_samples.append(0.0);
        m_firstBet = 0;
    }
    m_samples.append(profit);
    if (m_samples.size() > kMaxSamples) {
        const qsizetype drop = m_samples.size() - kMaxSamples;
        m_samples.remove(0, drop);
        m_firstBet += drop;
    }
    scheduleRepaint();
}

void BalanceGraphWidget::clearSamples()
{
    m_samples.clear();
    m_firstBet = 0;
    m_repaintTimer->stop();
    update();
}

int BalanceGraphWidget::sampleIndexAt(const QPoint &pos) const
{
    if (m_samples.size() < 2)
        return -1;
    const QRect r = plotRect();
    if (r.width() <= 0)
        return -1;
    const double t = std::clamp(double(pos.x() - r.left()) / double(r.width()), 0.0, 1.0);
    return int(t * double(m_samples.size() - 1) + 0.5);
}

void BalanceGraphWidget::mouseMoveEvent(QMouseEvent *event)
{
    const int x = event->pos().x();
    if (x != m_hoverX) {
        m_hoverX = x;
        update();
    }
}

void BalanceGraphWidget::leaveEvent(QEvent *event)
{
    m_hoverX = -1;
    update();
    QWidget::leaveEvent(event);
}

void BalanceGraphWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);

    const bool dark = palette().window().color().lightness() < 128;
    p.fillRect(rect(), dark ? QColor(0x1d, 0x1f, 0x22) : QColor(0xf7, 0xf8, 0xfa));

    const QRect r = plotRect();
    const qsizetype n = m_samples.size();
    if (n < 2 || r.width() < 2 || r.height() < 2) {
        p.setPen(dark ? QColor(0x6a, 0x70, 0x76) : QColor(0x9a, 0xa0, 0xa6));
        p.drawText(rect(), Qt::AlignCenter, tr("Waiting for bets…"));
        return;
    }

    // Vertical scale: the bets shown, always including the zero line.
    const auto [mn, mx] = std::minmax_element(m_samples.cbegin(), m_samples.cend());
    double lo = std::min(*mn, 0.0);
    double hi = std::max(*mx, 0.0);
    if (hi - lo < 1e-12) {
        hi += 1.0;
        lo -= 1.0;
    }
    const double pad = (hi - lo) * 0.06;
    lo -= pad;
    hi += pad;
    const double yScale = double(r.height()) / (hi - lo);
    const auto yOf = [&](double v) { return double(r.bottom()) - (v - lo) * yScale; };
    const double zeroY = yOf(0.0);

    // Zero line.
    p.setPen(QPen(dark ? QColor(0x6a, 0x70, 0x76) : QColor(0xb0, 0xb6, 0xbc), 1, Qt::DashLine));
    p.drawLine(QPointF(r.left(), zeroY), QPointF(r.right(), zeroY));

    // The curve: one point per bet in the window, spread over the full width.
    const int w = r.width();
    QPolygonF line;
    line.reserve(int(n));
    const double step = double(w) / double(n - 1);
    for (qsizetype i = 0; i < n; ++i)
        line << QPointF(r.left() + double(i) * step, yOf(m_samples.at(i)));

    // Green above the zero line, red below: the same line drawn twice, clipped to each side.
    const QColor green(0x4c, 0xd9, 0x64);
    const QColor red(0xef, 0x44, 0x44);
    p.setRenderHint(QPainter::Antialiasing);

    // Shaded fill between the curve and the zero line, tinted the same way (green above,
    // red below). One polygon (the curve closed along the zero line), filled twice clipped.
    {
        QPolygonF area = line;
        area << QPointF(line.last().x(), zeroY) << QPointF(line.first().x(), zeroY);
        QColor fillGreen = green;
        QColor fillRed = red;
        fillGreen.setAlpha(36);
        fillRed.setAlpha(36);
        p.setPen(Qt::NoPen);
        p.setClipRect(QRectF(0, 0, width(), zeroY));
        p.setBrush(fillGreen);
        p.drawPolygon(area, Qt::WindingFill);
        p.setClipRect(QRectF(0, zeroY, width(), height() - zeroY));
        p.setBrush(fillRed);
        p.drawPolygon(area, Qt::WindingFill);
        p.setBrush(Qt::NoBrush);
    }

    QPen pen(green, 1.4);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setClipRect(QRectF(0, 0, width(), zeroY));
    p.setPen(pen);
    p.drawPolyline(line);
    pen.setColor(red);
    p.setClipRect(QRectF(0, zeroY, width(), height() - zeroY));
    p.setPen(pen);
    p.drawPolyline(line);
    p.setClipping(false);

    // Hover: dashed guide, a dot on the curve and "#bet  profit" next to it.
    if (m_hoverX >= 0) {
        const int idx = sampleIndexAt(QPoint(m_hoverX, 0));
        if (idx >= 0) {
            const double v = m_samples.at(idx);
            const double x = r.left() + double(idx) / double(n - 1) * w;
            const QPointF hp(x, yOf(v));
            const QColor textColor = dark ? QColor(0xe6, 0xe8, 0xea) : QColor(0x20, 0x24, 0x28);
            p.setPen(QPen(dark ? QColor(0x9a, 0xa0, 0xa6) : QColor(0x8a, 0x90, 0x96), 1, Qt::DashLine));
            p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
            p.setPen(QPen(dark ? QColor(0x1d, 0x1f, 0x22) : Qt::white, 1.5));
            p.setBrush(v >= 0.0 ? green : red);
            p.drawEllipse(hp, 3.5, 3.5);
            p.setBrush(Qt::NoBrush);

            const QString label = QStringLiteral("#%1  %2%3")
                                      .arg(m_firstBet + idx) // the bet's number in the session
                                      .arg(v > 0 ? QStringLiteral("+") : QString())
                                      .arg(v, 0, 'f', 8);
            QFont f = font();
            f.setBold(true);
            p.setFont(f);
            const QFontMetrics fm(f);
            QRectF box(0, 0, fm.horizontalAdvance(label) + 10, fm.height() + 4);
            // Right of the guide (left of it near the right edge), just below the dot.
            double bx = x + 8;
            if (bx + box.width() > r.right())
                bx = x - 8 - box.width();
            double by = hp.y() + 8;
            if (by + box.height() > r.bottom())
                by = hp.y() - 8 - box.height();
            box.moveTo(std::max<double>(r.left(), bx), std::max<double>(r.top(), by));
            QColor bg = dark ? QColor(0x1d, 0x1f, 0x22) : QColor(0xf7, 0xf8, 0xfa);
            bg.setAlpha(210);
            p.fillRect(box, bg);
            p.setPen(textColor);
            p.drawText(box, Qt::AlignCenter, label);
        }
    }
}
