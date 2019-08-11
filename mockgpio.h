#pragma once

// Concrete implementation of the IGpio ABC,
// but mocked. Includes ability to write
// diags messages.

#include "igpio.h"

#include <iostream>
#include <sstream>
#include <unistd.h>


namespace mgo
{

class MockGpio : public IGpio
{
public:
    MockGpio( bool printDiags )
        : m_print( printDiags )
    {
        print( "Initialising GPIO library" );
    }

    virtual ~MockGpio()
    {
        print( "Terminating GPIO library" );
    }

    void setStepPin( PinState state ) override
    {
        if( state == PinState::high )
        {
            print( "Setting step pin HIGH" );
        }
        else
        {
            print( "Setting step pin LOW" );
        }
    }

    void setReversePin( PinState state ) override
    {
        if( state == PinState::high )
        {
            print( "Setting reverse pin HIGH" );
        }
        else
        {
            print( "Setting reverse pin LOW" );
        }
    }

    virtual void  setRotaryEncoderGearing( float gearing )
    {
        m_gearing = gearing;
    }

    float getRpm() override
    {
        return m_gearing * 1'000.f;
    }

    float getPositionDegrees() override
    {
        return 0.f;
    }

    RotationDirection getRotationDirection() override
    {
        return RotationDirection::normal;
    }

    void  callbackAtPositionDegrees(
        float, // targetDegrees,
        std::function<void()> cb
        )
    {
        cb();
    }

    void delayMicroSeconds( long usecs ) override
    {
        std::ostringstream oss;
        oss << "Sleeping for " << usecs << " usecs";
        print( oss.str() );
        usleep( usecs );
    }

private:
    bool  m_print;
    float m_gearing{ 1.f };
    void  print( const std::string& msg )
    {
        if( m_print )
        {
            std::cout << msg << std::endl;
        }
    }
};

} // end namespace
