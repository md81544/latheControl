#pragma once

#include "backlash.h"
#include "configreader.h"
#include "droserial.h"
#include "gearcutting.h"
#include "manualaxis.h"
#include "motionsequence.h"
#include "multipass.h"
#include "radius.h"
#include "rotaryencoder.h"
#include "scale.h"
#include "steppermotor.h"
#include "threadpitches.h"
#include "toollibrary.h"
#include "units.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cmath> // IWYU pragma: keep (need for M_PI)
#include <deque>
#include <functional>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <stack>
#include <string>
#include <vector>

// "Model", i.e. program state data

namespace mgo {

class IGpio;

constexpr int INF_RIGHT = std::numeric_limits<int>::min();
constexpr int INF_OUT = std::numeric_limits<int>::min();
constexpr int INF_LEFT = std::numeric_limits<int>::max();
constexpr int INF_IN = std::numeric_limits<int>::max();

constexpr double DEG_TO_RAD = M_PI / 180.0;

// What the status line shows when there's nothing else to show
constexpr const char* DEFAULT_GENERAL_STATUS = "Tab or F1: menu";

// "Modes" allow for special behaviour (like threading and tapering)
enum class Mode {
    None,
    Threading,
    Taper,
    Radius, // Radius cutting (see Model::setRadiusSetup())
    MultiPass,
    Knurling, // Cut knurling (see Model::setKnurlingSetup())
    GearCutting // (See Model::setGearSetup())
};

// "Key Modes" allow for two-key actions, a bit like vim.
// Initial use case is to allow for actions to only apply to
// Z or X axis - for example, "xz" means zero X only. "xm" means
// memorise X only. "z<Enter>" means go to the stored Z value only.
// Pressing "m" or "<Enter>" will apply to both axes. There won't
// be a single keypress for zeroing both axes at once - use "zz" and "xz".
enum class KeyMode {
    None,
    Axis1,
    Axis2,
    AxisAll,
    // The manual axes (see Model::manualAxis())
    Axis3,
    Axis4
};

enum class XDirection {
    Outwards, // towards operator, away from centre
    Inwards // away from operator, towards centre
};

enum class ZDirection {
    Left,
    Right
};

// Which way a motorised axis moves, for its moves, rapids and nudges. Forward
// is the left and up arrows' way: on the lathe, Z to the left and X in; on
// the mill, X to the left and A up. Forward raises the motor's step count,
// unless AxisNMotorFlipDirection is set
enum class AxisDirection {
    Forward,
    Back
};

constexpr AxisDirection axisDirection(ZDirection d)
{
    return d == ZDirection::Left ? AxisDirection::Forward : AxisDirection::Back;
}

constexpr AxisDirection axisDirection(XDirection d)
{
    return d == XDirection::Inwards ? AxisDirection::Forward : AxisDirection::Back;
}

// The touches which measure a tool's offsets, in order (see
// Model::startToolMeasurement()): the current tool on the work's diameter,
// then on its end; then, after changing tool, the tool being measured on the
// same diameter and end
enum class ToolTouchStep {
    ReferenceDiameter,
    ReferenceEnd,
    ToolDiameter,
    ToolEnd,
    Finished
};

// Threading: what to do with X when a pass's Z move ends (with ThreadingAutoRetract).
// "arrived" is whether Z reached its target, rather than being stopped short;
// "retractedAtStart" and "retractedNow" are whether X was retracted when the
// move started, and is now
enum class ThreadingStopAction {
    None,
    RetractOut,
    RetractIn
};
ThreadingStopAction
threadingStopAction(bool arrived, bool retractedAtStart, bool retractedNow, bool spindleTurning);

// (Privately an IMultiPassMachine: the multi-pass engine moves the axes
// through it)
class Model : private IMultiPassMachine {
public:
    // Below this, the spindle counts as stopped when knurling (see
    // minimumSpindleRpm()). At 5 rpm, the lathe's encoder still gives a
    // pulse every 17 ms or so
    static constexpr float KNURLING_MIN_SPINDLE_RPM = 5.f;

    Model(IGpio& gpio, mgo::IConfigReader& config)
        : m_gpio(gpio)
        , m_config(config)
        , m_threadingAutoRetract(config.readBool("ThreadingAutoRetract", false))
    {
    }
    // Stops the scale before the motors, and axis 2's motor before axis 1's
    // (see resetMotorThreads()), rather than leaving it to the members'
    // order: the fake scale's thread reads axis 1's motor, and the motors are
    // in m_motorAxes, which is destroyed first
    ~Model();
    Model(const Model&) = delete;
    Model& operator=(const Model&) = delete;

    void initialise();

    // This should be repeatedly called from the run loop
    void checkStatus();

    void changeMode(Mode mode);
    // (Also cancels any motion sequence)
    void stopAllMotors();
    // A rapid's key (or the gamepad's stick) has been let go. If a rapid is
    // running, and nothing else is, the moving motors slow down to a stop
    // (if they decelerate; see StepperMotor::decelerateToStop()), and this
    // returns true. Another release while they slow down changes nothing. Otherwise it returns
    // false, and the caller should stop everything at once (as Space)
    bool rapidReleased();
    // True while a series of moves (e.g. a nudge, or the backlash take-up
    // before a taper) is in progress. The controller holds back keys meanwhile
    bool isMotionSequenceRunning() const;

    // The motorised axes (1 and 2). Each of these takes an axis number
    bool isMotorRunning(int axisNumber) const;
    // In the axis's units (e.g. mm) per minute
    double motorSpeed(int axisNumber) const;
    void setMotorSpeed(int axisNumber, double speed);
    // Waits for the motor to stop (e.g. for the tests)
    void waitForMotor(int axisNumber);
    // Stops the motor at once, and waits for it to stop. (stopAllMotors(),
    // for Space, stops both together, and ends whatever they were doing)
    void stopAxis(int axisNumber);
    // Stops the motor, slowing down first if it decelerates (see
    // StepperMotor::decelerateToStop()), without waiting. If it's already
    // slowing down, it stops at once (e.g. a key pressed again meanwhile)
    void stopAxisRamped(int axisNumber);
    // Moves with no end, until stopped: pressing the same key again (i.e. a
    // move or rapid while it's moving) stops it, slowing down first if the
    // axis decelerates. A rapid is at the motor's full speed, and only when
    // not in a mode. Axis 1's follow the taper or the thread, in those modes
    void move(int axisNumber, AxisDirection direction);
    void rapid(int axisNumber, AxisDirection direction);
    // A short move by mm, at a tenth of the motor's full speed, which keys
    // wait for
    void nudge(int axisNumber, AxisDirection direction, double mm);
    // At full speed to the current memory slot, if it's set for the axis
    // (axis 1, in a taper, at 100 mm/min, which X can follow)
    void fastReturn(int axisNumber);
    long motorStep(int axisNumber) const;
    // The motor's position (for axis 2, what's shown is getAxis2Position(),
    // less any taper correction)
    double motorPosition(int axisNumber) const;
    // Shown on the status line, e.g. "stopped" or "Going to 10.000"
    std::string axisStatus(int axisNumber) const;
    // Moves to a position, or by an offset from where it is (which
    // repeatLastRelativeMove() repeats). Axis 1's moves follow the taper or
    // the thread, in those modes, and finish on its scale, if it has one
    void goToPosition(int axisNumber, double mm);
    void goToOffset(int axisNumber, double mm);
    void repeatLastRelativeMove(int axisNumber);
    // Goes back along the axis's "breadcrumb" trail: to the last position
    // it stopped at (or was nudged to) before this one
    void goToPreviousPosition(int axisNumber);
    // Speeds the motor up or slows it down by a step: fine steps at low
    // speeds, coarse ones above (axis 1: 1 below 20, then 20; axis 2: 2
    // below 10, then 10), never above the axis's AxisNMaxMotorRpm. Axis 1's
    // speed is left alone when threading, as the spindle sets it
    void speedIncrease(int axisNumber);
    void speedDecrease(int axisNumber);
    // Sets the axis's speed to one of its presets (1 to 5), except when
    // threading
    void selectSpeedPreset(int axisNumber, int preset);
    // An axis's preset speed (1 to 5): AxisNSpeedPresetM, or its default.
    // The menu shows these too
    static double speedPreset(const IConfigReader& config, int axisNumber, int preset);

