#include "gearcutting.h"

#include <cmath>

namespace mgo {

namespace {

std::optional<double> memoryAt(const std::vector<std::optional<double>>& memory, std::size_t slot)
{
    return slot < memory.size() ? memory[slot] : std::nullopt;
}

std::string slotName(std::size_t slot)
{
    return "Mem " + std::to_string(slot + 1);
}

} // end anonymous namespace

double gearWholeDepthMm(double module)
{
    return 2.157 * module;
}

double gearBlankDiameterMm(double module, int teeth)
{
    return (teeth + 2) * module;
}

int gearCutterNumber(int teeth)
{
    // The fewest teeth each cutter is for, from No. 1 to No. 8
    constexpr int fewest[] = { 135, 55, 35, 26, 21, 17, 14, 12 };
    for (int n = 0; n < 8; ++n) {
        if (teeth >= fewest[n]) {
            return n + 1;
        }
    }
    return 0;
}

GearCutResolved resolveGearCut(
    const GearSetup& setup,
    const MultiPassPositions& positions,
    const std::string& cutLabel)
{
    GearCutResolved r;
    if (setup.cutStartMemory) {
        const auto cut = memoryAt(positions.cutMemory, *setup.cutStartMemory);
        if (!cut) {
            r.error = slotName(*setup.cutStartMemory) + " has no " + cutLabel + " position";
            return r;
        }
        r.cutStart = *cut;
    } else {
        r.cutStart = positions.cutHere;
    }
    if (setup.cutEndMemory) {
        const auto cut = memoryAt(positions.cutMemory, *setup.cutEndMemory);
        if (!cut) {
            r.error = slotName(*setup.cutEndMemory) + " has no " + cutLabel + " position";
            return r;
        }
        r.cutEnd = *cut;
    } else {
        r.cutEnd = r.cutStart + setup.cutDistance;
    }
    if (std::abs(r.cutStart - r.cutEnd) < MultiPass::TOLERANCE) {
        r.error = "The " + cutLabel + " start and end are the same";
        return r;
    }
    if (!(setup.module > 0.0)) {
        r.error = "The module must be more than 0";
        return r;
    }
    if (setup.teeth < 1 || setup.teeth > 999) {
        r.error = "The number of teeth must be from 1 to 999";
        return r;
    }
    if (!(setup.depth > 0.0)) {
        r.error = "The depth must be more than 0";
        return r;
    }
    if (!(setup.depthPerPass > 0.0)) {
        r.error = "The depth per pass must be more than 0";
        return r;
    }
    return r;
}

int gearPassCount(const GearSetup& setup)
{
    return multiPassCount({ .feedStart = 0.0, .feedEnd = setup.depth }, setup.depthPerPass);
}

GearSetup defaultGearSetup(const GearSetup& previous, const MultiPassPositions& positions)
{
    GearSetup setup = previous;
    const auto& memory = positions.cutMemory;
    const auto set = [&](std::size_t slot) { return memoryAt(memory, slot).has_value(); };

    // At a memory slot? Then start from it
    setup.cutStartMemory.reset();
    for (std::size_t slot = 0; slot < memory.size(); ++slot) {
        if (memory[slot] && atMultiPassPosition(*memory[slot], positions.cutHere)) {
            setup.cutStartMemory = slot;
            break;
        }
    }
    const auto usableEnd
        = [&](std::size_t slot) { return set(slot) && setup.cutStartMemory != slot; };
    if (!(previous.cutEndMemory && usableEnd(*previous.cutEndMemory))) {
        setup.cutEndMemory.reset();
        if (setup.cutStartMemory) {
            for (std::size_t slot = 0; slot < memory.size(); ++slot) {
                if (usableEnd(slot)) {
                    setup.cutEndMemory = slot;
                    break;
                }
            }
        }
    }
    if (setup.depth <= 0.0) {
        setup.depth = gearWholeDepthMm(setup.module);
    }
    return setup;
}

} // namespace mgo
