#include "view_sfml.h"

#include "model.h"
#include "threadpitches.h"


#include <iomanip>

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
    std::ostringstream oss;
    oss.precision(3);
    oss << std::fixed << mm << " mm";
    return oss.str();
}

std::string cnv( const mgo::StepperMotor* motor )
{
    return cnv( motor, motor->getCurrentStep() );
}

int convertKeyCode( sf::Keyboard::Key sfKey )
{
    // Letters
    if( sfKey >= sf::Keyboard::A && sfKey <= sf::Keyboard::Z )
    {
        // A is defined in SFML's enum as zero
        return 65 + sfKey;
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
    m_txtZPosition = std::make_unique<sf::Text>("", *m_font, 50 );
    m_txtZPosition->setPosition( { 20, 10 });
    m_txtZPosition->setFillColor( sf::Color::Green );
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
        MGOLOG( std::to_string( event.key.code ) );
        return convertKeyCode( event.key.code );
    }
    return -1;
}

void ViewSfml::updateDisplay( const Model& model )
{
    std::string t = cnv( model.m_zAxisMotor.get() );
    // TODO
    m_window->clear();
    m_txtZPosition->setString( "Z: " + std::to_string( model.m_keyPressed ));
    m_window->draw( *m_txtZPosition );
    m_window->display();
    
}

} // namespace mgo