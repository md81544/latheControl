#pragma once

#include <string>

namespace mgo {

// What a multi-pass needs from the machine. The model provides this; the unit
// tests use a fake. Positions are in mm, speeds in mm/min.
//
// The "cut" axis is the one that moves during a cut (axis 1, e.g. Z on the
// lathe); the "feed" axis is stepped on between cuts (axis 2, e.g. X).
class IMultiPassMachine {
public:
    virtual ~IMultiPassMachine() = default;

    virtual double cutPosition() const = 0;
    virtual bool cutMoving() const = 0;
    virtual void cutMoveTo(double position) = 0; // (at the cut axis's speed)
    virtual double cutSpeed() const = 0;
    virtual void setCutSpeed(double speed) = 0;
    virtual double cutMaxSpeed() const = 0;

    // (Not used if the feed is manual)
    virtual double feedPosition() const = 0;
    virtual bool feedMoving() const = 0;
    virtual void feedMoveTo(double position) = 0; // (at the feed axis's speed)

    // Retracting the feed axis between passes (moving it while retracted
    // counts as feedMoving()). retract() toggles: a second call unretracts
    virtual void retract() = 0;
    virtual bool isRetracted() const = 0;
};

// The parameters of a multi-pass
struct MultiPassPlan {
    // Each cut moves the cut axis from cutStart to cutEnd, then returns
    double cutStart { 0.0 };
    double cutEnd { 0.0 };
    // The feed axis's position for the first pass and the last. With a
    // manual feed, these just give the distance to feed in total
    double feedStart { 0.0 };
    double feedEnd { 0.0 };
    // How far the feed axis moves on between passes (towards feedEnd). The
    // last step may be smaller, to finish exactly at feedEnd. Zero repeats
    // the same cut until stopped
    double feedPerPass { 0.0 };
    // The feed axis isn't motorised (e.g. Y on the mill): pause before each
    // pass for the user to feed it on by hand
    bool feedManual { false };
    // Retract the feed axis before returning after each cut
    bool retract { false };
    // Wait for continueAfterPause() before each pass after the first
    bool pause { false };
};

// A series of cuts: cut, (retract,) return, (pause,) (unretract,) feed on,
// and repeat until a cut has been made at the feed axis's end position.
// Non-blocking: call update() regularly (e.g. on each tick), and it starts
// the next move when the last has finished.
//
// The cuts are made at the cut axis's speed as it was when each cut starts
// (so the user can change it while the multi-pass runs); returns are at the
// cut axis's maximum speed.
class MultiPass {
public:
    enum class State {
        Idle, // Not started (or stopped)
        Cutting,
        Retracting,
        Returning,
        Paused, // Waiting for continueAfterPause()
        Unretracting,
        Feeding,
        Finishing, // Unretracting after the last pass
        Finished
    };

    // How close (in mm) an axis must be to a position to count as there
    static constexpr double TOLERANCE = 0.0001;

    explicit MultiPass(IMultiPassMachine& machine);

    // Starts the first cut. The cut and feed axes must be at the plan's start
    // positions (unless the feed is manual). Returns an empty string if
    // started, or else why not
    std::string start(const MultiPassPlan& plan);
    // Moves on from Paused. Does nothing in any other state
    void continueAfterPause();
    // Abandons the multi-pass, giving the reason (shown by message()). The
    // caller stops the motors
    void stop(const std::string& reason);
    // Starts the next move if the last has finished
    void update();

    State state() const;
    // True from start() until finished or stopped
    bool inProgress() const;
    // The pass being made (from 1), or 0 if not started
    int pass() const;
    // The number of passes in all, or 0 if repeating until stopped
    int totalPasses() const;
    // How far the feed axis has been fed on since the first pass (for the
    // pass being made, or about to be)
    double fed() const;
    // While paused: how far the feed axis will move on for the next pass
    // (with a manual feed, how far the user should feed it)
    double nextFeed() const;
    // Why the multi-pass last stopped (empty if it finished, or hasn't)
    const std::string& message() const;
    const MultiPassPlan& plan() const;

private:
    bool isLastPass() const;
    double nextFeedTarget() const;
    void startCut();
    void startReturn();
    void afterReturn();
    void startNextPass();
    void startFeed();
    void finish();
    void abandon(const std::string& reason);

    IMultiPassMachine& m_machine;
    MultiPassPlan m_plan;
    State m_state { State::Idle };
    int m_pass { 0 };
    // The feed axis's position for the pass being made
    double m_feedTarget { 0.0 };
    // The cut axis's speed for cutting, while returning at full speed
    double m_cutSpeed { 0.0 };
    std::string m_message;
};

} // namespace mgo
