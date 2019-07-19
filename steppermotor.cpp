#include "steppermotor.h"

#include <cassert>

namespace mgo
{

StepperMotor::StepperMotor(
    IGpio& gpio,
    long stepsPerRevolution
    )
    :   m_gpio( gpio ),
        m_stepsPerRevolution( stepsPerRevolution )
{
    // Start the thread
    std::thread t( [&]()
    {
        long delay = 500; // usecs, TODO
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
                    // Do step, assuming just forward for now
                    m_gpio.setStepPin( PinState::high );
                    m_gpio.delayMicroSeconds( delay );
                    m_gpio.setStepPin( PinState::low );

                    ++m_currentStep; // TOOD, direction
                    if ( m_currentStep == m_targetStep )
                    {
                        m_busy = false;
                    }
                }
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
    m_busy = true;
    m_targetStep = step;
}

void StepperMotor::stop()
{
    std::lock_guard<std::mutex> mtx( m_mtx );
    m_stop = true;
}

void StepperMotor::setSpeedPercent( int /* speed */ )
{
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

} // end namespace
