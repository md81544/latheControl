#include "gpio.h"
/*
namespace
{

// Although it hasn't been implemented as a singleton,
// logically we cannot have more than one instance of the
// Gpio object,so it's safe to use these static variables
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
bool firstCalls{ true };
float pulsesPerSpindleRevolution;

void callback( int pin, int level, uint32_t tick )
{
    using namespace mgo;

    if ( firstCalls )
    {
        // We ignnore the first few calls until we can
        // set the previous tick
        if( pin == painA && level == 1 )
        {
            lastTick = tick;
            firstCalls = false;
        }
        return;
    }

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

    // Note - we only count one pin's pulses, and measure from
    // rising edge to next rising edge
    if( pin == pinA && level == 1 )
    {
        ++tickCount;
        if( tickCount == static_cast<int>( pulsesPerSpindleRevolution ) )
        {
            tickCount = 0;
            averageTickDelta =
                tickDiffTotal / pulsesPerSpindleRevolution;
k            tickDiffTotal = 0;
        }
        tickDiffTotal += tick - lastTick; // don't need to worry about wrap
        lastTick = tick;
    }
}

} // end anonymous namespace
*/

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
    std::function<void(
        int      pin,
        int      level,
        uint32_t tick,
        void*    user
        )> callback,
    void* user
    )
{
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

/*
*/

void Gpio::delayMicroSeconds( long usecs )
{
    gpioDelay( usecs );
}

} // end namespace
