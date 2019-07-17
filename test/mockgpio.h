#pragma once

// Concrete implementation of the IGpio ABC,
// but mocked

#include "../igpio.h"

namespace mgo
{

class MockGpio : public IGpio
{
public:
    MockGpio() {}
    virtual ~MockGpio() {}
    void setStepPin( PinState ) override {}
    void setReversePin( PinState ) override {}
};

} // end namespace
