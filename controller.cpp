#include "controller.h"

#include "fmt/format.h"
#include "keycodes.h"
#include "log.h"
#include "stepperControl/igpio.h"
#include "threadpitches.h"  // for ThreadPitch, threadPitches
#include "view_curses.h"
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
    if( m_model->m_useSfml ) // driven from command line --sfml
    {
        m_view = std::make_unique<ViewSfml>();
    }
    else
    {
        m_view = std::make_unique<ViewCurses>();
    }

    m_view->initialise();

    // TODO: currently ignoring enable pin
    m_model->m_zAxisMotor = std::make_unique<mgo::StepperMotor>(
        m_model->m_gpio, 8, 7, 0, 1'000, -0.001, MAX_Z_MOTOR_SPEED );
    // My X-Axis motor is set to 800 steps per revolution and the gearing
    // is 3:1 so 2,400 steps make one revolution of the X-axis handwheel,
    // which would be 1mm of travel. TODO: this should be settable in config
    m_model->m_xAxisMotor = std::make_unique<mgo::StepperMotor>(
        m_model->m_gpio, 20, 21, 0, 800, 1.0 / 2'400.0, MAX_X_MOTOR_SPEED );
    m_model->m_rotaryEncoder = std::make_unique<mgo::RotaryEncoder>(
        m_model->m_gpio, 23, 24, 2000, 35.f/30.f );
}

void Controller::run()
{
    m_model->m_zAxisMotor->setSpeed( 40.f );
    m_model->m_xAxisMotor->setSpeed( 40.f );

    while( ! m_model->m_quit )
    {
        processKeyPress();

        if( m_model->m_taperAngle != 0.f )
        {
            syncXMotorPosition();
        }

        if( m_model->m_currentMode == Mode::Threading )
        {
            // We are cutting threads, so the stepper motor's speed
            // is dependent on the spindle's RPM and the thread pitch.
            float pitch = threadPitches.at( m_model->m_threadPitchIndex ).pitchMm;
            // because my stepper motor / leadscrew does one mm per
            // revolution, there is a direct correlation between spindle
            // rpm and stepper motor rpm for a 1mm thread pitch.
            float speed = pitch * m_model->m_rotaryEncoder->getRpm();
            if( speed > MAX_Z_MOTOR_SPEED )
            {
                m_model->m_zAxisMotor->stop();
                m_model->m_zAxisMotor->wait();
                m_model->m_warning = "RPM too high for threading";
            }
            else
            {
                m_model->m_warning = "";
            }
            m_model->m_zAxisMotor->setRpm( speed );
        }

        if( m_model->m_currentMode == Mode::Taper )
        {
            if( m_model->m_input.empty() )
            {
                m_model->m_taperAngle = 0.f;
            }
            else
            {
                try
                {
                    m_model->m_taperAngle = std::stof( m_model->m_input );
                    if( m_model->m_taperAngle > 90.f )
                    {
                        m_model->m_taperAngle = 90.f;
                        m_model->m_input = "90.0";
                    }
                }
                catch( const std::exception& )
                {
                    m_model->m_taperAngle = 0.f;
                    m_model->m_input = "";
                }
            }
        }

        if ( ! m_model->m_zAxisMotor->isRunning() )
        {
            m_model->m_status = "stopped";
            if( m_model->m_fastReturning )
            {
                m_model->m_zAxisMotor->setRpm( m_model->m_previousZSpeed );
                m_model->m_fastReturning = false;
            }
        }
        m_view->updateDisplay( *m_model );

        // Small delay just to avoid the UI loop spinning
        yieldSleep( std::chrono::microseconds( 100'000 ) );
    }
}

void Controller::processKeyPress()
{
    int t = m_view->getInput();
    t = checkKeyAllowedForMode( t );
    t = processInputKeys( t );
    if( t != key::None )
    {
        m_model->m_keyPressed = t;
        switch( m_model->m_keyPressed )
        {
            case key::None:
            {
                break;
            }
            case key::Q:
            case key::q:
            {
                stopAllMotors();
                m_model->m_quit = true;
                break;
            }
            // Cross-slide support is currently just for testing
            case key::C: // upper case, i.e. shift-c
            {
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
            // Cross-slide support is currently just for testing
            case key::c:
            {
                if( m_model->m_xAxisMotor->getSpeed() < 10.0 )
                {
                    m_model->m_xAxisMotor->setSpeed( 10.0 );
                }
                else if( m_model->m_xAxisMotor->getSpeed() < 240.0 )
                {
                    m_model->m_xAxisMotor->setSpeed( m_model->m_xAxisMotor->getSpeed() + 10.0 );
                }
                break;
            }
            // Cross-slide support is currently just for testing
            // Nudge in
            case key::D:
            case key::d:
            {
                if ( m_model->m_xAxisMotor->isRunning() )
                {
                    m_model->m_xAxisMotor->stop();
                    m_model->m_xAxisMotor->wait();
                }
                m_model->m_xAxisMotor->goToStep( m_model->m_xAxisMotor->getCurrentStep() + 60.0 );
                break;
            }
            case key::EQUALS: // (i.e. plus)
            {
                if( m_model->m_currentMode == Mode::Threading ) break; // TODO this check should be in checkKeyAllowedForMode
                if( m_model->m_zAxisMotor->getRpm() < 20.0 )
                {
                    m_model->m_zAxisMotor->setRpm( 20.0 );
                }
                else
                {
                    if( m_model->m_zAxisMotor->getRpm() < MAX_Z_MOTOR_SPEED )
                    {
                        m_model->m_zAxisMotor->setRpm( m_model->m_zAxisMotor->getRpm() + 20.0 );
                    }
                }
                break;
            }
            case key::MINUS:
            {
                if( m_model->m_currentMode == Mode::Threading ) break;
                if( m_model->m_zAxisMotor->getRpm() > 20 )
                {
                    m_model->m_zAxisMotor->setRpm( m_model->m_zAxisMotor->getRpm() - 20.0 );
                }
                else
                {
                    if ( m_model->m_zAxisMotor->getRpm() > 1 )
                    {
                        m_model->m_zAxisMotor->setRpm( m_model->m_zAxisMotor->getRpm() - 1.0 );
                    }
                }
                break;
            }
            case key::m:
            case key::M:
            {
                m_model->m_memory.at( m_model->m_currentMemory ) =
                    m_model->m_zAxisMotor->getCurrentStep();
                break;
            }
            case key::t:
            case key::T:
            {
                // Toggle threading on/off, deprecated, now use function key (TODO remove)
                stopAllMotors();
                if( m_model->m_currentMode == Mode::Threading )
                {
                    m_model->m_currentMode = Mode::None;
                }
                else
                {
                    m_model->m_currentMode = Mode::Threading;
                }
                break;
            }
            case key::p:
            {
                if ( m_model->m_currentMode != Mode::Threading ) break;
                // change threading pitch up
                ++m_model->m_threadPitchIndex;
                if( m_model->m_threadPitchIndex >= threadPitches.size() )
                {
                    m_model->m_threadPitchIndex = 0;
                }
                break;
            }
            case key::P:
            {
                if ( m_model->m_currentMode != Mode::Threading ) break;
                // change threading pitch down
                if( m_model->m_threadPitchIndex == 0 )
                {
                    m_model->m_threadPitchIndex = threadPitches.size() - 1;
                }
                else
                {
                    --m_model->m_threadPitchIndex;
                }
                break;
            }
            case key::BACKSLASH:
            {
                // causes the cut to start a fraction earlier
                // next time, this simulates feeding in at 29.5°
                ++m_model->m_threadCutAdvanceCount;
                m_model->m_rotaryEncoder->setAdvanceValueMicroseconds(
                    m_model->m_threadCutAdvanceCount *
                    ( ( 1'000'000.f / ( m_model->m_zAxisMotor->getRpm() / 60.f) ) * SIDEFEED )
                    );
                break;
            }
            case key::PIPE: // i.e. shift backslash
            {
                // decrement the cut advance count
                if( m_model->m_threadCutAdvanceCount > 0 )
                {
                    --m_model->m_threadCutAdvanceCount;
                }
                break;
            }
            case key::ENTER:
            {
                if( m_model->m_memory.at( m_model->m_currentMemory ) == INF_RIGHT ) break;
                // We always start at the same rotational position: it's
                // required for thread cutting, but doesn't impact
                // anything if we're just turning down, so we always do it.
                m_model->m_zAxisMotor->stop();
                m_model->m_zAxisMotor->wait();
                m_model->m_status = "returning";
                m_model->m_rotaryEncoder->callbackAtZeroDegrees([&]()
                    {
                        m_model->m_zAxisMotor->goToStep(
                            m_model->m_memory.at( m_model->m_currentMemory ) ); 
                    }
                    );
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
                    m_model->m_xAxisMotor->goToStep( INF_LEFT );
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
                    m_model->m_xAxisMotor->goToStep( INF_RIGHT );
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
                m_model->m_status = "moving left";
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
                m_model->m_status = "moving right";
                m_model->m_zAxisMotor->goToStep( INF_RIGHT );
                break;
            }
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
                if( m_model->m_currentMemory < m_model->m_memory.size() - 1 )
                {
                    ++m_model->m_currentMemory;
                }
                break;
            }

            // Speed presets with number keys 1-5
            case key::ONE:
            {
                if( m_model->m_currentMode != Mode::Threading )
                {
                    m_model->m_zAxisMotor->setRpm( 20.f );
                }
                break;
            }
            case key::TWO:
            {
                if( m_model->m_currentMode != Mode::Threading )
                {
                    m_model->m_zAxisMotor->setRpm( 40.f );
                }
                break;
            }
            case key::THREE:
            {
                if( m_model->m_currentMode != Mode::Threading )
                {
                    m_model->m_zAxisMotor->setRpm( 100.f );
                }
                break;
            }
            case key::FOUR:
            {
                if( m_model->m_currentMode != Mode::Threading )
                {
                    m_model->m_zAxisMotor->setRpm( 250.f );
                }
                break;
            }
            case key::FIVE:
            {
                if( m_model->m_currentMode != Mode::Threading )
                {
                    m_model->m_zAxisMotor->setRpm( MAX_Z_MOTOR_SPEED );
                }
                break;
            }
            case key::f:
            case key::F:
            {
                // Fast return to point
                if( m_model->m_memory.at( m_model->m_currentMemory ) == INF_RIGHT ) break;
                if( m_model->m_fastReturning ) break;
                m_model->m_previousZSpeed = m_model->m_zAxisMotor->getRpm();
                m_model->m_fastReturning = true;
                m_model->m_zAxisMotor->stop();
                m_model->m_zAxisMotor->wait();
                m_model->m_zAxisMotor->setRpm( m_model->m_zAxisMotor->getMaxRpm() );
                m_model->m_status = "fast returning";
                m_model->m_zAxisMotor->goToStep( m_model->m_memory.at( m_model->m_currentMemory ) );
                break;
            }
            case key::z:
            case key::Z:
            {
                m_model->m_zAxisMotor->zeroPosition();
                m_model->m_xAxisMotor->zeroPosition();
                break;
            }
            case key::ASTERISK: // shutdown
            // Note the command used for shutdown should be made passwordless
            // in the /etc/sudoers files
            {
                #ifndef FAKE
                stopAllMotors();
                m_model->m_quit = true;
                system( "sudo systemctl poweroff --no-block" );
                #endif
                break;
            }
            case key::F1: // help mode
            {
                changeMode( Mode::Help );
                break;
            }
            case key::F2: // setup mode
            {
                changeMode( Mode::Setup );
                break;
            }
            case key::F3: // threading mode
            {
                changeMode( Mode::Threading );
                break;
            }
            case key::F4: // taper mode
            {
                changeMode( Mode::Taper );
                break;
            }
            case key::ESC: // return to normal mode
            {
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
    m_model->m_warning = "";
    if( mode == Mode::Taper && m_model->m_taperAngle != 0.f )
    {
        m_model->m_input = std::to_string( m_model->m_taperAngle );
    }
    else
    {
        m_model->m_input = "";
    }
    m_model->m_currentMode = mode;
}

void Controller::stopAllMotors()
{
    m_model->m_zAxisMotor->stop();
    m_model->m_xAxisMotor->stop();
    m_model->m_zAxisMotor->wait();
    m_model->m_xAxisMotor->wait();
    m_model->m_status = "stopped";
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
        key == key::ESC
        )
    {
        return key;
    }
    switch( m_model->m_currentMode )
    {
        case Mode::None:
            return key;
        case Mode::Help:
            if( key == key::ENTER || key == key::ESC || key == key::Q || key == key::q ) return key;
            return -1;
        case Mode::Setup:
            if( key == key::ENTER || key == key::ESC || key == key::Q || key == key::q ) return key;
            return -1;
        case Mode::Taper:
            if( key == key::ENTER || key == key::ESC || key == key::Q || key == key::q ) return key;
            if( key >= key::ZERO && key <= key::NINE ) return key;
            if( key == key::FULLSTOP || key == key::BACKSPACE ) return key;
            return -1;
        case Mode::Threading:
            if( key == key::ENTER || key == key::ESC || key == key::Q || key == key::q ) return key;
            return -1;
        default:
            // unhandled mode
            assert( false );
    }
}

int Controller::processInputKeys( int key )
{
    // If we are in a "mode" then certain keys (e.g. the number keys) are used for input
    // so are processed here before allowing them to fall through to the main key processing
    if( m_model->m_currentMode == Mode::Taper )
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
        if( key == key::BACKSPACE )
        {
            m_model->m_input.pop_back();
            return -1;
        }
        if( key == key::ENTER )
        {
            m_model->m_currentMode = Mode::None;
            return -1;
        }
    }
    return key;
}

void Controller::syncXMotorPosition()
{
    static double previousZPosition = std::numeric_limits<double>::max();
    if( previousZPosition == std::numeric_limits<double>::max() )
    {
        previousZPosition = m_model->m_zAxisMotor->getPosition();
        return;
    }
    double currentZPosition = m_model->m_zAxisMotor->getPosition();
    double currentXPosition = m_model->m_xAxisMotor->getPosition();
    double zDelta = previousZPosition - currentZPosition;
    double newXPosition = currentXPosition -
        (std::tan( m_model->m_taperAngle * 0.0174533 ) * zDelta );
    m_model->m_xAxisMotor->setPosition( newXPosition );
    previousZPosition = currentZPosition;
}

} // end namespace
