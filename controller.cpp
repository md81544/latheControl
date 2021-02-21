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

    m_zMaxMotorSpeed = m_model->m_config->readDouble( "zMaxMotorSpeed", 700.0 );
    m_xMaxMotorSpeed = m_model->m_config->readDouble( "xMaxMotorSpeed", 240.0 );

    m_view->initialise();

    m_view->updateDisplay( *m_model ); // get SFML running before we start the motor threads

    // TODO: currently ignoring enable pin
    m_model->m_zAxisMotor = std::make_unique<mgo::StepperMotor>(
        m_model->m_gpio, 8, 7, 0, 1'000, -0.001, m_zMaxMotorSpeed );
    // My X-Axis motor is set to 800 steps per revolution and the gearing
    // is 3:1 so 2,400 steps make one revolution of the X-axis handwheel,
    // which would be 1mm of travel. TODO: this should be settable in config
    m_model->m_xAxisMotor = std::make_unique<mgo::StepperMotor>(
        m_model->m_gpio, 20, 21, 0, 800, 1.0 / 2'400.0, m_xMaxMotorSpeed );
    m_model->m_rotaryEncoder = std::make_unique<mgo::RotaryEncoder>(
        m_model->m_gpio, 23, 24, 2000, 35.f/30.f );

    // We need to ensure that the motors are in a known position with regard to
    // backlash - which means moving them initially by the amount of
    // configured backlash compensation to ensure any backlash is taken up
    // This initial movement will be small but this could cause an issue if
    // the tool is against work already - maybe TODO something here?
    unsigned long zBacklashCompensation =
        m_model->m_config->readLong( "ZAxisBacklashCompensationSteps", 0 );
    unsigned long xBacklashCompensation =
        m_model->m_config->readLong( "XAxisBacklashCompensationSteps", 0 );
    m_model->m_zAxisMotor->goToStep( zBacklashCompensation );
    m_model->m_zAxisMotor->setBacklashCompensation( zBacklashCompensation, zBacklashCompensation );
    m_model->m_xAxisMotor->goToStep( xBacklashCompensation );
    m_model->m_xAxisMotor->setBacklashCompensation( xBacklashCompensation, xBacklashCompensation );
    m_model->m_zAxisMotor->wait();
    m_model->m_xAxisMotor->wait();
    // re-zero after that:
    m_model->m_zAxisMotor->zeroPosition();
    m_model->m_xAxisMotor->zeroPosition();
}

void Controller::run()
{
    m_model->m_zAxisMotor->setSpeed( 40.f );
    m_model->m_xAxisMotor->setSpeed( 40.f );

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
            stopAllMotors();
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
            if( speed > m_zMaxMotorSpeed )
            {
                m_model->m_zAxisMotor->stop();
                m_model->m_zAxisMotor->wait();
                m_model->m_warning = "RPM too high for threading";
            }
            else
            {
                m_model->m_warning = "";
            }
            m_model->m_zAxisMotor->setSpeed( speed );
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
        if ( ! m_model->m_xAxisMotor->isRunning() )
        {
            m_model->m_xStatus = "stopped";
        }
        if ( ! m_model->m_zAxisMotor->isRunning() )
        {
            m_model->m_zStatus = "stopped";
            if( m_model->m_fastReturning )
            {
                m_model->m_zAxisMotor->setSpeed( m_model->m_previousZSpeed );
                m_model->m_fastReturning = false;
            }
            if( m_model->m_enabledFunction == Mode::Taper && m_model->m_zWasRunning )
            {
                m_model->m_xAxisMotor->stop();
            }
            if( m_model->m_zWasRunning && m_model->m_zAxisMotor->getRpm() >= 100.0 )
            {
                // We don't allow faster speeds to "stick" to avoid accidental
                // fast motion after a long fast movement
                m_model->m_zAxisMotor->setSpeed( 40.0 );
            }
            m_model->m_zWasRunning = false;
        }
        else
        {
            m_model->m_zWasRunning = true;
        }

        if ( ! m_model->m_xAxisMotor->isRunning() )
        {
            if( m_model->m_fastRetracting )
            {
                m_model->m_xAxisMotor->setSpeed( m_model->m_previousXSpeed );
                m_model->m_fastRetracting = false;
                m_model->m_xRetracted = false;
            }
            if( m_model->m_xWasRunning && m_model->m_xAxisMotor->getSpeed() >= 80.0 )
            {
                // We don't allow faster speeds to "stick" to avoid accidental
                // fast motion after a long fast movement
                m_model->m_xAxisMotor->setSpeed( 40.0 );
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
            m_model->m_xAxisMotor.reset();
            m_model->m_zAxisMotor.reset();
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
        }
        m_model->m_keyPressed = t;
        switch( m_model->m_keyPressed )
        {
            case key::None:
            {
                break;
            }
            // "Leader" keys ====================
            case key::z:
            case key::Z:
            {
                m_model->m_keyMode = KeyMode::ZAxis;
                break;
            }
            case key::x:
            case key::X:
            {
                m_model->m_keyMode = KeyMode::XAxis;
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
                stopAllMotors();
                m_model->m_quit = true;
                break;
            }
            case key::C: // upper case, i.e. shift-c
            {
                // X-axis speed decrease
                if( m_model->m_xAxisMotor->getSpeed() > 10.1 )
                {
                    m_model->m_xAxisMotor->setSpeed( m_model->m_xAxisMotor->getSpeed() - 10.0 );
                }
                else if( m_model->m_xAxisMotor->getSpeed() > 2.1 )
                {
                    m_model->m_xAxisMotor->setSpeed( m_model->m_xAxisMotor->getSpeed() - 2.0 );
                }
                break;
            }
            case key::c:
            {
                // X-axis speed increase
                if( m_model->m_xAxisMotor->getSpeed() < 10.0 )
                {
                    m_model->m_xAxisMotor->setSpeed( 10.0 );
                }
                else if( m_model->m_xAxisMotor->getSpeed() < m_xMaxMotorSpeed )
                {
                    m_model->m_xAxisMotor->setSpeed( m_model->m_xAxisMotor->getSpeed() + 10.0 );
                }
                break;
            }
            // Nudge in X axis
            case key::W:
            case key::w:
            {
                if ( m_model->m_xAxisMotor->isRunning() )
                {
                    m_model->m_xAxisMotor->stop();
                    m_model->m_xAxisMotor->wait();
                }
                if( m_model->m_keyPressed == key::W )
                {
                    // extra fine with shift
                    m_model->m_xAxisMotor->goToStep(
                        m_model->m_xAxisMotor->getCurrentStep() + 12.0 );
                }
                else
                {
                    m_model->m_xAxisMotor->goToStep(
                        m_model->m_xAxisMotor->getCurrentStep() + 60.0 );
                }
                break;
            }
            // Nudge out X axis
            case key::S:
            case key::s:
            {
                if ( m_model->m_xAxisMotor->isRunning() )
                {
                    m_model->m_xAxisMotor->stop();
                    m_model->m_xAxisMotor->wait();
                }
                if( m_model->m_keyPressed == key::S )
                {
                    // extra fine with shift
                    m_model->m_xAxisMotor->goToStep(
                        m_model->m_xAxisMotor->getCurrentStep() - 12.0 );
                }
                else
                {
                    m_model->m_xAxisMotor->goToStep(
                        m_model->m_xAxisMotor->getCurrentStep() - 60.0 );
                }
                break;
            }
            case key::EQUALS: // (i.e. plus)
            {
                if( m_model->m_enabledFunction == Mode::Threading ) break;
                if( m_model->m_zAxisMotor->getRpm() < 20.0 )
                {
                    m_model->m_zAxisMotor->setSpeed( 20.0 );
                }
                else
                {
                    if( m_model->m_zAxisMotor->getRpm() < m_zMaxMotorSpeed )
                    {
                        m_model->m_zAxisMotor->setSpeed( m_model->m_zAxisMotor->getRpm() + 20.0 );
                    }
                }
                break;
            }
            case key::MINUS:
            {
                if( m_model->m_enabledFunction == Mode::Threading ) break;
                if( m_model->m_zAxisMotor->getRpm() > 20 )
                {
                    m_model->m_zAxisMotor->setSpeed( m_model->m_zAxisMotor->getRpm() - 20.0 );
                }
                else
                {
                    if ( m_model->m_zAxisMotor->getRpm() > 1 )
                    {
                        m_model->m_zAxisMotor->setSpeed( m_model->m_zAxisMotor->getRpm() - 1.0 );
                    }
                }
                break;
            }
            case key::zm:
            {
                m_model->m_zMemory.at( m_model->m_currentMemory ) =
                    m_model->m_zAxisMotor->getCurrentStep();
                break;
            }
            case key::xm:
            {
                m_model->m_xMemory.at( m_model->m_currentMemory ) =
                    m_model->m_xAxisMotor->getCurrentStep();
                break;
            }
            case key::m:
            case key::M:
            {
                m_model->m_zMemory.at( m_model->m_currentMemory ) =
                    m_model->m_zAxisMotor->getCurrentStep();
                m_model->m_xMemory.at( m_model->m_currentMemory ) =
                    m_model->m_xAxisMotor->getCurrentStep();
                break;
            }
            case key::xg:
            {
                if( m_model->m_xMemory.at( m_model->m_currentMemory ) == INF_RIGHT ) break;
                if( m_model->m_xMemory.at( m_model->m_currentMemory ) ==
                    m_model->m_xAxisMotor->getCurrentStep() ) break;
                m_model->m_xAxisMotor->stop();
                m_model->m_xStatus = "returning";
                m_model->m_xAxisMotor->goToStep(
                    m_model->m_xMemory.at( m_model->m_currentMemory ) );
                break;
            }
            case key::ENTER:
            case key::zg:
            {
                if( m_model->m_zMemory.at( m_model->m_currentMemory ) == INF_RIGHT ) break;
                if( m_model->m_zMemory.at( m_model->m_currentMemory ) ==
                    m_model->m_zAxisMotor->getCurrentStep() ) break;
                m_model->m_zAxisMotor->stop();
                m_model->m_zAxisMotor->wait();
                m_model->m_zStatus = "returning";
                // Ensure z backlash is compensated first for tapering or threading:
                ZDirection direction;
                if( m_model->m_zMemory.at( m_model->m_currentMemory ) <
                    m_model->m_zAxisMotor->getCurrentStep() )
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
                takeUpZBacklash( direction );
                m_model->m_zAxisMotor->wait();
                // If threading, we need to start at the same point each time - we
                // wait for zero degrees on the chuck before starting:
                if( m_model->m_enabledFunction == Mode::Threading )
                {
                    m_model->m_rotaryEncoder->callbackAtZeroDegrees([&]()
                        {
                            m_model->m_zAxisMotor->goToStep(
                                m_model->m_zMemory.at( m_model->m_currentMemory ) );
                        }
                        );
                }
                else
                {
                    if( m_model->m_enabledFunction == Mode::Taper )
                    {
                        startSynchronisedXMotor( direction, m_model->m_zAxisMotor->getSpeed() );
                    }
                    m_model->m_zAxisMotor->goToStep(
                        m_model->m_zMemory.at( m_model->m_currentMemory ) );
                }
                break;
            }
            case key::UP:
            {
                if( m_model->m_xAxisMotor->isRunning() )
                {
                    m_model->m_xAxisMotor->stop();
                }
                else
                {
                    m_model->m_xStatus = "moving in";
                    m_model->m_xAxisMotor->goToStep( INF_IN );
                }
                break;
            }
            case key::DOWN:
            {
                if( m_model->m_xAxisMotor->isRunning() )
                {
                    m_model->m_xAxisMotor->stop();
                }
                else
                {
                    m_model->m_xStatus = "moving out";
                    m_model->m_xAxisMotor->goToStep( INF_OUT );
                }
                break;
            }
            case key::LEFT:
            {
                // Same key will cancel if we're already moving
                if ( m_model->m_zAxisMotor->isRunning() )
                {
                    m_model->m_zAxisMotor->stop();
                    m_model->m_zAxisMotor->wait();
                    break;
                }
                m_model->m_zStatus = "moving left";
                if( m_model->m_enabledFunction == Mode::Taper )
                {
                    takeUpZBacklash( ZDirection::Left );
                    startSynchronisedXMotor( ZDirection::Left, m_model->m_zAxisMotor->getSpeed() );
                }
                m_model->m_zAxisMotor->goToStep( INF_LEFT );
                break;
            }
            case key::RIGHT:
            {
                // Same key will cancel if we're already moving
                if ( m_model->m_zAxisMotor->isRunning() )
                {
                    m_model->m_zAxisMotor->stop();
                    break;
                }
                m_model->m_zStatus = "moving right";
                if( m_model->m_enabledFunction == Mode::Taper )
                {
                    takeUpZBacklash( ZDirection::Right );
                    startSynchronisedXMotor( ZDirection::Right, m_model->m_zAxisMotor->getSpeed() );
                }
                m_model->m_zAxisMotor->goToStep( INF_RIGHT );
                break;
            }
            case key::a:
            case key::A:
            case key::COMMA: // (<) - nudge left
            {
                if ( m_model->m_zAxisMotor->isRunning() )
                {
                    m_model->m_zAxisMotor->stop();
                    m_model->m_zAxisMotor->wait();
                }
                m_model->m_zAxisMotor->goToStep( m_model->m_zAxisMotor->getCurrentStep() + 25L );
                break;
            }
            case key::d:
            case key::D:
            case key::FULLSTOP: // (>) - nudge right
            {
                if ( m_model->m_zAxisMotor->isRunning() )
                {
                    m_model->m_zAxisMotor->stop();
                    m_model->m_zAxisMotor->wait();
                }
                m_model->m_zAxisMotor->goToStep( m_model->m_zAxisMotor->getCurrentStep() - 25L );
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
                if( m_model->m_currentMemory < m_model->m_zMemory.size() - 1 )
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
                    m_model->m_zAxisMotor->setSpeed( 20.0 );
                }
                break;
            }
            case key::TWO:
            {
                if( m_model->m_currentDisplayMode != Mode::Threading )
                {
                    m_model->m_zAxisMotor->setSpeed( 40.0 );
                }
                break;
            }
            case key::THREE:
            {
                if( m_model->m_currentDisplayMode != Mode::Threading )
                {
                    m_model->m_zAxisMotor->setSpeed( 100.0 );
                }
                break;
            }
            case key::FOUR:
            {
                if( m_model->m_currentDisplayMode != Mode::Threading )
                {
                    m_model->m_zAxisMotor->setSpeed( 250.0 );
                }
                break;
            }
            case key::FIVE:
            {
                if( m_model->m_currentDisplayMode != Mode::Threading )
                {
                    m_model->m_zAxisMotor->setSpeed( m_zMaxMotorSpeed );
                }
                break;
            }
            // Speed presets for X with number keys 6-0 or X leader + 1-5
            case key::x1:
            case key::SIX:
            {
                if( m_model->m_currentDisplayMode != Mode::Threading )
                {
                    m_model->m_xAxisMotor->setSpeed( 5.0 );
                }
                break;
            }
            case key::x2:
            case key::SEVEN:
            {
                if( m_model->m_currentDisplayMode != Mode::Threading )
                {
                    m_model->m_xAxisMotor->setSpeed( 20.0 );
                }
                break;
            }
            case key::x3:
            case key::EIGHT:
            {
                if( m_model->m_currentDisplayMode != Mode::Threading )
                {
                    m_model->m_xAxisMotor->setSpeed( 40.0 );
                }
                break;
            }
            case key::x4:
            case key::NINE:
            {
                if( m_model->m_currentDisplayMode != Mode::Threading )
                {
                    m_model->m_xAxisMotor->setSpeed( 80.f );
                }
                break;
            }
            case key::x5:
            case key::ZERO:
            {
                if( m_model->m_currentDisplayMode != Mode::Threading )
                {
                    m_model->m_xAxisMotor->setSpeed( m_xMaxMotorSpeed );
                }
                break;
            }
            case key::f:
            case key::F:
            {
                // Fast return to point
                if( m_model->m_zMemory.at( m_model->m_currentMemory ) == INF_RIGHT ) break;
                if( m_model->m_fastReturning ) break;
                m_model->m_previousZSpeed = m_model->m_zAxisMotor->getSpeed();
                m_model->m_fastReturning = true;
                m_model->m_zAxisMotor->stop();
                m_model->m_zAxisMotor->wait();
                if( m_model->m_enabledFunction == Mode::Taper )
                {
                    // If we are tapering, we need to set a speed the x-axis motor can keep up with
                    m_model->m_zAxisMotor->setSpeed( 100.0 );
                    ZDirection direction = ZDirection::Left;
                    if( m_model->m_zMemory.at( m_model->m_currentMemory ) <
                        m_model->m_zAxisMotor->getCurrentStep() )
                    {
                        direction = ZDirection::Right;
                    }
                    takeUpZBacklash( direction );
                    startSynchronisedXMotor( direction, m_model->m_zAxisMotor->getSpeed() );
                }
                else
                {
                    m_model->m_zAxisMotor->setSpeed( m_model->m_zAxisMotor->getMaxRpm() );
                }
                m_model->m_zStatus = "fast returning";
                m_model->m_zAxisMotor->goToStep( m_model->m_zMemory.at( m_model->m_currentMemory ) );
                break;
            }
            case key::r:
            case key::R:
            {
                // X retraction
                if( m_model->m_xAxisMotor->isRunning() ) return;
                if( m_model->m_enabledFunction == Mode::Taper ) return;
                if( m_model->m_xRetracted )
                {
                    // Return
                    m_model->m_xAxisMotor->goToStep( m_model->m_xOldPosition );
                    m_model->m_fastRetracting = true;
                }
                else
                {
                    m_model->m_xOldPosition = m_model->m_xAxisMotor->getCurrentStep();
                    m_model->m_previousXSpeed = m_model->m_xAxisMotor->getSpeed();
                    m_model->m_xAxisMotor->setSpeed( 100.0 );
                    int direction = -1;
                    if( m_model->m_xRetractionDirection == XRetractionDirection::Inwards )
                    {
                        direction = 1;
                    }
                    long stepsForRetraction =
                        2.0 / std::abs( m_model->m_xAxisMotor->getConversionFactor() );
                    m_model->m_xAxisMotor->goToStep(
                        m_model->m_xAxisMotor->getCurrentStep() + stepsForRetraction * direction );
                    m_model->m_xRetracted = true;
                }
                break;
            }
            case key::zz:
            {
                if( m_model->m_enabledFunction == Mode::Taper )
                {
                    changeMode( Mode::None );
                }
                m_model->m_zAxisMotor->zeroPosition();
                // Zeroing will invalidate any memorised Z positions, so we clear them
                for( auto& m : m_model->m_zMemory )
                {
                    m = INF_RIGHT;
                }
                break;
            }
            case key::xz:
            {
                if( m_model->m_enabledFunction == Mode::Taper )
                {
                    changeMode( Mode::None );
                }
                m_model->m_xAxisMotor->zeroPosition();
                // Zeroing will invalidate any memorised X positions, so we clear them
                for( auto& m : m_model->m_xMemory )
                {
                    m = INF_OUT;
                }
                break;
            }
            case key::ASTERISK: // shutdown
            {
                #ifndef FAKE
                stopAllMotors();
                m_model->m_quit = true;
                m_model->m_shutdown = true;
                #endif
                break;
            }
            case key::F1: // help mode
            case key::f2h:
            {
                changeMode( Mode::Help );
                break;
            }
            case key::f2s: // setup mode
            {
                m_model->m_enabledFunction = Mode::None;
                m_model->m_zAxisMotor->setSpeed( 0.8f );
                m_model->m_xAxisMotor->setSpeed( 1.f );
                changeMode( Mode::Setup );
                break;
            }
            case key::f2t: // threading mode
            {
                changeMode( Mode::Threading );
                break;
            }
            case key::f2p: // taper mode
            {
                changeMode( Mode::Taper );
                break;
            }
            case key::f2r: // X retraction setup
            {
                changeMode( Mode::XRetractSetup );
                break;
            }
            case key::xs: // X position set
            {
                changeMode( Mode::XPositionSetup );
                break;
            }
            case key::zs: // Z position set
            {
                changeMode( Mode::ZPositionSetup );
                break;
            }
            case key::ESC: // return to normal mode
            {
                // Cancel any retract as well
                m_model->m_xRetracted = false;
                changeMode( Mode::None );
                break;
            }
            default: // e.g. space bar to stop all motors
            {
                stopAllMotors();
                break;
            }
        }
    }
}

