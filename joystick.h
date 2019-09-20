#pragma once

#include <atomic>
#include <iosfwd>
#include <map>
#include <mutex>
#include <string>
#include <thread>


// My joystick has a lever which is an axis, but doesn't have
// potentiometers, so just returns -32767 or +32767 or zero
// for each axis. I treat the stick here like another button,
// but an analogue stick might be useful in future for
// manual control of the carriage with variable speed.
namespace mgo
{

struct AxisState
{
    short x = 0;
    short y = 0;
};

enum class ButtonState
{
    pressed,
    released
};

class Joystick
{
public:
    Joystick();
    Joystick( const std::string& deviceName );
    std::size_t getAxisCount() const;
    std::size_t getButtonCount() const;
    ButtonState getButtonState( int button );
    AxisState  getAxisState( int axis );
    ~Joystick();
private:
    bool m_initialised    = false;
    int  m_fileDescriptor = 0;
    std::atomic<bool> m_terminate{ false };
    std::thread m_thread;
    std::map<int, ButtonState> m_buttonStates;
    std::map<int, AxisState>   m_axisStates;
    std::mutex m_mutex;
};

} // end namespace