#include "radius.h"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>

namespace mgo {

namespace {

// A point in the work's own terms: a is how far along Z from the end face
// towards the chuck, and r how far from the centre line
struct WorkPoint {
    double a { 0.0 };
    double r { 0.0 };
};

} // end anonymous namespace

RadiusPlan planRadius(const RadiusSetup& setup, const RadiusWork& work)
{
    RadiusPlan plan;
    const double workRadius = work.diameter / 2.0;
    double radius = setup.radius;
    const double nose = setup.noseRadius;
    if (workRadius < MultiPass::TOLERANCE) {
        plan.error = "The work's diameter isn't set";
        return plan;
    }
    if (radius < MultiPass::TOLERANCE) {
        plan.error = "Type the radius";
        return plan;
    }
    if (radius > workRadius + MultiPass::TOLERANCE) {
        plan.error = fmt::format(
            "The radius can't be more than half the diameter ({:.3f} is a full ball end)",
            workRadius);
        return plan;
    }
    // (A full ball end, if within the tolerance)
    radius = std::min(radius, workRadius);
    if (setup.depthPerPass < MultiPass::TOLERANCE) {
        plan.error = "Type the depth per pass";
        return plan;
    }
    if (nose < 0.0) {
        plan.error = "The nose radius can't be negative";
        return plan;
    }
    // Each pass is an arc about the finished radius's centre. The corner of
    // the work is radius x root 2 from it, so the first pass is the largest
    // arc, a whole number of depths per pass out from the finished radius,
    // that's inside that: it takes off no more than the depth per pass
    const double toCorner = (std::sqrt(2.0) - 1.0) * radius;
    const int passCount
        = std::max(1, static_cast<int>(std::ceil(toCorner / setup.depthPerPass - 1e-9)));
    if (passCount > RADIUS_MAX_PASSES) {
        plan.error = fmt::format("That's {} passes: make the depth per pass larger", passCount);
        return plan;
    }

    // The tool's tip, as touched off, is off the centre of its nose by the
    // nose radius in each axis: towards the chuck (it was touched on the end
    // face) and towards the centre line (on the diameter). The nose's
    // centre follows an arc nose radius larger than the cut, so the tip
    // follows that arc moved by the same amount: an arc of the same size,
    // about this centre
    const WorkPoint centre { radius + nose, workRadius - radius - nose };
    const auto toMachine = [&work](double a, double r) {
        return MultiPassPathPoint { .cut = work.workEndZ + work.zTowardsChuck * a,
                                    .feed = work.xSign * r };
    };
    const double clearance = std::max(0.0, work.clearance);
    const double cutStart = work.workEndZ - work.zTowardsChuck * clearance;

    for (int n = passCount - 1; n >= 0; --n) {
        // The arc the tip follows on this pass. It's clipped to the work: from
        // where it crosses the end face (a = 0) to where it crosses the
        // diameter (r = workRadius). Measuring the angle from the end face's
        // direction (towards the centre line), both are at the same distance
        // from the centre, so cos(start) = sin(end) = k
        const double arc = radius + n * setup.depthPerPass + nose;
        const double k = std::min(1.0, (radius + nose) / arc);
        const double startAngle = std::acos(k);
        const double endAngle = std::asin(k);
        const auto onArc = [&centre](double radiusOfArc, double angle) {
            return WorkPoint { centre.a - radiusOfArc * std::cos(angle),
                               centre.r + radiusOfArc * std::sin(angle) };
        };

        // How many tapers: each covers an angle whose chord is no more than
        // twice the tolerance inside the arc. The points are moved out by
        // half that, so the tapers are within the tolerance either side of
        // it. (So the ends are just off the end face and the diameter, which
        // is clear of the work)
        const double cosHalf = 1.0 - 2.0 * RADIUS_CHORD_TOLERANCE / arc;
        double segmentAngle = RADIUS_MAX_SEGMENT_DEG * M_PI / 180.0;
        if (cosHalf > 0.0) {
            segmentAngle = std::min(segmentAngle, 2.0 * std::acos(cosHalf));
        }
        const double span = endAngle - startAngle;
        const int segments = std::max(1, static_cast<int>(std::ceil(span / segmentAngle)));
        const double step = span / segments;
        const double outBy = arc * (1.0 - std::cos(step / 2.0)) / 2.0;

        const double pointsArc = arc + outBy;
        MultiPassPath path;
        const WorkPoint start = onArc(pointsArc, startAngle);
        path.feedStart = work.xSign * start.r;
        if (clearance > MultiPass::TOLERANCE) {
            // Z on to the end face first, X staying where it is
            path.points.push_back(toMachine(start.a, start.r));
        }
        MultiPassPathPoint previous = toMachine(start.a, start.r);
        for (int s = 1; s <= segments; ++s) {
            const WorkPoint p = onArc(pointsArc, startAngle + s * step);
            MultiPassPathPoint point = toMachine(p.a, p.r);
            const double zDistance = std::abs(point.cut - previous.cut);
            const double xDistance = std::abs(point.feed - previous.feed);
            if (work.xMaxSpeed > 0.0 && xDistance > 0.0) {
                // (So that X needs no more than its share of its maximum)
                point.maxCutSpeed
                    = RADIUS_X_SPEED_FRACTION * work.xMaxSpeed * zDistance / xDistance;
            }
            path.points.push_back(point);
            previous = point;
        }
        plan.passes.push_back(std::move(path));
    }

    plan.range.cutStart = cutStart;
    plan.range.cutEnd = plan.passes.back().points.back().cut;
    plan.range.feedStart = work.xSign * workRadius;
    plan.range.feedEnd = plan.passes.back().feedStart;
    return plan;
}

} // namespace mgo
