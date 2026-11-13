#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

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
    // (Including the feed axis catching up, if it's following)
    virtual bool cutMoving() const = 0;
    // At the cut axis's speed. If feedFollows isn't zero, the feed axis
    // follows the cut axis, moving by feedFollows times as far (if it's
    // retracted, where it unretracts to moves by that much instead)
    virtual void cutMoveTo(double position, double feedFollows) = 0;
    virtual double cutSpeed() const = 0;
    virtual void setCutSpeed(double speed) = 0;
    virtual double cutMaxSpeed() const = 0;
    // A threading pass (for MultiPassCut::Thread): the cut axis follows the
    // spindle to position, at the thread's pitch (or lead) per revolution,
    // starting at the same angle of the spindle's rotation as every other
    // pass (so each pass cuts the same thread), plus revolutionOffset of a
    // revolution (from 0 to 1: see MultiPassPlan::starts). Returns an empty
    // string if it started, or else why not (e.g. the spindle isn't
    // turning). If it's stopped part way (e.g. the cut axis couldn't keep
    // up), it ends short of position
    virtual std::string cutThreadTo(double position, double revolutionOffset) = 0;
    // The size of a step of each axis's motor (in mm; 0 if it can move
    // anywhere). An axis can only stop on a whole step, so it counts as at a
    // position if it's within half a step: e.g. a thread's depth is seldom a
    // whole number of steps
    virtual double cutStepSize() const
    {
        return 0.0;
    }
    virtual double feedStepSize() const
    {
        return 0.0;
    }

    // (Not used if the feed is manual)
    virtual double feedPosition() const = 0;
    virtual bool feedMoving() const = 0;
    virtual void feedMoveTo(double position) = 0; // (at the feed axis's speed)

    // Retracting the feed axis between passes (moving it while retracted
    // counts as feedMoving()). If to is set, the feed axis retracts to there
    // (see MultiPassPlan::retractTo), or else by the machine's usual amount.
    // unretract() goes back to where it retracted from
    virtual void retract(std::optional<double> to) = 0;
    virtual void unretract() = 0;
    virtual bool isRetracted() const = 0;

    // The index axis (e.g. a rotary table, in degrees), which turns the work
    // between cuts (see MultiPassPlan::indexPerStart). Only used if the plan
    // indexes
    virtual double indexPosition() const
    {
        return 0.0;
    }
    virtual bool indexMoving() const
    {
        return false;
    }
    // Moves the index axis to position, ending with a move the same way as
    // every other index (forwards, i.e. towards larger positions), even if
    // it's there already, so its backlash is taken up the same way for every
    // cut: if it isn't forwards of where the axis is (e.g. the first start),
    // the axis backs off a little and comes forwards to it
    virtual void indexMoveTo(double /*position*/) { }
    virtual double indexStepSize() const
    {
        return 0.0;
    }
};

// What each cut is
enum class MultiPassCut {
    // A plain feed along the cut axis, at its speed (e.g. turning)
    Feed,
    // A threading pass, locked to the spindle (see
    // IMultiPassMachine::cutThreadTo()). Needs a retract: the return isn't
    // locked to the spindle, so would drag the tool across the thread
    // (unless the return is a cut too: see MultiPassPlan::cutBack)
    Thread
};

