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

std::string cnv( const mgo::StepperMotor* motor, long step )
{
    if( ! motor ) return std::string();
    double mm = motor->getPosition( step );
    if( std::abs( mm ) < 0.001 )
    {
        mm = 0.0;
    }
    return fmt::format( "{: .3f} mm", mm );
}

std::string cnv( const mgo::StepperMotor* motor )
{
    if( ! motor ) return std::string();
    return cnv( motor, motor->getCurrentStep() );
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

void ViewSfml::initialise()
{
    #ifdef FAKE
        // run in a window in "fake" mode for manual testing
    m_window = std::make_unique<sf::RenderWindow>(
        sf::VideoMode(1024, 600, 32), "Electronic Lead Screw" );
    #else
    m_window = std::make_unique<sf::RenderWindow>(
        sf::VideoMode::getDesktopMode(), "Electronic Lead Screw", sf::Style::Fullscreen);
    #endif
    m_window->setKeyRepeatEnabled( false );
    m_window->setMouseCursorVisible( false );
    m_font = std::make_unique<sf::Font>();
    if (!m_font->loadFromFile("./DroidSansMono.ttf"))
    {
       throw std::runtime_error("Could not load TTF font");
    }
    m_txtZPos = std::make_unique<sf::Text>("", *m_font, 60 );
    m_txtZPos->setPosition( { 20, 10 });
    m_txtZPos->setFillColor( sf::Color::Green );

    m_txtZSpeed = std::make_unique<sf::Text>("", *m_font, 40 );
    m_txtZSpeed->setPosition( { 550, 30 });
    m_txtZSpeed->setFillColor( { 209, 209, 50 } );

    m_txtXPos = std::make_unique<sf::Text>("", *m_font, 60 );
    m_txtXPos->setPosition( { 20, 70 });
    m_txtXPos->setFillColor( sf::Color::Green );

    m_txtXSpeed = std::make_unique<sf::Text>("", *m_font, 40 );
    m_txtXSpeed->setPosition( { 550, 90 });
    m_txtXSpeed->setFillColor( { 209, 209, 50 } );

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
        m_txtZMemoryValue.push_back( std::move( valZ ) );
        auto valX = std::make_unique<sf::Text>("", *m_font, 30 );
        valX->setPosition( { 60.f + n * 200.f, 275 });
        valX->setFillColor( { 128, 128, 128 } );
        valX->setString( " not set" );
        m_txtXMemoryValue.push_back( std::move( valX ) );
    }
    m_txtZMemoryLabel = std::make_unique<sf::Text>("Z:", *m_font, 30);
    m_txtZMemoryLabel->setPosition( { 24.f, 245 });
    m_txtZMemoryLabel->setFillColor( { 128, 128, 128 } );
    m_txtXMemoryLabel = std::make_unique<sf::Text>("X:", *m_font, 30);
    m_txtXMemoryLabel->setPosition( { 24.f, 275 });
    m_txtXMemoryLabel->setFillColor( { 128, 128, 128 } );

    m_txtGeneralStatus = std::make_unique<sf::Text>("", *m_font, 20 );
    m_txtGeneralStatus->setPosition( { 20, 550 });
    m_txtGeneralStatus->setFillColor( sf::Color::Green );

    m_txtZStatus = std::make_unique<sf::Text>("", *m_font, 20 );
    m_txtZStatus->setPosition( { 450, 550 });
    m_txtZStatus->setFillColor( sf::Color::Green );

    m_txtXStatus = std::make_unique<sf::Text>("", *m_font, 20 );
    m_txtXStatus->setPosition( { 700, 550 });
    m_txtXStatus->setFillColor( sf::Color::Green );

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
    m_window->pollEvent( event );
    if( event.type == sf::Event::KeyPressed )
    {
        if( lastKey == event.key.code && clock.getElapsedTime().asMilliseconds() - lastTime < 100 )
        {
            // Debounce
            return -1;
        }
        lastKey = event.key.code;
        lastTime = clock.getElapsedTime().asMilliseconds();
        return convertKeyCode( event );
    }
    return -1;
}

