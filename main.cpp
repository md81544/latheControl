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

int main()
{
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

        model.m_config = std::make_unique<mgo::ConfigReader>( "els.cfg" );

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