    void axis1GoToStep(long step);

    void axis2SynchroniseOff();
    // Retracts X by Axis2RetractionDistance, or, if retracted, unretracts
    void axis2Retract();

    // The X position has been set as a diameter (so X is now measured from
    // the centre). This lasts until X is zeroed, or its position is set again.
    // As the tool is on the surface just measured, its diameter becomes the
    // current feature's (if the current feature is enabled)
    void diameterIsSet();
    // The work's diameter at the tool (twice the X position), once the X
    // position has been set as a diameter (so X is measured from the centre);
    // nullopt until then. While X is retracted, it's the diameter X will
    // return to (so it doesn't change as X retracts and unretracts)
    std::optional<double> getDiameterMm() const;

    // The current feature (lathe; enabled by CurrentFeature in the config): a
    // remembered diameter of the surface being worked on, and the Z of the
    // work's end (e.g. after facing). Each is optional, and kept for this
    // session only. Unlike getDiameterMm(), the diameter doesn't change as X
    // moves. Each is cleared when its axis's position is lost or changed: a
    // re-zero, a motor fault, or (by the controller, unless adjusting) a
    // position set. The diameter also goes when X is no longer measured from
    // the centre. A multi-pass which cuts from the work's end sets the
    // diameter to the one it finished at.
    bool currentFeatureEnabled() const;
    std::optional<double> getFeatureDiameterMm() const;
    std::optional<double> getWorkEndZ() const;
    // Records the diameter at the tool (as getDiameterMm()) as the feature's.
    // Returns false (and changes nothing, but warns) if it isn't known
    bool recordFeatureDiameter();
    // Records Z here as the work's end
    void markWorkEnd();
    void clearWorkEnd();
    void clearFeature();
    // Going to the current feature (each needs the values it goes to). "Clear"
    // is FeatureStartClearance off the work's end, away from the chuck (see
    // Axis1TowardsChuck); otherwise exactly at it. If one can't be done, the
    // warning says why.
    // - The start: X on the feature's surface, Z at the work's end (both
    //   axes, moving as moveToSafely(), retracting first)
    // - X to the feature's surface (Z doesn't move)
    // - X to the feature's surface goes as moveToSafely() does (so if X was
    //   retracted, it's no longer)
    // - Z to the work's end, as a Z go to (at Z's speed; X doesn't move). Only
    //   if X isn't deeper into the work than the feature's surface, so Z can't
    //   run the tool into it
    void featureGoToStart(bool clear);
    void featureGoToDiameter();
    void featureGoToWorkEnd(bool clear);
    // Whether going to the feature is possible now: not while threading or
    // tapering (whose Z moves are synchronised), during a multi-pass, or
    // while a series of moves is under way
    bool featureGoToAllowed() const;
    // The work's end Z, or (if clear) off it by FeatureStartClearance
    std::optional<double> featureWorkEndTarget(bool clear) const;

    // The tool library (lathe; ToolLibrary in the config: see toollibrary.h),
    // read from ToolLibraryFile at initialise()
    bool toolLibraryEnabled() const;
    const ToolLibrary& toolLibrary() const;
    // Why the tool can't be changed now (empty if it can): not while anything
    // is moving, during a multi-pass, in a mode other than multi-pass or
    // radius, or while measuring a tool
    std::string toolChangeUnavailable() const;
    // Changes the current tool. The axes' positions shift by the difference
    // between the two tools' offsets (nothing moves), so memory slots, the
    // current feature, a multi-pass's start and so on stay where they were
    // on the work, now for the new tool's tip. Saves the library. Returns why
    // not (also shown as a warning), or empty.
    // number may be ToolLibrary::UNKNOWN_TOOL, whose offsets aren't known:
    // nothing shifts, and the positions still read the last tool's tip (see
    // positionsTool()). Changing from it to a tool in the library shifts
    // from that tool's offsets, unless an axis was zeroed or its position set
    // meanwhile: that axis's positions read the unknown tool's tip, so they
    // can't be shifted, and what was set on the work with it is cleared (see
    // toolChangeClears())
    std::string selectTool(int number);
    // What changing to a tool would clear (see selectTool()): X measured from
    // the centre and the feature's diameter, and the work's end
    std::pair<bool, bool> toolChangeClears(int number) const;
    // The tool whose tip both axes' positions read: the tool in use, or
    // while the unknown tool is, the one before it. nullopt if that's the
    // unknown tool, for either axis
    std::optional<int> positionsTool() const;
    // Changes a tool's description and nose radius, and saves the library.
    // Returns why not, or empty
    std::string editTool(int number, const std::string& description, double noseRadiusMm);
    // Measuring a tool's offsets from the current tool's (external tools only,
    // so far). There are four touches (see ToolTouchStep), each recorded with
    // recordToolTouch(); then finishToolMeasurement() stores the tool and
    // makes it current. Meanwhile, the axes can be moved as usual, but
    // nothing else can be done (see keyAllowedInCurrentMode()). Only with no
    // mode, nothing moving, and X not retracted, with an outwards retraction
    // (as for an external tool). number may be a new tool's, or one already
    // in the library (but not the current tool). Returns why not (also shown
    // as a warning), or empty
    std::string toolMeasurementUnavailable(int number) const;
    std::string startToolMeasurement(int number);
    bool isMeasuringTool() const;
    // The tool being measured (0 if none)
    int toolMeasurementNumber() const;
    ToolTouchStep toolTouchStep() const;
    // Records the step's touch, where the axes are now. False (with a warning
    // saying why) if the axis being touched didn't last move towards the
    // surface (X in, for the diameter; Z towards the chuck, for the end), as
    // its backlash must be taken up the same way each time, or is still moving
    bool recordToolTouch();
    // Once all four touches are in: the tool's offsets
    std::optional<ToolOffsets> toolMeasurementOffsets() const;
    // Stores the tool measured, with its description and nose radius, and
    // makes it current. Returns why not, or empty
    std::string finishToolMeasurement(const std::string& description, double noseRadiusMm);
    void cancelToolMeasurement();

    // Getters/setters:
    XDirection getRetractionDirection() const;
    void setRetractionDirection(XDirection direction);

    bool getIsAxis2Retracted() const;
    void setIsAxis2Retracted(bool flag);

    KeyMode getKeyMode() const;
    void setKeyMode(KeyMode mode);

    // The mode (e.g. threading), as set by changeMode()
    Mode getMode() const;

    double getTaperAngle() const;
    void setTaperAngle(double taperAngle);

    int getKeyPressed() const;
    void setKeyPressed(int key);

    bool isShuttingDown() const;
    void shutDown();

    bool isQuitting() const;
    void quit();

    std::string getInputString() const;
    std::string& getInputString();
    void setInputString(const std::string& str);

    std::string getWarning() const;
    // Non-empty if a motor has faulted (its driver went into alarm, e.g. after
    // a stall), or a limit switch has stopped the motors (or is still
    // pressed), describing it and what to do
    std::string getFaultMessage() const;
    std::string getGeneralStatus() const;

    // The thread sizes, read from the ThreadsFile at initialise() (empty if
    // it couldn't be read; the log says why)
    const ThreadTable& getThreads() const;
    void setThreads(ThreadTable threads); // (e.g. for testing)
    // The thread to cut. Choosing a female (internal) thread also sets X to
    // retract inwards, and a male one outwards
    void setThread(const ThreadChoice& choice);
    const ThreadChoice& getThreadChoice() const;
    // The chosen thread, or nullptr if there are none
    const Thread* getThread() const;

