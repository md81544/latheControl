#pragma once
// Axes moved by hand, whose positions are read from scales (e.g. the mill's Y
// and Z, from its DRO sender). See "An array of axes, with manual axes" under
// "Structure and refactoring" in TODO.md

#include "scale.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace mgo {

// An axis with no motor: its position is its scale's reading plus an offset,
// which zeroing it, setting its position or halving it changes (as on a DRO).
// It starts at zero, once there's a reading.
//
// If the readings are lost (e.g. the DRO sender was switched off, which
// restarts its count), the zero is lost with them: when they return, the
// axis starts at zero again, wherever it is.
//
// update() must be called regularly (from the model's checkStatus()). All of
// this is used only from the UI thread, so needs no locking
class ManualAxis {
public:
    struct Settings {
        // e.g. "Y"
        std::string label;
        double countsPerMm { 100.0 };
        // Whether the scale counts down as the axis's position goes up
        bool reversed { false };
    };

    ManualAxis(std::unique_ptr<IScale> scale, Settings settings);

    // Watches for the readings being lost, and returning (when it starts at
    // zero again). The first reading counts as a return
    void update();

    const std::string& label() const;
    bool hasReading() const;
    // The position (in mm), or nothing without a reading
    std::optional<double> positionMm() const;

    // Each returns false (changing nothing) if there's no reading
    bool zero();
    bool setPositionMm(double mm);
    // Sets the position to half what it is now. Having zeroed it on one edge
    // and moved to the other, zero is then halfway between them
    bool half();

private:
    // The reading in mm, before the offset is added
    double rawMm(int32_t counts) const;

    std::unique_ptr<IScale> m_scale;
    Settings m_settings;
    // Added to the raw reading to give the position
    double m_offsetMm { 0.0 };
    // Whether there was a reading at the last update() (so a loss, and a
    // return, are only logged once)
    bool m_hadReading { false };
};

} // namespace mgo
