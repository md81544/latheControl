#include "rotaryencoder.h"
#include "log.h"

namespace mgo
{

void RotaryEncoder::staticCallback(
    int      pin,
    int      level,
    uint32_t tick,
    void*    userData
    )
{
    RotaryEncoder* self = reinterpret_cast<RotaryEncoder*>(userData);
    self->callback( pin, level, tick );
}

void RotaryEncoder::callback(
    int      pin,
    int      level,
    uint32_t tick
    )
{
    if ( m_warmingUp )
    {
        // We ignnore the first few calls until we can
        // set the previous tick
        if( pin == m_pinA && level == 1 )
        {
            m_lastTick = tick;
            m_warmingUp = false;
        }
        return;
    }

    // Check rotation periodically
    if( m_tickCount == 0 )
    {
        if ( pin == m_pinA )
        {
            m_levelA = level;
        }
        else
        {
            m_levelB = level;
        }

        if ( pin != m_lastPin) // debounce
        {
            m_lastPin = pin;
            if ( pin == m_pinA && level == 1 )
            {
                if ( m_levelB ) m_direction = RotationDirection::normal;
            }
            else if ( pin == m_pinB && level == 1 )
            {
                if ( m_levelA ) m_direction = RotationDirection::reversed;
            }
        }
    }

    // Note - we only count one pin's pulses, and measure from
    // rising edge to next rising edge
    if( pin == m_pinA && level == 1 )
    {
        ++m_tickCount;
        if( m_tickCount == static_cast<uint32_t>( m_pulsesPerSpindleRev ) )
        {
            m_tickCount = 0;
            m_averageTickDelta =
                m_tickDiffTotal / m_pulsesPerSpindleRev;
            m_tickDiffTotal = 0;
        }
        m_tickDiffTotal += tick - m_lastTick; // don't need to worry about wrap
    }

    m_lastTick = tick;
}

float RotaryEncoder::getRpm()
{
    // TODO: the rpm value appears to stop updating above a certain speed:
    // investigate whether we are still getting called back, or does the rotary
    // encoder need to be driven more slowly (i.e. lower the gearing)?
    //
    // Ticks are in microseconds
    MGOLOG( "m_averageTickDelta = " << m_averageTickDelta );
    MGOLOG( "m_pulsesPerRev = " << m_pulsesPerRev );
    MGOLOG( "m_gearing = " << m_gearing );
    return  60'000'000 /
        ( m_averageTickDelta * m_pulsesPerRev * m_gearing );
}

float RotaryEncoder::getPositionDegrees()
{
    // There will be latency in this as the pigpio thread which calls back to
    // the callback in the anonymous namespace above only does so approx once
    // per millisecond (it batches up the callbacks and calls us maybe thirty
    // times per batch). So this shouldn't be relied upon - use the
    // callbackAtPositionDegrees() function which extrapolates out based on
    // previous data.

    // TODO - probably remove this function?

    return 360.f * ( m_tickCount / m_pulsesPerSpindleRev );
}

RotationDirection RotaryEncoder::getRotationDirection()
{
    return m_direction;
}

void RotaryEncoder::storeCurrentPosition()
{

}

void  RotaryEncoder::callbackAtPosition(
    uint32_t /* position */,
    std::function<void()> cb
    )
{
    // Because there is a latency on the callback (the pigpio
    // library batches up the callbacks), we interpolate here
    // for better accuracy. We set the target degrees, wait for
    // the tick for the last time we hit that position, then
    // calculate how long we need to wait, then block for that
    // time, then callback.
    cb();
}
} // end namespace
