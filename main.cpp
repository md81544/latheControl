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

        mgo::Model model( gpio );

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
        model.m_config = std::make_unique<mgo::ConfigReader>( configFile );

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
