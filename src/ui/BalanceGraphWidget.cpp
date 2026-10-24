#include "ui/BalanceGraphWidget.h"

#include <QPainter>
#include <QPaintEvent>

#include <algorithm>

BalanceGraphWidget::BalanceGraphWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(120);
}

void BalanceGraphWidget::addSample(double profit)
{
    m_samples.append(profit);
    if (m_samples.size() > kMaxSamples)
        m_samples.remove(0, m_samples.size() - kMaxSamples);
    update();
}

void BalanceGraphWidget::clearSamples()
{
    m_samples.clear();
    update();
}

void BalanceGraphWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRect r = rect().adjusted(6, 6, -6, -6);
    const bool dark = palette().window().color().lightness() < 128;

    p.fillRect(rect(), dark ? QColor(0x23, 0x26, 0x29) : QColor(0xf7, 0xf8, 0xfa));

    if (m_samples.size() < 2) {
        p.setPen(dark ? QColor(0x6a, 0x70, 0x76) : QColor(0x9a, 0xa0, 0xa6));
        p.drawText(rect(), Qt::AlignCenter, tr("Waiting for bets…"));
        return;
    }

    double lo = *std::min_element(m_samples.begin(), m_samples.end());
    double hi = *std::max_element(m_samples.begin(), m_samples.end());
    lo = std::min(lo, 0.0);
    hi = std::max(hi, 0.0);
    if (hi - lo < 1e-9) {
        hi += 1.0;
        lo -= 1.0;
    }
    const double pad = (hi - lo) * 0.08;

    // Zero line
    const int zeroY = r.bottom() - int((0.0 - lo + pad) / (hi - lo + 2 * pad) * r.height());
    p.setPen(QPen(dark ? QColor(0x4a, 0x50, 0x56) : QColor(0xd0, 0xd4, 0xd8), 1, Qt::DashLine));
    p.drawLine(r.left(), zeroY, r.right(), zeroY);

    QPolygonF line;
    line.reserve(m_samples.size());
    for (int i = 0; i < m_samples.size(); ++i) {
        const double x = r.left() + double(i) / double(m_samples.size() - 1) * r.width();
        const double y = r.bottom() - ((m_samples.at(i) - lo + pad) / (hi - lo + 2 * pad)) * r.height();
        line << QPointF(x, y);
    }

    const bool profitUp = m_samples.last() >= 0.0;

    const QColor green(0x2e, 0x9e, 0x46);
    const QColor red(0xc9, 0x37, 0x37);
    const auto colorFor = [&](double v) { return v >= 0.0 ? green : red; };

    // Fill under each segment, tinted by whether that piece of the curve
    // sits above (green) or below (red) the zero line.
    p.setPen(Qt::NoPen);
    for (int i = 1; i < line.size(); ++i) {
        const QPointF &a = line.at(i - 1);
        const QPointF &b = line.at(i);
        QColor fillColor = colorFor(std::min(m_samples.at(i - 1), m_samples.at(i)));
        fillColor.setAlpha(36);
        p.setBrush(fillColor);
        QPolygonF segment;
        segment << a << b << QPointF(b.x(), zeroY) << QPointF(a.x(), zeroY);
        p.drawPolygon(segment);
    }

    // The line itself: green while above zero, red while below. Segments
    // that cross zero are split at the exact intersection point.
    QPen linePen(green, 1.8);
    linePen.setCapStyle(Qt::RoundCap);
    p.setBrush(Qt::NoBrush);
    for (int i = 1; i < line.size(); ++i) {
        const QPointF a = line.at(i - 1);
        const QPointF b = line.at(i);
        const double v0 = m_samples.at(i - 1);
        const double v1 = m_samples.at(i);
        if ((v0 >= 0.0) == (v1 >= 0.0)) {
            linePen.setColor(colorFor((v0 + v1) / 2.0));
            p.setPen(linePen);
            p.drawLine(a, b);
        } else {
            const double t = v0 / (v0 - v1); // zero crossing fraction
            const QPointF cross(a.x() + t * (b.x() - a.x()), zeroY);
            linePen.setColor(colorFor(v0));
            p.setPen(linePen);
            p.drawLine(a, cross);
            linePen.setColor(colorFor(v1));
            p.setPen(linePen);
            p.drawLine(cross, b);
        }
    }

    // Value labels
    p.setPen(dark ? QColor(0x9a, 0xa0, 0xa6) : QColor(0x6a, 0x70, 0x76));
    p.drawText(r.adjusted(4, 0, -4, -2), Qt::AlignBottom | Qt::AlignLeft,
               QString::number(lo, 'f', 2));
    p.drawText(r.adjusted(4, 2, -4, 0), Qt::AlignTop | Qt::AlignRight,
               QStringLiteral("%1 (%2)")
                   .arg(m_samples.last(), 0, 'f', 2)
                   .arg(profitUp ? QStringLiteral("+") : QString()));
}