    // Axes are numbered 1 to AXES, as in the config: 1 and 2 have motors
    // (the lathe's Z and X; the mill's X and A), and 3 and 4 are manual
    // axes, if any (see manualAxis()). Calls taking an axis number for an
    // axis that isn't there do nothing (see hasAxis())
    static constexpr int AXES = 4;
    // Whether there's an axis numbered axisNumber: axes 1 and 2 always, and
    // axes 3 and 4 if they're manual axes in the config
    bool hasAxis(int axisNumber) const;
    // e.g. "Z" (Axis1Label in the config)
    std::string axisLabel(int axisNumber) const;

    // Zeroing an axis, setting its position, or halving it. A manual axis
    // without a reading warns, and changes nothing. Each clears the axis's
    // memory slots, except setting a motorised axis's position (the dialog
    // clears them unless told to keep them), and on the lathe, the parts of
    // the current feature on that axis (axis 1: the work's end; axis 2: the
    // diameter, as X is no longer known to be measured from the centre).
    // Setting a motorised axis's position (or zeroing it) also clears a
    // motor fault, as the position is known again
    void zero(int axisNumber);
    // Zeroes every axis (a manual one without a reading warns, and isn't)
    void zeroAllAxes();
    void setPosition(int axisNumber, double mm);
    // Sets the position to half what it is (e.g. having zeroed it on one
    // edge and moved to the other, zero is then halfway between them). Not
    // while a motorised axis is moving
    void half(int axisNumber);

    // Memory slots. Each slot holds a position for every axis, or not, and
    // one slot is the current one, which storing, going to and clearing use
    void selectPreviousMemorySlot();
    void selectNextMemorySlot();
    std::size_t getCurrentMemorySlot() const;
    unsigned getMemorySize() const;
    // An axis's position in a slot (in mm), or nothing if it isn't set.
    // Zeroing an axis, setting its position or halving it clears its slots,
    // as does losing a manual axis's readings (and so its zero)
    std::optional<double> getMemory(int axisNumber, std::size_t slot) const;
    // Stores where the axis is in the current slot (a manual axis without a
    // reading warns, and keeps what it had)
    void storePosition(int axisNumber);
    // Stores a position (in mm) in the current slot: for a motorised axis,
    // the nearest whole step
    void storePosition(int axisNumber, double mm);
    void clearCurrentMemorySlot(int axisNumber);
    // Clears every slot
    void clearMemory(int axisNumber);
    // Goes to the current slot. A motorised axis moves there (unless it's
    // empty, or it's there already). A manual axis has no motor, so the user
    // winds it there, with the distance to go shown meanwhile (see
    // manualAxisGoTo()); nothing blocks, and other axes may move at the same
    // time; it warns if the slot's empty or there's no reading
    void goToCurrentMemory(int axisNumber);
    // Whether going to a slot on every axis (\ then Enter) includes this
    // axis: the motorised ones always, and a manual one unless
    // Axis3InSlotGoTo is false (e.g. the mill's Z, which the user won't move
    // while X is moving)
    bool inSlotGoTo(int axisNumber) const;

    void resetMotorThreads();
    // X's position as shown: the motor's, less any taper correction it's
    // making (see multiPassXCorrection())
    double getAxis2Position() const;
    // (Experimental) The config's TaperCorrection: an angle (in degrees,
    // as for taper mode) added to tapers and multi-pass cuts, to cancel out
    // a slight taper the machine cuts
    double taperCorrection() const;
    // The config's MultiPassRetractLastPasses: how many passes at the end
    // retract, when the multi-pass dialog says only the last ones do (at
    // least 1)
    int retractLastPasses() const;
    // The config's MultiPassRetractClearance: how far beyond the multi-pass's
    // start (the work's surface) X retracts to, in mm
    double multiPassRetractClearance() const;
    // Where X retracts to during a multi-pass, threading passes or spring
    // passes, and before moving to their start: just clear of the work's
    // surface at the start (see MultiPassPlan::retractTo)
    double multiPassRetractTarget() const;
    // See RotaryEncoder::getSpindlePulses()
    int64_t getSpindlePulses() const;
    // See RotaryEncoder::getSpindlePosition(): what threading passes follow
    double getSpindlePosition() const;
    // When threading or knurling: the speed Z moves at (pitch or lead x the
    // spindle's speed)
    double getThreadingZSpeed() const;

    float getChuckAngle() const;

    // A position (in mm) as shown, in the display units, e.g. " 1.0000"
    std::string formatPosition(int axisNumber, double mm) const;

    // Display units (mm or inches). Everything is metric internally; these
    // affect only what's displayed and what's typed into dialogs
    DisplayUnits getDisplayUnits() const;
    void toggleDisplayUnits();
    // Whether an axis (1 or 2) measures a length in mm (as opposed to e.g.
    // a rotary table in degrees), so is shown in the display units
    bool isLengthAxis(int axisNumber) const;
    // Converts an axis value (e.g. a position) between mm and the display
    // units. Values for axes which aren't lengths are left as they are
    double toDisplayUnits(int axisNumber, double value) const;
    double fromDisplayUnits(int axisNumber, double value) const;
    // The unit name for an axis's values as displayed, e.g. "mm", "in", "deg"
    std::string axisUnitName(int axisNumber) const;
    // Decimal places for an axis's positions as displayed
    int axisDecimals(int axisNumber) const;

    float getRotaryEncoderRpm() const;
    // A motorised axis's linear scale (AxisNScale in the config), if it has
    // one (only axis 1 can, so far). Its position is in the motor's frame,
    // so it's only known once the scale has been lined up with the motor
    // (see AxisScale), and while it has a recent reading
    bool hasScale(int axisNumber) const;
    bool scaleHasReading(int axisNumber) const;
    std::optional<double> scalePositionMm(int axisNumber) const;
    // The scale's position less the motor's
    std::optional<double> scaleDifferenceMm(int axisNumber) const;

    // Measuring axis 1's backlash with its scale (System menu; see
    // BacklashMeasurement). Only with a scale which has been lined up, in no
    // mode, and with nothing moving. stopAllMotors() (e.g. Space) stops it
    bool canMeasureAxis1Backlash() const;
    void axis1MeasureBacklash();
    bool isMeasuringBacklash() const;
    // How far each of its moves goes (in mm), and which way the first goes
    // (+1 or -1): away from the chuck, for a lathe's Z
    double axis1BacklashMeasureDistanceMm() const;
    int axis1BacklashMeasureFirstDirection() const;
    // Once a measurement has finished, its result, to show (once)
    std::optional<std::vector<std::string>> takeBacklashReport();

    // An axis's scale against its motor, each time the axis stops and the
    // scale settles. Within Axis1ScaleWarnMm, with Axis1ScaleRebase, the
    // motor's position is set from the scale's (a re-base: nothing moves),
    // so the position, and every move made from it, follows where the table
    // really is. Further apart than Axis1ScaleWarnMm, everything stops, and
    // this is how far apart they are (the scale less the motor), until
    // dealt with by one of the two below. Nothing can move meanwhile
    std::optional<double> scaleMismatchMm(int axisNumber) const;
    // How far apart they were allowed to be: Axis1ScaleWarnMm, plus the
    // backlash if the table had gone further the way it was moving
    double scaleMismatchLimitMm(int axisNumber) const;
    // If the mismatch is because the scale is misreading (its reading jumped
    // at rest by more than Axis1ScaleFaultMm, which the table can't have
    // done), by how much it jumped. Then only the motor's position can be
    // kept, and the limit above is Axis1ScaleFaultMm
    std::optional<double> scaleMisreadJumpMm(int axisNumber) const;
    // Goes up each time the user is to be asked about a mismatch (a new one,
    // or the same one again), so each is asked about once
    int scaleMismatchAsks(int axisNumber) const;
    // Whether either motorised axis's scale and motor disagree (as above)
    bool anyScaleMismatch() const;
    // Whether a motorised axis with a scale, which doesn't disagree with its
    // motor (yet), is still moving, or its reading settling. A mismatch
    // stops everything, and the other axis's scale then settles a little
    // later, so the controller waits for this to ask about every axis which
    // disagrees at once. (An axis which already disagrees isn't waited for:
    // a misreading scale may never settle)
    bool scalesStillSettling() const;
    // Whether the scale's reading has settled and is steady (see
    // AxisScale::isSteady())
    bool scaleIsSteady(int axisNumber) const;
    // Use the scale's position (e.g. after a stall). Only once its reading
    // is steady: until then, the mismatch stays, and is asked about again
    // once the reading has settled
    void acceptScalePosition(int axisNumber);
    // Keep the motor's position, and line the scale up with it again
    void keepMotorPosition(int axisNumber);
    // Re-basing an axis from its scale by hand (its leader, then B): its
    // motor's position is set to the scale's, as the re-bases at rest do,
    // so nothing moves, and the memory slots and current feature stay where
    // they are on the work. Only when the axis has stopped and its scale's
    // reading is steady, and not in a multi-pass, a motion sequence (e.g. a
    // nudge), a finishing move, a backlash measurement, or the lathe's
    // modes (as for the re-bases at rest). The exception: a multi-pass (or
    // gear cutting) which is paused, as the next pass moves to its start
    // from wherever the re-base leaves the axis (see also
    // Axis1ScaleRebaseBetweenPasses, and cutReady())
    bool canRebaseFromScale(int axisNumber) const;
    // Re-bases it, or warns why it can't
    void rebaseFromScaleByHand(int axisNumber);

