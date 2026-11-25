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
        m_previousCounts.reset();
        return;
    }
    if (m_noReadings) {
        MGOLOG(fmt::format("{} scale: readings received", m_settings.axisLabel));
        m_noReadings = false;
    }

    // Watching the move under way (before the motor's position is noted
    // below: a move starting now starts afresh)
    if (motorRunning && m_state != State::Moving) {
        m_stallMm.reset();
        m_moveMostBehindMm.reset();
        m_moveMostAheadMm.reset();
    }
    m_motorHistory.emplace_back(now, motorMm);
    // (Keeping one from before the lag began, so the average covers it all)
    while (m_motorHistory.size() > 1 && m_motorHistory[1].first <= now - m_settings.lag) {
        m_motorHistory.pop_front();
    }
    if (motorRunning && m_aligned) {
        checkWhileMoving(*counts, motorMm, motorSpeed, now);
    }
    m_previousCounts = *counts;
    m_previousAt = now;

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
        startSettling(*counts, now);
        m_motorStoppedAt = now;
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
        checkForMisreadWhileSettling(*counts);
        MGOLOG(
            fmt::format(
                "{} scale's reading changed at rest by {:+.4f} mm ({:+} counts), {:.1f} s after "
                "the axis stopped",
                m_settings.axisLabel,
                rawMm(*counts) - rawMm(m_settledCounts),
                m_settings.reversed ? -change : change,
                std::chrono::duration<double>(now - m_motorStoppedAt).count()));
        startSettling(*counts, now);
        m_changedAtRest = true;
        return;
    }

    // Settling
    if (*counts != m_lastCounts) {
        m_lastCounts = *counts;
        m_lastChangeAt = now;
    }
    if (std::abs(*counts - m_jumpCounts) >= REST_CHANGE_COUNTS) {
        m_jumpCounts = *counts;
        m_lastJumpAt = now;
    }
    if (m_changedAtRest) {
        checkForMisreadWhileSettling(*counts);
    }
    const bool steady = now - m_lastChangeAt >= m_settings.settleTime;
    if (!steady && now - m_stoppedAt < m_settings.maxSettleTime) {
        return;
    }
    m_state = State::Settled;
    ++m_settleCount;
    // (Flicker of a count, which can go on for ever on the mill, still counts
    // as steady; jumps don't)
    m_lastSettleSteady = steady || now - m_lastJumpAt >= m_settings.settleTime;
    // (Before the reading it settled at is noted, as both compare with the
    // one before)
    if (m_aligned && m_changedAtRest && m_settings.slipCounts > 0 && !correctSlips(*counts)) {
        checkForMisread(*counts, m_settings.faultMm);
    }
    if (m_lastSettleSteady) {
        m_steadyCounts = *counts;
    }
    m_settledCounts = *counts;
    m_lastMoveDirection = 0;
    if (m_moveStartMm && !m_changedAtRest && motorMm != *m_moveStartMm) {
        m_lastMoveDirection = motorMm > *m_moveStartMm ? 1 : -1;
    }
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
                                " after a move of {:+.4f} mm at up to {:.0f} mm/min{}",
                                motorMm - *m_moveStartMm,
                                m_moveTopSpeed,
                                m_moveMostBehindMm && !m_changedAtRest
                                    ? fmt::format(
                                          " (while moving, the reading was up to {:.3f} mm "
                                          "behind and {:.3f} mm ahead of where expected)",
                                          *m_moveMostBehindMm,
                                          *m_moveMostAheadMm)
                                    : "")
                          : "",
            scaleMm,
            motorMm,
            difference,
            difference * m_settings.countsPerMm));
    m_moveStartMm.reset();
}

void AxisScale::startSettling(int32_t counts, Clock::time_point now)
{
    m_state = State::Settling;
    m_stoppedAt = now;
    m_lastChangeAt = now;
    m_lastCounts = counts;
    m_jumpCounts = counts;
    m_lastJumpAt = now;
}

