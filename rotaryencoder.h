#pragma once
// This class is used to read and respond to a rotary
// encoder which measures the lathe's spindle rotation.

#include "log.h"
#include "stepperControl/igpio.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <ostream>

namespace mgo {

enum class RotationDirection {
    normal,
    reversed
};

class RotaryEncoder {
public:
    RotaryEncoder(
        IGpio& gpio,
        int pinA,
        int pinB,
        int pulsesPerRev, // of the RE, not spindle
        float gearing)
        : m_gpio(gpio)
        , m_pinA(pinA)
        , m_pinB(pinB)
        , m_pulsesPerRev(pulsesPerRev)
        , m_gearing(gearing)
    {
        m_pulsesPerSpindleRev = m_pulsesPerRev * m_gearing;
        m_gpio.setRotaryEncoderCallback(m_pinA, m_pinB, staticCallback, this);
    }

    ~RotaryEncoder()
    {
        // The GPIO library holds a pointer to us, so must stop calling back
        // before we are destroyed
        m_gpio.clearRotaryEncoderCallback();
    }

    RotaryEncoder(const RotaryEncoder&) = delete;
    RotaryEncoder& operator=(const RotaryEncoder&) = delete;

    static void staticCallback(int pin, int level, uint32_t tick, void* userData);

    void callback(int pin, int level, uint32_t tick);

    float getRpm();
    // Time since the last pulse was seen (0 if none yet)
    uint32_t microsecondsSinceLastPulse();
    float getPositionDegrees();
    RotationDirection getRotationDirection();

    void callbackAtZeroDegrees(std::function<void()> cb);

    void setAdvanceValueMicroseconds(float value)
    {
        m_advanceValueMicroseconds = value;
    }

    bool warmingUp()
    {
        return m_warmingUp;
    }

private:
    IGpio& m_gpio;
    int m_pinA;
    int m_pinB;
    // These are only accessed from the pigpio callback thread:
    int m_levelA { 0 };
    int m_levelB { 0 };
    int m_lastPin { 0 };
    uint32_t m_tickDiffTotal { 0 };
    uint32_t m_pulsesInTotal { 0 }; // number of pulses in m_tickDiffTotal
    // Set at construction only:
    int m_pulsesPerRev; // of RE
    float m_pulsesPerSpindleRev;
    float m_gearing;
    // These are written by the callback thread and read by the UI thread:
    std::atomic<bool> m_warmingUp { true };
    std::atomic<uint32_t> m_lastTick { 0 };
    std::atomic<uint32_t> m_lastZeroDegreesTick { 0 };
    std::atomic<uint32_t> m_pulseCount { 0 };
    std::atomic<float> m_averageTickDelta { 0.f };
    std::atomic<RotationDirection> m_direction { RotationDirection::normal };
    std::atomic<float> m_advanceValueMicroseconds { 0.f };
};

} // end namespace
