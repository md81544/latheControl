#include "manualaxis.h"

#include "log.h"

#include <fmt/format.h>

namespace mgo {

ManualAxis::ManualAxis(std::unique_ptr<IScale> scale, Settings settings)
    : m_scale(std::move(scale))
    , m_settings(std::move(settings))
{
    // (Starting at zero, if there's a reading already)
    update();
}

void ManualAxis::update()
{
    const bool hasReading = m_scale->readCounts().has_value();
    if (hasReading == m_hadReading) {
        return;
    }
    m_hadReading = hasReading;
    if (!hasReading) {
        MGOLOG(fmt::format("{} scale: no readings; its zero is lost", m_settings.label));
        return;
    }
    // (After a loss, the sender's count may have started again, so the old
    // offset could put the zero anywhere)
    MGOLOG(fmt::format("{} scale: readings received; starting at zero", m_settings.label));
    zero();
}

const std::string& ManualAxis::label() const
{
    return m_settings.label;
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
