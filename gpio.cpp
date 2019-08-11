#include "gpio.h"

namespace
{

// Although it hasn't been implemented as a singleton,
// we cannot have more than one instance of the Gpio
// object, so it's safe to use these static variables
// and functions to make the callback processing easier

int pinA{ 0 };
int pinB{ 0 };
int levelA{ 0 };
int levelB{ 0 };
int lastPin;
mgo::RotationDirection direction;

void callback( int pin, int level, uint32_t /* tick */ )
{
    using namespace mgo;

    if ( pin == pinA )
    {
        levelA = level;
    }
    else
    {
        levelB = level;
    }

    if ( pin != lastPin) // debounce 
    {
        lastPin = pin;
        if ( pin == pinA && level == 1 )
        {
            if ( levelB ) direction = RotationDirection::normal;
        }
        else if ( pin == pinB && level == 1 )
        {
            if (levelA) direction = RotationDirection::reversed;
        }
    }
}

} // end anonymous namespace

namespace mgo
{

Gpio::Gpio(
    int   stepPin,
    int   reversePin,
    float rotaryEncoderGearing,
    int   rotaryEncoderPinA,
    int   rotaryEncoderPinB
    )
    :   m_stepPin( stepPin ),
        m_reversePin( reversePin ),
        m_rotaryEncoderGearing( rotaryEncoderGearing ),
        m_rotaryEncoderPinA( rotaryEncoderPinA ),
        m_rotaryEncoderPinB( rotaryEncoderPinB )
{
    if ( gpioInitialise() < 0 )
    {
        throw GpioException( "Could not initialise pigpio library" );
    }

    pinA = rotaryEncoderPinA;
    pinB = rotaryEncoderPinB;

    // Set up callbacks for rotary encoder
    gpioSetMode(m_rotaryEncoderPinA, PI_INPUT);
    gpioSetMode(m_rotaryEncoderPinB, PI_INPUT);
    // pull up is needed as encoder common is grounded
    gpioSetPullUpDown(m_rotaryEncoderPinA, PI_PUD_UP);
    gpioSetPullUpDown(m_rotaryEncoderPinB, PI_PUD_UP);
    // monitor encoder level changes
    gpioSetAlertFunc(m_rotaryEncoderPinA, callback );
    gpioSetAlertFunc(m_rotaryEncoderPinB, callback );
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

float Gpio::getRpm()
{
    // TODO
    return 1'000.f;
}

float Gpio::getPositionDegrees()
{
    // TODO
    return 0.f;
}

RotationDirection Gpio::getRotationDirection()
{
    // TODO
    return RotationDirection::normal;
}

void  Gpio::callbackAtPositionDegrees(
    float, // targetDegrees,
    std::function<void()> cb
    )
{
    // TODO
    cb();
}

void Gpio::delayMicroSeconds( long usecs )
{
    gpioDelay( usecs );
}

} // end namespace
