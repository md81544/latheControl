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
    // Function keys
    if( sfKey >= sf::Keyboard::F1 && sfKey <= sf::Keyboard::F15 )
    {
        // F1 = 85 for SFML
        return 180 + sfKey;
    }
    // Misc
    switch( sfKey )
    {
        case sf::Keyboard::Multiply: // *
            return 42;
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
            return 92;
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
    m_font = std::make_unique<sf::Font>();
    if (!m_font->loadFromFile("./DroidSansMono.ttf"))
    {
       throw std::runtime_error("Could not load TTF font");
    }
    m_txtZ = std::make_unique<sf::Text>("", *m_font, 50 );
    m_txtZ->setPosition( { 20, 10 });
    m_txtZ->setFillColor( sf::Color::Green );

    m_txtX = std::make_unique<sf::Text>("", *m_font, 50 );
    m_txtX->setPosition( { 20, 70 });
    m_txtX->setFillColor( sf::Color::Green );
}

void ViewSfml::close()
{
    m_window->close();
}

int  ViewSfml::getInput()
{
    sf::Event event;
    m_window->pollEvent( event );
    if( event.type == sf::Event::KeyPressed )
    {
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
    m_window->draw( *m_txtZ );
    m_window->draw( *m_txtX );
    m_window->display();
    
}

void ViewSfml::updateTextFromModel( const Model& model )
{
    // Updates all the text objects with data in the model
    m_txtZ->setString( fmt::format( "Z: {}", cnv( model.m_zAxisMotor.get() ) ) );
    m_txtX->setString( fmt::format( "X: {}", cnv( model.m_xAxisMotor.get() ) ) );
}


} // namespace mgo