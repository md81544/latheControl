#include "scale.h"

#include "log.h"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>

namespace mgo {

AxisScale::AxisScale(std::unique_ptr<IScale> scale, Settings settings)
    : m_scale(std::move(scale))
    , m_settings(std::move(settings))
{
}

void AxisScale::update(double motorMm, bool motorRunning, double motorSpeed, Clock::time_point now)
{
    const std::optional<int32_t> counts = m_scale->readCounts();
    if (!counts) {
        // The count may have been lost (if the sender was switched off, the
        // scales start counting from wherever they are), so line up again
        // once there's a reading
        if (!m_noReadings) {
            MGOLOG(
                fmt::format(
                    "{} scale: no readings{}",
                    m_settings.axisLabel,
                    m_aligned ? "; it will be lined up with the motor again when they return"
                              : ""));
            m_noReadings = true;
        }
        m_aligned = false;
        m_state = State::Moving;
        m_moveStartMm.reset();
        return;
    }
    if (m_noReadings) {
        MGOLOG(fmt::format("{} scale: readings received", m_settings.axisLabel));
        m_noReadings = false;
    }

    if (motorRunning) {
        if (m_state != State::Moving) {
            m_moveStartMm = motorMm;
            m_moveTopSpeed = 0.0;
        }
        m_moveTopSpeed = std::max(m_moveTopSpeed, std::abs(motorSpeed));
        m_state = State::Moving;
        m_lastMotorMm = motorMm;
        return;
    }
    // A move which started and finished between two updates (e.g. a quick
    // nudge) is a move all the same, so the reading must settle again
    if (m_state != State::Moving && m_lastMotorMm && motorMm != *m_lastMotorMm) {
        if (m_state == State::Settled) {
            m_moveStartMm = *m_lastMotorMm;
            m_moveTopSpeed = std::abs(motorSpeed);
        }
        m_state = State::Moving;
    }
    m_lastMotorMm = motorMm;
    if (m_state == State::Moving) {
        m_state = State::Settling;
        m_stoppedAt = now;
        m_motorStoppedAt = now;
        m_lastChangeAt = now;
        m_lastCounts = *counts;
        m_changedAtRest = false;
        return;
    }
    if (m_state == State::Settled) {
        if (!m_aligned) {
            // (realign() while at rest: the reading is already steady)
            align(*counts, motorMm);
            return;
        }
        // Still watching, at rest: the table may move (e.g. springiness
        // releasing, cutting forces, or a knock), or the sender's smoothing
        // may catch up late. A change of a count is just flicker; more, and
        // it settles again, as at the end of a move, so the position follows
        // (or, if far out, the user is asked)
        const int32_t change = *counts - m_settledCounts;
        if (std::abs(change) < REST_CHANGE_COUNTS) {
            return;
        }
        MGOLOG(
            fmt::format(
                "{} scale's reading changed at rest by {:+.4f} mm ({:+} counts), {:.1f} s after "
                "the axis stopped",
                m_settings.axisLabel,
                rawMm(*counts) - rawMm(m_settledCounts),
                m_settings.reversed ? -change : change,
                std::chrono::duration<double>(now - m_motorStoppedAt).count()));
        m_state = State::Settling;
        m_stoppedAt = now;
        m_lastChangeAt = now;
        m_lastCounts = *counts;
        m_changedAtRest = true;
        return;
    }

    // Settling
    if (*counts != m_lastCounts) {
        m_lastCounts = *counts;
        m_lastChangeAt = now;
    }
    const bool steady = now - m_lastChangeAt >= m_settings.settleTime;
    if (!steady && now - m_stoppedAt < m_settings.maxSettleTime) {
        return;
    }
    m_state = State::Settled;
    ++m_settleCount;
    m_settledCounts = *counts;
    const double seconds = std::chrono::duration<double>(now - m_stoppedAt).count();
    if (!m_aligned) {
        align(*counts, motorMm);
        return;
    }
    const double scaleMm = rawMm(*counts) + m_offsetMm;
    const double difference = scaleMm - motorMm;
    MGOLOG(
        fmt::format(
            "{} scale {} {:.2f} s after {}{}: scale {:.4f} mm, motor {:.4f} mm, "
            "difference {:+.4f} mm ({:+.1f} counts)",
            m_settings.axisLabel,
            steady ? "settled" : "still changing",
            seconds,
            m_changedAtRest ? "changing at rest" : "stopping",
            m_moveStartMm ? fmt::format(
                                " after a move of {:+.4f} mm at up to {:.0f} mm/min",
                                motorMm - *m_moveStartMm,
                                m_moveTopSpeed)
                          : "",
            scaleMm,
            motorMm,
            difference,
            difference * m_settings.countsPerMm));
    m_moveStartMm.reset();
}

void AxisScale::realign()
{
    m_aligned = false;
    // (The motor's position changes, e.g. when zeroed, without it moving)
    m_lastMotorMm.reset();
}

bool AxisScale::hasReading() const
{
    return m_scale->readCounts().has_value();
}

bool AxisScale::isAligned() const
{
    return m_aligned;
}

std::optional<double> AxisScale::positionMm() const
{
    if (!m_aligned) {
        return std::nullopt;
    }
    const std::optional<int32_t> counts = m_scale->readCounts();
    if (!counts) {
        return std::nullopt;
    }
    return rawMm(*counts) + m_offsetMm;
}

std::optional<double> AxisScale::differenceMm(double motorMm) const
{
    if (const auto position = positionMm()) {
        return *position - motorMm;
    }
    return std::nullopt;
}

void AxisScale::motorRebased(double motorMm)
{
    m_lastMotorMm = motorMm;
}

int AxisScale::settleCount() const
{
    return m_settleCount;
}

double AxisScale::countsPerMm() const
{
    return m_settings.countsPerMm;
}

double AxisScale::rawMm(int32_t counts) const
{
    return (m_settings.reversed ? -counts : counts) / m_settings.countsPerMm;
}

void AxisScale::align(int32_t counts, double motorMm)
{
    m_offsetMm = motorMm - rawMm(counts);
    m_aligned = true;
    m_settledCounts = counts;
    MGOLOG(
        fmt::format(
            "{} scale lined up with the motor at {:.4f} mm (reading {} counts)",
            m_settings.axisLabel,
            motorMm,
            counts));
}

} // namespace mgo
