#include "controller.h"

#include "fmt/format.h"
#include "keycodes.h"
#include "log.h"
#include "stepperControl/igpio.h"
#include "threadpitches.h"
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
    m_model->initialise();
}

void Controller::run()
{
    m_model->m_axis1Motor->setSpeed( m_model->m_config.readDouble( "Axis1SpeedPreset2", 40.0 ) );
    m_model->m_axis2Motor->setSpeed( m_model->m_config.readDouble( "Axis2SpeedPreset2", 20.0 ) );

    while( ! m_model->m_quit )
    {
        processKeyPress();

        m_model->checkStatus();

        m_view->updateDisplay( *m_model );

        if( m_model->isShuttingDown() )
        {
            // Stop the motor threads
            m_model->m_axis2Motor.reset();
            m_model->m_axis1Motor.reset();
            // Note the command used for shutdown should be made passwordless
            // in the /etc/sudoers files
            #pragma GCC diagnostic push
            #pragma GCC diagnostic ignored "-Wunused-result"
            system( "sudo shutdown -h now &" );
            #pragma GCC diagnostic pop
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
        if( m_model->getKeyMode() != KeyMode::None )
        {
            // If we are in a "leader key mode" (i.e. the first "prefix" key has
            // already been pressed) then we can modify t here. If the key
            // combination is recognised, we get a "chord" keypress e.g. key::xz.
            t = processLeaderKeyModeKeyPress( t );
            m_model->setKeyMode( KeyMode::None );
        }
        m_model->setKeyPressed( t );
        // Modify key press if it is a known axis leader key:
        m_model->setKeyPressed( checkForAxisLeaderKeys( m_model->getKeyPressed() ) );
        switch( m_model->getKeyPressed() )
        {
            case key::None:
            {
                break;
            }
            case key::F2:
            {
                m_model->setKeyMode( KeyMode::Function );
                break;
            }
            // End of "Leader" keys ==============
            case key::CtrlQ:
            {
                m_model->stopAllMotors();
                m_model->m_quit = true;
                break;
            }
            case key::l:
            case key::L:
            {
                m_model->axis1GoToPreviousPosition();
                break;
            }
            case key::FULLSTOP:
            {
                m_model->repeatLastRelativeMove();
                break;
            }
            case key::a2_MINUS:
            {
                // X-axis speed decrease
                m_model->axis2SpeedDecrease();
                break;
            }
            case key::a2_EQUALS:
            {
                // X-axis speed increase
                m_model->axis2SpeedIncrease();
                break;
            }
            // Nudge in X axis
            case key::W:
            case key::w:
            {
                m_model->axis2Nudge( XDirection::Inwards );
                break;
            }
            // Nudge out X axis
            case key::S:
            case key::s:
            {
                m_model->axis2Nudge( XDirection::Outwards );
                break;
            }
            case key::a1_EQUALS:
            case key::EQUALS: // (i.e. plus)
            {
                m_model->axis1SpeedIncrease();
                break;
            }
            case key::a1_MINUS:
            case key::MINUS:
            {
                m_model->axis1SpeedDecrease();
                break;
            }
            case key::a1_m:
            {
                m_model->axis1StorePosition();
                break;
            }
            case key::a2_m:
            {
                m_model->axis2StorePosition();
                break;
            }
            case key::m:
            case key::M:
            {
                m_model->axis1StorePosition();
                m_model->axis2StorePosition();
                break;
            }
            case key::a2_ENTER:
            {
                m_model->axis2GoToCurrentMemory();
                break;
            }
            case key::aAll_ENTER:
            {
                m_model->axis1GoToCurrentMemory();
                m_model->axis2GoToCurrentMemory();
                break;
            }
            case key::ENTER:
            case key::a1_ENTER:
            {
                m_model->axis1GoToCurrentMemory();
                break;
            }
            case key::UP:
            {
                m_model->axis2Move( XDirection::Inwards );
                break;
            }
            case key::DOWN:
            {
                m_model->axis2Move( XDirection::Outwards );
                break;
            }
            case key::LEFT:
            {
                m_model->axis1Move( ZDirection::Left );
                break;
            }
            case key::RIGHT:
            {
                m_model->axis1Move( ZDirection::Right );
                break;
            }
            case key::a:
            case key::A:
            {
                m_model->axis1Nudge( ZDirection::Left );
                break;
            }
            case key::d:
            case key::D:
            {
                m_model->axis1Nudge( ZDirection::Right );
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
            case key::TWO:
            case key::THREE:
            case key::FOUR:
            case key::FIVE:
            {
                m_model->axis1SpeedPreset();
                break;
            }
            // Speed presets for X with number keys 6-0 or X leader + 1-5
            case key::SIX:
            case key::SEVEN:
            case key::EIGHT:
            case key::NINE:
            case key::ZERO:
            case key::a2_1:
            case key::a2_2:
            case key::a2_3:
            case key::a2_4:
            case key::a2_5:
            {
                m_model->axis2SpeedPreset();
                break;
            }
            case key::f:
            case key::F:
            case key::a1_f:
            {
                // Fast return to point
                m_model->axis1FastReturn();
                break;
            }
            case key::a2_f:
            {
                // Fast return to point
                m_model->axis2FastReturn();
                break;
            }
            case key::r:
            case key::R:
            {
                // X retraction
                m_model->axis2Retract();
                break;
            }
            case key::a1_z:
            {
                m_model->axis1Zero();
                break;
            }
            case key::a2_z:
            {
                m_model->axis2Zero();
                break;
            }
            case key::aAll_z:
            {
                m_model->axis1Zero();
                m_model->axis2Zero();
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
            case key::a1_r:
            {
                m_model->changeMode( Mode::Axis1GoToOffset );
                break;
            }
            case key::a2_r:
            {
                m_model->changeMode( Mode::Axis2GoToOffset );
                break;
            }
            case key::ASTERISK: // shutdown
            {
                #ifndef FAKE
                m_model->stopAllMotors();
                m_model->m_quit = true;
                m_model->shutDown();
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
                m_model->setEnabledFunction( Mode::None );
                m_model->m_axis1Motor->setSpeed( 0.8f );
                m_model->m_axis2Motor->setSpeed( 1.f );
                m_model->changeMode( Mode::Setup );
                break;
            }
            case key::f2t: // threading mode
            {
                if( m_model->m_config.readBool( "DisableAxis2", false ) ) break;
                m_model->changeMode( Mode::Threading );
                break;
            }
            case key::f2p: // taper mode
            {
                if( m_model->m_config.readBool( "DisableAxis2", false ) ) break;
                m_model->changeMode( Mode::Taper );
                break;
            }
            case key::f2r: // X retraction setup
            {
                if( m_model->m_config.readBool( "DisableAxis2", false ) ) break;
                m_model->changeMode( Mode::Axis2RetractSetup );
                break;
            }
            case key::f2o: // Radius mode
            {
                if( m_model->m_config.readBool( "DisableAxis2", false ) ) break;
                m_model->changeMode( Mode::Radius );
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
                m_model->setIsAxis2Retracted( false );
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
    if( m_model->getIsAxis2Retracted() && ( key == key::UP || key == key::DOWN || key == key::W
        || key == key::w || key == key::s || key == key::S ) )
    {
        return -1;
    }
    switch( m_model->getCurrentDisplayMode() )
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
        case Mode::Axis1GoToOffset:
        case Mode::Axis2GoToOffset:
        case Mode::Radius:
            if( key >= key::ZERO && key <= key::NINE ) return key;
            if( key == key::FULLSTOP || key == key::BACKSPACE || key == key::DELETE ) return key;
            if( key == key::MINUS ) return key;
            return -1;
        default:
            // unhandled mode
            assert( false );
            return -1;
    }
}

int Controller::processModeInputKeys( int key )
{
    // If we are in a "mode" then certain keys (e.g. the number keys) are used for input
    // so are processed here before allowing them to fall through to the main key processing
    if( m_model->getCurrentDisplayMode() == Mode::Taper ||
        m_model->getCurrentDisplayMode() == Mode::Axis2PositionSetup ||
        m_model->getCurrentDisplayMode() == Mode::Axis1PositionSetup ||
        m_model->getCurrentDisplayMode() == Mode::Axis1GoTo ||
        m_model->getCurrentDisplayMode() == Mode::Axis2GoTo ||
        m_model->getCurrentDisplayMode() == Mode::Axis1GoToOffset ||
        m_model->getCurrentDisplayMode() == Mode::Axis2GoToOffset ||
        m_model->getCurrentDisplayMode() == Mode::Radius
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
            return -1;
        }
    }
    if(  m_model->getCurrentDisplayMode() == Mode::Threading )
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
                m_model->m_config.readDouble( "Axis1SpeedPreset2", 40.0 ) );
            // fall through...
        }
    }
    if(  m_model->getCurrentDisplayMode() == Mode::Axis2RetractSetup )
    {
        if( key == key::UP )
        {
            m_model->setRetractionDirection( XDirection::Inwards );
            return -1;
        }
        if( key == key::DOWN )
        {
            m_model->setRetractionDirection( XDirection::Outwards );
            return -1;
        }
    }

    // Diameter set
    if( m_model->getCurrentDisplayMode() == Mode::Axis2PositionSetup &&
        ( key == key::d || key == key::D ) )
    {
        float xPos = 0;
        try
        {
            xPos = std::abs( std::stof( m_model->m_input ) );
        }
        catch( ... ) {}
        m_model->m_axis2Motor->setPosition( xPos / 2 );
        // This will invalidate any memorised X positions, so we clear them
        for( auto& m : m_model->m_axis2Memory )
        {
            m = INF_OUT;
        }
        m_model->setCurrentDisplayMode( Mode::None );
        m_model->diameterIsSet();
        return -1;
    }

    if( m_model->getCurrentDisplayMode() != Mode::None && key == key::ENTER )
    {
        m_model->acceptInputValue();
        return -1;
    }
    return key;
}

int Controller::processLeaderKeyModeKeyPress( int keyPress )
{
    if( m_model->getKeyMode() == KeyMode::Axis1 ||
        m_model->getKeyMode() == KeyMode::Axis2 ||
        m_model->getKeyMode() == KeyMode::AxisAll )
    {
        int bitFlip = 0x1000; // bit 12
        if( m_model->getKeyMode() == KeyMode::Axis2 )
        {
            bitFlip = 0x2000; // bit 13
        }
        if( m_model->getKeyMode() == KeyMode::AxisAll )
        {
            bitFlip = 0x4000; // bit 14
        }
        return keyPress + bitFlip;
    }

    if( m_model->getKeyMode() == KeyMode::Function )
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
            case key::o:
            case key::O:
                keyPress = key::f2o;
                break;
            default:
                keyPress = key::None;
        }
    }
    m_model->setKeyMode( KeyMode::None );
    return keyPress;
}

int Controller::checkForAxisLeaderKeys( int key )
{
    // We determine whether the key pressed is a leader key for
    // an axis (note the key can be remapped in config) and sets
    // the model's keyMode if so. Returns true if one was pressed, false if not.
    if( key == static_cast<int>( m_model->m_config.readLong( "Axis1Leader", 122L ) ) )
    {
        m_model->setKeyMode( KeyMode::Axis1 );
        return key::None;
    }
    if( key == static_cast<int>( m_model->m_config.readLong( "Axis2Leader", 120L ) ) )
    {
        m_model->setKeyMode( KeyMode::Axis2 );
        return key::None;
    }
    if( key == key::BACKSLASH )
    {
        m_model->setKeyMode( KeyMode::AxisAll );
        return key::None;
    }
    return key;
}

} // end namespace
