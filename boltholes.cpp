#include "boltholes.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include <fmt/format.h>

namespace mgo {

double normaliseDegrees(double deg)
{
    double d = std::fmod(deg, 360.0);
    if (d < 0.0) {
        d += 360.0;
    }
    // (fmod can give 360 for a tiny negative angle, once 360 is added)
    return d >= 360.0 ? 0.0 : d;
}

std::vector<std::size_t> boltHoleCentreSlots(const BoltHolePositions& positions)
{
    std::vector<std::size_t> slots;
    const std::size_t n = std::min(positions.xMemory.size(), positions.yMemory.size());
    for (std::size_t slot = 0; slot < n; ++slot) {
        if (positions.xMemory[slot] && positions.yMemory[slot]) {
            slots.push_back(slot);
        }
    }
    return slots;
}

BoltHolePlan planBoltHoles(
    const BoltHoleSetup& setup,
    const BoltHolePositions& positions,
    const std::string& xLabel,
    const std::string& yLabel,
    const std::string& zLabel)
{
    BoltHolePlan plan;
    if (setup.holes < 1) {
        plan.error = "Type how many holes there are";
        return plan;
    }
    if (setup.holes > BOLT_HOLE_MAX_HOLES) {
        plan.error = fmt::format("Too many holes (at most {})", BOLT_HOLE_MAX_HOLES);
        return plan;
    }
    if (!(setup.pcd > 0.0)) {
        plan.error = "Type the pitch circle's diameter (PCD)";
        return plan;
    }
    switch (setup.centre) {
        case BoltHoleCentre::Here:
            if (!positions.yHere) {
                plan.error = yLabel + " has no reading from its scale, so the centre isn't known";
                return plan;
            }
            plan.centreX = positions.xHere;
            plan.centreY = *positions.yHere;
            break;
        case BoltHoleCentre::Zero:
            break;
        case BoltHoleCentre::Memory:
            {
                const std::size_t slot = setup.centreMemory;
                const bool x = slot < positions.xMemory.size() && positions.xMemory[slot];
                const bool y = slot < positions.yMemory.size() && positions.yMemory[slot];
                if (!x || !y) {
                    plan.error = fmt::format(
                        "Mem {} needs both {} and {} set to be the centre",
                        slot + 1,
                        xLabel,
                        yLabel);
                    return plan;
                }
                plan.centreX = *positions.xMemory[slot];
                plan.centreY = *positions.yMemory[slot];
                break;
            }
    }
    if (setup.depth) {
        if (!positions.hasZ) {
            plan.error = "There's no " + zLabel + " for a depth";
            return plan;
        }
        if (!positions.zHere) {
            plan.error = zLabel + " has no reading from its scale, so the depth can't be watched";
            return plan;
        }
        // (Which way is down comes from the depth, so it can't be here)
        if (std::abs(*setup.depth - *positions.zHere) <= BOLT_HOLE_Z_CLEAR_MM) {
            plan.error = "The depth is where " + zLabel
                + " is now: raise the drill clear of the work first";
            return plan;
        }
        plan.zStart = positions.zHere;
    }
    const double radius = setup.pcd / 2.0;
    const double pitch = 360.0 / setup.holes;
    for (int n = 0; n < setup.holes; ++n) {
        // (Clockwise from +Y: +X is at 90°)
        const double along = setup.firstAngleDeg + (setup.clockwise ? 1.0 : -1.0) * n * pitch;
        const double angle = normaliseDegrees(along);
        const double rad = angle * std::numbers::pi / 180.0;
        double x = radius * std::sin(rad);
        double y = radius * std::cos(rad);
        // (Not -0.000 for a hole on an axis)
        if (std::abs(x) < 1e-9) {
            x = 0.0;
        }
        if (std::abs(y) < 1e-9) {
            y = 0.0;
        }
        plan.holes.push_back({ .x = plan.centreX + x, .y = plan.centreY + y, .angleDeg = angle });
    }
    return plan;
}

double boltHoleZBelowStartMm(const BoltHoleSetup& setup, const BoltHolePlan& plan, double z)
{
    if (!setup.depth || !plan.zStart) {
        return 0.0;
    }
    const double down = *setup.depth < *plan.zStart ? -1.0 : 1.0;
    return (z - *plan.zStart) * down;
}

double boltHoleSpacingMm(const BoltHoleSetup& setup)
{
    if (setup.holes < 2) {
        return 0.0;
    }
    return setup.pcd * std::sin(std::numbers::pi / setup.holes);
}

BoltHoleSetup
defaultBoltHoleSetup(const BoltHoleSetup& previous, const BoltHolePositions& positions)
{
    BoltHoleSetup setup = previous;
    if (setup.centre == BoltHoleCentre::Memory) {
        const auto slots = boltHoleCentreSlots(positions);
        if (std::find(slots.begin(), slots.end(), setup.centreMemory) == slots.end()) {
            setup.centre = BoltHoleCentre::Here;
        }
    }
    return setup;
}

} // namespace mgo
