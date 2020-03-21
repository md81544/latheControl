#ifdef FAKE
#include "stepperControl/mockgpio.h"
#else
#include "stepperControl/gpio.h"
#endif

#include "ui.h"
#include "log.h"

#include <iostream>

int main()
{
    try
    {
        INIT_MGOLOG( "els.log" );
        MGOLOG( "Program started" );

        #ifdef FAKE
            mgo::MockGpio gpio( false );
        #else
            mgo::Gpio gpio( 8, 7 );
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
