#include "linearscale.h"

namespace mgo {

void LinearScale::staticCallback(int pin, int level, uint32_t tick, void* userData)
{
    LinearScale* self = reinterpret_cast<LinearScale*>(userData);
    self->callback(pin, level, tick);
}

void LinearScale::callback(int pin, int level, uint32_t /*tick*/)
{
    if (pin == m_lastPin) {
        // debounce
        return;
    }
    m_lastPin = pin;
    if (pin == m_pinA) {
        m_levelA = level;
    }
    if (pin == m_pinB) {
        m_levelB = level;
    }
    int currentPhase = m_levelA ? (m_levelB ? 3 : 4) : (m_levelB ? 2 : 1);

    if (m_previousPhase == 4 && currentPhase == 1) {
        --m_stepCount;
    } else if (m_previousPhase == 1 && currentPhase == 4) {
        ++m_stepCount;
    } else if (currentPhase > m_previousPhase) {
        --m_stepCount;
    } else if (currentPhase < m_previousPhase) {
        ++m_stepCount;
    }
    m_previousPhase = currentPhase;
}

float LinearScale::getPositionInMm()
{
    return m_stepCount / m_stepsPerMm;
}

} // end namespace
