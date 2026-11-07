#pragma once

#include "configreader.h"
#include "linearscale.h"
#include "motionsequence.h"
#include "multipass.h"
#include "rotaryencoder.h"
#include "stepperControl/steppermotor.h"
#include "threadpitches.h"
#include "units.h"

#include <chrono>
#include <cmath> // IWYU pragma: keep (need for M_PI)
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <stack>
#include <vector>

// "Model", i.e. program state data

namespace mgo {

class IGpio;

constexpr int INF_RIGHT = std::numeric_limits<int>::min();
constexpr int INF_OUT = std::numeric_limits<int>::min();
constexpr int INF_LEFT = std::numeric_limits<int>::max();
constexpr int INF_IN = std::numeric_limits<int>::max();
constexpr int AXIS1_UNSET = INF_RIGHT;
constexpr int AXIS2_UNSET = INF_OUT;

constexpr double DEG_TO_RAD = M_PI / 180.0;

// What the status line shows when there's nothing else to show
constexpr const char* DEFAULT_GENERAL_STATUS = "Tab or F1: menu";

constexpr float INFEED = 0.05f; // mm
// The large number below is tan 29.5°
// (Cannot use std::tan in constexpr owing to side effects)
constexpr float SIDEFEED = INFEED * 0.5657727781877700776025887010584;

// "Modes" allow for special behaviour (like threading and tapering)
enum class Mode {
    None,
    Setup,
    Threading,
    Taper,
    Radius,
    MultiPass
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
    Function
};

enum class XDirection {
    Outwards, // towards operator, away from centre
    Inwards // away from operator, towards centre
};

enum class ZDirection {
    Left,
    Right
};

enum class Axis {
    Axis1,
    Axis2
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
ThreadingStopAction threadingStopAction(
    bool arrived,
    bool retractedAtStart,
    bool retractedNow,
    bool spindleTurning);

// (Privately an IMultiPassMachine: the multi-pass engine moves the axes
// through it)
class Model : private IMultiPassMachine {
public:
    Model(IGpio& gpio, mgo::IConfigReader& config)
        : m_gpio(gpio)
        , m_config(config)
    {
    }

    void initialise();

    // This should be repeatedly called from the run loop
    void checkStatus();
    
    void changeMode(Mode mode);
    // (Also cancels any motion sequence)
    void stopAllMotors();
    // True while a series of moves (e.g. a nudge, or the backlash take-up
    // before a taper) is in progress. The controller holds back keys meanwhile
    bool isMotionSequenceRunning() const;

    void axis1GoToStep(long step);
    void axis1GoToPosition(double pos);
    void axis1GoToOffset(double pos);
    void axis1GoToPreviousPosition();
    void axis1GoToCurrentMemory();
    void axis1Nudge(ZDirection direction, double nudgeAmountMm);
    void axis1Zero();
    void axis1SpeedDecrease();
    void axis1SpeedIncrease();
    void axis1FastReturn();
    void axis1SetSpeed(double speed);
    void axis1Wait();
    void axis1Stop();
    void axis1StorePosition();
    void axis1StorePosition(double mm);
    void axis1Move(ZDirection direction);
    void axis1Rapid(ZDirection direction);
    void axis1SpeedPreset();
    void axis1SaveBreadcrumbPosition();
    void axis1ClearBreadcrumbs();

    void axis2GoToPosition(double pos);
    void axis2GoToOffset(double pos);
    void axis2GoToPreviousPosition();
    void axis2SetSpeed(double speed);
    void axis2Wait();
    void axis2Stop();
    void axis2Zero();
    void axis2SynchroniseOff();
    void axis2GoToCurrentMemory();
    void axis2SpeedDecrease();
    void axis2SpeedIncrease();
    void axis2Nudge(XDirection direction, double nudgeAmountMm);
    void axis2FastReturn();
    void axis2Retract();
    void axis2StorePosition();
    void axis2StorePosition(double mm);
    void axis2Move(XDirection direction);
    void axis2Rapid(XDirection direction);
    void axis2SpeedPreset();
    void axis2SaveBreadcrumbPosition();
    void axis2ClearBreadcrumbs();

    void repeatLastRelativeMove(Axis axis);
    // The X position has been set as a diameter (so X is now measured from
    // the centre). This lasts until X is zeroed, or its position is set again
    void diameterIsSet();
    // The work's diameter at the tool (twice the X position), once the X
    // position has been set as a diameter (so X is measured from the centre);
    // nullopt until then. While X is retracted, it's the diameter X will
    // return to (so it doesn't change as X retracts and unretracts)
    std::optional<double> getDiameterMm() const;

