#pragma once

#include <string>

namespace mgo {

// Units lengths are displayed (and typed) in. Everything is metric
// internally; imperial is purely for display and entry
enum class DisplayUnits {
    Metric, // mm
    Imperial // inches
};

constexpr double MM_PER_INCH = 25.4;
constexpr double FEET_PER_METRE = 1.0 / 0.3048;

// A length in mm, in the display units
inline double toDisplayLength(double mm, DisplayUnits units)
{
    return units == DisplayUnits::Imperial ? mm / MM_PER_INCH : mm;
}

// A length typed in the display units, in mm
inline double fromDisplayLength(double value, DisplayUnits units)
{
    return units == DisplayUnits::Imperial ? value * MM_PER_INCH : value;
}

inline std::string lengthUnitName(DisplayUnits units)
{
    return units == DisplayUnits::Imperial ? "in" : "mm";
}

// Decimal places for positions: 0.01mm, or 0.0001in (a tenth of a thou)
inline int positionDecimals(DisplayUnits units)
{
    return units == DisplayUnits::Imperial ? 4 : 2;
}

} // namespace mgo
