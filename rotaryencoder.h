#pragma once
// This class is used to read and respond to a rotary
// encoder which measures the lathe's spindle rotation.

#include "igpio.h"

namespace mgo
{

enum class RotationDirection
{
    normal,
    reversed
};

class RotaryEncoder
{
public:
    RotaryEncoder(
        IGpio&  gpio,
        int     pinA,
        int     pinB,
        int     pulsesPerRev, // of the RE, not spindle
        float   gearing
        )
        :
        m_gpio( gpio ),
        m_pinA( pinA ),
        m_pinB( pinB ),
        m_pulsesPerRev( pulsesPerRev ),
        m_gearing( gearing)
    {
        m_pulsesPerSpindleRev = m_pulsesPerRev * m_gearing;

        m_gpio.setRotaryEncoderCallback(
            m_pinA,
            m_pinB,
            staticCallback,
            this
            );
    }

    static void staticCallback(
        int      pin,
        int      level,
        uint32_t tick,
        void*    userData
        );

    void callback(
        int      pin,
        int      level,
        uint32_t tick
        );

    float getRpm();
    float getPositionDegrees();
    RotationDirection getRotationDirection();
    void  storeCurrentPosition();
    void  callbackAtPosition(
        uint32_t position,
        std::function<void()> cb
        );

private:
    IGpio&   m_gpio;
    int      m_pinA;
    int      m_pinB;
    int      m_levelA;
    int      m_levelB;
    int      m_lastPin{ 0 };
    int      m_pulsesPerRev; // of RE
    float    m_pulsesPerSpindleRev;
    float    m_gearing;
    bool     m_warmingUp{ true };
    uint32_t m_lastTick;
    uint32_t m_tickCount{ 0 };
    uint32_t m_tickDiffTotal{ 0 };
    float    m_averageTickDelta{ 0.f };
    RotationDirection m_direction;
};

} // end namespace
