#include "soundcues.h"

#include "model.h"

#include <utility>

namespace mgo {

SoundState soundStateOf(const Model& model)
{
    return SoundState {
        .warning = model.getWarning(),
        .faulted = !model.getFaultMessage().empty(),
        .limitSwitchTripped = model.limitSwitchTripped(),
        .scaleMismatchAsks = model.axis1ScaleMismatchAsks(),
        .multiPassState = model.multiPass().state(),
        .movingToStart = model.multiPassMovingToStart(),
        .atStart = model.multiPassAtStart(),
    };
}

std::optional<Sound>
SoundCues::update(const SoundState& afterKeys, const SoundState& now, Clock::time_point time)
{
    const SoundState last = std::exchange(m_last, now);

    bool alarm = (now.faulted && !last.faulted)
        || (now.limitSwitchTripped && !last.limitSwitchTripped)
        || now.scaleMismatchAsks > last.scaleMismatchAsks;
    if (!now.warning.empty() && now.warning != afterKeys.warning) {
        const bool repeat = now.warning == m_lastAlarmWarning && m_lastAlarmWarningTime
            && time - *m_lastAlarmWarningTime < WARNING_REPEAT_INTERVAL;
        if (!repeat) {
            alarm = true;
            m_lastAlarmWarning = now.warning;
            m_lastAlarmWarningTime = time;
        }
    }
    if (alarm) {
        return Sound::Alarm;
    }

    using State = MultiPass::State;
    const bool waiting
        = (now.multiPassState == State::Paused || now.multiPassState == State::Finished)
        && now.multiPassState != afterKeys.multiPassState;
    const bool arrivedAtStart = afterKeys.movingToStart && !now.movingToStart && now.atStart
        && now.multiPassState == State::Idle;
    if (waiting || arrivedAtStart) {
        return Sound::Alert;
    }
    return std::nullopt;
}

} // end namespace
