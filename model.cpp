#include "model.h"
#include "keycodes.h"
#include "log.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <sstream>

#include <fmt/format.h>

namespace {

// Sets up monitoring of a motor driver's alarm output, if the config file
// specifies one for this axis ("Axis1AlarmPin" etc). Without it, faults can't
// be detected, and the motor behaves as before
// Optionally pins a motor's thread to a CPU core, and keeps it spinning when
// idle (see lc.cfg)
void configureMotorThread(
    mgo::StepperMotor& motor,
    const mgo::IConfigReader& config,
    const std::string& axisPrefix)
{
    if (const std::string core = config.read(axisPrefix + "CpuCore", ""); !core.empty()) {
        if (motor.setCpuCore(std::stoi(core))) {
            MGOLOG(axisPrefix << " motor thread running on CPU core " << core);
        } else {
            MGOLOG(
                "*** Warning *** couldn't run " << axisPrefix << " motor thread on CPU core "
                                                << core);
        }
    }
    motor.setSpinWhenIdle(config.readBool("MotorThreadsSpinWhenIdle", false));
}

void configureAlarm(
    mgo::StepperMotor& motor,
    const mgo::IConfigReader& config,
    const std::string& axisPrefix)
{
    const std::string pin = config.read(axisPrefix + "AlarmPin", "");
    if (pin.empty()) {
        return;
    }
    std::string level = config.read(axisPrefix + "AlarmActiveLevel", "low");
    std::transform(level.begin(), level.end(), level.begin(), ::tolower);
    const auto activeLevel = level == "high" ? mgo::PinState::high : mgo::PinState::low;
    motor.setAlarmPin(std::stoi(pin), activeLevel);
    MGOLOG(axisPrefix << " monitoring driver alarm on GPIO " << pin << ", active " << level);
}

// Sets up a limit switch input on a motor, if the config file specifies one
// ("LimitSwitchPin"). All the motors share the one pin, so any switch stops
// them all. Without it, there are no limit switches
void configureLimitSwitch(mgo::StepperMotor& motor, const mgo::IConfigReader& config)
{
    const std::string pin = config.read("LimitSwitchPin", "");
    if (pin.empty()) {
        return;
    }
    std::string level = config.read("LimitSwitchActiveLevel", "high");
    std::transform(level.begin(), level.end(), level.begin(), ::tolower);
    const auto activeLevel = level == "low" ? mgo::PinState::low : mgo::PinState::high;
    motor.setLimitPin(std::stoi(pin), activeLevel);
}

std::string convertToString(double number, int decimalPlaces)
{
    // If the number appears to be an integer, dispense with all
    // decimal places but one
    if (decimalPlaces > 1 && std::abs(number - static_cast<int>(number)) < 0.00001) {
        decimalPlaces = 1;
    }
    std::ostringstream oss;
    oss.precision(decimalPlaces);
    oss << std::fixed << number;
    return oss.str();
}

[[maybe_unused]] // Currently just here for debugging
std::string translate_mode(mgo::Mode mode)
{
    switch (mode) {
        case mgo::Mode::None:
            return "None";
        case mgo::Mode::Setup:
            return "Setup";
        case mgo::Mode::Threading:
            return "Threading";
        case mgo::Mode::Taper:
            return "Taper";
        case mgo::Mode::Radius:
            return "Radius";
        case mgo::Mode::MultiPass:
            return "MultiPass";
        case mgo::Mode::Knurling:
            return "Knurling";
        default:
            // As this function is just used for debugging there's
            // no need for an assert here.
            MGOLOG("Missing mode in translate_mode");
            return "";
    }
}

// A threading pass won't start if the spindle is fast enough to need more
// than this fraction of Z's maximum speed, which leaves some margin for the
// spindle speeding up during the pass (e.g. as the cutting load comes off)
constexpr double THREADING_START_SPEED_FRACTION = 0.8;

// Below this, the spindle is regarded as stopped
constexpr float SPINDLE_STOPPED_RPM = 30.f;
// The knurling dialog's first suggestion for the number of starts: grooves
// about this far apart round the work, in mm (a medium knurl)
constexpr double KNURLING_DEFAULT_PITCH_MM = 1.0;
// Why threading (or knurling) passes stop, or won't start, once the encoder
// has lost pulses
std::string lostPulsesPasses(mgo::Mode mode)
{
    return mode == mgo::Mode::Knurling
        ? "Encoder lost pulses: further passes wouldn't follow this knurl. Choose cut knurling "
          "again for new work"
        : "Encoder lost pulses: further passes wouldn't follow this thread. Choose the thread "
          "again for new work";
}
// How far back spindleTurningSign() looks (at least), how far its count must
// have moved to tell, and how long the samples are kept
constexpr auto SPINDLE_DIRECTION_WINDOW = std::chrono::milliseconds(80);
constexpr int64_t SPINDLE_DIRECTION_MIN_PULSES = 3;
constexpr auto SPINDLE_SAMPLES_KEPT = std::chrono::milliseconds(500);
// A pass waiting for its start angle is stopped if the spindle turns this
// many revolutions away from it (e.g. it's turning the other way): it would
// otherwise wait for ever. (The start is never more than two revolutions
// ahead)
constexpr double PASS_WRONG_WAY_REVOLUTIONS = 3.0;
// ...once it has read below the minimum (see Model::minimumSpindleRpm()) for
// this long
constexpr auto SPINDLE_STOPPED_CONFIRM_TIME = std::chrono::milliseconds(300);

// The warning when the spindle is too fast to start a pass locked to it
std::string spindleTooFast(mgo::Mode mode, double maxRpm)
{
    return mode == mgo::Mode::Knurling
        ? fmt::format("Spindle too fast for this knurl: knurl at {:.0f} rpm or less", maxRpm)
        : fmt::format("Spindle too fast for this pitch: thread at {:.0f} rpm or less", maxRpm);
}

} // anonymous namepace

