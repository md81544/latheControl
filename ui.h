#pragma once

#include "igpio.h"
#include "curses.h"
#include "steppermotor.h"
#include "rotaryencoder.h"
#include "threadpitches.h"

#include <memory>
#include <string>
#include <vector>

namespace mgo
{

const float MAX_MOTOR_SPEED = 700.f;

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
    std::vector<long> m_memory{ 0, 0, 0, 0 };
    std::size_t m_currentMemory{ 0 };
    int m_keyPressed{ 0 };
    mgo::Curses::Window m_wnd;
    std::unique_ptr<mgo::StepperMotor> m_motor;
    std::unique_ptr<mgo::RotaryEncoder> m_rotaryEncoder;
    std::size_t m_threadPitchIndex{ 0 };
    bool    m_threadCuttingOn{ false };
    int     m_threadCutAdvanceCount{ 0 };
};

} // end namespace
