#include "ui.h"
#include "curses.h"
#include "steppermotor.h"

namespace mgo
{

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
        if ( !motor.isRunning() ) status = "stopped";

        std::string targetString = std::to_string( targetStep );
        if ( targetStep ==  100'000'000 ) targetString = "<----";
        if ( targetStep == -100'000'000 ) targetString = "---->";

        wnd.move( 6, 0 );
        wnd.setColour( Colours::yellowOnBlack );
        wnd << "Status: " << " " << status << "  " << speed << " rpm"
            "                                \n";
        wnd << "Target:  " << targetString << ", current: "
            << motor.getCurrentStep() << "                       \n\n";
        wnd.setColour( Colours::redOnBlack );
        wnd << "Memory:  " << memory << "                  \n";
        wnd.setColour( Colours::greenOnBlack );
        wnd.refresh();
    }
}


} // end namespace
