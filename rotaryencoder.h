#pragma once
// This class is used to read and respond to a rotary
// encoder which measures the lathe's spindle rotation.

#include "log.h"
#include "stepperControl/igpio.h"

#include <atomic>
#include <cstdint>
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

    // Pulses can be lost if pigpio's callback thread is held up for longer than
    // its sample buffer lasts (by default 120ms). We see that as a sudden gap
    // in the pulses, after which they carry on at the same rate as before (a
    // real change of spindle speed is far more gradual). The spindle position
    // (as used for threading) is then wrong.
    // The number of times this has happened:
    uint32_t getLostPulseEvents() const
    {
        return m_lostPulseEvents;
    }
    // ...and the length of the most recent gap
    uint32_t getLastLostPulseGapMicroseconds() const
    {
        return m_lastLostPulseGap;
    }
    float getPositionDegrees();
    RotationDirection getRotationDirection();

    // The spindle's position in encoder pulses, counted from start-up: up as
    // it turns in the normal direction, down when reversed. Never wraps. Every
    // whole multiple of getPulsesPerSpindleRev() is the same angle. Lost
    // pulses (see getLostPulseEvents()) leave it wrong.
    int64_t getSpindlePulses() const;
    // The same, but continuing past the last pulse at the current rate, by
    // the time since it (up to MAX_EXTRAPOLATION_MICROSECONDS). Pulses reach us
    // in batches, about every millisecond, so this keeps the position up to
    // date in between. Can be called from any thread
    double getSpindlePosition() const;
    static constexpr uint32_t MAX_EXTRAPOLATION_MICROSECONDS = 2'000;
    int getPulsesPerSpindleRev() const;

    bool warmingUp()
    {
        return m_warmingUp;
    }

private:
    // Called from callback()
    void checkForLostPulses(uint32_t microsecondsSincePreviousPulse);

    IGpio& m_gpio;
    int m_pinA;
    int m_pinB;
    // These are only accessed from the pigpio callback thread:
    int m_levelA { 0 };
    int m_levelB { 0 };
    int m_lastPin { 0 };
    uint32_t m_tickDiffTotal { 0 };
    uint32_t m_pulsesInTotal { 0 }; // number of pulses in m_tickDiffTotal
    // A suspiciously long gap before the latest pulse (see getLostPulseEvents())
    uint32_t m_pendingGap { 0 }; // 0 = none
    // Set at construction only:
    int m_pulsesPerRev; // of RE
    float m_pulsesPerSpindleRev;
    float m_gearing;
    // These are written by the callback thread and read by the UI thread:
    std::atomic<bool> m_warmingUp { true };
    std::atomic<uint32_t> m_lastTick { 0 };
    std::atomic<uint32_t> m_pulseCount { 0 };
    std::atomic<float> m_averageTickDelta { 0.f };
    std::atomic<RotationDirection> m_direction { RotationDirection::normal };
    // See getSpindlePulses(). These three are only consistent with each other
    // while m_spindleSequence is even and unchanged (it's odd while they're
    // being written)
    std::atomic<int64_t> m_spindlePulses { 0 };
    std::atomic<uint32_t> m_spindlePulseTick { 0 };
    std::atomic<uint32_t> m_spindlePulseInterval { 0 }; // (µs; 0 = not known)
    std::atomic<uint32_t> m_spindleSequence { 0 };
    std::atomic<uint32_t> m_lostPulseEvents { 0 };
    std::atomic<uint32_t> m_lastLostPulseGap { 0 };
};

} // end namespace
