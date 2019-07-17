#pragma once

// Concrete implementation of the IGpio ABC,
// but mocked

#include "../igpio.h"

#include <unistd.h>

namespace mgo
{

class MockGpio : public IGpio
{
public:
    MockGpio() {}
    virtual ~MockGpio() {}
    void setStepPin( PinState ) override {}
    void setReversePin( PinState ) override {}
    void delayMicroSeconds( long usecs ) override
    {
        usleep( usecs );
    }
};

} // end namespace
