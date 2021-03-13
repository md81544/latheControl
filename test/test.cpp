#include "stepperControl/mockgpio.h"
#include "stepperControl/steppermotor.h"
#include "rotaryencoder.h"
#include "log.h"

#include <chrono>
#include <thread>

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

TEST_CASE( "Step once" )
{
    mgo::MockGpio gpio( false );
    mgo::StepperMotor motor( gpio, 0, 0, 0, 1'000, 1.0, 10'000.0 );
    motor.goToStep( 3 );
    motor.wait();

    REQUIRE( motor.getCurrentStep() == 3 );
}

TEST_CASE( "Stop motor" )
{
    mgo::MockGpio gpio( false );
    mgo::StepperMotor motor( gpio, 0, 0, 0, 1'000, 1.0, 10'000.0 );
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
    mgo::StepperMotor motor( gpio, 0, 0, 0, 1'000, 1.0, 10'000.0 );
    motor.stop();
    motor.wait();
    REQUIRE( ! motor.isRunning() );
}

TEST_CASE( "Move then move again" )
{
    mgo::MockGpio gpio( false );
    mgo::StepperMotor motor( gpio, 0, 0, 0, 1'000, 1.0, 10'000.0 );
    motor.goToStep( 50 );
    motor.wait();
    motor.goToStep( 100 );
    motor.wait();
    motor.goToStep( 150 );
    motor.wait();
    REQUIRE( ! motor.isRunning() );
    REQUIRE( motor.getCurrentStep() == 150 );
}

TEST_CASE( "Forward and reverse" )
{
    mgo::MockGpio gpio( false );
    mgo::StepperMotor motor( gpio, 0, 0, 0, 1'000, 1.0, 10'000.0 );
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
    mgo::StepperMotor motor( gpio, 0, 0, 0, 1'000, 1.0, 10'000.0 );
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

TEST_CASE( "Stepper RPM" )
{
    mgo::MockGpio gpio( false );
    mgo::StepperMotor motor( gpio, 0, 0, 0, 1'000, 1.0, 10'000.0 );
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

TEST_CASE( "Stepper RPM Limits" )
{
    mgo::MockGpio gpio( false );
    mgo::StepperMotor motor( gpio, 0, 0, 0, 1'000, 1.0, 10'000.0 );
    motor.setRpm( 0 );
    motor.goToStep( 10 ); // Shouldn't take infinite time :)
    motor.wait();
    motor.setRpm( 9'999'999 ); // bit too fast :)
    REQUIRE( motor.getDelay() > 5 );
}

TEST_CASE( "Change target step while busy" )
{
    mgo::MockGpio gpio( false );
    mgo::StepperMotor motor( gpio, 0, 0, 0, 1'000, 1.0, 10'000.0 );
    motor.setRpm( 2'000 );
    motor.goToStep( 1'000 );
    motor.goToStep( 2'000 );
    motor.wait();
    // Check the second "go to" was ignored
    REQUIRE( motor.getCurrentStep() == 1'000 );
}

TEST_CASE( "Rotary Encoder RPM" )
{
    mgo::MockGpio gpio( false );
    mgo::RotaryEncoder re(
        gpio,
        23,
        24,
        2000,
        35.f / 30.f
        );
    while( re.warmingUp() ) gpio.delayMicroSeconds( 1'000 );
    REQUIRE( re.getRpm() > 0.f);
}

TEST_CASE( "Rotary Encoder Position Callback" )
{
    mgo::MockGpio gpio( false );
    mgo::RotaryEncoder re(
        gpio,
        23,
        24,
        2000,
        35.f / 30.f
        );
    bool called = false;
    while( re.warmingUp() ) gpio.delayMicroSeconds( 1'000 );
    re.callbackAtZeroDegrees([&](){ called = true; });
    REQUIRE( re.warmingUp() == false );
    REQUIRE( called == true );
}

TEST_CASE( "Check backlash compensation" )
{
    mgo::MockGpio gpio( false );
    mgo::StepperMotor motor( gpio, 0, 0, 0, 1'000, 1.0, 10'000.0 );
    // Set backlash compensation. This sets our backlash slop to be ten
    // steps which means if we move one step in a positive manner the
    // motor should really have to move eleven "real" steps
    motor.setBacklashCompensation( 10, 0 );
    motor.goToStep( 1 );
    motor.wait();
    REQUIRE( motor.getCurrentStep() == 1 );
    REQUIRE( motor.getCurrentStepWithoutBacklashCompensation() == 11 );

    // Now we are at the far end of the backlash "window" so moving the next
    // one step should only move one "real" step
    motor.goToStep( 2 );
    motor.wait();
    REQUIRE( motor.getCurrentStep() == 2 );
    REQUIRE( motor.getCurrentStepWithoutBacklashCompensation() == 12 );

    // Now as we are pushed up against one end of the backlash window, if
    // we want to move BACK one step, the motor will actually need to do
    // another eleven steps:
    motor.goToStep( 1 );
    motor.wait();
    REQUIRE( motor.getCurrentStep() == 1 );
    REQUIRE( motor.getCurrentStepWithoutBacklashCompensation() == 1 );
}

TEST_CASE( "Check motor speed ramping" )
{
    mgo::MockGpio gpio( false );
    mgo::StepperMotor motor( gpio, 0, 0, 0, 1'000, 1.0, 10'000.0 );
    motor.setRpm( 10'000.0 );
    motor.goToStep( 999'999'999 );
    // Motor should not be at full speed immediately:
    double previousRampedRpm = 0.0;
    for( int n = 0; n < 5; ++n )
    {
        std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
        double rampedRpm = motor.getRampedRpm();
        REQUIRE( rampedRpm < 10'000.0 );
        REQUIRE( rampedRpm > previousRampedRpm );
        previousRampedRpm = rampedRpm;
    }
}
