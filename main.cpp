#include "steppermotor.h"
#include "gpio.h"
#include "curses.h"

#include <iostream>
#include <functional>
#include <string>

std::function<void(int, int, uint32_t, void*)> cb;

void callback(
    int gpio,
    int level,
    uint32_t tick,
    void* user
    )
{
    cb( gpio, level, tick, user );
}


int main()
{
    try
    {
        mgo::Gpio gpio( 8, 7 ); // step pin, reverse pin

        mgo::StepperMotor motor( gpio, 1'000 );
        int speed = 400;
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
                case 27:
                {
                    moving = false;
                    quit = true;
                    break;
                }
                case 259:
                {
                    if( speed < 2000 ) speed += 20;
                    break;
                }
                case 258:
                {
                    if( speed > 20 ) speed -= 20;
                    break;
                }
                case 260:
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
                case 261:
                {
                    status = "moving right";
                    moving = true;
                    targetStep = -10'000'000;
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

            wnd.move( 5, 0 );
            wnd << "Status: " << key  << " " << status << "  " << speed << " rpm"
                "                                \n";
            wnd.refresh();
        }

        /* Experimental code, to put in a class------------------
        int pinA = 23;
        int pinB = 24;

        gpioSetMode(pinA, PI_INPUT);
        gpioSetMode(pinB, PI_INPUT);

        // pull up is needed as encoder common is grounded
        gpioSetPullUpDown(pinA, PI_PUD_UP);
        gpioSetPullUpDown(pinB, PI_PUD_UP);

        auto cbLambda = [&]( int gpio, int level, uint32_t, void * )
        {
            static int lastPin{ 0 };
            static int levA;
            static int levB;
            static int pos{ 0 };
            static int oldPos{ 0 };

            if ( gpio == pinA )
            {
                levA = level;
            }
            else
            {
                levB = level;
            }
            if ( gpio != lastPin ) // debounce
            {
                lastPin = gpio;
                if ( gpio == pinA && level == 1 )
                {
                    if( levB ) ++pos;
                }
                else if ( gpio == pinB && level == 1 )
                {
                    if( levA ) --pos;
                }
                if ( pos != oldPos )
                {
                    // Move the motor accordingly. Motor = 1,000 steps
                    // per rev, rotary encoder = 100 pulses per rev
                    motor.goToStep( pos * 10 );
                    // we don't wait for the motor as we are in a callback
                    // and timing is critical
                    oldPos = pos;
                }
            }
        };
        cb = cbLambda;

        gpioSetAlertFuncEx( pinA, callback, nullptr );
        gpioSetAlertFuncEx( pinB, callback, nullptr );

        std::cout << "Press ENTER to terminate " << std::endl;
        std::cin.ignore();

        gpioSetAlertFuncEx(pinA, 0, nullptr);
        gpioSetAlertFuncEx(pinB, 0, nullptr);
        // Experimental code, to put in a class------------------
        */

        return 0;
    }
    catch( const std::exception& e )
    {
        std::cout << "Exception encountered: " << e.what() << std::endl;
        return 1;
    }
}
