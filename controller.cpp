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

    m_zMaxMotorSpeed = m_model->m_config->readDouble( "zMaxMotorSpeed", 700.0 );
    m_xMaxMotorSpeed = m_model->m_config->readDouble( "xMaxMotorSpeed", 720.0 );

    m_view->initialise();

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
        if( m_model->m_enabledFunction == Mode::Taper )
        {
            syncXMotorPosition();
        }

        processKeyPress();

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
            m_model->m_zAxisMotor->setRpm( speed );
        }

        if( m_model->m_currentDisplayMode == Mode::Taper )
        {
            if( m_model->m_input.empty() )
            {
                m_model->m_taperAngle = 0.f;
            }
            else
            {
                if( m_model->m_input != "-" )
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

        if ( ! m_model->m_xAxisMotor->isRunning() )
        {
            if( m_model->m_fastRetracting )
            {
                m_model->m_xAxisMotor->setRpm( m_model->m_previousXSpeed );
                m_model->m_fastRetracting = false;
                m_model->m_xRetracted = false;
            }
        }

        m_view->updateDisplay( *m_model );

        // Small delay just to avoid the UI loop spinning
        yieldSleep( std::chrono::microseconds( 50'000 ) );
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
                m_model->m_xAxisMotor->goToStep( m_model->m_xAxisMotor->getCurrentStep() + 60.0 );
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
                m_model->m_xAxisMotor->goToStep( m_model->m_xAxisMotor->getCurrentStep() - 60.0 );
                break;
            }
            case key::EQUALS: // (i.e. plus)
            {
                if( m_model->m_enabledFunction == Mode::Threading ) break;
                if( m_model->m_zAxisMotor->getRpm() < 20.0 )
                {
                    m_model->m_zAxisMotor->setRpm( 20.0 );
                }
                else
                {
                    if( m_model->m_zAxisMotor->getRpm() < m_zMaxMotorSpeed )
                    {
                        m_model->m_zAxisMotor->setRpm( m_model->m_zAxisMotor->getRpm() + 20.0 );
                    }
                }
                break;
            }
            case key::MINUS:
            {
                if( m_model->m_enabledFunction == Mode::Threading ) break;
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
            case key::ENTER:
            {
                if( m_model->m_memory.at( m_model->m_currentMemory ) == INF_RIGHT ) break;
                // We always start at the same rotational position: it's
                // required for thread cutting, but doesn't impact
                // anything if we're just turning down, so we always do it.
                m_model->m_zAxisMotor->stop();
                m_model->m_zAxisMotor->wait();
                m_model->m_status = "returning";
                // Before we make the move at zero degrees, we want to ensure any potential
                // backlash is compensated, so we move one step towards the target first
                if( m_model->m_memory.at( m_model->m_currentMemory ) <
                    m_model->m_zAxisMotor->getCurrentStep() )
                {
                    m_model->m_zAxisMotor->goToStep( m_model->m_zAxisMotor->getCurrentStep() -1 );
                }
                else
                {
                    m_model->m_zAxisMotor->goToStep( m_model->m_zAxisMotor->getCurrentStep() +1 );
                }
                m_model->m_zAxisMotor->wait();
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
                if( m_model->m_currentMemory < m_model->m_memory.size() - 1 )
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
                    m_model->m_zAxisMotor->setRpm( 20.f );
                }
                break;
            }
            case key::TWO:
            {
                if( m_model->m_currentDisplayMode != Mode::Threading )
                {
                    m_model->m_zAxisMotor->setRpm( 40.f );
                }
                break;
            }
            case key::THREE:
            {
                if( m_model->m_currentDisplayMode != Mode::Threading )
                {
                    m_model->m_zAxisMotor->setRpm( 100.f );
                }
                break;
            }
            case key::FOUR:
            {
                if( m_model->m_currentDisplayMode != Mode::Threading )
                {
                    m_model->m_zAxisMotor->setRpm( 250.f );
                }
                break;
            }
            case key::FIVE:
            {
                if( m_model->m_currentDisplayMode != Mode::Threading )
                {
                    m_model->m_zAxisMotor->setRpm( m_zMaxMotorSpeed );
                }
                break;
            }
            // Speed presets for X with number keys 6-0
            case key::SIX:
            {
                if( m_model->m_currentDisplayMode != Mode::Threading )
                {
                    m_model->m_xAxisMotor->setRpm( 30.f );
                }
                break;
            }
            case key::SEVEN:
            {
                if( m_model->m_currentDisplayMode != Mode::Threading )
                {
                    m_model->m_xAxisMotor->setRpm( 60.f );
                }
                break;
            }
            case key::EIGHT:
            {
                if( m_model->m_currentDisplayMode != Mode::Threading )
                {
                    m_model->m_xAxisMotor->setRpm( 120.f );
                }
                break;
            }
            case key::NINE:
            {
                if( m_model->m_currentDisplayMode != Mode::Threading )
                {
                    m_model->m_xAxisMotor->setRpm( 240.f );
                }
                break;
            }
            case key::ZERO:
            {
                if( m_model->m_currentDisplayMode != Mode::Threading )
                {
                    m_model->m_xAxisMotor->setRpm( m_xMaxMotorSpeed );
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
                // If we are tapering, we need to set a speed the x-axis motor can keep up with
                // if the angle is steep. We choose an arbitrary 5.f for the angle
                if( m_model->m_taperAngle > 5.f && m_model->m_enabledFunction == Mode::Taper )
                {
                    m_model->m_zAxisMotor->setRpm( 100.f );
                }
                else
                {
                    m_model->m_zAxisMotor->setRpm( m_model->m_zAxisMotor->getMaxRpm() );
                }
                m_model->m_status = "fast returning";
                m_model->m_zAxisMotor->goToStep( m_model->m_memory.at( m_model->m_currentMemory ) );
                break;
            }
            case key::r:
            case key::R:
            {
                // X retraction
                if( m_model->m_xAxisMotor->isRunning() ) return;
                if( m_model->m_xRetracted )
                {
                    // Return
                    m_model->m_xAxisMotor->goToStep( m_model->m_xOldPosition );
                    m_model->m_fastRetracting = true;
                }
                else
                {
                    // TODO the steps for retraction should be derived from the figure we
                    // use to construct the motor in the first place, which in turn should
                    // come from config
                    m_model->m_xOldPosition = m_model->m_xAxisMotor->getCurrentStep();
                    m_model->m_previousXSpeed = m_model->m_xAxisMotor->getRpm();
                    m_model->m_xAxisMotor->setRpm( 300.f );
                    int direction = -1;
                    if( m_model->m_xRetractionDirection == XRetractionDirection::Inwards )
                    {
                        direction = 1;
                    }
                    m_model->m_xAxisMotor->goToStep(
                        m_model->m_xAxisMotor->getCurrentStep() + 4'800 * direction );
                    m_model->m_xRetracted = true;
                }
                break;
            }
            case key::z:
            case key::Z:
            {
                if( m_model->m_enabledFunction == Mode::Taper )
                {
                    changeMode( Mode::None );
                }
                m_model->m_zAxisMotor->zeroPosition();
                m_model->m_xAxisMotor->zeroPosition();
                // Zeroing will invalidate any memorised positions, so we clear them
                for( auto& m : m_model->m_memory )
                {
                    m = INF_RIGHT;
                }
                break;
            }
            case key::x:
            case key::X:
            {
                // Zero just X-axis
                if( m_model->m_enabledFunction == Mode::Taper )
                {
                    changeMode( Mode::None );
                }
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
                m_model->m_enabledFunction = Mode::None;
                m_model->m_zAxisMotor->setSpeed( 0.8f );
                m_model->m_xAxisMotor->setSpeed( 1.f );
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
            case key::F5: // X retraction setup
            {
                changeMode( Mode::XRetractSetup );
                break;
            }
            case key::F6: // X retraction setup
            {
                changeMode( Mode::XRadiusSetup );
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
    m_model->m_currentDisplayMode = mode;
    m_model->m_enabledFunction = mode;

    if( mode == Mode::Taper )
    {
        if( m_model->m_taperAngle != 0.f )
        {
            m_model->m_input = std::to_string( m_model->m_taperAngle );
        }
        else
        {
            m_model->m_input = "";
        }
    }
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
        case Mode::XRadiusSetup:
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

int Controller::processInputKeys( int key )
{
    // If we are in a "mode" then certain keys (e.g. the number keys) are used for input
    // so are processed here before allowing them to fall through to the main key processing
    if( m_model->m_currentDisplayMode == Mode::Taper )
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
            m_model->m_input.pop_back();
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

    if( m_model->m_currentDisplayMode != Mode::None && key == key::ENTER )
    {
        m_model->m_currentDisplayMode = Mode::None;
        return -1;
    }
    return key;
}

void Controller::syncXMotorPosition()
{
    m_model->m_xAxisMotor->setSpeed( 100.f );
    static double startZPosition = std::numeric_limits<double>::max();
    static double startXPosition = std::numeric_limits<double>::max();
    static double previousZPosition = std::numeric_limits<double>::max();
    if( startZPosition == std::numeric_limits<double>::max() )
    {
        startZPosition = m_model->m_zAxisMotor->getPosition();
        startXPosition = m_model->m_xAxisMotor->getPosition();
        return;
    }
    double currentZPosition = m_model->m_zAxisMotor->getPosition();
    if( currentZPosition == previousZPosition )
    {
        // we appear to have stopped, so store a new start position
        startZPosition = m_model->m_zAxisMotor->getPosition();
        startXPosition = m_model->m_xAxisMotor->getPosition();
        return;
    }
    double zDelta = startZPosition - currentZPosition;
    double newXPosition = startXPosition -
        (std::tan( m_model->m_taperAngle * 0.0174533 ) * zDelta );
    m_model->m_xAxisMotor->setPosition( newXPosition );
    previousZPosition = currentZPosition;
}

} // end namespace
