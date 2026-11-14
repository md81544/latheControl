#pragma once
// Measuring an axis's backlash with its linear scale (System menu, "Measure
// X's backlash"). See "Use the mill's scales in latheControl" in TODO.md

#include <optional>
#include <utility>
#include <vector>

namespace mgo {

// The axis moves back and forth by a distance, with the backlash
// compensation on, as for any move. The first move only puts the slack on a
// known side. Each move after it is a reversal, and once the scale has
// settled, the table should have moved the whole distance: any shortfall is
// backlash which the compensation doesn't cover (or, if it moved further, the
// compensation is too much). So the backlash is the compensation plus the
// average shortfall. The compensation stays on, so the motor's idea of where
// the table is stays right throughout. The axis ends where it started.
//
// This only keeps the arithmetic: the model makes the moves, and tells it
// where the motor and the scale are after each one
class BacklashMeasurement {
public:
    // How many moves there are: the first, then five reversals
    static constexpr int MOVES = 6;

    struct Result {
        // The compensation while measuring
        double compensationMm { 0.0 };
        // The average shortfall after reversing to move in the positive
        // direction, and the negative
        double positiveShortfallMm { 0.0 };
        double negativeShortfallMm { 0.0 };
        // The largest shortfall less the smallest: how far to trust it
        double spreadMm { 0.0 };
        double backlashMm() const
        {
            return compensationMm + (positiveShortfallMm + negativeShortfallMm) / 2.0;
        }
    };

    // firstDirection: +1 or -1, the way the first move goes from startMm
    BacklashMeasurement(
        double startMm,
        double distanceMm,
        int firstDirection,
        double compensationMm);

    // Where to move next, or nothing once finished
    std::optional<double> nextTarget() const;
    // After each move, once the axis has stopped and the scale has settled
    void record(double motorMm, double scaleMm);
    // How many moves have been recorded
    int movesDone() const;
    bool finished() const;
    // Once finished
    Result result() const;

private:
    std::vector<double> m_targets;
    double m_compensationMm;
    // The motor's and the scale's positions after each move
    std::vector<std::pair<double, double>> m_readings;
};

} // namespace mgo