void Controller::changeMode( Mode mode )
{
    stopAllMotors();
    if( m_model->m_enabledFunction == Mode::Taper && mode != Mode::Taper )
    {
        m_model->m_xAxisMotor->setSpeed( m_model->m_taperPreviousXSpeed );
    }
    m_model->m_warning = "";
    m_model->m_currentDisplayMode = mode;
    m_model->m_enabledFunction = mode;
    m_model->m_input="";

    if( mode == Mode::Taper )
    {
        // reset any taper start points
        m_model->m_taperPreviousXSpeed = m_model->m_xAxisMotor->getSpeed();
        if( m_model->m_taperAngle != 0.0 )
        {
            m_model->m_input = std::to_string( m_model->m_taperAngle );
        }
    }
}

void Controller::stopAllMotors()
{
    m_model->m_zAxisMotor->stop();
    m_model->m_xAxisMotor->stop();
    m_model->m_zAxisMotor->wait();
    m_model->m_xAxisMotor->wait();
    m_model->m_zStatus = "stopped";
    m_model->m_xStatus = "stopped";
}

int Controller::checkKeyAllowedForMode( int key )
{
    // The mode means some keys are ignored, for instance in
    // threading, you cannot change the speed of the z-axis as
    // that would affect the thread pitch
    // Always allow certain keys:
    if( key == key::q  ||
        key == key::Q  ||
        key == key::F1 ||
        key == key::F2 ||
        key == key::F3 ||
        key == key::F4 ||
        key == key::F5 ||
        key == key::ESC||
        key == key::ENTER
        )
    {
        return key;
    }
    // Don't allow any x movement when retracted
    if( m_model->m_xRetracted && ( key == key::UP || key == key::DOWN || key == key::W
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
        case Mode::XRetractSetup:
            if( key == key::UP || key == key::DOWN ) return key;
            return -1;
        // Any modes that have numerical input:
        case Mode::XPositionSetup:
            if( key >= key::ZERO && key <= key::NINE ) return key;
            if( key == key::FULLSTOP || key == key::BACKSPACE || key == key::DELETE ) return key;
            if( key == key::MINUS ) return key;
            if( key == key::d || key == key::D ) return key;
            return -1;
        case Mode::ZPositionSetup:
        case Mode::Taper:
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
        m_model->m_currentDisplayMode == Mode::XPositionSetup ||
        m_model->m_currentDisplayMode == Mode::ZPositionSetup )
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
            m_model->m_zAxisMotor->setSpeed( 40.f );
            // fall through...
        }
    }
    if(  m_model->m_currentDisplayMode == Mode::XRetractSetup )
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
    if( m_model->m_currentDisplayMode == Mode::XPositionSetup &&
        ( key == key::d || key == key::D ) )
    {
        float xPos = 0;
        try
        {
            xPos = - std::abs( std::stof( m_model->m_input ) );
        }
        catch( ... ) {}
        m_model->m_xAxisMotor->setPosition( xPos / 2 );
        // This will invalidate any memorised X positions, so we clear them
        for( auto& m : m_model->m_xMemory )
        {
            m = INF_OUT;
        }
        m_model->m_currentDisplayMode = Mode::None;
        return -1;
    }

    if( m_model->m_currentDisplayMode != Mode::None && key == key::ENTER )
    {
        if( m_model->m_currentDisplayMode == Mode::XPositionSetup )
        {
            float xPos = 0;
            try
            {
                xPos = std::stof( m_model->m_input );
            }
            catch( ... ) {}
            m_model->m_xAxisMotor->setPosition( xPos );
            // This will invalidate any memorised X positions, so we clear them
            for( auto& m : m_model->m_xMemory )
            {
                m = INF_OUT;
            }
        }
        if( m_model->m_currentDisplayMode == Mode::ZPositionSetup )
        {
            float zPos = 0;
            try
            {
                zPos = std::stof( m_model->m_input );
            }
            catch( ... ) {}
            m_model->m_zAxisMotor->setPosition( zPos );
            // This will invalidate any memorised Z positions, so we clear them
            for( auto& m : m_model->m_zMemory )
            {
                m = INF_RIGHT;
            }
        }
        m_model->m_currentDisplayMode = Mode::None;
        return -1;
    }
    return key;
}

