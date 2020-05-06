#pragma once

#include "rotaryencoder.h"
#include "stepperControl/steppermotor.h"

#include <cmath>
#include <limits>
#include <memory>
#include <vector>

// "Model", i.e. program state data

namespace mgo
{

class IGpio;

const int INF_RIGHT = std::numeric_limits<int>::min();
const int INF_LEFT  = std::numeric_limits<int>::max();

// TODO these need to be in a config file
const float MAX_MOTOR_SPEED = 700.f;
const float INFEED = 0.05f; // mm
// This should be constexpr, but std::tan (and other maths
// functions) aren't constexpr and have side effects -- they
// set errno.
const float SIDEFEED = INFEED * std::tan( 0.5148721f ); // 29.5° in radians

struct Model
{
    Model( IGpio& gpio ) : m_gpio(gpio) {}
    IGpio& m_gpio;
    std::string m_status{ "stopped" };
    bool    m_zMoving{ false };
    bool    m_quit{ false };
    long    m_targetStep{ 0 };
    float   m_zSpeed{ 100.f };
    float   m_xSpeed{ 60.f };
    float   m_oldZSpeed{ 100.f };
    bool    m_fastReturning{ false };
    std::vector<long> m_memory{ INF_RIGHT, INF_RIGHT, INF_RIGHT, INF_RIGHT };
    std::size_t m_currentMemory{ 0 };
    int m_keyPressed{ 0 };
    // Lead screw:
    std::unique_ptr<mgo::StepperMotor> m_zAxisMotor;
    // Cross slide:
    std::unique_ptr<mgo::StepperMotor> m_xAxisMotor;
    std::unique_ptr<mgo::RotaryEncoder> m_rotaryEncoder;
    std::size_t m_threadPitchIndex{ 0 };
    bool    m_threadCuttingOn{ false };
    int     m_threadCutAdvanceCount{ 0 };
};

} // end namespace