    // Finishing an axis's positioning moves on its scale (stage 4): after a go
    // to (a memory, a typed or relative position, the last position), once
    // the scale has settled, if the table is more than a count from the
    // target, a correction move: on, if short; if past, backing off past the
    // backlash and approaching again, so the slack is always taken up the
    // same way. At most MAX_SCALE_CORRECTIONS, then a warning. Not for jogs,
    // nudges or the fast return, nor in a multi-pass or the lathe's modes.
    // Space stops it, and keys wait meanwhile, as for a nudge. Starts as
    // Axis1ScaleFinishMoves says (off unless set); the menu turns it on and
    // off
    static constexpr int MAX_SCALE_CORRECTIONS = 3;
    bool finishesOnScale(int axisNumber) const;
    void toggleFinishOnScale(int axisNumber);
    bool isFinishingOnScale(int axisNumber) const;
    // Whether either motorised axis is finishing a move on its scale (keys
    // wait meanwhile)
    bool anyFinishingOnScale() const;
#ifdef FAKE
    // The simulated scale, if the axis has a serial one (for the tests)
    FakeDroScale* fakeScale(int axisNumber);
#endif

    // The axes moved by hand (axes 3 and 4: Axis3Kind = manual in the
    // config), e.g. the mill's Y and Z, whose positions are read from their
    // scales, as on a DRO: zeroed, set and halved, with memory slots, and
    // go-tos which the user winds the handle for. (See "An array of axes" in
    // TODO.md)
    static constexpr int FIRST_MANUAL_AXIS = 3;
    static constexpr int LAST_MANUAL_AXIS = 4;
    // The manual axis numbered axisNumber, or nullptr if there isn't one
    const ManualAxis* manualAxis(int axisNumber) const;
    // A manual axis's go-to, while the user winds it there. It ends once the
    // reading has settled within two counts of the target (as close as the
    // scale can tell, with the reading flickering by a count), or when
    // cancelled (Space, Esc, or its origin changing)
    struct ManualAxisGoTo {
        double targetMm { 0.0 };
        // The target less where it is now
        double toGoMm { 0.0 };
        // Past the target, the way it was being wound
        bool past { false };
        // Within two counts of it, waiting for the reading to settle
        bool there { false };
    };
    std::optional<ManualAxisGoTo> manualAxisGoTo(int axisNumber) const;
    void cancelManualAxisGoTos();
#ifdef FAKE
    // Moves a manual axis's simulated scale (as if its handle were wound),
    // to a position in mm from where the simulated scale started
    void fakeMoveManualAxis(int axisNumber, double mm);
#endif

    // Limit switches (LimitSwitchPin in the config): any switch stops all the
    // motors at once (each motor's thread watches the pin), and they refuse
    // to move until the trip is cleared. True from then until cleared
    bool limitSwitchTripped() const;
    // Whether a limit switch is pressed now (false if there are none)
    bool limitSwitchPressed() const;
    // Lets the motors move again, e.g. off the switch. A switch still
    // pressed won't stop them again until it has been released
    void clearLimitSwitchTrip();

    // A locked axis isn't moved by anything: keys, memory returns, retracts
    // (ThreadingAutoRetract too), or a multi-pass. Each such move is refused,
    // with a warning to unlock it first. In a taper, X follows Z, so Z's
    // moves are refused while X is locked too. An axis can't be locked
    // while it's moving, or while a multi-pass runs (it can when paused), so
    // nothing already under way finds its axis locked part way through
    void lockAxis(int axisNumber);
    void unlockAxis(int axisNumber);
    bool isAxisLocked(int axisNumber) const;

    RotationDirection getChuckRotationDirection() const;

    const IConfigReader& config() const;

