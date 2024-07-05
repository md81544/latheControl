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
    int32_t m_stepCount { 0 };
    int m_previousPhase { 0 };
};

} // end namespace
