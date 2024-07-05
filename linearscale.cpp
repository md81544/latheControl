#include "linearscale.h"

namespace mgo {

void LinearScale::staticCallback(int pin, int level, uint32_t tick, void* userData)
{
    LinearScale* self = reinterpret_cast<LinearScale*>(userData);
    self->callback(pin, level, tick);
}

void LinearScale::callback(int pin, int /*level*/, uint32_t /*tick*/)
{
    if (pin == m_lastPin) {
        // debounce
        return;
    }
    m_lastPin = pin;
    // DO STUFF
}

float LinearScale::getPositionInMm()
{
    return m_stepCount / m_stepsPerMm;
}

} // end namespace