    // Multi-pass mode. The setup is chosen on entering it, and its start and
    // end positions are fixed then, from where the axes and memory slots are
    // at the time (so moving the axes, or changing a memory slot, doesn't
    // change them). Returns an empty string, or why the setup can't be used
    // (in which case nothing is changed)
    std::string setMultiPassSetup(const MultiPassSetup& setup);
    const MultiPassSetup& getMultiPassSetup() const;
    const MultiPassRange& getMultiPassRange() const;
    // Where the axes and memory slots are now, for resolveMultiPass()
    MultiPassPositions multiPassPositions() const;
    // Threading passes: a multi-pass in threading mode, each cut a threading
    // pass, always retracting (see MultiPassCut::Thread). Set up as for
    // setMultiPassSetup() (with the retract on, whatever setup says), but only
    // in threading mode with a thread chosen. Cleared on changing mode
    // (including choosing a thread again), so Enter goes back to the slot
    std::string setThreadingPassesSetup(const MultiPassSetup& setup);
    const MultiPassSetup& getThreadingPassesSetup() const;
    bool threadingPassesSet() const;
    // The setup the threading passes dialog starts with: as for
    // defaultMultiPassSetup(), from last time's, but if the thread has
    // changed since, the infeed is the new thread's depth
    MultiPassSetup threadingPassesDialogSetup() const;
    // Threading: whether X retracts at the end of a cut, and comes back in at
    // the end of the return (not during threading passes, which do their own
    // retracting). Starts as ThreadingAutoRetract in the config, and is turned
    // on and off from the menu, without being saved back to the config
    bool threadingAutoRetract() const;
    void toggleThreadingAutoRetract();
    // The chosen thread's depth (male or female, as chosen), if one is chosen
    std::optional<double> getThreadDepthMm() const;
    // The angle a compound infeed goes in at, from the cross-slide: half the
    // chosen thread's angle, less half a degree so the trailing flank just
    // clears (29.5 degrees for a 60 degree thread). 0 if no thread is chosen
    double getCompoundInfeedAngleDeg() const;
    // Spring passes: once threading passes have finished, Enter takes another
    // pass at the same depth, from wherever X is (so it can be nudged in
    // first), and pauses; Enter again takes another, until stopped. If the
    // axes aren't at the start of the cut, Enter moves back there first, to
    // the depth of the last pass. Until the passes are set up again
    bool springPassesAvailable() const;
    // Whether the multi-pass running (or last run) is spring passes
    bool springPasses() const;
    // Whether the axes are where a spring pass can start: Z at the cut's
    // start, and X not retracted, at the last pass's depth or deeper (nudged
    // in). If X is shallower (e.g. moved out to try the nut on), Enter moves
    // it back to the last pass's depth first, even while paused
    bool springPassAtStart() const;
    // Cut knurling (knurling mode): a multi-pass whose cuts are threading
    // passes at the knurl's lead, several starts per pass, each cut there
    // (one hand) and back (the other hand: see MultiPassPlan::cutBack), with
    // no retract. Set up as for setMultiPassSetup(), on entering the mode.
    // Needs X measured from the centre: the work's diameter at the start
    // sets the lead (see knurlLeadMm())
    std::string setKnurlingSetup(const MultiPassSetup& setup);
    const MultiPassSetup& getKnurlingSetup() const;
    // The setup the knurling dialog starts with: as for
    // defaultMultiPassSetup(), from last time's, with a number of starts to
    // suit the diameter if none has been chosen yet
    MultiPassSetup knurlingDialogSetup() const;
    // The knurl's lead (in mm per revolution), once set up in knurling mode
    std::optional<double> getKnurlLeadMm() const;
    // Gear cutting (gear cutting mode; see gearcutting.h): a multi-pass along
    // axis 1 (X on the mill), indexed by axis 2 (a rotary table), whose
    // starts are the teeth, with the depth fed in by hand before each pass
    // (each pass goes once round the blank). Set up on entering the mode.
    // Needs axis 2 to be a rotary table (in degrees), and GearCutting in the
    // config. Each cut starts with axis 1 at the start and axis 2 where the
    // tooth is; the first tooth is where axis 2 is when Enter starts the
    // passes. When they finish, axis 2 has gone a whole number of turns, and
    // its position is put back to where it started (e.g. 0°). (Its memory
    // slots aren't changed: they're still the same places on the table.)
    // Once the passes have started, the teeth stay where they are until the
    // number of teeth changes, or axis 2 is zeroed or set: if they're
    // stopped (e.g. part way through turning axis 2) and started again,
    // moving to the start also turns axis 2 forwards to the next tooth
    std::string setGearSetup(const GearSetup& setup);
    const GearSetup& getGearSetup() const;
    // Whether gear cutting has been set up (in gear cutting mode)
    bool gearCuttingSet() const;
    // The setup the gear dialog starts with (see defaultGearSetup())
    GearSetup gearDialogSetup() const;
    // Why gear cutting can't be used with this config (empty if it can)
    std::string gearCuttingUnavailable() const;
    // Whether axis 2 is where gear cutting would start (on a tooth, if
    // they've been started before: see setGearSetup())
    bool gearAOnTooth() const;
    // The fastest Z may need to move at the start of a pass locked to the
    // spindle (threading or knurling), in mm/min, leaving a margin for the
    // spindle speeding up. The spindle may turn at up to this divided by the
    // pitch or lead
    double spindlePassSpeedLimit() const;
    // The slowest the spindle may turn for a pass locked to it, in rpm. Below
    // this, it counts as stopped. Lower for knurling, whose long lead needs
    // a very slow spindle
    float minimumSpindleRpm() const;
    // Radius cutting (radius mode; see radius.h): a radius on the work's end,
    // or a shoulder with a fillet (the setup's kind). A multi-pass whose
    // passes are arcs about the radius's centre (and, for a shoulder,
    // straight passes first), each cut as a series of short tapers, from the
    // current feature (the work's diameter and its faced end), which must be
    // set. Set up on entering the mode; the passes are fixed then, from the
    // feature as it is at the time. A shoulder, once finished, leaves the
    // feature's diameter as the finished diameter
    std::string setRadiusSetup(const RadiusSetup& setup);
    const RadiusSetup& getRadiusSetup() const;
    // The radius of each pass's arc (see RadiusPlan::arcs)
    const std::vector<double>& radiusPassArcs() const;
    // Whether radius cutting has been set up (in radius mode)
    bool radiusSet() const;
    // Why radius cutting can't be set up now (empty if it can): it needs the
    // current feature, with both its values, on the outside of the work
    std::string radiusUnavailable() const;
    // Where the work is for a radius, from the current feature (only if
    // radiusUnavailable() is empty)
    RadiusWork radiusWork() const;
    // The setup of the multi-pass that Enter runs: the threading passes' in
    // threading mode, the knurling's in knurling mode, the radius's (whose
    // only option is to retract before moving to the start) in radius mode,
    // otherwise multi-pass mode's
    const MultiPassSetup& activeMultiPassSetup() const;
    // Enter, in multi-pass mode, or threading mode once threading passes are
    // set up. If the axes aren't at the start, moves them there
    // (retracting X first, if the setup says so) and waits; once there,
    // starts the multi-pass. If it's paused, continues it; if it's running,
    // asks it to pause before the next pass (or cancels that). If it can't do
    // any of these, the warning says why
    void multiPassGo();
    // Whether both axes are at the multi-pass's start
    bool multiPassAtStart() const;
    // Whether the axes are moving to the start (after a multiPassGo())
    bool multiPassMovingToStart() const;
    const MultiPass& multiPass() const;

private:
    // The steps for a threading pass to a step (see axis1StartThreadingPass()):
    // taking up Z's backlash, if it's to move, then the pass
    std::vector<MotionSequence::Step>
    threadingPassSteps(long step, std::vector<double> revolutionOffsets);
    // The steps to move Z to a step: just the move, or, for a taper, taking
    // up both axes' backlash, then synchronising X and moving.
    // (In threading mode, a threading pass)
    // (The last step's done is left empty, so the sequence ends once Z starts)
    std::vector<MotionSequence::Step> axis1MoveSteps(long step);
    // The steps to move Z to a step with X following it, moving ratio times
    // as far (e.g. tan of a taper angle): take up both axes' backlash, then
    // synchronise X and start Z with move
    std::vector<MotionSequence::Step>
    xFollowsZSteps(long step, double ratio, std::function<void()> move);
    MotionSequence::Step takeUpZBacklashStep(ZDirection direction);
    MotionSequence::Step takeUpXBacklashStep(int stepAdd);
    void synchroniseXForTaper(double ratio);
    // The fastest X can move, in mm/min (once ramped up)
    double axis2MaxSpeed() const;
    // How far X has moved (in mm) to correct the taper, during a multi-pass
    // (0 if none, or while retracted, when X unretracts to where the cut
    // started instead)
    double multiPassXCorrection() const;
    // When X has caught up at the end of a multi-pass move it followed
    void multiPassXFollowEnded();
    MotionSequence m_motionSequence;

