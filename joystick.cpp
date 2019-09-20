#include "joystick.h"
#include "log.h"

#include <bits/stdint-uintn.h>
#include <fcntl.h>
#include <linux/joystick.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <chrono>
#include <utility>

namespace mgo
{

// Delegate ctor
Joystick::Joystick() : Joystick( "/dev/input/js0" ) {}
Joystick::Joystick( const std::string& deviceName )
{
    MGOLOG( "Joystick initialisation" );
    m_fileDescriptor = open( deviceName.c_str(), O_RDONLY | O_NONBLOCK );
    if ( m_fileDescriptor == -1 )
    {
        MGOLOG( "Could not open device " << deviceName );
    }
    else
    {
        auto t = std::thread( [&](){
            for(;;)
            {
                if( m_terminate ) break;
                js_event event;
                auto bytes = read( m_fileDescriptor, &event, sizeof(event));
                if (bytes == sizeof(event))
                {
                    switch (event.type)
                    {
                        case JS_EVENT_BUTTON:
                        {
                            std::lock_guard<std::mutex> mtx( m_mutex );
                            m_buttonStates[ event.number ] = 
                                event.value ? ButtonState::pressed : ButtonState::released;
                            break;
                        }
                        case JS_EVENT_AXIS:
                        {
                            std::lock_guard<std::mutex> mtx( m_mutex );
                            std::size_t axis = event.number / 2;
                            if (event.number % 2 == 0)
                            {
                                m_axisStates[ axis ].x = event.value;
                            }
                            else
                            {
                                m_axisStates[ axis ].y = event.value;
                            }
                            break;
                        }
                        default:
                        {
                            // Ignore other events
                            break;
                        }
                    }
                }
                else
                {
                    std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
                }
            }
        });
        m_thread.swap( t );
    }
}

Joystick::~Joystick()
{
    m_terminate = true;
    if( m_thread.joinable() )
    {
        m_thread.join();
    }
    close( m_fileDescriptor );
}

std::size_t Joystick::getAxisCount() const
{
    uint8_t axes;

    if ( ioctl( m_fileDescriptor, JSIOCGAXES, &axes) == -1)
    {
        return 0;
    }
    return axes;
}

std::size_t Joystick::getButtonCount() const
{
    uint8_t buttons;
    if (ioctl( m_fileDescriptor, JSIOCGBUTTONS, &buttons) == -1)
    {
        return 0;
    }
    return buttons;
}

ButtonState Joystick::getButtonState( int button )
{
    std::lock_guard<std::mutex> mtx( m_mutex );
    auto it = m_buttonStates.find( button );
    if ( it == m_buttonStates.end() )
    {
        return ButtonState::released;
    }
    else
    {
        return it->second;
    }
}

AxisState Joystick::getAxisState( int axis )
{
    std::lock_guard<std::mutex> mtx( m_mutex );
    auto it = m_axisStates.find( axis );
    if ( it == m_axisStates.end() )
    {
        return AxisState();
    }
    else
    {
        return it->second;
    }
}

} // end namespace