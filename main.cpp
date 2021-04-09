#ifdef FAKE
#include "stepperControl/mockgpio.h"
#else
#include "stepperControl/gpio.h"
#endif

#include "controller.h"
#include "log.h"
#include "model.h"
#include "configreader.h"

#include <iostream>

int main( int argc, char* argv[] )
{
    try
    {
        INIT_MGOLOG( "lc.log" );
        MGOLOG( "Program started" );

        #ifdef FAKE
            mgo::MockGpio gpio( false );
        #else
            mgo::Gpio gpio;
        #endif

        std::string configFile = "lc.cfg";
        if( argc > 1 )
        {
            if( argv[1][0] == '-' )
            {
                std::cout << "\nUsage: lc <configfile>\n\n";
                return 0;
            }
            configFile = argv[1];
        }

        mgo::ConfigReader config( configFile );
        mgo::Model model( gpio, config );

        mgo::Controller controller( &model );
        controller.run();

        return 0;
    }
    catch( const std::exception& e )
    {
        std::cout << "Exception encountered: " << e.what() << std::endl;
        return 1;
    }
}
