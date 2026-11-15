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
// While the axis is at rest, the reading is still watched: if it changes by
// REST_CHANGE_COUNTS or more (e.g. the table moved, or the sender's smoothing
// caught up late), it settles again, as at the end of a move.
//
// A scale can misread: on the mill, interference (from the lathe's VFD, it
// seems) once made the reading jump by hundreds of mm at a time, for half an
// hour. So a settle only counts as steady if the reading stopped jumping, and
// a jump at rest bigger than faultMm (with the motor holding the table) is
// taken as a misread.
//
// update() must be called regularly (from the model's checkStatus()). All of
// this is used only from the UI thread, so needs no locking
class AxisScale {
public:
    using Clock = std::chrono::steady_clock;

    // At rest, a change of the reading by less than this is just flicker
    static constexpr int32_t REST_CHANGE_COUNTS = 2;

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
        // A change of the reading at rest by more than this (in mm; 0 is off)
        // can't be the table moving, so the scale is misreading
        double faultMm { 0.0 };
    };

    AxisScale(std::unique_ptr<IScale> scale, Settings settings);

    // Watches for the motor stopping and the reading settling, lines the
    // scale up if it needs to be, and logs the difference each time it
    // settles (so lc.log shows how long settling takes, and how the
    // difference changes from move to move). The motor's speed (in mm/min)
    // is only for the log, as the scale may misread at speed
    void update(
        double motorMm,
        bool motorRunning,
        double motorSpeed,
        Clock::time_point now = Clock::now());
    // Line the scale up with the motor again, once the reading has settled
    // (e.g. after zeroing the axis, as the motor's position has changed)
    void realign();
    // The motor's position has been set from the scale's (a re-base): not a
    // move, and the scale stays lined up as it is
    void motorRebased(double motorMm);

    // Whether there's a recent reading from the scale
    bool hasReading() const;
    // Whether the scale has been lined up with the motor (until then, there's
    // no position or difference)
    bool isAligned() const;
    // The scale's position, in the motor's frame (in mm)
    std::optional<double> positionMm() const;
    // The scale's position less the motor's
    std::optional<double> differenceMm(double motorMm) const;
    // How many times the reading has settled after the motor stopped. (So
    // something waiting for a move's reading can tell when it's ready)
    int settleCount() const;
    // Whether the reading has settled and is steady: it settled within the
    // settle time, or (after the longest time) had only flickered by a count
    // for the settle time before that. Not while it's settling again
    bool isSteady() const;
    // If the reading has jumped at rest by more than faultMm since it was
    // last lined up, by how much (the first such jump, in mm)
    std::optional<double> misreadJumpMm() const;
    // Which way the move before the last settle went: +1, -1, or 0 if it
    // settled after a change at rest (or the move wasn't seen)
    int lastMoveDirection() const;
    double countsPerMm() const;

private:
    // The reading in mm, before it's lined up with the motor
    double rawMm(int32_t counts) const;
    void align(int32_t counts, double motorMm);
    // Starts settling (after a move, or a change at rest)
    void startSettling(int32_t counts, Clock::time_point now);
    void checkForMisread(int32_t counts);

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
    // The fastest the motor was set to go during that move
    double m_moveTopSpeed { 0.0 };
    Clock::time_point m_stoppedAt;
    Clock::time_point m_lastChangeAt;
    int32_t m_lastCounts { 0 };
    // Whether the readings have been missing (so it's only logged once)
    bool m_noReadings { false };
    // Where the motor was on the last update (to see moves too quick to be
    // seen running)
    std::optional<double> m_lastMotorMm;
    // The reading when it last settled (or was lined up), to watch at rest
    int32_t m_settledCounts { 0 };
    // When the motor last stopped (for the log)
    Clock::time_point m_motorStoppedAt;
    // Whether it's settling after a change at rest, not a move
    bool m_changedAtRest { false };
    int m_settleCount { 0 };
    int m_lastMoveDirection { 0 };
    // While settling, the reading the last jump (a change of
    // REST_CHANGE_COUNTS or more, i.e. not flicker) went to, and when
    int32_t m_jumpCounts { 0 };
    Clock::time_point m_lastJumpAt;
    // See isSteady()
    bool m_lastSettleSteady { false };
    // See misreadJumpMm()
    std::optional<double> m_misreadJumpMm;
};

} // namespace mgo
