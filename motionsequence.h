#pragma once

#include <cstddef>
#include <functional>
#include <vector>

namespace mgo {

// A series of motor moves, each started once the one before has finished,
// without blocking: advance() is called on each update (the model's
// checkStatus()) and moves the sequence on when it can.
//
// For example, starting a taper cut: take up Z's backlash; once that's
// done, take up X's; once that's done, synchronise X to Z and start Z.
class MotionSequence {
public:
    struct Step {
        // Starts the step's move(s)
        std::function<void()> action;
        // True once the step has finished, so the next can start. Leave
        // empty for a step that's finished as soon as its action returns
        std::function<bool()> done;
    };

    // Starts a sequence, running the first step(s) at once. Any sequence
    // already running is cancelled first. onEnd (if given) is called when
    // the sequence ends, whether it finished or was cancelled.
    void start(std::vector<Step> steps, std::function<void()> onEnd = {});

    // Moves on to the next step(s) if the current one has finished. Returns
    // true if the sequence is still running.
    bool advance();

    // Abandons the sequence (without running any more steps), calling its
    // onEnd. Does nothing if there's no sequence running
    void cancel();

    bool isRunning() const;

private:
    void end();

    std::vector<Step> m_steps;
    std::function<void()> m_onEnd;
    // The step whose action has run and which we're waiting for, if any
    std::size_t m_current { 0 };
    bool m_running { false };
    // Changes whenever a sequence starts or ends, so advance() can tell if a
    // step's action (or an onEnd) started or cancelled a sequence itself
    unsigned m_generation { 0 };
};

} // namespace mgo
