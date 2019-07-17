#pragma once

#include "igpio.h"

#include <atomic>
#include <memory>
#include <thread>

namespace mgo
{

enum class Direction
{
    forward,
    reverse
};

class StepperMotor
{
public:
    StepperMotor( const IGpio& gpio );
    ~StepperMotor();
    bool isRunning();
    Direction getDirection();
    long getCurrentStep();
    // Go to a specific step
    void goToStep( long step );
    // Set motor speed
    void setSpeedPercent( int percent );
    // Stop any motion
    void stop();
    // Block until the current operation completes
    void wait();
private:
    const IGpio& m_gpio;
    std::thread m_thread;
};

} // end namespace

