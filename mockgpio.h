#pragma once

// Concrete implementation of the IGpio ABC,
// but mocked. Includes ability to write
// diags messages.

#include "igpio.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <functional>
#include <sstream>
#include <thread>
#include <unistd.h>


namespace mgo
{

class MockGpio : public IGpio
{
public:
    explicit MockGpio( bool printDiags )
        : m_print( printDiags )
    {
        print( "Initialising GPIO library" );
    }

    virtual ~MockGpio()
    {
        print( "Terminating GPIO library" );
        m_terminate = true;
        if( m_callbacker.joinable() )
        {
            print( "Waiting for callbacker thread to terminate" );
            m_callbacker.join();
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

    uint32_t getTick() override
    {
        return std::chrono::duration_cast< std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()
            ).count();
    }

    void stopRotaryEncoderCallback()
    {
        // class-specific function to allow us to stop the
        // callbacker manually
        print( "Stopping callbacker" );
        m_terminate = true;
    }

    void setRotaryEncoderCallback(
        int pinA,
        int pinB,
        void (*callback)(int, int, uint32_t, void*),
        void* userData
        ) override
    {
        auto t = std::thread( [=]()
            {
                using namespace std::chrono;
                for(;;)
                {
                    try
                    {
                        if( m_terminate ) break;
                        callback(
                            pinA,
                            1,
                            getTick(),
                            userData
                            );
                        if( m_terminate ) break;
                        callback(
                            pinB,
                            1,
                            getTick(),
                            userData
                            );
                        if( m_terminate ) break;
                        callback(
                            pinA,
                            0,
                            getTick(),
                            userData
                            );
                        if( m_terminate ) break;
                        callback(
                            pinB,
                            0,
                            getTick(),
                            userData
                            );
                        std::this_thread::sleep_for( nanoseconds( 1 ) );
                    }
                    catch( const std::exception& e )
                    {
                        print( e.what() );
                        break;
                    }
                }
                print( "Callbacker thread ended" );
            } );
        m_callbacker.swap( t );
    }

    void delayMicroSeconds( long usecs ) override
    {
        std::ostringstream oss;
        oss << "Sleeping for " << usecs << " usecs";
        print( oss.str() );
        usleep( usecs );
    }

private:
    std::atomic<bool> m_terminate{ false };
    std::thread m_callbacker;

    bool  m_print;
    void  print( const std::string& msg )
    {
        if( m_print )
        {
            std::cout << msg << std::endl;
        }
    }

};

} // end namespace
