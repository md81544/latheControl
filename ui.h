#pragma once

#include "curses.h"
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
    // Currently (as it started off so simply) we mix model with view
    // in this class. Now complexity is growing this will soon be split
    // into a proper view and model classes so the UI can very independently
    // (intention soon is to have an SFML-based graphical UI)

public:
    explicit Ui( IGpio& gpio );
    // run() is the main loop. When this returns,
    // the application can quit.
    void run();
private:
    void processKeyPress();
    void highlightCheck( std::size_t memoryLocation );
    void updateDisplay();
    IGpio& m_gpio;

    std::string m_status{ "stopped" };
    bool    m_zMoving{ false };
    bool    m_xMoving{ false };
    bool    m_quit{ false };
    long    m_targetStep{ 0 };
    float   m_zSpeed{ 100.f };
    float   m_xSpeed{ 60.f };
    float   m_oldZSpeed{ 100.f };
    bool    m_fastReturning{ false };
    std::vector<long> m_memory{ INF_RIGHT, INF_RIGHT, INF_RIGHT, INF_RIGHT };
    std::size_t m_currentMemory{ 0 };
    int m_keyPressed{ 0 };
    mgo::Curses::Window m_wnd;
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
