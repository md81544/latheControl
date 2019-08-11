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
            // parameters are: step pin, reverse pin, rotary encoder A, r.e. B
            mgo::Gpio gpio( 8, 7, 23, 24, 35.f/30.f );
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
