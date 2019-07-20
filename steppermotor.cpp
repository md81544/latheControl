#include "steppermotor.h"

#include <cassert>
#include <cmath>

namespace mgo
{

StepperMotor::StepperMotor(
    IGpio& gpio,
    long stepsPerRevolution
    )
    :   m_gpio( gpio ),
        m_stepsPerRevolution( stepsPerRevolution )
{
    // Ensure we start off with the right direction
    m_gpio.setReversePin(
        m_direction == Direction::forward ?
            PinState::low : PinState::high
        );

    // Start the thread
    std::thread t( [&]()
    {
        Direction oldDirection = Direction::forward;
        int delay;
        for(;;)
        {
            {   // scope for lock_guard
                // When in this scope we can assume all member
                // variables can be written and read from freely
                // without them being changed externally. Other
                // member functions must lock the mutex m_mtx
                // before changing any member variables.
                std::lock_guard<std::mutex> mtx( m_mtx );
                if( m_terminateThread)
                {
                    break;
                }
                if ( m_stop )
                {
                    m_targetStep = long( m_currentStep );
                    m_stop = false;
                    m_busy = false;
                }
                if( m_targetStep != m_currentStep )
                {
                    if ( oldDirection != m_direction )
                    {
                        m_gpio.setReversePin(
                            m_direction == Direction::forward ?
                                PinState::low : PinState::high
                            );
                        oldDirection = m_direction;
                    }
                    // Do step, assuming just forward for now
                    m_gpio.setStepPin( PinState::high );
                    m_gpio.delayMicroSeconds( m_delay );
                    m_gpio.setStepPin( PinState::low );
                    if ( m_targetStep < m_currentStep )
                    {
                        --m_currentStep;
                    }
                    else
                    {
                        ++m_currentStep;
                    }
                    if ( m_currentStep == m_targetStep )
                    {
                        m_busy = false;
                    }
                }
                delay = m_delay; // remember delay value while in mtx scope
            } // scope for lock_guard
            // We always perform the second delay regardless of
            // whether we're stepping, to give the main thread a
            // chance to grab the mutex
            m_gpio.delayMicroSeconds( delay );
        }
    } // thread end
    );
    // Store the thread's handle in m_thread
    m_thread.swap(t);
}


StepperMotor::~StepperMotor()
{
    m_terminateThread = true;
    m_thread.join();
}


void StepperMotor::goToStep( long step )
{
    std::lock_guard<std::mutex> mtx( m_mtx );
    if ( m_busy )
    {
        // TODO... throw?
        // Or allow the target to just be
        // changed?
        // For now it's just ignored if the
        // stepper motor is busy
        return;
    }
    if ( m_currentStep == step )
    {
        return;
    }
    m_direction = Direction::forward;
    if ( step < m_currentStep )
    {
        m_direction = Direction::reverse;
    }
    m_busy = true;
    m_targetStep = step;
}

void StepperMotor::stop()
{
    std::lock_guard<std::mutex> mtx( m_mtx );
    m_stop = true;
}

void StepperMotor::setRpm( double rpm )
{
    // m_delay (in usecs) is used twice per thread loop
    std::lock_guard<std::mutex> mtx( m_mtx );
    m_delay = std::round( 500'000.0 /
            ( static_cast<double>( m_stepsPerRevolution ) * ( rpm / 60.0 ) )
        );
}

int StepperMotor::getDelay()
{
    std::lock_guard<std::mutex> mtx( m_mtx );
    return m_delay;
}

long StepperMotor::getCurrentStep()
{
    return m_currentStep;
}

void StepperMotor::wait()
{
    while ( m_busy )
    {
        m_gpio.delayMicroSeconds( 10'000 );
    }
}

bool StepperMotor::isRunning()
{
    return m_busy;
}

Direction StepperMotor::getDirection()
{
    return m_direction;
}

} // end namespace
