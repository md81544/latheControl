#pragma once

#include "boltholes.h"

#include <QFont>
#include <QPointF>
#include <QRectF>

#include <optional>
#include <string>

class QPainter;

namespace mgo {

// What a bolt hole diagram shows (see paintBoltHoleDiagram())
struct BoltHoleDiagram {
    // The holes, numbered from 1, at their angles (from +Y, clockwise). The
    // number shown is what's drawn: hole 1 is always marked
    int holes { 0 };
    double firstAngleDeg { 0.0 };
    bool clockwise { true };
    // e.g. "PCD 50.00 mm", under the circle (empty for none)
    std::string pcdText;
    // The hole gone to (from 0), drawn bright, if any
    std::optional<int> current;
    // Where X and Y are, from the centre, as a fraction of the radius (so
    // (0, 1) is on the circle at +Y), if known: drawn as a cross (kept
    // within the diagram, if it's off it)
    std::optional<QPointF> here;
};

// Draws the circle seen from above, +Y up and +X to the right: its centre,
// the holes (numbered, unless there are too many to read), the first hole's
// angle from +Y, and an arrow from hole 1 the way they're numbered
void paintBoltHoleDiagram(
    QPainter& p,
    const QRectF& area,
    const BoltHoleDiagram& diagram,
    const QFont& font);

} // namespace mgo
