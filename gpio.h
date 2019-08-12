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
        int   reversePin
        );
    virtual ~Gpio();
    void setStepPin( PinState ) override;
    void setReversePin( PinState ) override;
    void setRotaryEncoderCallback(
        int pinA,
        int pinB,
        std::function<void(
            int      pin,
            int      level,
            uint32_t tick,
            void*    user
            )> callback,
        void* userData
        ) override;
    void delayMicroSeconds( long usecs ) override;
private:
    int m_stepPin;
    int m_reversePin;
};

} // end namespace
