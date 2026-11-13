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
    LinearScale(IGpio& gpio, int pinA, int pinB, int stepsPerMm)
        : m_gpio(gpio)
        , m_pinA(pinA)
        , m_pinB(pinB)
        , m_stepsPerMm(stepsPerMm)
    {
        assert(pinA != pinB);
        m_gpio.setLinearScaleAxis1Callback(m_pinA, m_pinB, staticCallback, this);
    }

    ~LinearScale() override
    {
        // The GPIO library holds a pointer to us, so must stop calling back
        // before we are destroyed
        m_gpio.clearLinearScaleAxis1Callback();
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

    float getPositionInMm();

    // Sets the current step position as zero
    void setZeroMm();

private:
    IGpio& m_gpio;
    int m_pinA;
    int m_pinB;
    int m_stepsPerMm { 200 };
    // Only accessed from the pigpio callback thread:
    int m_levelA { 0 };
    int m_levelB { 0 };
    int m_previousPhase { 0 }; // 0 = unknown (no edges seen yet)
    // Written by the callback thread, read by the UI thread:
    std::atomic<int32_t> m_stepCount { 0 };
    std::atomic<int32_t> m_zeroPosition { 0 }; // the step count which is counted as 0.00 mm
};

} // end namespace
