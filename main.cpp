#ifdef FAKE
#include "stepperControl/mockgpio.h"
#else
#include "stepperControl/gpio.h"
#endif

#include "controller.h"
#include "log.h"
#include "model.h"

#include <iostream>

int main( int argc, char* argv[] )
{
    // TODO command line processing - to switch view from 
    // curses to SFML for example
    try
    {
        INIT_MGOLOG( "els.log" );
        MGOLOG( "Program started" );

        #ifdef FAKE
            mgo::MockGpio gpio( false );
        #else
            mgo::Gpio gpio;
        #endif

        mgo::Model model( gpio );

        if( argc > 1 && std::string( argv[1] ) == "--sfml" )
        {
            model.m_useSfml = true;
        }

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