// The parameters of a multi-pass
struct MultiPassPlan {
    MultiPassCut cut { MultiPassCut::Feed };
    // Each cut moves the cut axis from cutStart to cutEnd, then returns
    double cutStart { 0.0 };
    double cutEnd { 0.0 };
    // Where the feed axis starts (e.g. where the tool touched the work: the
    // first pass is a step in from here), and its position for the last
    // pass. With a manual feed, these just give the distance to feed in total
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
    // If retracting: only on this many passes at the end (0 for every pass).
    // The earlier, roughing passes return without retracting, which is
    // quicker, as only the finishing passes' surface matters. Repeating
    // the same cut until stopped (a feedPerPass of zero) retracts every
    // pass, as there's no last pass. Not for threading
    int retractLastPasses { 0 };
    // If retracting: where the feed axis retracts to, or nullopt for the
    // machine's usual amount. e.g. just clear of the work's surface at the
    // start (feedStart, plus a clearance): when boring, a fixed retract could
    // move the back of a boring bar into the far side of a small bore, but
    // the bar was clear of it with the tool at the start. (The machine
    // retracts at least a little anyway, e.g. if the feed axis is already
    // further out than this)
    std::optional<double> retractTo;
    // Wait for continueAfterPause() before each pass after the first
    bool pause { false };
    // Constant area: each pass removes about the same area as the first, as
    // for a V thread (whose area goes as the square of its depth), so the
    // passes get shallower as they go deeper. Pass n goes in to feedPerPass
    // times the square root of n from the start, but at least feedMinimum
    // further than the pass before (or than it was nudged to). Otherwise,
    // each pass goes feedPerPass further
    bool constantArea { false };
    double feedMinimum { 0.0 };
    // Compound (flank) infeed: each cut starts further along the cut axis by
    // this much per mm the feed axis has gone in from feedStart (signed, e.g.
    // tan 29.5 degrees the way the cut goes, as a topslide set over would),
    // so only one flank of a threading tool cuts. The cut axis moves there
    // before each cut, and returns there; the cut's end stays where it is.
    // Zero for none. Not with feedCorrection
    double cutShiftPerFeed { 0.0 };
    // (Experimental) For a machine which cuts a slight taper: how far the
    // feed axis follows the cut axis, as a fraction of the cut axis's
    // movement, during each cut (and back again on the return), to cancel
    // it out. e.g. tan(angle). Zero for none. Not with a manual feed
    double feedCorrection { 0.0 };
    // Threading only: how many starts each pass cuts, one after the other at
    // the same depth, each starting 1/starts of a revolution of the spindle
    // further round than the one before (as for a multi-start thread, or
    // the grooves of a cut knurl). The feed axis moves on once every start
    // has been cut
    int starts { 1 };
    // Threading only: the return is a threading pass too, with the tool
    // still in, so it cuts a helix of the opposite hand, with the spindle
    // still turning the same way (e.g. a cut knurl's second set of
    // grooves). The tool must have room to start at the cut's end, e.g. a
    // relief groove. Each start is cut there and back before the next. Not
    // with a retract
    bool cutBack { false };
    // Indexing (e.g. gear cutting, with the starts as the teeth): if not
    // zero, each start is a position of the index axis rather than of the
    // spindle, this much further on (forwards) than the one before. Before
    // each start's cut, the index axis moves there. It only ever moves
    // forwards: with starts × indexPerStart one turn (e.g. 360 / starts
    // degrees), each pass goes once round, and the next pass carries on
    // round, rather than going back. After the last pass it moves on once
    // more, so it ends a whole number of turns from where it started. Not
    // for threading, nor with a retract
    double indexPerStart { 0.0 };
    // (If indexing) Where the index axis is for the first start
    double indexStart { 0.0 };
};

// Where a multi-pass's start and end positions come from, as chosen in its
// dialog. The start is a point (both axes); each axis's end is chosen
// separately, so e.g. the cut can end at a shoulder stored in a memory slot
// while the feed goes in by a distance from wherever the tool is now.
// Lengths are in mm.
struct MultiPassSetup {
    // The start: a memory slot (its position on both axes), or nullopt for
    // where the axes are now (unless startFeature)
    std::optional<std::size_t> startMemory;
    // Start from the current feature instead (startMemory is then nullopt):
    // the feed axis on the feature's surface, and the cut axis just off the
    // work's end (by MultiPassPositions::featureClearance, the opposite way
    // to the cut, which runs into the work). A cut distance is then from the
    // work's end, rather than from the start
    bool startFeature { false };
    // The cut axis's end: a memory slot (its cut axis position), or nullopt
    // for cutDistance from the start
    std::optional<std::size_t> cutEndMemory;
    // The feed axis's end: a memory slot (its feed axis position), or
    // nullopt for feedInfeed from the start
    std::optional<std::size_t> feedEndMemory;
    // The cut axis's change from start to end (signed, as the display shows
    // positions)
    double cutDistance { 0.0 };
    // How far the feed axis goes into the work, from start to end (not
    // negative). Which way "into the work" is depends on the retraction
    // direction (see MultiPassPositions::feedIntoWork)
    double feedInfeed { 0.0 };
    double feedPerPass { 0.0 };
    bool pause { false };
    bool retract { false };
    // If retracting: only on the last few passes (how many is set in the
    // config; see MultiPassPlan::retractLastPasses)
    bool retractLastOnly { false };
    // Retract the feed axis before moving to the start (when the cut axis
    // has to move to get there)
    bool retractBeforeMoveToStart { true };
    // Threading passes only: each pass removes about the same area, and
    // the infeed is along a flank (see MultiPassPlan::constantArea and
    // cutShiftPerFeed)
    bool constantArea { false };
    bool compoundInfeed { false };
    // Cut knurling only: how many grooves of each hand (see
    // MultiPassPlan::starts; 0 until chosen), and their angle to the cut
    // axis on the work's surface, in degrees (see knurlLeadMm())
    int knurlStarts { 0 };
    double knurlHelixAngleDeg { 45.0 };
};

