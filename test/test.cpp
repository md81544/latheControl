#include "stepperControl/mockgpio.h"
#include "stepperControl/steppermotor.h"
#include "rotaryencoder.h"
#include "log.h"
#include "model.h"
#include "configreader.h"

#include <chrono>
#include <thread>

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

TEST_CASE( "Stepper: Step once" )
{
    mgo::MockGpio gpio( false );
    mgo::StepperMotor motor( gpio, 0, 0, 0, 1'000, 1.0, 10'000.0 );
    motor.setRpm( 500.0 );
    motor.goToStep( 3 );
    motor.wait();

    REQUIRE( motor.getCurrentStep() == 3 );
}

TEST_CASE( "Stepper: Stop motor" )
{
    mgo::MockGpio gpio( false );
    mgo::StepperMotor motor( gpio, 0, 0, 0, 1'000, 1.0, 10'000.0 );
    // High number of steps:
    motor.setRpm( 500.0 );
    motor.goToStep( 1'000'000 );
    gpio.delayMicroSeconds( 1'000 );
    REQUIRE( motor.isRunning() );
    motor.stop();
    motor.wait();
    REQUIRE( ! motor.isRunning() );
    REQUIRE( motor.getCurrentStep() > 0 );
}

TEST_CASE( "Stepper: Stop stopped motor" )
{
    mgo::MockGpio gpio( false );
    mgo::StepperMotor motor( gpio, 0, 0, 0, 1'000, 1.0, 10'000.0 );
    motor.stop();
    motor.wait();
    REQUIRE( ! motor.isRunning() );
}

TEST_CASE( "Stepper: Move then move again" )
{
    mgo::MockGpio gpio( false );
    mgo::StepperMotor motor( gpio, 0, 0, 0, 1'000, 1.0, 10'000.0 );
    motor.setRpm( 500.0 );
    motor.goToStep( 50 );
    motor.wait();
    motor.goToStep( 100 );
    motor.wait();
    motor.goToStep( 150 );
    motor.wait();
    REQUIRE( ! motor.isRunning() );
    REQUIRE( motor.getCurrentStep() == 150 );
}

TEST_CASE( "Stepper: Forward and reverse" )
{
    mgo::MockGpio gpio( false );
    mgo::StepperMotor motor( gpio, 0, 0, 0, 1'000, 1.0, 10'000.0 );
    motor.setRpm( 500.0 );
    motor.goToStep( 100 );
    motor.wait();
    REQUIRE( motor.getCurrentStep() == 100 );
    motor.goToStep( 50 ); // This is a reverse now
    motor.wait();
    REQUIRE( motor.getCurrentStep() == 50 );
}

TEST_CASE( "Stepper: Check direction" )
{
    mgo::MockGpio gpio( false );
    mgo::StepperMotor motor( gpio, 0, 0, 0, 1'000, 1.0, 10'000.0 );
    motor.setRpm( 500.0 );
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

TEST_CASE( "Stepper: RPM" )
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

TEST_CASE( "Stepper: RPM Limits" )
{
    mgo::MockGpio gpio( false );
    mgo::StepperMotor motor( gpio, 0, 0, 0, 1'000, 1.0, 10'000.0 );
    motor.setRpm( 0 );
    motor.goToStep( 10 ); // Shouldn't take infinite time :)
    motor.wait();
    motor.setRpm( 9'999'999 ); // bit too fast :)
    REQUIRE( motor.getDelay() > 5 );
}

TEST_CASE( "Stepper: Change target step while busy" )
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

TEST_CASE( "Stepper: Rotary Encoder RPM" )
{
    mgo::MockGpio gpio( false );
    mgo::RotaryEncoder re(
        gpio,
        23,
        24,
        2000,
        35.f / 30.f
        );
    gpio.delayMicroSeconds( 500'000 );
    REQUIRE( re.getRpm() > 0.f);
}

TEST_CASE( "Stepper: Rotary Encoder Position Callback" )
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

TEST_CASE( "Stepper: Check backlash compensation" )
{
    mgo::MockGpio gpio( false );
    mgo::StepperMotor motor( gpio, 0, 0, 0, 1'000, 1.0, 10'000.0 );
    // Set backlash compensation. This sets our backlash slop to be ten
    // steps which means if we move one step in a positive manner the
    // motor should really have to move eleven "real" steps
    motor.setRpm( 500.0 );
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

TEST_CASE( "Stepper: Check motor speed ramping" )
{
    mgo::MockGpio gpio( false );
    double maxSpeed = 1'000.0; // mm/sec, not rpm
    long stepsPerRev = 4'000;
    double conversionFactor = 0.0005;
    double maxRpm = maxSpeed / conversionFactor / stepsPerRev;
    mgo::StepperMotor motor( gpio, 0, 0, 0, stepsPerRev, conversionFactor, maxRpm );
    motor.setSpeed( maxSpeed );
    motor.goToStep( 999'999'999 );
    // Motor should not be at full speed immediately:
    double previousRampedRpm = 0.0;
    for( int n = 0; n < 5; ++n )
    {
        std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
        double rampedSpeed = motor.getRampedSpeed();
        REQUIRE( rampedSpeed < maxSpeed );
        REQUIRE( rampedSpeed > previousRampedRpm );
        previousRampedRpm = rampedSpeed;
    }
}

TEST_CASE( "Stepper: Check motor synchronisation" )
{
    mgo::MockGpio gpio( false );
    mgo::StepperMotor motor1( gpio, 0, 0, 0, 1'000, 0.01, 10'000.0 );
    mgo::StepperMotor motor2( gpio, 0, 0, 0, 1'000, 0.01, 10'000.0 );
    motor1.setRpm( 500.0 );
    motor2.setRpm( 500.0 );
    REQUIRE( motor2.getPosition() == 0.0 );
    motor2.synchroniseOn( &motor1, []( double pos, double ){ return pos / 2.0; } );
    motor1.setSpeed( 1'000.0 );
    motor1.goToPosition( 2.4 );
    motor1.wait();
    motor2.wait();
    REQUIRE( motor2.getPosition() == Approx( 1.2 ) );
}

TEST_CASE( "Model:   check tapering" )
{
    mgo::MockGpio gpio( false );
    // The mock config reader just returns whatever you specify
    // as the default return value
    mgo::MockConfigReader config;
    mgo::Model model( gpio, config );
    model.initialise();
    model.changeMode( mgo::Mode::Taper );
    model.setTaperAngle( 45.0 );
    model.axis1SetSpeed( 200.0 );
    model.axis1GoToPosition( -0.5 );
    model.axis1Wait();
    double pos = model.getAxis2MotorPosition();
    REQUIRE( pos < -0.495 );
    REQUIRE( pos > -0.505 );
}

TEST_CASE( "Model:   check radius" )
{
    mgo::MockGpio gpio( false );
    // The mock config reader just returns whatever you specify
    // as the default return value
    mgo::MockConfigReader config;
    mgo::Model model( gpio, config );
    model.initialise();
    model.changeMode( mgo::Mode::Radius );
    model.setRadius( 1.0 );
    model.axis1SetSpeed( 60.0 );
    model.axis1GoToPosition( 1.0 );
    model.axis1Wait();
    model.axis2Wait();

    double pos = model.getAxis2MotorPosition();
    REQUIRE( pos < 1.05 );
    REQUIRE( pos > 0.95 );

    model.axis1GoToPosition( 0.0 );
    model.axis1Wait();

    // X position should be zero
    pos = model.getAxis2MotorPosition();
    REQUIRE( pos < 0.05 );
}