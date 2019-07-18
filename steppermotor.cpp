#include "steppermotor.h"

#include <cassert>
#include <thread>
#include <mutex>

namespace mgo
{

StepperMotor::StepperMotor( IGpio& gpio )
    : m_gpio( gpio )
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
    m_busy = true;
    m_targetStep = step;
}

void StepperMotor::stop()
{
}

void StepperMotor::setSpeedPercent( int /* speed */ )
{
}

void StepperMotor::wait()
{
    while ( m_busy )
    {
        m_gpio.delayMicroSeconds( 10'000 );
    }
}

} // end namespace
