#include "ui.h"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace
{

const int INF_RIGHT = -100'000'000;
const int INF_LEFT  =  100'000'000;

// This converts steps to real-world units.
// We can also negate the value depending
// on motor orientation (on a lathe the z-
// axis decreases in value towards the chuck)
std::string cnv( int steps )
{
    double mm = steps * -0.001;
    if( std::abs( mm ) < 0.001 )
    {
        mm = 0.0;
    }
    std::ostringstream oss;
    oss.precision(3);
    oss << std::fixed << mm << " mm";
    return oss.str();
}

} // end anonymous namespace

namespace mgo
{

// TODO:
//   T (tailstock) - mandatory set before operation
//   C (chuck) - mandatory set before operation
//   Add a "shutdown" command at the end to return
//   to "T" so we start in a known position?

Ui::Ui( IGpio& gpio )
    : m_gpio( gpio )
{
    m_motor = std::make_unique<mgo::StepperMotor>( m_gpio, 1'000 );
}

void Ui::run()
{
    using namespace mgo::Curses;
    m_motor->setRpm( m_speed );

    m_wnd.cursor( Cursor::off );
    m_wnd << "Up/down to set speed\n";
    m_wnd << "Left and right arrow to move, comma and stop to nudge\n";
    m_wnd << "Space to stop\n";
    m_wnd << "M to remember position, and R to return to it\n";
    m_wnd << "Escape or Q to quit\n";
    m_wnd.setBlocking( Input::nonBlocking );
    while( ! m_quit )
    {
        processKeyPress();

        m_motor->setRpm( m_speed );
        if( !m_moving )
        {
            m_motor->stop();
        }
        else
        {
            m_motor->goToStep( m_targetStep );
        }
        if ( !m_motor->isRunning() )
        {
            m_status = "stopped";
        }

        updateDisplay();
    }
}

void Ui::processKeyPress()
{
    int t = m_wnd.getKey();
    if( t != -1 )
    {
        m_keyPressed = t;
        switch( m_keyPressed )
        {
            case -1:
            {
                break;
            }
            case 81:  // Q
            case 113: // q
            case 27:  // Esc
            {
                m_moving = false;
                m_quit = true;
                break;
            }
            case 259:
            {
                beep();
                if( m_speed < 900 ) m_speed += 20;
                break;
            }
            case 258:
            {
                beep();
                if( m_speed > 20 ) m_speed -= 20;
                break;
            }
            case 260: // Left arrow
            {
                m_status = "moving left";
                m_moving = true;
                m_targetStep = INF_LEFT;
                break;
            }
            case 77:  // M
            case 109: // m
            {
                m_memory.at( m_currentMemory ) = m_motor->getCurrentStep();
                break;
            }
            case 82:  // R
            case 114: // r
            {
                m_motor->stop();
                m_motor->wait();
                m_status = "returning";
                m_moving = true;
                m_targetStep = m_memory.at( m_currentMemory );
                break;
            }
            case 261: // Right arrow
            {
                m_status = "moving right";
                m_moving = true;
                m_targetStep = INF_RIGHT;
                break;
            }
            case 44: // comma (<) - nudge left
            {
                m_moving = true;
                m_targetStep = m_motor->getCurrentStep() + 50;
                break;
            }
            case 46: // full stop (>) - nudge right
            {
                m_moving = true;
                m_targetStep = m_motor->getCurrentStep() - 50;
                break;
            }
            case 49: // 1
            case 50: // 2
            case 51: // 3
            case 52: // 4
            {
                m_currentMemory = static_cast<std::size_t>( m_keyPressed - 49 );
                break;
            }
            default:
            {
                m_status = "stopped";
                m_moving = false;
                break;
            }
        }
    }
}

void Ui::updateDisplay()
{
    using namespace mgo::Curses;
    std::string targetString = cnv( m_targetStep );

    if ( m_targetStep == INF_LEFT  ) targetString = "<----";
    if ( m_targetStep == INF_RIGHT ) targetString = "---->";

    m_wnd.move( 6, 0 );
    m_wnd.setColour( Colours::yellowOnBlack );
    m_wnd.clearToEol();
    m_wnd << "Status:   "  << std::setw(3) << std::left << m_speed << " rpm   ";
    m_wnd << m_status << "\n";
    m_wnd.clearToEol();
    m_wnd << "Target:   " << targetString << ", current: "
        << cnv( m_motor->getCurrentStep() ) << "\n\n";

    m_wnd.clearToEol();
    highlightCheck( 0 );
    m_wnd << "Memory 1     ";
    highlightCheck( 1 );
    m_wnd << "Memory 2     ";
    highlightCheck( 2 );
    m_wnd << "Memory 3     ";
    highlightCheck( 3 );
    m_wnd << "Memory 4\n";
    m_wnd.clearToEol();

    highlightCheck( 0 );
    m_wnd << cnv( m_memory.at( 0 ) ) << "     ";
    highlightCheck( 1 );
    m_wnd << cnv( m_memory.at( 1 ) ) << "     ";
    highlightCheck( 2 );
    m_wnd << cnv( m_memory.at( 2 ) ) << "     ";
    highlightCheck( 3 );
    m_wnd << cnv( m_memory.at( 3 ) ) << "\n\n";

    m_wnd.setColour( Colours::greenOnBlack );
    m_wnd.clearToEol();
    m_wnd << "Keypress: " << m_keyPressed << "\n";
    m_wnd.refresh();
}

void Ui::highlightCheck( size_t memoryLocation )
{
    using namespace mgo::Curses;
    if ( memoryLocation == m_currentMemory )
    {
        m_wnd.setColour( Colours::whiteOnBlack );
    }
    else
    {
        m_wnd.setColour( Colours::redOnBlack );
    }
}

} // end namespace
