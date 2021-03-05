#include "view_sfml.h"

#include "keycodes.h"
#include "model.h"
#include "threadpitches.h"

#include <cassert>
#include <fmt/format.h>

namespace mgo
{

namespace
{

std::string cnv( const mgo::StepperMotor* motor, long step, const std::string& units = "mm" )
{
    if( ! motor ) return std::string();
    double mm = motor->getPosition( step );
    if( std::abs( mm ) < 0.001 )
    {
        mm = 0.0;
    }
    return fmt::format( "{: .3f} {}", mm, units );
}

std::string cnv( const mgo::StepperMotor* motor, const std::string& units = "mm" )
{
    if( ! motor ) return std::string();
    return cnv( motor, motor->getCurrentStep(), units );
}

int convertKeyCode( sf::Event event )
{
    int sfKey = event.key.code;
    // Letters
    if( sfKey >= sf::Keyboard::A && sfKey <= sf::Keyboard::Z )
    {
        if( event.key.control )
        {
            // Ctrl-letter has 0x10000 ORed to it
            return ( key::a + sfKey ) | 0x10000;
        }
        // A is defined in SFML's enum as zero
        if( event.key.shift )
        {
            return key::A + sfKey;
        }
        return key::a + sfKey;
    }
    // Number keys
    if( sfKey >= sf::Keyboard::Num0 && sfKey <= sf::Keyboard::Num9 )
    {
        // Check for shift-8 (asterisk)
        if( sfKey == sf::Keyboard::Num8 && event.key.shift )
        {
            return key::ASTERISK;
        }
        return 22 + sfKey;
    }
    // Function keys
    if( sfKey >= sf::Keyboard::F1 && sfKey <= sf::Keyboard::F15 )
    {
        // F1 = 85 for SFML
        return 180 + sfKey;
    }
    // Misc
    switch( sfKey )
    {
        case sf::Keyboard::Return:
            return key::ENTER;
        case sf::Keyboard::LBracket: // [
            return key::LBRACKET;
        case sf::Keyboard::RBracket: // ]
            return key::RBRACKET;
        case sf::Keyboard::Comma:
            return key::COMMA;
        case sf::Keyboard::Period:
            return key::FULLSTOP;
        case sf::Keyboard::Right:
            return key::RIGHT;
        case sf::Keyboard::Left:
            return key::LEFT;
        case sf::Keyboard::Up:
            return key::UP;
        case sf::Keyboard::Down:
            return key::DOWN;
        case sf::Keyboard::BackSlash:
            if( event.key.shift ) return key::PIPE;
            return key::BACKSLASH;
        case sf::Keyboard::Dash:
            return key::MINUS;
        case sf::Keyboard::Equal:
            return key::EQUALS;
        case sf::Keyboard::Space:
            return key::SPACE;
        case sf::Keyboard::Escape:
            return key::ESC;
        case sf::Keyboard::BackSpace:
            return key::BACKSPACE;
        case sf::Keyboard::Delete:
            return key::DELETE;
        default:
            return -1;
    }
    return -1;
}

} // end anonymous namespace

void ViewSfml::initialise( const Model& model )
{
    #ifdef FAKE
        // run in a window in "fake" mode for manual testing
    m_window = std::make_unique<sf::RenderWindow>(
        sf::VideoMode(1024, 600, 32), "Lathe Control" );
    #else
    m_window = std::make_unique<sf::RenderWindow>(
        sf::VideoMode::getDesktopMode(), "Lathe Control", sf::Style::Fullscreen);
    #endif
    m_window->setKeyRepeatEnabled( false );
    m_window->setMouseCursorVisible( false );
    m_font = std::make_unique<sf::Font>();
    if (!m_font->loadFromFile("./DroidSansMono.ttf"))
    {
       throw std::runtime_error("Could not load TTF font");
    }
    m_txtAxis1Pos = std::make_unique<sf::Text>("", *m_font, 60 );
    m_txtAxis1Pos->setPosition( { 20, 10 });
    m_txtAxis1Pos->setFillColor( sf::Color::Green );

    m_txtAxis1Speed = std::make_unique<sf::Text>("", *m_font, 40 );
    m_txtAxis1Speed->setPosition( { 550, 30 });
    m_txtAxis1Speed->setFillColor( { 209, 209, 50 } );

    m_txtAxis2Pos = std::make_unique<sf::Text>("", *m_font, 60 );
    m_txtAxis2Pos->setPosition( { 20, 70 });
    m_txtAxis2Pos->setFillColor( sf::Color::Green );

    m_txtAxis2Speed = std::make_unique<sf::Text>("", *m_font, 40 );
    m_txtAxis2Speed->setPosition( { 550, 90 });
    m_txtAxis2Speed->setFillColor( { 209, 209, 50 } );

    m_txtRpm = std::make_unique<sf::Text>("", *m_font, 60 );
    m_txtRpm->setPosition( { 20, 130 });
    m_txtRpm->setFillColor( sf::Color::Green );

    for( int n = 0; n < 4; ++n )
    {
        auto lbl = std::make_unique<sf::Text>("", *m_font, 30 );
        lbl->setPosition( { 60.f + n * 200.f, 210 });
        lbl->setFillColor( { 128, 128, 128 } );
        lbl->setString( fmt::format( " Mem {}", n + 1 ) );
        m_txtMemoryLabel.push_back( std::move(lbl) );
        auto valZ = std::make_unique<sf::Text>("", *m_font, 30 );
        valZ->setPosition( { 60.f + n * 200.f, 245 });
        valZ->setFillColor( { 128, 128, 128 } );
        valZ->setString( " not set" );
        m_txtAxis1MemoryValue.push_back( std::move( valZ ) );
        auto valX = std::make_unique<sf::Text>("", *m_font, 30 );
        valX->setPosition( { 60.f + n * 200.f, 275 });
        valX->setFillColor( { 128, 128, 128 } );
        valX->setString( " not set" );
        m_txtAxis2MemoryValue.push_back( std::move( valX ) );
    }
    std::string axis1Label = model.m_config->read( "Axis1Label", "Z" ) + ":";
    m_txtAxis1MemoryLabel = std::make_unique<sf::Text>( axis1Label, *m_font, 30);
    m_txtAxis1MemoryLabel->setPosition( { 24.f, 245 });
    m_txtAxis1MemoryLabel->setFillColor( { 128, 128, 128 } );
    std::string axis2Label = model.m_config->read( "Axis2Label", "X" ) + ":";
    m_txtAxis2MemoryLabel = std::make_unique<sf::Text>( axis2Label, *m_font, 30);
    m_txtAxis2MemoryLabel->setPosition( { 24.f, 275 });
    m_txtAxis2MemoryLabel->setFillColor( { 128, 128, 128 } );

    m_txtGeneralStatus = std::make_unique<sf::Text>("", *m_font, 20 );
    m_txtGeneralStatus->setPosition( { 20, 550 });
    m_txtGeneralStatus->setFillColor( sf::Color::Green );

    m_txtAxis1Status = std::make_unique<sf::Text>("", *m_font, 20 );
    m_txtAxis1Status->setPosition( { 450, 550 });
    m_txtAxis1Status->setFillColor( sf::Color::Green );

    m_txtAxis2Status = std::make_unique<sf::Text>("", *m_font, 20 );
    m_txtAxis2Status->setPosition( { 700, 550 });
    m_txtAxis2Status->setFillColor( sf::Color::Green );

    m_txtMode = std::make_unique<sf::Text>( "", *m_font, 25 );
    m_txtMode->setPosition( { 20, 320 } );
    m_txtMode->setFillColor( sf::Color::Yellow );

    m_txtMisc1 = std::make_unique<sf::Text>( "", *m_font, 25 );
    m_txtMisc1->setPosition( { 20, 350 } );
    m_txtMisc1->setFillColor( sf::Color::White );

    m_txtMisc2 = std::make_unique<sf::Text>( "", *m_font, 25 );
    m_txtMisc2->setPosition( { 20, 380 } );
    m_txtMisc2->setFillColor( sf::Color::White );

    m_txtMisc3 = std::make_unique<sf::Text>( "", *m_font, 25 );
    m_txtMisc3->setPosition( { 20, 410 } );
    m_txtMisc3->setFillColor( sf::Color::White );

    m_txtMisc4 = std::make_unique<sf::Text>( "", *m_font, 25 );
    m_txtMisc4->setPosition( { 20, 440 } );
    m_txtMisc4->setFillColor( sf::Color::White );

    m_txtMisc5 = std::make_unique<sf::Text>( "", *m_font, 25 );
    m_txtMisc5->setPosition( { 20, 470 } );
    m_txtMisc5->setFillColor( sf::Color::White );

    m_txtWarning = std::make_unique<sf::Text>("", *m_font, 20 );
    m_txtWarning->setPosition( { 20, 510 });
    m_txtWarning->setFillColor( sf::Color::Red );

    m_txtTaperAngle= std::make_unique<sf::Text>("", *m_font, 30 );
    m_txtTaperAngle->setPosition( { 550, 160 });
    m_txtTaperAngle->setFillColor( sf::Color::Red );

    m_txtNotification = std::make_unique<sf::Text>("", *m_font, 25 );
    m_txtNotification->setPosition( { 860, 45 });
    m_txtNotification->setFillColor( sf::Color::Red );

    m_txtXRetracted = std::make_unique<sf::Text>("", *m_font, 25 );
    m_txtXRetracted->setPosition( { 860, 105 });
    m_txtXRetracted->setFillColor( sf::Color::Red );
    m_txtXRetracted->setString( "RETRACTED" );

    m_txtXRetractDirection = std::make_unique<sf::Text>("", *m_font, 25 );
    m_txtXRetractDirection->setPosition( { 860, 165 });
    m_txtXRetractDirection->setFillColor( sf::Color::Red );
    m_txtXRetractDirection->setString( "-X RTRCT" );
}

void ViewSfml::close()
{
    m_window->close();
}

int  ViewSfml::getInput()
{
    static sf::Keyboard::Key lastKey = sf::Keyboard::F15;
    static sf::Clock clock;
    static sf::Int32 lastTime = 0;
    sf::Event event;
    // This loop is to quickly discard anything that's not a keypress
    // e.g. mouse movement, which we're not interested in
    for(;;)
    {
        if( ! m_window->pollEvent( event ) ) return -1;
        if( event.type == sf::Event::KeyPressed )
        {
            break;
        }
    }

    // We only get here if a key was pressed
    if( lastKey == event.key.code && clock.getElapsedTime().asMilliseconds() - lastTime < 100 )
    {
        // Debounce
        return -1;
    }
    lastKey = event.key.code;
    lastTime = clock.getElapsedTime().asMilliseconds();
    return convertKeyCode( event );
}

void ViewSfml::updateDisplay( const Model& model )
{
    m_window->clear();
    updateTextFromModel( model );

    if( ! model.m_shutdown )
    {
        if( ! model.m_config->readBool( "DisableAxis1", false ) )
        {
            m_window->draw( *m_txtAxis1Pos );
            m_window->draw( *m_txtAxis1Speed );
            m_window->draw( *m_txtAxis1Status );
            m_window->draw( *m_txtAxis1MemoryLabel );
        }
        if( ! model.m_config->readBool( "DisableAxis2", false ) )
        {
            m_window->draw( *m_txtAxis2Pos );
            m_window->draw( *m_txtAxis2Speed );
            m_window->draw( *m_txtAxis2Status );
            m_window->draw( *m_txtAxis2MemoryLabel );
        }
        if( ! model.m_config->readBool( "DisableRpm", false ) )
        {
            m_window->draw( *m_txtRpm );
        }
        m_window->draw( *m_txtGeneralStatus );
        m_window->draw( *m_txtWarning );
        m_window->draw( *m_txtNotification );
        if( model.m_enabledFunction == Mode::Taper )
        {
            m_window->draw( *m_txtTaperAngle );
        }
        if( model.m_xRetractionDirection == XRetractionDirection::Inwards )
        {
            m_window->draw( *m_txtXRetractDirection );
        }
        if( model.m_axis2Retracted )
        {
            m_window->draw( *m_txtXRetracted );
        }
        for( std::size_t n = 0; n < m_txtMemoryLabel.size(); ++n )
        {
            m_window->draw( *m_txtMemoryLabel.at( n ) );
            if( ! model.m_config->readBool( "DisableAxis1", false ) )
            {
                m_window->draw( *m_txtAxis1MemoryValue.at( n ) );
            }
            if( ! model.m_config->readBool( "DisableAxis2", false ) )
            {
                m_window->draw( *m_txtAxis2MemoryValue.at( n ) );
            }
        }
        // Z/X labels - make red if keyMode corresponds
        if( model.m_keyMode == KeyMode::Axis1 )
        {
            m_txtAxis1MemoryLabel->setFillColor( sf::Color::Red );
        }
        else
        {
            m_txtAxis1MemoryLabel->setFillColor( { 128, 128, 128 });
        }
        if( model.m_keyMode == KeyMode::Axis2 )
        {
            m_txtAxis2MemoryLabel->setFillColor( sf::Color::Red );
        }
        else
        {
            m_txtAxis2MemoryLabel->setFillColor( { 128, 128, 128 });
        }
        if( model.m_currentDisplayMode != Mode::None )
        {
            m_window->draw( *m_txtMode );
        }
        if( model.m_currentDisplayMode != Mode::None )
        {
            m_window->draw( *m_txtMisc1 );
            m_window->draw( *m_txtMisc2 );
            m_window->draw( *m_txtMisc3 );
            m_window->draw( *m_txtMisc4 );
            m_window->draw( *m_txtMisc5 );
        }
    }
    m_window->display();
}

void ViewSfml::updateTextFromModel( const Model& model )
{
    // Updates all the text objects with data in the model
    if( model.m_shutdown )
    {
        m_txtAxis1Pos->setString( "SHUTTING DOWN" );
        return;
    }

    m_txtAxis1Pos->setString(
        fmt::format( "{}: {}",
            model.m_config->read( "Axis1Label", "Z" ),
            cnv( model.m_axis1Motor.get(), model.m_config->read( "Axis1DisplayUnits", "mm" ) )
            )
        );
    if( model.m_axis1Motor )
    {
        m_txtAxis1Speed->setString(
            fmt::format( "{:<.1f} mm/min", model.m_axis1Motor->getSpeed() ) );
    }
    if( ! model.m_axis2Retracted )
    {
        m_txtAxis2Pos->setString(
            fmt::format( "{}: {}",
                model.m_config->read( "Axis2Label", "X" ),
                cnv( model.m_axis2Motor.get(), model.m_config->read( "Axis2DisplayUnits", "mm" ) )
                )
            );
    }
    else
    {
        m_txtAxis2Pos->setString( fmt::format( "{}:  ---",
            model.m_config->read( "Axis2Label", "X" ) ) );
    }
    if( model.m_axis2Motor )
    {
        m_txtAxis2Speed->setString(
            fmt::format( "{:<.1f} mm/min", model.m_axis2Motor->getSpeed() ) );
    }
    if( model.m_rotaryEncoder )
    {
        m_txtRpm->setString( fmt::format( "C:  {:<4}  rpm",
            static_cast<int>( model.m_rotaryEncoder->getRpm() ) ) );
    }

    m_txtGeneralStatus->setString( model.m_generalStatus );
    std::string status = fmt::format( "{}: {}",
        model.m_config->read( "Axis1Label", "Z" ), model.m_axis1Status );
    m_txtAxis1Status->setString( status );
    status = fmt::format( "{}: {}",
        model.m_config->read( "Axis2Label", "X" ), model.m_axis2Status );
    m_txtAxis2Status->setString( status );
    m_txtWarning->setString( model.m_warning );
    if( model.m_enabledFunction == Mode::Taper )
    {
        m_txtTaperAngle->setString( fmt::format( "Angle: {}", model.m_taperAngle ) );
    }

    for( std::size_t n = 0; n < m_txtMemoryLabel.size(); ++n )
    {
        if( model.m_currentMemory == n )
        {
            m_txtMemoryLabel.at( n )->setFillColor( { 255, 255, 255 } );
            m_txtAxis1MemoryValue.at( n )->setFillColor( { 255, 255, 255 } );
            m_txtAxis2MemoryValue.at( n )->setFillColor( { 255, 255, 255 } );
        }
        else
        {
            m_txtMemoryLabel.at( n )->setFillColor( { 100, 100, 100 } );
            m_txtAxis1MemoryValue.at( n )->setFillColor( { 100, 100, 100 } );
            m_txtAxis2MemoryValue.at( n )->setFillColor( { 100, 100, 100 } );
        }
        if ( model.m_axis1Memory.at( n ) == INF_RIGHT )
        {
            m_txtAxis1MemoryValue.at( n )->setString( " not set" );
        }
        else
        {
            m_txtAxis1MemoryValue.at( n )->setString( fmt::format(
                "{:<12}", cnv( model.m_axis1Motor.get(), model.m_axis1Memory.at( n ) ) ) );
        }
        if ( model.m_axis2Memory.at( n ) == INF_OUT )
        {
            m_txtAxis2MemoryValue.at( n )->setString( " not set" );
        }
        else
        {
            m_txtAxis2MemoryValue.at( n )->setString(
                fmt::format(
                    "{:<12}", cnv( model.m_axis2Motor.get(), model.m_axis2Memory.at( n ) ) ) );
        }
    }

    switch( model.m_enabledFunction )
    {
        case Mode::Threading:
            m_txtNotification->setString( "THREADING" );
            break;
        case Mode::Taper:
            m_txtNotification->setString( "TAPERING" );
            break;
        default:
            m_txtNotification->setString( "" );
    }

    switch( model.m_currentDisplayMode )
    {
        case Mode::Help:
        {
            m_txtMode->setString( "Help" );
            m_txtMisc1->setString( "Modes: (F2=Leader) s=Setup t=Thread p=taPer r=Retract" );
            m_txtMisc2->setString( "" );
            m_txtMisc3->setString( "Z axis speed: 1-5, X axis speed: 6-0" );
            m_txtMisc4->setString( "[ and ] select mem to use. M store, Enter return (F fast)." );
            m_txtMisc5->setString( "WASD = nudge 0.025mm. Space to stop all motors. R retract." );
            m_txtWarning->setString( "Press Esc to exit help" );
            break;
        }
        case Mode::Setup:
        {
            m_txtMode->setString( "Setup" );
            m_txtMisc1->setString( "This mode allows you to determine backlash compensation" );
            m_txtMisc2->setString( "Use a dial indicator to find number of steps "
                                   "backlash per axis" );
            m_txtMisc3->setString( "REMEMBER to unset any previous-set backlash figures in "
                                   "config!" );
            m_txtMisc4->setString( "" );
            m_txtMisc5->setString( fmt::format("Z step: {}   X step: {}",
                    model.m_axis1Motor->getCurrentStep(),
                    model.m_axis2Motor->getCurrentStep()
                    )
                );
            m_txtWarning->setString( "Press Esc to exit setup" );
            break;
        }
        case Mode::Taper:
        {
            m_txtMode->setString( "Taper" );
            m_txtMisc1->setString( fmt::format( "Taper angle (degrees from centre): {}_",
                model.m_input ) );
            m_txtMisc2->setString( "" );
            m_txtMisc3->setString( "MT1 = 1.4287, MT2 = 1.4307, MT3 = 1.4377, MT4 = 1.4876" );
            m_txtMisc4->setString( "" );
            m_txtMisc5->setString( "NOTE! Arbitrary max angle = 60 degrees" );
            m_txtWarning->setString( "Enter to keep enabled, Esc to disable, Del to clear" );
            break;
        }
        case Mode::Threading:
        {
            m_txtMode->setString( "Thread" );
            ThreadPitch tp = threadPitches.at( model.m_threadPitchIndex );
            m_txtMisc1->setString( fmt::format( "Thread required: {}", tp.name ) );
            m_txtMisc2->setString(
                fmt::format( "Male   OD: {} mm, cut: {} mm", tp.maleOd, tp.cutDepthMale ) );
            m_txtMisc3->setString(
                fmt::format( "Female ID: {} mm, cut: {} mm", tp.femaleId, tp.cutDepthFemale ) );
            m_txtMisc4->setString( "" );
            m_txtMisc5->setString( "Press Up/Down to change." );
            m_txtWarning->setString( "Enter to keep enabled, Esc to disable" );
            break;
        }
        case Mode::Axis2RetractSetup:
        {
            m_txtMode->setString(  "X Axis retraction mode" );
            m_txtMisc1->setString( "Normal X retraction is 2mm outwards" );
            m_txtMisc2->setString( "In a boring operation, retraction should be INWARDS." );
            m_txtMisc3->setString( "Current setting: " );
            if( model.m_xRetractionDirection == XRetractionDirection::Inwards )
            {
                m_txtMisc4->setString( "Inwards (i.e. away from you, for boring)" );
            }
            else
            {
                m_txtMisc4->setString( "Normal (i.e. towards you)" );
            }
            m_txtMisc5->setString( "(Press up / down to change" );
            m_txtWarning->setString( "Enter to close screen" );
            break;
        }
        case Mode::Axis2PositionSetup:
        {
            m_txtMode->setString( fmt::format( "{} Position Set",
                model.m_config->read( "Axis2Label", "X" ) ) );
            m_txtMisc1->setString( "" );
            m_txtMisc2->setString( fmt::format( "Specify a value for {} here",
                model.m_config->read( "Axis2Label", "X" ) ) );
            m_txtMisc3->setString( "" );
            m_txtMisc4->setString( "" );
            m_txtMisc5->setString( fmt::format( "Current {} position: {}_",
                model.m_config->read( "Axis2Label", "X" ),
                model.m_input ) );
            m_txtWarning->setString( "Enter to set, 'D' to enter as diameter, Esc to cancel" );
            break;
        }
        case Mode::Axis1PositionSetup:
        {
            m_txtMode->setString( fmt::format( "{} Position Set",
                model.m_config->read( "Axis1Label", "Z" ) ) );
            m_txtMisc1->setString( "" );
            m_txtMisc2->setString( fmt::format( "Specify a value for {} here",
                model.m_config->read( "Axis1Label", "Z" ) ) );
            m_txtMisc3->setString( "" );
            m_txtMisc4->setString( "" );
            m_txtMisc5->setString( fmt::format( "Current {} position: {}_",
                model.m_config->read( "Axis1Label", "Z" ),
                model.m_input ) );
            m_txtWarning->setString( "Enter to set, Esc to cancel" );
            break;
        }
        case Mode::Axis1GoTo:
        {
            m_txtMode->setString( fmt::format( "Go To {} Position ",
                model.m_config->read( "Axis1Label", "Z" ) ) );
            m_txtMisc1->setString( "" );
            m_txtMisc2->setString( "Specify a value" );
            m_txtMisc3->setString( "" );
            m_txtMisc4->setString( "" );
            m_txtMisc5->setString( fmt::format( "Position: {}_",
                model.m_input ) );
            m_txtWarning->setString( "Enter to set, Esc to cancel" );
            break;
        }
        case Mode::Axis2GoTo:
        {
            m_txtMode->setString( fmt::format( "Go To {} Position ",
                model.m_config->read( "Axis2Label", "Z" ) ) );
            m_txtMisc1->setString( "" );
            m_txtMisc2->setString( "Specify a value" );
            m_txtMisc3->setString( "" );
            m_txtMisc4->setString( "" );
            m_txtMisc5->setString( fmt::format( "Position: {}_",
                model.m_input ) );
            m_txtWarning->setString( "Enter to set, Esc to cancel" );
            break;
        }
        case Mode::None:
        {
            m_txtMode->setString(  "" );
            m_txtMisc1->setString( "" );
            m_txtMisc2->setString( "" );
            m_txtMisc3->setString( "" );
            m_txtMisc4->setString( "" );
            m_txtMisc5->setString( "" );
            m_txtWarning->setString( "" );
            break;
        }
        default:
        {
            assert( false );
        }
    }
}


} // namespace mgo
