#include "manualaxis.h"

#include "log.h"

#include <fmt/format.h>

#include <cstdlib>

namespace mgo {

ManualAxis::ManualAxis(std::unique_ptr<IScale> scale, Settings settings)
    : m_scale(std::move(scale))
    , m_settings(std::move(settings))
{
    // (Starting at zero, if there's a reading already)
    update();
}

void ManualAxis::update(Clock::time_point now)
{
    const std::optional<int32_t> counts = m_scale->readCounts();
    // (A change of a count is flicker, which can go on for ever on the mill)
    if (!counts) {
        m_settleCounts.reset();
    } else if (!m_settleCounts || std::abs(*counts - *m_settleCounts) > 1) {
        m_settleCounts = counts;
        m_settleSince = now;
    }
    const bool hasReading = counts.has_value();
    if (hasReading == m_hadReading) {
        return;
    }
    m_hadReading = hasReading;
    if (!hasReading) {
        MGOLOG(fmt::format("{} scale: no readings; its zero is lost", m_settings.label));
        // (Anything kept from the old zero is no use now)
        ++m_originChanges;
        return;
    }
    // (After a loss, the sender's count may have started again, so the old
    // offset could put the zero anywhere)
    MGOLOG(fmt::format("{} scale: readings received; starting at zero", m_settings.label));
    zero();
}

double ManualAxis::countMm() const
{
    return 1.0 / m_settings.countsPerMm;
}

bool ManualAxis::isSettled(Clock::time_point now) const
{
    return m_settleCounts && hasReading() && now - m_settleSince >= m_settings.settleTime;
}

int ManualAxis::originChanges() const
{
    return m_originChanges;
}

const std::string& ManualAxis::label() const
{
    return m_settings.label;
}

std::optional<bool> ManualAxis::clockwiseIncreases() const
{
    return m_settings.clockwiseIncreases;
}

const std::string& ManualAxis::handleName() const
{
    return m_settings.handleName;
}

bool ManualAxis::hasReading() const
{
    return m_scale->readCounts().has_value();
}

std::optional<double> ManualAxis::positionMm() const
{
    const std::optional<int32_t> counts = m_scale->readCounts();
    if (!counts) {
        return std::nullopt;
    }
    return rawMm(*counts) + m_offsetMm;
}

bool ManualAxis::zero()
{
    return setPositionMm(0.0);
}

bool ManualAxis::setPositionMm(double mm)
{
    const std::optional<int32_t> counts = m_scale->readCounts();
    if (!counts) {
        return false;
    }
    const double was = rawMm(*counts) + m_offsetMm;
    m_offsetMm = mm - rawMm(*counts);
    ++m_originChanges;
    MGOLOG(
        fmt::format(
            "{} position set to {:.4f} (was {:.4f}; reading {} counts)",
            m_settings.label,
            mm,
            was,
            *counts));
    return true;
}

bool ManualAxis::half()
{
    const std::optional<double> position = positionMm();
    if (!position) {
        return false;
    }
    return setPositionMm(*position / 2.0);
}

double ManualAxis::rawMm(int32_t counts) const
{
    return (m_settings.reversed ? -counts : counts) / m_settings.countsPerMm;
}

} // namespace mgo
