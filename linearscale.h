#pragma once
// This class is used to read and respond to a linear
// scale, used to determine the position of the tool on an axis
// (currently just the Z-axis)

#include "log.h"
#include "stepperControl/igpio.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <ostream>

namespace mgo {

class LinearScale {
public:
    LinearScale(IGpio& gpio, int pinA, int pinB, int stepsPerMm)
        : m_gpio(gpio)
        , m_pinA(pinA)
        , m_pinB(pinB)
        , m_stepsPerMm(stepsPerMm)
    {
        m_gpio.setEncoderCallback(m_pinA, m_pinB, staticCallback, this);
        // TODO remove these when these member variables are actually being used
        // (or deleted, as appropriate)
        (void)m_stepsPerMm;
        (void)m_levelA;
        (void)m_levelB;
        (void)m_lastTick;
    }

    static void staticCallback(int pin, int level, uint32_t tick, void* userData);

    void callback(int pin, int level, uint32_t tick);

    float getPositionInMm();

private:
    IGpio& m_gpio;
    int m_pinA;
    int m_pinB;
    int m_levelA;
    int m_levelB;
    int m_lastPin { 0 };
    int m_stepsPerMm { 200 };
    uint32_t m_lastTick;
    int32_t m_stepCount { 0 };
};

} // end namespace
