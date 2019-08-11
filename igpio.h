#pragma once

// Abstract base "interface" class
// to allow mocking out the hardware
// interface to allow the StepperMotor
// class to be used for tests

#include <functional>

namespace mgo
{

enum class PinState
{
    high,
    low
};

enum class RotationDirection
{
    normal,
    reversed
};

// concrete classes can initialise / terminate
// the GPIO library in their ctors / dtors

class IGpio
{
public:
    // Support for stepper motor:
    virtual void setStepPin( PinState ) = 0;
    virtual void setReversePin( PinState ) = 0;
    // Support for rotary encoder:
    virtual void  setRotaryEncoderGearing( float ) = 0;
    virtual float getRpm() = 0;
    virtual float getPositionDegrees() = 0;
    virtual void  callbackAtPositionDegrees( float, std::function<void()> ) = 0;
    virtual RotationDirection getRotationDirection() = 0;
    // TODO
    // General:
    virtual void delayMicroSeconds( long ) = 0;
};

} // end namespace
