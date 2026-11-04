#include "ui/BalanceGraphWidget.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QTimer>

#include <algorithm>
#include <limits>

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
        // The curve starts at 0 (before the first bet), so point n is the profit after bet n.
        m_samples.reserve(4096);
        m_samples.append(0.0);
        m_lo = m_hi = 0.0;
    }
    m_samples.append(profit);
    m_lo = std::min(m_lo, profit);
    m_hi = std::max(m_hi, profit);

    // Block summaries, filled in as each block completes (amortised O(1) per bet).
    const qsizetype n = m_samples.size();
    if (n % kBlock1 == 0) {
        const auto first = m_samples.cbegin() + (n - kBlock1);
        const auto [mn, mx] = std::minmax_element(first, m_samples.cend());
        m_blocks1.append({*mn, *mx});
        if (n % kBlock2 == 0) {
            Range r{std::numeric_limits<double>::max(), std::numeric_limits<double>::lowest()};
            for (qsizetype i = m_blocks1.size() - kBlock2 / kBlock1; i < m_blocks1.size(); ++i) {
                r.lo = std::min(r.lo, m_blocks1.at(i).lo);
                r.hi = std::max(r.hi, m_blocks1.at(i).hi);
            }
            m_blocks2.append(r);
        }
    }
    scheduleRepaint();
}

void BalanceGraphWidget::clearSamples()
{
    m_samples.clear();
    m_samples.squeeze();
    m_blocks1.clear();
    m_blocks2.clear();
    m_lo = m_hi = 0.0;
    m_repaintTimer->stop();
    update();
}

BalanceGraphWidget::Range BalanceGraphWidget::rangeMinMax(qsizetype from, qsizetype to) const
{
    Range r{std::numeric_limits<double>::max(), std::numeric_limits<double>::lowest()};
    qsizetype i = from;
    while (i < to) {
        if (i % kBlock2 == 0 && i + kBlock2 <= to) {
            const Range &b = m_blocks2.at(i / kBlock2);
            r.lo = std::min(r.lo, b.lo);
            r.hi = std::max(r.hi, b.hi);
            i += kBlock2;
        } else if (i % kBlock1 == 0 && i + kBlock1 <= to) {
            const Range &b = m_blocks1.at(i / kBlock1);
            r.lo = std::min(r.lo, b.lo);
            r.hi = std::max(r.hi, b.hi);
            i += kBlock1;
        } else {
            const double v = m_samples.at(i);
            r.lo = std::min(r.lo, v);
            r.hi = std::max(r.hi, v);
            ++i;
        }
    }
    return r;
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
    p.fillRect(rect(), dark ? QColor(0x23, 0x26, 0x29) : QColor(0xf7, 0xf8, 0xfa));

    const QRect r = plotRect();
    const qsizetype n = m_samples.size();
    if (n < 2 || r.width() < 2 || r.height() < 2) {
        p.setPen(dark ? QColor(0x6a, 0x70, 0x76) : QColor(0x9a, 0xa0, 0xa6));
        p.drawText(rect(), Qt::AlignCenter, tr("Waiting for bets…"));
        return;
    }

    // Vertical scale: the whole session, always including the zero line.
    double lo = std::min(m_lo, 0.0);
    double hi = std::max(m_hi, 0.0);
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

    // The curve. With more bets than pixel columns, each column gets its bets' first, highest,
    // lowest and last profit, so every bet is in the picture (spikes included) at a cost of
    // about one pass over the columns.
    const int w = r.width();
    QPolygonF line;
    if (n - 1 <= qsizetype(w)) {
        line.reserve(int(n));
        const double step = double(w) / double(n - 1);
        for (qsizetype i = 0; i < n; ++i)
            line << QPointF(r.left() + double(i) * step, yOf(m_samples.at(i)));
    } else {
        line.reserve(w * 4);
        for (int c = 0; c < w; ++c) {
            const qsizetype from = qsizetype(c) * n / w;
            const qsizetype to = std::max(from + 1, qsizetype(c + 1) * n / w);
            const double x = r.left() + c + 0.5;
            const Range mm = rangeMinMax(from, to);
            const double first = m_samples.at(from);
            const double last = m_samples.at(to - 1);
            line << QPointF(x, yOf(first));
            // Highest and lowest in the order they are reached from the first value.
            if (mm.hi - first < first - mm.lo)
                line << QPointF(x, yOf(mm.hi)) << QPointF(x, yOf(mm.lo));
            else
                line << QPointF(x, yOf(mm.lo)) << QPointF(x, yOf(mm.hi));
            line << QPointF(x, yOf(last));
        }
    }

    // Green above the zero line, red below: the same line drawn twice, clipped to each side.
    const QColor green(0x4c, 0xd9, 0x64);
    const QColor red(0xef, 0x44, 0x44);
    p.setRenderHint(QPainter::Antialiasing, n - 1 <= qsizetype(w) * 2);

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
    p.setRenderHint(QPainter::Antialiasing);

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
            p.setPen(QPen(dark ? QColor(0x23, 0x26, 0x29) : Qt::white, 1.5));
            p.setBrush(v >= 0.0 ? green : red);
            p.drawEllipse(hp, 3.5, 3.5);
            p.setBrush(Qt::NoBrush);

            const QString label = QStringLiteral("#%1  %2%3")
                                      .arg(idx)
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
            QColor bg = dark ? QColor(0x23, 0x26, 0x29) : QColor(0xf7, 0xf8, 0xfa);
            bg.setAlpha(210);
            p.fillRect(box, bg);
            p.setPen(textColor);
            p.drawText(box, Qt::AlignCenter, label);
        }
    }
}