    IGpio& m_gpio;
    IConfigReader& m_config;
    std::unique_ptr<mgo::RotaryEncoder> m_rotaryEncoder;
    // Reads the mill's DRO sender, if any axis's scale is on it. (Declared
    // before the scales, which are in m_motorAxes and the manual axes, so
    // outlives them. ~Model() stops each scale before the motors, which a
    // simulated scale follows)
    std::unique_ptr<mgo::DroSerialReader> m_droReader;
    // The serial device m_droReader reads (the first axis's to need it)
    std::string m_droDevice;
#ifdef FAKE
    // Where each manual axis's simulated scale is (see fakeMoveManualAxis()).
    // (Declared before the manual axes, as their simulated scales read these)
    std::array<std::atomic<double>, LAST_MANUAL_AXIS - FIRST_MANUAL_AXIS + 1> m_fakeManualAxisMm {};
#endif
    // Axes 3 and 4, if they're manual axes (see manualAxis())
    static constexpr std::size_t MANUAL_AXES = LAST_MANUAL_AXIS - FIRST_MANUAL_AXIS + 1;
    std::array<std::unique_ptr<ManualAxis>, MANUAL_AXES> m_manualAxes;
    // Each one's originChanges() when last seen (see checkManualAxes())
    std::array<int, MANUAL_AXES> m_manualOriginsSeen {};
    // Their go-tos under way (see manualAxisGoTo()): the target, and which
    // way it was to be wound (+1 or -1)
    struct ManualGoToTarget {
        double targetMm { 0.0 };
        double direction { 1.0 };
    };
    std::array<std::optional<ManualGoToTarget>, MANUAL_AXES> m_manualGoTos;
    // While measuring the backlash (see axis1MeasureBacklash())
    std::optional<BacklashMeasurement> m_backlashMeasurement;
    // The scale's settleCount() when the measurement's last move started
    int m_backlashSettleCount { 0 };
    double m_backlashPreviousSpeed { 0.0 };
    std::optional<std::vector<std::string>> m_backlashReport;
    // Each axis's memory slots (see getMemory()), indexed by its number less
    // one: positions in mm, which for a motorised axis are whole steps
    static constexpr std::size_t MEMORY_SLOTS = 6;
    std::array<std::vector<std::optional<double>>, AXES> m_memory { [] {
        std::array<std::vector<std::optional<double>>, AXES> memory;
        memory.fill(std::vector<std::optional<double>>(MEMORY_SLOTS));
        return memory;
    }() };
    std::size_t m_currentMemory { 0 };
    // A motorised axis's current slot, in steps, or nothing if it's not set
    std::optional<long> memoryStep(int axisNumber) const;
    void axis1GoToCurrentMemory();
    void axis2GoToCurrentMemory();
    void manualAxisGoToCurrentMemory(int axisNumber);
    // What logMotorMovement() last saw of a motor
    struct MotorLog {
        bool running { false };
        long step { 0 };
    };
    // What's kept for each motorised axis (axes 1 and 2), besides its motor
    // A finishing move on the scale under way (see finishesOnScale())
    struct ScaleFinish {
        // Where the table should end up (in the motor's frame when it
        // started, which re-basing keeps the same as the scale's)
        double targetMm { 0.0 };
        // +1 or -1: the way the move went, and so the way to approach it
        int direction { 1 };
        int corrections { 0 };
        // Backing off, to approach approachMm again
        bool backingOff { false };
        double approachMm { 0.0 };
        // The scale's settleCount() when its last move started
        int settles { 0 };
        // Stopped short of the target, by AxisNScaleApproachMm, for the
        // rest to be crept (see prepareFinish())
        bool approaching { false };
        // The speed before creeping (at AxisNScaleApproachSpeed), to go back
        // to after
        std::optional<double> speedBeforeCreep;
    };
    // Where a positioning move which is to finish on the scale goes first
    // (see prepareFinish())
    struct FinishStart {
        long firstStep { 0 };
        // The motor's speed before (if the whole move creeps, it's changed)
        double speed { 0.0 };
    };
    // A motorised axis's scale, if it has one, and its checks against the
    // motor (see checkScaleAtRest())
    struct ScaleState {
        std::unique_ptr<AxisScale> scale;
        // The scale's settleCount() when last checked against the motor
        int checkedSettles { 0 };
        // See scaleMismatchMm(), scaleMismatchLimitMm(),
        // scaleMisreadJumpMm() and scaleMismatchAsks()
        std::optional<double> mismatchMm;
        double mismatchLimitMm { 0.0 };
        std::optional<double> misreadJumpMm;
        int mismatchAsks { 0 };
        // The scale's position was chosen before its reading was steady
        bool askAgain { false };
        // Whether everything has been stopped for the stall the scale saw
        // during the last move (see AxisScale::stallMm())
        bool stallStopped { false };
        // See finishesOnScale()
        bool finishOnScale { false };
        std::optional<ScaleFinish> finish;
#ifdef FAKE
        FakeDroScale* fake { nullptr };
#endif
    };
    struct MotorAxisState {
        // Shown on the status line, e.g. "stopped" or "returning"
        std::string status { "stopped" };
        bool fastReturning { false };
        bool rapidInProgress { false };
        // The speed to go back to after a fast return or a rapid (axis 2's
        // is also used for X's speed around a taper)
        float previousSpeed { 40.f };
        // Whether the motor was running at the last checkStatus()
        bool wasRunning { false };
        MotorLog motorLog;
        // The tool whose tip the axis's positions read (see positionsTool()):
        // the tool in use when the axis was last zeroed or set, or changed to
        int positionsTool { ToolLibrary::REFERENCE_TOOL };
        // The last relative move (see repeatLastRelativeMove())
        double lastRelativeMove { 0.0 };
        // The "breadcrumb" trail of positions, for going back to the last
        std::stack<double> previousPositions;
        // Set when the motor's driver goes into alarm (see
        // StepperMotor::setAlarmPin()). The position is then lost, so this
        // stays set until the axis is re-zeroed or its position set (which
        // also clears the motor's fault, if the driver's alarm has been
        // cleared)
        bool faulted { false };
        // (Null before initialise())
        std::unique_ptr<StepperMotor> motor;
        // (After the motor, so destroyed before it: a simulated scale reads
        // it)
        ScaleState scaleState;
    };
    // (Destroyed in reverse: axis 2's motor, which follows axis 1's in a
    // taper, goes first)
    std::array<MotorAxisState, 2> m_motorAxes;
    // Axis 1's or axis 2's
    MotorAxisState& axisState(int axisNumber);
    const MotorAxisState& axisState(int axisNumber) const;
    // (See goToPosition(), goToOffset() and goToPreviousPosition())
    void axis1GoToPosition(double pos);
    void axis1GoToOffset(double pos);
    void axis2GoToPosition(double pos);
    void axis2GoToOffset(double pos);
    // Pushes where the axis is onto its breadcrumb trail (unless it's there
    // already), or clears the trail, leaving only where it is
    void saveBreadcrumbPosition(int axisNumber);
    void clearBreadcrumbs(int axisNumber);
    // Axis 1's or axis 2's motor (nullptr before initialise()). The const
    // one isn't a pointer to const, as the unique_ptr it wraps isn't either:
    // const functions call the motor's non-const ones (e.g. getMaxRpm())
    StepperMotor* motor(int axisNumber);
    StepperMotor* motor(int axisNumber) const;
    // The axis's scale and its state; scale() is null if it has none
    ScaleState& scaleState(int axisNumber);
    const ScaleState& scaleState(int axisNumber) const;
    AxisScale* scale(int axisNumber) const;
    ThreadTable m_threads;
    ThreadChoice m_threadChoice;
    std::string m_generalStatus { DEFAULT_GENERAL_STATUS };
    std::string m_warning;
    std::string m_input; // general-purpose string for user-entered data
    bool m_quit { false };
    bool m_shutdown { false };
    int m_keyPressed { 0 };
    double m_taperAngle { 0.0 };
    float m_taperPreviousXSpeed { 40.f };
    // The current mode (e.g. threading, taper). Only set by changeMode()
    Mode m_mode { Mode::None };
    // Z has stopped at the end of a taper cut (or one X follows in a
    // multi-pass): X's synchronisation
    // is turned off once it has stopped too
    bool m_synchronisedCutEnding { false };
    bool m_synchronisedCutXStoppedOnce { false };
    KeyMode m_keyMode { KeyMode::None };
    // Used to store position to return to after retract:
    long m_xOldPosition;
    bool m_axis2Retracted { false };
    bool m_fastRetracting { false };

    bool m_spindleWasRunning { false };
    // When the spindle started reading as stopped (see checkSpindleStopped())
    std::optional<std::chrono::steady_clock::time_point> m_spindleLowSince;

    XDirection m_xRetractionDirection { XDirection::Outwards };
    // How far X retracts (Axis2RetractionDistance), in mm
    double m_axis2RetractionDistance { 2.0 };
    // Once the user has set the x position once then we use
    // the status bar to display the effective diameter
    bool m_xDiameterSet { false };
    // The current feature (see getFeatureDiameterMm()): X's position on the
    // feature's surface (so its diameter is twice this), and the work's end Z
    std::optional<double> m_featureX;
    std::optional<double> m_workEndZ;
    // See toolLibrary()
    ToolLibrary m_toolLibrary;
    // While measuring a tool (see startToolMeasurement()): which, and the
    // positions touched so far, in ToolTouchStep's order (for the diameter,
    // axis 2's; for the end, axis 1's)
    struct ToolMeasurement {
        int number { 0 };
        std::vector<double> touches;
    };
    std::optional<ToolMeasurement> m_toolMeasurement;
    DisplayUnits m_displayUnits { DisplayUnits::Metric };

