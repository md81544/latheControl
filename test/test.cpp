#include "mockgpio.h"
#include "../steppermotor.h"

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

TEST_CASE( "Step once" )
{
    mgo::MockGpio gpio( false );
    mgo::StepperMotor motor( gpio, 1'000 );
    motor.goToStep( 3 );
    motor.wait();

    REQUIRE( motor.getCurrentStep() == 3 );
}

TEST_CASE( "Stop motor" )
{
    mgo::MockGpio gpio( false );
    mgo::StepperMotor motor( gpio, 1'000 );
    // High number of steps:
    motor.goToStep( 1'000'000 );
    gpio.delayMicroSeconds( 1'000 );
    REQUIRE( motor.isRunning() );
    motor.stop();
    motor.wait();
    REQUIRE( ! motor.isRunning() );
    REQUIRE( motor.getCurrentStep() > 0 );
}
