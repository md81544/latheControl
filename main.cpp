#include "steppermotor.h"
#include "gpio.h"

#include <iostream>
#include <functional>

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
        motor.setRpm( 2'000 );

        // Experimental code, to put in a class------------------
        int pinA = 23;
        int pinB = 24;

        gpioSetMode(pinA, PI_INPUT);
        gpioSetMode(pinB, PI_INPUT);

        // pull up is needed as encoder common is grounded
        gpioSetPullUpDown(pinA, PI_PUD_UP);
        gpioSetPullUpDown(pinB, PI_PUD_UP);

        auto cbLambda = [&]( int gpio, int level, uint32_t /* tick */, void * )
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

        return 0;
    }
    catch( const std::exception& e )
    {
        std::cout << "Exception encountered: " << e.what() << std::endl;
        return 1;
    }
}