    MultiPassSetup m_multiPassSetup;
    // (The range is for whichever was set up last: multi-pass mode's, or the
    // threading passes'. Each is set up on entering its mode, or after it)
    MultiPassRange m_multiPassRange;
    MultiPassSetup m_threadingPassesSetup;
    // The thread m_threadingPassesSetup was for (see threadingPassesDialogSetup())
    std::string m_threadingPassesThread;
    bool m_threadingPassesSet { false };
    // Where X was for the last threading pass, once they've finished (for
    // spring passes: see springPassesAvailable())
    std::optional<double> m_springPassX;
    // Where Z started the last threading pass (moved along by a compound
    // infeed): where spring passes start
    double m_springPassZ { 0.0 };
    bool m_springPasses { false };
    MultiPassSetup m_knurlingSetup;
    // (See getKnurlLeadMm(): 0 until set up)
    double m_knurlLeadMm { 0.0 };
    GearSetup m_gearSetup;
    bool m_gearSet { false };
    // Where axis 2 was for the first tooth, once gear cutting has started
    // (see setGearSetup())
    std::optional<double> m_gearToothOrigin;
    // Where axis 2 goes for the first tooth of the next run of passes
    double gearStartTooth() const;
    RadiusSetup m_radiusSetup;
    bool m_radiusSet { false };
    // The radius's passes, and its multi-pass setup (see
    // activeMultiPassSetup())
    std::vector<MultiPassPath> m_radiusPasses;
    std::vector<double> m_radiusArcs;
    MultiPassSetup m_radiusMultiPassSetup;
    MultiPass m_multiPass { *this };
    bool m_multiPassMovingToStart { false };
    // X is following Z, to correct the taper, in a multi-pass cut or return
    // (and stays so until it's caught up with Z, as at the end of a taper)
    bool m_multiPassXFollowing { false };
    // Where X was when the last such cut started (in steps)
    long m_multiPassCutStartXStep { 0 };
    // X is finishing a return: see multiPassXFollowEnded()
    bool m_multiPassXSettling { false };

    std::set<int> m_axisLocks;
    // The last warning about a lock, which unlocking clears if still shown
    std::string m_lockWarning;
    // The axis lockAxis() last refused to lock while it was moving, so that
    // checkStatus() can clear the warning once it's stopped
    std::optional<int> m_lockRefusedAxis;

    // Whether a limit switch trip has been dealt with (see checkLimitSwitch())
    bool m_limitTripSeen { false };

    // See threadingAutoRetract()
    bool m_threadingAutoRetract;
    // Threading: the Z move (a cut or a return) currently in progress
    bool m_threadingMoveInProgress { false };
    long m_threadingMoveTarget { 0 };
    bool m_threadingMoveStartedRetracted { false };
    // During a pass: the step Z should be at now (from the spindle's position)
    std::function<long()> m_threadingTarget;
    // How far Z may fall behind that before the pass is stopped
    long m_threadingMaxLagSteps { 0 };
    // The pulse a pass starts at, and which way the spindle turns for it
    // (+1 if its pulse count goes up), for threadingCheckSpeed()'s watchdog
    double m_threadingStartPulse { 0.0 };
    int m_threadingSpindleSign { 1 };
    // Which of cutThreadTo()'s offsets the pass started at (see
    // threadStartChosen())
    std::optional<std::size_t> m_threadStartChosen;
    // Recent readings of the spindle's pulse count, one per checkStatus()
    // (see spindleTurningSign())
    struct SpindleSample {
        std::chrono::steady_clock::time_point time;
        int64_t pulses;
    };
    std::deque<SpindleSample> m_spindleSamples;
    // Which way the spindle turned for the first pass of the multi-pass
    // running (see threadingPassError()), once it's started
    std::optional<int> m_passesSpindleSign;
    // The speed Z moves at when threading (pitch x spindle speed), for display
    double m_threadingZSpeed { 0.0 };
    // For logging the end of a retract or unretract
    bool m_axis2RetractMoveInProgress { false };
    // Set if encoder pulses are lost while threading (see
    // RotaryEncoder::getLostPulseEvents()); stays until threading is restarted
    bool m_threadingPositionLost { false };
    uint32_t m_encoderLostPulseEventsSeen { 0 };
    // Threading warnings (e.g. spindle too fast). A "sticky" one stays until
    // the next pass starts; otherwise it reflects the current state
    std::string m_threadingWarning;
    bool m_threadingWarningSticky { false };

    // Private functions
    void checkMotorFaults();
    // Stops everything (e.g. a multi-pass) when a limit switch trips
    void checkLimitSwitch();
    void checkSpindleStopped(float chuckRpm);
    void recordSpindleSample();
    // Which way the spindle is turning (+1 if its pulse count is going up, -1
    // if down), from how far its count has moved over the last tenth of a
    // second or so, or nullopt if it hasn't moved far enough to tell. (Not
    // from the encoder's direction, which is only that of its latest edge: at
    // low speeds, a stray edge can flip it for a few milliseconds)
    std::optional<int> spindleTurningSign() const;
    void checkEncoderLostPulses();
    // Creates an axis's scale, if the config gives it one
    void configureScale(int axisNumber);
    // Makes the scale described by an axis's Axis<n>Scale... config keys, or
    // returns nullptr (having logged why) if the config is wrong. source is
    // set to where it's read from, for the log. fakePosition is what a
    // simulated serial scale follows (in FAKE builds), and fake is set to
    // it
    std::unique_ptr<IScale> makeScale(
        int axisNumber,
        const std::string& type,
        double countsPerMm,
        bool reversed,
        std::function<double()> fakePosition,
        FakeDroScale** fake,
        std::string& source);
    // Creates the manual axes (axes 3 and 4), if the config has them
    void configureManualAxes();
    ManualAxis* manualAxisOrWarn(int axisNumber);
    // (See zero(), setPosition() and half())
    void axis1Zero();
    void axis2Zero();
    void manualAxisZero(int axisNumber);
    void setAxis1Position(double mm);
    void setAxis2Position(double mm);
    void setManualAxisPosition(int axisNumber, double mm);
    void axis1Half();
    void axis2Half();
    void manualAxisHalf(int axisNumber);
    // Called from checkStatus(): clears a manual axis's memory slots (and
    // cancels its go-to) when its origin changes, and ends each go-to once
    // it's there
    void checkManualAxes();
    // Called from checkStatus(): records each move's reading once the scale
    // has settled, and starts the next move, or reports the result
    void advanceBacklashMeasurement();
    // Starts the measurement's next move
    void backlashMeasurementMove();
    // Ends it (finished or not), putting the speed back
    void endBacklashMeasurement(const std::string& why);
    // An axis's backlash compensation, in mm
    double backlashCompensationMm(int axisNumber) const;
    // Called from checkStatus(): compares an axis's scale with its motor
    // once it has settled, re-basing or warning (see scaleMismatchMm())
    void checkScaleAtRest(int axisNumber);
    // Sets the axis's motor's position to the scale's
    void rebaseFromScale(int axisNumber, const char* why);
    // Whether a multi-pass is paused where axis 1 may be re-based from its
    // scale: a plain multi-pass, or gear cutting (not threading, knurling or
    // a radius, which haven't been tried with it)
    bool multiPassPausedForRebase() const;
    // Re-bases an axis from its scale if the reading is steady and more
    // than a count from the motor's position, and nothing (e.g. a nudge) is
    // moving it
    void rebaseIfApart(int axisNumber, const char* why);