namespace mgo {

ThreadingStopAction
threadingStopAction(bool arrived, bool retractedAtStart, bool retractedNow, bool spindleTurning)
{
    if (retractedNow != retractedAtStart) {
        // The user retracted (or unretracted) X during the move; leave it be
        return ThreadingStopAction::None;
    }
    if (arrived) {
        // A cut (made with the tool in) ends by retracting; a return (made
        // with it out) ends by bringing it back in for the next cut
        return retractedAtStart ? ThreadingStopAction::RetractIn : ThreadingStopAction::RetractOut;
    }
    // Stopped short. Get the tool out of the groove if the spindle is still
    // turning, but never bring it in, as we don't know where Z is in the cycle
    if (!retractedNow && spindleTurning) {
        return ThreadingStopAction::RetractOut;
    }
    return ThreadingStopAction::None;
}

void Model::initialise()
{
    double axis1ConversionFactor = m_config.readDouble("Axis1ConversionNumerator", -1.0)
        / m_config.readDouble("Axis1ConversionDivisor", 1'000.0);
    double maxAxis1Rpm = m_config.readDouble("Axis1MaxMotorRpm", 1'000.0);
    long axis1StepsPerRevolution = m_config.readLong("Axis1StepsPerRev", 1'000);
    bool usingMockLinearScale = false;
#ifdef FAKE
    usingMockLinearScale = true;
#endif
    m_axis1Motor = std::make_unique<mgo::StepperMotor>(
        m_gpio,
        m_config.readLong("Axis1GpioStepPin", 8),
        m_config.readLong("Axis1GpioReversePin", 7),
        m_config.readLong("Axis1GpioEnablePin", 0),
        axis1StepsPerRevolution,
        axis1ConversionFactor,
        maxAxis1Rpm,
        m_config.readDouble("Axis1RampingSpeed", 100.0),
        usingMockLinearScale,
        m_config.readLong("LinearScaleAxis1StepsPerMM", 200));
    if (!m_axis1Motor->isRunningRealTimeScheduled()) {
        MGOLOG("*** Warning *** axis1 steppermotor thread not running real-time");
    }
    configureAlarm(*m_axis1Motor, m_config, "Axis1");
    configureLimitSwitch(*m_axis1Motor, m_config);
    configureMotorThread(*m_axis1Motor, m_config, "Axis1");

    double axis2ConversionFactor = m_config.readDouble("Axis2ConversionNumerator", -1.0)
        / m_config.readDouble("Axis2ConversionDivisor", 1'000.0);
    double maxAxis2Rpm = m_config.readDouble("Axis2MaxMotorRpm", 1'000.0);
    long axis2StepsPerRevolution = m_config.readLong("Axis2StepsPerRev", 800);
    m_axis2Motor = std::make_unique<mgo::StepperMotor>(
        m_gpio,
        m_config.readLong("Axis2GpioStepPin", 20),
        m_config.readLong("Axis2GpioReversePin", 21),
        m_config.readLong("Axis2GpioEnablePin", 0),
        axis2StepsPerRevolution,
        axis2ConversionFactor,
        maxAxis2Rpm,
        m_config.readDouble("Axis2RampingSpeed", 100.0));
    if (!m_axis2Motor->isRunningRealTimeScheduled()) {
        MGOLOG("*** Warning *** axis2 steppermotor thread not running real-time");
    }
    configureAlarm(*m_axis2Motor, m_config, "Axis2");
    configureLimitSwitch(*m_axis2Motor, m_config);
    if (const std::string pin = m_config.read("LimitSwitchPin", ""); !pin.empty()) {
        MGOLOG(
            fmt::format(
                "Limit switches on GPIO {}, active {}",
                pin,
                m_config.read("LimitSwitchActiveLevel", "high")));
    }
    configureMotorThread(*m_axis2Motor, m_config, "Axis2");

    m_rotaryEncoder = std::make_unique<mgo::RotaryEncoder>(
        m_gpio,
        m_config.readLong("RotaryEncoderGpioPinA", 23),
        m_config.readLong("RotaryEncoderGpioPinB", 24),
        m_config.readLong("RotaryEncoderPulsesPerRev", 2'000),
        m_config.readDouble("RotaryEncoderGearingNumerator", 35.0)
            / m_config.readDouble("RotaryEncoderGearingDivisor", 30.0));
    {
        // Threading counts whole revolutions in pulses, so needs a whole number
        const double pulsesPerRev = m_config.readLong("RotaryEncoderPulsesPerRev", 2'000)
            * m_config.readDouble("RotaryEncoderGearingNumerator", 35.0)
            / m_config.readDouble("RotaryEncoderGearingDivisor", 30.0);
        if (std::abs(pulsesPerRev - std::round(pulsesPerRev)) > 0.001) {
            MGOLOG(
                fmt::format(
                    "*** Warning *** {} rotary encoder pulses per spindle revolution isn't a whole "
                    "number, so threading passes won't all start at the same angle",
                    pulsesPerRev));
        }
    }

    {
        // Which way X retracts to start with (the menu's retraction
        // direction changes it, as does choosing a male or female thread)
        const std::string direction = m_config.read("Axis2RetractionDirection", "outwards");
        if (direction == "inwards") {
            m_xRetractionDirection = XDirection::Inwards;
        } else if (direction != "outwards") {
            MGOLOG(
                fmt::format(
                    "*** Warning *** Axis2RetractionDirection should be outwards or inwards, not "
                    "\"{}\": using outwards",
                    direction));
        }
        // How far it retracts (in mm of X travel, not on the diameter). Not
        // used by a multi-pass, which retracts to just clear of the work (see
        // multiPassRetractTarget())
        const double distance = m_config.readDouble("Axis2RetractionDistance", 2.0);
        if (distance > 0.0) {
            m_axis2RetractionDistance = distance;
        } else {
            MGOLOG(
                fmt::format(
                    "*** Warning *** Axis2RetractionDistance should be more than zero, not {}: "
                    "using {}",
                    distance,
                    m_axis2RetractionDistance));
        }
    }
    {
        const std::string threadsFile = m_config.read("ThreadsFile", "threadpitches.cfg");
        std::vector<std::string> errors;
        m_threads = readThreadsFile(threadsFile, errors);
        for (const auto& e : errors) {
            MGOLOG("*** Warning *** " << threadsFile << ": " << e);
        }
        std::size_t count = 0;
        for (const auto& c : m_threads) {
            count += c.threads.size();
        }
        MGOLOG(fmt::format("{} thread sizes read from {}", count, threadsFile));
    }

    m_linearScaleAxis1 = std::make_unique<mgo::LinearScale>(
        m_gpio,
        m_config.readLong("LinearScaleAxis1GpioPinA", 5),
        m_config.readLong("LinearScaleAxis1GpioPinB", 6),
        m_config.readLong("LinearScaleAxis1StepsPerMM", 200));

    // We need to ensure that the motors are in a known position with regard to
    // backlash - which means moving them initially by the amount of
    // configured backlash compensation to ensure any backlash is taken up
    // This initial movement will be small but this could cause an issue if
    // the tool is against work already - maybe TODO something here?
    unsigned long zBacklashCompensation = m_config.readLong("Axis1BacklashCompensationSteps", 0);
    unsigned long xBacklashCompensation = m_config.readLong("Axis2BacklashCompensationSteps", 0);
    axis1GoToStep(zBacklashCompensation);
    m_axis1Motor->setBacklashCompensation(zBacklashCompensation, zBacklashCompensation);
    m_axis2Motor->goToStep(xBacklashCompensation);
    m_axis2Motor->setBacklashCompensation(xBacklashCompensation, xBacklashCompensation);
    m_axis1Motor->wait();
    m_axis2Motor->wait();
    // re-zero after that:
    m_axis1Motor->zeroPosition();
    m_axis2Motor->zeroPosition();

    axis1SaveBreadcrumbPosition();
    axis2SaveBreadcrumbPosition();
}

void Model::checkStatus()
{
    checkMotorFaults();

    float chuckRpm = m_rotaryEncoder->getRpm();

    checkLimitSwitch();
    checkLockRefusal();

    recordSpindleSample();
    checkSpindleStopped(chuckRpm);
    checkEncoderLostPulses();

    if (const auto diameter = getDiameterMm()) {
        m_generalStatus = fmt::format(
            "Diameter: {: .{}f} {}",
            toDisplayUnits(2, *diameter),
            axisDecimals(2),
            axisUnitName(2));
    }

    // While a motion sequence (e.g. a nudge) is running, only the checks
    // above (faults, limit switch, spindle) apply. Everything below reacts to
    // motors starting and stopping, which would misread the sequence's moves:
    // e.g. the end of a taper's backlash take-up as the end of a fast return
    if (m_motionSequence.advance()) {
        return;
    }

    if (spindleSynchronised()) {
        threadingCheckSpeed();
        threadingCheckMoveEnded();
    }
    if (!m_axis2Motor->isRunning()) {
        m_axis2Status = "stopped";
    }
    if (!m_axis1Motor->isRunning()) {
        m_axis1Status = "stopped";
        if (m_axis1WasRunning) {
            // We see that axis1 has stopped. We save the position
            // in case the user wants to return to it without
            // explicitly having saved it.
            axis1SaveBreadcrumbPosition();
        }
        if (m_axis1FastReturning || m_axis1RapidInProgress) {
            m_axis1Motor->setSpeed(m_previousAxis1Speed);
            m_axis1FastReturning = false;
            m_axis1RapidInProgress = false;
        }
        // X has caught up with where Z stopped (see below) once it's been
        // seen stopped on two updates in a row: it can be stopped for a
        // moment between its synchronised moves while still behind
        if (m_synchronisedCutEnding && m_axis2Motor->isRunning()) {
            m_synchronisedCutXStoppedOnce = false;
        } else if (m_synchronisedCutEnding && !m_synchronisedCutXStoppedOnce) {
            m_synchronisedCutXStoppedOnce = true;
        } else if (m_synchronisedCutEnding) {
            // Turn off synchronisation *before* setting the speed, otherwise
            // the motor thread may overwrite our speed with a synchronised one
            m_synchronisedCutEnding = false;
            axis2SynchroniseOff();
            m_axis2Motor->setSpeed(m_previousAxis2Speed);
            if (m_multiPassXFollowing) {
                m_multiPassXFollowing = false;
                multiPassXFollowEnded();
            }
        }
        if ((m_mode == Mode::Taper || m_multiPassXFollowing) && m_axis1WasRunning) {
            // The end of a taper cut. X follows Z in small updates, so may
            // still be a few steps behind: leave it synchronised until it's
            // stopped too (on a later update, by when it's had Z's final
            // position), so it ends exactly on the taper
            m_synchronisedCutEnding = true;
            m_synchronisedCutXStoppedOnce = false;
        }
        // (Not during a multi-pass, which sets the speeds itself: the cut
        // speed must be kept from one pass to the next)
        if (m_axis1WasRunning && !m_multiPass.inProgress()
            && m_axis1Motor->getSpeed() > m_config.readDouble("Axis1SpeedResetAbove", 100.0)) {
            // We don't allow faster speeds to "stick" to avoid accidental
            // fast motion after a long fast movement
            m_axis1Motor->setSpeed(m_config.readDouble("Axis1SpeedResetTo", 40.0));
        }
        m_axis1WasRunning = false;
    } else {
        m_axis1WasRunning = true;
    }

    if (!m_axis2Motor->isRunning()) {
        m_axis2Status = "stopped";
        if (m_axis2WasRunning) {
            axis2SaveBreadcrumbPosition();
        }
        m_multiPassXSettling = false;
        if (m_axis2RetractMoveInProgress) {
            // (If this shows the target was reached, but the tool didn't move
            // that far, the motor stalled. If it stopped short, it was stopped)
            MGOLOG(
                fmt::format(
                    "X retract/unretract ended at step {}", m_axis2Motor->getCurrentStep()));
            m_axis2RetractMoveInProgress = false;
        }
        if (m_fastRetracting) {
            m_axis2Motor->setSpeed(m_previousAxis2Speed);
            m_fastRetracting = false;
            m_axis2Retracted = false;
        }
        if (m_axis2FastReturning || m_axis2RapidInProgress) {
            m_axis2Motor->setSpeed(m_previousAxis2Speed);
            m_axis2FastReturning = false;
            m_axis2RapidInProgress = false;
        }
        if (!(m_mode == Mode::Taper) && m_axis2WasRunning && !m_multiPass.inProgress()
            && m_axis2Motor->getSpeed() >= m_config.readDouble("Axis2SpeedResetAbove", 80.0)) {
            // We don't allow faster speeds to "stick" to avoid accidental
            // fast motion after a long fast movement
            m_axis2Motor->setSpeed(m_config.readDouble("Axis2SpeedResetTo", 20.0));
        }
        m_axis2WasRunning = false;
    } else {
        m_axis2WasRunning = true;
    }
    if (m_mode == Mode::Taper) {
        m_axis2Status = "synchronised";
    }

    // (Last, once the above has dealt with any move that's ended)
    if (m_multiPass.inProgress()) {
        if (spindleSynchronised() && m_threadingPositionLost) {
            // Every pass from now on would cut a different thread. (Once
            // clear of the work: stopped part way, the tool would cut a ring)
            m_multiPass.stopAfterPass(lostPulsesPasses(m_mode));
        }
        if (m_springPasses && m_multiPass.state() == MultiPass::State::Cutting) {
            // (The depth to move back to, if stopped: it may have been nudged)
            m_springPassX = getAxis2Position();
        }
        m_multiPass.update();
        if (!m_multiPass.inProgress() && !m_multiPass.message().empty()) {
            // It stopped, rather than finished. (If a threading pass stopped
            // short, e.g. Z couldn't keep up with the spindle, its warning
            // says why)
            const std::string reason = spindleSynchronised() && m_threadingWarningSticky
                ? m_threadingWarning
                : m_multiPass.message();
            MGOLOG("Multi-pass stopped: " << reason);
            stopAllMotors();
            m_warning = "Multi-pass stopped: " + reason;
        } else if (m_multiPass.state() == MultiPass::State::Finished) {
            MGOLOG("Multi-pass finished");
            multiPassFinished();
        }
        if (!m_multiPass.inProgress() && m_mode == Mode::Threading) {
            // (A threading multi-pass's returns turn it on)
            m_axis1Motor->enableRamping(false);
        }
        if (!m_multiPass.inProgress() && m_mode == Mode::Knurling) {
            // (Its passes turn it off)
            m_axis1Motor->enableRamping(true);
        }
    }
}

void Model::checkEncoderLostPulses()
{
    const uint32_t events = m_rotaryEncoder->getLostPulseEvents();
    if (events == m_encoderLostPulseEventsSeen) {
        return;
    }
    m_encoderLostPulseEventsSeen = events;
    MGOLOG(
        fmt::format(
            "*** Rotary encoder lost pulses (none for {} us): spindle position is now wrong",
            m_rotaryEncoder->getLastLostPulseGapMicroseconds()));
    if (spindleSynchronised()) {
        // Every pass from now on would start at a different point of the
        // spindle's rotation from the passes before, i.e. cut a new thread
        m_threadingPositionLost = true;
    }
}

void Model::recordSpindleSample()
{
    const auto now = std::chrono::steady_clock::now();
    m_spindleSamples.push_back({ now, m_rotaryEncoder->getSpindlePulses() });
    while (now - m_spindleSamples.front().time > SPINDLE_SAMPLES_KEPT) {
        m_spindleSamples.pop_front();
    }
}

std::optional<int> Model::spindleTurningSign() const
{
    const auto now = std::chrono::steady_clock::now();
    const int64_t pulses = m_rotaryEncoder->getSpindlePulses();
    // (From the newest sample that's old enough)
    for (auto it = m_spindleSamples.rbegin(); it != m_spindleSamples.rend(); ++it) {
        if (now - it->time >= SPINDLE_DIRECTION_WINDOW) {
            const int64_t moved = pulses - it->pulses;
            if (std::abs(moved) < SPINDLE_DIRECTION_MIN_PULSES) {
                return std::nullopt;
            }
            return moved > 0 ? 1 : -1;
        }
    }
    return std::nullopt;
}

void Model::checkSpindleStopped(float chuckRpm)
{
    // If the chuck has stopped, we stop X/Z motors as a safety feature, just
    // in case the motor has stalled or the operator has turned it off because
    // of some issue. This won't stop the motors being started again even if
    // the chuck isn't moving.
    // The encoder can momentarily read zero (e.g. a gap in its callbacks), so
    // it must read stopped for a while before we act; a real spindle takes
    // longer than this to stop anyway
    using namespace std::chrono;
    const auto now = steady_clock::now();
    if (chuckRpm >= minimumSpindleRpm()) {
        if (m_spindleLowSince) {
            MGOLOG(
                fmt::format(
                    "Spindle reading recovered ({:.0f} rpm) after {} ms",
                    chuckRpm,
                    duration_cast<milliseconds>(now - *m_spindleLowSince).count()));
            m_spindleLowSince.reset();
        }
        m_spindleWasRunning = true;
        return;
    }
    if (!m_spindleWasRunning) {
        return;
    }
    if (!m_spindleLowSince) {
        m_spindleLowSince = now;
        MGOLOG(
            fmt::format(
                "Spindle reads {:.0f} rpm (last encoder pulse {} us ago)",
                chuckRpm,
                m_rotaryEncoder->microsecondsSinceLastPulse()));
        return;
    }
    if (now - *m_spindleLowSince < SPINDLE_STOPPED_CONFIRM_TIME) {
        return;
    }
    m_spindleLowSince.reset();
    m_spindleWasRunning = false;
#ifndef FAKE
    MGOLOG("Spindle stopped: stopping all motors");
    // (Saying so, if it stopped anything: otherwise, e.g. a multi-pass just
    // shows that it stopped)
    const bool stoppingSomething = m_multiPass.inProgress() || m_multiPassMovingToStart
        || m_axis1Motor->isRunning() || m_axis2Motor->isRunning();
    stopAllMotors();
    if (stoppingSomething) {
        m_warning = "The spindle stopped, so all motors were stopped";
    }
#endif
}

void Model::checkMotorFaults()
{
    // (The motor itself stops as soon as its driver's alarm is detected, and
    // won't move again until the fault is cleared; here we deal with the rest)
    bool newFault = false;
    if (m_axis1Motor->isFaulted() && !m_axis1Faulted) {
        m_axis1Faulted = true;
        newFault = true;
        clearWorkEnd(); // (the position's lost)
        MGOLOG("*** Axis1 motor fault: driver alarm (e.g. stalled); position lost");
    }
    if (m_axis2Motor->isFaulted() && !m_axis2Faulted) {
        m_axis2Faulted = true;
        newFault = true;
        m_featureX.reset(); // (the position's lost)
        MGOLOG("*** Axis2 motor fault: driver alarm (e.g. stalled); position lost");
    }
    if (newFault) {
        // Stop everything: the other axis might otherwise carry on (e.g.
        // synchronised in a taper, or as part of a multi-pass sequence), and
        // end any automated function, as its positions are now meaningless
        stopAllMotors();
        if (m_mode != Mode::None) {
            changeMode(Mode::None);
        }
    }
}

void Model::clearMotorFault(Axis axis)
{
    // Called when an axis is re-zeroed or its position set, which re-establishes
    // the position lost in a fault. The motor will only clear its fault if its
    // driver is no longer in alarm
    bool& faulted = axis == Axis::Axis1 ? m_axis1Faulted : m_axis2Faulted;
    StepperMotor& motor = axis == Axis::Axis1 ? *m_axis1Motor : *m_axis2Motor;
    if (!faulted) {
        return;
    }
    const std::string label
        = axis == Axis::Axis1 ? m_config.read("Axis1Label", "Z") : m_config.read("Axis2Label", "X");
    if (motor.clearFault()) {
        faulted = false;
        m_warning = "";
        MGOLOG(label << " motor fault cleared");
    } else {
        m_warning = label + " driver is still in alarm: clear it, then set the position again";
    }
}

std::string Model::getFaultMessage() const
{
    std::string message;
    const auto add = [&message](const std::string& label) {
        if (!message.empty()) {
            message += "  ";
        }
        message += fmt::format(
            "{} MOTOR FAULT: position lost. Clear the driver alarm, then re-zero {}", label, label);
    };
    if (m_axis1Faulted) {
        add(m_config.read("Axis1Label", "Z"));
    }
    if (m_axis2Faulted) {
        add(m_config.read("Axis2Label", "X"));
    }
    if (limitSwitchTripped()) {
        if (!message.empty()) {
            message += "  ";
        }
        message += "LIMIT SWITCH: all motors stopped. Clear it (menu: System), then move off it";
    } else if (limitSwitchPressed()) {
        if (!message.empty()) {
            message += "  ";
        }
        message += "LIMIT SWITCH still pressed: move off it carefully";
    }
    return message;
}

void Model::changeMode(Mode mode)
{
    if (mode != Mode::None || m_multiPass.inProgress() || m_multiPassMovingToStart) {
        // (Leaving multi-pass mode part way through stops it, too, as it
        // does a move to its start)
        stopAllMotors();
    }
    axis2SynchroniseOff();
    // (X's speed on leaving a taper is restored below, rather than as at the
    // end of a cut)
    m_synchronisedCutEnding = false;
    if (mode == Mode::Threading) {
        // Z must not ramp, as its speed is locked to the spindle's. X isn't
        // synchronised when threading, and does need to ramp: it's an open-loop
        // stepper, and starting a retract at full speed can stall it
        m_axis1Motor->enableRamping(false);
        m_axis2Motor->enableRamping(true);
    } else if (mode == Mode::Taper) {
        // X is synchronised with Z, so neither can ramp
        m_axis1Motor->enableRamping(false);
        m_axis2Motor->enableRamping(false);
    } else {
        m_axis1Motor->enableRamping(true);
        m_axis2Motor->enableRamping(true);
    }
    if (m_mode == Mode::Taper && mode != Mode::Taper) {
        m_axis2Motor->setSpeed(m_taperPreviousXSpeed);
    }
    if (spindleSynchronised() && mode != m_mode) {
        // Z's speed was set for threading passes: back to something sane
        m_axis1Motor->setSpeed(m_config.readDouble("Axis1SpeedPreset2", 40.0));
    }
    m_warning = "";
    m_threadingMoveInProgress = false;
    m_threadingPositionLost = false;
    // (Set up afresh in the mode, if wanted: Enter goes back to the slot)
    m_threadingPassesSet = false;
    m_springPassX.reset();
    m_springPasses = false;
    m_passesSpindleSign.reset();
    // (Set up afresh on entering the mode)
    m_knurlLeadMm = 0.0;
    m_gearSet = false;
    m_radiusSet = false;
    setThreadingWarning("", false);
    m_mode = mode;
    m_input = "";

    if (mode == Mode::Taper) {
        m_taperPreviousXSpeed = m_axis2Motor->getSpeed();
        if (m_taperAngle != 0.0) {
            m_input = convertToString(m_taperAngle, 4);
        }
    }
}

void Model::stopAllMotors()
{
    m_axis1Motor->stop();
    m_axis2Motor->stop();
    m_axis1Motor->wait();
    m_axis2Motor->wait();
    // (After stopping, so a sequence's end, e.g. restoring a nudge's speed,
    // doesn't race with a move it started)
    m_motionSequence.cancel();
    // Otherwise X, synchronised to Z (e.g. in a taper), would carry on
    // following it. (Its speed is still restored when the cut's end is seen)
    axis2SynchroniseOff();
    m_multiPassXSettling = false;
    if (m_multiPassXFollowing) {
        // (Its end won't be seen if stopped while taking up the backlash)
        m_multiPassXFollowing = false;
        m_synchronisedCutEnding = false;
        m_axis2Motor->setSpeed(m_previousAxis2Speed);
    }
    m_multiPass.stop("stopped");
    if (m_mode == Mode::Threading) {
        // (In case a threading multi-pass was stopped during a return, which
        // turns it on)
        m_axis1Motor->enableRamping(false);
    }
    if (m_mode == Mode::Knurling) {
        // (In case a pass was stopped, which turns it off)
        m_axis1Motor->enableRamping(true);
    }
    m_axis1Status = "stopped";
    m_axis2Status = "stopped";
#ifdef FAKE
    m_gpio.scaleStop();
#endif
}

MotionSequence::Step Model::takeUpZBacklashStep(ZDirection direction)
{
    return { [this, direction]() {
                axis1Stop();
                if (direction == ZDirection::Right) {
                    m_axis1Motor->goToStep(getAxis1MotorCurrentStep() - 1);
                } else if (direction == ZDirection::Left) {
                    m_axis1Motor->goToStep(getAxis1MotorCurrentStep() + 1);
                }
            },
             [this]() { return !m_axis1Motor->isRunning(); } };
}

MotionSequence::Step Model::takeUpXBacklashStep(int stepAdd)
{
    // As this is done just before the Z motor starts moving, we take up any
    // backlash in X first (making sure X isn't already running)
    return { [this, stepAdd]() {
                axis2SynchroniseOff();
                m_axis2Motor->stop();
                m_axis2Motor->setSpeed(100.0);
                m_axis2Motor->goToStep(m_axis2Motor->getCurrentStep() + stepAdd);
            },
             [this]() { return !m_axis2Motor->isRunning(); } };
}

void Model::synchroniseXForTaper(double ratio)
{
    m_synchronisedCutEnding = false; // (a new cut)
    m_axis2Motor->synchroniseOn(m_axis1Motor.get(), [ratio](double zPosDelta, double) -> double {
        return zPosDelta * ratio;
    });
}

double Model::axis2MaxSpeed() const
{
    // (As threadingMaxZSpeed(), with the steps per revolution as initialise())
    return m_axis2Motor->getMaxRpm() * m_config.readLong("Axis2StepsPerRev", 800)
        * std::abs(m_axis2Motor->getConversionFactor());
}

std::vector<MotionSequence::Step> Model::threadingPassSteps(long step, double revolutionOffset)
{
    const auto move
        = [this, step, revolutionOffset]() { axis1StartThreadingPass(step, revolutionOffset); };
    if (step == getAxis1MotorCurrentStep()) {
        return { { move, {} } };
    }
    // A threading pass follows the spindle from the moment it starts, so Z's
    // backlash must be taken up first. Otherwise, when Z reverses, the
    // spindle moves on while Z takes it up: Z starts late, and (if the
    // backlash is more than half the pitch) falls so far behind that the
    // pass is stopped
    // (At a set speed: Z's speed is whatever the last pass needed)
    auto takeUp = takeUpZBacklashStep(
        step < getAxis1MotorCurrentStep() ? ZDirection::Right : ZDirection::Left);
    takeUp.action = [this, action = std::move(takeUp.action)]() {
        m_axis1Motor->setSpeed(m_axis1Motor->getMaxRpm() / 10.0);
        action();
    };
    return { std::move(takeUp), { move, {} } };
}

std::vector<MotionSequence::Step> Model::axis1MoveSteps(long step)
{
    if (m_mode == Mode::Threading) {
        return threadingPassSteps(step, 0.0);
    }
    const auto move = [this, step]() { m_axis1Motor->goToStep(step); };
    if (m_mode == Mode::Taper) {
        // (Plus any correction for a taper the machine cuts itself)
        return xFollowsZSteps(
            step, std::tan((m_taperAngle + taperCorrection()) * DEG_TO_RAD), move);
    }
    return { { move, {} } };
}

std::vector<MotionSequence::Step>
Model::xFollowsZSteps(long step, double ratio, std::function<void()> move)
{
    const ZDirection direction
        = step < getAxis1MotorCurrentStep() ? ZDirection::Right : ZDirection::Left;
    // X's backlash is taken up the way it's about to move. X moves ratio
    // times as far as Z in mm (see synchroniseXForTaper()), so which way that
    // is in X's steps depends on both axes' conversion factors, which may
    // have either sign
    const double zMm = (step - getAxis1MotorCurrentStep()) * m_axis1Motor->getConversionFactor();
    const double xSteps = ratio * zMm / m_axis2Motor->getConversionFactor();
    const int xStepAdd = xSteps > 0.0 ? 1 : -1;
    return { takeUpZBacklashStep(direction),
             takeUpXBacklashStep(xStepAdd),
             { [this, ratio, move = std::move(move)]() {
                  synchroniseXForTaper(ratio);
                  move();
              },
               {} } };
}

double Model::threadingMaxZSpeed() const
{
    // In mm/min, as for StepperMotor::setSpeed()
    return m_axis1Motor->getMaxRpm() * m_config.readLong("Axis1StepsPerRev", 1'000)
        * std::abs(m_axis1Motor->getConversionFactor());
}

double Model::spindlePassSpeedLimit() const
{
    return threadingMaxZSpeed() * THREADING_START_SPEED_FRACTION;
}

float Model::minimumSpindleRpm() const
{
    return m_mode == Mode::Knurling ? KNURLING_MIN_SPINDLE_RPM : SPINDLE_STOPPED_RPM;
}

bool Model::spindleSynchronised() const
{
    return m_mode == Mode::Threading || m_mode == Mode::Knurling;
}

std::optional<double> Model::spindleLeadMm() const
{
    if (m_mode == Mode::Knurling) {
        return getKnurlLeadMm();
    }
    const Thread* thread = getThread();
    return thread ? std::optional(thread->pitchMm) : std::nullopt;
}

void Model::setThreadingWarning(const std::string& warning, bool sticky)
{
    m_threadingWarning = warning;
    m_threadingWarningSticky = sticky && !warning.empty();
}

void Model::threadingCheckSpeed()
{
    // Z follows the spindle's position (see axis1StartThreadingPass()), so the
    // spindle's speed doesn't set Z's: it's only used here for warnings, and
    // to show the speed Z moves at
    const auto lead = spindleLeadMm();
    if (!lead) {
        return;
    }
    const double pitch = *lead;
    const float rpm = m_rotaryEncoder->getRpm();
    const double speed = pitch * rpm;
    const double startLimit = spindlePassSpeedLimit();
    if (rpm >= minimumSpindleRpm()) {
        m_threadingZSpeed = speed;
    }
    if (m_axis1Motor->isFollowing() && m_threadingTarget) {
        // If the spindle has turned well away from the pass's start angle,
        // rather than towards it (e.g. it's turning the other way), the pass
        // would wait for ever
        const double towardsStart = (m_rotaryEncoder->getSpindlePosition() - m_threadingStartPulse)
            * m_threadingSpindleSign;
        if (towardsStart
            < -PASS_WRONG_WAY_REVOLUTIONS * m_rotaryEncoder->getPulsesPerSpindleRev()) {
            m_axis1Motor->stop();
            MGOLOG(
                fmt::format(
                    "Pass stopped: the spindle turned {:.0f} pulses away from its start (pulse "
                    "{:.0f})",
                    -towardsStart,
                    m_threadingStartPulse));
            setThreadingWarning(
                "The spindle turned away from the pass's start: pass stopped. Is it turning the "
                "right way?",
                true);
            return;
        }
        // If Z has fallen well behind (the spindle has sped up beyond what it
        // can follow), this pass is cutting a different thread: stop it
        const long lag = std::abs(m_threadingTarget() - m_axis1Motor->getCurrentStep());
        if (lag > m_threadingMaxLagSteps) {
            m_axis1Motor->stop();
            MGOLOG(
                fmt::format(
                    "Threading pass stopped: Z {} steps behind the spindle (at {:.0f} rpm)",
                    lag,
                    rpm));
            setThreadingWarning(
                fmt::format(
                    "Z couldn't keep up with the spindle ({:.0f} rpm): pass stopped. {} at "
                    "{:.0f} rpm or less",
                    rpm,
                    m_mode == Mode::Knurling ? "Knurl" : "Thread",
                    startLimit / pitch),
                true);
        }
        return;
    }
    if (!m_threadingWarningSticky) {
        if (!m_axis1Motor->isRunning() && speed > startLimit) {
            setThreadingWarning(spindleTooFast(m_mode, startLimit / pitch), false);
        } else {
            setThreadingWarning("", false);
        }
    }
}

Model::ThreadingPassError Model::threadingPassError() const
{
    const auto lead = spindleLeadMm();
    if (!lead) {
        return { m_mode == Mode::Knurling ? "Set up the knurling first" : "Choose a thread first",
                 true };
    }
    const double pitch = *lead;
    const float rpm = m_rotaryEncoder->getRpm();
    if (rpm < minimumSpindleRpm()) {
        return { m_mode == Mode::Knurling ? "Start the spindle before knurling"
                                          : "Start the spindle before threading",
                 true };
    }
    const double startLimit = spindlePassSpeedLimit();
    if (pitch * rpm > startLimit) {
        return { spindleTooFast(m_mode, startLimit / pitch), false };
    }
    const auto turning = spindleTurningSign();
    if (!turning) {
        // (Sticky, or it would be cleared before it's seen)
        return { "Can't yet tell which way the spindle is turning: try again", true };
    }
    if (m_multiPass.inProgress() && m_passesSpindleSign && *m_passesSpindleSign != *turning) {
        // (This pass would cut the other hand)
        return { "The spindle is turning the other way from the earlier passes", true };
    }
    return {};
}

bool Model::axis1StartThreadingPass(long endStep, double revolutionOffset)
{
    if (const auto error = threadingPassError(); !error.message.empty()) {
        MGOLOG("Threading pass not started: " << error.message);
        setThreadingWarning(error.message, error.sticky);
        return false;
    }
    const double pitch = *spindleLeadMm();
    const float rpm = m_rotaryEncoder->getRpm();
    const double nominalSpeed = pitch * rpm;
    const double maxSpeed = threadingMaxZSpeed();
    const long startStep = m_axis1Motor->getCurrentStep();
    if (endStep == startStep) {
        return false;
    }
    setThreadingWarning("", false);

    // Z is locked to the spindle's position, as if geared to it: Z's target is
    // the start, plus the pitch for every revolution since the pass started.
    // Every pass must start at the same point of the spindle's rotation, so
    // it starts at a whole number of revolutions of the encoder's count: the
    // next one, at least an eighth of a revolution away (so it can't have
    // passed by the time the motor thread first looks)
    const int64_t pulsesPerRev = m_rotaryEncoder->getPulsesPerSpindleRev();
    // (Known: see threadingPassError())
    const int spindleSign = *spindleTurningSign();
    if (m_multiPass.inProgress()) {
        m_passesSpindleSign = spindleSign;
    }
    const int64_t margin = pulsesPerRev / 8;
    const int64_t now = m_rotaryEncoder->getSpindlePulses();
    int64_t startPulse;
    if (spindleSign > 0) {
        const int64_t from = now + margin;
        startPulse = (from / pulsesPerRev + (from % pulsesPerRev > 0 ? 1 : 0)) * pulsesPerRev;
    } else {
        const int64_t from = now - margin;
        startPulse = (from / pulsesPerRev - (from % pulsesPerRev < 0 ? 1 : 0)) * pulsesPerRev;
    }
    // (Further round, in the way the spindle turns, so it's still ahead)
    startPulse += spindleSign * std::lround(revolutionOffset * static_cast<double>(pulsesPerRev));
    const double stepsPerRev = pitch / std::abs(m_axis1Motor->getConversionFactor());
    const double stepsPerPulse = stepsPerRev / static_cast<double>(pulsesPerRev);
    const int zSign = endStep > startStep ? 1 : -1;
    const RotaryEncoder* encoder = m_rotaryEncoder.get();
    m_threadingTarget = [encoder, startPulse, spindleSign, startStep, stepsPerPulse, zSign]() {
        const double pulses
            = (encoder->getSpindlePosition() - static_cast<double>(startPulse)) * spindleSign;
        if (pulses <= 0.0) {
            return startStep;
        }
        return startStep + zSign * std::lround(pulses * stepsPerPulse);
    };
    m_threadingMaxLagSteps = std::max(1L, std::lround(stepsPerRev / 4.0));
    m_threadingStartPulse = static_cast<double>(startPulse);
    m_threadingSpindleSign = spindleSign;
    // Z's speed only limits how fast it can follow: twice what it needs now
    // leaves room for the spindle speeding up
    m_axis1Motor->setSpeed(std::min(maxSpeed, 2.0 * nominalSpeed));
    m_axis1Motor->followTo(endStep, m_threadingTarget);
    if (!m_axis1Motor->isFollowing()) {
        return false;
    }
    m_threadingMoveInProgress = true;
    m_threadingMoveTarget = endStep;
    m_threadingMoveStartedRetracted = m_axis2Retracted;
    MGOLOG(
        fmt::format(
            "{} pass: Z from step {} to {} at {:.0f} rpm, from spindle pulse {} (now {}, "
            "turning {}), X {}",
            m_mode == Mode::Knurling ? "Knurling" : "Threading",
            startStep,
            endStep,
            rpm,
            startPulse,
            now,
            spindleSign > 0 ? "+" : "-",
            m_axis2Retracted ? "retracted" : "in"));
    return true;
}

void Model::threadingCheckMoveEnded()
{
    if (!m_threadingMoveInProgress || m_axis1Motor->isRunning()) {
        return;
    }
    m_threadingMoveInProgress = false;
    const bool arrived = m_axis1Motor->getCurrentStep() == m_threadingMoveTarget;
    MGOLOG(
        fmt::format(
            "Threading pass {} at Z step {}",
            arrived ? "finished" : "STOPPED SHORT",
            m_axis1Motor->getCurrentStep()));
    // (A multi-pass does its own retracting)
    if (!m_threadingAutoRetract || m_multiPass.inProgress()) {
        return;
    }
    const auto action = threadingStopAction(
        arrived, m_threadingMoveStartedRetracted, m_axis2Retracted, m_spindleWasRunning);
    if (action == ThreadingStopAction::None) {
        return;
    }
    if (m_axis2Motor->isRunning()) {
        MGOLOG("Threading: X was moving at the end of a pass, so wasn't retracted");
        setThreadingWarning(
            fmt::format(
                "{} was moving, so wasn't retracted automatically",
                m_config.read("Axis2Label", "X")),
            true);
        return;
    }
    // (axis2Retract() toggles, and the action always matches the current state)
    axis2Retract();
}

void Model::axis1GoToStep(long step)
{
    if (axis1MoveRefused()) {
        return;
    }
    // (Usually just the move itself, which starts at once. For a taper, the
    // backlash is taken up first, on later updates)
    m_motionSequence.start(axis1MoveSteps(step));
}

void Model::axis1GoToPosition(double pos)
{
    if (axis1MoveRefused()) {
        return;
    }
    // TODO - if the linear scale is enabled, we need to perhaps go to a step close to
    // the desired position, then start nudging while monitoring the linear scale
    // until we get to the desired position. This could be implemented in the motor
    // perhaps by supplying a callback that is called after each step is taken.
    axis1GoToStep(std::lround(pos / m_axis1Motor->getConversionFactor()));
    if (std::abs(pos) < 0.00001) {
        // Round to zero for display purposes, just to
        // avoid having "-0.000" displayed
        pos = 0.0;
    }
    m_axis1Status = fmt::format("Going to {:.3f}", pos);
}

void Model::axis1GoToOffset(double offset)
{
    if (axis1MoveRefused()) {
        return;
    }
    m_axis1LastRelativeMove = offset;
    m_lastRelativeMoveAxis = Axis::Axis1;
    axis1GoToPosition(m_axis1Motor->getPosition() + offset);
    m_axis1Status = fmt::format("To rel {}", offset);
}

void Model::axis1GoToPreviousPosition()
{
    if (axis1MoveRefused()) {
        return;
    }
    axis1Stop();
    while (!m_axis1PreviousPositions.empty()
           && (std::abs(m_axis1PreviousPositions.top() - m_axis1Motor->getPosition()) < 0.0001)) {
        if (m_axis1PreviousPositions.size() == 1) {
            return;
        }
        m_axis1PreviousPositions.pop();
    }
    if (m_axis1PreviousPositions.empty()) {
        return;
    }
    axis1GoToPosition(m_axis1PreviousPositions.top());
    m_axis1PreviousPositions.pop();
}

void Model::axis1GoToCurrentMemory()
{
    if (m_axis1Memory.at(m_currentMemory) == AXIS1_UNSET) {
        return;
    }
    if (m_axis1Memory.at(m_currentMemory) == getAxis1MotorCurrentStep()) {
        return;
    }
    if (axis1MoveRefused()) {
        return;
    }
    axis1Stop();
    m_axis1Status = "returning";
    // (If threading, this waits for zero degrees on the chuck before starting)
    axis1GoToStep(m_axis1Memory.at(m_currentMemory));
}

void Model::axis1Nudge(ZDirection direction, double nudgeAmountMm)
{
    if (axis1MoveRefused()) {
        return;
    }
    // (First, so that its end can't restore a speed over ours)
    m_motionSequence.cancel();
    if (m_axis1Motor->isRunning()) {
        axis1Stop();
    }
    const double oldSpeed = m_axis1Motor->getSpeed();
    m_axis1Motor->setSpeed(m_axis1Motor->getMaxRpm() / 10.0);
    long steps = nudgeAmountMm / m_axis1Motor->getConversionFactor();
    if (direction == ZDirection::Left) {
        steps = -steps;
    }
    m_axis1Motor->stop();
    // The nudge is a sequence which ends when the move does, so that (as
    // with any sequence) keys pressed meanwhile wait until it's finished,
    // and a quick series of nudges each moves the full amount
    auto moveSteps = axis1MoveSteps(getAxis1MotorCurrentStep() + steps);
    moveSteps.back().done = [this]() { return !m_axis1Motor->isRunning(); };
    m_motionSequence.start(std::move(moveSteps), [this, oldSpeed]() {
        m_axis1Motor->setSpeed(oldSpeed);
        // A nudge isn't a threading pass, so it shouldn't retract X when it ends
        m_threadingMoveInProgress = false;
        // Save position
        axis1SaveBreadcrumbPosition();
    });
}

void Model::axis1Zero()
{
    if (m_mode == Mode::Taper) {
        changeMode(Mode::None);
    }
    if (m_config.readBool("Axis1UseLinearScale", false)) {
        m_linearScaleAxis1->setZeroMm();
    }
    m_axis1Motor->zeroPosition();
    // Zeroing will invalidate any memorised Z positions, so we clear them
    for (auto& m : m_axis1Memory) {
        m = AXIS1_UNSET;
    }
    // Also clear any "breadcrumb" positions
    axis1ClearBreadcrumbs();
    clearWorkEnd();
    // The position is known again, so a motor fault can be cleared
    clearMotorFault(Axis::Axis1);
}

void Model::axis1SpeedDecrease()
{
    if (m_mode == Mode::Threading) {
        return;
    }
    if (m_axis1Motor->getSpeed() > 20) {
        m_axis1Motor->setSpeed(m_axis1Motor->getSpeed() - 20.0);
    } else {
        if (m_axis1Motor->getSpeed() > 1) {
            m_axis1Motor->setSpeed(m_axis1Motor->getSpeed() - 1.0);
        }
    }
}

void Model::axis1SpeedIncrease()
{
    if (m_mode == Mode::Threading) {
        return;
    }
    if (m_axis1Motor->getSpeed() < 20.0) {
        m_axis1Motor->setSpeed(m_axis1Motor->getSpeed() + 1.0);
    } else {
        if (m_axis1Motor->getSpeed() <= m_config.readDouble("Axis1MaxMotorRpm", 1'000.0) - 20) {
            m_axis1Motor->setSpeed(m_axis1Motor->getSpeed() + 20.0);
        }
    }
}

void Model::axis1FastReturn()
{
    if (m_axis1Memory.at(m_currentMemory) == AXIS1_UNSET || m_axis1FastReturning) {
        return;
    }
    if (axis1MoveRefused()) {
        return;
    }
    m_previousAxis1Speed = m_axis1Motor->getSpeed();
    m_axis1FastReturning = true;
    axis1Stop();
    if (m_mode == Mode::Taper) {
        // If we are tapering, we need to set a speed the x-axis motor can keep up with
        m_axis1Motor->setSpeed(100.0);
        m_previousAxis2Speed = m_axis2Motor->getSpeed();
        // (axis1GoToStep() takes up the backlash and synchronises X)
    } else {
        m_axis1Motor->setSpeed(m_axis1Motor->getMaxRpm());
    }
    m_axis1Status = "fast returning";
    axis1GoToStep(m_axis1Memory.at(m_currentMemory));
}

void Model::axis2GoToPosition(double pos)
{
    if (axis2MoveRefused()) {
        return;
    }
    m_axis2Motor->goToPosition(pos);
    m_axis2Status = fmt::format("Going to {:.3f}", pos);
}

void Model::axis2GoToOffset(double offset)
{
    if (axis2MoveRefused()) {
        return;
    }
    m_axis2LastRelativeMove = offset;
    m_lastRelativeMoveAxis = Axis::Axis2;
    axis2GoToPosition(m_axis2Motor->getPosition() + offset);
    m_axis2Status = fmt::format("To rel {}", offset);
}

void Model::axis2GoToPreviousPosition()
{
    if (axis2MoveRefused()) {
        return;
    }
    axis2Stop();
    while (!m_axis2PreviousPositions.empty()
           && (std::abs(m_axis2PreviousPositions.top() - m_axis2Motor->getPosition()) < 0.0001)) {
        if (m_axis2PreviousPositions.size() == 1) {
            return;
        }
        m_axis2PreviousPositions.pop();
    }
    if (m_axis2PreviousPositions.empty()) {
        return;
    }
    axis2GoToPosition(m_axis2PreviousPositions.top());
    m_axis2PreviousPositions.pop();
}

void Model::axis1Move(ZDirection direction)
{
    // Issuing the same command (i.e. pressing the same key)
    // when it is already running will cause the motor to stop
    if (m_axis1Motor->isRunning()) {
        axis1Stop();
        return;
    }
    if (axis1MoveRefused()) {
        return;
    }
    if (direction == ZDirection::Left) {
        m_axis1Status = "moving left";
        if (!m_config.readBool("Axis1MotorFlipDirection", false)) {
            axis1GoToStep(INF_LEFT);
        } else {
            axis1GoToStep(INF_RIGHT);
        }
    } else {
        m_axis1Status = "moving right";
        if (!m_config.readBool("Axis1MotorFlipDirection", false)) {
            axis1GoToStep(INF_RIGHT);
        } else {
            axis1GoToStep(INF_LEFT);
        }
    }
}

void Model::axis1Rapid(ZDirection direction)
{
    // Don't allow rapids if in a mode;
    if (m_mode != Mode::None) {
        return;
    }
    if (axis1MoveRefused()) {
        return;
    }
    // Issuing the same command (i.e. pressing the same key)
    // when it is already running will cause the motor to stop
    if (m_axis1Motor->isRunning()) {
        axis1Stop();
        return;
    }
    m_previousAxis1Speed = m_axis1Motor->getSpeed();
    m_axis1RapidInProgress = true;
    m_axis1Motor->setSpeed(m_axis1Motor->getMaxRpm());
    axis1Move(direction);
}

void Model::axis1SpeedPreset()
{
    if (m_mode == Mode::Threading) {
        return;
    }
    switch (m_keyPressed) {
        case key::ONE:
            m_axis1Motor->setSpeed(m_config.readDouble("Axis1SpeedPreset1", 20.0));
            break;
        case key::TWO:
            m_axis1Motor->setSpeed(m_config.readDouble("Axis1SpeedPreset2", 40.0));
            break;
        case key::THREE:
            m_axis1Motor->setSpeed(m_config.readDouble("Axis1SpeedPreset3", 100.0));
            break;
        case key::FOUR:
            m_axis1Motor->setSpeed(m_config.readDouble("Axis1SpeedPreset4", 250.0));
            break;
        case key::FIVE:
            m_axis1Motor->setSpeed(m_config.readDouble("Axis1SpeedPreset5", 1'000.0));
            break;
    }
}

void Model::axis1SaveBreadcrumbPosition()
{
    if (!m_axis1PreviousPositions.empty()
        && m_axis1PreviousPositions.top() == m_axis1Motor->getPosition()) {
        return;
    }
    m_axis1PreviousPositions.push(m_axis1Motor->getPosition());
}

void Model::axis1ClearBreadcrumbs()
{
    m_axis1PreviousPositions = std::stack<double>();
    axis1SaveBreadcrumbPosition();
}

void Model::axis1SetSpeed(double speed)
{
    m_axis1Motor->setSpeed(speed);
}

void Model::axis1Wait()
{
    m_axis1Motor->wait();
}

void Model::axis2Wait()
{
    m_axis2Motor->wait();
}

void Model::axis1Stop()
{
    m_axis1Motor->stop();
    m_axis1Motor->wait();
}

void Model::axis1StorePosition()
{
    m_axis1Memory.at(m_currentMemory) = getAxis1MotorCurrentStep();
}

void Model::axis1StorePosition(double mm)
{
    const auto stepValue = mm / m_axis1Motor->getConversionFactor();
    m_axis1Memory.at(m_currentMemory) = stepValue;
}

void Model::axis2SetSpeed(double speed)
{
    m_axis2Motor->setSpeed(speed);
}

void Model::axis2Stop()
{
    m_axis2Motor->stop();
    m_axis2Motor->wait();
}

void Model::axis2Zero()
{
    if (m_mode == Mode::Taper) {
        changeMode(Mode::None);
    }
    m_axis2Motor->zeroPosition();
    // (Gear cutting's teeth are now wherever axis 2 is when it starts)
    m_gearToothOrigin.reset();
    // Zeroing will invalidate any memorised X positions, so we clear them
    for (auto& m : m_axis2Memory) {
        m = AXIS2_UNSET;
    }
    // Also clear any "breadcrumb" positions
    axis2ClearBreadcrumbs();
    // The position is known again, so a motor fault can be cleared
    clearMotorFault(Axis::Axis2);
    // X is no longer measured from the centre (unless set as a diameter again)
    clearDiameter();
}

void Model::axis2SynchroniseOff()
{
    m_axis2Motor->synchroniseOff();
}

void Model::axis2GoToCurrentMemory()
{
    if (m_axis2Memory.at(m_currentMemory) == AXIS2_UNSET
        || m_axis2Memory.at(m_currentMemory) == m_axis2Motor->getCurrentStep()) {
        return;
    }
    if (axis2MoveRefused()) {
        return;
    }
    axis2Stop();
    m_axis2Status = "returning";
    m_axis2Motor->goToStep(m_axis2Memory.at(m_currentMemory));
}

void Model::axis2SpeedDecrease()
{
    if (m_axis2Motor->getSpeed() > 10.1) {
        m_axis2Motor->setSpeed(m_axis2Motor->getSpeed() - 10.0);
    } else if (m_axis2Motor->getSpeed() > 2.1) {
        m_axis2Motor->setSpeed(m_axis2Motor->getSpeed() - 2.0);
    }
}

void Model::axis2SpeedIncrease()
{
    if (m_axis2Motor->getSpeed() < 10.0) {
        m_axis2Motor->setSpeed(m_axis2Motor->getSpeed() + 2.0);
    } else if (m_axis2Motor->getSpeed() < m_config.readDouble("Axis2MaxMotorRpm", 1'000.0)) {
        m_axis2Motor->setSpeed(m_axis2Motor->getSpeed() + 10.0);
    }
}

void Model::axis2Nudge(XDirection direction, double nudgeAmountMm)
{
    if (axis2MoveRefused()) {
        return;
    }
    // (First, so that its end can't restore a speed over ours)
    m_motionSequence.cancel();
    if (m_axis2Motor->isRunning()) {
        axis2Stop();
    }
    const double oldSpeed = m_axis2Motor->getSpeed();
    m_axis2Motor->setSpeed(m_axis2Motor->getMaxRpm() / 10.0);
    long steps = nudgeAmountMm / m_axis2Motor->getConversionFactor();
    if (direction == XDirection::Inwards) {
        steps = -steps;
    }
    if (m_config.readBool("Axis2MotorFlipDirection", false)) {
        steps = -steps;
    }
    // (A sequence, for the same reasons as a Z nudge)
    m_motionSequence.start(
        { { [this, steps]() { m_axis2Motor->goToStep(m_axis2Motor->getCurrentStep() + steps); },
            [this]() { return !m_axis2Motor->isRunning(); } } },
        [this, oldSpeed]() {
            m_axis2Motor->setSpeed(oldSpeed);
            // Save position
            axis2SaveBreadcrumbPosition();
        });
}

void Model::axis2FastReturn()
{
    if (m_axis2Memory.at(m_currentMemory) == AXIS2_UNSET || m_axis2FastReturning) {
        return;
    }
    if (axis2MoveRefused()) {
        return;
    }
    m_previousAxis2Speed = m_axis2Motor->getSpeed();
    m_axis2FastReturning = true;
    axis2Stop();
    m_axis2Motor->setSpeed(m_axis2Motor->getMaxRpm());
    m_axis2Status = "fast returning";
    m_axis2Motor->goToStep(m_axis2Memory.at(m_currentMemory));
}

bool Model::axis2CanRetract() const
{
    return !m_config.readBool("DisableAxis2", false) && !m_axis2Motor->isRunning()
        && m_mode != Mode::Taper;
}

void Model::axis2Retract()
{
    if (m_axis2Retracted) {
        axis2Unretract();
    } else {
        axis2RetractBy(m_axis2RetractionDistance);
    }
}

namespace {

// The motor only ramps up when its speed is increased, not at the start of
// every move, so (e.g. for an unretract straight after a retract, which
// leaves the speed unchanged) we drop the speed first to be sure it ramps.
// (Each retract starts from rest, as X can't retract while moving)
void setRetractSpeed(mgo::StepperMotor& motor)
{
    motor.setSpeed(1.0);
    motor.setSpeed(100.0);
}

} // end anonymous namespace

void Model::axis2Unretract()
{
    if (!m_axis2Retracted || !axis2CanRetract()) {
        return;
    }
    if (axis2MoveRefused()) {
        return;
    }
    setRetractSpeed(*m_axis2Motor);
    m_axis2Motor->goToStep(m_xOldPosition);
    m_axis2Status = "Unretracting";
    m_fastRetracting = true;
    MGOLOG(
        fmt::format(
            "X unretract: from step {} to {}",
            m_axis2Motor->getCurrentStep(),
            m_axis2Motor->getTargetStep()));
    m_axis2RetractMoveInProgress = true;
}

void Model::axis2RetractBy(double distance)
{
    if (m_axis2Retracted || !axis2CanRetract()) {
        return;
    }
    if (axis2MoveRefused()) {
        return;
    }
    m_xOldPosition = m_axis2Motor->getCurrentStep();
    m_previousAxis2Speed = m_axis2Motor->getSpeed();
    setRetractSpeed(*m_axis2Motor);
    int direction = -1;
    if (m_xRetractionDirection == XDirection::Inwards) {
        direction = 1;
    }
    long stepsForRetraction = std::lround(distance / std::abs(m_axis2Motor->getConversionFactor()));
    if (m_config.readBool("Axis2MotorFlipDirection", false)) {
        stepsForRetraction = -stepsForRetraction;
    }
    m_axis2Motor->goToStep(m_axis2Motor->getCurrentStep() + stepsForRetraction * direction);
    m_axis2Retracted = true;
    m_axis2Status = "Retracting";
    MGOLOG(
        fmt::format(
            "X retract by {} mm: from step {} to {}",
            distance,
            m_axis2Motor->getCurrentStep(),
            m_axis2Motor->getTargetStep()));
    m_axis2RetractMoveInProgress = true;
}

void Model::axis2RetractTo(double position, double minimum)
{
    // How far out position is from X (negative if it's further in)
    const double out = (m_axis2Motor->getPosition() - position) * axis2IntoWorkSign();
    axis2RetractBy(std::max(out, minimum));
}

void Model::axis2StorePosition()
{
    m_axis2Memory.at(m_currentMemory) = m_axis2Motor->getCurrentStep();
}

void Model::axis2StorePosition(double mm)
{
    const auto stepValue = mm / m_axis2Motor->getConversionFactor();
    m_axis2Memory.at(m_currentMemory) = stepValue;
}

void Model::axis2Move(XDirection direction)
{
    if (m_axis2Motor->isRunning()) {
        axis2Stop();
        return;
    }
    if (axis2MoveRefused()) {
        return;
    }
    if (direction == XDirection::Inwards) {
        m_axis2Status = "moving in";
        if (m_config.readBool("Axis2MotorFlipDirection", false)) {
            m_axis2Motor->goToStep(INF_OUT);
        } else {
            m_axis2Motor->goToStep(INF_IN);
        }
    } else {
        m_axis2Status = "moving out";
        if (m_config.readBool("Axis2MotorFlipDirection", false)) {
            m_axis2Motor->goToStep(INF_IN);
        } else {
            m_axis2Motor->goToStep(INF_OUT);
        }
    }
}

void Model::axis2Rapid(XDirection direction)
{
    // Don't allow rapids if in a mode;
    if (m_mode != Mode::None) {
        return;
    }
    if (axis2MoveRefused()) {
        return;
    }
    if (m_axis2Motor->isRunning()) {
        axis2Stop();
        return;
    }
    m_previousAxis2Speed = m_axis2Motor->getSpeed();
    m_axis2RapidInProgress = true;
    m_axis2Motor->setSpeed(m_axis2Motor->getMaxRpm());
    axis2Move(direction);
}

void Model::axis2SpeedPreset()
{
    if (m_mode == Mode::Threading) {
        return;
    }
    switch (m_keyPressed) {
        case key::a2_1:
        case key::SIX:
            m_axis2Motor->setSpeed(m_config.readDouble("Axis2SpeedPreset1", 5.0));
            break;
        case key::a2_2:
        case key::SEVEN:
            m_axis2Motor->setSpeed(m_config.readDouble("Axis2SpeedPreset2", 20.0));
            break;
        case key::a2_3:
        case key::EIGHT:
            m_axis2Motor->setSpeed(m_config.readDouble("Axis2SpeedPreset3", 40.0));
            break;
        case key::a2_4:
        case key::NINE:
            m_axis2Motor->setSpeed(m_config.readDouble("Axis2SpeedPreset4", 80.0));
            break;
        case key::a2_5:
        case key::ZERO:
            m_axis2Motor->setSpeed(m_config.readDouble("Axis2SpeedPreset5", 1'00.0));
            break;
    }
}

void Model::axis2SaveBreadcrumbPosition()
{
    if (!m_axis2PreviousPositions.empty()
        && m_axis2PreviousPositions.top() == m_axis2Motor->getPosition()) {
        return;
    }
    m_axis2PreviousPositions.push(m_axis2Motor->getPosition());
}

void Model::axis2ClearBreadcrumbs()
{
    m_axis2PreviousPositions = std::stack<double>();
    axis2SaveBreadcrumbPosition();
}

void Model::repeatLastRelativeMove(Axis axis)
{
    if (axis == Axis::Axis1) {
        if (m_axis1LastRelativeMove != 0.0) {
            axis1GoToOffset(m_axis1LastRelativeMove);
        }
    } else if (axis == Axis::Axis2) {
        if (m_axis2LastRelativeMove != 0.0) {
            axis2GoToOffset(m_axis2LastRelativeMove);
        }
    } else {
        assert(false); // Missing axis
    }
}

void Model::diameterIsSet()
{
    m_xDiameterSet = true;
    recordFeatureDiameter();
}

double Model::axis2WorkPosition() const
{
    // While retracted (from the start of a retract to the end of the
    // unretract), where X will return to: the diameter being cut
    return m_axis2Retracted ? m_axis2Motor->getPosition(m_xOldPosition) : getAxis2Position();
}

std::optional<double> Model::getDiameterMm() const
{
    if (!m_xDiameterSet || !m_axis2Motor) {
        return std::nullopt;
    }
    return std::abs(axis2WorkPosition() * 2.0);
}

bool Model::currentFeatureEnabled() const
{
    return m_config.readBool("CurrentFeature", false) && !m_config.readBool("DisableAxis1", false)
        && !m_config.readBool("DisableAxis2", false);
}

std::optional<double> Model::getFeatureDiameterMm() const
{
    if (!m_featureX) {
        return std::nullopt;
    }
    return std::abs(*m_featureX * 2.0);
}

std::optional<double> Model::getWorkEndZ() const
{
    return m_workEndZ;
}

bool Model::recordFeatureDiameter()
{
    if (!currentFeatureEnabled()) {
        return false;
    }
    if (!getDiameterMm()) {
        m_warning = "Set " + m_config.read("Axis2Label", "X") + " as a diameter first";
        return false;
    }
    m_featureX = axis2WorkPosition();
    MGOLOG(fmt::format("Feature diameter recorded: {}", *getFeatureDiameterMm()));
    return true;
}

void Model::markWorkEnd()
{
    if (!currentFeatureEnabled()) {
        return;
    }
    m_workEndZ = m_axis1Motor->getPosition();
    MGOLOG(fmt::format("Work end marked: Z {}", *m_workEndZ));
}

void Model::clearWorkEnd()
{
    m_workEndZ.reset();
}

void Model::clearFeature()
{
    m_featureX.reset();
    m_workEndZ.reset();
}

bool Model::featureGoToAllowed() const
{
    const bool modeAllows = m_mode == Mode::None
        || ((m_mode == Mode::MultiPass || m_mode == Mode::Radius) && !m_multiPass.inProgress()
            && !m_multiPassMovingToStart);
    return currentFeatureEnabled() && modeAllows && !m_motionSequence.isRunning() && !m_axis1Faulted
        && !m_axis2Faulted;
}

std::optional<double> Model::featureWorkEndTarget(bool clear) const
{
    if (!m_workEndZ) {
        return std::nullopt;
    }
    if (!clear) {
        return m_workEndZ;
    }
    const double towardsChuck
        = m_config.read("Axis1TowardsChuck", "negative") == "positive" ? 1.0 : -1.0;
    return *m_workEndZ - towardsChuck * m_config.readDouble("FeatureStartClearance", 0.5);
}

void Model::featureGoToStart(bool clear)
{
    if (!featureGoToAllowed()) {
        return;
    }
    const auto z = featureWorkEndTarget(clear);
    if (!z || !m_featureX) {
        m_warning = "The feature's diameter and the work's end are both needed";
        return;
    }
    if (moveRefused(true, true)) {
        return;
    }
    if (m_axis1Motor->isRunning() || m_axis2Motor->isRunning()) {
        m_warning = "Stop the motors first";
        return;
    }
    m_warning = "";
    moveToSafely(*z, *m_featureX, true, "to feature", {});
}

void Model::featureGoToDiameter()
{
    if (!featureGoToAllowed()) {
        return;
    }
    if (!m_featureX) {
        m_warning = "The feature's diameter isn't set";
        return;
    }
    if (axis2MoveRefused()) {
        return;
    }
    if (m_axis2Motor->isRunning()) {
        m_warning = "Stop the motors first";
        return;
    }
    m_warning = "";
    // (Not retracting: X stays where Z is)
    moveToSafely(m_axis1Motor->getPosition(), *m_featureX, false, "to feature", {});
}

void Model::featureGoToWorkEnd(bool clear)
{
    if (!featureGoToAllowed()) {
        return;
    }
    const auto z = featureWorkEndTarget(clear);
    if (!z) {
        m_warning = "The work's end isn't set";
        return;
    }
    if (m_axis1Motor->isRunning() || m_axis2Motor->isRunning()) {
        m_warning = "Stop the motors first";
        return;
    }
    // X must be clear of the work: not deeper in than the feature's surface.
    // (Without the diameter, nothing's known: X is the user's responsibility,
    // as for any Z move)
    if (m_featureX
        && (m_axis2Motor->getPosition() - *m_featureX) * axis2IntoWorkSign()
            > MultiPass::TOLERANCE) {
        m_warning = "Move " + m_config.read("Axis2Label", "X") + " clear of the work first";
        return;
    }
    m_warning = "";
    // (As a Z go to: at Z's speed, and X stays as it is, even if retracted)
    axis1GoToPosition(*z);
}

XDirection Model::getRetractionDirection() const
{
    return m_xRetractionDirection;
}

void Model::setRetractionDirection(XDirection direction)
{
    m_xRetractionDirection = direction;
}

bool Model::getIsAxis2Retracted() const
{
    return m_axis2Retracted;
}

void Model::setIsAxis2Retracted(bool flag)
{
    m_axis2Retracted = flag;
}

KeyMode Model::getKeyMode() const
{
    return m_keyMode;
}

void Model::setKeyMode(KeyMode mode)
{
    m_keyMode = mode;
}

Mode Model::getMode() const
{
    return m_mode;
}

double Model::getTaperAngle() const
{
    return m_taperAngle;
}

void Model::setTaperAngle(double taperAngle)
{
    m_taperAngle = taperAngle;
}

int Model::getKeyPressed() const
{
    return m_keyPressed;
}

void Model::setKeyPressed(int key)
{
    m_keyPressed = key;
}

bool Model::isShuttingDown() const
{
    return m_shutdown;
}

void Model::shutDown()
{
    m_shutdown = true;
}

int64_t Model::getSpindlePulses() const
{
    return m_rotaryEncoder->getSpindlePulses();
}

double Model::getThreadingZSpeed() const
{
    return m_threadingZSpeed;
}

bool Model::isMotionSequenceRunning() const
{
    return m_motionSequence.isRunning();
}

bool Model::isQuitting() const
{
    return m_quit;
}

void Model::quit()
{
    m_quit = true;
}

std::string Model::getInputString() const
{
    return m_input;
}

std::string& Model::getInputString()
{
    return m_input;
}

void Model::setInputString(const std::string& str)
{
    m_input = str;
}

std::string Model::getWarning() const
{
    if (m_warning.empty() && spindleSynchronised()) {
        if (m_threadingPositionLost) {
            return m_mode == Mode::Knurling
                ? "Encoder lost pulses: further passes won't follow this knurl"
                : "Encoder lost pulses: further passes won't follow this thread";
        }
        return m_threadingWarning;
    }
    return m_warning;
}

std::string Model::getAxis1Status() const
{
    return m_axis1Status;
}

std::string Model::getAxis2Status() const
{
    return m_axis2Status;
}

std::string Model::getGeneralStatus() const
{
    return m_generalStatus;
}

const ThreadTable& Model::getThreads() const
{
    return m_threads;
}

void Model::setThreads(ThreadTable threads)
{
    m_threads = std::move(threads);
    m_threadChoice = {};
}

void Model::setThread(const ThreadChoice& choice)
{
    if (choice.category >= m_threads.size()
        || choice.thread >= m_threads.at(choice.category).threads.size()) {
        return;
    }
    m_threadChoice = choice;
    setRetractionDirection(choice.female ? XDirection::Inwards : XDirection::Outwards);
    const Thread& thread = *getThread();
    MGOLOG(
        fmt::format(
            "Thread chosen: {} ({}), pitch {}",
            thread.name,
            choice.female ? "female" : "male",
            describePitch(thread)));
}

const ThreadChoice& Model::getThreadChoice() const
{
    return m_threadChoice;
}

const Thread* Model::getThread() const
{
    if (m_threadChoice.category >= m_threads.size()) {
        return nullptr;
    }
    const auto& threads = m_threads.at(m_threadChoice.category).threads;
    return m_threadChoice.thread < threads.size() ? &threads.at(m_threadChoice.thread) : nullptr;
}

void Model::selectPreviousMemorySlot()
{
    if (m_currentMemory > 0) {
        --m_currentMemory;
    }
}

void Model::selectNextMemorySlot()
{
    if (m_currentMemory < m_axis1Memory.size() - 1) {
        ++m_currentMemory;
    }
}

std::size_t Model::getCurrentMemorySlot() const
{
    return m_currentMemory;
}

long Model::getAxis1Memory(std::size_t index) const
{
    return m_axis1Memory.at(index);
}

std::optional<double> Model::getAxis1MemoryAsPosition(std::size_t index) const
{
    long step = m_axis1Memory.at(index);
    if (step == AXIS1_UNSET) {
        return std::nullopt;
    }
    return step * m_axis1Motor->getConversionFactor();
}

long Model::getAxis2Memory(std::size_t index) const
{
    return m_axis2Memory.at(index);
}

std::optional<double> Model::getAxis2MemoryAsPosition(std::size_t index) const
{
    long step = m_axis2Memory.at(index);
    if (step == AXIS2_UNSET) {
        return std::nullopt;
    }
    return step * m_axis2Motor->getConversionFactor();
}

unsigned Model::getMemorySize() const
{
    return m_axis1Memory.size();
}

void Model::clearAllAxis1Memories()
{
    for (auto& m : m_axis1Memory) {
        m = AXIS1_UNSET;
    }
}

void Model::clearAllAxis2Memories()
{
    for (auto& m : m_axis2Memory) {
        m = AXIS2_UNSET;
    }
}

void Model::setAxis1MotorSpeed(double speed)
{
    m_axis1Motor->setSpeed(speed);
}

void Model::setAxis2MotorSpeed(double speed)
{
    m_axis2Motor->setSpeed(speed);
}

void Model::setAxis1Position(double mm)
{
    m_axis1Motor->setPosition(mm);
    // The position is known again, so a motor fault can be cleared
    clearMotorFault(Axis::Axis1);
}

void Model::setAxis2Position(double mm)
{
    m_axis2Motor->setPosition(mm);
    // (Gear cutting's teeth are now wherever axis 2 is when it starts)
    m_gearToothOrigin.reset();
    // The position is known again, so a motor fault can be cleared
    clearMotorFault(Axis::Axis2);
    // A new position isn't necessarily measured from the centre: if it was
    // set as a diameter, diameterIsSet() is called after this
    clearDiameter();
}

void Model::clearDiameter()
{
    if (m_xDiameterSet) {
        m_xDiameterSet = false;
        m_generalStatus = DEFAULT_GENERAL_STATUS;
    }
    // (Its diameter needs X measured from the centre)
    m_featureX.reset();
}

void Model::resetMotorThreads()
{
    m_axis2Motor.reset();
    m_axis1Motor.reset();
}

double Model::getAxis1MotorPosition() const
{
    if (!m_axis1Motor) {
        return 0.0;
    }
    if (m_config.readBool("Axis1UseLinearScale", false)) {
        return m_linearScaleAxis1->getPositionInMm();
    }
    return m_axis1Motor->getPosition();
}

double Model::getAxis2Position() const
{
    return getAxis2MotorPosition() - multiPassXCorrection();
}

double Model::getAxis2MotorPosition() const
{
    if (!m_axis2Motor) {
        return 0.0;
    }
    return m_axis2Motor->getPosition();
}

double Model::getAxis1MotorSpeed() const
{
    if (!m_axis1Motor) {
        return 0.0;
    }
    return m_axis1Motor->getSpeed();
}

double Model::getAxis2MotorSpeed() const
{
    if (!m_axis2Motor) {
        return 0.0;
    }
    return m_axis2Motor->getSpeed();
}

double Model::getAxis1MotorCurrentStep() const
{
    if (!m_axis1Motor) {
        return 0.0;
    }
    if (m_config.readBool("Axis1UseLinearScale", false)) {
        return m_linearScaleAxis1->getPositionInMm() / m_axis1Motor->getConversionFactor();
    }
    return m_axis1Motor->getCurrentStep();
}

double Model::getAxis2MotorCurrentStep() const
{
    if (!m_axis2Motor) {
        return 0.0;
    }
    return m_axis2Motor->getCurrentStep();
}

bool Model::isAxis1MotorRunning() const
{
    return m_axis1Motor->isRunning();
}

bool Model::isAxis2MotorRunning() const
{
    return m_axis2Motor->isRunning();
}

float Model::getChuckAngle() const
{
    if (!m_rotaryEncoder) {
        return 0.f;
    }
    return m_rotaryEncoder->getPositionDegrees();
}

namespace {

// Formats a value to a number of decimal places, avoiding "-0.00"
std::string formatValue(double value, int decimals)
{
    if (std::abs(value) < 0.5 * std::pow(10.0, -decimals)) {
        value = 0.0;
    }
    return fmt::format("{: .{}f}", value, decimals);
}

} // end anonymous namespace

std::string Model::formatAxis1Position(long step) const
{
    if (!m_axis1Motor) {
        return std::string();
    }
    return formatValue(toDisplayUnits(1, m_axis1Motor->getPosition(step)), axisDecimals(1));
}

std::string Model::formatAxis2Position(long step) const
{
    if (!m_axis2Motor) {
        return std::string();
    }
    return formatValue(toDisplayUnits(2, m_axis2Motor->getPosition(step)), axisDecimals(2));
}

DisplayUnits Model::getDisplayUnits() const
{
    return m_displayUnits;
}

void Model::toggleDisplayUnits()
{
    m_displayUnits
        = m_displayUnits == DisplayUnits::Metric ? DisplayUnits::Imperial : DisplayUnits::Metric;
}

bool Model::threadingAutoRetract() const
{
    return m_threadingAutoRetract;
}

void Model::toggleThreadingAutoRetract()
{
    m_threadingAutoRetract = !m_threadingAutoRetract;
    MGOLOG(fmt::format("Threading auto-retract {}", m_threadingAutoRetract ? "on" : "off"));
}

bool Model::isLengthAxis(int axisNumber) const
{
    return m_config.read(fmt::format("Axis{}DisplayUnits", axisNumber), "mm") == "mm";
}

double Model::toDisplayUnits(int axisNumber, double value) const
{
    return isLengthAxis(axisNumber) ? toDisplayLength(value, m_displayUnits) : value;
}

double Model::fromDisplayUnits(int axisNumber, double value) const
{
    return isLengthAxis(axisNumber) ? fromDisplayLength(value, m_displayUnits) : value;
}

std::string Model::axisUnitName(int axisNumber) const
{
    if (isLengthAxis(axisNumber)) {
        return lengthUnitName(m_displayUnits);
    }
    return m_config.read(fmt::format("Axis{}DisplayUnits", axisNumber), "mm");
}

int Model::axisDecimals(int axisNumber) const
{
    return isLengthAxis(axisNumber) ? positionDecimals(m_displayUnits) : 2;
}

float Model::getRotaryEncoderRpm() const
{
    if (!m_rotaryEncoder) {
        return 0.f;
    }
    return m_rotaryEncoder->getRpm();
}

float Model::getAxis1LinearScalePosMm() const
{
    if (m_linearScaleAxis1) {
        return m_linearScaleAxis1->getPositionInMm();
    } else {
        return 0.f;
    }
}

void Model::clearCurrentMemorySlot(Axis axis)
{
    if (axis == Axis::Axis1) {
        m_axis1Memory.at(m_currentMemory) = AXIS1_UNSET;
    }
    if (axis == Axis::Axis2) {
        m_axis2Memory.at(m_currentMemory) = AXIS2_UNSET;
    }
}

bool Model::limitSwitchTripped() const
{
    // (Not before initialise(), which makes the motors)
    if (!m_axis1Motor || !m_axis2Motor) {
        return false;
    }
    return m_axis1Motor->isLimitTripped() || m_axis2Motor->isLimitTripped();
}

bool Model::limitSwitchPressed() const
{
    // (Both motors are given the same pin, so either will do)
    return m_axis1Motor && m_axis1Motor->isLimitSwitchActive();
}

void Model::clearLimitSwitchTrip()
{
    if (!limitSwitchTripped()) {
        return;
    }
    m_axis1Motor->clearLimitTrip();
    m_axis2Motor->clearLimitTrip();
    m_limitTripSeen = false;
    MGOLOG(
        fmt::format(
            "Limit switch stop cleared{}",
            limitSwitchPressed() ? " (a switch is still pressed)" : ""));
}

void Model::checkLimitSwitch()
{
    // (The motors have already stopped themselves, within a couple of
    // milliseconds; here we deal with the rest)
    if (!limitSwitchTripped() || m_limitTripSeen) {
        return;
    }
    m_limitTripSeen = true;
    MGOLOG(
        fmt::format(
            "*** Limit switch: all motors stopped, at {} {}, {} {}",
            m_config.read("Axis1Label", "Z"),
            m_axis1Motor->getPosition(),
            m_config.read("Axis2Label", "X"),
            m_axis2Motor->getPosition()));
    // e.g. a multi-pass, a motion sequence, or X synchronised to Z. Each motor
    // only stops itself when it sees the switch, which an idle one may not
    // until asked to move, so this stops them all at once too. (The position
    // isn't lost, so the mode and current feature are kept)
    stopAllMotors();
}

bool Model::lockRefused(unsigned axisNumber) const
{
    // Not while anything is moving it, so nothing under way (e.g. a pass, or
    // a nudge's sequence) finds it locked part way through. (In a taper, X
    // follows Z, so is moving whenever Z is)
    const bool follows = axisNumber == 2 && m_mode == Mode::Taper;
    const bool moving = (axisNumber == 1 ? m_axis1Motor : m_axis2Motor)->isRunning()
        || (follows && m_axis1Motor->isRunning());
    const bool multiPassRunning
        = (m_multiPass.inProgress() && m_multiPass.state() != MultiPass::State::Paused)
        || m_multiPassMovingToStart;
    return moving || m_motionSequence.isRunning() || multiPassRunning;
}

void Model::lockAxis(unsigned axisNumber)
{
    if (lockRefused(axisNumber)) {
        const std::string label
            = axisNumber == 1 ? m_config.read("Axis1Label", "Z") : m_config.read("Axis2Label", "X");
        m_warning = "Stop " + label + " before locking it";
        m_lockWarning = m_warning;
        m_lockRefusedAxis = axisNumber;
        return;
    }
    m_axisLocks.insert(axisNumber);
}

void Model::checkLockRefusal()
{
    // "Stop Z before locking it" goes once Z has stopped (unless something
    // else has been warned about since)
    if (!m_lockRefusedAxis || lockRefused(*m_lockRefusedAxis)) {
        return;
    }
    if (m_warning == m_lockWarning) {
        m_warning = "";
    }
    m_lockRefusedAxis.reset();
}

void Model::unlockAxis(unsigned axisNumber)
{
    auto it = m_axisLocks.find(axisNumber);
    if (it != m_axisLocks.end()) {
        m_axisLocks.erase(it);
    }
    // (Unless something else has been warned about since)
    if (!m_warning.empty() && m_warning == m_lockWarning) {
        m_warning = "";
    }
}

bool Model::moveRefused(bool axis1, bool axis2)
{
    const bool z = axis1 && isAxisLocked(1);
    const bool x = axis2 && isAxisLocked(2);
    if (!z && !x) {
        return false;
    }
    const std::string zLabel = m_config.read("Axis1Label", "Z");
    const std::string xLabel = m_config.read("Axis2Label", "X");
    const std::string which = z && x ? zLabel + " and " + xLabel : (z ? zLabel : xLabel);
    m_warning = "Unlock " + which + " first";
    m_lockWarning = m_warning;
    m_lockRefusedAxis.reset();
    MGOLOG(fmt::format("Move refused: {} locked", which));
    return true;
}

bool Model::axis1MoveRefused()
{
    return moveRefused(true, m_mode == Mode::Taper);
}

bool Model::axis2MoveRefused()
{
    return moveRefused(false, true);
}

bool Model::isAxisLocked(unsigned axisNumber) const
{
    auto it = m_axisLocks.find(axisNumber);
    if (it != m_axisLocks.end()) {
        return true;
    }
    return false;
}

RotationDirection Model::getChuckRotationDirection() const
{
    if (m_rotaryEncoder && m_rotaryEncoder->getRpm() > 50.f) {
        return m_rotaryEncoder->getRotationDirection();
    }
    return RotationDirection::normal;
}

const IConfigReader& Model::config() const
{
    return m_config;
}

std::string Model::setMultiPassSetup(const MultiPassSetup& setup)
{
    const MultiPassResolved resolved = resolveMultiPass(
        setup,
        multiPassPositions(),
        m_config.read("Axis1Label", "Z"),
        m_config.read("Axis2Label", "X"));
    if (!resolved.error.empty()) {
        return resolved.error;
    }
    m_multiPassSetup = setup;
    m_multiPassRange = resolved.range;
    MGOLOG(
        fmt::format(
            "Multi-pass set up: Z {} to {}, X {} to {}",
            m_multiPassRange.cutStart,
            m_multiPassRange.cutEnd,
            m_multiPassRange.feedStart,
            m_multiPassRange.feedEnd));
    return {};
}

const MultiPassSetup& Model::getMultiPassSetup() const
{
    return m_multiPassSetup;
}

const MultiPassRange& Model::getMultiPassRange() const
{
    return m_multiPassRange;
}

namespace {

// e.g. "M6 male": which thread a threading passes setup was for
std::string threadKey(const Thread& thread, bool female)
{
    return thread.name + (female ? " female" : " male");
}

} // end anonymous namespace

std::string Model::setThreadingPassesSetup(const MultiPassSetup& setup)
{
    if (m_mode != Mode::Threading) {
        return "Threading passes are only in threading mode";
    }
    const Thread* thread = getThread();
    if (!thread) {
        return "Choose a thread first";
    }
    MultiPassSetup s = setup;
    // (The return isn't locked to the spindle, so would drag the tool across
    // the thread)
    s.retract = true;
    const MultiPassResolved resolved = resolveMultiPass(
        s,
        multiPassPositions(),
        m_config.read("Axis1Label", "Z"),
        m_config.read("Axis2Label", "X"));
    if (!resolved.error.empty()) {
        return resolved.error;
    }
    m_threadingPassesSetup = s;
    m_threadingPassesThread = threadKey(*thread, m_threadChoice.female);
    m_multiPassRange = resolved.range;
    m_threadingPassesSet = true;
    m_springPassX.reset();
    m_springPasses = false;
    MGOLOG(
        fmt::format(
            "Threading passes set up for {}: Z {} to {}, X {} to {} by {}",
            m_threadingPassesThread,
            m_multiPassRange.cutStart,
            m_multiPassRange.cutEnd,
            m_multiPassRange.feedStart,
            m_multiPassRange.feedEnd,
            s.feedPerPass));
    return {};
}

const MultiPassSetup& Model::getThreadingPassesSetup() const
{
    return m_threadingPassesSetup;
}

bool Model::threadingPassesSet() const
{
    return m_threadingPassesSet;
}

MultiPassSetup Model::threadingPassesDialogSetup() const
{
    MultiPassSetup s = defaultMultiPassSetup(m_threadingPassesSetup, multiPassPositions());
    s.retract = true;
    if (m_threadingPassesThread.empty()) {
        // (The first time: afterwards, as last time)
        s.constantArea = true;
        s.compoundInfeed = true;
    }
    const Thread* thread = getThread();
    if (thread && threadKey(*thread, m_threadChoice.female) != m_threadingPassesThread) {
        // A different thread: in by its depth
        s.feedEndMemory = std::nullopt;
        s.feedInfeed = *getThreadDepthMm();
    }
    return s;
}

std::optional<double> Model::getThreadDepthMm() const
{
    const Thread* thread = getThread();
    if (!thread) {
        return std::nullopt;
    }
    return m_threadChoice.female ? thread->femaleDepth() : thread->maleDepth();
}

double Model::getCompoundInfeedAngleDeg() const
{
    const Thread* thread = getThread();
    return thread ? threadFormInfo(thread->form).angle / 2.0 - 0.5 : 0.0;
}

std::string Model::setKnurlingSetup(const MultiPassSetup& setup)
{
    if (m_mode != Mode::Knurling) {
        return "Knurling passes are only in knurling mode";
    }
    if (!m_xDiameterSet) {
        return "Set " + m_config.read("Axis2Label", "X")
            + " as a diameter first: the knurl's lead depends on it";
    }
    if (setup.knurlStarts < 1) {
        return "There must be at least one start";
    }
    if (setup.knurlHelixAngleDeg <= 0.0 || setup.knurlHelixAngleDeg >= 90.0) {
        return "The helix angle must be between 0 and 90 degrees";
    }
    MultiPassSetup s = setup;
    // (Both ways are cuts, with the tool in)
    s.retract = false;
    const MultiPassResolved resolved = resolveMultiPass(
        s,
        multiPassPositions(),
        m_config.read("Axis1Label", "Z"),
        m_config.read("Axis2Label", "X"));
    if (!resolved.error.empty()) {
        return resolved.error;
    }
    // (The helix angle is on the work's surface, where the tool starts)
    const double diameter = 2.0 * std::abs(resolved.range.feedStart);
    if (diameter < MultiPass::TOLERANCE) {
        return "The start is on the centre line";
    }
    m_knurlingSetup = s;
    m_multiPassRange = resolved.range;
    m_knurlLeadMm = knurlLeadMm(diameter, s.knurlHelixAngleDeg);
    MGOLOG(
        fmt::format(
            "Knurling set up: Z {} to {}, X {} to {} by {}, diameter {}, {} starts at {} deg, lead "
            "{}",
            m_multiPassRange.cutStart,
            m_multiPassRange.cutEnd,
            m_multiPassRange.feedStart,
            m_multiPassRange.feedEnd,
            s.feedPerPass,
            diameter,
            s.knurlStarts,
            s.knurlHelixAngleDeg,
            m_knurlLeadMm));
    return {};
}

const MultiPassSetup& Model::getKnurlingSetup() const
{
    return m_knurlingSetup;
}

MultiPassSetup Model::knurlingDialogSetup() const
{
    MultiPassSetup s = defaultMultiPassSetup(m_knurlingSetup, multiPassPositions());
    s.retract = false;
    if (s.knurlStarts < 1) {
        // (The first time: afterwards, as last time)
        const auto diameter = getDiameterMm();
        s.knurlStarts = diameter ? knurlStartsForPitch(*diameter, KNURLING_DEFAULT_PITCH_MM) : 1;
        // (As for threading)
        s.constantArea = true;
    }
    return s;
}

std::optional<double> Model::getKnurlLeadMm() const
{
    return m_mode == Mode::Knurling && m_knurlLeadMm > 0.0 ? std::optional(m_knurlLeadMm)
                                                           : std::nullopt;
}

std::string Model::gearCuttingUnavailable() const
{
    if (!m_config.readBool("GearCutting", false)) {
        return "Gear cutting isn't turned on (GearCutting in the config file)";
    }
    if (m_config.readBool("DisableAxis1", false) || m_config.readBool("DisableAxis2", false)) {
        return "Gear cutting needs both axes";
    }
    // (Its indexes are fractions of 360)
    if (m_config.read("Axis2DisplayUnits", "mm") != "deg") {
        return "Gear cutting needs " + m_config.read("Axis2Label", "X")
            + " to be a rotary table (Axis2DisplayUnits = deg)";
    }
    return {};
}

std::string Model::setGearSetup(const GearSetup& setup)
{
    if (m_mode != Mode::GearCutting) {
        return "Gear cutting is only in gear cutting mode";
    }
    if (const std::string error = gearCuttingUnavailable(); !error.empty()) {
        return error;
    }
    const GearCutResolved resolved
        = resolveGearCut(setup, multiPassPositions(), m_config.read("Axis1Label", "Z"));
    if (!resolved.error.empty()) {
        return resolved.error;
    }
    if (setup.teeth != m_gearSetup.teeth) {
        // A different gear: its first tooth is wherever axis 2 is then
        m_gearToothOrigin.reset();
    }
    m_gearSetup = setup;
    // (The depth is fed by hand, so is just a distance)
    m_multiPassRange = { .cutStart = resolved.cutStart,
                         .cutEnd = resolved.cutEnd,
                         .feedStart = 0.0,
                         .feedEnd = setup.depth };
    m_gearSet = true;
    MGOLOG(
        fmt::format(
            "Gear cutting set up: module {}, {} teeth, axis 1 {} to {}, depth {} by {}",
            setup.module,
            setup.teeth,
            resolved.cutStart,
            resolved.cutEnd,
            setup.depth,
            setup.depthPerPass));
    return {};
}

const GearSetup& Model::getGearSetup() const
{
    return m_gearSetup;
}

bool Model::gearCuttingSet() const
{
    return m_mode == Mode::GearCutting && m_gearSet;
}

double Model::gearStartTooth() const
{
    const double a = m_axis2Motor->getPosition();
    if (!m_gearToothOrigin || m_gearSetup.teeth < 1) {
        return a;
    }
    // The next tooth forwards (so the table's backlash stays taken up), or
    // this one, if it's within half a step of it
    const double pitch = 360.0 / m_gearSetup.teeth;
    const double slack = indexStepSize() / 2.0 + MultiPass::TOLERANCE;
    return *m_gearToothOrigin + std::ceil((a - *m_gearToothOrigin - slack) / pitch) * pitch;
}

bool Model::gearAOnTooth() const
{
    return std::abs(m_axis2Motor->getPosition() - gearStartTooth())
        < indexStepSize() / 2.0 + MultiPass::TOLERANCE;
}

GearSetup Model::gearDialogSetup() const
{
    return defaultGearSetup(m_gearSetup, multiPassPositions());
}

std::string Model::radiusUnavailable() const
{
    const std::string z = m_config.read("Axis1Label", "Z");
    const std::string x = m_config.read("Axis2Label", "X");
    if (!currentFeatureEnabled()) {
        return "Radius cutting needs the current feature (CurrentFeature = true in the config)";
    }
    if (!m_featureX || !m_workEndZ) {
        return "Face the end and mark it (" + z + "E), and set " + x
            + " as a diameter on the work, first";
    }
    if (m_xRetractionDirection != XDirection::Outwards) {
        // (The radius is on the outside of the work: X must come out of it)
        return "Radius cutting is on the outside of the work: set the retraction to outwards";
    }
    return {};
}

RadiusWork Model::radiusWork() const
{
    RadiusWork work;
    work.diameter = getFeatureDiameterMm().value_or(0.0);
    work.workEndZ = m_workEndZ.value_or(0.0);
    work.zTowardsChuck = m_config.read("Axis1TowardsChuck", "negative") == "positive" ? 1.0 : -1.0;
    work.xSign = m_featureX.value_or(0.0) < 0.0 ? -1.0 : 1.0;
    work.clearance = m_config.readDouble("FeatureStartClearance", 0.5);
    // X ramps up to anything faster than this, which on a short taper it
    // might never get to: it would lag, and cut too deep. (It's also as fast
    // as an open-loop stepper can safely start)
    work.xMaxSpeed = axis2MaxSpeed() * StepperMotor::RAMP_START_FRACTION;
    return work;
}

std::string Model::setRadiusSetup(const RadiusSetup& setup)
{
    if (m_mode != Mode::Radius) {
        return "Radius cutting is only in radius mode";
    }
    if (const std::string error = radiusUnavailable(); !error.empty()) {
        return error;
    }
    const RadiusWork work = radiusWork();
    if (work.xSign * axis2IntoWorkSign() > 0.0) {
        // X's position must get smaller (nearer the centre) going into the
        // work, as it does on the outside of it
        return m_config.read("Axis2Label", "X")
            + " goes the wrong way into the work for a radius on its outside";
    }
    RadiusPlan plan = planRadius(setup, work);
    if (!plan.error.empty()) {
        return plan.error;
    }
    m_radiusSetup = setup;
    m_radiusPasses = std::move(plan.passes);
    m_multiPassRange = plan.range;
    m_radiusMultiPassSetup = {};
    m_radiusMultiPassSetup.retractBeforeMoveToStart = true;
    m_radiusSet = true;
    MGOLOG(
        fmt::format(
            "Radius set up: radius {} on diameter {}, end Z {}, {} per pass, nose {}: {} passes, Z "
            "{} to {}, X {} to {}",
            setup.radius,
            work.diameter,
            work.workEndZ,
            setup.depthPerPass,
            setup.noseRadius,
            m_radiusPasses.size(),
            m_multiPassRange.cutStart,
            m_multiPassRange.cutEnd,
            m_multiPassRange.feedStart,
            m_multiPassRange.feedEnd));
    return {};
}

const RadiusSetup& Model::getRadiusSetup() const
{
    return m_radiusSetup;
}

bool Model::radiusSet() const
{
    return m_mode == Mode::Radius && m_radiusSet;
}

const MultiPassSetup& Model::activeMultiPassSetup() const
{
    switch (m_mode) {
        case Mode::Threading:
            return m_threadingPassesSetup;
        case Mode::Knurling:
            return m_knurlingSetup;
        case Mode::Radius:
            return m_radiusMultiPassSetup;
        default:
            return m_multiPassSetup;
    }
}

MultiPassPositions Model::multiPassPositions() const
{
    MultiPassPositions p;
    p.cutHere = m_axis1Motor->getPosition();
    p.feedHere = m_axis2Motor->getPosition();
    for (std::size_t slot = 0; slot < m_axis1Memory.size(); ++slot) {
        p.cutMemory.push_back(getAxis1MemoryAsPosition(slot));
        p.feedMemory.push_back(getAxis2MemoryAsPosition(slot));
    }
    p.feedIntoWork = axis2IntoWorkSign();
    if (m_featureX) {
        p.featureFeed = m_featureX;
    }
    p.featureWorkEnd = m_workEndZ;
    p.featureClearance = m_config.readDouble("FeatureStartClearance", 0.5);
    return p;
}

double Model::axis2IntoWorkSign() const
{
    // As axis2Retract() moves: its steps, then those in mm
    double retractSteps = m_xRetractionDirection == XDirection::Inwards ? 1.0 : -1.0;
    if (m_config.readBool("Axis2MotorFlipDirection", false)) {
        retractSteps = -retractSteps;
    }
    const double retractMm = retractSteps * m_axis2Motor->getConversionFactor();
    return retractMm > 0.0 ? -1.0 : 1.0;
}

bool Model::multiPassAtStart() const
{
    if (m_mode == Mode::GearCutting) {
        return atMultiPassPosition(m_axis1Motor->getPosition(), m_multiPassRange.cutStart)
            && gearAOnTooth();
    }
    return atMultiPassPosition(m_axis1Motor->getPosition(), m_multiPassRange.cutStart)
        && atMultiPassPosition(m_axis2Motor->getPosition(), m_multiPassRange.feedStart);
}

bool Model::multiPassMovingToStart() const
{
    return m_multiPassMovingToStart;
}

void Model::multiPassMoveToStart()
{
    MGOLOG(
        fmt::format(
            "Multi-pass: moving to the start, Z {}, X {}",
            m_multiPassRange.cutStart,
            m_multiPassRange.feedStart));
    m_multiPassMovingToStart = true;
    if (m_mode == Mode::GearCutting) {
        // Axis 1 to the start (clear of the blank), and only then axis 2 on
        // to the next tooth, if it isn't on one (e.g. stopped while turning)
        const double tooth = gearStartTooth();
        const double oldSpeed = m_axis1Motor->getSpeed();
        std::vector<MotionSequence::Step> steps;
        steps.push_back(
            { [this]() {
                 m_axis1Status = "to start";
                 m_axis1Motor->setSpeed(m_axis1Motor->getMaxRpm());
                 m_axis1Motor->goToPosition(m_multiPassRange.cutStart);
             },
              [this]() { return !m_axis1Motor->isRunning(); } });
        if (!gearAOnTooth()) {
            steps.push_back(
                { [this, tooth]() {
                     m_axis2Status = "to tooth";
                     // (At full speed, as when indexing)
                     m_axis2Motor->setSpeed(m_axis2Motor->getMaxRpm());
                     m_axis2Motor->goToPosition(tooth);
                 },
                  [this]() { return !m_axis2Motor->isRunning(); } });
        }
        MGOLOG(
            fmt::format(
                "Gear cutting: moving to the start, axis 1 to {}, axis 2 to {}",
                m_multiPassRange.cutStart,
                tooth));
        m_motionSequence.start(std::move(steps), [this, oldSpeed]() {
            m_axis1Motor->setSpeed(oldSpeed);
            m_multiPassMovingToStart = false;
            axis1SaveBreadcrumbPosition();
            axis2SaveBreadcrumbPosition();
        });
        return;
    }
    moveToSafely(
        m_multiPassRange.cutStart,
        m_multiPassRange.feedStart,
        activeMultiPassSetup().retractBeforeMoveToStart,
        "to start",
        [this]() { m_multiPassMovingToStart = false; },
        multiPassRetractTarget());
}

void Model::moveToSafely(
    double zTarget,
    double xTarget,
    bool retract,
    const std::string& status,
    std::function<void()> onDone,
    std::optional<double> retractTo)
{
    const bool zToMove = !atMultiPassPosition(m_axis1Motor->getPosition(), zTarget);
    const bool xToMove = !atMultiPassPosition(m_axis2Motor->getPosition(), xTarget);
    const double oldZSpeed = m_axis1Motor->getSpeed();
    const double oldXSpeed = m_axis2Motor->getSpeed();

    const auto zStopped = [this]() { return !m_axis1Motor->isRunning(); };
    const auto xStopped = [this]() { return !m_axis2Motor->isRunning(); };
    const MotionSequence::Step retractStep { [this, retractTo]() {
                                                if (retractTo) {
                                                    axis2RetractTo(
                                                        *retractTo, multiPassRetractClearance());
                                                } else {
                                                    axis2RetractBy(m_axis2RetractionDistance);
                                                }
                                            },
                                             xStopped };
    const MotionSequence::Step zStep { [this, zTarget, status]() {
                                          m_axis1Status = status;
                                          if (m_mode == Mode::Threading) {
                                              // (Z doesn't ramp while threading,
                                              // but this is at full speed. Off
                                              // again at the end, or on a stop)
                                              m_axis1Motor->enableRamping(true);
                                          }
                                          m_axis1Motor->setSpeed(m_axis1Motor->getMaxRpm());
                                          m_axis1Motor->goToPosition(zTarget);
                                      },
                                       zStopped };
    // (At the retract speed. Once X is moving to its target, it's no longer
    // at a retracted position, so isn't counted as retracted)
    const MotionSequence::Step xStep { [this, xTarget, status]() {
                                          m_axis2Retracted = false;
                                          m_axis2Status = status;
                                          m_axis2Motor->setSpeed(1.0); // (so it ramps)
                                          m_axis2Motor->setSpeed(100.0);
                                          m_axis2Motor->goToPosition(xTarget);
                                      },
                                       xStopped };

    // X moves away from the work before Z moves, and towards it after, so
    // that Z doesn't move with the tool deeper in than it need be. With a
    // retract (only needed if Z moves), X starts by moving away anyway
    std::vector<MotionSequence::Step> steps;
    const bool retractFirst = zToMove && retract && !m_axis2Retracted;
    const bool xAwayFromWork = (xTarget - m_axis2Motor->getPosition()) * axis2IntoWorkSign() < 0.0;
    const bool xFirst = xToMove && xAwayFromWork && !retractFirst;
    // (If X was already retracted, it's moved to its target even if it's
    // there, to count as unretracted)
    const bool xLast = !xFirst && (retractFirst || xToMove || m_axis2Retracted);
    if (retractFirst) {
        steps.push_back(retractStep);
    }
    if (xFirst) {
        steps.push_back(xStep);
    }
    if (zToMove) {
        steps.push_back(zStep);
    }
    if (xLast) {
        steps.push_back(xStep);
    }
    MGOLOG(
        fmt::format(
            "Moving {}: Z {}, X {}{}",
            status,
            zTarget,
            xTarget,
            retractFirst ? " (retracting first)" : ""));
    m_motionSequence.start(
        std::move(steps), [this, oldZSpeed, oldXSpeed, onDone = std::move(onDone)]() {
            if (m_mode == Mode::Threading) {
                m_axis1Motor->enableRamping(false);
            }
            m_axis1Motor->setSpeed(oldZSpeed);
            m_axis2Motor->setSpeed(oldXSpeed);
            if (onDone) {
                onDone();
            }
            axis1SaveBreadcrumbPosition();
            axis2SaveBreadcrumbPosition();
        });
}

void Model::multiPassGo()
{
    const bool threading = m_mode == Mode::Threading && m_threadingPassesSet;
    const bool knurling = m_mode == Mode::Knurling && getKnurlLeadMm().has_value();
    const bool gear = gearCuttingSet();
    const bool radius = radiusSet();
    if ((m_mode != Mode::MultiPass && !threading && !knurling && !gear && !radius)
        || m_multiPassMovingToStart) {
        return;
    }
    // (Every branch below may move both axes, except asking for a pause
    // during a pass, but an axis can't be locked while a pass is under way)
    if (moveRefused(true, true)) {
        return;
    }
    if (m_multiPass.state() == MultiPass::State::Paused && m_springPasses
        && (m_axis1Motor->isRunning() || m_axis2Motor->isRunning())) {
        // (e.g. Z still moving off the end)
        m_warning = "Stop the motors first";
        return;
    }
    if (m_multiPass.state() == MultiPass::State::Paused && m_springPasses && !springPassAtStart()) {
        // X moved out, or Z moved along, while paused (e.g. to try the nut
        // on): these spring passes end, and it goes back to the last pass's
        // start and depth; Enter then starts them again
        // (With no reason: it wasn't stopped by anything going wrong)
        m_multiPass.stop("");
        springPassMoveBack();
        return;
    }
    if (m_multiPass.state() == MultiPass::State::Paused) {
        MGOLOG("Multi-pass: continuing");
        m_multiPass.continueAfterPause();
        return;
    }
    if (m_multiPass.inProgress()) {
        // While a pass is under way: pause before the next (or not, if asked
        // already)
        const bool wasRequested = m_multiPass.pauseRequested();
        m_multiPass.togglePauseRequest();
        if (m_multiPass.pauseRequested() != wasRequested) {
            MGOLOG(
                (wasRequested ? "Multi-pass: pause request cancelled"
                              : "Multi-pass: pause requested"));
        }
        return;
    }
    if (m_axis1Motor->isRunning() || m_axis2Motor->isRunning()) {
        m_warning = "Stop the motors first";
        return;
    }
    if ((threading || knurling) && m_threadingPositionLost) {
        // (Choosing the thread again clears it, e.g. for new work)
        m_warning = lostPulsesPasses(m_mode);
        return;
    }
    if (threading && m_springPassX) {
        m_warning = "";
        if (springPassAtStart()) {
            startSpringPasses();
            return;
        }
        // Back to the start of the cut, at the last pass's depth
        springPassMoveBack();
        return;
    }
    if (!multiPassAtStart() || m_axis2Retracted) {
        // The first Enter moves to the start; the next starts cutting
        m_warning = "";
        multiPassMoveToStart();
        return;
    }
    m_springPasses = false;
    const MultiPassSetup& setup = activeMultiPassSetup();
    MultiPassPlan plan;
    plan.cut = threading || knurling ? MultiPassCut::Thread : MultiPassCut::Feed;
    plan.cutStart = m_multiPassRange.cutStart;
    plan.cutEnd = m_multiPassRange.cutEnd;
    plan.feedStart = m_multiPassRange.feedStart;
    plan.feedEnd = m_multiPassRange.feedEnd;
    plan.feedPerPass = std::abs(setup.feedPerPass);
    // (Knurling cuts back, with the tool in)
    plan.retract = threading || (setup.retract && !knurling);
    if (!threading && !knurling && setup.retract && setup.retractLastOnly) {
        plan.retractLastPasses = retractLastPasses();
    }
    plan.retractTo = multiPassRetractTarget();
    plan.pause = setup.pause;
    // (A thread's cut is locked to the spindle, so X can't follow Z)
    plan.feedCorrection = threading || knurling ? 0.0 : std::tan(taperCorrection() * DEG_TO_RAD);
    if (knurling) {
        // Each start there (one hand) and back (the other)
        plan.starts = setup.knurlStarts;
        plan.cutBack = true;
        plan.constantArea = setup.constantArea;
        plan.feedMinimum = m_config.readDouble("ThreadingMinimumInfeed", 0.02);
    }
    if (threading) {
        plan.constantArea = setup.constantArea;
        plan.feedMinimum = m_config.readDouble("ThreadingMinimumInfeed", 0.02);
        if (setup.compoundInfeed) {
            // Z moves along the way the cut goes, so the leading flank cuts
            const double direction = plan.cutEnd > plan.cutStart ? 1.0 : -1.0;
            plan.cutShiftPerFeed = direction * std::tan(getCompoundInfeedAngleDeg() * DEG_TO_RAD);
        }
    }
    if (gear) {
        // Each start is a tooth, cut along axis 1, with axis 2 turned on by a
        // tooth before each. The depth is fed in by hand before each pass
        // (once round the blank), so there's no retract
        plan.feedPerPass = m_gearSetup.depthPerPass;
        plan.feedManual = true;
        plan.retract = false;
        plan.pause = false;
        plan.feedCorrection = 0.0;
        plan.starts = m_gearSetup.teeth;
        plan.indexPerStart = 360.0 / m_gearSetup.teeth;
        // (On the tooth: see multiPassAtStart())
        plan.indexStart = gearStartTooth();
    }
    if (radius) {
        // Each pass is an arc, as a series of tapers. The return is along
        // the work's diameter, which is clear of the work once the pass has
        // cut (the radius is all inside it), so there's no retract
        plan.paths = m_radiusPasses;
        plan.feedPerPass = m_radiusSetup.depthPerPass;
        plan.retract = false;
        plan.pause = false;
        plan.feedCorrection = 0.0;
    }
    // (Its first pass sets which way the spindle must turn for the rest)
    m_passesSpindleSign.reset();
    if (const std::string error = m_multiPass.start(plan); !error.empty()) {
        m_warning = error;
        return;
    }
    m_warning = "";
    if (gear && !m_gearToothOrigin) {
        // (The teeth stay here: see setGearSetup())
        m_gearToothOrigin = plan.indexStart;
    }
    MGOLOG(
        fmt::format(
            "{} started: Z {} to {}, X {} to {} by {}{}{}{}{}{}{} ({} passes)",
            threading      ? "Threading passes"
                : knurling ? "Knurling passes"
                : gear     ? "Gear cutting"
                : radius
                ? fmt::format("Radius {} (nose {})", m_radiusSetup.radius, m_radiusSetup.noseRadius)
                : "Multi-pass",
            plan.cutStart,
            plan.cutEnd,
            plan.feedStart,
            plan.feedEnd,
            plan.feedPerPass,
            plan.retract ? (plan.retractLastPasses > 0
                                ? fmt::format(", retract on the last {}", plan.retractLastPasses)
                                : std::string(", retract"))
                         : std::string(),
            plan.pause ? ", pause" : "",
            plan.feedCorrection != 0.0 ? fmt::format(", taper correction {} deg", taperCorrection())
                                       : "",
            plan.constantArea ? fmt::format(", constant area (at least {})", plan.feedMinimum) : "",
            plan.cutShiftPerFeed != 0.0 ? fmt::format(", Z shift {} per X", plan.cutShiftPerFeed)
                                        : "",
            knurling
                ? fmt::format(", {} starts there and back, lead {}", plan.starts, m_knurlLeadMm)
                : gear ? fmt::format(
                             ", {} teeth, indexing {} from {}",
                             plan.starts,
                             plan.indexPerStart,
                             plan.indexStart)
                       : "",
            m_multiPass.totalPasses()));
}

const MultiPass& Model::multiPass() const
{
    return m_multiPass;
}

double Model::cutPosition() const
{
    return m_axis1Motor->getPosition();
}

bool Model::cutMoving() const
{
    // (Including taking up the backlash first, and X catching up with Z)
    return m_axis1WasRunning || m_motionSequence.isRunning() || m_multiPassXFollowing
        || m_multiPassXSettling;
}

void Model::cutMoveTo(double position, double feedFollows)
{
    m_axis1Status = "multi-pass";
    if (spindleSynchronised()) {
        // A threading multi-pass's return (retracted, so not locked to the
        // spindle). Z doesn't ramp while threading, but this is at full
        // speed, so it does here. (cutThreadTo() turns it off again)
        m_axis1Motor->enableRamping(true);
    }
    // (While retracted, X doesn't follow: the correction was taken off where
    // it unretracts to when it retracted. See retract())
    if (feedFollows == 0.0 || m_axis2Retracted) {
        m_axis1Motor->goToPosition(position);
        // (So a move that's over by the next update is still seen to have ended)
        m_axis1WasRunning = m_axis1Motor->isRunning();
        return;
    }
    // As for a taper (the backlash taken up first, so X moves as soon as Z
    // does). X's speed is restored once it's caught up (see checkStatus())
    m_multiPassXFollowing = true;
    m_previousAxis2Speed = m_axis2Motor->getSpeed();
    if (atMultiPassPosition(m_axis1Motor->getPosition(), m_multiPass.plan().cutStart)) {
        // A cut (not a return): where X is, so it can unretract back to it
        m_multiPassCutStartXStep = m_axis2Motor->getCurrentStep();
    }
    const long step = std::lround(position / m_axis1Motor->getConversionFactor());
    m_motionSequence.start(xFollowsZSteps(step, feedFollows, [this, step]() {
        m_axis1Motor->goToStep(step);
        m_axis1WasRunning = true;
    }));
}

double Model::cutSpeed() const
{
    return m_axis1Motor->getSpeed();
}

void Model::setCutSpeed(double speed)
{
    m_axis1Motor->setSpeed(speed);
}

double Model::cutMaxSpeed() const
{
    return m_axis1Motor->getMaxRpm();
}

double Model::cutStepSize() const
{
    return std::abs(m_axis1Motor->getConversionFactor());
}

double Model::feedStepSize() const
{
    return std::abs(m_axis2Motor->getConversionFactor());
}

std::string Model::cutThreadTo(double position, double revolutionOffset)
{
    if (!spindleSynchronised()) {
        return "Threading passes are only in threading or knurling mode";
    }
    if (const auto error = threadingPassError(); !error.message.empty()) {
        setThreadingWarning(error.message, error.sticky);
        return error.message;
    }
    m_axis1Status = m_mode == Mode::Knurling ? "knurling" : "threading";
    // (Z mustn't ramp while it follows the spindle: the return turned it on,
    // and in knurling mode, it's on but for the passes)
    m_axis1Motor->enableRamping(false);
    // As a threading pass by hand: Z's backlash is taken up first if it
    // reverses (it does, after the return), then the pass waits for the
    // spindle's next whole revolution (plus the offset)
    auto steps = threadingPassSteps(
        std::lround(position / m_axis1Motor->getConversionFactor()), revolutionOffset);
    steps.back().action = [this, move = std::move(steps.back().action)]() {
        move();
        // (So a pass that's over by the next update is still seen to have
        // ended, as in cutMoveTo())
        m_axis1WasRunning = m_axis1Motor->isRunning();
    };
    m_motionSequence.start(std::move(steps));
    return {};
}

double Model::feedPosition() const
{
    return getAxis2Position();
}

bool Model::feedMoving() const
{
    return m_axis2WasRunning;
}

void Model::feedMoveTo(double position)
{
    m_axis2Motor->goToPosition(position + multiPassXCorrection());
    m_axis2WasRunning = m_axis2Motor->isRunning();
}

void Model::retract(std::optional<double> to)
{
    const double correction = multiPassXCorrection();
    const bool correcting = correction != 0.0;
    if (to) {
        // (Where the work's surface is here, if X has followed Z to correct a
        // taper)
        axis2RetractTo(*to + correction, multiPassRetractClearance());
    } else {
        axis2RetractBy(m_axis2RetractionDistance);
    }
    if (m_axis2Retracted && correcting) {
        // Rather than follow Z back while retracted, X unretracts to where
        // it would have been if it had: where the cut started (see
        // cutMoveTo()). (Not by the correction the cut should have made: X
        // can lag Z a little, so it may have moved slightly less)
        m_xOldPosition = m_multiPassCutStartXStep;
    }
    m_axis2WasRunning = m_axis2Motor->isRunning();
}

void Model::unretract()
{
    axis2Unretract();
    m_axis2WasRunning = m_axis2Motor->isRunning();
}

void Model::multiPassXFollowEnded()
{
    // After following Z back to the start, X should be where the cut
    // started. It can lag Z a little as it starts following (more so on the
    // cut than the return), so may be a step or so short: if so, it finishes
    // the move, so that nothing builds up from pass to pass
    if (m_axis2Retracted || !m_multiPass.inProgress()
        || !atMultiPassPosition(m_axis1Motor->getPosition(), m_multiPass.plan().cutStart)
        || m_axis2Motor->getCurrentStep() == m_multiPassCutStartXStep) {
        return;
    }
    MGOLOG(
        fmt::format(
            "Multi-pass: X ended the return at step {}, not {}: moving there",
            m_axis2Motor->getCurrentStep(),
            m_multiPassCutStartXStep));
    m_axis2Motor->goToStep(m_multiPassCutStartXStep);
    m_multiPassXSettling = true;
    m_axis2WasRunning = true;
}

double Model::taperCorrection() const
{
    return m_config.readDouble("TaperCorrection", 0.0);
}

int Model::retractLastPasses() const
{
    return std::max(1, static_cast<int>(m_config.readLong("MultiPassRetractLastPasses", 2)));
}

double Model::multiPassRetractClearance() const
{
    // (Never zero or less: a retract must move clear of the surface)
    const double clearance = m_config.readDouble("MultiPassRetractClearance", 0.25);
    return clearance > 0.0 ? clearance : 0.25;
}

double Model::multiPassRetractTarget() const
{
    // The start is where the tool touched the work (for threading, the
    // thread's crests are there too), so just outside it is clear of all of
    // it, however deep the passes have gone. Inside a bore, the back of a
    // boring bar is then only the clearance nearer the far side than it was
    // at the start
    return m_multiPassRange.feedStart - axis2IntoWorkSign() * multiPassRetractClearance();
}

double Model::multiPassXCorrection() const
{
    if (!m_multiPass.inProgress() || m_axis2Retracted) {
        return 0.0;
    }
    return m_multiPass.plan().feedCorrection
        * (m_axis1Motor->getPosition() - m_multiPass.plan().cutStart);
}

bool Model::isRetracted() const
{
    return m_axis2Retracted;
}

double Model::indexPosition() const
{
    return m_axis2Motor->getPosition();
}

bool Model::indexMoving() const
{
    // (Including backing off first)
    return m_axis2WasRunning || m_motionSequence.isRunning();
}

void Model::indexMoveTo(double position)
{
    m_axis2Status = "indexing";
    // Forwards is towards larger positions, which may be fewer steps
    const double conversion = m_axis2Motor->getConversionFactor();
    const long forwards = conversion > 0.0 ? 1 : -1;
    const long target = std::lround(position / conversion);
    const long current = m_axis2Motor->getCurrentStep();
    MGOLOG(fmt::format("Indexing to {} (step {})", position, target));
    // (At full speed, as for a return: the cutter is clear of the blank. A
    // set speed would do, but isn't kept: a fast move resets it, between
    // runs)
    m_axis2Motor->setSpeed(m_axis2Motor->getMaxRpm());
    if ((target - current) * forwards > 0) {
        m_axis2Motor->goToStep(target);
        // (So a move that's over by the next update is still seen to have ended)
        m_axis2WasRunning = m_axis2Motor->isRunning();
        return;
    }
    // Already there (the first tooth), or (never, in gear cutting) behind:
    // back off and come forwards to it, so the table's backlash is taken up
    // the same way as for every other tooth, however it last moved (which
    // includes any backlash compensation). A degree is plenty: it only has
    // to be more than a step, as the motor takes up the backlash it's set to
    const long backOff = std::max(1L, std::lround(1.0 / std::abs(conversion)));
    const auto stopped = [this]() { return !m_axis2Motor->isRunning(); };
    m_motionSequence.start(
        { { [this, target, backOff, forwards]() {
               m_axis2Motor->goToStep(target - forwards * backOff);
           },
            stopped },
          { [this, target]() { m_axis2Motor->goToStep(target); }, stopped } });
}

double Model::indexStepSize() const
{
    return std::abs(m_axis2Motor->getConversionFactor());
}

void Model::gearCuttingFinished()
{
    // (Whole turns only: it ends a whole number of turns from the start, but
    // within a step of it)
    const double turns
        = std::round((m_axis2Motor->getPosition() - m_multiPass.plan().indexStart) / 360.0);
    if (turns == 0.0) {
        return;
    }
    const long shift = std::lround(turns * 360.0 / m_axis2Motor->getConversionFactor());
    const long step = m_axis2Motor->getCurrentStep() - shift;
    // (The motor's backlash is unchanged: it's still taken up forwards.) The
    // memory slots are left as they are: whole turns don't change where on
    // the table a slot is, and a slot stored before the passes is then
    // that position again, not whole turns behind
    m_axis2Motor->setPosition(m_axis2Motor->getPosition(step));
    // (Going back to them would take whole turns)
    axis2ClearBreadcrumbs();
    MGOLOG(
        fmt::format(
            "Gear cutting finished: {} {} turn(s), so its position is back to {}",
            m_config.read("Axis2Label", "X"),
            turns,
            m_axis2Motor->getPosition()));
}

void Model::startSpringPasses()
{
    // The same cut, repeated at X's position (where the last pass was, or
    // nudged in from there), pausing after each. (As a multi-pass with a
    // step-over of zero)
    const double x = getAxis2Position();
    MultiPassPlan plan;
    plan.cut = MultiPassCut::Thread;
    // (Where the last pass started, which a compound infeed moves along)
    plan.cutStart = m_springPassZ;
    plan.cutEnd = m_multiPassRange.cutEnd;
    plan.feedStart = x;
    plan.feedEnd = x;
    plan.feedPerPass = 0.0;
    plan.retract = true;
    // (Clear of the thread's crests: the threading passes' start, not where
    // the spring passes cut)
    plan.retractTo = multiPassRetractTarget();
    plan.pause = true;
    if (const std::string error = m_multiPass.start(plan); !error.empty()) {
        m_warning = error;
        return;
    }
    m_springPasses = true;
    m_springPassX = x;
    MGOLOG(fmt::format("Spring passes started: Z {} to {}, X {}", plan.cutStart, plan.cutEnd, x));
}

bool Model::springPassesAvailable() const
{
    return m_mode == Mode::Threading && m_threadingPassesSet && m_springPassX.has_value();
}

bool Model::springPasses() const
{
    return m_springPasses;
}

bool Model::springPassAtStart() const
{
    if (!m_springPassX || m_axis2Retracted
        || !atMultiPassPosition(m_axis1Motor->getPosition(), m_springPassZ)) {
        return false;
    }
    // X at the last pass's depth, or deeper (nudged in on purpose). Not
    // shallower: e.g. moved out to try the nut on, so it goes back first.
    // (Within half a step, as it can only stop on a whole one)
    const double deeper = (getAxis2Position() - *m_springPassX) * axis2IntoWorkSign();
    return deeper > -(MultiPass::TOLERANCE + std::abs(m_axis2Motor->getConversionFactor()) / 2.0);
}

void Model::springPassMoveBack()
{
    MGOLOG(fmt::format("Spring passes: moving back to Z {}, X {}", m_springPassZ, *m_springPassX));
    m_multiPassMovingToStart = true;
    moveToSafely(
        m_springPassZ,
        *m_springPassX,
        activeMultiPassSetup().retractBeforeMoveToStart,
        "to start",
        [this]() { m_multiPassMovingToStart = false; },
        multiPassRetractTarget());
}

void Model::multiPassFinished()
{
    if (m_multiPass.indexes()) {
        gearCuttingFinished();
        return;
    }
    if (m_multiPass.plan().cut == MultiPassCut::Thread && m_mode == Mode::Threading) {
        // Spring passes can follow, at this depth
        m_springPassX = getAxis2Position();
        // (Where Z is: a shifted start is seldom a whole number of steps)
        m_springPassZ = m_axis1Motor->getPosition();
    }
    // If it cut from the work's end, the feature's diameter is now the one it
    // finished at. (Not if it cut part way along, which would leave the
    // diameter at the end as it was: a start from the feature assumes the
    // diameter runs from the end)
    // (Not for a thread: its depth isn't a surface's diameter)
    // (Nor for paths, e.g. a radius, which doesn't change the diameter)
    const MultiPassPlan& plan = m_multiPass.plan();
    if (!m_workEndZ || !m_xDiameterSet || plan.feedManual || plan.cut == MultiPassCut::Thread
        || !plan.paths.empty()) {
        return;
    }
    const double direction = plan.cutEnd > plan.cutStart ? 1.0 : -1.0;
    if ((plan.cutStart - *m_workEndZ) * direction <= MultiPass::TOLERANCE) {
        m_featureX = plan.feedEnd;
        MGOLOG(fmt::format("Feature diameter now {}", *getFeatureDiameterMm()));
    }
}

} // namespace mgo