void AxisScale::checkForMisreadWhileSettling(int32_t counts)
{
    // (A slip comes in through the sender's smoothing a bit at a time, so it
    // can only be told from a misread once it has settled)
    const double slipsMm = m_settings.slipCounts > 0
        ? MAX_SLIPS_AT_ONCE * m_settings.slipCounts / m_settings.countsPerMm
        : 0.0;
    checkForMisread(counts, m_settings.faultMm + slipsMm);
}

void AxisScale::checkForMisread(int32_t counts, double limitMm)
{
    if (m_settings.faultMm <= 0.0 || !m_aligned || m_misreadJumpMm) {
        return;
    }
    const double jumpMm = rawMm(counts) - rawMm(m_settledCounts);
    if (std::abs(jumpMm) <= limitMm) {
        return;
    }
    m_misreadJumpMm = jumpMm;
    MGOLOG(
        fmt::format(
            "*** Warning *** {} scale's reading jumped by {:+.4f} mm at rest, more than {:.4f}: "
            "it's misreading",
            m_settings.axisLabel,
            jumpMm,
            limitMm));
}

bool AxisScale::correctSlips(int32_t counts)
{
    // (Only from a steady reading to a steady reading: part way through a
    // slip, or while the reading jumps about, it isn't one yet)
    if (!m_lastSettleSteady) {
        return false;
    }
    const int32_t change = counts - m_steadyCounts;
    const int32_t slips
        = static_cast<int32_t>(std::lround(static_cast<double>(change) / m_settings.slipCounts));
    if (slips == 0 || std::abs(slips) > MAX_SLIPS_AT_ONCE
        || std::abs(change - slips * m_settings.slipCounts) > SLIP_TOLERANCE_COUNTS) {
        return false;
    }
    // The position stays where it was, give or take the flicker
    const double slipMm = rawMm(slips * m_settings.slipCounts);
    m_offsetMm -= slipMm;
    m_slipsCorrected += std::abs(slips);
    MGOLOG(
        fmt::format(
            "*** Warning *** {} scale slipped by {} period{} of {} counts ({:+.4f} mm) at rest, "
            "most likely interference: taken off ({} so far)",
            m_settings.axisLabel,
            slips,
            std::abs(slips) == 1 ? "" : "s",
            m_settings.slipCounts,
            slipMm,
            m_slipsCorrected));
    return true;
}

void AxisScale::checkWhileMoving(
    int32_t counts,
    double motorMm,
    double motorSpeed,
    Clock::time_point now)
{
    const double scaleMm = rawMm(counts) + m_offsetMm;
    // A change in the reading faster than the table could move: a misread.
    // (A sender which averages its readings smooths a small one out, but not
    // the jumps of hundreds of mm seen on the mill)
    if (m_settings.movingFaultMm > 0.0 && !m_misreadJumpMm && m_previousCounts) {
        const double change = rawMm(counts) - rawMm(*m_previousCounts);
        const double seconds = std::chrono::duration<double>(now - m_previousAt).count();
        const double speed = std::max(std::abs(motorSpeed), m_moveTopSpeed);
        if (std::abs(change) > speed / 60.0 * seconds + m_settings.movingFaultMm) {
            m_misreadJumpMm = change;
            MGOLOG(
                fmt::format(
                    "*** Warning *** {} scale's reading jumped by {:+.4f} mm in {:.3f} s while "
                    "moving, faster than the table can at {:.0f} mm/min: it's misreading",
                    m_settings.axisLabel,
                    change,
                    seconds,
                    speed));
            return;
        }
    }
    // (Not while the motor's position isn't changing, e.g. taking up the
    // backlash, as there's no direction to be behind in)
    if (!m_lastMotorMm || motorMm == *m_lastMotorMm) {
        return;
    }
    const double direction = motorMm > *m_lastMotorMm ? 1.0 : -1.0;
    const double behind = (expectedReadingMm(motorMm, now) - scaleMm) * direction;
    m_moveMostBehindMm = std::max(m_moveMostBehindMm.value_or(0.0), behind);
    m_moveMostAheadMm = std::max(m_moveMostAheadMm.value_or(0.0), -behind);
    if (m_settings.stallMm > 0.0 && !m_stallMm && behind > m_settings.stallMm) {
        m_stallMm = behind;
        MGOLOG(
            fmt::format(
                "*** Warning *** {} is {:.4f} mm behind the motor while moving (more than {:.4f}, "
                "allowing for the scale's lag): it has stalled. Motor {:.4f} mm, scale {:.4f} mm",
                m_settings.axisLabel,
                behind,
                m_settings.stallMm,
                motorMm,
                scaleMm));
    }
    // The table can't get ahead of the motor by more than its slack
    if (m_settings.movingFaultMm > 0.0 && !m_misreadJumpMm && -behind > m_settings.movingFaultMm) {
        m_misreadJumpMm = -behind * direction;
        MGOLOG(
            fmt::format(
                "*** Warning *** {} scale's reading is {:.4f} mm ahead of the motor while moving "
                "(more than {:.4f}, allowing for its lag), which the table can't be: it's "
                "misreading. Motor {:.4f} mm, scale {:.4f} mm",
                m_settings.axisLabel,
                -behind,
                m_settings.movingFaultMm,
                motorMm,
                scaleMm));
    }
}