// What's needed to work out a MultiPassSetup's positions (in mm)
struct MultiPassPositions {
    // Where the axes are now
    double cutHere { 0.0 };
    double feedHere { 0.0 };
    // Each memory slot's position on each axis (nullopt if not set)
    std::vector<std::optional<double>> cutMemory;
    std::vector<std::optional<double>> feedMemory;
    // +1 if the feed axis's position increases as it moves into the work,
    // -1 if it decreases (the opposite way to a retract)
    double feedIntoWork { -1.0 };
    // The current feature, if set: the work's end on the cut axis, and the
    // feed axis's position on the feature's surface (see
    // MultiPassSetup::startFeature)
    std::optional<double> featureWorkEnd;
    std::optional<double> featureFeed;
    // How far off the work's end a start from the feature is
    double featureClearance { 0.5 };
};

// A multi-pass's start and end positions, worked out from a MultiPassSetup
struct MultiPassRange {
    double cutStart { 0.0 };
    double cutEnd { 0.0 };
    double feedStart { 0.0 };
    double feedEnd { 0.0 };
};

// Either the range, or (if error isn't empty) why there isn't one
struct MultiPassResolved {
    MultiPassRange range;
    std::string error;
};

// Works out a setup's start and end positions. cutLabel and feedLabel (e.g.
// "Z" and "X") are for the error messages
MultiPassResolved resolveMultiPass(
    const MultiPassSetup& setup,
    const MultiPassPositions& positions,
    const std::string& cutLabel = "Z",
    const std::string& feedLabel = "X");

// The setup the dialog starts with, from the one used last time:
// - The start is a memory slot if the axes are at one (set on both axes),
//   or else the current feature if it's set (both its values), or else
//   where the axes are now.
// - Each end is the previous one's memory slot if that's still set (and,
//   for the cut axis, isn't the start), or else, if the start is a memory
//   slot, the first other slot set on that axis, or else a distance.
// - The distances, step-over, pause and retract are as before. Retracting
//   before moving to the start is always on.
MultiPassSetup
defaultMultiPassSetup(const MultiPassSetup& previous, const MultiPassPositions& positions);

// Cut knurling: the lead of the grooves (how far the cut axis moves per
// revolution of the spindle, for each start), for a work diameter and a helix
// angle (the grooves' angle to the cut axis, in degrees): pi x diameter /
// tan(angle). For 45 degrees, that's the work's circumference, so the cut
// axis has to move fast, and the spindle turn slowly
double knurlLeadMm(double diameterMm, double helixAngleDeg);

// Cut knurling: how many starts give grooves about pitchMm apart round the
// work (at least one)
int knurlStartsForPitch(double diameterMm, double pitchMm);

// True if an axis is at a position (within MultiPass::TOLERANCE)
bool atMultiPassPosition(double position, double target);

// How many passes a range takes with a step-over (the first a step in from
// the feed axis's start, the last at its end; at least one), or 0 if
// repeating until stopped (a step-over of zero). (See MultiPassPlan for
// constantArea and feedMinimum)
int multiPassCount(
    const MultiPassRange& range,
    double feedPerPass,
    bool constantArea = false,
    double feedMinimum = 0.0);

// A series of cuts: feed in (a step from the start), cut, (retract,) return,
// (unretract,) (pause,) and repeat until a cut has been made at the feed
// axis's end position. (With a step-over of zero, there's no feeding in:
// the same cut is repeated, at the start, until stopped.) With several starts
// (see MultiPassPlan::starts), each pass cuts every start before feeding in,
// and with a cut back, the return is a cut instead.
// While paused, a motorised feed axis may be moved (e.g. nudged): the next
// pass then feeds on from where it is.
// Non-blocking: call update() regularly (e.g. on each tick), and it starts
// the next move when the last has finished.
//
// The cuts are made at the cut axis's speed as it was when each cut starts
// (so the user can change it while the multi-pass runs), or, for threading,
// locked to the spindle; returns are at the cut axis's maximum speed.
class MultiPass {
public:
    enum class State {
        Idle, // Not started (or stopped)
        Cutting,
        CuttingBack, // (See MultiPassPlan::cutBack)
        Retracting,
        Returning,
        Paused, // Waiting for continueAfterPause()
        Unretracting,
        Feeding,
        Positioning, // Moving the cut axis to a pass's start (see cutShiftPerFeed)
        Indexing, // Moving the index axis to a start (see indexPerStart)
        Finishing, // Unretracting (or indexing on) after the last pass
        Finished
    };

    // How close (in mm) an axis must be to a position to count as there
    static constexpr double TOLERANCE = 0.0001;

