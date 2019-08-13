#include "mockgpio.h"
#include "steppermotor.h"
#include "rotaryencoder.h"
#include "log.h"

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

TEST_CASE( "Test RPM" )
{
    mgo::MockGpio gpio( false );
    mgo::StepperMotor motor( gpio, 1'000 );
    motor.setRpm( 60 );
    // That's one revolution per second, and we've set steps per
    // revolution to be 1000, so delay (which is used TWICE per
    // thread loop), should be 500 usecs
    REQUIRE( motor.getDelay() == 500 );
    motor.setRpm( 120 );
    REQUIRE( motor.getDelay() == 250 );
    motor.setRpm( 2'000 );
    REQUIRE( motor.getDelay() == 15 );
    motor.setRpm( 1 );
    REQUIRE( motor.getDelay() == 30'000 );
}

TEST_CASE( "Test RPM Limits" )
{
    mgo::MockGpio gpio( false );
    mgo::StepperMotor motor( gpio, 1'000 );
    motor.setRpm( 0 );
    motor.goToStep( 10 ); // Shouldn't take infinite time :)
    motor.wait();
    motor.setRpm( 9'999'999 ); // bit too fast :)
    REQUIRE( motor.getDelay() > 5 );
}

TEST_CASE( "Change target step while busy" )
{
    mgo::MockGpio gpio( false );
    mgo::StepperMotor motor( gpio, 1'000 );
    motor.setRpm( 2'000 );
    motor.goToStep( 1'000 );
    gpio.delayMicroSeconds( 1'000 );
    motor.goToStep( 2'000 );
    motor.wait();
    // Check the second "go to" was ignored
    REQUIRE( motor.getCurrentStep() == 1'000 );
}

TEST_CASE( "Rotary Encoder RPM" )
{
    INIT_MGOLOG( "test.log" );
    mgo::MockGpio gpio( false );
    mgo::RotaryEncoder re(
        gpio,
        23,
        24,
        2000,
        35.f / 30.f
        );
    gpio.delayMicroSeconds( 1'000'000 ); // give it a chance to "warm up"
    REQUIRE( re.getRpm() > 0.f);
}

