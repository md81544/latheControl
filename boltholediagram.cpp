#include "boltholediagram.h"

#include <QColor>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace mgo {

namespace {

// The most holes which are numbered: more would be too crowded to read (only
// hole 1, and the current one, are numbered then)
constexpr int MAX_NUMBERED_HOLES = 24;

// A point at an angle (degrees from +Y, clockwise, as the holes' angles are)
// and distance from the centre, on the screen (where y goes down)
QPointF at(QPointF centre, double angleDeg, double distance)
{
    const double rad = angleDeg * std::numbers::pi / 180.0;
    return centre + QPointF(distance * std::sin(rad), -distance * std::cos(rad));
}

// An arrowhead at tip, pointing along dir (a unit vector), using the
// painter's brush
void arrowHead(QPainter& p, QPointF tip, QPointF dir)
{
    const QPointF across(-dir.y(), dir.x());
    QPolygonF head;
    head << tip << tip - dir * 9.0 + across * 4.0 << tip - dir * 9.0 - across * 4.0;
    p.drawPolygon(head);
}

} // namespace

void paintBoltHoleDiagram(
    QPainter& p,
    const QRectF& area,
    const BoltHoleDiagram& d,
    const QFont& baseFont)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(area, QColor(20, 20, 20));
    QFont font(baseFont);
    font.setPixelSize(14);
    font.setBold(false);
    p.setFont(font);
    const QColor grey(110, 110, 110);
    const QColor green(0, 192, 0);
    const QColor dark(0, 70, 0);
    const QColor bright(0, 255, 0);
    const QColor yellow(255, 255, 0);
    const QColor text(220, 220, 220);
    const QColor red(255, 60, 60);

    // (Room below for the PCD, and round the circle for the numbers)
    const double bottom = d.pcdText.empty() ? 0.0 : 22.0;
    const QRectF square(area.left(), area.top(), area.width(), area.height() - bottom);
    const QPointF centre = square.center();
    const double radius = std::max(10.0, std::min(square.width(), square.height()) / 2.0 - 30.0);

    // The axes through the centre
    p.setPen(QPen(grey, 1, Qt::DashDotLine));
    const double reach = radius + 16.0;
    p.drawLine(centre - QPointF(reach, 0.0), centre + QPointF(reach, 0.0));
    p.drawLine(centre - QPointF(0.0, reach), centre + QPointF(0.0, reach));
    // and which way is +X and +Y, in the top left corner (clear of the
    // holes' numbers, which can be anywhere round the circle)
    {
        const QPointF origin = area.topLeft() + QPointF(10.0, 34.0);
        p.setPen(QPen(grey, 1.5));
        p.setBrush(grey);
        p.drawLine(origin, origin + QPointF(22.0, 0.0));
        p.drawLine(origin, origin - QPointF(0.0, 22.0));
        arrowHead(p, origin + QPointF(24.0, 0.0), QPointF(1.0, 0.0));
        arrowHead(p, origin - QPointF(0.0, 24.0), QPointF(0.0, -1.0));
        p.drawText(
            QRectF(origin.x() + 26.0, origin.y() - 9.0, 30.0, 18.0),
            Qt::AlignLeft | Qt::AlignVCenter,
            "X");
        p.drawText(
            QRectF(origin.x() + 5.0, origin.y() - 32.0, 30.0, 18.0),
            Qt::AlignLeft | Qt::AlignVCenter,
            "Y");
    }

    // The pitch circle
    p.setPen(QPen(green, 1.5));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(centre, radius, radius);
    // (A small cross at the centre)
    p.setPen(QPen(text, 1));
    p.drawLine(centre - QPointF(5.0, 0.0), centre + QPointF(5.0, 0.0));
    p.drawLine(centre - QPointF(0.0, 5.0), centre + QPointF(0.0, 5.0));

    if (d.holes < 1) {
        p.setPen(text);
        p.drawText(square, Qt::AlignCenter, "How many holes?");
        p.restore();
        return;
    }

    const double pitch = 360.0 / d.holes;
    const double sign = d.clockwise ? 1.0 : -1.0;
    const double first = normaliseDegrees(d.firstAngleDeg);
    const double holeRadius
        = std::clamp(radius * std::sin(std::numbers::pi / std::max(d.holes, 2)) * 0.45, 2.5, 9.0);

    // The first hole's angle from +Y: a line out to it, and an arc from +Y
    // (the short way round, so -30° rather than 330°)
    if (first > 0.05 && first < 359.95) {
        const double shown = first <= 180.0 ? first : first - 360.0;
        p.setPen(QPen(yellow, 1, Qt::DashLine));
        p.drawLine(centre, at(centre, first, radius));
        p.setPen(QPen(yellow, 1.5));
        const double arcRadius = radius * 0.42;
        // (Qt's arcs go anticlockwise from 3 o'clock, in 1/16ths of a degree)
        p.drawArc(
            QRectF(centre - QPointF(arcRadius, arcRadius), centre + QPointF(arcRadius, arcRadius)),
            90 * 16,
            static_cast<int>(std::lround(-shown * 16.0)));
        const QPointF label = at(centre, shown / 2.0, arcRadius + 22.0);
        p.drawText(
            QRectF(label - QPointF(40.0, 9.0), QSizeF(80.0, 18.0)),
            Qt::AlignCenter,
            QString::number(shown, 'f', std::abs(shown - std::round(shown)) < 0.05 ? 0 : 1)
                + QChar(0x00B0));
    }

    // The way they're numbered: an arrow inside the circle, from hole 1
    // towards hole 2
    {
        const double arrowRadius = radius - holeRadius - 9.0;
        const double sweep = std::min(pitch * 0.7, 40.0);
        const double from = first + sign * (holeRadius / radius) * 180.0 / std::numbers::pi;
        QPainterPath path;
        path.moveTo(at(centre, from, arrowRadius));
        constexpr int STEPS = 12;
        for (int n = 1; n <= STEPS; ++n) {
            path.lineTo(at(centre, from + sign * sweep * n / STEPS, arrowRadius));
        }
        p.setPen(QPen(yellow, 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
        const QPointF tip = at(centre, from + sign * sweep, arrowRadius);
        const QPointF before = at(centre, from + sign * (sweep - 2.0), arrowRadius);
        QPointF dir = tip - before;
        dir /= std::hypot(dir.x(), dir.y());
        p.setBrush(yellow);
        arrowHead(p, tip, dir);
    }

    // The holes: hole 1 yellow, the current one bright, the others dark
    for (int n = 0; n < d.holes; ++n) {
        const double angle = first + sign * n * pitch;
        const QPointF c = at(centre, angle, radius);
        const bool current = d.current && *d.current == n;
        p.setPen(QPen(n == 0 ? yellow : green, current ? 2.5 : 1.5));
        p.setBrush(current ? bright : n == 0 ? QColor(90, 90, 0) : dark);
        const double r = current ? holeRadius + 2.0 : holeRadius;
        p.drawEllipse(c, r, r);
        if (d.holes <= MAX_NUMBERED_HOLES || n == 0 || current) {
            const QPointF label = at(centre, angle, radius + holeRadius + 11.0);
            p.setPen(current ? bright : n == 0 ? yellow : text);
            p.drawText(
                QRectF(label - QPointF(15.0, 9.0), QSizeF(30.0, 18.0)),
                Qt::AlignCenter,
                QString::number(n + 1));
        }
    }

    // Where X and Y are now
    if (d.here) {
        QPointF h = centre + QPointF(d.here->x() * radius, -d.here->y() * radius);
        h.setX(std::clamp(h.x(), area.left() + 6.0, area.right() - 6.0));
        h.setY(std::clamp(h.y(), area.top() + 6.0, square.bottom() - 6.0));
        p.setPen(QPen(red, 2));
        p.drawLine(h - QPointF(6.0, 6.0), h + QPointF(6.0, 6.0));
        p.drawLine(h - QPointF(6.0, -6.0), h + QPointF(6.0, -6.0));
    }

    if (!d.pcdText.empty()) {
        p.setPen(text);
        p.drawText(
            QRectF(area.left(), area.bottom() - bottom, area.width(), bottom),
            Qt::AlignCenter,
            QString::fromStdString(d.pcdText));
    }
    p.restore();
}

} // namespace mgo
