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
const int INF_OUT   = std::numeric_limits<int>::min();
const int INF_LEFT  = std::numeric_limits<int>::max();
const int INF_IN    = std::numeric_limits<int>::max();

constexpr double DEG_TO_RAD = 3.14159265359 / 180.0;

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
    Taper,
    XRetractSetup,
    XPositionSetup,
    ZPositionSetup
};

// "Key Modes" allow for two-key actions, a bit like vim.
// Initial use case is to allow for actions to only apply to
// Z or X axis - for example, "xz" means zero X only. "xm" means
// memorise X only. "z<Enter>" means go to the stored Z value only.
// Pressing "m" or "<Enter>" will apply to both axes. There won't
// be a single keypress for zeroing both axes at once - use "zz" and "xz".
enum class KeyMode
{
    None,
    Axis1,
    Axis2,
    Function
};

enum class XRetractionDirection
{
    Outwards,   // towards operator
    Inwards     // away from operator (when boring)
};

enum class ZDirection
{
    Left,
    Right
};

struct Model
{
    Model( IGpio& gpio ) : m_gpio(gpio) {}
    IGpio& m_gpio;
    // Lead screw:
    std::unique_ptr<mgo::StepperMotor> m_axis1Motor;
    // Cross slide:
    std::unique_ptr<mgo::StepperMotor> m_axis2Motor;
    std::unique_ptr<mgo::RotaryEncoder> m_rotaryEncoder;
    std::vector<long> m_axis1Memory{ INF_RIGHT, INF_RIGHT, INF_RIGHT, INF_RIGHT };
    std::vector<long> m_xMemory{ INF_RIGHT, INF_RIGHT, INF_RIGHT, INF_RIGHT };
    std::size_t m_currentMemory{ 0 };
    std::size_t m_threadPitchIndex{ 0 };
    std::string m_generalStatus{ "Press F1 for help" };
    std::string m_axis1Status{ "stopped" };
    std::string m_axis2Status{ "stopped" };
    std::string m_warning;
    std::string m_input; // general-purpose string for user-entered data
    bool        m_quit{ false };
    bool        m_shutdown{ false };
    float       m_previousZSpeed{ 40.f };
    bool        m_fastReturning{ false };
    int         m_keyPressed{ 0 };
    double      m_taperAngle{ 0.0 };
    float       m_taperPreviousXSpeed{ 40.f };
    std::unique_ptr<mgo::ConfigReader> m_config;
    // Stores the current function displayed on the screen:
    Mode        m_currentDisplayMode{ Mode::None };
    // Stores current function, i.e. whether tapering or threading is on
    // we use the same enum class as "mode"
    Mode        m_enabledFunction{ Mode::None };
    KeyMode     m_keyMode{ KeyMode::None };

    // Used to store position to return to after retract:
    long        m_xOldPosition;
    bool        m_axis2Retracted{ false };
    float       m_previousXSpeed{ 40.f };
    bool        m_fastRetracting{ false };

    bool        m_zWasRunning{ false };
    bool        m_xWasRunning{ false };
    bool        m_spindleWasRunning{ false };

    XRetractionDirection    m_xRetractionDirection;
};

} // end namespace
