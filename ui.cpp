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
    mgo::StepperMotor motor( m_gpio, 1'000 );
    int speed = 200;
    long targetStep = 0;
    long memory = 0;
    motor.setRpm( speed );

    mgo::Curses::Window wnd;
    wnd.cursor( mgo::Curses::Cursor::off );
    wnd << "Press up/down to set speed\n";
    wnd << "Press left and right arrow to move\n";
    wnd << "Press space to stop\n";
    wnd << "Press M to remember position, and R to return to it\n";
    wnd << "Escape quits the application\n";
    wnd.refresh();
    wnd.setBlocking( mgo::Curses::Input::nonBlocking );
    std::string status;
    bool moving = false;
    bool quit = false;
    while( ! quit )
    {
        int key = wnd.getKey();
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
                if( speed < 900 ) speed += 20;
                break;
            }
            case 258:
            {
                if( speed > 20 ) speed -= 20;
                break;
            }
            case 260: // Left arrow
            {
                status = "moving left";
                moving = true;
                targetStep = 10'000'000;
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
                status = "Returning";
                moving = true;
                targetStep = memory;
                break;
            }
            case 261: // Right arrow
            {
                status = "moving right";
                moving = true;
                targetStep = -10'000'000;
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

        motor.setRpm( speed );
        if( !moving )
        {
            motor.stop();
        }
        else
        {
            motor.goToStep( targetStep );
        }

        wnd.move( 6, 0 );
        wnd << "Status: " << " " << status << "  " << speed << " rpm"
            "                                \n";
        wnd << "Target: " << targetStep << ", current: "
            << motor.getCurrentStep() << "                       \n";
        wnd << "Memory: " << memory << "                  \n";
        wnd.refresh();
    }
}


} // end namespace
