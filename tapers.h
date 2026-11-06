#pragma once

#include <string>
#include <vector>

namespace mgo {

// Tapers offered in the taper mode dialog (in addition to a custom angle).
// Angles are the half-angle (i.e. the angle of the tool's path to the axis),
// in degrees, and always positive: the dialog asks separately whether the tool
// moves in or out as it approaches the chuck
struct TaperPreset {
    std::string name;
    double angle;
};

// Morse taper half-angles are from their standard taper per foot (on the
// diameter): atan(taperPerFoot / 12 / 2)
const std::vector<TaperPreset> taperPresets {
    { "Morse taper MT0", 1.4908 },
    { "Morse taper MT1", 1.4287 },
    { "Morse taper MT2", 1.4307 },
    { "Morse taper MT3", 1.4377 },
    { "Morse taper MT4", 1.4876 },
    { "Morse taper MT5", 1.5073 },
    { "Morse taper MT6", 1.4933 },
    { "Chamfer 45 deg", 45.0 },
    { "Chamfer 30 deg", 30.0 },
};

} // namespace mgo
