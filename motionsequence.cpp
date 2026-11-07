#include "motionsequence.h"

#include <utility>

namespace mgo {

void MotionSequence::start(std::vector<Step> steps, std::function<void()> onEnd)
{
    cancel();
    m_steps = std::move(steps);
    m_onEnd = std::move(onEnd);
    m_current = 0;
    m_running = true;
    ++m_generation;
    if (m_steps.empty()) {
        end();
        return;
    }
    const unsigned generation = m_generation;
    m_steps.front().action();
    if (generation == m_generation) {
        advance();
    }
}

bool MotionSequence::advance()
{
    while (m_running) {
        const Step& step = m_steps.at(m_current);
        if (step.done && !step.done()) {
            return true;
        }
        if (m_current + 1 == m_steps.size()) {
            end();
            return false;
        }
        ++m_current;
        const unsigned generation = m_generation;
        m_steps.at(m_current).action();
        if (generation != m_generation) {
            // The action cancelled this sequence, or started another
            return m_running;
        }
    }
    return false;
}

void MotionSequence::cancel()
{
    if (m_running) {
        end();
    }
}

bool MotionSequence::isRunning() const
{
    return m_running;
}

void MotionSequence::end()
{
    m_running = false;
    ++m_generation;
    // (Moved out first, as onEnd may start another sequence)
    auto onEnd = std::exchange(m_onEnd, {});
    m_steps.clear();
    if (onEnd) {
        onEnd();
    }
}

} // namespace mgo
