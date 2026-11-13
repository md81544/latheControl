#pragma once

#include "multipass.h"

#include <string>
#include <vector>

namespace mgo {

// Radius cutting (lathe): a convex radius on the corner of the work's end,
// from a radius of 0.5 mm or so up to a full ball end (a radius of half the
// diameter). It starts from the current feature: with the end faced and the
// diameter set, the program knows where the corner is, so nothing else needs
// setting up.
//
// Each pass is an arc about the finished radius's centre, starting at the
// largest that still touches the corner and stepping in by the depth per pass
// to the finished radius, so every pass takes off about the same depth. Each
// arc is clipped to the work: it starts on the end face and ends on the
// diameter, so the first passes are short and nearly straight (light taper
// cuts across the corner), and the last runs from the end face round to the
// diameter. Each arc is cut as a series of short tapers (see
// MultiPassPlan::paths), as tapers are what the machine already does well: X
// follows Z at a fixed ratio on each. X only moves outwards and Z only towards
// the chuck during a cut, so neither reverses (and takes up its backlash) part
// way.
//
// Near the end face the arc is almost square to Z, so X has to move many times
// as fast as Z: Z is slowed there so that X can keep up (see
// RadiusWork::xMaxSpeed), as an X lagging behind would cut too deep.

// What the radius dialog chooses. Lengths are in mm
struct RadiusSetup {
    double radius { 0.0 };
    double depthPerPass { 0.0 };
    // The tool's nose radius (0 for a sharp tool). The path is offset to
    // allow for it, assuming the tool was touched off on the diameter and
    // the end face, as a right-hand turning tool is
    double noseRadius { 0.0 };
};

// Where the work is, from the current feature, and what the machine can do
struct RadiusWork {
    // The work's diameter, and the Z of its (faced) end
    double diameter { 0.0 };
    double workEndZ { 0.0 };
    // +1 if Z's position increases towards the chuck, -1 if it decreases
    double zTowardsChuck { -1.0 };
    // +1 if X's position is positive on the side of the centre the tool is
    // on (i.e. as the feature's X), -1 if negative
    double xSign { 1.0 };
    // How far off the end face each pass starts (Z moves on to the face
    // before the arc starts)
    double clearance { 0.5 };
    // The fastest X can follow Z, in mm/min: Z is slowed where the arc is
    // steep so that X needs no more than RADIUS_X_SPEED_FRACTION of it. 0
    // for no limit. (The fastest it can start at, without ramping up: on a
    // short taper it may never ramp up)
    double xMaxSpeed { 0.0 };
};

// How much of X's maximum speed a pass may need, leaving a margin (X
// follows Z in small updates, so needs to be able to catch up)
constexpr double RADIUS_X_SPEED_FRACTION = 0.8;
// The most an arc may be from the tapers it's cut as (in mm, either way)
constexpr double RADIUS_CHORD_TOLERANCE = 0.005;
// The largest angle of arc one taper may cover, in degrees (for small radii,
// whose tapers could otherwise cover a lot of the arc)
constexpr double RADIUS_MAX_SEGMENT_DEG = 10.0;
// The most passes a radius may take (more is surely a mistake, e.g. a tiny
// depth per pass)
constexpr int RADIUS_MAX_PASSES = 500;

// A radius's passes, and the multi-pass's range for display and the move to
// the start: Z from off the end face to the end of the last pass, and X from
// the work's diameter (where the tool goes for the start) to where the last
// pass starts. Or, if error isn't empty, why it can't be cut
struct RadiusPlan {
    MultiPassRange range;
    std::vector<MultiPassPath> passes;
    std::string error;
};
RadiusPlan planRadius(const RadiusSetup& setup, const RadiusWork& work);

} // namespace mgo
