#include "view_sfml.h"

#include "model.h"
#include "threadpitches.h"

#include <fmt/format.h>

namespace mgo
{

namespace
{

std::string cnv( const mgo::StepperMotor* motor, long step )
{
    double mm = motor->getPosition( step );
    if( std::abs( mm ) < 0.001 )
    {
        mm = 0.0;
    }
    return fmt::format( "{: .3f} mm", mm );
}

std::string cnv( const mgo::StepperMotor* motor )
{
    return cnv( motor, motor->getCurrentStep() );
}

int convertKeyCode( sf::Event event )
{
    int sfKey = event.key.code;
    // Letters
    if( sfKey >= sf::Keyboard::A && sfKey <= sf::Keyboard::Z )
    {
        // A is defined in SFML's enum as zero
        if( event.key.shift )
        {
            return 65 + sfKey;
        }
        return 97 + sfKey;
    }
    // Number keys
    if( sfKey >= sf::Keyboard::Num0 && sfKey <= sf::Keyboard::Num9 )
    {
        // Check for shift-8 (asterisk)
        if( sfKey == sf::Keyboard::Num8 && event.key.shift )
        {
            return 42;
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
            return 10;
        case sf::Keyboard::LBracket: // [
            return 91;
        case sf::Keyboard::RBracket: // ]
            return 93;
        case sf::Keyboard::Comma:
            return 44;
        case sf::Keyboard::Period:
            return 46;
        case sf::Keyboard::Right:
            return 261;
        case sf::Keyboard::Left:
            return 260;
        case sf::Keyboard::Up:
            return 259;
        case sf::Keyboard::Down:
            return 258;
        case sf::Keyboard::BackSlash:
            if( event.key.shift ) return 124;
            return 92;
        case sf::Keyboard::Dash:
            return 45;
        case sf::Keyboard::Equal:
            return 61;
        case sf::Keyboard::Space:
            return 32;
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
        lbl->setPosition( { 4.f + n * 200.f, 225 });
        lbl->setFillColor( { 128, 128, 128 } );
        lbl->setString( fmt::format( " Memory {}", n + 1 ) );
        m_txtMemoryLabel.push_back( std::move(lbl) );
        auto val = std::make_unique<sf::Text>("", *m_font, 30 );
        val->setPosition( { 4.f + n * 200.f, 255 });
        val->setFillColor( { 128, 128, 128 } );
        val->setString( " not set" );
        m_txtMemoryValue.push_back( std::move( val ) );
    }

    m_txtStatus = std::make_unique<sf::Text>("", *m_font, 20 );
    m_txtStatus->setPosition( { 20, 550 });
    m_txtStatus->setFillColor( sf::Color::Green );

    m_txtTaperAngle = std::make_unique<sf::Text>( "", *m_font, 25 );
    m_txtTaperAngle->setPosition( { 20, 320 } );
    m_txtTaperAngle->setFillColor( sf::Color::Yellow );
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
        if( lastKey == event.key.code && clock.getElapsedTime().asMilliseconds() - lastTime < 250 )
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
    std::string t = cnv( model.m_zAxisMotor.get() );
    // TODO
    m_window->clear();
    updateTextFromModel( model );
    m_window->draw( *m_txtZPos );
    m_window->draw( *m_txtZSpeed );
    m_window->draw( *m_txtXPos );
    m_window->draw( *m_txtXSpeed );
    m_window->draw( *m_txtRpm );
    m_window->draw( *m_txtStatus );
    for( std::size_t n = 0; n < m_txtMemoryLabel.size(); ++n )
    {
        m_window->draw( *m_txtMemoryLabel.at( n ) );
        m_window->draw( *m_txtMemoryValue.at( n ) );
    }
    if( model.m_taperModeOn )
    {
        m_window->draw( *m_txtTaperAngle );
    }
    m_window->display();
    
}

void ViewSfml::updateTextFromModel( const Model& model )
{
    // Updates all the text objects with data in the model
    m_txtZPos->setString( fmt::format( "Z: {}", cnv( model.m_zAxisMotor.get() ) ) );
    m_txtZSpeed->setString( fmt::format( "{:<.1f} mm/min", model.m_zAxisMotor->getSpeed() ) );
    m_txtXPos->setString( fmt::format( "X: {}", cnv( model.m_xAxisMotor.get() ) ) );
    m_txtXSpeed->setString( fmt::format( "{:<.1f} mm/min", model.m_xAxisMotor->getSpeed() ) );
    m_txtRpm->setString( fmt::format( "C:  {:<4}  rpm", 
        static_cast<int>( model.m_rotaryEncoder->getRpm() ) ) );
    m_txtStatus->setString( fmt::format( "Status: {}    Debug: last keycode={}",
            model.m_status,
            model.m_keyPressed
            )
        );
    for( std::size_t n = 0; n < m_txtMemoryLabel.size(); ++n )
    {
        if( model.m_currentMemory == n )
        {
            m_txtMemoryLabel.at( n )->setFillColor( { 255, 255, 255 } );
            m_txtMemoryValue.at( n )->setFillColor( { 255, 255, 255 } );
        }
        else
        {
            m_txtMemoryLabel.at( n )->setFillColor( { 100, 100, 100 } );
            m_txtMemoryValue.at( n )->setFillColor( { 100, 100, 100 } );
        }
        if ( model.m_memory.at( n ) != INF_RIGHT )
        {
            m_txtMemoryValue.at( n )->setString(
                fmt::format( "{:<12}", cnv( model.m_zAxisMotor.get(), model.m_memory.at( n ) ) )
                );
        }
    }
    if( model.m_taperModeOn )
    {
        m_txtTaperAngle->setString( fmt::format( "Taper angle (degrees from centre): {:.3f}", 
            model.m_taperAngle ) );
    }
}


} // namespace mgo