    // Getters/setters:
    XDirection getRetractionDirection() const;
    void setRetractionDirection(XDirection direction);

    bool getIsAxis2Retracted() const;
    void setIsAxis2Retracted(bool flag);

    KeyMode getKeyMode() const;
    void setKeyMode(KeyMode mode);

    // The mode (e.g. threading), as set by changeMode()
    Mode getMode() const;


    double getRadius() const;
    void setRadius(double radius);

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
    // a stall), describing the fault and what to do
    std::string getFaultMessage() const;
    std::string getAxis1Status() const;
    std::string getAxis2Status() const;
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

    void selectPreviousMemorySlot();
    void selectNextMemorySlot();
    std::size_t getCurrentMemorySlot() const;

    long getAxis1Memory(std::size_t index) const;
    std::optional<double> getAxis1MemoryAsPosition(std::size_t index) const;
    long getAxis2Memory(std::size_t index) const;
    std::optional<double> getAxis2MemoryAsPosition(std::size_t index) const;
    unsigned getMemorySize() const;
    void clearAllAxis1Memories();
    void clearAllAxis2Memories();

    void setAxis1MotorSpeed(double speed);
    void setAxis2MotorSpeed(double speed);
    void setAxis1Position(double mm);
    void setAxis2Position(double mm);
    void resetMotorThreads();
    double getAxis1MotorPosition() const;
    double getAxis2MotorPosition() const;
    double getAxis1MotorSpeed() const;
    // See RotaryEncoder::getSpindlePulses()
    int64_t getSpindlePulses() const;
    // When threading: the speed Z moves at (pitch x the spindle's speed)
    double getThreadingZSpeed() const;
    double getAxis2MotorSpeed() const;
    double getAxis1MotorCurrentStep() const;
    double getAxis2MotorCurrentStep() const;

    bool isAxis1MotorRunning() const;
    bool isAxis2MotorRunning() const;

    float getChuckAngle() const;

    std::string formatAxis1Position(long step) const;
    std::string formatAxis2Position(long step) const;

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
    float getAxis1LinearScalePosMm() const;

    void clearCurrentMemorySlot(Axis axis);

    // Checks to see whether any limit switches have been triggered
    bool limitSwitchTriggered() const;

    void lockAxis(unsigned axisNumber);
    void unlockAxis(unsigned axisNumber);
    bool isAxisLocked(unsigned axisNumber) const;

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
    // Enter, in the mode. If the axes aren't at the start, moves them there
    // (retracting X first, if the setup says so) and waits; once there,
    // starts the multi-pass. If it's paused, continues it. If it can't do
    // any of these, the warning says why
    void multiPassGo();
    // Whether both axes are at the multi-pass's start
    bool multiPassAtStart() const;
    // Whether the axes are moving to the start (after a multiPassGo())
    bool multiPassMovingToStart() const;
    const MultiPass& multiPass() const;

private:
    // The steps to move Z to a step: just the move, or, for a taper or
    // radius, taking up both axes' backlash, then synchronising X and moving.
    // (The last step's done is left empty, so the sequence ends once Z starts)
    std::vector<MotionSequence::Step> axis1MoveSteps(long step);
    MotionSequence::Step takeUpZBacklashStep(ZDirection direction);
    MotionSequence::Step takeUpXBacklashStep(int stepAdd);
    void synchroniseXForTaper();
    void synchroniseXForRadius();
    MotionSequence m_motionSequence;

