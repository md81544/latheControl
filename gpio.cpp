#include "gpio.h"

namespace mgo
{

Gpio::Gpio(
    int   stepPin,
    int   reversePin
    )
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
    if( m_pinA != 0 ) gpioSetAlertFuncEx( m_pinA, nullptr, this);
    if( m_pinB != 0 ) gpioSetAlertFuncEx( m_pinB, nullptr, this);
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

void Gpio::setRotaryEncoderCallback(
    int pinA,
    int pinB,
    void (*callback)( int, int, uint32_t, void* ),
    void* user
    )
{
    m_pinA = pinA;
    m_pinB = pinB;
    // Set up callbacks for rotary encoder
    gpioSetMode( pinA, PI_INPUT );
    gpioSetMode( pinB, PI_INPUT );
    // pull up is needed as encoder common is grounded
    gpioSetPullUpDown( pinA, PI_PUD_UP );
    gpioSetPullUpDown( pinB, PI_PUD_UP );
    // monitor encoder level changes
    gpioSetAlertFuncEx( pinA, callback, user );
    gpioSetAlertFuncEx( pinB, callback, user );
}

void Gpio::delayMicroSeconds( long usecs )
{
    gpioDelay( usecs );
}

uint32_t Gpio::getTick()
{
    return gpioTick();
}

} // end namespace
