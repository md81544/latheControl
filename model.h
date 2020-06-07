#pragma once

#include "configreader.h"
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

constexpr float INFEED = 0.05f; // mm
// The large number below is tan 29.5°
// (Cannot use std::tan in constexpr owing to side effects)
constexpr float SIDEFEED = INFEED * 0.5657727781877700776025887010584;

// "Modes" allow for special behaviour (like threading and tapering)
// or completely modal-like input / display (like help, or setup)
enum class Mode
{
    None,
    Help,
    Setup,
    Threading,
    Taper
};

struct Model
{
    Model( IGpio& gpio ) : m_gpio(gpio) {}
    IGpio& m_gpio;
    // Lead screw:
    std::unique_ptr<mgo::StepperMotor> m_zAxisMotor;
    // Cross slide:
    std::unique_ptr<mgo::StepperMotor> m_xAxisMotor;
    std::unique_ptr<mgo::RotaryEncoder> m_rotaryEncoder;
    std::vector<long> m_memory{ INF_RIGHT, INF_RIGHT, INF_RIGHT, INF_RIGHT };
    std::size_t m_currentMemory{ 0 };
    std::size_t m_threadPitchIndex{ 0 };
    std::string m_status{ "stopped" };
    std::string m_warning;
    std::string m_input; // general-purpose string for user-entered data
    bool        m_quit{ false };
    float       m_previousZSpeed{ 40.f };
    bool        m_fastReturning{ false };
    int         m_keyPressed{ 0 };
    int         m_threadCutAdvanceCount{ 0 };
    float       m_taperAngle{ 0.f };
    bool        m_taperingOn{ false };
    bool        m_useSfml{ true };
    Mode        m_currentMode{ Mode::None };
    std::unique_ptr<mgo::ConfigReader> m_config;
};

} // end namespace
