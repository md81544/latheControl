#include "approachticks.h"

#include <algorithm>
#include <cmath>

namespace mgo {

ApproachTicks::ApproachTicks()
    : ApproachTicks(Settings {})
{
}

ApproachTicks::ApproachTicks(Settings settings)
    : m_settings(settings)
{
}

void ApproachTicks::reset()
{
    m_targetMm.reset();
    m_done = false;
    m_lastTick.reset();
    m_samples.clear();
}

std::optional<Sound>
ApproachTicks::update(const std::optional<Approach>& approach, Clock::time_point time)
{
    if (!approach) {
        reset();
        return std::nullopt;
    }
    const bool newGoTo = !m_targetMm || *m_targetMm != approach->targetMm;
    if (newGoTo) {
        reset();
        m_targetMm = approach->targetMm;
    }
    const double distance = std::abs(approach->toGoMm);
    m_samples.push_back({ time, distance });
    // (Keeping the newest sample older than the window, so the speed is
    // always taken over the whole window once there is one)
    while (m_samples.size() > 2 && time - m_samples[1].time >= SPEED_WINDOW) {
        m_samples.pop_front();
    }
    if (m_done) {
        return std::nullopt;
    }
    if (approach->past || distance <= m_settings.nearMm) {
        m_done = true;
        // (Starting this close, the user is already there)
        return newGoTo ? std::nullopt : std::optional(Sound::Alert);
    }
    const auto wait = interval(distance, time);
    if (!wait) {
        return std::nullopt;
    }
    if (m_lastTick && time - *m_lastTick < *wait) {
        return std::nullopt;
    }
    m_lastTick = time;
    return Sound::Tick;
}

std::optional<ApproachTicks::Clock::duration>
ApproachTicks::interval(double distanceMm, Clock::time_point time) const
{
    // As a fraction of the start distance or time: the interval is the
    // slowest one times this
    std::optional<double> fraction;
    if (distanceMm <= m_settings.startMm && m_settings.startMm > 0) {
        fraction = distanceMm / m_settings.startMm;
    }
    // The speed towards the target, over the window (only once there's most
    // of a window's worth, so a go-to's first moments don't give a wild one)
    const Sample& oldest = m_samples.front();
    const std::chrono::duration<double> span = time - oldest.time;
    if (span >= SPEED_WINDOW / 2 && m_settings.startSeconds > 0) {
        const double speed = (oldest.distanceMm - distanceMm) / span.count();
        if (speed > 0) {
            const double seconds = distanceMm / speed;
            if (seconds <= m_settings.startSeconds) {
                const double byTime = seconds / m_settings.startSeconds;
                fraction = fraction ? std::min(*fraction, byTime) : byTime;
            }
        }
    }
    if (!fraction) {
        return std::nullopt;
    }
    const auto wait = std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double>(m_settings.slowestInterval) * *fraction);
    return std::max<Clock::duration>(wait, FASTEST_INTERVAL);
}

} // end namespace
