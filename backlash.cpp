#include "backlash.h"

#include <algorithm>
#include <cmath>

namespace mgo {

BacklashMeasurement::BacklashMeasurement(
    double startMm,
    double distanceMm,
    int firstDirection,
    double compensationMm)
    : m_compensationMm(compensationMm)
{
    const double away = startMm + firstDirection * distanceMm;
    for (int n = 0; n < MOVES; ++n) {
        m_targets.push_back(n % 2 == 0 ? away : startMm);
    }
}

std::optional<double> BacklashMeasurement::nextTarget() const
{
    if (finished()) {
        return std::nullopt;
    }
    return m_targets.at(m_readings.size());
}

void BacklashMeasurement::record(double motorMm, double scaleMm)
{
    if (!finished()) {
        m_readings.emplace_back(motorMm, scaleMm);
    }
}

int BacklashMeasurement::movesDone() const
{
    return static_cast<int>(m_readings.size());
}

bool BacklashMeasurement::finished() const
{
    return m_readings.size() == m_targets.size();
}

BacklashMeasurement::Result BacklashMeasurement::result() const
{
    Result result;
    result.compensationMm = m_compensationMm;
    double positiveTotal = 0.0;
    double negativeTotal = 0.0;
    int positiveCount = 0;
    int negativeCount = 0;
    double smallest = 0.0;
    double largest = 0.0;
    // (Not the first move, which may not have been a reversal)
    for (std::size_t n = 1; n < m_readings.size(); ++n) {
        const double motorMove = m_readings[n].first - m_readings[n - 1].first;
        const double scaleMove = m_readings[n].second - m_readings[n - 1].second;
        const double shortfall = std::abs(motorMove) - std::abs(scaleMove);
        if (motorMove > 0.0) {
            positiveTotal += shortfall;
            ++positiveCount;
        } else {
            negativeTotal += shortfall;
            ++negativeCount;
        }
        if (n == 1) {
            smallest = largest = shortfall;
        } else {
            smallest = std::min(smallest, shortfall);
            largest = std::max(largest, shortfall);
        }
    }
    if (positiveCount > 0) {
        result.positiveShortfallMm = positiveTotal / positiveCount;
    }
    if (negativeCount > 0) {
        result.negativeShortfallMm = negativeTotal / negativeCount;
    }
    result.spreadMm = largest - smallest;
    return result;
}

} // namespace mgo