double AxisScale::expectedReadingMm(double motorMm, Clock::time_point now) const
{
    const double lagSeconds = std::chrono::duration<double>(m_settings.lag).count();
    if (lagSeconds <= 0.0 || m_motorHistory.empty()) {
        return motorMm;
    }
    const auto seconds = [](Clock::duration d) { return std::chrono::duration<double>(d).count(); };
    const Clock::time_point start = now - m_settings.lag;
    double total = 0.0;
    // (Before the history begins, it was where it first was)
    if (m_motorHistory.front().first > start) {
        total += m_motorHistory.front().second * seconds(m_motorHistory.front().first - start);
    }
    // Between updates, moving steadily from one position to the next
    for (std::size_t n = 0; n + 1 < m_motorHistory.size(); ++n) {
        auto [t0, p0] = m_motorHistory[n];
        const auto [t1, p1] = m_motorHistory[n + 1];
        if (t1 <= start) {
            continue;
        }
        if (t0 < start) {
            p0 += (p1 - p0) * seconds(start - t0) / seconds(t1 - t0);
            t0 = start;
        }
        total += (p0 + p1) / 2.0 * seconds(t1 - t0);
    }
    // (The history ends now, with the motor where it is)
    if (m_motorHistory.back().first < now) {
        total += motorMm * seconds(now - m_motorHistory.back().first);
    }
    return total / lagSeconds;
}

void AxisScale::realign()
{
    m_aligned = false;
    m_misreadJumpMm.reset();
    // (The motor's position changes without it moving)
    m_motorHistory.clear();
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
    // (Where the motor was is where it was on the new footing too)
    if (m_lastMotorMm) {
        const double shift = motorMm - *m_lastMotorMm;
        for (auto& [time, position] : m_motorHistory) {
            position += shift;
        }
    }
    m_lastMotorMm = motorMm;
}

int AxisScale::settleCount() const
{
    return m_settleCount;
}

bool AxisScale::isSteady() const
{
    return m_aligned && m_state == State::Settled && m_lastSettleSteady;
}

bool AxisScale::isSettled() const
{
    return m_state == State::Settled;
}

std::optional<double> AxisScale::misreadJumpMm() const
{
    return m_misreadJumpMm;
}

std::optional<double> AxisScale::stallMm() const
{
    return m_stallMm;
}

int AxisScale::lastMoveDirection() const
{
    return m_lastMoveDirection;
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
    m_misreadJumpMm.reset();
    m_settledCounts = counts;
    m_steadyCounts = counts;
    MGOLOG(
        fmt::format(
            "{} scale lined up with the motor at {:.4f} mm (reading {} counts)",
            m_settings.axisLabel,
            motorMm,
            counts));
}

} // namespace mgo
