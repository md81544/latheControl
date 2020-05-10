#include "controller.h"

#include "stepperControl/igpio.h"
#include "view_curses.h"
#include "view_sfml.h"
#include "threadpitches.h"  // for ThreadPitch, threadPitches

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
    
    m_view->initialise();

    // TODO: currently ignoring enable pin
    m_model->m_zAxisMotor = std::make_unique<mgo::StepperMotor>(
        m_model->m_gpio, 8, 7, 0, 1'000, -0.001 );
    m_model->m_xAxisMotor = std::make_unique<mgo::StepperMotor>(
        m_model->m_gpio, 20, 21, 0, 800, 1.0 / 2'400.0 );
    m_model->m_rotaryEncoder = std::make_unique<mgo::RotaryEncoder>(
        m_model->m_gpio, 23, 24, 2000, 35.f/30.f );
}

void Controller::run()
{
    m_model->m_zAxisMotor->setRpm( m_model->m_zSpeed );
    m_model->m_xAxisMotor->setRpm( m_model->m_xSpeed );

    while( ! m_model->m_quit )
    {
        processKeyPress();

        if( ! m_model->m_threadCuttingOn || m_model->m_fastReturning )
        {
            m_model->m_zAxisMotor->setRpm( m_model->m_zSpeed );
        }
        else
        {
            // We are cutting threads, so the stepper motor's speed
            // is dependent on the spindle's RPM and the thread pitch.
            float pitch = threadPitches.at( m_model->m_threadPitchIndex ).pitchMm;
            // because my stepper motor / leadscrew does one mm per
            // revolution, there is a direct correlation between spindle
            // rpm and stepper motor rpm for a 1mm thread pitch.
            m_model->m_zSpeed = pitch * m_model->m_rotaryEncoder->getRpm();
            if( m_model->m_zSpeed > MAX_MOTOR_SPEED )
            {
                m_model->m_zSpeed = MAX_MOTOR_SPEED;
                m_model->m_zMoving = false;
            }
            m_model->m_zAxisMotor->setRpm( m_model->m_zSpeed );
        }

        if( ! m_model->m_zMoving )
        {
            m_model->m_zAxisMotor->stop();
        }
        else
        {
            m_model->m_zAxisMotor->goToStep( m_model->m_targetStep );
        }
        if ( !m_model->m_zAxisMotor->isRunning() )
        {
            m_model->m_zMoving = false;
            m_model->m_status = "stopped";
            if( m_model->m_fastReturning )
            {
                m_model->m_zSpeed = m_model->m_oldZSpeed;
                m_model->m_fastReturning = false;
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
    if( t != -1 )
    {
        m_model->m_keyPressed = t;
        switch( m_model->m_keyPressed )
        {
            case -1:
            {
                break;
            }
            case 81:  // Q
            case 113: // q
            case 27:  // Esc
            {
                m_model->m_zMoving = false;
                m_model->m_quit = true;
                break;
            }
            // Cross-slide support is currently just for testing
            case 88:   // X (shift-x)
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
            // Cross-slide support is currently just for testing
            case 120:  // x
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
            // Cross-slide support is currently just for testing
            case 67:  // shift-C
            {
                if( m_model->m_xSpeed > 10.f )
                {
                    m_model->m_xSpeed -= 10.f;
                    m_model->m_xAxisMotor->setRpm( m_model->m_xSpeed );
                }
                else if( m_model->m_xSpeed > 2.f )
                {
                    m_model->m_xSpeed -= 2.f;
                }
                break;
            }
            // Cross-slide support is currently just for testing
            case 99:  // c
            {
                if( m_model->m_xSpeed < 10.f )
                {
                    m_model->m_xSpeed = 0.f;
                }
                if( m_model->m_xSpeed < 240.f )
                {
                    m_model->m_xSpeed += 10.f;
                    m_model->m_xAxisMotor->setRpm( m_model->m_xSpeed );
                }
                break;
            }
            // Cross-slide support is currently just for testing
            case 68:  // D - nudge in
            case 100: // d - nudge in
            {
                if ( m_model->m_xAxisMotor->isRunning() )
                {
                    m_model->m_xAxisMotor->stop();
                    m_model->m_xAxisMotor->wait();
                }
                m_model->m_xAxisMotor->goToStep( m_model->m_xAxisMotor->getCurrentStep() + 60 );
                break;
            }
            case 259: // Up arrow
            {
                if( m_model->m_threadCuttingOn ) break;
                if( m_model->m_zSpeed < 20 )
                {
                    m_model->m_zSpeed = 20;
                }
                else
                {
                    if( m_model->m_zSpeed < MAX_MOTOR_SPEED ) m_model->m_zSpeed += 20;
                }
                break;
            }
            case 258: // Down arrow
            {
                if( m_model->m_threadCuttingOn ) break;
                if( m_model->m_zSpeed > 20 )
                {
                    m_model->m_zSpeed -= 20;
                }
                else
                {
                    if ( m_model->m_zSpeed > 1 ) --m_model->m_zSpeed;
                }
                break;
            }
            case 77:  // M
            case 109: // m
            {
                m_model->m_memory.at( m_model->m_currentMemory ) =
                    m_model->m_zAxisMotor->getCurrentStep();
                break;
            }
            case 84:   // T
            case 116:  // t
            {
                // Toggle threading on/off
                m_model->m_threadCuttingOn = ! m_model->m_threadCuttingOn;
                break;
            }
            case 112:  // p
            {
                if ( ! m_model->m_threadCuttingOn ) break;
                // change threading pitch up
                ++m_model->m_threadPitchIndex;
                if( m_model->m_threadPitchIndex >= threadPitches.size() )
                {
                    m_model->m_threadPitchIndex = 0;
                }
                break;
            }
            case 80:  // P change thread pitch
            {
                if ( ! m_model->m_threadCuttingOn ) break;
                // change threading pitch down
                if( m_model->m_threadPitchIndex == 0 )
                {
                    m_model->m_threadPitchIndex = threadPitches.size() - 1;
                }
                else
                {
                    --m_model->m_threadPitchIndex;
                }
                break;
            }
            case 92:  // \ (backslash)
            {
                // causes the cut to start a fraction earlier
                // next time, this simulates feeding in at 29.5°
                ++m_model->m_threadCutAdvanceCount;
                m_model->m_rotaryEncoder->setAdvanceValueMicroseconds(
                    m_model->m_threadCutAdvanceCount *
                    ( ( 1'000'000.f / ( m_model->m_zSpeed / 60.f) ) * SIDEFEED )
                    );
                break;
            }
            case 124: // | (pipe, i.e. shift backslash
            {
                // decrement the cut advance count
                if( m_model->m_threadCutAdvanceCount > 0 )
                {
                    --m_model->m_threadCutAdvanceCount;
                }
                break;
            }
            case 10:  // ENTER
            case 82:  // R
            case 114: // r
            {
                if( m_model->m_memory.at( m_model->m_currentMemory ) == INF_RIGHT ) break;
                // We always start at the same rotational position: it's
                // required for thread cutting, but doesn't impact
                // anything if we're just turning down, so we always do it.
                m_model->m_zAxisMotor->stop();
                m_model->m_zAxisMotor->wait();
                m_model->m_status = "returning";
                m_model->m_rotaryEncoder->callbackAtZeroDegrees([&]()
                    {
                        m_model->m_zMoving = true;
                        m_model->m_targetStep = m_model->m_memory.at( m_model->m_currentMemory );
                    }
                    );
                break;
            }
            case 260: // Left arrow
            {
                // Same key will cancel if we're already moving
                if ( m_model->m_zMoving )
                {
                    m_model->m_zMoving = false;
                    break;
                }
                m_model->m_status = "moving left";
                m_model->m_zMoving = true;
                m_model->m_targetStep = INF_LEFT;
                break;
            }
            case 261: // Right arrow
            {
                // Same key will cancel if we're already moving
                if ( m_model->m_zMoving )
                {
                    m_model->m_zMoving = false;
                    break;
                }
                m_model->m_status = "moving right";
                m_model->m_zMoving = true;
                m_model->m_targetStep = INF_RIGHT;
                break;
            }
            case 44: // comma (<) - nudge left
            {
                if ( m_model->m_zMoving )
                {
                    m_model->m_zAxisMotor->stop();
                    m_model->m_zAxisMotor->wait();
                }
                m_model->m_zMoving = true;
                m_model->m_targetStep = m_model->m_zAxisMotor->getCurrentStep() + 25;
                break;
            }
            case 46: // full stop (>) - nudge right
            {
                if ( m_model->m_zMoving )
                {
                    m_model->m_zAxisMotor->stop();
                    m_model->m_zAxisMotor->wait();
                }
                m_model->m_zMoving = true;
                m_model->m_targetStep = m_model->m_zAxisMotor->getCurrentStep() - 25;
                break;
            }
            case 91: // [
            {
                if( m_model->m_currentMemory > 0 )
                {
                    --m_model->m_currentMemory;
                }
                break;
            }
            case 93: // ]
            {
                if( m_model->m_currentMemory < m_model->m_memory.size() - 1 )
                {
                    ++m_model->m_currentMemory;
                }
                break;
            }

            // Speed presets with Function keys

            case 265: // F1
            {
                if( ! m_model->m_threadCuttingOn ) m_model->m_zSpeed = 20;
                break;
            }
            case 266: // F2
            {
                if( ! m_model->m_threadCuttingOn ) m_model->m_zSpeed = 40;
                break;
            }
            case 267: // F3
            {
                if( ! m_model->m_threadCuttingOn ) m_model->m_zSpeed = 100;
                break;
            }
            case 268: // F4
            {
                if( ! m_model->m_threadCuttingOn ) m_model->m_zSpeed = 200;
                break;
            }
            case 269: // F5
            {
                if( ! m_model->m_threadCuttingOn ) m_model->m_zSpeed = 250;
                break;
            }
            case 270: // F6
            {
                if( ! m_model->m_threadCuttingOn ) m_model->m_zSpeed = 300;
                break;
            }
            case 271: // F7
            {
                if( ! m_model->m_threadCuttingOn ) m_model->m_zSpeed = 350;
                break;
            }
            case 272: // F8
            {
                if( ! m_model->m_threadCuttingOn ) m_model->m_zSpeed = 400;
                break;
            }
            case 273: // F9
            {
                if( ! m_model->m_threadCuttingOn ) m_model->m_zSpeed = 450;
                break;
            }
            case 274: // F10
            {
                if( ! m_model->m_threadCuttingOn ) m_model->m_zSpeed = 500;
                break;
            }
            case 275: // F11
            {
                if( ! m_model->m_threadCuttingOn ) m_model->m_zSpeed = 550;
                break;
            }
            case 276: // F12
            {
                if( ! m_model->m_threadCuttingOn ) m_model->m_zSpeed = MAX_MOTOR_SPEED;
                break;
            }

            case 102: // f
            case 70:  // F
            {
                if( m_model->m_memory.at( m_model->m_currentMemory ) == INF_RIGHT ) break;
                // Fast return to point
                m_model->m_oldZSpeed = m_model->m_zSpeed;
                m_model->m_zSpeed = MAX_MOTOR_SPEED;
                m_model->m_fastReturning = true;
                m_model->m_zAxisMotor->stop();
                m_model->m_zAxisMotor->wait();
                m_model->m_status = "fast returning";
                m_model->m_zMoving = true;
                m_model->m_targetStep = m_model->m_memory.at( m_model->m_currentMemory );
                break;
            }
            case 122: // z
            case 90:  // Z
            {
                // Zeroing can be confusing unless we
                // force a stop (i.e. the motor might start
                // as we are no longer at the target position)
                m_model->m_zAxisMotor->stop();
                m_model->m_targetStep = 0;
                m_model->m_zAxisMotor->zeroPosition();
                m_model->m_zMoving = false;
                m_model->m_xAxisMotor->stop();
                m_model->m_xAxisMotor->zeroPosition();
                break;
            }
            case 42: // asterisk, shutdown
            // Note the command used for shutdown should be made passwordless
            // in the /etc/sudoers files
            {
                #ifndef FAKE
                m_model->m_zAxisMotor->stop();
                m_model->m_zAxisMotor->wait();
                m_model->m_zMoving = false;
                m_model->m_xAxisMotor->stop();
                m_model->m_xAxisMotor->wait();
                m_model->m_quit = true;
                system( "sudo systemctl poweroff --no-block" );
                #endif
                break;
            }
            default: // e.g. space bar (32) to stop all motors
            {
                m_model->m_status = "stopped";
                m_model->m_zMoving = false;
                m_model->m_xAxisMotor->stop();
                break;
            }
        }
    }
}

} // end namespace
