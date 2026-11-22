#pragma once

#include "multipass.h"

#include <chrono>
#include <optional>
#include <string>

namespace mgo {

class Model;

// The sounds that can be played (each a WAV file set in the config: see
// SoundAlert, SoundAlarm and SoundTick in lc.cfg), least important first:
// when more than one is due at once, the last of them plays
enum class Sound {
    Tick, // A manual axis nearing its go-to's target (see ApproachTicks)
    Alert, // The program is waiting for the user, e.g. Enter between passes
    Alarm // Something has gone wrong, e.g. a fault or a warning
};

// What SoundCues looks at: the parts of the model's state which call for a
// sound when they change
struct SoundState {
    std::string warning; // Model::getWarning()
    bool faulted { false }; // A motor's driver is in alarm
    bool limitSwitchTripped { false };
    int scaleMismatchAsks { 0 }; // Model::axis1ScaleMismatchAsks()
    MultiPass::State multiPassState { MultiPass::State::Idle };
    bool movingToStart { false }; // Model::multiPassMovingToStart()
    bool atStart { false }; // Model::multiPassAtStart()
};

SoundState soundStateOf(const Model& model);

// Decides when to play a sound, from the model's state each tick. The alarm
// is for problems the user didn't just cause: a motor fault, a limit switch,
// the scale disagreeing with the motor, or a new warning which appeared while
// checking the status (not one replying to a key, such as "Stop the motors
// first", as the user is already looking). The alert is for when the program
// stops to wait for the user: a multi-pass pausing between passes, finishing,
// or arriving at its start. Ordinary dialogs, which the user opened, have no
// sound
class SoundCues {
public:
    using Clock = std::chrono::steady_clock;

    // The same warning doesn't sound again within this time (e.g. the
    // spindle hovering around the speed limit for a thread, which turns
    // its warning on and off)
    static constexpr std::chrono::seconds WARNING_REPEAT_INTERVAL { 10 };

    // Called once per tick: afterKeys is the state just after the keys were
    // acted on, and now after the status was checked. Returns the sound to
    // play, if any (the alarm, if both are called for)
    std::optional<Sound>
    update(const SoundState& afterKeys, const SoundState& now, Clock::time_point time);

private:
    // The state at the end of the last tick (faults and the like can appear
    // at any time, e.g. while the keys are acted on)
    SoundState m_last;
    std::string m_lastAlarmWarning;
    std::optional<Clock::time_point> m_lastAlarmWarningTime;
};

} // end namespace
