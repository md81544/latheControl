#include "radius.h"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <functional>

namespace mgo {

namespace {

// A point in the work's own terms: a is how far along Z from the end face
// towards the chuck, and r how far from the centre line
struct WorkPoint {
    double a { 0.0 };
    double r { 0.0 };
};

MultiPassPathPoint toMachine(const RadiusWork& work, WorkPoint p)
{
    return MultiPassPathPoint { .cut = work.workEndZ + work.zTowardsChuck * p.a,
                                .feed = work.xSign * p.r };
}

// The points of an arc of a radius, cut as a series of short tapers from
// startAngle to endAngle (in radians). pointAt(radius, angle) is the point at
// an angle on an arc of a radius about the arc's centre. Each taper covers an
// angle whose chord is no more than twice the tolerance inside the arc. The
// points are moved out by half that, so the tapers are within the tolerance
// either side of it (and the ends are just off the arc, by up to half the
// tolerance)
std::vector<WorkPoint> arcAsTapers(
    double radius,
    double startAngle,
    double endAngle,
    const std::function<WorkPoint(double, double)>& pointAt)
{
    const double cosHalf = 1.0 - 2.0 * RADIUS_CHORD_TOLERANCE / radius;
    double segmentAngle = RADIUS_MAX_SEGMENT_DEG * M_PI / 180.0;
    if (cosHalf > 0.0) {
        segmentAngle = std::min(segmentAngle, 2.0 * std::acos(cosHalf));
    }
    const double span = endAngle - startAngle;
    const int segments = std::max(1, static_cast<int>(std::ceil(span / segmentAngle)));
    const double step = span / segments;
    const double outBy = radius * (1.0 - std::cos(step / 2.0)) / 2.0;
    std::vector<WorkPoint> points;
    for (int s = 0; s <= segments; ++s) {
        points.push_back(pointAt(radius + outBy, startAngle + s * step));
    }
    return points;
}

// A taper from one point to the next, Z slowed if need be so that X needs no
// more than its share of its maximum speed
MultiPassPathPoint taperTo(const RadiusWork& work, const MultiPassPathPoint& from, WorkPoint to)
{
    MultiPassPathPoint point = toMachine(work, to);
    const double zDistance = std::abs(point.cut - from.cut);
    const double xDistance = std::abs(point.feed - from.feed);
    if (work.xMaxSpeed > 0.0 && xDistance > 0.0) {
        point.maxCutSpeed = RADIUS_X_SPEED_FRACTION * work.xMaxSpeed * zDistance / xDistance;
    }
    return point;
}

// What both kinds need: empty if all's well
std::string commonError(const RadiusSetup& setup, const RadiusWork& work)
{
    if (work.diameter / 2.0 < MultiPass::TOLERANCE) {
        return "The work's diameter isn't set";
    }
    if (setup.radius < MultiPass::TOLERANCE) {
        return "Type the radius";
    }
    return {};
}

std::string depthAndNoseError(const RadiusSetup& setup)
{
    if (setup.depthPerPass < MultiPass::TOLERANCE) {
        return "Type the depth per pass";
    }
    if (setup.noseRadius < 0.0) {
        return "The nose radius can't be negative";
    }
    return {};
}

std::string tooManyPasses(int passCount)
{
    return fmt::format("That's {} passes: make the depth per pass larger", passCount);
}

RadiusPlan planEnd(const RadiusSetup& setup, const RadiusWork& work)
{
    RadiusPlan plan;
    const double workRadius = work.diameter / 2.0;
    double radius = setup.radius;
    const double nose = setup.noseRadius;
    if (radius > workRadius + MultiPass::TOLERANCE) {
        plan.error = fmt::format(
            "The radius can't be more than half the diameter ({:.3f} is a full ball end)",
            workRadius);
        return plan;
    }
    // (A full ball end, if within the tolerance)
    radius = std::min(radius, workRadius);
    if (plan.error = depthAndNoseError(setup); !plan.error.empty()) {
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
        plan.error = tooManyPasses(passCount);
        return plan;
    }

    // The tool's tip, as touched off, is off the centre of its nose by the
    // nose radius in each axis: towards the chuck (it was touched on the end
    // face) and towards the centre line (on the diameter). The nose's
    // centre follows an arc nose radius larger than the cut, so the tip
    // follows that arc moved by the same amount: an arc of the same size,
    // about this centre
    const WorkPoint centre { radius + nose, workRadius - radius - nose };
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
        const std::vector<WorkPoint> points = arcAsTapers(
            arc, std::acos(k), std::asin(k), [&centre](double r, double angle) {
                return WorkPoint { centre.a - r * std::cos(angle), centre.r + r * std::sin(angle) };
            });

        MultiPassPath path;
        const WorkPoint start = points.front();
        path.feedStart = work.xSign * start.r;
        if (clearance > MultiPass::TOLERANCE) {
            // Z on to the end face first, X staying where it is
            path.points.push_back(toMachine(work, start));
        }
        MultiPassPathPoint previous = toMachine(work, start);
        for (std::size_t p = 1; p < points.size(); ++p) {
            previous = taperTo(work, previous, points[p]);
            path.points.push_back(previous);
        }
        plan.passes.push_back(std::move(path));
        plan.arcs.push_back(arc - nose);
    }

