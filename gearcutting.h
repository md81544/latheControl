#pragma once

#include "multipass.h"

#include <cstddef>
#include <optional>
#include <string>

namespace mgo {

// Gear cutting: a spur gear cut with a form (involute) cutter, the blank on a
// rotary table whose axis is along the cut axis (e.g. X on the mill). Each
// tooth space is a cut along the cut axis; the rotary table (axis 2) turns
// the blank on by one tooth between cuts; and the depth is fed in by hand,
// once per turn of the blank. (It replaces the electronicRotaryTable program,
// which turned the table and waited for each cut to be taken by hand.)

// What the gear dialog chooses. Lengths are in mm
struct GearSetup {
    double module { 1.0 };
    // (0 until chosen)
    int teeth { 0 };
    // The cut axis's start: a memory slot (its cut axis position), or nullopt
    // for where it is now. It should be clear of the blank, as the table
    // turns there
    std::optional<std::size_t> cutStartMemory;
    // The cut axis's end: a memory slot, or nullopt for cutDistance from the
    // start (signed, as the display shows positions). It should be clear of
    // the blank's far side
    std::optional<std::size_t> cutEndMemory;
    double cutDistance { 0.0 };
    // How deep to cut the tooth spaces (usually gearWholeDepthMm()), and how
    // much deeper each pass goes (each pass cuts every tooth once, going
    // once round the blank). The last pass may go in by less, to finish at
    // the depth
    double depth { 0.0 };
    double depthPerPass { 0.0 };
};

// The whole depth of a tooth space: 2.157 × module, as the
// electronicRotaryTable program used. (Some tables give 2.25 × module, or
// 2.4 × module below module 1.25: the dialog's depth can be changed)
double gearWholeDepthMm(double module);

// The blank's diameter (the gear's outside diameter): (teeth + 2) × module
double gearBlankDiameterMm(double module, int teeth);

// Which cutter of the usual set of eight involute gear cutters suits a number
// of teeth (1 for 135 teeth up to a rack, to 8 for 12 or 13 teeth), or 0 if
// there are too few teeth for any of them
int gearCutterNumber(int teeth);

// The cut axis's start and end for a setup, or (if error isn't empty) why
// they can't be worked out. positions gives where the cut axis is now, and
// its memory slots (the feed axis's aren't used). cutLabel (e.g. "X") is for
// the error messages
struct GearCutResolved {
    double cutStart { 0.0 };
    double cutEnd { 0.0 };
    std::string error;
};
GearCutResolved resolveGearCut(
    const GearSetup& setup,
    const MultiPassPositions& positions,
    const std::string& cutLabel = "X");

// How many passes a setup takes (at least one), or 0 if it has no depth per
// pass
int gearPassCount(const GearSetup& setup);

// The setup the dialog starts with, from the one used last time: as before,
// but the start is a memory slot if the cut axis is at one, or else where it
// is now; the end is the previous one's memory slot if that's still set (and
// isn't the start), or else, if the start is a memory slot, the first other
// slot set, or else a distance. The depth is the whole depth if it isn't set
GearSetup defaultGearSetup(const GearSetup& previous, const MultiPassPositions& positions);

} // namespace mgo
