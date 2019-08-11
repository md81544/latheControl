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

uint32_t tickCount{ 0 };
uint32_t lastTick;
uint32_t tickDiffTotal{ 0 };
mgo::RotationDirection direction;
float averageTickDelta{ 0.f };

void callback( int pin, int level, uint32_t tick )
{
    using namespace mgo;

    // Check rotation periodically
    if( tickCount == 0 )
    {
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

    ++tickCount;
    if( tickCount == 1000 ) // arbitrary bucket size
    {
        tickCount = 0;
        averageTickDelta = tickDiffTotal / 1000.f;
        tickDiffTotal = 0;
    }
    tickDiffTotal += tick - lastTick; // don't need to worry about wrap
}

} // end anonymous namespace

namespace mgo
{

Gpio::Gpio(
    int   stepPin,
    int   reversePin,
    int   rotaryEncoderPulsesPerRevolution,
    float rotaryEncoderGearing,
    int   rotaryEncoderPinA,
    int   rotaryEncoderPinB
    )
    :   m_stepPin( stepPin ),
        m_reversePin( reversePin ),
        m_rotaryEncoderPulsesPerRevolution( rotaryEncoderPulsesPerRevolution ),
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
    // ticks are in microseconds
    return  60'000'000 /
    ( averageTickDelta * m_rotaryEncoderPulsesPerRevolution * m_rotaryEncoderGearing );
}

float Gpio::getPositionDegrees()
{
    // There will be latency in this as the pigpio thread which calls back to
    // the callback in the anonymous namespace above only does so approx once
    // per millisecond (it batches up the callbacks and calls us maybe thirty
    // times per batch). So this shouldn't be relied upon - use the
    // callbackAtPositionDegrees() function which extrapolates out based on
    // previous data.

    // TODO - probably remove this function?

    return 0.f;
}

RotationDirection Gpio::getRotationDirection()
{
    return direction;
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
