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
    Gpio( int stepPin, int reversePin );
    virtual ~Gpio();
    void setStepPin( PinState ) override;
    void setReversePin( PinState ) override;
    void delayMicroSeconds( long usecs ) override;
    void setRotaryEncoderCallback(
        std::function<void(int)>
        ) override{} // TODO
private:
    int m_stepPin;
    int m_reversePin;
};

} // end namespace
