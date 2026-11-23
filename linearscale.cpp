#include "linearscale.h"

namespace mgo {

void LinearScale::staticCallback(int pin, int level, uint32_t tick, void* userData)
{
    LinearScale* self = reinterpret_cast<LinearScale*>(userData);
    self->callback(pin, level, tick);
}

void LinearScale::callback(int pin, int level, uint32_t tick)
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
    // (Each callback changes one pin's level, so this is always a move to the
    // next phase or the one before)
    int delta = 0;
    if (m_previousPhase == 4 && currentPhase == 1) {
        delta = -1;
    } else if (m_previousPhase == 1 && currentPhase == 4) {
        delta = 1;
    } else if (currentPhase > m_previousPhase) {
        delta = -1;
    } else if (currentPhase < m_previousPhase) {
        delta = 1;
    }
    m_previousPhase = currentPhase;

    // A real scale never changes both pins at once, but a spike of
    // interference on its ground or supply does: on the lathe, with the VFD
    // running and the carriage still, the reading drifted steadily one way,
    // and which way depended on where the carriage stood. pigpio reports two
    // pins which changed in the same sample one after the other (A first, as
    // it goes in pin order), with the same tick, which would be read as two
    // steps the same way; and again, the same way, when the spike ends. So
    // each spike would move the reading four counts. Instead, undo the
    // first, don't count the second, and count it as noise.
    if (m_lastPin >= 0 && pin != m_lastPin && tick == m_lastTick) {
        m_stepCount -= m_lastDelta;
        ++m_noiseEvents;
        m_lastPin = -1; // (Nothing left to undo)
        return;
    }
    m_stepCount += delta;
    m_lastPin = pin;
    m_lastTick = tick;
    m_lastDelta = delta;
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
