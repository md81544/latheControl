#pragma once

// Abstract base "interface" class
// to allow mocking out the hardware
// interface to allow the StepperMotor
// class to be used for tests

namespace mgo
{

enum class PinState
{
    High,
    Low
};

// concrete classes can initialise / terminate
// the GPIO library in their ctors / dtors

class IGpio
{
public:
    virtual void setStepPin( PinState ) = 0;
    virtual void setReversePin( PinState ) = 0;
};

} // end namespace
