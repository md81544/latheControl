#include "ui.h"
#include "stepperControl/igpio.h"
#include "threadpitches.h"  // for ThreadPitch, threadPitches

#include <stdlib.h>
#include <iomanip>
#include <sstream>


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

} // end anonymous namespace

namespace mgo
{

Ui::Ui( IGpio& gpio )
    :   m_gpio( gpio )
{
    // TODO: currently ignoring enable pin
    m_zAxisMotor = std::make_unique<mgo::StepperMotor>( m_gpio, 8, 7, 0, 1'000, -0.001 );
    m_xAxisMotor = std::make_unique<mgo::StepperMotor>( m_gpio, 20, 21, 0, 800, 1.0 / 2'400.0 );
    m_rotaryEncoder = std::make_unique<mgo::RotaryEncoder>( m_gpio, 23, 24, 2000, 35.f/30.f );
}

void Ui::run()
{
    using namespace mgo::Curses;

    m_zAxisMotor->setRpm( m_speed );
    m_xAxisMotor->setRpm( 60.0 );

    m_wnd.cursor( Cursor::off );
    m_wnd << "Up/down (or Fn keys) to set speed\n";
    m_wnd << "Left and right arrow to move, comma and full stop to nudge\n";
    m_wnd << "Space to stop\n";
    m_wnd << "Square brackets [ ] select memory store to use\n";
    m_wnd << "M remembers pos, and Enter returns to it (F "
             "for fast return)\n";
    m_wnd << "T - toggle thread cutting mode, P to choose thread pitch\n";
    m_wnd << "\\ - advance thread cut by " << std::setprecision(2) << SIDEFEED
          << "mm (suitable for " << std::setprecision(2) << INFEED << "mm in-feed)\n";
    m_wnd << "Escape or Q to quit\n\n";
    m_wnd.setBlocking( Input::nonBlocking );
    while( ! m_quit )
    {
        processKeyPress();

        if( ! m_threadCuttingOn || m_fastReturning )
        {
            m_zAxisMotor->setRpm( m_speed );
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
                m_zMoving = false;
            }
            m_zAxisMotor->setRpm( m_speed );
        }

        if( ! m_xMoving )
        {
            m_xAxisMotor->stop();
        }

        if( ! m_zMoving )
        {
            m_zAxisMotor->stop();
        }
        else
        {
            m_zAxisMotor->goToStep( m_targetStep );
        }
        if ( !m_zAxisMotor->isRunning() )
        {
            m_zMoving = false;
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
        m_gpio.delayMicroSeconds( 1'000 );
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
                m_zMoving = false;
                m_quit = true;
                break;
            }
            // Cross-slide support is currently just for testing
            case 88:   // X (shift-x)
            {
                m_xMoving = ! m_xMoving;
                if( m_xMoving )
                {
                    m_xAxisMotor->goToStep( INF_LEFT );
                }
                break;
            }
            case 120:  // x
            {
                m_xMoving = ! m_xMoving;
                if( m_xMoving )
                {
                    m_xAxisMotor->goToStep( INF_RIGHT );
                }
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
                m_memory.at( m_currentMemory ) = m_zAxisMotor->getCurrentStep();
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
                ++m_threadCutAdvanceCount;
                m_rotaryEncoder->setAdvanceValueMicroseconds(
                    m_threadCutAdvanceCount *
                    ( ( 1'000'000.f / ( m_speed / 60.f) ) * SIDEFEED )
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
                if( m_memory.at( m_currentMemory ) == INF_RIGHT ) break;
                // We always start at the same rotational position: it's
                // required for thread cutting, but doesn't impact
                // anything if we're just turning down, so we always do it.
                m_zAxisMotor->stop();
                m_zAxisMotor->wait();
                m_status = "returning";
                m_rotaryEncoder->callbackAtZeroDegrees([&]()
                    {
                        m_zMoving = true;
                        m_targetStep = m_memory.at( m_currentMemory );
                    }
                    );
                break;
            }
            case 260: // Left arrow
            {
                // Same key will cancel if we're already moving
                if ( m_zMoving )
                {
                    m_zMoving = false;
                    break;
                }
                m_status = "moving left";
                m_zMoving = true;
                m_targetStep = INF_LEFT;
                break;
            }
            case 261: // Right arrow
            {
                // Same key will cancel if we're already moving
                if ( m_zMoving )
                {
                    m_zMoving = false;
                    break;
                }
                m_status = "moving right";
                m_zMoving = true;
                m_targetStep = INF_RIGHT;
                break;
            }
            case 44: // comma (<) - nudge left
            {
                if ( m_zMoving )
                {
                    m_zAxisMotor->stop();
                    m_zAxisMotor->wait();
                }
                m_zMoving = true;
                m_targetStep = m_zAxisMotor->getCurrentStep() + 25;
                break;
            }
            case 46: // full stop (>) - nudge right
            {
                if ( m_zMoving )
                {
                    m_zAxisMotor->stop();
                    m_zAxisMotor->wait();
                }
                m_zMoving = true;
                m_targetStep = m_zAxisMotor->getCurrentStep() - 25;
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

            case 102: // f
            case 70:  // F
            {
                if( m_memory.at( m_currentMemory ) == INF_RIGHT ) break;
                // Fast return to point
                m_oldSpeed = m_speed;
                m_speed = MAX_MOTOR_SPEED;
                m_fastReturning = true;
                m_zAxisMotor->stop();
                m_zAxisMotor->wait();
                m_status = "fast returning";
                m_zMoving = true;
                m_targetStep = m_memory.at( m_currentMemory );
                break;
            }
            case 122: // z
            case 90:  // Z
            {
                // Zeroing can be confusing unless we
                // force a stop (i.e. the motor might start
                // as we are no longer at the target position)
                m_zAxisMotor->stop();
                m_targetStep = 0;
                m_zAxisMotor->zeroPosition();
                m_zMoving = false;
                break;
            }
            case 42: // asterisk, shutdown
            // Note the command used for shutdown should be made passwordless
            // in the /etc/sudoers files
            {
                #ifndef FAKE
                m_zAxisMotor->stop();
                m_zAxisMotor->wait();
                m_zMoving = false;
                m_quit = true;
                m_zAxisMotor.reset();
                system( "sudo systemctl poweroff --no-block" );
                #endif
                break;
            }
            default:
            {
                m_status = "stopped";
                m_zMoving = false;
                break;
            }
        }
    }
}

void Ui::updateDisplay()
{
    using namespace mgo::Curses;
    std::string targetString = cnv( m_zAxisMotor.get() );

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
        << cnv( m_zAxisMotor.get() ) << "\n";
    m_wnd.clearToEol();
    m_wnd << "X: " << cnv( m_xAxisMotor.get() ) << "\n";
    m_wnd.clearToEol();
    m_wnd << "Spindle RPM: " << static_cast<int>( m_rotaryEncoder->getRpm() ) << "\n";
    if( m_threadCuttingOn )
    {
        ThreadPitch tp = threadPitches.at( m_threadPitchIndex );
        m_wnd.clearToEol();
        m_wnd << "Pitch:       " << tp.pitchMm << " mm (" << tp.name << ")\n";
        m_wnd << "Current cut: " << m_threadCutAdvanceCount * INFEED << " mm\n";
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
        if ( m_memory.at( n ) != INF_RIGHT )
        {
            m_wnd << std::setw(12) << std::left << cnv( m_zAxisMotor.get(), m_memory.at( n ) );
        }
        else
        {
            m_wnd << std::setw(12) << "not set";
        }
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
