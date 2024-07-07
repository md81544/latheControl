#pragma once
// This class is used to read and respond to a linear
// scale, used to determine the position of the tool on an axis
// (currently just the Z-axis)

#include "log.h"
#include "stepperControl/igpio.h"

#include <atomic>
#include <cassert>
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
        assert(pinA != pinB);
        m_gpio.setLinearScaleAxis1Callback(m_pinA, m_pinB, staticCallback, this);
    }

    static void staticCallback(int pin, int level, uint32_t tick, void* userData);

    void callback(int pin, int level, uint32_t tick);

    float getPositionInMm();

    // Sets the current step position as zero
    void setZeroMm();

private:
    IGpio& m_gpio;
    int m_pinA;
    int m_pinB;
    int m_levelA { 0 };
    int m_levelB { 0 };
    int m_lastPin { 0 };
    int m_stepsPerMm { 200 };
    int32_t m_stepCount { 0 };
    int m_previousPhase { 0 };
    int32_t m_zeroPosition { 0 }; // the step count which is counted as 0.00 mm
};

} // end namespace
