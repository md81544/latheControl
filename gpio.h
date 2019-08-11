#pragma once

// Concrete implementation of the IGpio ABC

#include "igpio.h"
#include "pigpio.h"

#include <stdexcept>

namespace mgo
{

class GpioException: public std::runtime_error
{
public:
    explicit GpioException( const std::string& message )
        : std::runtime_error(message)
    {}
};

class Gpio : public IGpio
{
public:
    Gpio(
        int   stepPin,
        int   reversePin,
        float rotaryEncoderGearing,
        int   rotaryEncoderPinA,
        int   rotaryEncoderPinB
        );
    virtual ~Gpio();
    void setStepPin( PinState ) override;
    void setReversePin( PinState ) override;
    float getRpm() override;
    float getPositionDegrees() override;
    RotationDirection getRotationDirection() override;
    // Blocks until spingle is at position:
    void  callbackAtPositionDegrees(
        float, // targetDegrees,
        std::function<void()> cb
        ) override;
    void delayMicroSeconds( long usecs ) override;
private:
    int m_stepPin;
    int m_reversePin;
    float m_rotaryEncoderGearing;
    int m_rotaryEncoderPinA;
    int m_rotaryEncoderPinB;
};

} // end namespace
