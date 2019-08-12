#include "rotaryencoder.h"

namespace mgo
{

void RotaryEncoder::staticCallback(
    int      pin,
    int      level,
    uint32_t tick,
    void*    user
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
        m_lastTick = tick;
    }
}

} // end namespace