int Controller::processLeaderKeyModeKeyPress( int keyPress )
{
    // TODO speed settings, ENTER (go to), memorise
    if( m_model->m_keyMode == KeyMode::ZAxis )
    {
        switch( keyPress )
        {
            case key::z:
            case key::Z:
                keyPress = key::zz;
                break;
            case key::m:
            case key::M:
                keyPress = key::zm;
                break;
            case key::g:
            case key::G:
                keyPress = key::zg;
                break;
            case key::ONE:
                keyPress = key::z1;
                break;
            case key::TWO:
                keyPress = key::z2;
                break;
            case key::THREE:
                keyPress = key::z3;
                break;
            case key::FOUR:
                keyPress = key::z4;
                break;
            case key::FIVE:
                keyPress = key::z5;
                break;
            case key::s:
            case key::S:
                keyPress = key::zs;
                break;
            default:
                keyPress = key::None;
        }
    }
    else if( m_model->m_keyMode == KeyMode::XAxis )
    {
        switch( keyPress )
        {
            case key::z:
            case key::Z:
                keyPress = key::xz;
                break;
            case key::m:
            case key::M:
                keyPress = key::xm;
                break;
            case key::g:
            case key::G:
                keyPress = key::xg;
                break;
            case key::ONE:
                keyPress = key::x1;
                break;
            case key::TWO:
                keyPress = key::x2;
                break;
            case key::THREE:
                keyPress = key::x3;
                break;
            case key::FOUR:
                keyPress = key::x4;
                break;
            case key::FIVE:
                keyPress = key::x5;
                break;
            case key::s:
            case key::S:
                keyPress = key::xs;
                break;
            default:
                keyPress = key::None;
        }
    }
    else if( m_model->m_keyMode == KeyMode::Function )
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

void Controller::startSynchronisedXMotor( ZDirection direction, double zSpeed )
{
    // Make sure X isn't already running first
    m_model->m_xAxisMotor->stop();
    m_model->m_xAxisMotor->wait();

    int target = INF_OUT;
    int stepAdd = -1;
    if( ( direction == ZDirection::Left  && m_model->m_taperAngle < 0.0 ) ||
        ( direction == ZDirection::Right && m_model->m_taperAngle > 0.0 ) )
    {
        target = INF_IN;
        stepAdd = 1;
    }
    // As this is called just before the Z motor starts moving, we take
    // up any backlash first.
    m_model->m_xAxisMotor->setSpeed( 100.0 );
    m_model->m_xAxisMotor->goToStep( m_model->m_xAxisMotor->getCurrentStep() + stepAdd );
    m_model->m_xAxisMotor->wait();
    // What speed will we need for the angle required?
    m_model->m_xAxisMotor->setSpeed(
        zSpeed  * std::abs( std::tan( m_model->m_taperAngle * DEG_TO_RAD ) ) );
    m_model->m_xAxisMotor->goToStep( target );
}

void Controller::takeUpZBacklash( ZDirection direction )
{
    if( direction == ZDirection::Right )
    {
        m_model->m_zAxisMotor->goToStep( m_model->m_zAxisMotor->getCurrentStep() - 1 );
    }
    else if( direction == ZDirection::Left )
    {
        m_model->m_zAxisMotor->goToStep( m_model->m_zAxisMotor->getCurrentStep() + 1 );
    }
    m_model->m_zAxisMotor->wait();
}

} // end namespace
