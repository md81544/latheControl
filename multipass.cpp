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
    if (setup.startFeature) {
        if (!positions.featureWorkEnd || !positions.featureFeed) {
            r.error = "The feature (the work's end " + cutLabel + " and its Ø) isn't set";
            return r;
        }
        const double workEnd = *positions.featureWorkEnd;
        // The cut runs into the work, so the start is off its end the other way
        double cutEnd = workEnd + setup.cutDistance;
        if (setup.cutEndMemory) {
            const auto cut = memoryAt(positions.cutMemory, *setup.cutEndMemory);
            if (!cut) {
                r.error = slotName(*setup.cutEndMemory) + " has no " + cutLabel + " position";
                return r;
            }
            cutEnd = *cut;
        }
        if (near(cutEnd, workEnd)) {
            r.error = "The " + cutLabel + " end is at the work's end";
            return r;
        }
        range.cutStart = workEnd - (cutEnd > workEnd ? 1.0 : -1.0) * positions.featureClearance;
        range.feedStart = *positions.featureFeed;
    } else if (setup.startMemory) {
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
        // (From the work's end, if starting from the feature)
        range.cutEnd
            = (setup.startFeature ? *positions.featureWorkEnd : range.cutStart) + setup.cutDistance;
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

MultiPassSetup
defaultMultiPassSetup(const MultiPassSetup& previous, const MultiPassPositions& positions)
{
    MultiPassSetup setup = previous;
    setup.retractBeforeMoveToStart = true;
    const std::size_t slots = std::min(positions.cutMemory.size(), positions.feedMemory.size());

    // At a memory slot? Then start from it
    setup.startMemory.reset();
    setup.startFeature = false;
    for (std::size_t slot = 0; slot < slots; ++slot) {
        const auto cut = positions.cutMemory[slot];
        const auto feed = positions.feedMemory[slot];
        if (cut && feed && near(*cut, positions.cutHere) && near(*feed, positions.feedHere)) {
            setup.startMemory = slot;
            break;
        }
    }
    // If not, from the current feature, if it's set
    if (!setup.startMemory && positions.featureWorkEnd && positions.featureFeed) {
        setup.startFeature = true;
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

namespace {

// How deep (from the feed axis's start) pass n (from 1) goes, from depth (as
// for the pass before, or nudged), before stopping at the end. See
// MultiPassPlan::constantArea
double passDepth(double feedPerPass, bool constantArea, double feedMinimum, int n, double depth)
{
    if (!constantArea) {
        return depth + feedPerPass;
    }
    return std::max(feedPerPass * std::sqrt(static_cast<double>(n)), depth + feedMinimum);
}

// The number of passes to go a distance in, after passesDone passes have gone
// depth in (at least one in all)
int countPasses(
    double distance,
    double feedPerPass,
    bool constantArea,
    double feedMinimum,
    int passesDone,
    double depth)
{
    if (!constantArea) {
        // (Less the tolerance, so that rounding doesn't add a pass of nothing)
        const double remaining = std::max(0.0, distance - depth - MultiPass::TOLERANCE);
        return std::max(1, passesDone + static_cast<int>(std::ceil(remaining / feedPerPass)));
    }
    int n = passesDone;
    // (The limit is only in case of a mistake: each pass goes in by at least
    // feedMinimum)
    while (depth < distance - MultiPass::TOLERANCE && n < 10'000) {
        depth = std::min(distance, passDepth(feedPerPass, true, feedMinimum, n + 1, depth));
        ++n;
    }
    return std::max(1, n);
}

} // end anonymous namespace

int multiPassCount(
    const MultiPassRange& range,
    double feedPerPass,
    bool constantArea,
    double feedMinimum)
{
    if (feedPerPass < MultiPass::TOLERANCE) {
        return 0;
    }
    // (As MultiPass::totalPasses(). The first pass is a step in, so a
    // distance of nothing still takes one)
    return countPasses(
        std::abs(range.feedEnd - range.feedStart), feedPerPass, constantArea, feedMinimum, 0, 0.0);
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
    if (plan.feedManual && plan.feedCorrection != 0.0) {
        return "Can't correct a taper with a manual feed";
    }
    if (plan.retractLastPasses < 0) {
        return "The number of passes to retract on can't be negative";
    }
    if (plan.constantArea && plan.feedMinimum < TOLERANCE) {
        return "A constant area needs a minimum feed per pass";
    }
    if (plan.cutShiftPerFeed != 0.0 && plan.feedCorrection != 0.0) {
        return "Can't shift the cut's start and correct a taper together";
    }
    if (plan.cut == MultiPassCut::Thread) {
        if (!plan.retract || plan.retractLastPasses != 0) {
            return "Threading needs a retract between passes";
        }
        if (plan.feedManual) {
            return "Can't thread with a manual feed";
        }
        if (plan.feedCorrection != 0.0) {
            return "Can't correct a taper while threading";
        }
    }
    if (m_machine.cutMoving() || (!plan.feedManual && m_machine.feedMoving())) {
        return "Stop the motors first";
    }
    if (!plan.feedManual && m_machine.isRetracted()) {
        return "Unretract first";
    }
    if (!cutAt(plan.cutStart) || (!plan.feedManual && !feedAt(plan.feedStart))) {
        return "Go to the start position first";
    }
    m_plan = plan;
    m_message.clear();
    m_pauseRequested = false;
    m_stopAfterPassReason.clear();
    m_passCutStart = plan.cutStart;
    // The first pass is one step in from the start (which is usually where
    // the tool touched the work, so a pass there would cut nothing)
    const double firstTarget = nextFeedTarget(plan.feedStart, 1);
    if (plan.feedManual && !near(firstTarget, plan.feedStart)) {
        // (Paused first, for the user to feed it in)
        m_pass = 0;
        m_feedTarget = plan.feedStart;
        m_state = State::Paused;
        return {};
    }
    m_pass = 1;
    m_feedTarget = firstTarget;
    startFeed();
    // (A threading pass may fail to start at once, if there was no feeding
    // in first)
    if (!inProgress()) {
        return m_message;
    }
    return {};
}

void MultiPass::continueAfterPause()
{
    if (m_state == State::Paused) {
        startNextPass();
    }
}

void MultiPass::togglePauseRequest()
{
    if (!inProgress() || m_state == State::Paused || m_plan.pause || m_plan.feedManual) {
        return;
    }
    m_pauseRequested = !m_pauseRequested;
}

bool MultiPass::pauseRequested() const
{
    return m_pauseRequested;
}

void MultiPass::stop(const std::string& reason)
{
    if (inProgress()) {
        abandon(reason);
    }
}

void MultiPass::stopAfterPass(const std::string& reason)
{
    switch (m_state) {
        case State::Cutting:
        case State::Retracting:
        case State::Returning:
            // (See afterReturn())
            m_stopAfterPassReason = reason;
            return;
        case State::Finishing:
            // (The last pass is done: just unretracting)
            return;
        default:
            stop(reason);
            return;
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
            if (!cutAt(m_plan.cutEnd)) {
                abandon("The cut stopped short of its end");
                return;
            }
            if (passRetracts()) {
                m_machine.retract(m_plan.retractTo);
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
            if (!cutAt(m_passCutStart)) {
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
            if (!feedAt(m_feedTarget)) {
                abandon("The feed stopped short");
                return;
            }
            startPass();
            return;
        case State::Positioning:
            if (m_machine.cutMoving()) {
                return;
            }
            m_machine.setCutSpeed(m_cutSpeed);
            if (!cutAt(m_passCutStart)) {
                abandon("The move to the cut's start stopped short");
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
    return countPasses(
        std::abs(m_plan.feedEnd - m_plan.feedStart),
        m_plan.feedPerPass,
        m_plan.constantArea,
        m_plan.feedMinimum,
        m_pass,
        fed());
}

double MultiPass::fed() const
{
    return std::abs(m_feedTarget - m_plan.feedStart);
}

double MultiPass::nextFeed() const
{
    const double from = feedBase();
    return std::abs(nextFeedTarget(from, m_pass + 1) - from);
}

const std::string& MultiPass::message() const
{
    return m_message;
}

const MultiPassPlan& MultiPass::plan() const
{
    return m_plan;
}

namespace {

// Whether an axis is at a position: within half a step of its motor (it can
// only stop on a whole step), or else within the tolerance
bool atPosition(double position, double target, double stepSize)
{
    return std::abs(position - target) < MultiPass::TOLERANCE + stepSize / 2.0;
}

} // end anonymous namespace

bool MultiPass::cutAt(double target) const
{
    return atPosition(m_machine.cutPosition(), target, m_machine.cutStepSize());
}

bool MultiPass::feedAt(double target) const
{
    return atPosition(m_machine.feedPosition(), target, m_machine.feedStepSize());
}

bool MultiPass::isLastPass() const
{
    return m_plan.feedPerPass >= TOLERANCE && near(m_feedTarget, m_plan.feedEnd);
}

bool MultiPass::passRetracts() const
{
    if (!m_plan.retract) {
        return false;
    }
    const int total = totalPasses();
    if (m_plan.retractLastPasses == 0 || total == 0) {
        return true;
    }
    // (The total is worked out afresh, as a nudge while paused may change it)
    return m_pass > total - m_plan.retractLastPasses;
}

double MultiPass::feedBase() const
{
    // (A motorised feed axis may have been nudged while paused)
    return m_plan.feedManual ? m_feedTarget : m_machine.feedPosition();
}

double MultiPass::passCutStart() const
{
    return m_passCutStart;
}

double MultiPass::nextFeedTarget(double from, int pass) const
{
    const double direction = m_plan.feedEnd < m_plan.feedStart ? -1.0 : 1.0;
    // (As deep as from is, which may have been nudged)
    const double depth = (from - m_plan.feedStart) * direction;
    const double target = m_plan.feedStart
        + direction
            * passDepth(m_plan.feedPerPass, m_plan.constantArea, m_plan.feedMinimum, pass, depth);
    // (Not past the end)
    if ((direction > 0.0 && target > m_plan.feedEnd)
        || (direction < 0.0 && target < m_plan.feedEnd)) {
        return m_plan.feedEnd;
    }
    return target;
}

void MultiPass::startPass()
{
    // (With a compound infeed, the deeper the feed axis, the further along)
    m_passCutStart = m_plan.cutStart + m_plan.cutShiftPerFeed * fed();
    if (cutAt(m_passCutStart)) {
        startCut();
        return;
    }
    // (Clear of the work, so at full speed, as for a return)
    m_cutSpeed = m_machine.cutSpeed();
    m_machine.setCutSpeed(m_machine.cutMaxSpeed());
    m_state = State::Positioning;
    m_machine.cutMoveTo(m_passCutStart, 0.0);
}

void MultiPass::startCut()
{
    m_state = State::Cutting;
    if (m_plan.cut == MultiPassCut::Thread) {
        if (std::string error = m_machine.cutThreadTo(m_plan.cutEnd); !error.empty()) {
            abandon(error);
        }
        return;
    }
    m_machine.cutMoveTo(m_plan.cutEnd, m_plan.feedCorrection);
}

void MultiPass::startReturn()
{
    // (Saved here, at the end of the cut, so a speed the user set during the
    // cut is kept for the next)
    m_cutSpeed = m_machine.cutSpeed();
    m_machine.setCutSpeed(m_machine.cutMaxSpeed());
    m_state = State::Returning;
    // (Following back, so the feed axis is where the cut started)
    m_machine.cutMoveTo(m_passCutStart, m_plan.feedCorrection);
}

void MultiPass::afterReturn()
{
    if (!m_stopAfterPassReason.empty()) {
        // (Before unretracting, so the tool stays clear of the work)
        abandon(m_stopAfterPassReason);
        return;
    }
    if (isLastPass()) {
        finish();
        return;
    }
    // Unretract now, at the start of the cut (which is clear of the work),
    // so that while paused, the feed axis shows where this pass cut, and can
    // be nudged
    if (m_machine.isRetracted()) {
        m_machine.unretract();
        m_state = State::Unretracting;
        return;
    }
    readyForNextPass();
}

void MultiPass::readyForNextPass()
{
    if (m_plan.pause || m_plan.feedManual || m_pauseRequested) {
        // (A request is for one pause only)
        m_pauseRequested = false;
        m_state = State::Paused;
        return;
    }
    startNextPass();
}

void MultiPass::startNextPass()
{
    // On by a step from where the feed axis is now (so if it was nudged while
    // paused, that's added to the step)
    m_feedTarget = nextFeedTarget(feedBase(), m_pass + 1);
    ++m_pass;
    startFeed();
}

void MultiPass::startFeed()
{
    if (m_plan.feedManual) {
        // (The user fed it on while paused)
        startPass();
        return;
    }
    if (feedAt(m_feedTarget)) {
        // e.g. repeating the same cut
        startPass();
        return;
    }
    m_state = State::Feeding;
    m_machine.feedMoveTo(m_feedTarget);
}

void MultiPass::finish()
{
    // (No pause after the last pass)
    m_pauseRequested = false;
    // Unretract, so the feed axis shows the final position
    if (m_machine.isRetracted()) {
        m_machine.unretract();
        m_state = State::Finishing;
        return;
    }
    m_state = State::Finished;
}

void MultiPass::abandon(const std::string& reason)
{
    m_state = State::Idle;
    m_pauseRequested = false;
    m_message = reason;
}

} // namespace mgo
