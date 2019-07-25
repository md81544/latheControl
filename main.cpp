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
            mgo::Gpio gpio( 8, 7 ); // step pin, reverse pin
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