    // Whether an axis's positioning moves finish on its scale now (see
    // finishesOnScale())
    bool finishApplies(int axisNumber) const;
    // The approach (AxisNScaleApproachMm, off unless set, and
    // AxisNScaleApproachSpeed): a move which is to finish on the scale, or a
    // multi-pass's feed which is to be re-based (see feedReady()), stops
    // this far short of its target, waits for the scale, then creeps the
    // rest at this speed, the same way. So the last of it never reverses
    // (so neither the backlash compensation nor its error comes into it),
    // has next to no momentum to carry it past, and leaves the slack taken
    // up the way the tool goes into the work. An overshoot into the work
    // can't be undone by a correction afterwards
    double scaleApproachMm(int axisNumber) const;
    double scaleApproachSpeed(int axisNumber) const;
    // The speed an axis's motor is left at after a move at this speed has
    // stopped: AxisNSpeedResetTo, if it was at or above AxisNSpeedResetAbove
    // (see checkStatus()), so a fast move ending with a creep still leaves
    // it slow
    double speedAfterStop(int axisNumber, double speed) const;
    // Before a positioning move of an axis to step: where it should go first,
    // if it's to finish on the scale with an approach (the approach short of
    // step; or step, at the approach speed, if it's closer than that)
    FinishStart prepareFinish(int axisNumber, long step);
    // After the move has started (to start.firstStep): finish it on the
    // axis's scale, if that's on and can be done now
    void startFinish(int axisNumber, long step, const FinishStart& start);
    // Called from checkStatus(): each time the scale settles, corrects or
    // finishes
    void advanceFinish(int axisNumber);
    // Starts one of its moves, to a position (in mm), creeping (at the
    // approach speed) or at the speed it was at
    void finishMove(int axisNumber, double position, bool creep);
    void endFinish(int axisNumber, const std::string& why);
    // Logs a motor starting or stopping, with where it was, where it's going,
    // and how fast, for diagnosing problems from lc.log. It's called on each
    // update, so a move which starts and stops between two of them (e.g. a
    // nudge) is logged as a change of position. (Anything else which changes
    // the position, e.g. zeroing, must update log.step)
    static void
    logMotorMovement(const StepperMotor& motor, MotorLog& log, const std::string& label);
    // X is no longer known to be measured from the centre
    void clearDiameter();
    void clearMotorFault(int axisNumber);
    // Shifts an axis's positions from one tool's offset to another's (see
    // selectTool())
    void shiftPositionsForTool(int axisNumber, double fromMm, double toMm);
    // Writes the tool library to ToolLibraryFile. Returns why not (also
    // shown as a warning), or empty
    std::string saveToolLibrary();
    // IMultiPassMachine: the cut axis is axis 1, and the feed axis is axis 2.
    // (Whether they're moving is as seen by checkStatus(), so the multi-pass
    // only moves on once checkStatus() has dealt with a move's end, e.g.
    // clearing the retracted flag after an unretract)
    double cutPosition() const override;
    // With Axis1ScaleRebaseBetweenPasses, in a plain multi-pass or gear
    // cutting: before each cut, waits for axis 1's scale to settle (after the
    // return, about 2 s on the mill), then re-bases it, as at rest. So a
    // drift or lost steps are corrected at every pass, paused or not, and a
    // mismatch beyond Axis1ScaleWarnMm stops everything at the next pass
    bool cutReady() override;
    // The same for the feed axis (axis 2), with Axis2ScaleRebaseBetweenPasses:
    // after each feed to a pass's depth, waits for its scale to settle, then
    // re-bases it, so the pass is cut at the depth the scale says (the
    // multi-pass feeds again if the re-base leaves it short or past)
    bool feedReady() override;
    // A multi-pass's feed with an approach (see scaleApproachMm()): moving
    // short of the depth (backing off first, if past it), waiting for the
    // scale and re-basing, then creeping the rest. feedMoving() is true
    // until it's done
    struct FeedApproach {
        enum class Phase {
            BackingOff,
            MovingShort,
            Settling,
            Creeping
        };
        Phase phase { Phase::MovingShort };
        // In the motor's frame (re-basing keeps the same as the scale's)
        double targetMm { 0.0 };
        // +1 or -1: the way the feed goes in (from feedStart to feedEnd)
        int direction { 1 };
        // The scale's settleCount() when the move short started
        int settles { 0 };
        double speedBeforeCreep { 0.0 };
    };
    std::optional<FeedApproach> m_feedApproach;
    // Whether the feed now is to approach (Axis2ScaleApproachMm, with
    // Axis2ScaleRebaseBetweenPasses, as for feedReady())
    bool feedApproaches() const;
    // Called from checkStatus(): moves the feed's approach on
    void advanceFeedApproach();
    void endFeedApproach();
    bool cutMoving() const override;
    void cutMoveTo(double position, double feedFollows) override;
    double cutSpeed() const override;
    void setCutSpeed(double speed) override;
    double cutMaxSpeed() const override;
    // (Only in threading mode, with a thread chosen, or in knurling mode)
    std::string cutThreadTo(double position, const std::vector<double>& revolutionOffsets) override;
    std::optional<std::size_t> threadStartChosen() const override;
    double cutStepSize() const override;
    double feedStepSize() const override;
    double feedPosition() const override;
    bool feedMoving() const override;
    void feedMoveTo(double position) override;
    void retract(std::optional<double> to) override;
    void unretract() override;
    bool isRetracted() const override;
    // The index axis is axis 2 (only when gear cutting)
    double indexPosition() const override;
    bool indexMoving() const override;
    void indexMoveTo(double position) override;
    double indexStepSize() const override;
    // Gear cutting has finished: axis 2's position goes back to where the
    // passes started, a whole number of turns ago (see setGearSetup())
    void gearCuttingFinished();
    // Axis locks (see lockAxis()): true if a move of these axes is refused
    // because any of them is locked, in which case the warning says which to
    // unlock
    bool moveRefused(std::initializer_list<int> axisNumbers);
    // Whether lockAxis() would refuse, as something is moving the axis
    bool lockRefused(int axisNumber) const;
    // Clears lockAxis()'s warning once the axis has stopped
    void checkLockRefusal();
    // A move of one axis (with X too, for axis 1 in a taper, as X follows Z)
    bool axisMoveRefused(int axisNumber);
    // +1 if the motor's step count rises going that way, or -1
    int stepSign(int axisNumber, AxisDirection direction) const;
    // Moving to the multi-pass's start (see multiPassGo())
    void multiPassMoveToStart();
    // Moves both axes to a position without dragging the tool through the
    // work: X moves away from the work (or retracts first, if retract and Z
    // has to move) before Z moves, and towards it after. Z moves at full
    // speed. status is shown for each axis meanwhile; onDone is called once
    // there (not if stopped). The retract is to retractTo if set (as
    // axis2RetractTo()), or else the usual distance
    void moveToSafely(
        double zTarget,
        double xTarget,
        bool retract,
        const std::string& status,
        std::function<void()> onDone,
        std::optional<double> retractTo = std::nullopt);
    // Whether X can retract (or unretract) now
    bool axis2CanRetract() const;
    // Retracts X by distance mm (in the retraction direction)
    void axis2RetractBy(double distance);
    // Retracts X to position (in mm, as the motor's position), or, if that's
    // less than minimum mm further out than X is now, by minimum (so it
    // always moves clear of the surface it's on)
    void axis2RetractTo(double position, double minimum);
    // Back to where X retracted from
    void axis2Unretract();
    // +1 if X's position increases as it moves into the work (i.e. the
    // opposite way to a retract), or -1 if it decreases
    double axis2IntoWorkSign() const;
    // Updates the current feature after a multi-pass finishes
    void multiPassFinished();
    // Starts spring passes from where X is (see springPassesAvailable())
    void startSpringPasses();
    // Moves back to the start of the cut, at the last spring pass's depth
    void springPassMoveBack();
    // X's position, or while retracted, where it will return to
    double axis2WorkPosition() const;
    // Threading
    // Whether passes are locked to the spindle: threading or knurling mode
    bool spindleSynchronised() const;
    // How far Z moves per revolution of the spindle in a pass: the thread's
    // pitch, or the knurl's lead (nullopt if neither is set)
    std::optional<double> spindleLeadMm() const;
    // Starts a threading pass: Z follows the spindle to endStep, starting at
    // whichever of revolutionOffsets (fractions of a revolution after the
    // usual angle: see MultiPassPlan::starts) comes round first, which is
    // noted for threadStartChosen(). Returns false if it couldn't start
    // (with a warning)
    bool
    axis1StartThreadingPass(long endStep, const std::vector<double>& revolutionOffsets = { 0.0 });
    // Why a threading pass can't start now (empty if it can). sticky is
    // whether its warning should stay (see setThreadingWarning())
    struct ThreadingPassError {
        std::string message;
        bool sticky { false };
    };
    ThreadingPassError threadingPassError() const;
    void threadingCheckSpeed();
    void threadingCheckMoveEnded();
    double threadingMaxZSpeed() const;
    void setThreadingWarning(const std::string& warning, bool sticky);
};

} // end namespace
