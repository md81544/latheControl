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

TEST_CASE( "Stop stopped motor" )
{
    mgo::MockGpio gpio( false );
    mgo::StepperMotor motor( gpio, 1'000 );
    motor.stop();
    motor.wait();
    REQUIRE( ! motor.isRunning() );
}

TEST_CASE( "Move then move again" )
{
    mgo::MockGpio gpio( false );
    mgo::StepperMotor motor( gpio, 1'000 );
    motor.goToStep( 50 );
    motor.wait();
    motor.goToStep( 100 );
    motor.wait();
    motor.goToStep( 150 );
    motor.wait();
    REQUIRE( ! motor.isRunning() );
    REQUIRE( motor.getCurrentStep() == 150 );
}

TEST_CASE( "Test forward and reverse" )
{
    mgo::MockGpio gpio( false );
    mgo::StepperMotor motor( gpio, 1'000 );
    motor.goToStep( 100 );
    motor.wait();
    REQUIRE( motor.getCurrentStep() == 100 );
    motor.goToStep( 50 ); // This is a reverse now
    motor.wait();
    REQUIRE( motor.getCurrentStep() == 50 );
}

TEST_CASE( "Check direction" )
{
    mgo::MockGpio gpio( false );
    mgo::StepperMotor motor( gpio, 1'000 );
    REQUIRE( motor.getDirection() == mgo::Direction::forward );
    motor.goToStep( 100 );
    motor.wait();
    REQUIRE( motor.getDirection() == mgo::Direction::forward );
    motor.goToStep( 50 ); // This is a reverse now
    motor.wait();
    REQUIRE( motor.getDirection() == mgo::Direction::reverse );
    motor.goToStep( 100 );
    motor.wait();
    REQUIRE( motor.getDirection() == mgo::Direction::forward );
}
