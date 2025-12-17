#pragma once

#include "configreader.h"
#include "linearscale.h"
#include "rotaryencoder.h"
#include "stepperControl/steppermotor.h"

#include <cmath>
#include <limits>
#include <memory>
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

enum class MultiPassStage {
    NotStarted,
    Cutting,
    StepOver,
    NextCut,
    Finished
};

class Model {
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
    void stopAllMotors();
    void takeUpZBacklash(ZDirection direction);
    void startSynchronisedXMotorForTaper(ZDirection direction);
    void startSynchronisedXMotorForRadius(ZDirection direction);

    void axis1GoToStep(long step);
    void axis1GoToPosition(double pos);
    void axis1GoToOffset(double pos);
    void axis1GoToPreviousPosition();
    void axis1CheckForSynchronisation(ZDirection direction);
    void axis1CheckForSynchronisation(long step);
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
    void diameterIsSet();

    // Getters/setters:
    XDirection getRetractionDirection() const;
    void setRetractionDirection(XDirection direction);

    bool getIsAxis2Retracted() const;
    void setIsAxis2Retracted(bool flag);

    KeyMode getKeyMode() const;
    void setKeyMode(KeyMode mode);

    Mode getEnabledFunction() const;
    void setEnabledFunction(Mode mode);

    Mode getCurrentDisplayMode() const;
    void setCurrentDisplayMode(Mode mode);

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
    std::string getAxis1Status() const;
    std::string getAxis2Status() const;
    std::string getGeneralStatus() const;

    std::size_t getCurrentThreadPitchIndex() const;
    void setThreadPitch(std::size_t index);

    void selectPreviousMemorySlot();
    void selectNextMemorySlot();
    std::size_t getCurrentMemorySlot() const;

    long getAxis1Memory(std::size_t index) const;
    long getAxis2Memory(std::size_t index) const;
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
    double getAxis2MotorSpeed() const;
    double getAxis1MotorCurrentStep() const;
    double getAxis2MotorCurrentStep() const;

    bool isAxis1MotorRunning() const;
    bool isAxis2MotorRunning() const;

    float getChuckAngle() const;

    std::string convertAxis1StepToPosition(long step) const;
    std::string convertAxis2StepToPosition(long step) const;

    float getRotaryEncoderRpm() const;
    float getAxis1LinearScalePosMm() const;

    void clearCurrentMemorySlot(Axis axis);

    // This is called when the user presses ENTER when
    // inputting a mode parameter (e.g. taper angle)
    void acceptInputValue();

    // Checks to see whether any limit switches have been triggered
    bool limitSwitchTriggered() const;

    void lockAxis(unsigned axisNumber);
    void unlockAxis(unsigned axisNumber);
    bool isAxisLocked(unsigned axisNumber) const;

    RotationDirection getChuckRotationDirection() const;

    const IConfigReader& config() const;

private:
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
    std::size_t m_threadPitchIndex { 0 };
    std::string m_generalStatus { "Press F1 for help" };
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
    double m_stepOver { 0.0 };
    float m_taperPreviousXSpeed { 40.f };
    // Stores the current function displayed on the screen:
    Mode m_currentDisplayMode { Mode::None };
    // Stores current function, i.e. whether tapering or threading is on
    // we use the same enum class as "mode"
    Mode m_enabledFunction { Mode::None };
    KeyMode m_keyMode { KeyMode::None };
    // Used to store position to return to after retract:
    long m_xOldPosition;
    bool m_axis2Retracted { false };
    float m_previousAxis2Speed { 40.f };
    bool m_fastRetracting { false };

    bool m_axis1WasRunning { false };
    bool m_axis2WasRunning { false };
    bool m_spindleWasRunning { false };

    XDirection m_xRetractionDirection { XDirection::Outwards };
    // Once the user has set the x position once then we use
    // the status bar to display the effective diameter
    bool m_xDiameterSet { false };
    double m_axis1LastRelativeMove { 0.0 };
    double m_axis2LastRelativeMove { 0.0 };
    Axis m_lastRelativeMoveAxis;

    // "Breadcrumb" trail of positions for axes:
    std::stack<double> m_axis1PreviousPositions;
    std::stack<double> m_axis2PreviousPositions;
    MultiPassStage m_multiPassStage { MultiPassStage::NotStarted };

    std::set<unsigned> m_axisLocks;
};

} // end namespace
