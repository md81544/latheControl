#include "ui.h"
#include "log.h"

#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace
{

const int INF_RIGHT = std::numeric_limits<int>::min();
const int INF_LEFT  = std::numeric_limits<int>::max(); 

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

Ui::Ui( IGpio& gpio )
    :   m_gpio( gpio )
{
    m_motor = std::make_unique<mgo::StepperMotor>( m_gpio, 1'000 );
    m_rotaryEncoder = std::make_unique<mgo::RotaryEncoder>( m_gpio, 23, 24, 2000, 35.f/30.f );
}

void Ui::run()
{
    using namespace mgo::Curses;
    m_motor->setRpm( m_speed );

    m_wnd.cursor( Cursor::off );
    m_wnd << "Up/down (or Fn keys) to set speed\n";
    m_wnd << "Left and right arrow to move, comma and stop to nudge\n";
    m_wnd << "Space to stop\n";
    m_wnd << "Square brackets [ ] select memory store to use\n";
    m_wnd << "M to remember position, and R to return to it (shift-F "
             "for fast return)\n";
    m_wnd << "T - toggle thread cutting mode, P to choose thread pitch\n";
    m_wnd << "\\ - advance thread cut by 0.05mm (suitable for 0.1mm in-feed\n";
    m_wnd << "Escape or Q to quit\n\n";
    m_wnd.setBlocking( Input::nonBlocking );
    while( ! m_quit )
    {
        processKeyPress();

        if( ! m_threadCuttingOn || m_fastReturning )
        {
            m_motor->setRpm( m_speed );
        }
        else
        {
            // We are cutting threads, so the stepper motor's speed
            // is dependent on the spindle's RPM and the thread pitch.
            float pitch = threadPitches.at( m_threadPitchIndex ).pitchMm;
            // because my stepper motor / leadscrew does one mm per
            // revolution, there is a direct correlation between spindle
            // rpm and stepper motor rpm for a 1mm thread pitch.
            m_speed = pitch * m_rotaryEncoder->getRpm();
            if( m_speed > MAX_MOTOR_SPEED )
            {
                m_speed = MAX_MOTOR_SPEED;
                m_moving = false;
            }
            m_motor->setRpm( m_speed );
        }
        
        if( ! m_moving )
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
            if( m_fastReturning )
            {
                m_speed = m_oldSpeed;
                m_fastReturning = false;
            }
        }

        updateDisplay();
        // Small delay just to avoid the loop spinning
        // at full speed
        m_gpio.delayMicroSeconds( 5'000 );
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
            case 259: // Up arrow
            {
                if( m_threadCuttingOn ) break;
                if( m_speed < 20 )
                {
                    m_speed = 20;
                }
                else
                {
                    if( m_speed < MAX_MOTOR_SPEED ) m_speed += 20;
                }
                break;
            }
            case 258: // Down arrow
            {
                if( m_threadCuttingOn ) break;
                if( m_speed > 20 )
                {
                    m_speed -= 20;
                }
                else
                {
                    if (m_speed > 1) --m_speed;
                }
                break;
            }
            case 77:  // M
            case 109: // m
            {
                m_memory.at( m_currentMemory ) = m_motor->getCurrentStep();
                break;
            }
            case 84:   // T
            case 116:  // t
            {
                // Toggle threading on/off
                m_threadCuttingOn = ! m_threadCuttingOn;
                break;
            }
            case 112:  // p
            {
                if ( ! m_threadCuttingOn ) break;
                // change threading pitch up
                ++m_threadPitchIndex;
                if( m_threadPitchIndex >= threadPitches.size() )
                {
                    m_threadPitchIndex = 0;
                }
                break;
            }
            case 80:  // P change thread pitch
            {
                if ( ! m_threadCuttingOn ) break;
                // change threading pitch down
                if( m_threadPitchIndex == 0 )
                {
                    m_threadPitchIndex = threadPitches.size() - 1;
                }
                else
                {
                    --m_threadPitchIndex;
                }
                break;
            }
            case 92:  // \ (backslash)
            {
                // causes the cut to start a fraction earlier
                // next time, this simulates feeding in at 29.5°
                // The use should feed in 0.1mm, then press this
                // key to cause an advance of 0.05658mm, i.e. 0.05658
                // revolutions, so we need to advance the callback by
                ++m_threadCutAdvanceCount;
                m_rotaryEncoder->setAdvanceValueMicroseconds(
                    m_threadCutAdvanceCount *
                    ( ( 1'000'000.f / ( m_speed / 60.f) ) * 0.05658f )
                    );
                break;
            }
            case 124: // | (pipe, i.e. shift backslash
            {
                // decrement the cut advance count
                if( m_threadCutAdvanceCount > 0 )
                {
                    --m_threadCutAdvanceCount;
                }
                break;
            }
            case 10:  // ENTER
            case 82:  // R
            case 114: // r
            {
                // We always start at the same rotational position: it's
                // required for thread cutting, but doesn't impact
                // anything if we're just turning down, so we always do it.
                m_motor->stop();
                m_motor->wait();
                m_status = "returning";
                m_rotaryEncoder->callbackAtZeroDegrees([&]()
                    {
                        m_moving = true;
                        m_targetStep = m_memory.at( m_currentMemory );
                    }
                    );
                break;
            }
            case 260: // Left arrow
            {
                // Same key will cancel if we're already moving
                if ( m_moving )
                {
                    m_moving = false;
                    break;
                }
                if ( m_moving && m_targetStep < m_motor->getCurrentStep() )
                {
                    m_motor->stop();
                    m_motor->wait();
                }
                m_status = "moving left";
                m_moving = true;
                m_targetStep = INF_LEFT;
                break;
            }
            case 261: // Right arrow
            {
                // Same key will cancel if we're already moving
                if ( m_moving )
                {
                    m_moving = false;
                    break;
                }
                if ( m_moving && m_targetStep > m_motor->getCurrentStep() )
                {
                    m_motor->stop();
                    m_motor->wait();
                }
                m_status = "moving right";
                m_moving = true;
                m_targetStep = INF_RIGHT;
                break;
            }
            case 44: // comma (<) - nudge left
            {
                if ( m_moving )
                {
                    m_motor->stop();
                    m_motor->wait();
                }
                m_moving = true;
                m_targetStep = m_motor->getCurrentStep() + 25;
                break;
            }
            case 46: // full stop (>) - nudge right
            {
                if ( m_moving )
                {
                    m_motor->stop();
                    m_motor->wait();
                }
                m_moving = true;
                m_targetStep = m_motor->getCurrentStep() - 25;
                break;
            }
            case 91: // [
            {
                if( m_currentMemory > 0 )
                {
                    --m_currentMemory;
                }
                break;
            }
            case 93: // ]
            {
                if( m_currentMemory < m_memory.size() - 1 )
                {
                    ++m_currentMemory;
                }
                break;
            }

            // Speed presets with Function keys

            case 265: // F1
            {
                if( ! m_threadCuttingOn ) m_speed = 20;
                break;
            }
            case 266: // F2
            {
                if( ! m_threadCuttingOn ) m_speed = 40;
                break;
            }
            case 267: // F3
            {
                if( ! m_threadCuttingOn ) m_speed = 100;
                break;
            }
            case 268: // F4
            {
                if( ! m_threadCuttingOn ) m_speed = 200;
                break;
            }
            case 269: // F5
            {
                if( ! m_threadCuttingOn ) m_speed = 250;
                break;
            }
            case 270: // F6
            {
                if( ! m_threadCuttingOn ) m_speed = 300;
                break;
            }
            case 271: // F7
            {
                if( ! m_threadCuttingOn ) m_speed = 350;
                break;
            }
            case 272: // F8
            {
                if( ! m_threadCuttingOn ) m_speed = 400;
                break;
            }
            case 273: // F9
            {
                if( ! m_threadCuttingOn ) m_speed = 450;
                break;
            }
            case 274: // F10
            {
                if( ! m_threadCuttingOn ) m_speed = 500;
                break;
            }
            case 275: // F11
            {
                if( ! m_threadCuttingOn ) m_speed = 550;
                break;
            }
            case 276: // F12
            {
                if( ! m_threadCuttingOn ) m_speed = MAX_MOTOR_SPEED;
                break;
            }

            case 70:  // F (note uppercase ONLY)
            {
                // Fast return to point
                m_oldSpeed = m_speed;
                m_speed = MAX_MOTOR_SPEED;
                m_fastReturning = true;
                m_motor->stop();
                m_motor->wait();
                m_status = "fast returning";
                m_moving = true;
                m_targetStep = m_memory.at( m_currentMemory );
                break;
            }
            case 122: // z
            case 90:  // Z
            {
                // Zeroing can be confusing unless we
                // force a stop (i.e. the motor might start
                // as we are no longer at the target position)
                m_motor->stop();
                m_targetStep = 0;
                m_motor->zeroPosition();
                m_moving = false;
                break;
            }
            case 42: // asterisk, shutdown
            {
                #ifndef FAKE
                m_motor->stop();
                m_motor->wait();
                m_moving = false;
                m_quit = true;
                m_motor.reset();
                system( "sudo shutdown -h now" );
                #endif
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

    m_wnd.move( 9, 0 );
    m_wnd.setColour( Colours::yellowOnBlack );
    m_wnd.clearToEol();
    m_wnd << "Tool speed:  "  << std::setw(3) << std::left << static_cast<int>( m_speed )
        << " mm/min.   ";
    m_wnd << m_status << "\n";
    m_wnd.clearToEol();
    m_wnd << "Target:      " << targetString << ", current: "
        << cnv( m_motor->getCurrentStep() ) << "\n";
    m_wnd.clearToEol();
    m_wnd << "Spindle RPM: " << static_cast<int>( m_rotaryEncoder->getRpm() ) << "\n";
    if( m_threadCuttingOn )
    {
        ThreadPitch tp = threadPitches.at( m_threadPitchIndex );
        m_wnd.clearToEol();
        m_wnd << "Pitch:       " << tp.pitchMm << " mm (" << tp.name << ")\n";
        m_wnd << "Current cut: " << m_threadCutAdvanceCount / 10.f << " mm\n";
        m_wnd.setColour( Colours::cyanOnBlack );
        m_wnd.clearToEol();
        m_wnd << "    ( Male   OD: " << tp.maleOd << " mm, cut: " << tp.cutDepthMale << " mm )\n";
        m_wnd.clearToEol();
        m_wnd << "    ( Female ID: " << tp.femaleId << " mm, cut: " << tp.cutDepthFemale
            << " mm )\n";
        m_wnd.clearToEol();
        m_wnd.setColour( Colours::yellowOnBlack );
    }

    m_wnd << "\n";
    // uncomment for keycodes m_wnd << m_keyPressed << "\n";

    // Memory labels
    m_wnd.clearToEol();
    for ( int n = 0; n < 4; ++n )
    {
        std::string label = "Memory " + std::to_string( n + 1 );
        highlightCheck( n );
        m_wnd << std::setw(12) << label;
    }
    m_wnd << "\n";

    // Memory values
    m_wnd.clearToEol();
    for ( int n = 0; n < 4; ++n )
    {
        highlightCheck( n );
        m_wnd << std::setw(12) << std::left
            << cnv( m_memory.at( n ) );
    }

    // Clear a few extra lines as pitch information can push things up/down
    for( int n = 0; n < 6; ++n )
    {
        m_wnd << "\n";
        m_wnd.clearToEol();
    }

    m_wnd.refresh();
}

void Ui::highlightCheck( std::size_t memoryLocation )
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
