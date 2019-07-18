#include "steppermotor.h"

#include <cassert>
#include <thread>
#include <mutex>

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
        long currentStep = 0;
        long targetStep = m_targetStep;
        long delay = 500; // usecs, TODO
        for(;;)
        {
            if( m_terminateThread)
            {
                break;
            }
            if( targetStep != m_targetStep )
            {
                targetStep = m_targetStep;
            }
            if( targetStep != currentStep )
            {
                // Do step, assuming just forward for now
                m_gpio.setStepPin( PinState::high );
                m_gpio.delayMicroSeconds( delay );
                m_gpio.setStepPin( PinState::low );
                m_gpio.delayMicroSeconds( delay );

                ++currentStep; // TOOD, direction
                if ( currentStep == targetStep )
                {
                    m_busy = false;
                }
            }
            // Update any status variables
            m_currentStep = currentStep;
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
    if ( m_busy )
    {
        // TODO... throw?
        // Or allow the target to just be
        // changed?
        // For now it's just ignored if the
        // stepper motor is busy
        return;
    }
    m_busy = true;
    m_targetStep = step;
}

void StepperMotor::stop()
{
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

} // end namespace
