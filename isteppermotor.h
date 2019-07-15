#pragma once

namespace mgo
{

enum class Direction
{
    forward,
    reverse
};

// Interface class to the stepper motor to allow us
// to mock it out for testing

class IStepperMotor
{
    virtual bool isRunning() = 0;
    virtual Direction getDirection() = 0;
    virtual long getCurrentStep() = 0;
    virtual void goToStep( long step ) = 0;
    virtual void setSpeedPercent( int percent ) = 0;
    virtual void stop() = 0;
    virtual void wait() = 0;
};

} // namespace mgo
