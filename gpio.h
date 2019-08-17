#pragma once

// Concrete implementation of the IGpio ABC

#include "igpio.h"
#include "pigpio.h"

#include <stdexcept>

namespace mgo
{

//typedef void (*eventFuncEx_t)
//   (int event, int level, uint32_t tick, void *userdata);

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
        void (*callback)( int, int, uint32_t, void* ),
        void* userData
        ) override;
    void delayMicroSeconds( long usecs ) override;
    uint32_t getTick() override;
private:
    int m_stepPin;
    int m_reversePin;
    int m_pinA{ 0 };
    int m_pinB{ 0 };
};

} // end namespace
