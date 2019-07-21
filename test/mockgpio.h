#pragma once

#include <iostream>
#include <sstream>

// Concrete implementation of the IGpio ABC,
// but mocked. Includes ability to write
// diags messages.

#include "../igpio.h"

#include <unistd.h>
#include <atomic>
#include <thread>

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
        m_terminate = true;
        if ( m_thread.joinable() )
        {
            m_thread.join();
        }
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

    void delayMicroSeconds( long usecs ) override
    {
        std::ostringstream oss;
        oss << "Sleeping for " << usecs << " usecs";
        print( oss.str() );
        usleep( usecs );
    }

    void setRotaryEncoderCallback(
        std::function<void(int)> cb
        ) override
    {
        m_callback = cb;
        std::thread t([&]()
            {
                for(;;)
                {
                    if( m_terminate ) break;
                    usleep( 10'000 );
                    m_callback( 1 );
                }
            }
            );
        t.swap( m_thread );

    }

private:
    bool m_print;
    std::atomic<bool> m_terminate{ false };
    std::thread m_thread;
    std::function<void(int)> m_callback;

    void print( const std::string& msg )
    {
        if( m_print )
        {
            std::cout << msg << std::endl;
        }
    }
};

} // end namespace
