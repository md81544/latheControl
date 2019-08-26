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
    if ( pin == m_lastPin )
    {
        // debounce
        return;
    }
    m_lastPin = pin;

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

    // Check rotation
    if ( pin == m_pinA )
    {
        m_levelA = level;
    }
    else
    {
        m_levelB = level;
    }

    if ( pin == m_pinA && level == 1 )
    {
        if ( m_levelB ) m_direction = RotationDirection::normal;
    }
    else if ( pin == m_pinB && level == 1 )
    {
        if ( m_levelA ) m_direction = RotationDirection::reversed;
    }

    // Note - we only count one pin's pulses, and measure from
    // rising edge to next rising edge
    if( pin == m_pinA && level == 1 )
    {
        ++m_tickCount;
        if( m_tickCount == static_cast<uint32_t>( m_pulsesPerSpindleRev ) )
        {
            m_averageTickDelta =
                m_tickDiffTotal / m_pulsesPerSpindleRev;
            m_tickDiffTotal = 0;
            m_tickCount = 0;
            // We remember what the tick was at the last zero degrees position
            // (we arbitrarily call the start position zero) so we can extrappolate
            // out to the next one for accurate starting when waiting to cut a
            // thread. Owing to the latency on the callbaks, it's not sufficient
            // to simply wait for the next zero-degree tick. With the 1 ms latency,
            // this could result in an inaccuracy of up to 6° at 1,000 prm.
            m_lastZeroDegreesTick = tick;
        }
        m_tickDiffTotal += tick - m_lastTick; // don't need to worry about wrap
        m_lastTick = tick;
    }

}

float RotaryEncoder::getRpm()
{
    // TODO: the rpm value appears to stop updating above a certain speed:
    // investigate whether we are still getting called back, or does the rotary
    // encoder need to be driven more slowly (i.e. lower the gearing)?
    //
    // Ticks are in microseconds
    auto rpm = 60'000'000.f /
        ( m_averageTickDelta * m_pulsesPerSpindleRev );
    return rpm;
}

float RotaryEncoder::getPositionDegrees()
{
    // There will be latency in this as the pigpio thread which calls back to
    // the callback in the anonymous namespace above only does so approx once
    // per millisecond (it batches up the callbacks and calls us maybe thirty
    // times per batch). So this shouldn't be relied upon* - use the
    // callbackAtPositionDegrees() function which extrapolates out based on
    // previous data.

    // * But it's useful for unit testing :)

    // TODO this only works in one direction currently, we need to
    // take rotation direction into account

    return 360.f * ( m_tickCount / m_pulsesPerSpindleRev );
}

RotationDirection RotaryEncoder::getRotationDirection()
{
    return m_direction;
}

void  RotaryEncoder::callbackAtZeroDegrees(
    std::function<void()> cb
    )
{
    // We just need to start threading operations at a
    // repeatable rotational position each time, so we
    // arbitrarily choose zero.
    // Because there is a latency on the callback (the pigpio
    // library batches up the callbacks), we interpolate here
    // for better accuracy.
    if( m_warmingUp ) return; // spindle not running?
    while( m_lastZeroDegreesTick == 0 ); // spin if the last pos isn't set yet
    uint32_t timeForOneRevolution = m_averageTickDelta * m_pulsesPerSpindleRev;
    uint32_t targetTick =
        m_lastZeroDegreesTick + ( timeForOneRevolution - m_advanceValueMicroseconds );
    while( m_gpio.getTick() > targetTick )
    {
        targetTick += timeForOneRevolution;
    }
    // Now spin until we get to the right time
    while( m_gpio.getTick() < targetTick );
    cb();
}
} // end namespace
