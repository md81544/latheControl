#include "controller.h"

#include "fmt/format.h"
#include "keycodes.h"
#include "log.h"
#include "stepperControl/igpio.h"
#include "threadpitches.h"  // for ThreadPitch, threadPitches
#include "view_sfml.h"

#include <cassert>
#include <chrono>

namespace mgo
{

namespace
{

void yieldSleep( std::chrono::microseconds microsecs )
{
    auto start = std::chrono::high_resolution_clock::now();
    auto end = start + microsecs;
    while( std::chrono::high_resolution_clock::now() < end )
    {
        std::this_thread::yield();
    }
}

} // end anonymous namespace

Controller::Controller( Model* model )
    : m_model( model )
{
    m_view = std::make_unique<ViewSfml>();


    m_view->initialise( *m_model );

    m_view->updateDisplay( *m_model ); // get SFML running before we start the motor threads

    double axis1ConversionFactor =
        m_model->m_config->readDouble( "Axis1ConversionNumerator", -1.0 ) /
            m_model->m_config->readDouble( "Axis1ConversionDivisor", 1'000.0 );
    m_axis1MaxMotorSpeed = m_model->m_config->readDouble( "Axis1MaxMotorSpeed", 700.0 );
    long axis1StepsPerRevolution = m_model->m_config->readLong( "Axis1StepsPerRev", 1'000 );
    m_model->m_axis1Motor = std::make_unique<mgo::StepperMotor>(
        m_model->m_gpio,
        m_model->m_config->readLong( "Axis1GpioStepPin", 8 ),
        m_model->m_config->readLong( "Axis1GpioReversePin", 7 ),
        m_model->m_config->readLong( "Axis1GpioEnablePin", 0 ),
        axis1StepsPerRevolution,
        axis1ConversionFactor,
        std::abs( m_axis1MaxMotorSpeed / axis1ConversionFactor / axis1StepsPerRevolution )
        );

    double axis2ConversionFactor =
        m_model->m_config->readDouble( "Axis2ConversionNumerator", -1.0 ) /
            m_model->m_config->readDouble( "Axis2ConversionDivisor", 1'000.0 );
    m_axis2MaxMotorSpeed = m_model->m_config->readDouble( "Axis2MaxMotorSpeed", 240.0 );
    long axis2StepsPerRevolution = m_model->m_config->readLong( "Axis2StepsPerRev", 800 );
    m_model->m_axis2Motor = std::make_unique<mgo::StepperMotor>(
        m_model->m_gpio,
        m_model->m_config->readLong( "Axis2GpioStepPin", 20 ),
        m_model->m_config->readLong( "Axis2GpioReversePin", 21 ),
        m_model->m_config->readLong( "Axis2GpioEnablePin", 0 ),
        axis2StepsPerRevolution,
        axis2ConversionFactor,
        std::abs( m_axis2MaxMotorSpeed / axis2ConversionFactor / axis2StepsPerRevolution )
        );

    m_model->m_rotaryEncoder = std::make_unique<mgo::RotaryEncoder>(
        m_model->m_gpio,
        m_model->m_config->readLong(  "RotaryEncoderGpioPinA", 23 ),
        m_model->m_config->readLong(  "RotaryEncoderGpioPinB", 24 ),
        m_model->m_config->readLong(  "RotaryEncoderPulsesPerRev", 2'000 ),
        m_model->m_config->readDouble( "RotaryEncoderGearingNumerator", 35.0 ) /
            m_model->m_config->readDouble( "RotaryEncoderGearingDivisor", 30.0 )
        );

    // We need to ensure that the motors are in a known position with regard to
    // backlash - which means moving them initially by the amount of
    // configured backlash compensation to ensure any backlash is taken up
    // This initial movement will be small but this could cause an issue if
    // the tool is against work already - maybe TODO something here?
    unsigned long zBacklashCompensation =
        m_model->m_config->readLong( "Axis1BacklashCompensationSteps", 0 );
    unsigned long xBacklashCompensation =
        m_model->m_config->readLong( "Axis2BacklashCompensationSteps", 0 );
    m_model->m_axis1Motor->goToStep( zBacklashCompensation );
    m_model->m_axis1Motor->setBacklashCompensation( zBacklashCompensation, zBacklashCompensation );
    m_model->m_axis2Motor->goToStep( xBacklashCompensation );
    m_model->m_axis2Motor->setBacklashCompensation( xBacklashCompensation, xBacklashCompensation );
    m_model->m_axis1Motor->wait();
    m_model->m_axis2Motor->wait();
    // re-zero after that:
    m_model->m_axis1Motor->zeroPosition();
    m_model->m_axis2Motor->zeroPosition();
}

void Controller::run()
{
    m_model->m_axis1Motor->setSpeed( m_model->m_config->readDouble( "Axis1SpeedPreset2", 40.0 ) );
    m_model->m_axis2Motor->setSpeed( m_model->m_config->readDouble( "Axis2SpeedPreset2", 20.0 ) );

    while( ! m_model->m_quit )
    {
        processKeyPress();

        float chuckRpm = m_model->m_rotaryEncoder->getRpm();
        if( m_model->m_spindleWasRunning && chuckRpm < 30.f )
        {
            // If the chuck has stopped, we stop X/Z motors as a safety
            // feature, just in case the motor has stalled or the operator
            // has turned it off because of some issue.
            // This won't stop the motors being started again even
            // if the chuck isn't moving.
            m_model->stopAllMotors();
        }
        m_model->m_spindleWasRunning = chuckRpm > 30.f;

        if( m_model->m_enabledFunction == Mode::Threading )
        {
            // We are cutting threads, so the stepper motor's speed
            // is dependent on the spindle's RPM and the thread pitch.
            float pitch = threadPitches.at( m_model->m_threadPitchIndex ).pitchMm;
            // because my stepper motor / leadscrew does one mm per
            // revolution, there is a direct correlation between spindle
            // rpm and stepper motor rpm for a 1mm thread pitch.
            float speed = pitch * m_model->m_rotaryEncoder->getRpm();
            if( speed > m_axis1MaxMotorSpeed * 0.8 )
            {
                m_model->m_axis1Motor->stop();
                m_model->m_axis1Motor->wait();
                m_model->m_warning = "RPM too high for threading";
            }
            else
            {
                m_model->m_warning = "";
            }
            m_model->m_axis1Motor->setSpeed( speed );
        }

        if( m_model->m_currentDisplayMode == Mode::Taper )
        {
            if( m_model->m_input.empty() )
            {
                m_model->m_taperAngle = 0.0;
            }
            else
            {
                if( m_model->m_input != "-" )
                {
                    try
                    {
                        m_model->m_taperAngle = std::stod( m_model->m_input );
                        if( m_model->m_taperAngle > 60.0 )
                        {
                            m_model->m_taperAngle = 60.0;
                            m_model->m_input = "60.0";
                        }
                        else if( m_model->m_taperAngle < -60.0 )
                        {
                            m_model->m_taperAngle = -60.0;
                            m_model->m_input = "-60.0";
                        }
                    }
                    catch( const std::exception& )
                    {
                        m_model->m_taperAngle = 0.0;
                        m_model->m_input = "";
                    }
                }
            }
        }
        if( m_model->m_xDiameterSet )
        {
            m_model->m_generalStatus = fmt::format("Diameter: {: .3f} mm",
                std::abs( m_model->m_axis2Motor->getPosition() * 2 ) );
        }
        if ( ! m_model->m_axis2Motor->isRunning() )
        {
            m_model->m_axis2Status = "stopped";
        }
        if ( ! m_model->m_axis1Motor->isRunning() )
        {
            m_model->m_axis1Status = "stopped";
            if( m_model->m_axis1FastReturning )
            {
                m_model->m_axis1Motor->setSpeed( m_model->m_previousZSpeed );
                m_model->m_axis1FastReturning = false;
            }
            if( m_model->m_enabledFunction == Mode::Taper && m_model->m_zWasRunning )
            {
                m_model->m_axis2Motor->stop();
            }
            if( m_model->m_zWasRunning && m_model->m_axis1Motor->getRpm() >= 100.0 )
            {
                // We don't allow faster speeds to "stick" to avoid accidental
                // fast motion after a long fast movement
                m_model->m_axis1Motor->setSpeed(
                    m_model->m_config->readDouble( "Axis1SpeedPreset2", 40.0 ) );
            }
            m_model->m_zWasRunning = false;
        }
        else
        {
            m_model->m_zWasRunning = true;
        }

        if ( ! m_model->m_axis2Motor->isRunning() )
        {
            if( m_model->m_fastRetracting )
            {
                m_model->m_axis2Motor->setSpeed( m_model->m_previousXSpeed );
                m_model->m_fastRetracting = false;
                m_model->m_axis2Retracted = false;
            }
            if( m_model->m_axis2FastReturning )
            {
                m_model->m_axis2Motor->setSpeed( m_model->m_previousXSpeed );
                m_model->m_axis2FastReturning = false;
            }
            if( m_model->m_xWasRunning && m_model->m_axis2Motor->getSpeed() >= 80.0 )
            {
                // We don't allow faster speeds to "stick" to avoid accidental
                // fast motion after a long fast movement
                m_model->m_axis2Motor->setSpeed(
                    m_model->m_config->readDouble( "Axis2SpeedPreset2", 20.0 ) );
            }
            m_model->m_xWasRunning = false;
        }
        else
        {
            m_model->m_xWasRunning = true;
        }

        m_view->updateDisplay( *m_model );

        if( m_model->m_shutdown )
        {
            // Stop the motor threads
            m_model->m_axis2Motor.reset();
            m_model->m_axis1Motor.reset();
            // Note the command used for shutdown should be made passwordless
            // in the /etc/sudoers files
            system( "sudo shutdown -h now &" );
        }

        // Small delay just to avoid the UI loop spinning
        yieldSleep( std::chrono::microseconds( 50'000 ) );
    }
}

void Controller::processKeyPress()
{
    int t = m_view->getInput();
    t = checkKeyAllowedForMode( t );
    t = processModeInputKeys( t );
    if( t != key::None )
    {
        if( m_model->m_keyMode != KeyMode::None )
        {
            // If we are in a "leader key mode" (i.e. the first "prefix" key has
            // already been pressed) then we can modify t here. If the key
            // combination is recognised, we get a "chord" keypress e.g. key::xz.
            t = processLeaderKeyModeKeyPress( t );
            m_model->m_keyMode = KeyMode::None;
        }
        m_model->m_keyPressed = t;
        // Modify key press if it is a known axis leader key:
        m_model->m_keyPressed = checkForAxisLeaderKeys( m_model->m_keyPressed );
        switch( m_model->m_keyPressed )
        {
            case key::None:
            {
                break;
            }
            case key::F2:
            {
                m_model->m_keyMode = KeyMode::Function;
                break;
            }
            // End of "Leader" keys ==============
            case key::CtrlQ:
            {
                m_model->stopAllMotors();
                m_model->m_quit = true;
                break;
            }
            case key::a2_MINUS:
            {
                // X-axis speed decrease
                if( m_model->m_axis2Motor->getSpeed() > 10.1 )
                {
                    m_model->m_axis2Motor->setSpeed( m_model->m_axis2Motor->getSpeed() - 10.0 );
                }
                else if( m_model->m_axis2Motor->getSpeed() > 2.1 )
                {
                    m_model->m_axis2Motor->setSpeed( m_model->m_axis2Motor->getSpeed() - 2.0 );
                }
                break;
            }
            case key::a2_EQUALS:
            {
                // X-axis speed increase
                if( m_model->m_axis2Motor->getSpeed() < 10.0 )
                {
                    m_model->m_axis2Motor->setSpeed( m_model->m_axis2Motor->getSpeed() + 2.0 );
                }
                else if( m_model->m_axis2Motor->getSpeed() < m_axis2MaxMotorSpeed )
                {
                    m_model->m_axis2Motor->setSpeed( m_model->m_axis2Motor->getSpeed() + 10.0 );
                }
                break;
            }
            // Nudge in X axis
            case key::W:
            case key::w:
            {
                if ( m_model->m_axis2Motor->isRunning() )
                {
                    m_model->m_axis2Motor->stop();
                    m_model->m_axis2Motor->wait();
                }
                double nudgeValue = 60.0;
                if( m_model->m_keyPressed == key::W )
                {
                    // extra fine with shift
                    nudgeValue = 6.0;
                }
                if( m_model->m_config->readBool( "Axis2MotorFlipDirection", false ) )
                {
                    nudgeValue = -nudgeValue;
                }
                m_model->m_axis2Motor->goToStep(
                    m_model->m_axis2Motor->getCurrentStep() + nudgeValue );
                break;
            }
            // Nudge out X axis
            case key::S:
            case key::s:
            {
                if ( m_model->m_axis2Motor->isRunning() )
                {
                    m_model->m_axis2Motor->stop();
                    m_model->m_axis2Motor->wait();
                }
                double nudgeValue = 60.0;
                if( m_model->m_keyPressed == key::S )
                {
                    // extra fine with shift
                    nudgeValue = 6.0;
                }
                if( m_model->m_config->readBool( "Axis2MotorFlipDirection", false ) )
                {
                    nudgeValue = -nudgeValue;
                }
                m_model->m_axis2Motor->goToStep(
                    m_model->m_axis2Motor->getCurrentStep() - nudgeValue );
                break;
            }
            case key::a1_EQUALS:
            case key::EQUALS: // (i.e. plus)
            {
                if( m_model->m_enabledFunction == Mode::Threading ) break;
                if( m_model->m_axis1Motor->getRpm() < 20.0 )
                {
                    m_model->m_axis1Motor->setSpeed( m_model->m_axis1Motor->getRpm() + 1.0 );
                }
                else
                {
                    if( m_model->m_axis1Motor->getRpm() <= m_axis1MaxMotorSpeed - 20 )
                    {
                        m_model->m_axis1Motor->setSpeed( m_model->m_axis1Motor->getRpm() + 20.0 );
                    }
                }
                break;
            }
            case key::a1_MINUS:
            case key::MINUS:
            {
                if( m_model->m_enabledFunction == Mode::Threading ) break;
                if( m_model->m_axis1Motor->getRpm() > 20 )
                {
                    m_model->m_axis1Motor->setSpeed( m_model->m_axis1Motor->getRpm() - 20.0 );
                }
                else
                {
                    if ( m_model->m_axis1Motor->getRpm() > 1 )
                    {
                        m_model->m_axis1Motor->setSpeed( m_model->m_axis1Motor->getRpm() - 1.0 );
                    }
                }
                break;
            }
            case key::a1_m:
            {
                m_model->m_axis1Memory.at( m_model->m_currentMemory ) =
                    m_model->m_axis1Motor->getCurrentStep();
                break;
            }
            case key::a2_m:
            {
                m_model->m_axis2Memory.at( m_model->m_currentMemory ) =
                    m_model->m_axis2Motor->getCurrentStep();
                break;
            }
            case key::m:
            case key::M:
            {
                m_model->m_axis1Memory.at( m_model->m_currentMemory ) =
                    m_model->m_axis1Motor->getCurrentStep();
                m_model->m_axis2Memory.at( m_model->m_currentMemory ) =
                    m_model->m_axis2Motor->getCurrentStep();
                break;
            }
            case key::a2_ENTER:
            {
                if( m_model->m_axis2Memory.at( m_model->m_currentMemory ) == INF_RIGHT ) break;
                if( m_model->m_axis2Memory.at( m_model->m_currentMemory ) ==
                    m_model->m_axis2Motor->getCurrentStep() ) break;
                m_model->m_axis2Motor->stop();
                m_model->m_axis2Status = "returning";
                m_model->m_axis2Motor->goToStep(
                    m_model->m_axis2Memory.at( m_model->m_currentMemory ) );
                break;
            }
            case key::ENTER:
            case key::a1_ENTER:
            {
                if( m_model->m_axis1Memory.at( m_model->m_currentMemory ) == INF_RIGHT ) break;
                if( m_model->m_axis1Memory.at( m_model->m_currentMemory ) ==
                    m_model->m_axis1Motor->getCurrentStep() ) break;
                m_model->m_axis1Motor->stop();
                m_model->m_axis1Motor->wait();
                m_model->m_axis1Status = "returning";
                // Ensure z backlash is compensated first for tapering or threading:
                ZDirection direction;
                if( m_model->m_axis1Memory.at( m_model->m_currentMemory ) <
                    m_model->m_axis1Motor->getCurrentStep() )
                {
                    // Z motor is moving away from chuck
                    // NOTE!! Memory is stored as STEPS which, on the Z-axis is
                    // reversed from POSITION (see stepper's getPosition() vs getCurrentStep())
                    // so the direction we save is reversed
                    direction = ZDirection::Right;
                }
                else
                {
                    // Z motor is moving towards chuck
                    direction = ZDirection::Left;
                }
                m_model->takeUpZBacklash( direction );
                m_model->m_axis1Motor->wait();
                // If threading, we need to start at the same point each time - we
                // wait for zero degrees on the chuck before starting:
                if( m_model->m_enabledFunction == Mode::Threading )
                {
                    m_model->m_rotaryEncoder->callbackAtZeroDegrees([&]()
                        {
                            m_model->m_axis1Motor->goToStep(
                                m_model->m_axis1Memory.at( m_model->m_currentMemory ) );
                        }
                        );
                }
                else
                {
                    if( m_model->m_enabledFunction == Mode::Taper )
                    {
                        m_model->startSynchronisedXMotor(
                            direction, m_model->m_axis1Motor->getSpeed() );
                    }
                    m_model->m_axis1Motor->goToStep(
                        m_model->m_axis1Memory.at( m_model->m_currentMemory ) );
                }
                break;
            }
            case key::UP:
            {
                if( m_model->m_axis2Motor->isRunning() )
                {
                    m_model->m_axis2Motor->stop();
                }
                else
                {
                    m_model->m_axis2Status = "moving in";
                    if( m_model->m_config->readBool( "Axis2MotorFlipDirection", false ) )
                    {
                        m_model->m_axis2Motor->goToStep( INF_OUT );
                    }
                    else
                    {
                        m_model->m_axis2Motor->goToStep( INF_IN );
                    }
                }
                break;
            }
            case key::DOWN:
            {
                if( m_model->m_axis2Motor->isRunning() )
                {
                    m_model->m_axis2Motor->stop();
                }
                else
                {
                    m_model->m_axis2Status = "moving out";
                    if( m_model->m_config->readBool( "Axis2MotorFlipDirection", false ) )
                    {
                        m_model->m_axis2Motor->goToStep( INF_IN );
                    }
                    else
                    {
                        m_model->m_axis2Motor->goToStep( INF_OUT );
                    }
                }
                break;
            }
            case key::LEFT:
            {
                // Same key will cancel if we're already moving
                if ( m_model->m_axis1Motor->isRunning() )
                {
                    m_model->m_axis1Motor->stop();
                    m_model->m_axis1Motor->wait();
                    break;
                }
                m_model->m_axis1Status = "moving left";
                if( m_model->m_enabledFunction == Mode::Taper )
                {
                    m_model->takeUpZBacklash( ZDirection::Left );
                    m_model->startSynchronisedXMotor(
                        ZDirection::Left, m_model->m_axis1Motor->getSpeed() );
                }
                if( m_model->m_config->readBool( "Axis1MotorFlipDirection", false ) )
                {
                    m_model->m_axis1Motor->goToStep( INF_RIGHT );
                }
                else
                {
                    m_model->m_axis1Motor->goToStep( INF_LEFT );
                }
                break;
            }
            case key::RIGHT:
            {
                // Same key will cancel if we're already moving
                if ( m_model->m_axis1Motor->isRunning() )
                {
                    m_model->m_axis1Motor->stop();
                    break;
                }
                m_model->m_axis1Status = "moving right";
                if( m_model->m_enabledFunction == Mode::Taper )
                {
                    m_model->takeUpZBacklash( ZDirection::Right );
                    m_model->startSynchronisedXMotor(
                        ZDirection::Right, m_model->m_axis1Motor->getSpeed() );
                }
                if( m_model->m_config->readBool( "Axis1MotorFlipDirection", false ) )
                {
                    m_model->m_axis1Motor->goToStep( INF_LEFT );
                }
                else
                {
                    m_model->m_axis1Motor->goToStep( INF_RIGHT );
                }
                break;
            }
            case key::a:
            case key::A:
            {
                if ( m_model->m_axis1Motor->isRunning() )
                {
                    m_model->m_axis1Motor->stop();
                    m_model->m_axis1Motor->wait();
                }
                long nudgeValue = 25L;
                if( m_model->m_keyPressed == key::A ) // extra fine with shift
                {
                    nudgeValue = 2L;
                }
                if( m_model->m_config->readBool( "Axis1MotorFlipDirection", false ) )
                {
                    nudgeValue = -nudgeValue;
                }
                m_model->m_axis1Motor->goToStep(
                    m_model->m_axis1Motor->getCurrentStep() + nudgeValue );
                break;
            }
            case key::d:
            case key::D:
            {
                if ( m_model->m_axis1Motor->isRunning() )
                {
                    m_model->m_axis1Motor->stop();
                    m_model->m_axis1Motor->wait();
                }
                long nudgeValue = 25L;
                if( m_model->m_keyPressed == key::D ) // extra fine with shift
                {
                    nudgeValue = 2L;
                }
                if( m_model->m_config->readBool( "Axis1MotorFlipDirection", false ) )
                {
                    nudgeValue = -nudgeValue;
                }
                m_model->m_axis1Motor->goToStep(
                    m_model->m_axis1Motor->getCurrentStep() - nudgeValue );
                break;
            }
            case key::LBRACKET: // [
            {
                if( m_model->m_currentMemory > 0 )
                {
                    --m_model->m_currentMemory;
                }
                break;
            }
            case key::RBRACKET: // ]
            {
                if( m_model->m_currentMemory < m_model->m_axis1Memory.size() - 1 )
                {
                    ++m_model->m_currentMemory;
                }
                break;
            }

            // Speed presets for Z with number keys 1-5
            case key::ONE:
            {
                if( m_model->m_currentDisplayMode != Mode::Threading )
                {
                    m_model->m_axis1Motor->setSpeed(
                        m_model->m_config->readDouble( "Axis1SpeedPreset1", 20.0 )
                        );
                }
                break;
            }
            case key::TWO:
            {
                if( m_model->m_currentDisplayMode != Mode::Threading )
                {
                    m_model->m_axis1Motor->setSpeed(
                        m_model->m_config->readDouble( "Axis1SpeedPreset2", 40.0 )
                        );
                }
                break;
            }
            case key::THREE:
            {
                if( m_model->m_currentDisplayMode != Mode::Threading )
                {
                    m_model->m_axis1Motor->setSpeed(
                        m_model->m_config->readDouble( "Axis1SpeedPreset3", 100.0 )
                        );
                }
                break;
            }
            case key::FOUR:
            {
                if( m_model->m_currentDisplayMode != Mode::Threading )
                {
                    m_model->m_axis1Motor->setSpeed(
                        m_model->m_config->readDouble( "Axis1SpeedPreset4", 250.0 )
                        );
                }
                break;
            }
            case key::FIVE:
            {
                if( m_model->m_currentDisplayMode != Mode::Threading )
                {
                    m_model->m_axis1Motor->setSpeed(
                        m_model->m_config->readDouble( "Axis1SpeedPreset5", m_axis1MaxMotorSpeed )
                        );
                }
                break;
            }
            // Speed presets for X with number keys 6-0 or X leader + 1-5
            case key::a2_1:
            case key::SIX:
            {
                if( m_model->m_currentDisplayMode != Mode::Threading )
                {
                    m_model->m_axis2Motor->setSpeed(
                        m_model->m_config->readDouble( "Axis2SpeedPreset1", 5.0 )
                        );
                }
                break;
            }
            case key::a2_2:
            case key::SEVEN:
            {
                if( m_model->m_currentDisplayMode != Mode::Threading )
                {
                    m_model->m_axis2Motor->setSpeed(
                        m_model->m_config->readDouble( "Axis2SpeedPreset2", 20.0 )
                        );
                }
                break;
            }
            case key::a2_3:
            case key::EIGHT:
            {
                if( m_model->m_currentDisplayMode != Mode::Threading )
                {
                    m_model->m_axis2Motor->setSpeed(
                        m_model->m_config->readDouble( "Axis2SpeedPreset3", 40.0 )
                        );
                }
                break;
            }
            case key::a2_4:
            case key::NINE:
            {
                if( m_model->m_currentDisplayMode != Mode::Threading )
                {
                    m_model->m_axis2Motor->setSpeed(
                        m_model->m_config->readDouble( "Axis2SpeedPreset4", 80.0 )
                        );
                }
                break;
            }
            case key::a2_5:
            case key::ZERO:
            {
                if( m_model->m_currentDisplayMode != Mode::Threading )
                {
                    m_model->m_axis2Motor->setSpeed(
                        m_model->m_config->readDouble( "Axis2SpeedPreset5", m_axis2MaxMotorSpeed )
                        );
                }
                break;
            }
            case key::f:
            case key::F:
            case key::a1_f:
            {
                // Fast return to point
                if( m_model->m_axis1Memory.at( m_model->m_currentMemory ) == INF_RIGHT ) break;
                if( m_model->m_axis1FastReturning ) break;
                m_model->m_previousZSpeed = m_model->m_axis1Motor->getSpeed();
                m_model->m_axis1FastReturning = true;
                m_model->m_axis1Motor->stop();
                m_model->m_axis1Motor->wait();
                if( m_model->m_enabledFunction == Mode::Taper )
                {
                    // If we are tapering, we need to set a speed the x-axis motor can keep up with
                    m_model->m_axis1Motor->setSpeed( 100.0 );
                    ZDirection direction = ZDirection::Left;
                    if( m_model->m_axis1Memory.at( m_model->m_currentMemory ) <
                        m_model->m_axis1Motor->getCurrentStep() )
                    {
                        direction = ZDirection::Right;
                    }
                    m_model->takeUpZBacklash( direction );
                    m_model->startSynchronisedXMotor(
                        direction, m_model->m_axis1Motor->getSpeed() );
                }
                else
                {
                    m_model->m_axis1Motor->setSpeed( m_model->m_axis1Motor->getMaxRpm() );
                }
                m_model->m_axis1Status = "fast returning";
                m_model->m_axis1Motor->goToStep(
                    m_model->m_axis1Memory.at( m_model->m_currentMemory ) );
                break;
            }
            case key::a2_f:
            {
                // Fast return to point
                if( m_model->m_axis2Memory.at( m_model->m_currentMemory ) == INF_RIGHT ) break;
                if( m_model->m_axis2FastReturning ) break;
                m_model->m_previousZSpeed = m_model->m_axis2Motor->getSpeed();
                m_model->m_axis2FastReturning = true;
                m_model->m_axis2Motor->stop();
                m_model->m_axis2Motor->wait();
                m_model->m_axis2Motor->setSpeed( m_model->m_axis2Motor->getMaxRpm() );
                m_model->m_axis2Status = "fast returning";
                m_model->m_axis2Motor->goToStep(
                    m_model->m_axis2Memory.at( m_model->m_currentMemory ) );
                break;
            }
            case key::r:
            case key::R:
            {
                // X retraction
                if( m_model->m_config->readBool( "DisableAxis2", false ) ) break;
                if( m_model->m_axis2Motor->isRunning() ) break;
                if( m_model->m_enabledFunction == Mode::Taper ) break;
                if( m_model->m_axis2Retracted )
                {
                    // Return
                    m_model->m_axis2Motor->setSpeed( 100.0 );
                    m_model->m_axis2Motor->goToStep( m_model->m_xOldPosition );
                    m_model->m_axis2Status = "Unretracting";
                    m_model->m_fastRetracting = true;
                }
                else
                {
                    m_model->m_xOldPosition = m_model->m_axis2Motor->getCurrentStep();
                    m_model->m_previousXSpeed = m_model->m_axis2Motor->getSpeed();
                    m_model->m_axis2Motor->setSpeed( 100.0 );
                    int direction = -1;
                    if( m_model->m_xRetractionDirection == XRetractionDirection::Inwards )
                    {
                        direction = 1;
                    }
                    long stepsForRetraction =
                        2.0 / std::abs( m_model->m_axis2Motor->getConversionFactor() );
                    m_model->m_axis2Motor->goToStep(
                        m_model->m_axis2Motor->getCurrentStep() + stepsForRetraction * direction );
                    m_model->m_axis2Retracted = true;
                    m_model->m_axis2Status = "Retracting";
                }
                break;
            }
            case key::a1_z:
            {
                if( m_model->m_enabledFunction == Mode::Taper )
                {
                    m_model->changeMode( Mode::None );
                }
                m_model->m_axis1Motor->zeroPosition();
                // Zeroing will invalidate any memorised Z positions, so we clear them
                for( auto& m : m_model->m_axis1Memory )
                {
                    m = INF_RIGHT;
                }
                break;
            }
            case key::a2_z:
            {
                if( m_model->m_enabledFunction == Mode::Taper )
                {
                    m_model->changeMode( Mode::None );
                }
                m_model->m_axis2Motor->zeroPosition();
                // Zeroing will invalidate any memorised X positions, so we clear them
                for( auto& m : m_model->m_axis2Memory )
                {
                    m = INF_OUT;
                }
                break;
            }
            case key::a1_g:
            {
                m_model->changeMode( Mode::Axis1GoTo );
                break;
            }
            case key::a2_g:
            {
                m_model->changeMode( Mode::Axis2GoTo );
                break;
            }
            case key::ASTERISK: // shutdown
            {
                #ifndef FAKE
                m_model->stopAllMotors();
                m_model->m_quit = true;
                m_model->m_shutdown = true;
                #endif
                break;
            }
            case key::F1: // help mode
            case key::f2h:
            {
                m_model->changeMode( Mode::Help );
                break;
            }
            case key::f2s: // setup mode
            {
                m_model->m_enabledFunction = Mode::None;
                m_model->m_axis1Motor->setSpeed( 0.8f );
                m_model->m_axis2Motor->setSpeed( 1.f );
                m_model->changeMode( Mode::Setup );
                break;
            }
            case key::f2t: // threading mode
            {
                if( m_model->m_config->readBool( "DisableAxis2", false ) ) break;
                m_model->changeMode( Mode::Threading );
                break;
            }
            case key::f2p: // taper mode
            {
                if( m_model->m_config->readBool( "DisableAxis2", false ) ) break;
                m_model->changeMode( Mode::Taper );
                break;
            }
            case key::f2r: // X retraction setup
            {
                if( m_model->m_config->readBool( "DisableAxis2", false ) ) break;
                m_model->changeMode( Mode::Axis2RetractSetup );
                break;
            }
            case key::a2_s: // X position set
            {
                m_model->changeMode( Mode::Axis2PositionSetup );
                break;
            }
            case key::a1_s: // Z position set
            {
                m_model->changeMode( Mode::Axis1PositionSetup );
                break;
            }
            case key::ESC: // return to normal mode
            {
                // Cancel any retract as well
                m_model->m_axis2Retracted = false;
                m_model->changeMode( Mode::None );
                break;
            }
            default: // e.g. space bar to stop all motors
            {
                m_model->stopAllMotors();
                break;
            }
        }
    }
}

int Controller::checkKeyAllowedForMode( int key )
{
    // The mode means some keys are ignored, for instance in
    // threading, you cannot change the speed of the z-axis as
    // that would affect the thread pitch
    // Always allow certain keys:
    if( key == key::CtrlQ  ||
        key == key::ESC    ||
        key == key::ENTER
        )
    {
        return key;
    }
    // Don't allow any x movement when retracted
    if( m_model->m_axis2Retracted && ( key == key::UP || key == key::DOWN || key == key::W
        || key == key::w || key == key::s || key == key::S ) )
    {
        return -1;
    }
    switch( m_model->m_currentDisplayMode )
    {
        case Mode::None:
            return key;
        case Mode::Help:
            return key;
        case Mode::Setup:
            if( key == key::LEFT || key == key::RIGHT || key == key::UP || key == key::DOWN )
            {
                return key;
            }
            if( key == key::SPACE ) return key;
            return -1;
        case Mode::Threading:
            if( key == key::UP || key == key::DOWN ) return key;
            return -1;
        case Mode::Axis2RetractSetup:
            if( key == key::UP || key == key::DOWN ) return key;
            return -1;
        // Any modes that have numerical input:
        case Mode::Axis2PositionSetup:
            if( key >= key::ZERO && key <= key::NINE ) return key;
            if( key == key::FULLSTOP || key == key::BACKSPACE || key == key::DELETE ) return key;
            if( key == key::MINUS ) return key;
            if( key == key::d || key == key::D ) return key;
            return -1;
        case Mode::Axis1PositionSetup:
        case Mode::Taper:
        case Mode::Axis1GoTo:
        case Mode::Axis2GoTo:
            if( key >= key::ZERO && key <= key::NINE ) return key;
            if( key == key::FULLSTOP || key == key::BACKSPACE || key == key::DELETE ) return key;
            if( key == key::MINUS ) return key;
            return -1;
        default:
            // unhandled mode
            assert( false );
    }
}

int Controller::processModeInputKeys( int key )
{
    // If we are in a "mode" then certain keys (e.g. the number keys) are used for input
    // so are processed here before allowing them to fall through to the main key processing
    if( m_model->m_currentDisplayMode == Mode::Taper ||
        m_model->m_currentDisplayMode == Mode::Axis2PositionSetup ||
        m_model->m_currentDisplayMode == Mode::Axis1PositionSetup ||
        m_model->m_currentDisplayMode == Mode::Axis1GoTo ||
        m_model->m_currentDisplayMode == Mode::Axis2GoTo
        )
    {
        if( key >= key::ZERO && key <= key::NINE )
        {
            m_model->m_input += static_cast<char>( key );
            return -1;
        }
        if( key == key::FULLSTOP )
        {
            if( m_model->m_input.find( "." ) == std::string::npos )
            {
                m_model->m_input += static_cast<char>( key );
            }
            return -1;
        }
        if( key == key::DELETE )
        {
            m_model->m_input = "";
        }
        if( key == key::BACKSPACE )
        {
            if( ! m_model->m_input.empty() )
            {
                m_model->m_input.pop_back();
            }
            return -1;
        }
        if( key == key::MINUS && m_model->m_input.empty() )
        {
            m_model->m_input = "-";
        }
    }
    if(  m_model->m_currentDisplayMode == Mode::Threading )
    {
        if( key == key::UP )
        {
            if( m_model->m_threadPitchIndex == 0 )
            {
                m_model->m_threadPitchIndex = threadPitches.size() - 1;
            }
            else
            {
                --m_model->m_threadPitchIndex;
            }
            return -1;
        }
        if( key == key::DOWN )
        {
            if( m_model->m_threadPitchIndex == threadPitches.size() - 1 )
            {
                m_model->m_threadPitchIndex = 0;
            }
            else
            {
                ++m_model->m_threadPitchIndex;
            }
            return -1;
        }
        if( key == key::ESC )
        {
            // Reset motor speed to something sane
            m_model->m_axis1Motor->setSpeed(
                m_model->m_config->readDouble( "Axis1SpeedPreset2", 40.0 ) );
            // fall through...
        }
    }
    if(  m_model->m_currentDisplayMode == Mode::Axis2RetractSetup )
    {
        if( key == key::UP )
        {
            m_model->m_xRetractionDirection = XRetractionDirection::Inwards;
            return -1;
        }
        if( key == key::DOWN )
        {
            m_model->m_xRetractionDirection = XRetractionDirection::Outwards;
            return -1;
        }
    }

    // Diameter set
    if( m_model->m_currentDisplayMode == Mode::Axis2PositionSetup &&
        ( key == key::d || key == key::D ) )
    {
        float xPos = 0;
        try
        {
            xPos = - std::abs( std::stof( m_model->m_input ) );
        }
        catch( ... ) {}
        m_model->m_axis2Motor->setPosition( xPos / 2 );
        // This will invalidate any memorised X positions, so we clear them
        for( auto& m : m_model->m_axis2Memory )
        {
            m = INF_OUT;
        }
        m_model->m_currentDisplayMode = Mode::None;
        m_model->m_xDiameterSet = true;
        return -1;
    }

    if( m_model->m_currentDisplayMode != Mode::None && key == key::ENTER )
    {
        if( m_model->m_currentDisplayMode == Mode::Axis2PositionSetup )
        {
            float xPos = 0;
            try
            {
                xPos = std::stof( m_model->m_input );
            }
            catch( ... ) {}
            m_model->m_axis2Motor->setPosition( xPos );
            // This will invalidate any memorised X positions, so we clear them
            for( auto& m : m_model->m_axis2Memory )
            {
                m = INF_OUT;
            }
        }

        if( m_model->m_currentDisplayMode == Mode::Axis1PositionSetup )
        {
            float zPos = 0;
            try
            {
                zPos = std::stof( m_model->m_input );
            }
            catch( ... ) {}
            m_model->m_axis1Motor->setPosition( zPos );
            // This will invalidate any memorised Z positions, so we clear them
            for( auto& m : m_model->m_axis1Memory )
            {
                m = INF_RIGHT;
            }
        }

        if( m_model->m_currentDisplayMode == Mode::Axis1GoTo )
        {
            float pos = 0;
            bool valid = true;
            try
            {
                pos = std::stof( m_model->m_input );
            }
            catch( ... )
            {
                valid = false;
            }
            if( valid )
            {
                m_model->m_axis1Motor->goToPosition( pos );
                m_model->m_axis1Status = fmt::format( "Going to {}", pos );
            }
        }

        if( m_model->m_currentDisplayMode == Mode::Axis2GoTo )
        {
            float pos = 0;
            bool valid = true;
            try
            {
                pos = std::stof( m_model->m_input );
            }
            catch( ... )
            {
                valid = false;
            }
            if( valid )
            {
                m_model->m_axis2Motor->goToPosition( pos );
                m_model->m_axis2Status = fmt::format( "Going to {}", pos );
            }
        }

        m_model->m_currentDisplayMode = Mode::None;
        return -1;
    }
    return key;
}

int Controller::processLeaderKeyModeKeyPress( int keyPress )
{
    if( m_model->m_keyMode == KeyMode::Axis1 ||
        m_model->m_keyMode == KeyMode::Axis2 )
    {
        int bitFlip = 4096; // bit 12
        if( m_model->m_keyMode == KeyMode::Axis2 )
        {
            bitFlip = 8192; // bit 13
        }
        return keyPress + bitFlip;
    }

    if( m_model->m_keyMode == KeyMode::Function )
    {
        switch( keyPress )
        {
            case key::h:
            case key::H:
                keyPress = key::f2h;
                break;
            case key::s:
            case key::S:
                keyPress = key::f2s;
                break;
            case key::t:
            case key::T:
                keyPress = key::f2t;
                break;
            case key::p:
            case key::P:
            case key::FULLSTOP:  // > - looks like a taper
            case key::COMMA:     // < - looks like a taper
                keyPress = key::f2p;
                break;
            case key::r:
            case key::R:
                keyPress = key::f2r;
                break;
            default:
                keyPress = key::None;
        }
    }
    m_model->m_keyMode = KeyMode::None;
    return keyPress;
}

int Controller::checkForAxisLeaderKeys( int key )
{
    // We determine whether the key pressed is a leader key for
    // an axis (note the key can be remapped in config) and sets
    // the model's m_keyMode variable if so. Returns true if one
    // was pressed, false if not.
    if( key == static_cast<int>( m_model->m_config->readLong( "Axis1Leader", 122L ) ) )
    {
        m_model->m_keyMode = KeyMode::Axis1;
        return key::None;
    }
    if( key == static_cast<int>( m_model->m_config->readLong( "Axis2Leader", 120L ) ) )
    {
        m_model->m_keyMode = KeyMode::Axis2;
        return key::None;
    }
    return key;
}

} // end namespace
