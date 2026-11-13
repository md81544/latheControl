#pragma once
// Linear scales, which measure where an axis really is, as opposed to where
// the motor's step count says it is. See "Use the mill's scales in
// latheControl" and "Linear scale" under Sensors in TODO.md

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace mgo {

// A scale's raw reading, in whatever counts it gives. Implemented by
// LinearScale (a quadrature scale read on two GPIO pins) and DroSerialScale
// (one axis of the mill's DRO sender, read from its serial stream)
class IScale {
public:
    virtual ~IScale() = default;
    // The latest reading, or nothing if there isn't a recent one (e.g. the
    // sender is switched off or unplugged)
    virtual std::optional<int32_t> readCounts() const = 0;
};

// A scale fitted to an axis with a motor. The scale's counts and the motor's
// steps are kept separate, and never mixed: the scale is given an offset
// which lines it up with the motor's position, so the two can be compared.
// It's lined up once the scale's reading has settled with the motor at rest:
// at the start, after the axis is zeroed or its position set, and after the
// readings have been lost for a while (e.g. the sender was switched off,
// which loses its count). After that, the difference between the two shows
// any steps lost since, or a wrong backlash setting.
//
// The motor's position already leaves out the steps spent taking up the
// backlash, so with the right backlash setting the difference stays close
// to zero whichever way the axis last moved.
//
// update() must be called regularly (from the model's checkStatus()). All of
// this is used only from the UI thread, so needs no locking
class AxisScale {
public:
    using Clock = std::chrono::steady_clock;

    struct Settings {
        // e.g. "X", for the log
        std::string axisLabel;
        double countsPerMm { 200.0 };
        // Whether the scale counts up as the motor's position goes down
        bool reversed { false };
        // The reading counts as settled once it hasn't changed for this long
        // after the motor stops...
        std::chrono::milliseconds settleTime { 500 };
        // ...or, if it keeps changing (e.g. flickering between two counts),
        // after this long
        std::chrono::milliseconds maxSettleTime { 3'000 };
    };

    AxisScale(std::unique_ptr<IScale> scale, Settings settings);

    // Watches for the motor stopping and the reading settling, lines the
    // scale up if it needs to be, and logs the difference each time it
    // settles (so lc.log shows how long settling takes, and how the
    // difference changes from move to move)
    void update(double motorMm, bool motorRunning, Clock::time_point now = Clock::now());
    // Line the scale up with the motor again, once the reading has settled
    // (e.g. after zeroing the axis, as the motor's position has changed)
    void realign();

    // Whether there's a recent reading from the scale
    bool hasReading() const;
    // Whether the scale has been lined up with the motor (until then, there's
    // no position or difference)
    bool isAligned() const;
    // The scale's position, in the motor's frame (in mm)
    std::optional<double> positionMm() const;
    // The scale's position less the motor's
    std::optional<double> differenceMm(double motorMm) const;
    double countsPerMm() const;

private:
    // The reading in mm, before it's lined up with the motor
    double rawMm(int32_t counts) const;
    void align(int32_t counts, double motorMm);

    enum class State {
        Moving, // (Or not yet seen at rest)
        Settling,
        Settled
    };

    std::unique_ptr<IScale> m_scale;
    Settings m_settings;
    State m_state { State::Moving };
    bool m_aligned { false };
    // Added to the raw reading to give the motor's frame
    double m_offsetMm { 0.0 };
    // Where the motor was when it started its last move (if seen)
    std::optional<double> m_moveStartMm;
    Clock::time_point m_stoppedAt;
    Clock::time_point m_lastChangeAt;
    int32_t m_lastCounts { 0 };
    // Whether the readings have been missing (so it's only logged once)
    bool m_noReadings { false };
};

} // namespace mgo