void ViewSfml::updateDisplay( const Model& model )
{
    m_window->clear();
    updateTextFromModel( model );
    m_window->draw( *m_txtZPos );

    // If we're shutting down there's a "shutting down" message in m_txtZpos,
    // we don't want to display anything else
    if( ! model.m_shutdown )
    {
        m_window->draw( *m_txtZSpeed );
        m_window->draw( *m_txtXPos );
        m_window->draw( *m_txtXSpeed );
        m_window->draw( *m_txtRpm );
        m_window->draw( *m_txtGeneralStatus );
        m_window->draw( *m_txtZStatus );
        m_window->draw( *m_txtXStatus );
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
        if( model.m_xRetracted )
        {
            m_window->draw( *m_txtXRetracted );
        }
        for( std::size_t n = 0; n < m_txtMemoryLabel.size(); ++n )
        {
            m_window->draw( *m_txtMemoryLabel.at( n ) );
            m_window->draw( *m_txtZMemoryValue.at( n ) );
            m_window->draw( *m_txtXMemoryValue.at( n ) );
        }
        // Z/X labels - make red if keyMode corresponds
        if( model.m_keyMode == KeyMode::ZAxis )
        {
            m_txtZMemoryLabel->setFillColor( sf::Color::Red );
        }
        else
        {
            m_txtZMemoryLabel->setFillColor( { 128, 128, 128 });
        }
        if( model.m_keyMode == KeyMode::XAxis )
        {
            m_txtXMemoryLabel->setFillColor( sf::Color::Red );
        }
        else
        {
            m_txtXMemoryLabel->setFillColor( { 128, 128, 128 });
        }
        m_window->draw( *m_txtZMemoryLabel );
        m_window->draw( *m_txtXMemoryLabel );
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
        m_txtZPos->setString( "SHUTTING DOWN" );
        return;
    }

    m_txtZPos->setString( fmt::format( "Z: {}", cnv( model.m_zAxisMotor.get() ) ) );
    if( model.m_zAxisMotor )
    {
        m_txtZSpeed->setString( fmt::format( "{:<.1f} mm/min", model.m_zAxisMotor->getSpeed() ) );
    }
    if( ! model.m_xRetracted )
    {
        m_txtXPos->setString( fmt::format( "X: {}", cnv( model.m_xAxisMotor.get() ) ) );
    }
    else
    {
        m_txtXPos->setString( "X:  ---" );
    }
    if( model.m_xAxisMotor )
    {
        m_txtXSpeed->setString( fmt::format( "{:<.1f} mm/min", model.m_xAxisMotor->getSpeed() ) );
    }
    if( model.m_rotaryEncoder )
    {
        m_txtRpm->setString( fmt::format( "C:  {:<4}  rpm",
            static_cast<int>( model.m_rotaryEncoder->getRpm() ) ) );
    }

    m_txtGeneralStatus->setString( model.m_generalStatus );
    std::string status = fmt::format( "Z: {}", model.m_zStatus );
    m_txtZStatus->setString( status );
    status = fmt::format( "X: {}", model.m_xStatus );
    m_txtXStatus->setString( status );
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
            m_txtZMemoryValue.at( n )->setFillColor( { 255, 255, 255 } );
            m_txtXMemoryValue.at( n )->setFillColor( { 255, 255, 255 } );
        }
        else
        {
            m_txtMemoryLabel.at( n )->setFillColor( { 100, 100, 100 } );
            m_txtZMemoryValue.at( n )->setFillColor( { 100, 100, 100 } );
            m_txtXMemoryValue.at( n )->setFillColor( { 100, 100, 100 } );
        }
        if ( model.m_zMemory.at( n ) == INF_RIGHT )
        {
            m_txtZMemoryValue.at( n )->setString( " not set" );
        }
        else
        {
            m_txtZMemoryValue.at( n )->setString(
                fmt::format( "{:<12}", cnv( model.m_zAxisMotor.get(), model.m_zMemory.at( n ) ) )
                );
        }
        if ( model.m_xMemory.at( n ) == INF_OUT )
        {
            m_txtXMemoryValue.at( n )->setString( " not set" );
        }
        else
        {
            m_txtXMemoryValue.at( n )->setString(
                fmt::format( "{:<12}", cnv( model.m_xAxisMotor.get(), model.m_xMemory.at( n ) ) )
                );
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
            m_txtMisc2->setString( "Use a dial indicator to find number of steps backlash per axis" );
            m_txtMisc3->setString( "REMEMBER to unset any previous-set backlash figures in config!" );
            m_txtMisc4->setString( "" );
            m_txtMisc5->setString( fmt::format("Z step: {}   X step: {}",
                    model.m_zAxisMotor->getCurrentStep(),
                    model.m_xAxisMotor->getCurrentStep()
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
        case Mode::XRetractSetup:
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
        case Mode::XPositionSetup:
        {
            m_txtMode->setString( "X Position Setup" );
            m_txtMisc1->setString( "" );
            m_txtMisc2->setString( "Specify a value for X here" );
            m_txtMisc3->setString( "NOTE! X would normally be negative" );
            m_txtMisc4->setString( "" );
            m_txtMisc5->setString( fmt::format( "Current X position: {}_",
                model.m_input ) );
            m_txtWarning->setString( "Enter to set, Esc to cancel" );
            break;
        }
        case Mode::ZPositionSetup:
        {
            m_txtMode->setString( "Z Position Setup" );
            m_txtMisc1->setString( "" );
            m_txtMisc2->setString( "Specify a value for Z here" );
            m_txtMisc3->setString( "" );
            m_txtMisc4->setString( "" );
            m_txtMisc5->setString( fmt::format( "Current Z position: {}_",
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
