#include "linearscale.h"

namespace mgo {

void LinearScale::staticCallback(int pin, int level, uint32_t tick, void* userData)
{
    LinearScale* self = reinterpret_cast<LinearScale*>(userData);
    self->callback(pin, level, tick);
}

void LinearScale::callback(int pin, int level, uint32_t /*tick*/)
{
    // Note we must NOT ignore consecutive edges on the same pin: that is what a
    // reversal (or the scale dithering across one edge) looks like, and the
    // phase decoding below handles it correctly. We only ignore a callback that
    // doesn't change the pin's level, as that can't represent any movement.
    if (pin == m_pinA) {
        if (level == m_levelA) {
            return;
        }
        m_levelA = level;
    } else if (pin == m_pinB) {
        if (level == m_levelB) {
            return;
        }
        m_levelB = level;
    } else {
        return;
    }
    int currentPhase = m_levelA ? (m_levelB ? 3 : 4) : (m_levelB ? 2 : 1);

    if (m_previousPhase == 0) {
        // First edge seen: we didn't know the phase before, so can't tell
        // which way we moved. Just record it.
        m_previousPhase = currentPhase;
        return;
    }
    // NB a jump of two phases (e.g. 1 -> 3) means an edge was missed, and the
    // direction can't be known; it's counted as one step either way below.
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
    return (m_stepCount - m_zeroPosition) / static_cast<float>(m_stepsPerMm);
}

void LinearScale::setZeroMm()
{
    m_zeroPosition = m_stepCount.load();
}

} // end namespace