    IGpio& m_gpio;
    IConfigReader& m_config;
    std::unique_ptr<mgo::RotaryEncoder> m_rotaryEncoder;
    // Currently only one linear scale is supported; another could be added for Axis2
    std::unique_ptr<mgo::LinearScale> m_linearScaleAxis1;
    std::unique_ptr<mgo::StepperMotor> m_axis1Motor;
    std::unique_ptr<mgo::StepperMotor> m_axis2Motor;
    std::vector<long> m_axis1Memory { AXIS1_UNSET, AXIS1_UNSET, AXIS1_UNSET,
                                      AXIS1_UNSET, AXIS1_UNSET, AXIS1_UNSET };
    std::vector<long> m_axis2Memory { AXIS2_UNSET, AXIS2_UNSET, AXIS2_UNSET,
                                      AXIS2_UNSET, AXIS2_UNSET, AXIS2_UNSET };
    std::size_t m_currentMemory { 0 };
    ThreadTable m_threads;
    ThreadChoice m_threadChoice;
    std::string m_generalStatus { DEFAULT_GENERAL_STATUS };
    std::string m_axis1Status { "stopped" };
    std::string m_axis2Status { "stopped" };
    std::string m_warning;
    std::string m_input; // general-purpose string for user-entered data
    bool m_quit { false };
    bool m_shutdown { false };
    float m_previousAxis1Speed { 40.f };
    bool m_axis1FastReturning { false };
    bool m_axis2FastReturning { false };
    bool m_axis1RapidInProgress { false };
    bool m_axis2RapidInProgress { false };
    int m_keyPressed { 0 };
    double m_taperAngle { 0.0 };
    double m_radius { 0.0 };
    float m_taperPreviousXSpeed { 40.f };
    // The current mode (e.g. threading, taper). Only set by changeMode()
    Mode m_mode { Mode::None };
    // Z has stopped at the end of a taper or radius cut: X's synchronisation
    // is turned off once it has stopped too
    bool m_synchronisedCutEnding { false };
    bool m_synchronisedCutXStoppedOnce { false };
    KeyMode m_keyMode { KeyMode::None };
    // Used to store position to return to after retract:
    long m_xOldPosition;
    bool m_axis2Retracted { false };
    float m_previousAxis2Speed { 40.f };
    bool m_fastRetracting { false };

    bool m_axis1WasRunning { false };
    bool m_axis2WasRunning { false };
    bool m_spindleWasRunning { false };
    // When the spindle started reading as stopped (see checkSpindleStopped())
    std::optional<std::chrono::steady_clock::time_point> m_spindleLowSince;

    XDirection m_xRetractionDirection { XDirection::Outwards };
    // Once the user has set the x position once then we use
    // the status bar to display the effective diameter
    bool m_xDiameterSet { false };
    DisplayUnits m_displayUnits { DisplayUnits::Metric };
    double m_axis1LastRelativeMove { 0.0 };
    double m_axis2LastRelativeMove { 0.0 };
    Axis m_lastRelativeMoveAxis;

    // "Breadcrumb" trail of positions for axes:
    std::stack<double> m_axis1PreviousPositions;
    std::stack<double> m_axis2PreviousPositions;
    MultiPassSetup m_multiPassSetup;
    MultiPassRange m_multiPassRange;
    MultiPass m_multiPass { *this };
    bool m_multiPassMovingToStart { false };

    std::set<unsigned> m_axisLocks;

    // Set when a motor's driver goes into alarm (see StepperMotor::setAlarmPin()).
    // The position is then lost, so these stay set until the axis is re-zeroed
    // or its position set (which also clears the motor's fault, if the
    // driver's alarm has been cleared)
    bool m_axis1Faulted { false };
    bool m_axis2Faulted { false };

    // Threading: the Z move (a cut or a return) currently in progress
    bool m_threadingMoveInProgress { false };
    long m_threadingMoveTarget { 0 };
    bool m_threadingMoveStartedRetracted { false };
    // During a pass: the step Z should be at now (from the spindle's position)
    std::function<long()> m_threadingTarget;
    // How far Z may fall behind that before the pass is stopped
    long m_threadingMaxLagSteps { 0 };
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
    void checkSpindleStopped(float chuckRpm);
    void checkEncoderLostPulses();
    // X is no longer known to be measured from the centre
    void clearDiameter();
    void clearMotorFault(Axis axis);
    // IMultiPassMachine: the cut axis is axis 1, and the feed axis is axis 2.
    // (Whether they're moving is as seen by checkStatus(), so the multi-pass
    // only moves on once checkStatus() has dealt with a move's end, e.g.
    // clearing the retracted flag after an unretract)
    double cutPosition() const override;
    bool cutMoving() const override;
    void cutMoveTo(double position) override;
    double cutSpeed() const override;
    void setCutSpeed(double speed) override;
    double cutMaxSpeed() const override;
    double feedPosition() const override;
    bool feedMoving() const override;
    void feedMoveTo(double position) override;
    void retract() override;
    bool isRetracted() const override;
    // Moving to the multi-pass's start (see multiPassGo())
    void multiPassMoveToStart();
    // +1 if X's position increases as it moves into the work (i.e. the
    // opposite way to a retract), or -1 if it decreases
    double axis2IntoWorkSign() const;
    // Threading
    // Starts a threading pass: Z follows the spindle to endStep. Returns false
    // if it couldn't start (with a warning)
    bool axis1StartThreadingPass(long endStep);
    void threadingCheckSpeed();
    void threadingCheckMoveEnded();
    double threadingMaxZSpeed() const;
    void setThreadingWarning(const std::string& warning, bool sticky);
};

} // end namespace
