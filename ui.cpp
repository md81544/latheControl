#include "ui.h"
#include "curses.h"
#include "steppermotor.h"

#include <cmath>
#include <sstream>
#include <string>

namespace
{

// This converts steps to real-world units.
// We can also negate the value depending
// on motor orientation (on a lathe the z-
// axis decreases in value towards the chuck)
std::string cnv( int steps )
{
    double mm = steps * -0.001;
    if( std::abs( mm ) < 0.001 )
    {
        mm = 0.0;
    }
    std::ostringstream oss;
    oss.precision(3);
    oss << std::fixed << mm << " mm";
    return oss.str();
}

} // end anonymous namespace

namespace mgo
{

// TODO:
//   show memory stores on screen, use 1-5 to select
//   then M/R work on that one.
//   T (tailstock) - mandatory set before operation
//   C (chuck) - mandatory set before operation
//   Add a "shutdown" command at the end to return
//   to "T" so we start in a known position?

Ui::Ui( IGpio& gpio )
    : m_gpio( gpio )
{
}

void Ui::run()
{
    using namespace mgo::Curses;
    mgo::StepperMotor motor( m_gpio, 1'000 );
    int speed = 200;
    long targetStep = 0;
    long memory = 0;
    motor.setRpm( speed );

    Window wnd;
    wnd.cursor( Cursor::off );
    wnd << "Press up/down to set speed\n";
    wnd << "Press left and right arrow to move\n";
    wnd << "Press space to stop\n";
    wnd << "Press M to remember position, and R to return to it\n";
    wnd << "Escape quits the application\n";
    wnd.refresh();
    wnd.setBlocking( Input::nonBlocking );
    std::string status = "stopped";
    bool moving = false;
    bool quit = false;
    int key;
    while( ! quit )
    {
        int t = wnd.getKey();
        if( t != -1 )
        {
            key = t;
            switch( key )
            {
                case -1:
                {
                    break;
                }
                case 81:  // Q
                case 113: // q
                case 27:  // Esc
                {
                    moving = false;
                    quit = true;
                    break;
                }
                case 259:
                {
                    beep();
                    if( speed < 900 ) speed += 20;
                    break;
                }
                case 258:
                {
                    beep();
                    if( speed > 20 ) speed -= 20;
                    break;
                }
                case 260: // Left arrow
                {
                    status = "moving left";
                    moving = true;
                    targetStep = 100'000'000;
                    break;
                }
                case 77:  // M
                case 109: // m
                {
                    memory = motor.getCurrentStep();
                    break;
                }
                case 82:  // R
                case 114: // r
                {
                    motor.stop();
                    motor.wait();
                    status = "returning";
                    moving = true;
                    targetStep = memory;
                    break;
                }
                case 261: // Right arrow
                {
                    status = "moving right";
                    moving = true;
                    targetStep = -100'000'000;
                    break;
                }
                case 44: // comma (<) - nudge left
                {
                    moving = true;
                    targetStep = motor.getCurrentStep() + 50;
                    break;
                }
                case 46: // full stop (>) - nudge right
                {
                    moving = true;
                    targetStep = motor.getCurrentStep() - 50;
                    break;
                }
                default:
                {
                    status = "stopped";
                    moving = false;
                    break;
                }
            }
        }

        motor.setRpm( speed );
        if( !moving )
        {
            motor.stop();
        }
        else
        {
            motor.goToStep( targetStep );
        }
        if ( !motor.isRunning() )
        {
            status = "stopped";
        }

        std::string targetString = cnv( targetStep );

        if ( targetStep ==  100'000'000 ) targetString = "<----";
        if ( targetStep == -100'000'000 ) targetString = "---->";

        wnd.move( 6, 0 );
        wnd.setColour( Colours::yellowOnBlack );
        wnd.clearToEol();
        wnd << "Status:  " << " " << status << "  " << speed << " rpm\n";
        wnd.clearToEol();
        wnd << "Target:   " << targetString << ", current: "
            << cnv( motor.getCurrentStep() ) << "\n\n";
        wnd.setColour( Colours::redOnBlack );
        wnd.clearToEol();
        wnd << "Memory:   " << cnv( memory ) << "\n";
        wnd.setColour( Colours::greenOnBlack );
        wnd.clearToEol();
        wnd << "Keypress: " << key << "\n";
        wnd.refresh();
    }
}


} // end namespace
