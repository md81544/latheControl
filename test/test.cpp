#include "mockgpio.h"
#include "../steppermotor.h"

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

TEST_CASE( "Step once", "[step once]" )
{
    mgo::MockGpio gpio( true );
    mgo::StepperMotor motor( gpio, 1'000 );
    motor.goToStep( 3 );
    motor.wait();

    REQUIRE( motor.getCurrentStep() == 3 );
}
