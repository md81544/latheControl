#include "steppermotor.h"

#include <cassert>
#include <thread>
#include <mutex>

namespace mgo
{

StepperMotor::StepperMotor( const IGpio& gpio )
    : m_gpio( gpio )
{
    // Start the thread
    std::thread t( [&]()
    {
        // Do stuff
    } // thread end
    );
    // Store the thread's handle in m_thread
    m_thread.swap(t);
}


StepperMotor::~StepperMotor()
{
}


void StepperMotor::goToStep( long /* step */ )
{
}

void StepperMotor::stop()
{
}

void StepperMotor::setSpeedPercent( int /* speed */ )
{
}

void StepperMotor::wait()
{
}

} // end namespace
