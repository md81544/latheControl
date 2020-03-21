#pragma once

#include "curses.h"
#include "joystick.h"
#include "rotaryencoder.h"
#include "stepperControl/steppermotor.h"

#include <cmath>
#include <iosfwd>
#include <limits>
#include <memory>
#include <string>
#include <vector>


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

class Ui
{
    // As it's such a simple interface, we don't separate
    // "model" data from interface. This may be revisited
    // at a later stage.

public:
    explicit Ui( IGpio& gpio );
    // run() is the main loop. When this returns,
    // the application can quit.
    void run();
private:
    void processKeyPress();
    void processJoystick();
    void highlightCheck( std::size_t memoryLocation );
    void updateDisplay();
    IGpio& m_gpio;

    std::string m_status{ "stopped" };
    bool    m_moving{ false };
    bool    m_quit{ false };
    long    m_targetStep{ 0 };
    float   m_speed{ 100.f };
    int     m_oldSpeed{ 100 };
    bool    m_fastReturning{ false };
    std::vector<long> m_memory{ INF_RIGHT, INF_RIGHT, INF_RIGHT, INF_RIGHT };
    std::size_t m_currentMemory{ 0 };
    int m_keyPressed{ 0 };
    mgo::Curses::Window m_wnd;
    std::unique_ptr<mgo::StepperMotor> m_motor;
    std::unique_ptr<mgo::RotaryEncoder> m_rotaryEncoder;
    std::size_t m_threadPitchIndex{ 0 };
    bool    m_threadCuttingOn{ false };
    int     m_threadCutAdvanceCount{ 0 };
    mgo::Joystick m_joystick;
};

} // end namespace
