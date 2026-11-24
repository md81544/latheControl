#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace mgo {

// Bolt holes (mill): a number of holes spaced evenly round a circle (the
// pitch circle, whose diameter is the PCD), worked out as X and Y positions.
// In the mode, n and p go to the next and previous hole: X (axis 1) moves
// there, and Y (axis 3, moved by hand) is wound there, with the ticks as for
// any go-to wound by hand. Nothing is drilled by the program: it only puts
// the work under the spindle.
//
// Optionally, a depth: a position on Z (axis 4, moved by hand, e.g. the
// mill's quill). Once X and Y are at a hole, Z gets a go-to to the depth,
// wound by hand with the ticks, as a depth stop. Where Z was when the mode
// started is taken as clear of the work: n, p and Enter won't move X and Y
// while Z is further than BOLT_HOLE_Z_CLEAR_MM below it ("below" being
// towards the depth), so the drill can't be dragged through the work.
//
// Angles are in degrees from +Y (so a first hole at 0° is the one with the
// highest Y), going clockwise as seen from above, from +Y towards +X, unless
// the holes are numbered anticlockwise. (That assumes X's position goes up to
// the right and Y's away from you, as a DRO usually shows them)

// Where the circle's centre is
enum class BoltHoleCentre {
    // Where X and Y are when the dialog opens
    Here,
    // At X 0, Y 0 (e.g. having zeroed both on the work's centre)
    Zero,
    // In a memory slot (which has both X and Y set)
    Memory
};

// What the bolt hole dialog chooses. Lengths are in mm
struct BoltHoleSetup {
    // (0 until chosen)
    int holes { 0 };
    // The pitch circle's diameter
    double pcd { 0.0 };
    // The first hole's angle from +Y, the way the holes are numbered
    double firstAngleDeg { 0.0 };
    // Whether the holes are numbered clockwise (seen from above)
    bool clockwise { true };
    BoltHoleCentre centre { BoltHoleCentre::Here };
    // (Only for BoltHoleCentre::Memory)
    std::size_t centreMemory { 0 };
    // The depth to drill to, as a Z position, or none
    std::optional<double> depth;
};

// Where X and Y are now, and their memory slots (in mm). Y may have no
// reading (e.g. the DRO sender is off)
struct BoltHolePositions {
    double xHere { 0.0 };
    std::optional<double> yHere;
    std::vector<std::optional<double>> xMemory;
    std::vector<std::optional<double>> yMemory;
    // Whether there's a Z (axis 4), and where it is, if it has a reading
    bool hasZ { false };
    std::optional<double> zHere;
};

// How far below its start Z may be (towards the depth) for n, p and Enter to
// move X and Y, in mm. (Enough for the scale's flicker, and for not quite
// winding the quill back to where it was)
constexpr double BOLT_HOLE_Z_CLEAR_MM = 0.1;

// The most holes a circle may have (more is surely a mistake)
constexpr int BOLT_HOLE_MAX_HOLES = 360;

struct BoltHole {
    double x { 0.0 };
    double y { 0.0 };
    // From +Y, clockwise, from 0 up to 360 (whichever way they're numbered)
    double angleDeg { 0.0 };
};

// The circle's centre and its holes, in order (hole 1 first), or (if error
// isn't empty) why they can't be worked out. xLabel and yLabel (e.g. "X" and
// "Y"), and zLabel, are for the error messages. A depth needs a Z with a
// reading, away from where it is now
struct BoltHolePlan {
    double centreX { 0.0 };
    double centreY { 0.0 };
    // With a depth: where Z is at the start (taken as clear of the work)
    std::optional<double> zStart;
    std::vector<BoltHole> holes;
    std::string error;
};
BoltHolePlan planBoltHoles(
    const BoltHoleSetup& setup,
    const BoltHolePositions& positions,
    const std::string& xLabel = "X",
    const std::string& yLabel = "Y",
    const std::string& zLabel = "Z");

// How far Z is below its start (towards the depth), in mm: negative if it's
// above it. 0 without a depth
double boltHoleZBelowStartMm(const BoltHoleSetup& setup, const BoltHolePlan& plan, double z);

// The distance between neighbouring holes' centres (a chord of the circle),
// e.g. to check the first two with calipers. 0 with fewer than two holes
double boltHoleSpacingMm(const BoltHoleSetup& setup);

// An angle put into the range 0 up to 360
double normaliseDegrees(double deg);

// The memory slots which can be the centre: those with both X and Y set
std::vector<std::size_t> boltHoleCentreSlots(const BoltHolePositions& positions);

// The setup the dialog starts with: the one used last time, but with the
// centre here if it was a memory slot which no longer has both X and Y set
BoltHoleSetup
defaultBoltHoleSetup(const BoltHoleSetup& previous, const BoltHolePositions& positions);

} // namespace mgo