    explicit MultiPass(IMultiPassMachine& machine);

    // Starts the first pass, feeding in by a step (with a manual feed, it
    // pauses first, for the user to). The cut and feed axes must be at the
    // plan's start positions (unless the feed is manual). Returns an empty
    // string if started, or else why not
    std::string start(const MultiPassPlan& plan);
    // Moves on from Paused. Does nothing in any other state
    void continueAfterPause();
    // While a pass is under way (not paused): asks for a pause before the
    // next pass, as if the plan paused (e.g. to clear swarf), or, if already
    // asked, cancels the request. Does nothing if it would pause anyway, or
    // in any other state. There's no pause after the last pass. When
    // indexing, the pause is before the next start instead (e.g. to look at
    // the tooth just cut), and continuing goes on to that start
    void togglePauseRequest();
    // True if a pause before the next pass has been asked for
    bool pauseRequested() const;
    // Abandons the multi-pass, giving the reason (shown by message()). The
    // caller stops the motors
    void stop(const std::string& reason);
    // Stops once the tool is clear of the work: during a cut, the cut is
    // finished, and the feed axis retracts (if the plan retracts) and the cut
    // axis returns as usual, but it's left retracted; otherwise (e.g.
    // paused), at once. e.g. for a threading pass, which, stopped part way,
    // would leave the tool cutting a ring in the thread
    void stopAfterPass(const std::string& reason);
    // Starts the next move if the last has finished
    void update();

    State state() const;
    // True from start() until finished or stopped
    bool inProgress() const;
    // The pass being made (from 1), or 0 if not started (or, with a manual
    // feed, paused before the first)
    int pass() const;
    // The number of passes in all, or 0 if repeating until stopped. (May
    // change if the feed axis is moved while paused)
    int totalPasses() const;
    // Which of the plan's starts is being cut (from 1; see
    // MultiPassPlan::starts)
    int startNumber() const;
    // How far the feed axis has been fed in from the start (for the pass
    // being made, or about to be)
    double fed() const;
    // While paused: how far the feed axis will move on for the next pass
    // (with a manual feed, how far the user should feed it). Zero if paused
    // between starts
    double nextFeed() const;
    // Whether it's paused between two starts of a pass (see
    // togglePauseRequest()), rather than between passes
    bool pausedBetweenStarts() const;
    // Whether the plan indexes (see MultiPassPlan::indexPerStart)
    bool indexes() const;
    // Why the multi-pass last stopped (empty if it finished, or hasn't)
    const std::string& message() const;
    const MultiPassPlan& plan() const;
    // Where the cut axis starts the pass being made (or last made): the
    // plan's cutStart, unless shifted (see MultiPassPlan::cutShiftPerFeed)
    double passCutStart() const;

private:
    // Whether the cut or feed axis is at a position (see cutStepSize())
    bool cutAt(double target) const;
    bool feedAt(double target) const;
    bool indexAt(double target) const;
    // Where the index axis goes for the start being cut
    double indexTarget() const;
    bool isLastPass() const;
    // Whether the pass being made retracts before its return
    bool passRetracts() const;
    // How far round from the first start the start being cut begins, as a
    // fraction of a revolution
    double revolutionOffset() const;
    // Where the next step on starts from: the feed axis's position (or, for
    // a manual feed, where it should be)
    double feedBase() const;
    // Where the feed axis goes for a pass (from 1), from where it is
    double nextFeedTarget(double from, int pass) const;
    // Indexes (if the plan does), then moves the cut axis to the pass's
    // start if need be, then cuts
    void startPass();
    void positionAndCut();
    void startCut();
    void startCutBack();
    void startReturn();
    void afterReturn();
    // The next start at this depth, if there is one, or else the next pass
    void readyForNextCut();
    void readyForNextPass();
    void startNextPass();
    void startFeed();
    void finish();
    void abandon(const std::string& reason);

    IMultiPassMachine& m_machine;
    MultiPassPlan m_plan;
    State m_state { State::Idle };
    int m_pass { 0 };
    // The start being cut (from 0)
    int m_start { 0 };
    // The feed axis's position for the pass being made
    double m_feedTarget { 0.0 };
    double m_passCutStart { 0.0 };
    // The cut axis's speed for cutting, while returning at full speed
    double m_cutSpeed { 0.0 };
    // (See togglePauseRequest())
    bool m_pauseRequested { false };
    // (See pausedBetweenStarts())
    bool m_pausedBetweenStarts { false };
    // (See stopAfterPass())
    std::string m_stopAfterPassReason;
    std::string m_message;
};

} // namespace mgo