    plan.range.cutStart = cutStart;
    plan.range.cutEnd = plan.passes.back().points.back().cut;
    plan.range.feedStart = work.xSign * workRadius;
    plan.range.feedEnd = plan.passes.back().feedStart;
    return plan;
}

// Straight passes' depths, from one height (from the centre line) down to
// another, by the depth per pass (the last may be less)
std::vector<double> stepsDown(double from, double to, double depthPerPass)
{
    std::vector<double> levels;
    if (from - to < MultiPass::TOLERANCE) {
        return levels;
    }
    const int count = static_cast<int>(std::ceil((from - to) / depthPerPass - 1e-9));
    for (int n = 1; n <= count; ++n) {
        levels.push_back(std::max(to, from - n * depthPerPass));
    }
    return levels;
}

RadiusPlan planShoulder(const RadiusSetup& setup, const RadiusWork& work)
{
    RadiusPlan plan;
    const double workRadius = work.diameter / 2.0;
    const double finishedRadius = setup.finishedDiameter / 2.0;
    const double fillet = setup.radius;
    const double nose = setup.noseRadius;
    const double length = setup.shoulderLength;
    if (finishedRadius < MultiPass::TOLERANCE) {
        plan.error = "Type the finished diameter";
        return plan;
    }
    if (finishedRadius > workRadius - MultiPass::TOLERANCE) {
        plan.error = fmt::format(
            "The finished diameter must be less than the work's ({:.3f})", work.diameter);
        return plan;
    }
    if (length < MultiPass::TOLERANCE) {
        plan.error = "Type the length to the shoulder";
        return plan;
    }
    if (fillet > workRadius - finishedRadius + MultiPass::TOLERANCE) {
        plan.error = fmt::format(
            "The radius can't be more than the step's height ({:.3f})",
            workRadius - finishedRadius);
        return plan;
    }
    if (fillet > length + MultiPass::TOLERANCE) {
        plan.error = "The radius can't be more than the length to the shoulder";
        return plan;
    }
    if (plan.error = depthAndNoseError(setup); !plan.error.empty()) {
        return plan;
    }
    if (nose > fillet + MultiPass::TOLERANCE) {
        plan.error = "The tool's nose radius can't be more than the fillet's radius";
        return plan;
    }
    const double depth = setup.depthPerPass;
    // The fillet's top, where it meets the shoulder's face, and where it
    // starts, on the finished diameter
    const double filletTop = std::min(workRadius, finishedRadius + fillet);
    const double filletStart = std::max(0.0, length - fillet);

    // The straight passes: down to the fillet's top, right up to the
    // shoulder, then down to the finished diameter, stopping where the fillet
    // starts (if it doesn't start at the end face)
    const std::vector<double> toTop = stepsDown(workRadius, filletTop, depth);
    const std::vector<double> toFinished = filletStart > MultiPass::TOLERANCE
        ? stepsDown(filletTop, finishedRadius, depth)
        : std::vector<double> {};
    // The arcs' radii, growing by the depth per pass up to the fillet's, but
    // none the nose would fill (as it's at least as big): the first arc the
    // tip can follow then takes those off too. (If the nose is the fillet's
    // size, the last pass is just a move to the corner and up the face)
    const int arcCount = static_cast<int>(std::ceil(fillet / depth - 1e-9));
    std::vector<double> arcs;
    for (int n = arcCount - 1; n >= 0; --n) {
        const double arc = fillet - n * depth;
        if (arc - nose > MultiPass::TOLERANCE || n == 0) {
            arcs.push_back(arc);
        }
    }
    const int passCount = static_cast<int>(toTop.size() + toFinished.size() + arcs.size());
    if (passCount > RADIUS_MAX_PASSES) {
        plan.error = tooManyPasses(passCount);
        return plan;
    }

    const double clearance = std::max(0.0, work.clearance);
    const auto straight = [&](double level, double endA) {
        // (From off the end, as the tip is on the diameter and the face, as
        // touched off: a straight pass needs no allowing for the nose)
        plan.passes.push_back(
            { .feedStart = work.xSign * level, .points = { toMachine(work, { endA, level }) } });
        plan.arcs.push_back(0.0);
    };
    for (const double level : toTop) {
        straight(level, length);
    }
    for (const double level : toFinished) {
        straight(level, filletStart);
    }

    // The arcs are about the corner of the block left in the corner, at the
    // fillet's start and top. As on the end, the tip is off the nose's centre
    // by the nose radius towards the chuck and the centre line, and the nose's
    // centre follows an arc nose radius smaller than the cut (it's on the
    // inside of the curve): so the tip follows that, moved, about this centre
    const WorkPoint centre { filletStart + nose, filletTop - nose };
    // Each starts just short of the block, which is clear (the straight
    // passes have cut down to the finished diameter there)
    const double arcsCutStart = toMachine(work, { filletStart - clearance, 0.0 }).cut;
    for (std::size_t n = 0; n < arcs.size(); ++n) {
        const bool last = n + 1 == arcs.size();
        // Measuring the angle from straight down towards the centre line: it
        // starts on the block's side, and turns towards the chuck to its top
        const double arc = arcs[n] - nose;
        const std::vector<WorkPoint> points = arc > MultiPass::TOLERANCE
            ? arcAsTapers(
                  arc,
                  0.0,
                  M_PI / 2.0,
                  [&centre](double r, double angle) {
                      return WorkPoint { centre.a + r * std::sin(angle),
                                         centre.r - r * std::cos(angle) };
                  })
            : std::vector<WorkPoint> { centre };

        MultiPassPath path;
        path.cutStart = arcsCutStart;
        const WorkPoint start = points.front();
        path.feedStart = work.xSign * start.r;
        // Z on to the arc's start first, X staying where it is. (It's further
        // on than the block's side by the nose radius, which takes off what
        // the nose left there at the end of the straight passes)
        MultiPassPathPoint previous = toMachine(work, start);
        if (std::abs(previous.cut - arcsCutStart) > MultiPass::TOLERANCE) {
            path.points.push_back(previous);
        }
        for (std::size_t p = 1; p < points.size(); ++p) {
            previous = taperTo(work, previous, points[p]);
            path.points.push_back(previous);
        }
        if (last && points.back().r < filletTop - MultiPass::TOLERANCE) {
            // Up the shoulder's face to the fillet's top, X alone: the nose
            // left a little in the corner there at the end of the straight
            // passes to the shoulder, which the arc (ending square to the
            // face) doesn't reach
            path.points.push_back(
                { .cut = previous.cut, .feed = work.xSign * filletTop, .maxCutSpeed = 0.0 });
        }
        plan.passes.push_back(std::move(path));
        plan.arcs.push_back(arcs[n]);
    }

    plan.range.cutStart = work.workEndZ - work.zTowardsChuck * clearance;
    plan.range.cutEnd = toMachine(work, { length, 0.0 }).cut;
    plan.range.feedStart = work.xSign * workRadius;
    plan.range.feedEnd = work.xSign * finishedRadius;
    return plan;
}

} // end anonymous namespace

RadiusPlan planRadius(const RadiusSetup& setup, const RadiusWork& work)
{
    if (std::string error = commonError(setup, work); !error.empty()) {
        return { .error = std::move(error) };
    }
    return setup.kind == RadiusKind::Shoulder ? planShoulder(setup, work) : planEnd(setup, work);
}

} // namespace mgo
