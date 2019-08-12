#pragma once
// This class is used to read and respond to a rotary
// encoder which measures the lathe's spindle rotation.

#include "igpio.h"

namespace mgo
{

class RotaryEncoder
{
    RotaryEncoder(
        IGpio&  gpio,
        int     pinA,
        int     pinB,
        int     pulsesPerRev, // of the RE, not spindle
        float   gearing
        )
        :
        m_pulsesPerRev( pulsesPerRev ),
        m_gearing( gearing)
    {
        // set up the callbacks
    }
    static void staticCallback(
        int      pin,
        int      level,
        uint32_t tick,
        void*    kuser
        );
    void callback(
        int      pin,
        int      level,
        uint32_t tick
        );
public:
private:
    int     m_pulsesPerRev;
    float   m_gearing;
};

} // end namespace