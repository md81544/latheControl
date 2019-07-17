#pragma once

// Concrete implementation of the IGpio ABC

#include "igpio.h"
#include "pigpio.h"

namespace mgo
{

class Gpio : public IGpio
{
public:
    Gpio( int stepPin, int reversePin );
    virtual ~Gpio();
    void setStepPin( PinState ) override;
    void setReversePin( PinState ) override;
};

} // end namespace
