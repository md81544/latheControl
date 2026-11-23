#pragma once
// This class is used to read and respond to a quadrature linear
// scale, used to determine the position of the tool on an axis
// (currently just the lathe's Z-axis)

#include "igpio.h"
#include "scale.h"

#include <atomic>
#include <cassert>
#include <cstdint>

namespace mgo {

class LinearScale final : public IScale {
public:
    // glitchMicroseconds: see IGpio::setGlitchFilter() (0 for none)
    LinearScale(IGpio& gpio, int pinA, int pinB, int stepsPerMm, unsigned glitchMicroseconds = 0)
        : m_gpio(gpio)
        , m_pinA(pinA)
        , m_pinB(pinB)
        , m_stepsPerMm(stepsPerMm)
        , m_glitchMicroseconds(glitchMicroseconds)
    {
        assert(pinA != pinB);
        if (m_glitchMicroseconds > 0) {
            m_gpio.setGlitchFilter(m_pinA, m_glitchMicroseconds);
            m_gpio.setGlitchFilter(m_pinB, m_glitchMicroseconds);
        }
        m_gpio.setLinearScaleAxis1Callback(m_pinA, m_pinB, staticCallback, this);
    }

    ~LinearScale() override
    {
        // The GPIO library holds a pointer to us, so must stop calling back
        // before we are destroyed
        m_gpio.clearLinearScaleAxis1Callback();
        if (m_glitchMicroseconds > 0) {
            m_gpio.setGlitchFilter(m_pinA, 0);
            m_gpio.setGlitchFilter(m_pinB, 0);
        }
    }

    LinearScale(const LinearScale&) = delete;
    LinearScale& operator=(const LinearScale&) = delete;

    static void staticCallback(int pin, int level, uint32_t tick, void* userData);

    void callback(int pin, int level, uint32_t tick);

    // Always has a reading, as it counts every edge
    std::optional<int32_t> readCounts() const override
    {
        return m_stepCount.load();
    }

    uint32_t noiseEvents() const override
    {
        return m_noiseEvents.load();
    }

    float getPositionInMm();

    // Sets the current step position as zero
    void setZeroMm();

private:
    IGpio& m_gpio;
    int m_pinA;
    int m_pinB;
    int m_stepsPerMm { 200 };
    unsigned m_glitchMicroseconds { 0 };
    // Only accessed from the pigpio callback thread:
    int m_levelA { 0 };
    int m_levelB { 0 };
    int m_previousPhase { 0 }; // 0 = unknown (no edges seen yet)
    // The last change counted, so that it can be undone if the other pin
    // changed in the same sample (see callback()). -1: none to undo
    int m_lastPin { -1 };
    uint32_t m_lastTick { 0 };
    int m_lastDelta { 0 };
    // Written by the callback thread, read by the UI thread:
    std::atomic<int32_t> m_stepCount { 0 };
    std::atomic<uint32_t> m_noiseEvents { 0 };
    std::atomic<int32_t> m_zeroPosition { 0 }; // the step count which is counted as 0.00 mm
};

} // end namespace
