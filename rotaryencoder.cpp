#include "rotaryencoder.h"

namespace mgo
{

void RotaryEncoder::staticCallback(
    int      pin,
    int      level,
    uint32_t tick,
    void*    kuser
    )
{
    RotaryEncoder* self = reinterpret_cast<RotaryEncoder*>(user);
    self->callback( pin, level, tick );
}

void RotaryEncoder::callback(
    int      pin,
    int      level,
    uint32_t tick
    )
{
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

} // end namespace