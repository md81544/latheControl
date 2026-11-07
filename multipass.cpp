#include "multipass.h"

#include <algorithm>
#include <cmath>

namespace mgo {

namespace {

bool near(double a, double b)
{
    return std::abs(a - b) < MultiPass::TOLERANCE;
}

std::optional<double> memoryAt(const std::vector<std::optional<double>>& memory, std::size_t slot)
{
    return slot < memory.size() ? memory[slot] : std::nullopt;
}

std::string slotName(std::size_t slot)
{
    return "Mem " + std::to_string(slot + 1);
}

} // end anonymous namespace

bool atMultiPassPosition(double position, double target)
{
    return near(position, target);
}

MultiPassResolved resolveMultiPass(
    const MultiPassSetup& setup,
    const MultiPassPositions& positions,
    const std::string& cutLabel,
    const std::string& feedLabel)
{
    MultiPassResolved r;
    MultiPassRange& range = r.range;
    if (setup.startMemory) {
        const auto cut = memoryAt(positions.cutMemory, *setup.startMemory);
        const auto feed = memoryAt(positions.feedMemory, *setup.startMemory);
        if (!cut || !feed) {
            r.error = slotName(*setup.startMemory) + " isn't set on both " + cutLabel + " and "
                + feedLabel;
            return r;
        }
        range.cutStart = *cut;
        range.feedStart = *feed;
    } else {
        range.cutStart = positions.cutHere;
        range.feedStart = positions.feedHere;
    }

    if (setup.cutEndMemory) {
        const auto cut = memoryAt(positions.cutMemory, *setup.cutEndMemory);
        if (!cut) {
            r.error = slotName(*setup.cutEndMemory) + " has no " + cutLabel + " position";
            return r;
        }
        range.cutEnd = *cut;
    } else {
        range.cutEnd = range.cutStart + setup.cutDistance;
    }

    if (setup.feedEndMemory) {
        const auto feed = memoryAt(positions.feedMemory, *setup.feedEndMemory);
        if (!feed) {
            r.error = slotName(*setup.feedEndMemory) + " has no " + feedLabel + " position";
            return r;
        }
        range.feedEnd = *feed;
    } else {
        if (setup.feedInfeed < 0.0) {
            r.error = "The " + feedLabel + " infeed can't be negative";
            return r;
        }
        range.feedEnd = range.feedStart + positions.feedIntoWork * setup.feedInfeed;
    }

    if (near(range.cutStart, range.cutEnd)) {
        r.error = "The " + cutLabel + " start and end are the same";
        return r;
    }
    if (setup.feedPerPass < 0.0) {
        r.error = "The step-over can't be negative";
        return r;
    }
    return r;
}

MultiPassSetup defaultMultiPassSetup(
    const MultiPassSetup& previous, const MultiPassPositions& positions)
{
    MultiPassSetup setup = previous;
    setup.retractBeforeMoveToStart = true;
    const std::size_t slots = std::min(positions.cutMemory.size(), positions.feedMemory.size());

    // At a memory slot? Then start from it
    setup.startMemory.reset();
    for (std::size_t slot = 0; slot < slots; ++slot) {
        const auto cut = positions.cutMemory[slot];
        const auto feed = positions.feedMemory[slot];
        if (cut && feed && near(*cut, positions.cutHere) && near(*feed, positions.feedHere)) {
            setup.startMemory = slot;
            break;
        }
    }

    const auto endFor = [&](const std::vector<std::optional<double>>& memory,
                            std::optional<std::size_t> previousEnd,
                            bool notTheStart) -> std::optional<std::size_t> {
        const auto usable = [&](std::size_t slot) {
            return memoryAt(memory, slot).has_value()
                && !(notTheStart && setup.startMemory == slot);
        };
        if (previousEnd && usable(*previousEnd)) {
            return previousEnd;
        }
        if (setup.startMemory) {
            for (std::size_t slot = 0; slot < memory.size(); ++slot) {
                if (slot != *setup.startMemory && usable(slot)) {
                    return slot;
                }
            }
        }
        return std::nullopt;
    };
    // (The cut can't end where it starts, but the feed can: the same depth)
    setup.cutEndMemory = endFor(positions.cutMemory, previous.cutEndMemory, true);
    setup.feedEndMemory = endFor(positions.feedMemory, previous.feedEndMemory, false);
    return setup;
}

int multiPassCount(const MultiPassRange& range, double feedPerPass)
{
    if (feedPerPass < MultiPass::TOLERANCE) {
        return 0;
    }
    const double distance = std::abs(range.feedEnd - range.feedStart);
    // (As MultiPass::totalPasses(), at the first pass)
    return 1 + static_cast<int>(std::ceil(std::max(0.0, distance - MultiPass::TOLERANCE) / feedPerPass));
}

MultiPass::MultiPass(IMultiPassMachine& machine)
    : m_machine(machine)
{
}

std::string MultiPass::start(const MultiPassPlan& plan)
{
    if (inProgress()) {
        return "A multi-pass is already running";
    }
    if (near(plan.cutStart, plan.cutEnd)) {
        return "The cut's start and end are the same";
    }
    if (plan.feedPerPass < 0.0) {
        return "The feed per pass can't be negative";
    }
    if (plan.feedManual && plan.retract) {
        return "Can't retract with a manual feed";
    }
    if (m_machine.cutMoving() || (!plan.feedManual && m_machine.feedMoving())) {
        return "Stop the motors first";
    }
    if (!plan.feedManual && m_machine.isRetracted()) {
        return "Unretract first";
    }
    if (!near(m_machine.cutPosition(), plan.cutStart)
        || (!plan.feedManual && !near(m_machine.feedPosition(), plan.feedStart))) {
        return "Go to the start position first";
    }
    m_plan = plan;
    m_message.clear();
    m_pass = 1;
    m_feedTarget = plan.feedStart;
    startCut();
    return {};
}

void MultiPass::continueAfterPause()
{
    if (m_state == State::Paused) {
        startNextPass();
    }
}

void MultiPass::stop(const std::string& reason)
{
    if (inProgress()) {
        abandon(reason);
    }
}

void MultiPass::update()
{
    const bool feedMoving = !m_plan.feedManual && m_machine.feedMoving();
    switch (m_state) {
        case State::Idle:
        case State::Paused:
        case State::Finished:
            return;
        case State::Cutting:
            if (m_machine.cutMoving()) {
                return;
            }
            if (!near(m_machine.cutPosition(), m_plan.cutEnd)) {
                abandon("The cut stopped short of its end");
                return;
            }
            if (m_plan.retract) {
                m_machine.retract();
                if (!m_machine.isRetracted()) {
                    abandon("Couldn't retract");
                    return;
                }
                m_state = State::Retracting;
                return;
            }
            startReturn();
            return;
        case State::Retracting:
            if (!feedMoving) {
                startReturn();
            }
            return;
        case State::Returning:
            if (m_machine.cutMoving()) {
                return;
            }
            m_machine.setCutSpeed(m_cutSpeed);
            if (!near(m_machine.cutPosition(), m_plan.cutStart)) {
                abandon("The return stopped short of the start");
                return;
            }
            afterReturn();
            return;
        case State::Unretracting:
            if (!feedMoving) {
                readyForNextPass();
            }
            return;
        case State::Feeding:
            if (feedMoving) {
                return;
            }
            if (!near(m_machine.feedPosition(), m_feedTarget)) {
                abandon("The feed stopped short");
                return;
            }
            startCut();
            return;
        case State::Finishing:
            if (!feedMoving) {
                m_state = State::Finished;
            }
            return;
    }
}

MultiPass::State MultiPass::state() const
{
    return m_state;
}

bool MultiPass::inProgress() const
{
    return m_state != State::Idle && m_state != State::Finished;
}

int MultiPass::pass() const
{
    return m_pass;
}

int MultiPass::totalPasses() const
{
    if (m_plan.feedPerPass < TOLERANCE) {
        return 0;
    }
    // The passes so far, and those still to go from this pass's position (so
    // a nudge while paused can change the number)
    const double remaining = std::abs(m_plan.feedEnd - m_feedTarget);
    // (Less the tolerance, so that rounding doesn't add a pass of nothing)
    return std::max(1, m_pass)
        + static_cast<int>(std::ceil(std::max(0.0, remaining - TOLERANCE) / m_plan.feedPerPass));
}

double MultiPass::fed() const
{
    return std::abs(m_feedTarget - m_plan.feedStart);
}

double MultiPass::nextFeed() const
{
    const double from = feedBase();
    return std::abs(nextFeedTarget(from) - from);
}

const std::string& MultiPass::message() const
{
    return m_message;
}

const MultiPassPlan& MultiPass::plan() const
{
    return m_plan;
}

bool MultiPass::isLastPass() const
{
    return m_plan.feedPerPass >= TOLERANCE && near(m_feedTarget, m_plan.feedEnd);
}

double MultiPass::feedBase() const
{
    // (A motorised feed axis may have been nudged while paused)
    return m_plan.feedManual ? m_feedTarget : m_machine.feedPosition();
}

double MultiPass::nextFeedTarget(double from) const
{
    const double direction = m_plan.feedEnd < m_plan.feedStart ? -1.0 : 1.0;
    const double target = from + direction * m_plan.feedPerPass;
    // (Not past the end)
    if ((direction > 0.0 && target > m_plan.feedEnd) || (direction < 0.0 && target < m_plan.feedEnd)) {
        return m_plan.feedEnd;
    }
    return target;
}

void MultiPass::startCut()
{
    m_state = State::Cutting;
    m_machine.cutMoveTo(m_plan.cutEnd);
}

void MultiPass::startReturn()
{
    // (Saved here, at the end of the cut, so a speed the user set during the
    // cut is kept for the next)
    m_cutSpeed = m_machine.cutSpeed();
    m_machine.setCutSpeed(m_machine.cutMaxSpeed());
    m_state = State::Returning;
    m_machine.cutMoveTo(m_plan.cutStart);
}

void MultiPass::afterReturn()
{
    if (isLastPass()) {
        finish();
        return;
    }
    // Unretract now, at the start of the cut (which is clear of the work),
    // so that while paused, the feed axis shows where this pass cut, and can
    // be nudged
    if (m_machine.isRetracted()) {
        m_machine.retract(); // (i.e. unretract)
        m_state = State::Unretracting;
        return;
    }
    readyForNextPass();
}

void MultiPass::readyForNextPass()
{
    if (m_plan.pause || m_plan.feedManual) {
        m_state = State::Paused;
        return;
    }
    startNextPass();
}

void MultiPass::startNextPass()
{
    // On by a step from where the feed axis is now (so if it was nudged while
    // paused, that's added to the step)
    m_feedTarget = nextFeedTarget(feedBase());
    ++m_pass;
    startFeed();
}

void MultiPass::startFeed()
{
    if (m_plan.feedManual) {
        // (The user fed it on while paused)
        startCut();
        return;
    }
    if (near(m_machine.feedPosition(), m_feedTarget)) {
        // e.g. repeating the same cut
        startCut();
        return;
    }
    m_state = State::Feeding;
    m_machine.feedMoveTo(m_feedTarget);
}

void MultiPass::finish()
{
    // Unretract, so the feed axis shows the final position
    if (m_machine.isRetracted()) {
        m_machine.retract();
        m_state = State::Finishing;
        return;
    }
    m_state = State::Finished;
}

void MultiPass::abandon(const std::string& reason)
{
    m_state = State::Idle;
    m_message = reason;
}

} // namespace mgo
