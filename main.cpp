#ifdef FAKE
#include "mockgpio.h"
#else
#include "gpio.h"
#endif

#include "ui.h"

#include <iostream>

int main()
{
    try
    {
        #ifdef FAKE
            mgo::MockGpio gpio( false );
        #else
            // parameters are:
            //    step pin
            //    reverse pin
            //    rotary encoder pulses per revolution
            //    rotary encoder gearing
            //    rotary encoder pin A
            //    rotary encoder pin B
            mgo::Gpio gpio( 8, 7, 2000, 35.f/30.f, 23, 24 );
        #endif

        mgo::Ui ui( gpio );
        ui.run();


        return 0;
    }
    catch( const std::exception& e )
    {
        std::cout << "Exception encountered: " << e.what() << std::endl;
        return 1;
    }
}
