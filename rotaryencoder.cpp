#include "rotaryencoder.h"

#include <algorithm>
#include <cmath>

namespace mgo {

void RotaryEncoder::staticCallback(int pin, int level, uint32_t tick, void* userData)
{
    RotaryEncoder* self = reinterpret_cast<RotaryEncoder*>(userData);
    self->callback(pin, level, tick);
}

void RotaryEncoder::callback(int pin, int level, uint32_t tick)
{
    if (pin == m_lastPin) {
        // Ignore repeated edges on the same pin. pigpio only reports genuine
        // level changes, so this isn't contact bounce; it's the encoder dithering
        // back and forth across one edge (e.g. a stationary spindle). As we only
        // count rising edges on pin A (below) and don't decode full quadrature,
        // letting these through would count every dither as a pulse.
        return;
    }
    m_lastPin = pin;

    // Record levels even while warming up, otherwise the direction check
    // below works from stale levels and gets the first pulse wrong
    if (pin == m_pinA) {
        m_levelA = level;
    } else {
        m_levelB = level;
    }

    if (m_warmingUp) {
        // We ignore the first few calls until we can
        // set the previous tick
        if (pin == m_pinA && level == 1) {
            m_lastTick = tick;
            m_warmingUp = false;
        }
        return;
    }

    // Check rotation

    if (pin == m_pinA && level == 1) {
        if (m_levelB) {
            m_direction = RotationDirection::normal;
        }
    } else if (pin == m_pinB && level == 1) {
        if (m_levelA) {
            m_direction = RotationDirection::reversed;
        }
    }

    // Note - we only count one pin's pulses, and measure from
    // rising edge to next rising edge
    if (pin == m_pinA && level == 1) {
        if (m_direction == RotationDirection::normal) {
            --m_pulseCount;
        } else {
            ++m_pulseCount;
        }
        // When physically setting up the rotary encoder, it's important
        // to set gearing such that there are a round number of pulses
        // per spindle revolution, otherwise we'll get a drift in the
        // apparent position of pulse zero.
        // Current gearing of the RE on my lathe is 35:100
        // so the rotary encoder does 0.35 revolutions per spindle revolution.
        // The RE has 2'000 pulses per rev, which means we get 700 pulses
        // per chuck revolution.
        bool hitZeroPulse = false;
        if (m_direction == RotationDirection::reversed) {
            if (m_pulseCount == static_cast<uint32_t>(m_pulsesPerSpindleRev)) {
                hitZeroPulse = true;
                m_pulseCount = 0;
            }
        } else {
            if (m_pulseCount > static_cast<uint32_t>(m_pulsesPerSpindleRev)) {
                hitZeroPulse = true;
                m_pulseCount = static_cast<uint32_t>(m_pulsesPerSpindleRev) - 1;
            }
        }
        if (hitZeroPulse) {
            // Only update the average once we have measured a full revolution.
            // The first zero after start-up can arrive after only a few pulses,
            // and averaging over a whole revolution's worth would give a
            // near-zero (i.e. wildly wrong) value
            if (m_pulsesInTotal >= static_cast<uint32_t>(m_pulsesPerSpindleRev)) {
                m_averageTickDelta = m_tickDiffTotal / static_cast<float>(m_pulsesInTotal);
            }
            m_tickDiffTotal = 0;
            m_pulsesInTotal = 0;
        }
        ++m_spindleSequence; // (odd: writing)
        m_spindlePulses += m_direction == RotationDirection::normal ? 1 : -1;
        m_spindlePulseTick = tick;
        m_spindlePulseInterval = tick - m_lastTick;
        ++m_spindleSequence;
        checkForLostPulses(tick - m_lastTick);
        m_tickDiffTotal += tick - m_lastTick; // don't need to worry about wrap
        ++m_pulsesInTotal;
        m_lastTick = tick;
    }
}

void RotaryEncoder::checkForLostPulses(uint32_t microsecondsSincePreviousPulse)
{
    // Pulses were lost if there was a long gap, but the pulse after it then
    // came at the usual rate. (A gap of a second or more is more likely to be
    // the spindle being stopped and started again)
    constexpr float gapFactor = 10.f;
    constexpr float resumedFactor = 2.f;
    constexpr uint32_t longestGap = 1'000'000;
    const float average = m_averageTickDelta;
    if (average <= 0.f) {
        return; // the speed isn't known yet
    }
    const float delta = static_cast<float>(microsecondsSincePreviousPulse);
    if (m_pendingGap != 0 && delta < average * resumedFactor) {
        m_lastLostPulseGap = m_pendingGap;
        ++m_lostPulseEvents;
    }
    m_pendingGap = 0;
    if (delta > average * gapFactor && microsecondsSincePreviousPulse < longestGap) {
        m_pendingGap = microsecondsSincePreviousPulse;
    }
}

uint32_t RotaryEncoder::microsecondsSinceLastPulse()
{
    // m_lastTick must be read *before* the current tick. The order of the two
    // reads in "getTick() - m_lastTick" is unspecified, and if the callback
    // thread updated m_lastTick in between, the (unsigned) difference would
    // be huge, i.e. the spindle would appear to have stopped
    const uint32_t lastTick = m_lastTick;
    const int32_t elapsed = static_cast<int32_t>(m_gpio.getTick() - lastTick);
    return elapsed > 0 ? static_cast<uint32_t>(elapsed) : 0;
}

float RotaryEncoder::getRpm()
{
    // Ticks are in microseconds
    if (warmingUp()) {
        return 0.f;
    }
    if (microsecondsSinceLastPulse() > 100'000) {
        return 0.f;
    }
    float rpm = 60'000'000.f / (m_averageTickDelta * m_pulsesPerSpindleRev);
    if (rpm > 5'000.f) {
        rpm = 0.f;
    }
    return rpm;
}

float RotaryEncoder::getPositionDegrees()
{
    // There will be latency in this as the pigpio thread which calls back to
    // the callback in the anonymous namespace above only does so approx once
    // per millisecond (it batches up the callbacks and calls us maybe thirty
    // times per batch). So this shouldn't be used in real time - use the
    // callbackAtPositionDegrees() function which extrapolates out based on
    // previous data.

    // However this is useful for manual chuck rotation.

    return 360.f * (static_cast<float>(m_pulseCount) / m_pulsesPerSpindleRev);
}

RotationDirection RotaryEncoder::getRotationDirection()
{
    return m_direction;
}

int64_t RotaryEncoder::getSpindlePulses() const
{
    return m_spindlePulses;
}

double RotaryEncoder::getSpindlePosition() const
{
    int64_t pulses;
    uint32_t tick;
    uint32_t interval;
    RotationDirection direction;
    for (;;) {
        const uint32_t sequence = m_spindleSequence;
        pulses = m_spindlePulses;
        tick = m_spindlePulseTick;
        interval = m_spindlePulseInterval;
        direction = m_direction;
        if (sequence % 2 == 0 && sequence == m_spindleSequence) {
            break;
        }
        // (The callback thread was part way through a pulse: try again)
    }
    if (interval == 0) {
        return static_cast<double>(pulses);
    }
    // (Read after the pulse's tick, so this isn't negative)
    const int32_t elapsed = static_cast<int32_t>(m_gpio.getTick() - tick);
    if (elapsed <= 0) {
        return static_cast<double>(pulses);
    }
    const uint32_t extrapolated = std::min(static_cast<uint32_t>(elapsed), MAX_EXTRAPOLATION_MICROSECONDS);
    const double fraction = static_cast<double>(extrapolated) / interval;
    return static_cast<double>(pulses)
        + (direction == RotationDirection::normal ? fraction : -fraction);
}

int RotaryEncoder::getPulsesPerSpindleRev() const
{
    return static_cast<int>(std::lround(m_pulsesPerSpindleRev));
}

} // end namespace
