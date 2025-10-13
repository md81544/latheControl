#include "rotaryencoder.h"

namespace mgo {

void RotaryEncoder::staticCallback(int pin, int level, uint32_t tick, void* userData)
{
    RotaryEncoder* self = reinterpret_cast<RotaryEncoder*>(userData);
    self->callback(pin, level, tick);
}

void RotaryEncoder::callback(int pin, int level, uint32_t tick)
{
    if (pin == m_lastPin) {
        // debounce
        return;
    }
    m_lastPin = pin;

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
    if (pin == m_pinA) {
        m_levelA = level;
    } else {
        m_levelB = level;
    }

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
            m_averageTickDelta = m_tickDiffTotal / static_cast<float>(m_pulsesPerSpindleRev);
            m_tickDiffTotal = 0;
            // We remember what the tick was at the last zero degrees position
            // (we arbitrarily call the start position zero) so we can extrapolate
            // out to the next one for accurate starting when waiting to cut a
            // thread. Owing to the latency on the callbacks, it's not sufficient
            // to simply wait for the next zero-degree tick. With the 1 ms latency,
            // this could result in an inaccuracy of up to 6° at 1,000 rpm.
            m_lastZeroDegreesTick = tick;
        }
        m_tickDiffTotal += tick - m_lastTick; // don't need to worry about wrap
        m_lastTick = tick;
    }
}

float RotaryEncoder::getRpm()
{
    // Ticks are in microseconds
    if (warmingUp()) {
        return 0.f;
    }
    if (m_gpio.getTick() - m_lastTick > 100'000) {
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

void RotaryEncoder::callbackAtZeroDegrees(std::function<void()> cb)
{
    // We just need to start threading operations at a
    // repeatable rotational position each time, so we
    // arbitrarily choose zero.
    // Because there is a latency on the callback (the pigpio
    // library batches up the callbacks), we interpolate here
    // for better accuracy.
    // Note: ramping should be turned off for threading operations
    if (m_warmingUp) {
        return; // spindle not running?
    }
    while (m_lastZeroDegreesTick == 0)
        ; // spin if the last pos isn't set yet
    uint32_t timeForOneRevolution = m_averageTickDelta * m_pulsesPerSpindleRev;
    uint32_t targetTick
        = m_lastZeroDegreesTick + (timeForOneRevolution - m_advanceValueMicroseconds);
    while (m_gpio.getTick() > targetTick) {
        targetTick += timeForOneRevolution;
    }
    // Now spin until we get to the right time
    while (m_gpio.getTick() < targetTick)
        ;
    cb();
}
} // end namespace
