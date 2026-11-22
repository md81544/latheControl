#pragma once
// Ticks which get faster as a manual axis is wound towards a go-to's target
// (as TouchDRO does), then the alert once it's close. See "Ticks nearing a
// target" in TODO.md

#include "soundcues.h"

#include <chrono>
#include <deque>
#include <optional>

namespace mgo {

// One for each manual axis. Called once per tick, it says when to play a
// tick, or the alert.
//
// The time between ticks is the shorter of two: one from the distance to go,
// and one from the time to go at the speed the handle is being wound. Each is
// in proportion: it's the slowest interval at the start distance (or time),
// and halves each time the distance (or time) halves. Winding fast, the time
// to go starts the ticks early; creeping, or stopped to read the figures, the
// distance keeps them going, so they never slow down while the axis gets
// closer (as ticks from the time alone would, when the user slows down near
// the target). Moving away gives no ticks from the time.
//
// Once the axis is within the near distance, or has gone past the target, the
// alert plays once, and that go-to has no more sounds: from there the user
// reads the figures. A go-to which starts that close has none at all
class ApproachTicks {
public:
    using Clock = std::chrono::steady_clock;

    struct Settings {
        // Ticks start within this distance of the target (mm)
        double startMm { 2.0 };
        // or this long from it, at the speed it's being wound (s)
        double startSeconds { 1.0 };
        // The alert plays within this distance (mm), and the ticks stop
        double nearMm { 0.1 };
        // The time between ticks at the start distance or time
        std::chrono::milliseconds slowestInterval { 500 };
    };

    // The fastest the ticks go: once per tick of the run loop (see
    // Controller::TICK_INTERVAL)
    static constexpr std::chrono::milliseconds FASTEST_INTERVAL { 50 };
    // The speed is averaged over this long, as the readings arrive in
    // counts, and winding a handle is jerky
    static constexpr std::chrono::milliseconds SPEED_WINDOW { 400 };

    // A go-to under way (see Model::manualAxisGoTo())
    struct Approach {
        double targetMm { 0.0 };
        // The target less where the axis is now
        double toGoMm { 0.0 };
        // Past the target, the way it was being wound
        bool past { false };
    };

    // (With the default settings)
    ApproachTicks();
    explicit ApproachTicks(Settings settings);

    // The axis's go-to, or nothing if none is under way. Returns Sound::Tick
    // or Sound::Alert when one is due
    std::optional<Sound> update(const std::optional<Approach>& approach, Clock::time_point time);

private:
    void reset();
    // The time between ticks now, or nothing if there are none
    std::optional<Clock::duration> interval(double distanceMm, Clock::time_point time) const;

    Settings m_settings;
    // The go-to being followed (a new target is a new go-to)
    std::optional<double> m_targetMm;
    // Its alert has played (or it started too close for one)
    bool m_done { false };
    std::optional<Clock::time_point> m_lastTick;
    struct Sample {
        Clock::time_point time;
        double distanceMm;
    };
    // The distances to go over the last SPEED_WINDOW, oldest first
    std::deque<Sample> m_samples;
};

} // end namespace
