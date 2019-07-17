#include "gpio.h"

namespace mgo
{

Gpio::Gpio( int stepPin, int reversePin )
    :   m_stepPin( stepPin ),
        m_reversePin( reversePin )
{
    if ( gpioInitialise() < 0 )
    {
        throw GpioException( "Could not initialise pigpio library" );
    }
}

Gpio::~Gpio()
{
    gpioTerminate();
}

void Gpio::setStepPin( PinState state )
{
    gpioWrite( m_stepPin, state == PinState::high ? 1 : 0 );
}

void Gpio::setReversePin( PinState state )
{
    gpioWrite( m_reversePin, state == PinState::high ? 1 : 0 );
}

void Gpio::delayMicroSeconds( long usecs )
{
    gpioDelay( usecs );
}

} // end namespace
