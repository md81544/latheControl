#include "model.h"
#include "keycodes.h"
#include "linearscale.h"
#include "log.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <ctime>
#include <functional>
#include <sstream>
#include <utility>

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
    // (See StepperMotor::setBacklashTakeUpSpeed())
    if (const double speed = config.readDouble(axisPrefix + "BacklashTakeUpSpeed", 0.0);
        speed > 0.0) {
        motor.setBacklashTakeUpSpeed(speed);
        MGOLOG(fmt::format("{} takes up its backlash at {} per minute", axisPrefix, speed));
    }
    // (See StepperMotor::setDecelerationRamping())
    if (config.readBool(axisPrefix + "DecelerationRamping", false)) {
        const double rate = config.readDouble(
            axisPrefix + "DecelerationRampingSpeed",
            config.readDouble(axisPrefix + "RampingSpeed", 100.0));
        motor.setDecelerationRamping(rate);
        MGOLOG(fmt::format("{} slows down before stopping (ramping speed {})", axisPrefix, rate));
    }
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
        case mgo::Mode::GearCutting:
            return "GearCutting";
        case mgo::Mode::BoltHoles:
            return "BoltHoles";
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

// a / b, rounded up (towards plus infinity, even if a is negative; b > 0)
int64_t ceilDiv(int64_t a, int64_t b)
{
    return a / b + (a % b > 0 ? 1 : 0);
}

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
    // (The mock GPIO's quadrature scale follows this motor)
    usingMockLinearScale = m_config.read("Axis1Scale", "none") == "quadrature";
#endif
    axisState(1).motor = std::make_unique<mgo::StepperMotor>(
        m_gpio,
        m_config.readLong("Axis1GpioStepPin", 8),
        m_config.readLong("Axis1GpioReversePin", 7),
        m_config.readLong("Axis1GpioEnablePin", 0),
        axis1StepsPerRevolution,
        axis1ConversionFactor,
        maxAxis1Rpm,
        m_config.readDouble("Axis1RampingSpeed", 100.0),
        usingMockLinearScale,
        std::lround(m_config.readDouble("Axis1ScaleCountsPerMm", 200.0)));
    if (!motor(1)->isRunningRealTimeScheduled()) {
        MGOLOG("*** Warning *** axis1 steppermotor thread not running real-time");
    }
    configureAlarm(*motor(1), m_config, "Axis1");
    configureLimitSwitch(*motor(1), m_config);
    configureMotorThread(*motor(1), m_config, "Axis1");

    double axis2ConversionFactor = m_config.readDouble("Axis2ConversionNumerator", -1.0)
        / m_config.readDouble("Axis2ConversionDivisor", 1'000.0);
    double maxAxis2Rpm = m_config.readDouble("Axis2MaxMotorRpm", 1'000.0);
    long axis2StepsPerRevolution = m_config.readLong("Axis2StepsPerRev", 800);
    axisState(2).motor = std::make_unique<mgo::StepperMotor>(
        m_gpio,
        m_config.readLong("Axis2GpioStepPin", 20),
        m_config.readLong("Axis2GpioReversePin", 21),
        m_config.readLong("Axis2GpioEnablePin", 0),
        axis2StepsPerRevolution,
        axis2ConversionFactor,
        maxAxis2Rpm,
        m_config.readDouble("Axis2RampingSpeed", 100.0));
    if (!motor(2)->isRunningRealTimeScheduled()) {
        MGOLOG("*** Warning *** axis2 steppermotor thread not running real-time");
    }
    configureAlarm(*motor(2), m_config, "Axis2");
    configureLimitSwitch(*motor(2), m_config);
    if (const std::string pin = m_config.read("LimitSwitchPin", ""); !pin.empty()) {
        MGOLOG(
            fmt::format(
                "Limit switches on GPIO {}, active {}",
                pin,
                m_config.read("LimitSwitchActiveLevel", "high")));
    }
    configureMotorThread(*motor(2), m_config, "Axis2");

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
    if (toolLibraryEnabled()) {
        const std::string toolsFile = m_config.read("ToolLibraryFile", "tools.cfg");
        std::vector<std::string> errors;
        m_toolLibrary = readToolLibrary(toolsFile, errors);
        for (const auto& e : errors) {
            MGOLOG("*** Warning *** " << e);
        }
        if (!errors.empty()) {
            m_warning = "Some of the tool library couldn't be read (see lc.log)";
        }
        // (Positions are as at start-up, set with the tool in use)
        axisState(1).positionsTool = m_toolLibrary.current().number;
        axisState(2).positionsTool = m_toolLibrary.current().number;
        MGOLOG(
            fmt::format(
                "{} tools read from {}; T{} is current",
                m_toolLibrary.tools().size(),
                toolsFile,
                m_toolLibrary.current().number));
    }

    configureScale(1);
    configureScale(2);
    configureManualAxes();

    // We need to ensure that the motors are in a known position with regard to
    // backlash - which means moving them initially by the amount of
    // configured backlash compensation to ensure any backlash is taken up
    // This initial movement will be small but this could cause an issue if
    // the tool is against work already - maybe TODO something here?
    // The speeds must be set first: a motor at zero speed cancels any move.
    // (They used to be set after this, so until 2026-11-14 the backlash was
    // never taken up, though the motors were told it had been. The mill's
    // X scale showed it: its first move after starting was short by the
    // backlash, if the last session's last move had been the other way)
    motor(1)->setSpeed(m_config.readDouble("Axis1SpeedPreset2", 40.0));
    motor(2)->setSpeed(m_config.readDouble("Axis2SpeedPreset2", 20.0));
    unsigned long zBacklashCompensation = m_config.readLong("Axis1BacklashCompensationSteps", 0);
    unsigned long xBacklashCompensation = m_config.readLong("Axis2BacklashCompensationSteps", 0);
    MGOLOG(
        fmt::format(
            "Taking up the backlash: {} {} steps, {} {} steps",
            axisLabel(1),
            zBacklashCompensation,
            axisLabel(2),
            xBacklashCompensation));
    axis1GoToStep(zBacklashCompensation);
    motor(1)->setBacklashCompensation(zBacklashCompensation, zBacklashCompensation);
    motor(2)->goToStep(xBacklashCompensation);
    motor(2)->setBacklashCompensation(xBacklashCompensation, xBacklashCompensation);
    motor(1)->wait();
    motor(2)->wait();
    // re-zero after that:
    motor(1)->zeroPosition();
    motor(2)->zeroPosition();

    saveBreadcrumbPosition(1);
    saveBreadcrumbPosition(2);
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
    logMotorMovement(*motor(1), axisState(1).motorLog, axisLabel(1));
    logMotorMovement(*motor(2), axisState(2).motorLog, axisLabel(2));
    for (const int n : { 1, 2 }) {
        if (AxisScale* sc = scale(n)) {
            StepperMotor* m = motor(n);
            sc->update(m->getPosition(), m->isRunning(), m->getSpeed());
            logScaleNoise(n);
        }
    }
    for (auto& axis : m_manualAxes) {
        if (axis) {
            axis->update();
        }
    }
    checkManualAxes();
    checkBoltHoleDepth();
    advanceBacklashMeasurement();
    checkScaleAtRest(1);
    checkScaleAtRest(2);
    advanceFinish(1);
    advanceFinish(2);
    advanceApproach(1);
    advanceApproach(2);

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
    if (!motor(2)->isRunning()) {
        axisState(2).status = "stopped";
    }
    if (!motor(1)->isRunning()) {
        axisState(1).status = "stopped";
        if (axisState(1).wasRunning) {
            // We see that axis1 has stopped. We save the position
            // in case the user wants to return to it without
            // explicitly having saved it.
            saveBreadcrumbPosition(1);
        }
        if (axisState(1).fastReturning || axisState(1).rapidInProgress) {
            motor(1)->setSpeed(axisState(1).previousSpeed);
            axisState(1).fastReturning = false;
            axisState(1).rapidInProgress = false;
        }
        // X has caught up with where Z stopped (see below) once it's been
        // seen stopped on two updates in a row: it can be stopped for a
        // moment between its synchronised moves while still behind
        if (m_synchronisedCutEnding && motor(2)->isRunning()) {
            m_synchronisedCutXStoppedOnce = false;
        } else if (m_synchronisedCutEnding && !m_synchronisedCutXStoppedOnce) {
            m_synchronisedCutXStoppedOnce = true;
        } else if (m_synchronisedCutEnding) {
            // Turn off synchronisation *before* setting the speed, otherwise
            // the motor thread may overwrite our speed with a synchronised one
            m_synchronisedCutEnding = false;
            axis2SynchroniseOff();
            motor(2)->setSpeed(axisState(2).previousSpeed);
            if (m_multiPassXFollowing) {
                m_multiPassXFollowing = false;
                multiPassXFollowEnded();
            }
        }
        if ((m_mode == Mode::Taper || m_multiPassXFollowing) && axisState(1).wasRunning) {
            // The end of a taper cut. X follows Z in small updates, so may
            // still be a few steps behind: leave it synchronised until it's
            // stopped too (on a later update, by when it's had Z's final
            // position), so it ends exactly on the taper
            m_synchronisedCutEnding = true;
            m_synchronisedCutXStoppedOnce = false;
        }
        // (Not during a multi-pass, which sets the speeds itself: the cut
        // speed must be kept from one pass to the next. Nor while measuring
        // the backlash, which puts the speed back when it ends)
        if (axisState(1).wasRunning && !m_multiPass.inProgress() && !m_backlashMeasurement
            && motor(1)->getSpeed() > m_config.readDouble("Axis1SpeedResetAbove", 100.0)) {
            // We don't allow faster speeds to "stick" to avoid accidental
            // fast motion after a long fast movement
            motor(1)->setSpeed(m_config.readDouble("Axis1SpeedResetTo", 40.0));
        }
        axisState(1).wasRunning = false;
    } else {
        axisState(1).wasRunning = true;
    }

    if (!motor(2)->isRunning()) {
        axisState(2).status = "stopped";
        if (axisState(2).wasRunning) {
            saveBreadcrumbPosition(2);
        }
        m_multiPassXSettling = false;
        if (m_axis2RetractMoveInProgress) {
            // (If this shows the target was reached, but the tool didn't move
            // that far, the motor stalled. If it stopped short, it was stopped)
            MGOLOG(fmt::format("X retract/unretract ended at step {}", motor(2)->getCurrentStep()));
            m_axis2RetractMoveInProgress = false;
        }
        if (m_fastRetracting) {
            motor(2)->setSpeed(axisState(2).previousSpeed);
            m_fastRetracting = false;
            m_axis2Retracted = false;
        }
        if (axisState(2).fastReturning || axisState(2).rapidInProgress) {
            motor(2)->setSpeed(axisState(2).previousSpeed);
            axisState(2).fastReturning = false;
            axisState(2).rapidInProgress = false;
        }
        if (!(m_mode == Mode::Taper) && axisState(2).wasRunning && !m_multiPass.inProgress()
            && motor(2)->getSpeed() >= m_config.readDouble("Axis2SpeedResetAbove", 80.0)) {
            // We don't allow faster speeds to "stick" to avoid accidental
            // fast motion after a long fast movement
            motor(2)->setSpeed(m_config.readDouble("Axis2SpeedResetTo", 20.0));
        }
        axisState(2).wasRunning = false;
    } else {
        axisState(2).wasRunning = true;
    }
    if (m_mode == Mode::Taper) {
        axisState(2).status = "synchronised";
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
            motor(1)->enableRamping(false);
        }
        if (!m_multiPass.inProgress() && m_mode == Mode::Knurling) {
            // (Its passes turn it off)
            motor(1)->enableRamping(true);
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

void Model::logScaleNoise(int axisNumber)
{
    // At most every 10 seconds, so that a noisy scale doesn't flood lc.log
    ScaleState& state = scaleState(axisNumber);
    const uint32_t events = state.scale->noiseEvents();
    const auto now = std::chrono::steady_clock::now();
    if (events == state.noiseEventsLogged || now - state.noiseLoggedAt < std::chrono::seconds(10)) {
        return;
    }
    MGOLOG(
        fmt::format(
            "{} scale: {} interference spikes (both pins changing at once) ignored since the "
            "last report, {} in all",
            axisLabel(axisNumber),
            events - state.noiseEventsLogged,
            events));
    state.noiseEventsLogged = events;
    state.noiseLoggedAt = now;
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
        || motor(1)->isRunning() || motor(2)->isRunning();
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
    if (motor(1)->isFaulted() && !axisState(1).faulted) {
        axisState(1).faulted = true;
        newFault = true;
        clearWorkEnd(); // (the position's lost)
        MGOLOG("*** Axis1 motor fault: driver alarm (e.g. stalled); position lost");
    }
    if (motor(2)->isFaulted() && !axisState(2).faulted) {
        axisState(2).faulted = true;
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

void Model::clearMotorFault(int axisNumber)
{
    // Called when an axis is re-zeroed or its position set, which re-establishes
    // the position lost in a fault. The motor will only clear its fault if its
    // driver is no longer in alarm
    bool& faulted = axisState(axisNumber).faulted;
    if (!faulted) {
        return;
    }
    const std::string label = axisLabel(axisNumber);
    if (motor(axisNumber)->clearFault()) {
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
    if (axisState(1).faulted) {
        add(axisLabel(1));
    }
    if (axisState(2).faulted) {
        add(axisLabel(2));
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
    // (Measuring a tool needs no mode: see startToolMeasurement())
    if (mode != Mode::None) {
        cancelToolMeasurement();
    }
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
        motor(1)->enableRamping(false);
        motor(2)->enableRamping(true);
    } else if (mode == Mode::Taper) {
        // X is synchronised with Z, so neither can ramp
        motor(1)->enableRamping(false);
        motor(2)->enableRamping(false);
    } else {
        motor(1)->enableRamping(true);
        motor(2)->enableRamping(true);
    }
    if (m_mode == Mode::Taper && mode != Mode::Taper) {
        motor(2)->setSpeed(m_taperPreviousXSpeed);
    }
    if (spindleSynchronised() && mode != m_mode) {
        // Z's speed was set for threading passes: back to something sane
        motor(1)->setSpeed(m_config.readDouble("Axis1SpeedPreset2", 40.0));
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
    m_boltHolesSet = false;
    m_boltHoleDepthPending = false;
    setThreadingWarning("", false);
    m_mode = mode;
    m_input = "";

    if (mode == Mode::Taper) {
        m_taperPreviousXSpeed = motor(2)->getSpeed();
        if (m_taperAngle != 0.0) {
            m_input = convertToString(m_taperAngle, 4);
        }
    }
}

void Model::stopAllMotors()
{
    motor(1)->stop();
    motor(2)->stop();
    motor(1)->wait();
    motor(2)->wait();
    if (m_backlashMeasurement) {
        endBacklashMeasurement("stopped");
    }
    for (const int n : { 1, 2 }) {
        if (scaleState(n).finish) {
            endFinish(n, "stopped");
        }
    }
    endApproach(1);
    endApproach(2);
    m_returnApproachRebased = false;
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
        motor(2)->setSpeed(axisState(2).previousSpeed);
    }
    m_multiPass.stop("stopped");
    if (m_mode == Mode::Threading) {
        // (In case a threading multi-pass was stopped during a return, which
        // turns it on)
        motor(1)->enableRamping(false);
    }
    if (m_mode == Mode::Knurling) {
        // (In case a pass was stopped, which turns it off)
        motor(1)->enableRamping(true);
    }
    axisState(1).status = "stopped";
    axisState(2).status = "stopped";
#ifdef FAKE
    m_gpio.scaleStop();
#endif
}

MotionSequence::Step Model::takeUpZBacklashStep(ZDirection direction)
{
    return { [this, direction]() {
                stopAxis(1);
                if (direction == ZDirection::Right) {
                    motor(1)->goToStep(motorStep(1) - 1);
                } else if (direction == ZDirection::Left) {
                    motor(1)->goToStep(motorStep(1) + 1);
                }
            },
             [this]() { return !motor(1)->isRunning(); } };
}

MotionSequence::Step Model::takeUpXBacklashStep(int stepAdd)
{
    // As this is done just before the Z motor starts moving, we take up any
    // backlash in X first (making sure X isn't already running)
    return { [this, stepAdd]() {
                axis2SynchroniseOff();
                motor(2)->stop();
                motor(2)->setSpeed(100.0);
                motor(2)->goToStep(motor(2)->getCurrentStep() + stepAdd);
            },
             [this]() { return !motor(2)->isRunning(); } };
}

void Model::synchroniseXForTaper(double ratio)
{
    m_synchronisedCutEnding = false; // (a new cut)
    motor(2)->synchroniseOn(
        motor(1), [ratio](double zPosDelta, double) -> double { return zPosDelta * ratio; });
}

double Model::axis2MaxSpeed() const
{
    // (As threadingMaxZSpeed(), with the steps per revolution as initialise())
    return motor(2)->getMaxRpm() * m_config.readLong("Axis2StepsPerRev", 800)
        * std::abs(motor(2)->getConversionFactor());
}

std::vector<MotionSequence::Step>
Model::threadingPassSteps(long step, std::vector<double> revolutionOffsets)
{
    const auto move = [this, step, offsets = std::move(revolutionOffsets)]() {
        axis1StartThreadingPass(step, offsets);
    };
    if (step == motorStep(1)) {
        return { { move, {} } };
    }
    // A threading pass follows the spindle from the moment it starts, so Z's
    // backlash must be taken up first. Otherwise, when Z reverses, the
    // spindle moves on while Z takes it up: Z starts late, and (if the
    // backlash is more than half the pitch) falls so far behind that the
    // pass is stopped
    // (At a set speed: Z's speed is whatever the last pass needed)
    auto takeUp = takeUpZBacklashStep(step < motorStep(1) ? ZDirection::Right : ZDirection::Left);
    takeUp.action = [this, action = std::move(takeUp.action)]() {
        motor(1)->setSpeed(motor(1)->getMaxRpm() / 10.0);
        action();
    };
    return { std::move(takeUp), { move, {} } };
}

std::vector<MotionSequence::Step> Model::axis1MoveSteps(long step)
{
    if (m_mode == Mode::Threading) {
        return threadingPassSteps(step, { 0.0 });
    }
    const auto move = [this, step]() { motor(1)->goToStep(step); };
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
    const ZDirection direction = step < motorStep(1) ? ZDirection::Right : ZDirection::Left;
    // X's backlash is taken up the way it's about to move. X moves ratio
    // times as far as Z in mm (see synchroniseXForTaper()), so which way that
    // is in X's steps depends on both axes' conversion factors, which may
    // have either sign
    const double zMm = (step - motorStep(1)) * motor(1)->getConversionFactor();
    const double xSteps = ratio * zMm / motor(2)->getConversionFactor();
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
    return motor(1)->getMaxRpm() * m_config.readLong("Axis1StepsPerRev", 1'000)
        * std::abs(motor(1)->getConversionFactor());
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
    if (motor(1)->isFollowing() && m_threadingTarget) {
        // If the spindle has turned well away from the pass's start angle,
        // rather than towards it (e.g. it's turning the other way), the pass
        // would wait for ever
        const double towardsStart = (m_rotaryEncoder->getSpindlePosition() - m_threadingStartPulse)
            * m_threadingSpindleSign;
        if (towardsStart
            < -PASS_WRONG_WAY_REVOLUTIONS * m_rotaryEncoder->getPulsesPerSpindleRev()) {
            motor(1)->stop();
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
        const long lag = std::abs(m_threadingTarget() - motor(1)->getCurrentStep());
        if (lag > m_threadingMaxLagSteps) {
            motor(1)->stop();
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
        if (!motor(1)->isRunning() && speed > startLimit) {
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

bool Model::axis1StartThreadingPass(long endStep, const std::vector<double>& revolutionOffsets)
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
    const long startStep = motor(1)->getCurrentStep();
    if (endStep == startStep) {
        return false;
    }
    setThreadingWarning("", false);

    // Z is locked to the spindle's position, as if geared to it: Z's target is
    // the start, plus the pitch for every revolution since the pass started.
    // Every pass must start at the same point of the spindle's rotation, so
    // it starts at a whole number of revolutions of the encoder's count, plus
    // the offset (for one of several starts): the first such point at least
    // an eighth of a revolution away (so it can't have passed by the time the
    // motor thread first looks). With several offsets, whichever comes first
    const int64_t pulsesPerRev = m_rotaryEncoder->getPulsesPerSpindleRev();
    // (Known: see threadingPassError())
    const int spindleSign = *spindleTurningSign();
    if (m_multiPass.inProgress()) {
        m_passesSpindleSign = spindleSign;
    }
    const int64_t margin = pulsesPerRev / 8;
    const int64_t now = m_rotaryEncoder->getSpindlePulses();
    // (Counting the way the spindle turns, so the count goes up)
    const int64_t from = spindleSign * now + margin;
    int64_t startPulse = 0;
    std::optional<std::size_t> chosen;
    for (std::size_t n = 0; n < revolutionOffsets.size(); ++n) {
        const int64_t offset
            = std::lround(revolutionOffsets[n] * static_cast<double>(pulsesPerRev));
        // The first whole revolution, plus the offset, at or after from
        const int64_t revolutions = ceilDiv(from - offset, pulsesPerRev);
        const int64_t start = revolutions * pulsesPerRev + offset;
        if (!chosen || start < startPulse) {
            startPulse = start;
            chosen = n;
        }
    }
    if (!chosen) {
        return false;
    }
    startPulse *= spindleSign;
    m_threadStartChosen = chosen;
    const double stepsPerRev = pitch / std::abs(motor(1)->getConversionFactor());
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
    motor(1)->setSpeed(std::min(maxSpeed, 2.0 * nominalSpeed));
    motor(1)->followTo(endStep, m_threadingTarget);
    if (!motor(1)->isFollowing()) {
        return false;
    }
    m_threadingMoveInProgress = true;
    m_threadingMoveTarget = endStep;
    m_threadingMoveStartedRetracted = m_axis2Retracted;
    MGOLOG(
        fmt::format(
            "{} pass: Z from step {} to {} at {:.0f} rpm, from spindle pulse {} (now {}, "
            "turning {}, offset {:.4g} rev of {} offered), X {}",
            m_mode == Mode::Knurling ? "Knurling" : "Threading",
            startStep,
            endStep,
            rpm,
            startPulse,
            now,
            spindleSign > 0 ? "+" : "-",
            revolutionOffsets[*chosen],
            revolutionOffsets.size(),
            m_axis2Retracted ? "retracted" : "in"));
    return true;
}

void Model::threadingCheckMoveEnded()
{
    if (!m_threadingMoveInProgress || motor(1)->isRunning()) {
        return;
    }
    m_threadingMoveInProgress = false;
    const bool arrived = motor(1)->getCurrentStep() == m_threadingMoveTarget;
    MGOLOG(
        fmt::format(
            "Threading pass {} at Z step {}",
            arrived ? "finished" : "STOPPED SHORT",
            motor(1)->getCurrentStep()));
    // (A multi-pass does its own retracting)
    if (!m_threadingAutoRetract || m_multiPass.inProgress()) {
        return;
    }
    const auto action = threadingStopAction(
        arrived, m_threadingMoveStartedRetracted, m_axis2Retracted, m_spindleWasRunning);
    if (action == ThreadingStopAction::None) {
        return;
    }
    if (motor(2)->isRunning()) {
        MGOLOG("Threading: X was moving at the end of a pass, so wasn't retracted");
        setThreadingWarning(
            fmt::format("{} was moving, so wasn't retracted automatically", axisLabel(2)), true);
        return;
    }
    // (axis2Retract() toggles, and the action always matches the current state)
    axis2Retract();
}

void Model::axis1GoToStep(long step)
{
    if (axisMoveRefused(1)) {
        return;
    }
    // (Usually just the move itself, which starts at once. For a taper, the
    // backlash is taken up first, on later updates)
    m_motionSequence.start(axis1MoveSteps(step));
}

void Model::axis1GoToPosition(double pos)
{
    if (axisMoveRefused(1)) {
        return;
    }
    const long step = std::lround(pos / motor(1)->getConversionFactor());
    // (With a scale: see finishesOnScale(1))
    const FinishStart start = prepareFinish(1, step);
    axis1GoToStep(start.firstStep);
    startFinish(1, step, start);
    if (std::abs(pos) < 0.00001) {
        // Round to zero for display purposes, just to
        // avoid having "-0.000" displayed
        pos = 0.0;
    }
    axisState(1).status = fmt::format("Going to {:.3f}", pos);
}

void Model::axis1GoToOffset(double offset)
{
    if (axisMoveRefused(1)) {
        return;
    }
    axisState(1).lastRelativeMove = offset;
    axis1GoToPosition(motor(1)->getPosition() + offset);
    axisState(1).status = fmt::format("To rel {}", offset);
}

void Model::axis1GoToCurrentMemory()
{
    const auto step = memoryStep(1);
    if (!step || *step == motorStep(1)) {
        return;
    }
    if (axisMoveRefused(1)) {
        return;
    }
    stopAxis(1);
    axisState(1).status = "returning";
    // (With a scale: see finishesOnScale(1))
    const FinishStart start = prepareFinish(1, *step);
    // (If threading, this waits for zero degrees on the chuck before starting)
    axis1GoToStep(start.firstStep);
    startFinish(1, *step, start);
}

void Model::axis1Zero()
{
    // (The positions now read the tip of the tool in use, which may be the
    // unknown tool: see positionsTool())
    axisState(1).positionsTool = m_toolLibrary.current().number;
    if (m_mode == Mode::Taper) {
        changeMode(Mode::None);
    }
    MGOLOG(fmt::format("{} zeroed at {:.4f}", axisLabel(1), motor(1)->getPosition()));
    motor(1)->zeroPosition();
    axisState(1).motorLog.step = 0;
    if (scale(1)) {
        scale(1)->realign();
    }
    // Zeroing will invalidate any memorised Z positions, so we clear them
    clearMemory(1);
    // Also clear any "breadcrumb" positions
    clearBreadcrumbs(1);
    clearWorkEnd();
    // The position is known again, so a motor fault can be cleared
    clearMotorFault(1);
}

void Model::axis1Half()
{
    if (motor(1)->isRunning()) {
        m_warning = "Stop the motors first";
        return;
    }
    setAxis1Position(motor(1)->getPosition() / 2.0);
    clearMemory(1);
    clearWorkEnd();
}

void Model::axis2GoToPosition(double pos)
{
    if (axisMoveRefused(2)) {
        return;
    }
    // (As the motor's goToPosition() rounds it)
    const long step = std::lround(pos / motor(2)->getConversionFactor());
    // (With a scale: see finishesOnScale(2))
    const FinishStart start = prepareFinish(2, step);
    motor(2)->goToStep(start.firstStep);
    startFinish(2, step, start);
    axisState(2).status = fmt::format("Going to {:.3f}", pos);
}

void Model::axis2GoToOffset(double offset)
{
    if (axisMoveRefused(2)) {
        return;
    }
    axisState(2).lastRelativeMove = offset;
    axis2GoToPosition(motor(2)->getPosition() + offset);
    axisState(2).status = fmt::format("To rel {}", offset);
}

bool Model::rapidReleased()
{
    if (!axisState(1).rapidInProgress && !axisState(2).rapidInProgress) {
        return false;
    }
    if (isMotionSequenceRunning() || anyFinishingOnScale() || m_backlashMeasurement
        || m_mode != Mode::None) {
        return false;
    }
    // (Not stopAxisRamped(), as letting go of Alt and the arrow are two
    // releases, and the second mustn't stop it dead while it slows down)
    if (motor(1)->isRunning()) {
        motor(1)->decelerateToStop();
    }
    if (motor(2)->isRunning()) {
        motor(2)->decelerateToStop();
    }
    return true;
}

void Model::axis2Zero()
{
    // (The positions now read the tip of the tool in use, which may be the
    // unknown tool: see positionsTool())
    axisState(2).positionsTool = m_toolLibrary.current().number;
    if (m_mode == Mode::Taper) {
        changeMode(Mode::None);
    }
    MGOLOG(fmt::format("{} zeroed at {:.4f}", axisLabel(2), motor(2)->getPosition()));
    motor(2)->zeroPosition();
    axisState(2).motorLog.step = 0;
    if (scale(2)) {
        scale(2)->realign();
    }
    // (Gear cutting's teeth are now wherever axis 2 is when it starts)
    m_gearToothOrigin.reset();
    // Zeroing will invalidate any memorised X positions, so we clear them
    clearMemory(2);
    // Also clear any "breadcrumb" positions
    clearBreadcrumbs(2);
    // The position is known again, so a motor fault can be cleared
    clearMotorFault(2);
    // X is no longer measured from the centre (unless set as a diameter again)
    clearDiameter();
}

void Model::axis2Half()
{
    if (motor(2)->isRunning()) {
        m_warning = "Stop the motors first";
        return;
    }
    setAxis2Position(motor(2)->getPosition() / 2.0);
    clearMemory(2);
}

void Model::axis2SynchroniseOff()
{
    motor(2)->synchroniseOff();
}

void Model::axis2GoToCurrentMemory()
{
    const auto step = memoryStep(2);
    if (!step || *step == motor(2)->getCurrentStep()) {
        return;
    }
    if (axisMoveRefused(2)) {
        return;
    }
    stopAxis(2);
    axisState(2).status = "returning";
    // (With a scale: see finishesOnScale(2))
    const FinishStart start = prepareFinish(2, *step);
    motor(2)->goToStep(start.firstStep);
    startFinish(2, *step, start);
}

bool Model::axis2CanRetract() const
{
    return !m_config.readBool("DisableAxis2", false) && !motor(2)->isRunning()
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
    if (axisMoveRefused(2)) {
        return;
    }
    setRetractSpeed(*motor(2));
    motor(2)->goToStep(m_xOldPosition);
    axisState(2).status = "Unretracting";
    m_fastRetracting = true;
    MGOLOG(
        fmt::format(
            "X unretract: from step {} to {}",
            motor(2)->getCurrentStep(),
            motor(2)->getTargetStep()));
    m_axis2RetractMoveInProgress = true;
}

void Model::axis2RetractBy(double distance)
{
    if (m_axis2Retracted || !axis2CanRetract()) {
        return;
    }
    if (axisMoveRefused(2)) {
        return;
    }
    m_xOldPosition = motor(2)->getCurrentStep();
    axisState(2).previousSpeed = motor(2)->getSpeed();
    setRetractSpeed(*motor(2));
    int direction = -1;
    if (m_xRetractionDirection == XDirection::Inwards) {
        direction = 1;
    }
    long stepsForRetraction = std::lround(distance / std::abs(motor(2)->getConversionFactor()));
    if (m_config.readBool("Axis2MotorFlipDirection", false)) {
        stepsForRetraction = -stepsForRetraction;
    }
    motor(2)->goToStep(motor(2)->getCurrentStep() + stepsForRetraction * direction);
    m_axis2Retracted = true;
    axisState(2).status = "Retracting";
    MGOLOG(
        fmt::format(
            "X retract by {} mm: from step {} to {}",
            distance,
            motor(2)->getCurrentStep(),
            motor(2)->getTargetStep()));
    m_axis2RetractMoveInProgress = true;
}

void Model::axis2RetractTo(double position, double minimum)
{
    // How far out position is from X (negative if it's further in)
    const double out = (motor(2)->getPosition() - position) * axis2IntoWorkSign();
    axis2RetractBy(std::max(out, minimum));
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
    return m_axis2Retracted ? motor(2)->getPosition(m_xOldPosition) : getAxis2Position();
}

std::optional<double> Model::getDiameterMm() const
{
    if (!m_xDiameterSet || !motor(2)) {
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
        m_warning = "Set " + axisLabel(2) + " as a diameter first";
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
    m_workEndZ = motor(1)->getPosition();
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
    return currentFeatureEnabled() && modeAllows && !m_motionSequence.isRunning()
        && !axisState(1).faulted && !axisState(2).faulted;
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
    if (moveRefused({ 1, 2 })) {
        return;
    }
    if (motor(1)->isRunning() || motor(2)->isRunning()) {
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
    if (axisMoveRefused(2)) {
        return;
    }
    if (motor(2)->isRunning()) {
        m_warning = "Stop the motors first";
        return;
    }
    m_warning = "";
    // (Not retracting: X stays where Z is)
    moveToSafely(motor(1)->getPosition(), *m_featureX, false, "to feature", {});
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
    if (motor(1)->isRunning() || motor(2)->isRunning()) {
        m_warning = "Stop the motors first";
        return;
    }
    // X must be clear of the work: not deeper in than the feature's surface.
    // (Without the diameter, nothing's known: X is the user's responsibility,
    // as for any Z move)
    if (m_featureX
        && (motor(2)->getPosition() - *m_featureX) * axis2IntoWorkSign() > MultiPass::TOLERANCE) {
        m_warning = "Move " + axisLabel(2) + " clear of the work first";
        return;
    }
    m_warning = "";
    // (As a Z go to: at Z's speed, and X stays as it is, even if retracted)
    axis1GoToPosition(*z);
}

namespace {

// Today's date, as YYYY-MM-DD
std::string today()
{
    const std::time_t now = std::time(nullptr);
    std::tm local {};
    localtime_r(&now, &local);
    char date[16];
    std::strftime(date, sizeof(date), "%Y-%m-%d", &local);
    return date;
}

// +1 if the motor's last move made its position (in mm) go up, or -1 if down
int lastMoveSign(const StepperMotor& motor)
{
    const int steps = motor.getDirection() == Direction::forward ? 1 : -1;
    return motor.getConversionFactor() > 0.0 ? steps : -steps;
}

} // end anonymous namespace

bool Model::toolLibraryEnabled() const
{
    return m_config.readBool("ToolLibrary", false) && !m_config.readBool("DisableAxis1", false)
        && !m_config.readBool("DisableAxis2", false);
}

const ToolLibrary& Model::toolLibrary() const
{
    return m_toolLibrary;
}

std::string Model::toolChangeUnavailable() const
{
    if (!toolLibraryEnabled()) {
        return "The tool library isn't turned on (ToolLibrary in the config)";
    }
    if (m_toolMeasurement) {
        return "Finish measuring the tool first (or Esc to cancel it)";
    }
    // (In multi-pass or radius mode, the passes' positions are on the work,
    // so carry over to the new tool, as memory slots do)
    if (m_mode != Mode::None && m_mode != Mode::MultiPass && m_mode != Mode::Radius) {
        return "Leave the mode first (Esc)";
    }
    if (m_multiPass.inProgress() || m_multiPassMovingToStart) {
        return "Not during a multi-pass";
    }
    if (motor(1)->isRunning() || motor(2)->isRunning() || m_motionSequence.isRunning()
        || anyFinishingOnScale() || m_backlashMeasurement) {
        return "Stop the motors first";
    }
    return "";
}

void Model::shiftPositionsForTool(int axisNumber, double fromMm, double toMm)
{
    StepperMotor& motor = *this->motor(axisNumber);
    // In whole steps for each tool, so changing to a tool and back again
    // always comes back to the same step
    const double factor = motor.getConversionFactor();
    const long steps = std::lround(toMm / factor) - std::lround(fromMm / factor);
    if (steps != 0) {
        motor.setPosition(static_cast<double>(motor.getCurrentStep() + steps) * factor);
    }
    axisState(axisNumber).motorLog.step = motor.getCurrentStep();
    if (AxisScale* sc = scale(axisNumber)) {
        // (As for a position set: the scale is lined up with the motor again)
        sc->realign();
    }
    // Unlike a position set, nothing is cleared: memory slots, breadcrumbs,
    // the current feature and so on are all positions on the work, which the
    // new tool's tip now reads, and X is still measured from the centre if it
    // was before
}

std::string Model::saveToolLibrary()
{
    const std::string path = m_config.read("ToolLibraryFile", "tools.cfg");
    const std::string error = writeToolLibrary(m_toolLibrary, path, axisLabel(1), axisLabel(2));
    if (!error.empty()) {
        MGOLOG("*** Warning *** " << error);
        m_warning = "The tool library couldn't be saved (see lc.log)";
        return m_warning;
    }
    return "";
}

std::pair<bool, bool> Model::toolChangeClears(int number) const
{
    // (Only changing to a tool in the library, when an axis's positions
    // read the unknown tool's tip)
    if (number == ToolLibrary::UNKNOWN_TOOL || number == m_toolLibrary.current().number) {
        return { false, false };
    }
    return { axisState(2).positionsTool == ToolLibrary::UNKNOWN_TOOL,
             axisState(1).positionsTool == ToolLibrary::UNKNOWN_TOOL };
}

std::optional<int> Model::positionsTool() const
{
    if (axisState(1).positionsTool != axisState(2).positionsTool
        || axisState(1).positionsTool == ToolLibrary::UNKNOWN_TOOL) {
        return std::nullopt;
    }
    return axisState(1).positionsTool;
}

std::string Model::selectTool(int number)
{
    if (std::string why = toolChangeUnavailable(); !why.empty()) {
        m_warning = why;
        return why;
    }
    const bool toUnknown = number == ToolLibrary::UNKNOWN_TOOL;
    const Tool* to = m_toolLibrary.find(number);
    if (!toUnknown && !to) {
        m_warning = fmt::format("There's no tool {}", number);
        return m_warning;
    }
    const int from = m_toolLibrary.current().number;
    if (from == number) {
        return "";
    }
    m_toolLibrary.setCurrent(number);
    m_warning = "";
    if (toUnknown) {
        // Its offsets aren't known, so nothing can be shifted: the positions
        // still read the last tool's tip (see positionsTool()), until an
        // axis is zeroed or its position set
        MGOLOG(fmt::format("Tool changed from T{} to an unknown tool", from));
        return saveToolLibrary();
    }
    // Each axis shifts from the tool its positions read, if that's known.
    // If they read the unknown tool's tip (it was zeroed or set with that
    // tool), there's nothing to shift from: the positions are left as they
    // are, but what was set on the work with the unknown tool is cleared
    const Tool* axis1From = m_toolLibrary.find(axisState(1).positionsTool);
    const Tool* axis2From = m_toolLibrary.find(axisState(2).positionsTool);
    if (axis1From) {
        shiftPositionsForTool(1, axis1From->axis1OffsetMm, to->axis1OffsetMm);
    } else {
        clearWorkEnd();
    }
    if (axis2From) {
        shiftPositionsForTool(2, axis2From->axis2OffsetMm, to->axis2OffsetMm);
    } else {
        // (X is no longer known to be measured from the centre, either)
        clearDiameter();
    }
    MGOLOG(
        fmt::format(
            "Tool changed from T{} to T{} ({}): {} {}, {} {}",
            from,
            to->number,
            to->description,
            axisLabel(1),
            axis1From ? fmt::format(
                            "shifted by {:.4f} (from T{})",
                            to->axis1OffsetMm - axis1From->axis1OffsetMm,
                            axis1From->number)
                      : std::string("not shifted (set with an unknown tool)"),
            axisLabel(2),
            axis2From ? fmt::format(
                            "shifted by {:.4f} (from T{})",
                            to->axis2OffsetMm - axis2From->axis2OffsetMm,
                            axis2From->number)
                      : std::string("not shifted (set with an unknown tool)")));
    axisState(1).positionsTool = number;
    axisState(2).positionsTool = number;
    return saveToolLibrary();
}

std::string Model::editTool(int number, const std::string& description, double noseRadiusMm)
{
    const Tool* existing = m_toolLibrary.find(number);
    if (!existing) {
        m_warning = fmt::format("There's no tool {}", number);
        return m_warning;
    }
    Tool tool = *existing;
    tool.description = description;
    tool.noseRadiusMm = std::max(0.0, noseRadiusMm);
    m_toolLibrary.store(std::move(tool));
    return saveToolLibrary();
}

std::string Model::toolMeasurementUnavailable(int number) const
{
    if (std::string why = toolChangeUnavailable(); !why.empty()) {
        return why;
    }
    if (m_mode != Mode::None) {
        return "Leave the mode first (Esc)";
    }
    if (number < ToolLibrary::REFERENCE_TOOL || number > ToolLibrary::MAX_TOOL) {
        return "The tool library is full";
    }
    if (!m_toolLibrary.currentIsKnown()) {
        return "Tools are measured from one in the library: change to one first";
    }
    if (number == m_toolLibrary.current().number) {
        return fmt::format("T{} is the tool in use: it's measured from another tool", number);
    }
    if (m_xRetractionDirection != XDirection::Outwards) {
        return "Only external tools can be measured so far (the retraction is set for boring)";
    }
    if (m_axis2Retracted) {
        return "Unretract " + axisLabel(2) + " first";
    }
    if (isAxisLocked(1) || isAxisLocked(2)) {
        return "Unlock the axes first";
    }
    return "";
}

std::string Model::startToolMeasurement(int number)
{
    if (std::string why = toolMeasurementUnavailable(number); !why.empty()) {
        m_warning = why;
        return why;
    }
    m_toolMeasurement = ToolMeasurement { .number = number, .touches = {} };
    m_warning = "";
    MGOLOG(fmt::format("Measuring T{} from T{}", number, m_toolLibrary.current().number));
    return "";
}

bool Model::isMeasuringTool() const
{
    return m_toolMeasurement.has_value();
}

int Model::toolMeasurementNumber() const
{
    return m_toolMeasurement ? m_toolMeasurement->number : 0;
}

ToolTouchStep Model::toolTouchStep() const
{
    if (!m_toolMeasurement) {
        return ToolTouchStep::Finished;
    }
    return static_cast<ToolTouchStep>(std::min<std::size_t>(m_toolMeasurement->touches.size(), 4));
}

bool Model::recordToolTouch()
{
    const ToolTouchStep step = toolTouchStep();
    if (!m_toolMeasurement || step == ToolTouchStep::Finished) {
        return false;
    }
    if (motor(1)->isRunning() || motor(2)->isRunning() || m_motionSequence.isRunning()) {
        m_warning = "Wait for the axes to stop";
        return false;
    }
    const bool diameter
        = step == ToolTouchStep::ReferenceDiameter || step == ToolTouchStep::ToolDiameter;
    double position = 0.0;
    if (diameter) {
        // The tool comes in on to the diameter, so X's backlash is taken up
        // the same way for each tool
        if (lastMoveSign(*motor(2)) != static_cast<int>(axis2IntoWorkSign())) {
            m_warning
                = "Touch the diameter moving " + axisLabel(2) + " in (back off, and come in again)";
            return false;
        }
        position = motor(2)->getPosition();
    } else {
        // Likewise Z, on to the end, towards the chuck
        const int towardsChuck
            = m_config.read("Axis1TowardsChuck", "negative") == "positive" ? 1 : -1;
        if (lastMoveSign(*motor(1)) != towardsChuck) {
            m_warning = "Touch the end moving " + axisLabel(1)
                + " towards the chuck (back off, and come in again)";
            return false;
        }
        position = motor(1)->getPosition();
    }
    m_toolMeasurement->touches.push_back(position);
    m_warning = "";
    MGOLOG(
        fmt::format(
            "Tool measurement: touch {} at {} {:.4f}",
            m_toolMeasurement->touches.size(),
            m_config.read(diameter ? "Axis2Label" : "Axis1Label", diameter ? "X" : "Z"),
            position));
    return true;
}

std::optional<ToolOffsets> Model::toolMeasurementOffsets() const
{
    if (toolTouchStep() != ToolTouchStep::Finished || !m_toolMeasurement) {
        return std::nullopt;
    }
    const auto& t = m_toolMeasurement->touches;
    return ToolLibrary::measuredOffsets(
        m_toolLibrary.current(),
        ToolTouches { .axis1Mm = t.at(1), .axis2Mm = t.at(0) },
        ToolTouches { .axis1Mm = t.at(3), .axis2Mm = t.at(2) });
}

std::string Model::finishToolMeasurement(const std::string& description, double noseRadiusMm)
{
    const auto offsets = toolMeasurementOffsets();
    if (!offsets) {
        return "The tool hasn't been measured";
    }
    const int number = m_toolMeasurement->number;
    m_toolMeasurement.reset();
    Tool tool { .number = number,
                .description = description,
                .noseRadiusMm = std::max(0.0, noseRadiusMm),
                .axis1OffsetMm = offsets->axis1Mm,
                .axis2OffsetMm = offsets->axis2Mm,
                .measured = today() };
    if (tool.description.empty()) {
        tool.description = fmt::format("Tool {}", number);
    }
    MGOLOG(
        fmt::format(
            "T{} ({}) measured from T{}: offsets {:.4f}, {:.4f}",
            number,
            tool.description,
            m_toolLibrary.current().number,
            tool.axis1OffsetMm,
            tool.axis2OffsetMm));
    // (The current tool's offsets change too, if it's the reference tool
    // that's been measured: see ToolLibrary::store(). Its tip hasn't moved,
    // so the positions mustn't: the shift below is worked out from the
    // offsets as they are now, which keep their differences)
    m_toolLibrary.store(std::move(tool));
    // The tool measured is the one in the toolpost now
    return selectTool(number);
}

void Model::cancelToolMeasurement()
{
    if (m_toolMeasurement) {
        MGOLOG(fmt::format("Measuring T{} cancelled", m_toolMeasurement->number));
    }
    m_toolMeasurement.reset();
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

double Model::getSpindlePosition() const
{
    return m_rotaryEncoder->getSpindlePosition();
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

bool Model::warningClearable() const
{
    return !getWarning().empty() && (!m_warning.empty() || !m_threadingPositionLost);
}

void Model::clearWarning()
{
    if (!warningClearable()) {
        return;
    }
    MGOLOG("Warning cleared: " << getWarning());
    if (!m_warning.empty()) {
        m_warning = "";
    } else {
        setThreadingWarning("", false);
    }
}

std::string Model::axisStatus(int axisNumber) const
{
    if (const auto& finish = scaleState(axisNumber).finish) {
        // (The move itself as usual, e.g. "Going to 10.000"; then waiting for
        // the scale, and correcting)
        if (finish->corrections == 0 && motor(axisNumber)->isRunning()) {
            return axisState(axisNumber).status;
        }
        return finish->corrections == 0
            ? "Finishing on the scale"
            : fmt::format("Finishing on the scale (correction {})", finish->corrections);
    }
    if (axisNumber == 1 && m_backlashMeasurement) {
        return fmt::format(
            "Measuring backlash: move {} of {}",
            std::min(m_backlashMeasurement->movesDone() + 1, BacklashMeasurement::MOVES),
            BacklashMeasurement::MOVES);
    }
    return axisState(axisNumber).status;
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
    if (m_currentMemory < MEMORY_SLOTS - 1) {
        ++m_currentMemory;
    }
}

std::size_t Model::getCurrentMemorySlot() const
{
    return m_currentMemory;
}

unsigned Model::getMemorySize() const
{
    return MEMORY_SLOTS;
}

bool Model::hasAxis(int axisNumber) const
{
    if (axisNumber == 1 || axisNumber == 2) {
        return true;
    }
    return manualAxis(axisNumber) != nullptr;
}

bool Model::isMotorRunning(int axisNumber) const
{
    return motor(axisNumber)->isRunning();
}

double Model::motorSpeed(int axisNumber) const
{
    const StepperMotor* m = motor(axisNumber);
    return m ? m->getSpeed() : 0.0;
}

void Model::setMotorSpeed(int axisNumber, double speed)
{
    motor(axisNumber)->setSpeed(speed);
}

void Model::waitForMotor(int axisNumber)
{
    motor(axisNumber)->wait();
}

void Model::stopAxis(int axisNumber)
{
    StepperMotor* m = motor(axisNumber);
    m->stop();
    m->wait();
}

void Model::stopAxisRamped(int axisNumber)
{
    StepperMotor* m = motor(axisNumber);
    if (m->isDecelerating()) {
        m->stop();
        m->wait();
        return;
    }
    m->decelerateToStop();
}

long Model::motorStep(int axisNumber) const
{
    const StepperMotor* m = motor(axisNumber);
    return m ? m->getCurrentStep() : 0;
}

double Model::motorPosition(int axisNumber) const
{
    const StepperMotor* m = motor(axisNumber);
    return m ? m->getPosition() : 0.0;
}

void Model::goToPosition(int axisNumber, double mm)
{
    if (axisNumber == 1) {
        axis1GoToPosition(mm);
    } else if (axisNumber == 2) {
        axis2GoToPosition(mm);
    } else {
        // (Wound there by hand)
        manualAxisGoToPosition(axisNumber, mm);
    }
}

void Model::goToOffset(int axisNumber, double mm)
{
    if (axisNumber == 1) {
        axis1GoToOffset(mm);
    } else {
        axis2GoToOffset(mm);
    }
}

void Model::repeatLastRelativeMove(int axisNumber)
{
    if (axisState(axisNumber).lastRelativeMove != 0.0) {
        goToOffset(axisNumber, axisState(axisNumber).lastRelativeMove);
    }
}

namespace {

// Each motorised axis's speed steps (see Model::speedIncrease()): fine below
// the coarse step, coarse above it. They're as they were before the axes
// shared this code (axis 1's 1 and 20 mm/min suit the lathe's Z; axis 2's 2
// and 10 its X, or the mill's rotary table)
struct SpeedSteps {
    double fine;
    double coarse;
};
constexpr std::array<SpeedSteps, 2> SPEED_STEPS { { { 1.0, 20.0 }, { 2.0, 10.0 } } };

// A speed read back from the motor is worked out from its rpm, so may be a
// hair either side of the speed which was set: this keeps e.g. 20 from
// counting as above 20
constexpr double SPEED_SLACK = 0.1;

} // namespace

double Model::speedPreset(const IConfigReader& config, int axisNumber, int preset)
{
    assert(axisNumber == 1 || axisNumber == 2);
    assert(preset >= 1 && preset <= 5);
    static constexpr std::array<std::array<double, 5>, 2> DEFAULTS { {
        { 20.0, 40.0, 100.0, 250.0, 1'000.0 },
        { 5.0, 20.0, 40.0, 80.0, 100.0 },
    } };
    return config.readDouble(
        fmt::format("Axis{}SpeedPreset{}", axisNumber, preset),
        DEFAULTS.at(axisNumber - 1).at(preset - 1));
}

void Model::speedIncrease(int axisNumber)
{
    if (axisNumber == 1 && m_mode == Mode::Threading) {
        return;
    }
    const auto& steps = SPEED_STEPS.at(axisNumber - 1);
    const double speed = motorSpeed(axisNumber);
    const double max = m_config.readDouble(fmt::format("Axis{}MaxMotorRpm", axisNumber), 1'000.0);
    if (speed < steps.coarse - SPEED_SLACK) {
        setMotorSpeed(axisNumber, std::min(speed + steps.fine, max));
    } else if (speed < max - SPEED_SLACK) {
        setMotorSpeed(axisNumber, std::min(speed + steps.coarse, max));
    }
}

void Model::speedDecrease(int axisNumber)
{
    if (axisNumber == 1 && m_mode == Mode::Threading) {
        return;
    }
    const auto& steps = SPEED_STEPS.at(axisNumber - 1);
    const double speed = motorSpeed(axisNumber);
    if (speed > steps.coarse + SPEED_SLACK) {
        setMotorSpeed(axisNumber, speed - steps.coarse);
    } else if (speed > steps.fine + SPEED_SLACK) {
        setMotorSpeed(axisNumber, speed - steps.fine);
    }
}

void Model::selectSpeedPreset(int axisNumber, int preset)
{
    if (m_mode == Mode::Threading) {
        return;
    }
    setMotorSpeed(axisNumber, speedPreset(m_config, axisNumber, preset));
}

bool Model::axisMoveRefused(int axisNumber)
{
    if (axisNumber == 1 && m_mode == Mode::Taper) {
        // (X follows Z in a taper)
        return moveRefused({ 1, 2 });
    }
    return moveRefused({ axisNumber });
}

int Model::stepSign(int axisNumber, AxisDirection direction) const
{
    const bool flip = m_config.readBool(fmt::format("Axis{}MotorFlipDirection", axisNumber), false);
    return (direction == AxisDirection::Forward) != flip ? 1 : -1;
}

void Model::move(int axisNumber, AxisDirection direction)
{
    // Issuing the same command (i.e. pressing the same key)
    // when it is already running will cause the motor to stop
    if (isMotorRunning(axisNumber)) {
        stopAxisRamped(axisNumber);
        return;
    }
    if (axisMoveRefused(axisNumber)) {
        return;
    }
    const bool forward = direction == AxisDirection::Forward;
    if (axisNumber == 1) {
        axisState(1).status = forward ? "moving left" : "moving right";
    } else {
        axisState(2).status = forward ? "moving in" : "moving out";
    }
    // (INF_LEFT and INF_IN are the highest step, INF_RIGHT and INF_OUT the
    // lowest)
    const long target = stepSign(axisNumber, direction) > 0 ? INF_LEFT : INF_RIGHT;
    if (axisNumber == 1) {
        // (Through the motion sequence, for a taper or threading)
        axis1GoToStep(target);
    } else {
        motor(2)->goToStep(target);
    }
}

void Model::rapid(int axisNumber, AxisDirection direction)
{
    // Don't allow rapids if in a mode;
    if (m_mode != Mode::None) {
        return;
    }
    if (axisMoveRefused(axisNumber)) {
        return;
    }
    // Issuing the same command (i.e. pressing the same key)
    // when it is already running will cause the motor to stop
    if (isMotorRunning(axisNumber)) {
        stopAxisRamped(axisNumber);
        return;
    }
    StepperMotor* m = motor(axisNumber);
    axisState(axisNumber).previousSpeed = m->getSpeed();
    axisState(axisNumber).rapidInProgress = true;
    m->setSpeed(m->getMaxRpm());
    move(axisNumber, direction);
}

void Model::nudge(int axisNumber, AxisDirection direction, double mm)
{
    if (axisMoveRefused(axisNumber)) {
        return;
    }
    // (First, so that its end can't restore a speed over ours)
    m_motionSequence.cancel();
    if (isMotorRunning(axisNumber)) {
        stopAxis(axisNumber);
    }
    StepperMotor* m = motor(axisNumber);
    const double oldSpeed = m->getSpeed();
    m->setSpeed(m->getMaxRpm() / 10.0);
    // The same way as a move (see AxisDirection). (Until 20 Nov 2026, a nudge
    // took its direction from the conversion factor's sign instead, which
    // agreed with the moves on the lathe and the mill's X, but turned the
    // mill's rotary table the opposite way to its arrow keys)
    const long steps
        = stepSign(axisNumber, direction) * std::lround(mm / std::abs(m->getConversionFactor()));
    const long target = motorStep(axisNumber) + steps;
    // The nudge is a sequence which ends when the move does, so that (as
    // with any sequence) keys pressed meanwhile wait until it's finished,
    // and a quick series of nudges each moves the full amount
    std::vector<MotionSequence::Step> moveSteps;
    if (axisNumber == 1) {
        // (Following the taper or the thread, in those modes)
        moveSteps = axis1MoveSteps(target);
    } else {
        moveSteps = { { [this, target]() { motor(2)->goToStep(target); }, {} } };
    }
    moveSteps.back().done = [this, axisNumber]() { return !isMotorRunning(axisNumber); };
    m_motionSequence.start(std::move(moveSteps), [this, axisNumber, oldSpeed]() {
        motor(axisNumber)->setSpeed(oldSpeed);
        if (axisNumber == 1) {
            // A nudge isn't a threading pass, so it shouldn't retract X when it ends
            m_threadingMoveInProgress = false;
        }
        // Save position
        saveBreadcrumbPosition(axisNumber);
    });
}

void Model::fastReturn(int axisNumber)
{
    const auto step = memoryStep(axisNumber);
    auto& state = axisState(axisNumber);
    if (!step || state.fastReturning) {
        return;
    }
    if (axisMoveRefused(axisNumber)) {
        return;
    }
    StepperMotor* m = motor(axisNumber);
    state.previousSpeed = m->getSpeed();
    state.fastReturning = true;
    stopAxis(axisNumber);
    if (axisNumber == 1 && m_mode == Mode::Taper) {
        // If we are tapering, we need to set a speed the x-axis motor can keep up with
        m->setSpeed(100.0);
        axisState(2).previousSpeed = motor(2)->getSpeed();
        // (axis1GoToStep() takes up the backlash and synchronises X)
    } else {
        m->setSpeed(m->getMaxRpm());
    }
    state.status = "fast returning";
    if (axisNumber == 1) {
        axis1GoToStep(*step);
    } else {
        motor(2)->goToStep(*step);
    }
}

void Model::goToPreviousPosition(int axisNumber)
{
    if (axisMoveRefused(axisNumber)) {
        return;
    }
    stopAxis(axisNumber);
    // (Skipping where it is now)
    auto& trail = axisState(axisNumber).previousPositions;
    while (!trail.empty() && (std::abs(trail.top() - motorPosition(axisNumber)) < 0.0001)) {
        if (trail.size() == 1) {
            return;
        }
        trail.pop();
    }
    if (trail.empty()) {
        return;
    }
    goToPosition(axisNumber, trail.top());
    trail.pop();
}

void Model::saveBreadcrumbPosition(int axisNumber)
{
    auto& trail = axisState(axisNumber).previousPositions;
    if (!trail.empty() && trail.top() == motorPosition(axisNumber)) {
        return;
    }
    trail.push(motorPosition(axisNumber));
}

void Model::clearBreadcrumbs(int axisNumber)
{
    axisState(axisNumber).previousPositions = std::stack<double>();
    saveBreadcrumbPosition(axisNumber);
}

Model::MotorAxisState& Model::axisState(int axisNumber)
{
    return m_motorAxes.at(static_cast<std::size_t>(axisNumber - 1));
}

const Model::MotorAxisState& Model::axisState(int axisNumber) const
{
    return m_motorAxes.at(static_cast<std::size_t>(axisNumber - 1));
}

StepperMotor* Model::motor(int axisNumber)
{
    return axisState(axisNumber).motor.get();
}

StepperMotor* Model::motor(int axisNumber) const
{
    return axisState(axisNumber).motor.get();
}

Model::ScaleState& Model::scaleState(int axisNumber)
{
    return axisState(axisNumber).scaleState;
}

const Model::ScaleState& Model::scaleState(int axisNumber) const
{
    return axisState(axisNumber).scaleState;
}

AxisScale* Model::scale(int axisNumber) const
{
    return axisState(axisNumber).scaleState.scale.get();
}

std::string Model::axisLabel(int axisNumber) const
{
    switch (axisNumber) {
        case 1:
            return m_config.read("Axis1Label", "Z");
        case 2:
            return m_config.read("Axis2Label", "X");
        default:
            {
                const ManualAxis* axis = manualAxis(axisNumber);
                return axis ? axis->label() : "";
            }
    }
}

std::optional<double> Model::getMemory(int axisNumber, std::size_t slot) const
{
    if (!hasAxis(axisNumber) || slot >= MEMORY_SLOTS) {
        return std::nullopt;
    }
    return m_memory.at(static_cast<std::size_t>(axisNumber - 1)).at(slot);
}

std::optional<long> Model::memoryStep(int axisNumber) const
{
    const StepperMotor* m = motor(axisNumber);
    const auto mm = getMemory(axisNumber, m_currentMemory);
    if (!mm || !m) {
        return std::nullopt;
    }
    // (Stored as a whole number of steps, so this is exact)
    return std::lround(*mm / m->getConversionFactor());
}

std::vector<int> Model::memoryAxes(int axisNumber) const
{
    std::vector<int> axes;
    for (int n = 1; n <= AXES; ++n) {
        if ((axisNumber == 0 || axisNumber == n) && hasAxis(n)) {
            axes.push_back(n);
        }
    }
    return axes;
}

void Model::pushMemoryUndo()
{
    MemoryUndo undo { m_memory, m_memoryClipboard };
    // (Not again if nothing has changed since: e.g. storing the same place
    // twice. undoMemory() also skips any which are the same as now)
    if (!m_memoryUndo.empty() && m_memoryUndo.back() == undo) {
        return;
    }
    if (m_memoryUndo.size() == MEMORY_UNDO_LEVELS) {
        m_memoryUndo.erase(m_memoryUndo.begin());
    }
    m_memoryUndo.push_back(std::move(undo));
}

void Model::storePosition(int axisNumber)
{
    if (axisNumber == 0) {
        // (One undo for them all)
        pushMemoryUndo();
        for (int n = 1; n <= AXES; ++n) {
            const ManualAxis* axis = manualAxis(n);
            if (hasAxis(n) && (!axis || axis->hasReading())) {
                storePositionInSlot(n);
            }
        }
        return;
    }
    if (!hasAxis(axisNumber)) {
        return;
    }
    pushMemoryUndo();
    storePositionInSlot(axisNumber);
}

void Model::storePositionInSlot(int axisNumber)
{
    auto& slot = m_memory.at(static_cast<std::size_t>(axisNumber - 1)).at(m_currentMemory);
    if (axisNumber == 1 || axisNumber == 2) {
        slot = motorPosition(axisNumber);
    } else if (ManualAxis* axis = manualAxisOrWarn(axisNumber)) {
        slot = axis->positionMm();
    }
}

void Model::storePosition(int axisNumber, double mm)
{
    if (!hasAxis(axisNumber)) {
        return;
    }
    pushMemoryUndo();
    auto& slot = m_memory.at(static_cast<std::size_t>(axisNumber - 1)).at(m_currentMemory);
    if (axisNumber == 1 || axisNumber == 2) {
        // (To the nearest step: a slot holds a position the motor can go to
        // exactly, so that being there is seen as being there)
        const StepperMotor& m = *motor(axisNumber);
        slot = m.getPosition(std::lround(mm / m.getConversionFactor()));
    } else {
        slot = mm;
    }
}

void Model::clearCurrentMemorySlot(int axisNumber)
{
    const auto axes = memoryAxes(axisNumber);
    if (axes.empty()) {
        return;
    }
    pushMemoryUndo();
    for (const int n : axes) {
        m_memory.at(static_cast<std::size_t>(n - 1)).at(m_currentMemory).reset();
    }
}

void Model::clearMemory(int axisNumber)
{
    if (!hasAxis(axisNumber)) {
        return;
    }
    const auto index = static_cast<std::size_t>(axisNumber - 1);
    const auto clear = [index](
                           std::array<std::vector<std::optional<double>>, AXES>& memory,
                           std::optional<MemoryClipboard>& clipboard) {
        for (auto& slot : memory.at(index)) {
            slot.reset();
        }
        if (clipboard) {
            clipboard->holds.at(index) = false;
            clipboard->values.at(index).reset();
            if (std::ranges::none_of(clipboard->holds, std::identity {})) {
                clipboard.reset();
            }
        }
    };
    clear(m_memory, m_memoryClipboard);
    // (So an undo never brings back a position from before, which would be
    // somewhere else now. The other axes' changes can still be undone)
    for (auto& undo : m_memoryUndo) {
        clear(undo.memory, undo.clipboard);
    }
}

bool Model::canCopyMemorySlot(int axisNumber) const
{
    return std::ranges::any_of(memoryAxes(axisNumber), [this](int n) {
        return getMemory(n, m_currentMemory).has_value();
    });
}

bool Model::canPasteMemorySlot(int axisNumber) const
{
    return m_memoryClipboard && std::ranges::any_of(memoryAxes(axisNumber), [this](int n) {
               return m_memoryClipboard->holds.at(static_cast<std::size_t>(n - 1));
           });
}

void Model::copyMemorySlot(int axisNumber)
{
    if (!canCopyMemorySlot(axisNumber)) {
        m_warning = axisNumber == 0
            ? fmt::format("Mem {} is empty: nothing to copy", m_currentMemory + 1)
            : fmt::format("{} isn't set in this slot", axisLabel(axisNumber));
        return;
    }
    pushMemoryUndo();
    MemoryClipboard clipboard;
    clipboard.slot = m_currentMemory;
    for (const int n : memoryAxes(axisNumber)) {
        const auto index = static_cast<std::size_t>(n - 1);
        clipboard.holds.at(index) = true;
        clipboard.values.at(index) = m_memory.at(index).at(m_currentMemory);
    }
    m_memoryClipboard = clipboard;
    MGOLOG(fmt::format("Memory: copied Mem {}", m_currentMemory + 1));
}

void Model::cutMemorySlot(int axisNumber)
{
    if (!canCopyMemorySlot(axisNumber)) {
        copyMemorySlot(axisNumber); // (which warns)
        return;
    }
    // (One undo for both: copying's own finds nothing has changed yet)
    pushMemoryUndo();
    copyMemorySlot(axisNumber);
    for (const int n : memoryAxes(axisNumber)) {
        m_memory.at(static_cast<std::size_t>(n - 1)).at(m_currentMemory).reset();
    }
    m_memoryClipboard->cut = true;
}

void Model::pasteMemorySlot(int axisNumber)
{
    if (!canPasteMemorySlot(axisNumber)) {
        m_warning = !m_memoryClipboard || axisNumber == 0
            ? "Nothing to paste: copy a memory slot first (Ctrl+C)"
            : fmt::format("Nothing to paste for {}: copy it first", axisLabel(axisNumber));
        return;
    }
    pushMemoryUndo();
    for (const int n : memoryAxes(axisNumber)) {
        const auto index = static_cast<std::size_t>(n - 1);
        if (m_memoryClipboard->holds.at(index)) {
            m_memory.at(index).at(m_currentMemory) = m_memoryClipboard->values.at(index);
        }
    }
    MGOLOG(
        fmt::format(
            "Memory: pasted Mem {}'s values into Mem {}",
            m_memoryClipboard->slot + 1,
            m_currentMemory + 1));
}

std::optional<std::size_t> Model::memoryClipboardSourceSlot(int axisNumber) const
{
    if (!m_memoryClipboard || !hasAxis(axisNumber)) {
        return std::nullopt;
    }
    const auto index = static_cast<std::size_t>(axisNumber - 1);
    const std::size_t slot = m_memoryClipboard->slot;
    const auto& now = m_memory.at(index).at(slot);
    const bool unchanged
        = m_memoryClipboard->cut ? !now.has_value() : now == m_memoryClipboard->values.at(index);
    if (!m_memoryClipboard->holds.at(index) || !unchanged) {
        return std::nullopt;
    }
    return slot;
}

bool Model::canUndoMemory() const
{
    const MemoryUndo now { m_memory, m_memoryClipboard };
    return std::ranges::any_of(m_memoryUndo, [&now](const MemoryUndo& u) { return u != now; });
}

void Model::undoMemory()
{
    // (Skipping any which changed nothing)
    const MemoryUndo now { m_memory, m_memoryClipboard };
    while (!m_memoryUndo.empty() && m_memoryUndo.back() == now) {
        m_memoryUndo.pop_back();
    }
    if (m_memoryUndo.empty()) {
        m_warning = "Nothing to undo in the memory slots";
        return;
    }
    m_memory = std::move(m_memoryUndo.back().memory);
    m_memoryClipboard = std::move(m_memoryUndo.back().clipboard);
    m_memoryUndo.pop_back();
    MGOLOG("Memory: undone");
}

void Model::goToCurrentMemory(int axisNumber)
{
    switch (axisNumber) {
        case 1:
            axis1GoToCurrentMemory();
            break;
        case 2:
            axis2GoToCurrentMemory();
            break;
        default:
            manualAxisGoToCurrentMemory(axisNumber);
            break;
    }
}

bool Model::inSlotGoTo(int axisNumber) const
{
    if (axisNumber == 1 || axisNumber == 2) {
        return true;
    }
    return manualAxis(axisNumber)
        && m_config.readBool(fmt::format("Axis{}InSlotGoTo", axisNumber), true);
}

void Model::setAxis1Position(double mm)
{
    // (The positions now read the tip of the tool in use, which may be the
    // unknown tool: see positionsTool())
    axisState(1).positionsTool = m_toolLibrary.current().number;
    MGOLOG(
        fmt::format(
            "{} position set to {:.4f} (was {:.4f})", axisLabel(1), mm, motor(1)->getPosition()));
    motor(1)->setPosition(mm);
    axisState(1).motorLog.step = motor(1)->getCurrentStep();
    if (scale(1)) {
        scale(1)->realign();
    }
    // The position is known again, so a motor fault can be cleared
    clearMotorFault(1);
}

void Model::setAxis2Position(double mm)
{
    // (The positions now read the tip of the tool in use, which may be the
    // unknown tool: see positionsTool())
    axisState(2).positionsTool = m_toolLibrary.current().number;
    MGOLOG(
        fmt::format(
            "{} position set to {:.4f} (was {:.4f})", axisLabel(2), mm, motor(2)->getPosition()));
    motor(2)->setPosition(mm);
    axisState(2).motorLog.step = motor(2)->getCurrentStep();
    if (scale(2)) {
        scale(2)->realign();
    }
    // (Gear cutting's teeth are now wherever axis 2 is when it starts)
    m_gearToothOrigin.reset();
    // The position is known again, so a motor fault can be cleared
    clearMotorFault(2);
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

Model::~Model()
{
    resetMotorThreads();
}

void Model::resetMotorThreads()
{
    // (First, as a simulated scale follows the motor)
#ifdef FAKE
    scaleState(1).fake = nullptr;
    scaleState(2).fake = nullptr;
#endif
    scaleState(2).scale.reset();
    scaleState(1).scale.reset();
    // (Axis 2's first, as it may follow axis 1's in a taper)
    axisState(2).motor.reset();
    axisState(1).motor.reset();
}

double Model::getAxis2Position() const
{
    return motorPosition(2) - multiPassXCorrection();
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

std::string Model::formatPosition(int axisNumber, double mm) const
{
    return formatValue(toDisplayUnits(axisNumber, mm), axisDecimals(axisNumber));
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

bool Model::hasScale(int axisNumber) const
{
    return scale(axisNumber) != nullptr;
}

bool Model::scaleHasReading(int axisNumber) const
{
    return scale(axisNumber) && scale(axisNumber)->hasReading();
}

std::optional<double> Model::scalePositionMm(int axisNumber) const
{
    if (!scale(axisNumber)) {
        return std::nullopt;
    }
    return scale(axisNumber)->positionMm();
}

std::optional<double> Model::scaleDifferenceMm(int axisNumber) const
{
    if (!scale(axisNumber) || !motor(axisNumber)) {
        return std::nullopt;
    }
    return scale(axisNumber)->differenceMm(motor(axisNumber)->getPosition());
}

void Model::logMotorMovement(const StepperMotor& motor, MotorLog& log, const std::string& label)
{
    const bool running = motor.isRunning();
    const long step = motor.getCurrentStep();
    // (Adding 0.0 turns -0.0, from a negative conversion factor, into 0.0)
    const auto position = [&motor](long s) { return motor.getPosition(s) + 0.0; };
    if (running && !log.running) {
        // (A move with no end, e.g. an arrow key's, goes towards one of these)
        const long target = motor.getTargetStep();
        const bool endless = target == INF_LEFT || target == INF_RIGHT;
        MGOLOG(
            fmt::format(
                "{} moving from {:.4f} (seen at {:.4f}), {}, at {:.0f}/min",
                label,
                position(log.step),
                position(step),
                endless ? (target == INF_LEFT ? "with no end (step count rising)"
                                              : "with no end (step count falling)")
                        : fmt::format("to {:.4f}", position(target)),
                motor.getSpeed()));
    } else if (!running && log.running) {
        MGOLOG(fmt::format("{} stopped at {:.4f}", label, position(step)));
    } else if (!running && step != log.step) {
        // Started and stopped between two updates (e.g. a nudge)
        MGOLOG(
            fmt::format(
                "{} moved from {:.4f} to {:.4f} between updates, at {:.0f}/min",
                label,
                position(log.step),
                position(step),
                motor.getSpeed()));
    }
    log.running = running;
    log.step = step;
}

double Model::backlashCompensationMm(int axisNumber) const
{
    return m_config.readLong(fmt::format("Axis{}BacklashCompensationSteps", axisNumber), 0)
        * std::abs(motor(axisNumber)->getConversionFactor());
}

bool Model::canMeasureAxis1Backlash() const
{
    return scale(1) && scale(1)->isAligned() && !m_backlashMeasurement && !scaleState(1).finish
        && m_mode == Mode::None && !isAxisLocked(1) && !motor(1)->isRunning()
        && !motor(2)->isRunning() && !isMotionSequenceRunning() && !limitSwitchTripped()
        && !motor(1)->isFaulted();
}

bool Model::isMeasuringBacklash() const
{
    return m_backlashMeasurement.has_value();
}

double Model::axis1BacklashMeasureDistanceMm() const
{
    // Comfortably more than any shortfall: even with no compensation set, the
    // whole backlash is less than the move
    return std::max(1.0, 2.0 * backlashCompensationMm(1));
}

int Model::axis1BacklashMeasureFirstDirection() const
{
    // (Away from the chuck, as the safer way for a lathe's Z)
    return m_config.read("Axis1TowardsChuck", "negative") == "positive" ? -1 : 1;
}

void Model::axis1MeasureBacklash()
{
    if (!canMeasureAxis1Backlash()) {
        return;
    }
    m_backlashPreviousSpeed = motor(1)->getSpeed();
    motor(1)->setSpeed(m_config.readDouble("Axis1SpeedPreset2", 40.0));
    m_backlashMeasurement.emplace(
        motor(1)->getPosition(),
        axis1BacklashMeasureDistanceMm(),
        axis1BacklashMeasureFirstDirection(),
        backlashCompensationMm(1));
    MGOLOG(
        fmt::format(
            "Measuring {}'s backlash: moves of {:.3f} mm, with compensation of {} steps "
            "({:.4f} mm)",
            axisLabel(1),
            axis1BacklashMeasureDistanceMm(),
            m_config.readLong("Axis1BacklashCompensationSteps", 0),
            backlashCompensationMm(1)));
    backlashMeasurementMove();
}

void Model::backlashMeasurementMove()
{
    const long step
        = std::lround(*m_backlashMeasurement->nextTarget() / motor(1)->getConversionFactor());
    m_backlashSettleCount = scale(1)->settleCount();
    motor(1)->goToStep(step);
    if (!motor(1)->isRunning() && motor(1)->getCurrentStep() != step) {
        // Refused (e.g. a limit switch, or the driver's alarm)
        endBacklashMeasurement(fmt::format("{} didn't move", axisLabel(1)));
    }
}

void Model::advanceBacklashMeasurement()
{
    if (!m_backlashMeasurement) {
        return;
    }
    const std::string label = axisLabel(1);
    const auto scaleMm = scale(1)->positionMm();
    if (!scaleMm) {
        endBacklashMeasurement(fmt::format("{}'s scale has no reading", label));
        return;
    }
    if (motor(1)->isRunning() || scale(1)->settleCount() == m_backlashSettleCount) {
        return;
    }
    m_backlashMeasurement->record(motor(1)->getPosition(), *scaleMm);
    if (!m_backlashMeasurement->finished()) {
        backlashMeasurementMove();
        return;
    }
    const BacklashMeasurement::Result r = m_backlashMeasurement->result();
    const double perStep = std::abs(motor(1)->getConversionFactor());
    const long steps = std::lround(r.backlashMm() / perStep);
    const double countMm = 1.0 / scale(1)->countsPerMm();
    MGOLOG(
        fmt::format(
            "{}'s backlash measured: {:.4f} mm ({} steps); shortfall after reversing {:+.4f} mm "
            "positive, {:+.4f} mm negative, spread {:.4f} mm",
            label,
            r.backlashMm(),
            steps,
            r.positiveShortfallMm,
            r.negativeShortfallMm,
            r.spreadMm));
    std::vector<std::string> report {
        fmt::format("{}'s backlash is {:.3f} mm, which is {} steps.", label, r.backlashMm(), steps),
        fmt::format(
            "Axis1BacklashCompensationSteps is {} now.",
            m_config.readLong("Axis1BacklashCompensationSteps", 0)),
        "To use the new value, change it in the config file.",
        "",
        fmt::format(
            "After reversing, {} differed by {:+.3f} mm moving right, and {:+.3f} mm moving left",
            label,
            r.positiveShortfallMm,
            r.negativeShortfallMm),
        fmt::format(
            "(the measurements varied by {:.3f} mm; a count of the scale is {:.3f} mm).",
            r.spreadMm,
            countMm),
    };
    if (r.spreadMm > 3.0 * countMm) {
        report.push_back("");
        report.push_back("They varied by more than a few counts, so take this as a rough guide,");
        report.push_back("and perhaps measure again.");
    }
    m_backlashReport = std::move(report);
    endBacklashMeasurement("finished");
}

void Model::endBacklashMeasurement(const std::string& why)
{
    MGOLOG(fmt::format("Backlash measurement {}", why));
    if (why != "finished" && why != "stopped") {
        m_warning = fmt::format("Backlash measurement stopped: {}", why);
    }
    m_backlashMeasurement.reset();
    motor(1)->setSpeed(m_backlashPreviousSpeed);
}

std::optional<std::vector<std::string>> Model::takeBacklashReport()
{
    return std::exchange(m_backlashReport, std::nullopt);
}

void Model::checkScaleAtRest(int axisNumber)
{
    ScaleState& state = scaleState(axisNumber);
    const auto key = [axisNumber](std::string_view name) {
        return fmt::format("Axis{}Scale{}", axisNumber, name);
    };
    // (Not while measuring the backlash, which compares them itself)
    if (!scale(axisNumber) || m_backlashMeasurement) {
        return;
    }
    // A misread stops everything at once, rather than once the reading has
    // settled: it may not, for a long time (half an hour, on the mill)
    if (const auto jump = scale(axisNumber)->misreadJumpMm(); jump && !state.misreadJumpMm) {
        const auto difference = scaleDifferenceMm(axisNumber);
        MGOLOG(fmt::format("{}'s scale is misreading: stopping everything", axisLabel(axisNumber)));
        stopAllMotors();
        state.misreadJumpMm = *jump;
        state.mismatchMm = difference.value_or(*jump);
        state.mismatchLimitMm = m_config.readDouble(key("FaultMm"), 0.0);
        state.askAgain = false;
        ++state.mismatchAsks;
        return;
    }
    // A stall seen while moving stops everything at once, rather than once
    // the reading has settled, as the motor would carry on regardless (seen on
    // the mill, 19 Nov 2026: a fast return counted 29 mm while the table
    // stayed put). Once settled, the difference is checked as usual, which
    // asks which to believe if it's more than Axis1ScaleWarnMm
    if (const auto stall = scale(axisNumber)->stallMm(); !stall) {
        state.stallStopped = false;
    } else if (!state.stallStopped) {
        state.stallStopped = true;
        const std::string label = axisLabel(axisNumber);
        MGOLOG(fmt::format("{} stalled: stopping everything", label));
        stopAllMotors();
        m_warning = fmt::format(
            "{} stalled ({:.2f} {} behind): stopped",
            label,
            toDisplayUnits(axisNumber, *stall),
            axisUnitName(axisNumber));
    }
    // Once each time the scale settles
    const int settles = scale(axisNumber)->settleCount();
    if (settles == state.checkedSettles) {
        return;
    }
    state.checkedSettles = settles;
    if (state.mismatchMm) {
        // The scale's position was chosen while its reading was still
        // changing: now it has settled, ask again (it may have settled
        // somewhere else entirely)
        if (state.askAgain) {
            if (const auto difference = scaleDifferenceMm(axisNumber)) {
                state.mismatchMm = *difference;
            }
            state.askAgain = false;
            ++state.mismatchAsks;
        }
        return;
    }
    const auto difference = scaleDifferenceMm(axisNumber);
    if (!difference || motor(axisNumber)->isRunning() || motor(axisNumber)->isFaulted()) {
        return;
    }
    const std::string label = axisLabel(axisNumber);
    double limit = m_config.readDouble(key("WarnMm"), 0.0);
    // A table which has gone further than the motor, the way it was moving,
    // has carried on into the slack: the motor stops dead, even from full
    // speed (seen on the mill: 0.12-0.29 mm after stops from 800 mm/min).
    // That's expected, up to about the backlash, and a stall can't do it (it
    // leaves the table behind), so that much more is allowed that way
    const int direction = scale(axisNumber)->lastMoveDirection();
    if (limit > 0.0 && direction != 0 && *difference * direction > 0.0) {
        limit += backlashCompensationMm(axisNumber);
    }
    if (limit > 0.0 && std::abs(*difference) > limit) {
        // Something's wrong: the motor lost steps (e.g. a stall), or the scale
        // misread. So everything stops (e.g. a multi-pass), until the user
        // says which to believe
        MGOLOG(
            fmt::format(
                "*** Warning *** {}'s scale and motor are {:+.4f} mm apart (more than {:.4f}): "
                "stopping everything",
                label,
                *difference,
                limit));
        stopAllMotors();
        state.mismatchMm = *difference;
        state.mismatchLimitMm = limit;
        ++state.mismatchAsks;
        return;
    }
    // Paused between a multi-pass's passes, only with its own setting. The
    // next pass moves the axis to its start first, from wherever the re-base
    // leaves it, so the cut goes where the table really is: without it, a
    // stall or drift builds up from pass to pass (seen on the mill, 18 Nov
    // 2026)
    if (multiPassPausedForRebase()) {
        if (m_config.readBool(key("RebaseBetweenPasses"), false)) {
            rebaseIfApart(axisNumber, "re-based between passes");
        }
        return;
    }
    if (!m_config.readBool(key("Rebase"), false)) {
        return;
    }
    // Not part way through something which works from the motor's position,
    // e.g. while a multi-pass's pass is under way, nor in the lathe's modes
    // (threading, tapers, etc.), which haven't been tried with it
    if (!scaleAtRestMode() || m_multiPass.inProgress()) {
        return;
    }
    rebaseIfApart(axisNumber, "re-based");
}

bool Model::multiPassPausedForRebase() const
{
    return m_multiPass.state() == MultiPass::State::Paused
        && (m_mode == Mode::MultiPass || m_mode == Mode::GearCutting);
}

void Model::rebaseIfApart(int axisNumber, const char* why)
{
    // Not from a reading which only settled because it ran out of time, still
    // jumping about (e.g. a misreading scale), nor while something is moving
    // the axis (e.g. a nudge), nor while the scale and motor disagree (the
    // user is asked which to believe)
    if (!scale(axisNumber) || !scale(axisNumber)->isSteady() || motor(axisNumber)->isRunning()
        || isMotionSequenceRunning() || limitSwitchTripped() || scaleState(axisNumber).mismatchMm) {
        return;
    }
    const auto difference = scaleDifferenceMm(axisNumber);
    // (Less than a count apart is as close as the scale can tell)
    if (!difference || std::abs(*difference) * scale(axisNumber)->countsPerMm() < 1.0) {
        return;
    }
    rebaseFromScale(axisNumber, why);
}

bool Model::feedReady()
{
    // (As for cutReady(), with nothing to wait for without a reading)
    if (!scale(2) || !m_config.readBool("Axis2ScaleRebaseBetweenPasses", false)
        || (m_mode != Mode::MultiPass && m_mode != Mode::GearCutting) || !scale(2)->isAligned()
        || !scale(2)->hasReading()) {
        return true;
    }
    if (!scale(2)->isSettled()) {
        return false;
    }
    rebaseIfApart(2, "re-based after the feed");
    return true;
}

bool Model::cutReady()
{
    // (Without a reading, or before it's lined up, there's nothing to wait
    // for)
    if (!scale(1) || !m_config.readBool("Axis1ScaleRebaseBetweenPasses", false)
        || (m_mode != Mode::MultiPass && m_mode != Mode::GearCutting) || !scale(1)->isAligned()
        || !scale(1)->hasReading()) {
        return true;
    }
    // (The return's approach has just re-based it, at the start)
    if (std::exchange(m_returnApproachRebased, false)) {
        return true;
    }
    // (Settling ends after Axis1ScaleMaxSettleMs at most, even if the reading
    // keeps changing, so this doesn't wait for ever. checkScaleAtRest(),
    // called before the multi-pass is updated, has then compared it with the
    // motor, and stopped everything if they're too far apart)
    if (!scale(1)->isSettled()) {
        return false;
    }
    rebaseIfApart(1, "re-based between passes");
    return true;
}

void Model::rebaseFromScale(int axisNumber, const char* why)
{
    const auto scaleMm = scalePositionMm(axisNumber);
    if (!scaleMm) {
        return;
    }
    const double before = motor(axisNumber)->getPosition();
    motor(axisNumber)->setPosition(*scaleMm);
    axisState(axisNumber).motorLog.step = motor(axisNumber)->getCurrentStep();
    scale(axisNumber)->motorRebased(motor(axisNumber)->getPosition());
    MGOLOG(
        fmt::format(
            "{} {} from its scale: {:.4f} to {:.4f} ({:+.4f} mm)",
            axisLabel(axisNumber),
            why,
            before,
            motor(axisNumber)->getPosition(),
            motor(axisNumber)->getPosition() - before));
}

bool Model::finishesOnScale(int axisNumber) const
{
    return scaleState(axisNumber).finishOnScale;
}

void Model::toggleFinishOnScale(int axisNumber)
{
    ScaleState& state = scaleState(axisNumber);
    state.finishOnScale = !state.finishOnScale;
    MGOLOG(
        fmt::format(
            "Finishing {}'s moves on the scale turned {}",
            axisLabel(axisNumber),
            state.finishOnScale ? "on" : "off"));
    if (!state.finishOnScale && state.finish) {
        // (Where it's got to is where it stays)
        stopAllMotors();
    }
}

bool Model::anyFinishingOnScale() const
{
    return isFinishingOnScale(1) || isFinishingOnScale(2);
}

bool Model::isFinishingOnScale(int axisNumber) const
{
    return scaleState(axisNumber).finish.has_value();
}

bool Model::finishApplies(int axisNumber) const
{
    return scaleState(axisNumber).finishOnScale && scale(axisNumber)
        && scale(axisNumber)->isAligned() && scaleAtRestMode() && !m_multiPass.inProgress();
}

double Model::scaleApproachMm(int axisNumber) const
{
    return std::max(
        0.0, m_config.readDouble(fmt::format("Axis{}ScaleApproachMm", axisNumber), 0.0));
}

double Model::scaleApproachSpeed(int axisNumber) const
{
    return m_config.readDouble(fmt::format("Axis{}ScaleApproachSpeed", axisNumber), 20.0);
}

double Model::creepSpeed(int axisNumber) const
{
    // (Never faster than it was going: e.g. a fine cut)
    return std::min(scaleApproachSpeed(axisNumber), motor(axisNumber)->getSpeed());
}

double Model::speedAfterStop(int axisNumber, double speed) const
{
    // (As checkStatus() does, each axis as it always has: axis 1's above
    // the limit, axis 2's at or above it)
    const double above = m_config.readDouble(
        fmt::format("Axis{}SpeedResetAbove", axisNumber), axisNumber == 1 ? 100.0 : 80.0);
    const bool resets = axisNumber == 1 ? speed > above : speed >= above;
    return resets
        ? m_config.readDouble(
              fmt::format("Axis{}SpeedResetTo", axisNumber), axisNumber == 1 ? 40.0 : 20.0)
        : speed;
}

Model::FinishStart Model::prepareFinish(int axisNumber, long step)
{
    StepperMotor* m = motor(axisNumber);
    FinishStart start { .firstStep = step, .speed = m->getSpeed() };
    const double margin = scaleApproachMm(axisNumber);
    if (margin <= 0.0 || !finishApplies(axisNumber)) {
        return start;
    }
    const double target = m->getPosition(step);
    const double distance = target - m->getPosition();
    if (std::abs(distance) > margin) {
        const int direction = distance > 0.0 ? 1 : -1;
        start.firstStep = std::lround((target - direction * margin) / m->getConversionFactor());
    } else {
        // (All of it is the approach)
        m->setSpeed(creepSpeed(axisNumber));
    }
    return start;
}

void Model::startFinish(int axisNumber, long step, const FinishStart& start)
{
    StepperMotor* m = motor(axisNumber);
    scaleState(axisNumber).finish.reset();
    if (!finishApplies(axisNumber)) {
        return;
    }
    // (Only if it's moving: not if refused, e.g. a locked axis, or already there)
    if (!m->isRunning() && !isMotionSequenceRunning()) {
        m->setSpeed(start.speed);
        return;
    }
    const double target = m->getPosition(step);
    scaleState(axisNumber).finish = ScaleFinish {
        .targetMm = target,
        .direction = target >= m->getPosition() ? 1 : -1,
        .settles = scale(axisNumber)->settleCount(),
        .approaching = start.firstStep != step,
    };
    if (m->getSpeed() != start.speed) {
        // (Creeping all the way: see prepareFinish())
        scaleState(axisNumber).finish->speedBeforeCreep = start.speed;
    }
    if (start.firstStep != step) {
        MGOLOG(
            fmt::format(
                "{} going to {:.4f} on the scale: stopping {:.3f} mm short first",
                axisLabel(axisNumber),
                target,
                scaleApproachMm(axisNumber)));
    }
}

void Model::advanceFinish(int axisNumber)
{
    if (!scaleState(axisNumber).finish || motor(axisNumber)->isRunning()
        || isMotionSequenceRunning()) {
        return;
    }
    ScaleFinish& f = *scaleState(axisNumber).finish;
    const double margin = scaleApproachMm(axisNumber);
    if (f.backingOff) {
        // (No need to wait for the scale: it's only to come back.) With an
        // approach, to the approach's start, and then creep the rest
        f.backingOff = false;
        f.approaching = margin > 0.0;
        finishMove(axisNumber, f.approachMm - f.direction * margin, false);
        return;
    }
    if (scale(axisNumber)->settleCount() == f.settles) {
        return;
    }
    const auto scaleMm = scale(axisNumber)->positionMm();
    if (!scaleMm) {
        endFinish(axisNumber, "no scale reading");
        return;
    }
    // (A correction from a reading still jumping about could go anywhere)
    if (!scale(axisNumber)->isSteady()) {
        endFinish(axisNumber, "the scale's reading didn't settle");
        return;
    }
    const double countMm = 1.0 / scale(axisNumber)->countsPerMm();
    const double error = f.targetMm - *scaleMm;
    // (A count is as close as the scale can tell)
    if (std::abs(error) < 1.5 * countMm) {
        endFinish(axisNumber, fmt::format("on target ({:+.4f} mm)", -error));
        return;
    }
    // Stopped short on purpose (see scaleApproachMm()): creep the rest, the
    // same way. (Not a correction: it was the plan.) If it's past already, it
    // overshot by more than the approach: the corrections below back off and
    // approach again
    const bool approached = std::exchange(f.approaching, false);
    if (approached && error * f.direction < 0.0) {
        warnApproachTooShort(axisNumber, std::abs(error));
    }
    if (approached && error * f.direction > 0.0) {
        MGOLOG(
            fmt::format(
                "{} {:.4f} mm short of {:.4f}: creeping the rest",
                axisLabel(axisNumber),
                std::abs(error),
                f.targetMm));
        finishMove(axisNumber, motor(axisNumber)->getPosition() + error, true);
        return;
    }
    if (f.corrections >= MAX_SCALE_CORRECTIONS) {
        const std::string label = axisLabel(axisNumber);
        m_warning = fmt::format(
            "{} is {:.3f} {} from its target after {} corrections",
            label,
            toDisplayUnits(axisNumber, std::abs(error)),
            axisUnitName(axisNumber),
            f.corrections);
        endFinish(axisNumber, fmt::format("still {:+.4f} mm out", -error));
        return;
    }
    ++f.corrections;
    // (The motor's position and the scale's agree, if re-based; this works
    // either way)
    const double approach = motor(axisNumber)->getPosition() + error;
    if (error * f.direction > 0.0) {
        MGOLOG(
            fmt::format(
                "{} {:.4f} mm short of {:.4f}: moving on (correction {})",
                axisLabel(axisNumber),
                std::abs(error),
                f.targetMm,
                f.corrections));
        // (Creeping, with an approach: it's only a little way, into the work)
        finishMove(axisNumber, approach, margin > 0.0);
    } else {
        // Past it: back off past the backlash, and approach again the same
        // way, rather than reverse onto it (which would leave the slack the
        // other way, and could push back into the work)
        const double backOff = std::max(0.5, 2.0 * backlashCompensationMm(axisNumber));
        MGOLOG(
            fmt::format(
                "{} {:.4f} mm past {:.4f}: backing off {:.3f} mm to approach again (correction "
                "{})",
                axisLabel(axisNumber),
                std::abs(error),
                f.targetMm,
                backOff,
                f.corrections));
        f.backingOff = true;
        f.approachMm = approach;
        finishMove(axisNumber, approach - f.direction * (backOff + margin), false);
    }
}

void Model::finishMove(int axisNumber, double position, bool creep)
{
    StepperMotor* m = motor(axisNumber);
    ScaleFinish& f = *scaleState(axisNumber).finish;
    if (creep) {
        if (!f.speedBeforeCreep) {
            f.speedBeforeCreep = m->getSpeed();
        }
        m->setSpeed(std::min(scaleApproachSpeed(axisNumber), *f.speedBeforeCreep));
    } else if (f.speedBeforeCreep) {
        m->setSpeed(speedAfterStop(axisNumber, *std::exchange(f.speedBeforeCreep, std::nullopt)));
    }
    const long step = std::lround(position / m->getConversionFactor());
    f.settles = scale(axisNumber)->settleCount();
    m->goToStep(step);
    if (!m->isRunning() && m->getCurrentStep() != step) {
        // Refused (e.g. a limit switch, or the driver's alarm)
        endFinish(axisNumber, "the move was refused");
    }
}

void Model::endFinish(int axisNumber, const std::string& why)
{
    if (scaleState(axisNumber).finish) {
        MGOLOG(
            fmt::format(
                "{}: finishing on the scale at {:.4f} ended ({}), after {} correction{}",
                axisLabel(axisNumber),
                scaleState(axisNumber).finish->targetMm,
                why,
                scaleState(axisNumber).finish->corrections,
                scaleState(axisNumber).finish->corrections == 1 ? "" : "s"));
        if (const auto speed = scaleState(axisNumber).finish->speedBeforeCreep) {
            motor(axisNumber)->setSpeed(speedAfterStop(axisNumber, *speed));
        }
    }
    scaleState(axisNumber).finish.reset();
}

std::optional<double> Model::scaleMismatchMm(int axisNumber) const
{
    return scaleState(axisNumber).mismatchMm;
}

double Model::scaleMismatchLimitMm(int axisNumber) const
{
    return scaleState(axisNumber).mismatchLimitMm;
}

std::optional<double> Model::scaleMisreadJumpMm(int axisNumber) const
{
    return scaleState(axisNumber).misreadJumpMm;
}

int Model::scaleMismatchAsks(int axisNumber) const
{
    return scaleState(axisNumber).mismatchAsks;
}

bool Model::scaleIsSteady(int axisNumber) const
{
    return scale(axisNumber) && scale(axisNumber)->isSteady();
}

void Model::acceptScalePosition(int axisNumber)
{
    ScaleState& state = scaleState(axisNumber);
    if (!state.mismatchMm) {
        return;
    }
    const std::string label = axisLabel(axisNumber);
    // (Only answered this way from a dialog shown before the misread was
    // seen: its own dialog follows)
    if (state.misreadJumpMm) {
        MGOLOG(fmt::format("{}'s scale is misreading: its position isn't used", label));
        return;
    }
    // A reading which is still changing could be anywhere: on the mill, once,
    // thousands of mm out. So it waits for the reading to settle, and asks
    // again (see checkScaleAtRest())
    if (!scale(axisNumber) || !scale(axisNumber)->isSteady()) {
        MGOLOG(
            fmt::format(
                "{}'s scale reading isn't steady, so its position isn't used yet: asking again "
                "once it has settled",
                label));
        state.askAgain = true;
        return;
    }
    state.mismatchMm.reset();
    rebaseFromScale(axisNumber, "set (the scale's position accepted)");
}

bool Model::anyScaleMismatch() const
{
    return scaleState(1).mismatchMm || scaleState(2).mismatchMm;
}

bool Model::scalesStillSettling() const
{
    for (const int n : { 1, 2 }) {
        const AxisScale* sc = scale(n);
        if (sc && !scaleState(n).mismatchMm
            && (motor(n)->isRunning() || (sc->hasReading() && !sc->isSettled()))) {
            return true;
        }
    }
    return false;
}

bool Model::canRebaseFromScale(int axisNumber) const
{
    const bool atRest = scaleAtRestMode() && !m_multiPass.inProgress();
    return scale(axisNumber) && !motor(axisNumber)->isRunning() && scalePositionMm(axisNumber)
        && scale(axisNumber)->isSteady() && (atRest || multiPassPausedForRebase())
        && !isMotionSequenceRunning() && !scaleState(axisNumber).finish && !m_backlashMeasurement;
}

void Model::rebaseFromScaleByHand(int axisNumber)
{
    if (!scale(axisNumber)) {
        return;
    }
    const std::string label = axisLabel(axisNumber);
    if (motor(axisNumber)->isRunning()) {
        m_warning = "Stop the motors first";
        return;
    }
    if (!scalePositionMm(axisNumber)) {
        m_warning = fmt::format("{}'s scale has no reading", label);
        return;
    }
    if (!scale(axisNumber)->isSteady()) {
        m_warning = fmt::format("{}'s scale is still settling: try again in a moment", label);
        return;
    }
    if (!canRebaseFromScale(axisNumber)) {
        m_warning = fmt::format("{} can't be re-based from its scale now", label);
        return;
    }
    rebaseFromScale(axisNumber, "re-based by hand");
}

void Model::keepMotorPosition(int axisNumber)
{
    ScaleState& state = scaleState(axisNumber);
    if (!state.mismatchMm) {
        return;
    }
    state.mismatchMm.reset();
    state.misreadJumpMm.reset();
    state.askAgain = false;
    MGOLOG(
        fmt::format(
            "{}'s position kept: lining its scale up with it again", axisLabel(axisNumber)));
    if (scale(axisNumber)) {
        scale(axisNumber)->realign();
    }
}

#ifdef FAKE
FakeDroScale* Model::fakeScale(int axisNumber)
{
    return scaleState(axisNumber).fake;
}
#endif

void Model::configureScale(int axisNumber)
{
    const auto key = [axisNumber](std::string_view name) {
        return fmt::format("Axis{}Scale{}", axisNumber, name);
    };
    const std::string type = m_config.read(key(""), "none");
    if (type == "none") {
        return;
    }
    AxisScale::Settings settings {
        .axisLabel = axisLabel(axisNumber),
        .countsPerMm = m_config.readDouble(key("CountsPerMm"), 200.0),
        .reversed = m_config.readBool(key("Reversed"), false),
        .settleTime = std::chrono::milliseconds(m_config.readLong(key("SettleMs"), 500)),
        .maxSettleTime = std::chrono::milliseconds(m_config.readLong(key("MaxSettleMs"), 3'000)),
        .faultMm = m_config.readDouble(key("FaultMm"), 0.0),
        .lag = std::chrono::milliseconds(m_config.readLong(key("LagMs"), 0)),
        .stallMm = m_config.readDouble(key("StallMm"), 0.0),
        .movingFaultMm = m_config.readDouble(key("MovingFaultMm"), 0.0),
    };
    // (Simulated, following the motor's actual steps, as a table with no
    // backlash would: so neither the backlash compensation nor re-basing the
    // motor's position changes where it thinks the table is. The motor is
    // reset after the scale; see resetMotorThreads())
    const auto followMotor = [this, axisNumber]() {
        return motor(axisNumber)
            ->getPosition(motor(axisNumber)->getCurrentStepWithoutBacklashCompensation());
    };
    FakeDroScale* fake = nullptr;
    std::string source;
    std::unique_ptr<IScale> scale = makeScale(
        axisNumber, type, settings.countsPerMm, settings.reversed, followMotor, &fake, source);
    if (!scale) {
        return;
    }
#ifdef FAKE
    scaleState(axisNumber).fake = fake;
#endif
    scaleState(axisNumber).finishOnScale = m_config.readBool(key("FinishMoves"), false);
    MGOLOG(
        fmt::format(
            "{} scale: {}, {} counts per mm{}",
            settings.axisLabel,
            source,
            settings.countsPerMm,
            settings.reversed ? ", reversed" : ""));
    scaleState(axisNumber).scale
        = std::make_unique<AxisScale>(std::move(scale), std::move(settings));
}

#ifdef FAKE
double Model::fakeSlackMm(int axisNumber) const
{
    // (A manual axis's simulated scale follows a position set by the tests:
    // no motor, so no slack)
    if (axisNumber >= FIRST_MANUAL_AXIS) {
        return 0.0;
    }
    const long compensation = static_cast<long>(
        m_config.readLong(fmt::format("Axis{}BacklashCompensationSteps", axisNumber), 0));
    return static_cast<double>(
               m_config.readLong(fmt::format("Axis{}FakeBacklashSteps", axisNumber), compensation))
        * motor(axisNumber)->getConversionFactor();
}
#endif

std::unique_ptr<IScale> Model::makeScale(
    int axisNumber,
    const std::string& type,
    double countsPerMm,
    bool reversed,
    std::function<double()> fakePosition,
    FakeDroScale** fake,
    std::string& source)
{
    const auto key = [axisNumber](std::string_view name) {
        return fmt::format("Axis{}Scale{}", axisNumber, name);
    };
    if (countsPerMm <= 0.0) {
        MGOLOG(
            fmt::format(
                "*** Warning *** {} should be more than zero, not {}: no scale",
                key("CountsPerMm"),
                countsPerMm));
        return nullptr;
    }
    if (type == "quadrature") {
        // Axis 1's pins default to 5 and 6, as they always have. Any other
        // axis's must be given, so two scales can't end up on the same pins
        if (axisNumber != 1
            && (m_config.read(key("GpioPinA"), "").empty()
                || m_config.read(key("GpioPinB"), "").empty())) {
            MGOLOG(
                fmt::format(
                    "*** Warning *** {} and {} must be set for a quadrature scale: no scale",
                    key("GpioPinA"),
                    key("GpioPinB")));
            return nullptr;
        }
        const long pinA = m_config.readLong(key("GpioPinA"), 5);
        const long pinB = m_config.readLong(key("GpioPinB"), 6);
        source = fmt::format("GPIO {} and {}", pinA, pinB);
#ifdef FAKE
        // (The mock GPIO simulates one quadrature scale, axis 1's: any
        // other follows its motor, as a simulated serial scale does)
        if (axisNumber != 1 && fakePosition) {
            auto simulated = std::make_unique<FakeDroScale>(
                std::move(fakePosition), countsPerMm, reversed, fakeSlackMm(axisNumber));
            *fake = simulated.get();
            source += " (simulated)";
            return simulated;
        }
#endif
        const long glitchUs = m_config.readLong(key("GlitchUs"), 0);
        if (glitchUs > 0) {
            source += fmt::format(", ignoring spikes under {} us", glitchUs);
        }
        return std::make_unique<LinearScale>(
            m_gpio,
            pinA,
            pinB,
            std::lround(countsPerMm),
            static_cast<unsigned>(std::max(glitchUs, 0L)));
    }
    if (type == "serial") {
        const std::string letter = m_config.read(key("SerialLetter"), "X");
        if (letter.size() != 1 || letter[0] < 'A' || letter[0] > 'Z') {
            MGOLOG(
                fmt::format(
                    "*** Warning *** {} should be a capital letter, not \"{}\": no scale",
                    key("SerialLetter"),
                    letter));
            return nullptr;
        }
        const std::string device = m_config.read(key("SerialDevice"), "/dev/ttyUSB0");
        // (The sender's one stream has every axis's scale, so it's read once)
        if (m_droDevice.empty()) {
            m_droDevice = device;
        } else if (device != m_droDevice) {
            MGOLOG(
                fmt::format(
                    "*** Warning *** {} is {}, but the scales are already read from {}: "
                    "reading it from there",
                    key("SerialDevice"),
                    device,
                    m_droDevice));
        }
#ifdef FAKE
        auto simulated = std::make_unique<FakeDroScale>(
            std::move(fakePosition), countsPerMm, reversed, fakeSlackMm(axisNumber));
        *fake = simulated.get();
        source = fmt::format("{} on {} (simulated)", letter, m_droDevice);
        return simulated;
#else
        (void)fakePosition;
        (void)fake;
        (void)reversed;
        if (!m_droReader) {
            m_droReader = std::make_unique<DroSerialReader>(m_droDevice);
        }
        source = fmt::format("{} on {}", letter, m_droDevice);
        return std::make_unique<DroSerialScale>(*m_droReader, letter[0]);
#endif
    }
    MGOLOG(
        fmt::format(
            "*** Warning *** Axis{}Scale should be {}quadrature or serial, not \"{}\": no "
            "scale",
            axisNumber,
            axisNumber < FIRST_MANUAL_AXIS ? "none, " : "",
            type));
    return nullptr;
}

void Model::configureManualAxes()
{
    for (int n = FIRST_MANUAL_AXIS; n <= LAST_MANUAL_AXIS; ++n) {
        const std::string kind = m_config.read(fmt::format("Axis{}Kind", n), "none");
        if (kind == "none") {
            continue;
        }
        if (kind != "manual") {
            MGOLOG(
                fmt::format(
                    "*** Warning *** Axis{}Kind should be none or manual, not \"{}\": no axis {}",
                    n,
                    kind,
                    n));
            continue;
        }
        ManualAxis::Settings settings {
            .label = m_config.read(fmt::format("Axis{}Label", n), n == 3 ? "Y" : "Z"),
            .countsPerMm = m_config.readDouble(fmt::format("Axis{}ScaleCountsPerMm", n), 100.0),
            .reversed = m_config.readBool(fmt::format("Axis{}ScaleReversed", n), false),
            .settleTime = std::chrono::milliseconds(
                m_config.readLong(fmt::format("Axis{}ScaleSettleMs", n), 500)),
            // (Not known unless set)
            .clockwiseIncreases
            = m_config.read(fmt::format("Axis{}ClockwiseIncreases", n), "").empty()
                ? std::nullopt
                : std::optional(
                      m_config.readBool(fmt::format("Axis{}ClockwiseIncreases", n), true)),
            .handleName = m_config.read(fmt::format("Axis{}HandleName", n), ""),
        };
        // (A manual axis is only known from its scale, so it must have one)
        const std::string type = m_config.read(fmt::format("Axis{}Scale", n), "none");
        if (type == "none") {
            MGOLOG(
                fmt::format(
                    "*** Warning *** axis {} ({}) is manual, so needs a scale (Axis{}Scale): no "
                    "axis {}",
                    n,
                    settings.label,
                    n,
                    n));
            continue;
        }
        const std::size_t index = static_cast<std::size_t>(n - FIRST_MANUAL_AXIS);
        std::function<double()> handPosition = [] { return 0.0; };
#ifdef FAKE
        handPosition = [this, index] { return m_fakeManualAxisMm.at(index).load(); };
#endif
        FakeDroScale* fake = nullptr;
        std::string source;
        std::unique_ptr<IScale> scale = makeScale(
            n, type, settings.countsPerMm, settings.reversed, handPosition, &fake, source);
        if (!scale) {
            continue;
        }
        MGOLOG(
            fmt::format(
                "{} (axis {}) is moved by hand; its scale: {}, {} counts per mm{}",
                settings.label,
                n,
                source,
                settings.countsPerMm,
                settings.reversed ? ", reversed" : ""));
        // (Its leader key mustn't be another axis's, which would win)
        const unsigned long leader = m_config.readLong(fmt::format("Axis{}Leader", n), 0);
        for (int other = 1; other < n; ++other) {
            if (leader != 0 && leader == m_config.readLong(fmt::format("Axis{}Leader", other), 0)) {
                MGOLOG(
                    fmt::format(
                        "*** Warning *** Axis{}Leader is the same as Axis{}Leader", n, other));
            }
        }
        m_manualAxes.at(index)
            = std::make_unique<ManualAxis>(std::move(scale), std::move(settings));
        m_manualOriginsSeen.at(index) = m_manualAxes.at(index)->originChanges();
    }
}

const ManualAxis* Model::manualAxis(int axisNumber) const
{
    if (axisNumber < FIRST_MANUAL_AXIS || axisNumber > LAST_MANUAL_AXIS) {
        return nullptr;
    }
    return m_manualAxes.at(static_cast<std::size_t>(axisNumber - FIRST_MANUAL_AXIS)).get();
}

ManualAxis* Model::manualAxisOrWarn(int axisNumber)
{
    ManualAxis* axis = const_cast<ManualAxis*>(std::as_const(*this).manualAxis(axisNumber));
    if (axis && !axis->hasReading()) {
        m_warning = fmt::format("{} has no reading from its scale", axis->label());
        return nullptr;
    }
    return axis;
}

void Model::manualAxisZero(int axisNumber)
{
    if (ManualAxis* axis = manualAxisOrWarn(axisNumber)) {
        axis->zero();
        // (Its memory slots are cleared now, not at the next update)
        checkManualAxes();
    }
}

void Model::zeroAllAxes()
{
    for (int n = 1; n <= AXES; ++n) {
        if (hasAxis(n)) {
            zero(n);
        }
    }
}

void Model::zero(int axisNumber)
{
    switch (axisNumber) {
        case 1:
            axis1Zero();
            break;
        case 2:
            axis2Zero();
            break;
        default:
            manualAxisZero(axisNumber);
            break;
    }
}

void Model::setPosition(int axisNumber, double mm)
{
    switch (axisNumber) {
        case 1:
            setAxis1Position(mm);
            break;
        case 2:
            setAxis2Position(mm);
            break;
        default:
            setManualAxisPosition(axisNumber, mm);
            break;
    }
}

void Model::half(int axisNumber)
{
    switch (axisNumber) {
        case 1:
            axis1Half();
            break;
        case 2:
            axis2Half();
            break;
        default:
            manualAxisHalf(axisNumber);
            break;
    }
}

void Model::setManualAxisPosition(int axisNumber, double mm)
{
    if (ManualAxis* axis = manualAxisOrWarn(axisNumber)) {
        axis->setPositionMm(mm);
        checkManualAxes();
    }
}

void Model::manualAxisHalf(int axisNumber)
{
    if (ManualAxis* axis = manualAxisOrWarn(axisNumber)) {
        axis->half();
        checkManualAxes();
    }
}

void Model::manualAxisGoToCurrentMemory(int axisNumber)
{
    const auto target = getMemory(axisNumber, m_currentMemory);
    ManualAxis* axis = manualAxisOrWarn(axisNumber);
    if (!axis) {
        return;
    }
    if (!target) {
        m_warning = fmt::format("{} isn't set in this slot", axis->label());
        return;
    }
    manualAxisGoToPosition(axisNumber, *target);
}

void Model::manualAxisGoToPosition(int axisNumber, double mm)
{
    ManualAxis* axis = manualAxisOrWarn(axisNumber);
    if (!axis) {
        return;
    }
    const double from = *axis->positionMm();
    const std::size_t index = static_cast<std::size_t>(axisNumber - FIRST_MANUAL_AXIS);
    m_manualGoTos.at(index)
        = ManualGoToTarget { .targetMm = mm, .direction = mm >= from ? 1.0 : -1.0 };
    MGOLOG(fmt::format("{}: go to {:.4f} (from {:.4f}), wound by hand", axis->label(), mm, from));
}

std::optional<Model::ManualAxisGoTo> Model::manualAxisGoTo(int axisNumber) const
{
    const ManualAxis* axis = manualAxis(axisNumber);
    if (!axis) {
        return std::nullopt;
    }
    const auto& goTo = m_manualGoTos.at(static_cast<std::size_t>(axisNumber - FIRST_MANUAL_AXIS));
    const auto position = axis->positionMm();
    if (!goTo || !position) {
        return std::nullopt;
    }
    const double toGo = goTo->targetMm - *position;
    const double tolerance = 2.0 * axis->countMm();
    std::optional<bool> clockwise;
    if (const auto increases = axis->clockwiseIncreases()) {
        clockwise = (toGo > 0) == *increases;
    }
    return ManualAxisGoTo { .targetMm = goTo->targetMm,
                            .toGoMm = toGo,
                            .past = toGo * goTo->direction < -tolerance,
                            .there = std::abs(toGo) <= tolerance,
                            .clockwise = clockwise,
                            .handle = axis->handleName() };
}

void Model::cancelManualAxisGoTos()
{
    // (Including the depth's, which would otherwise start later)
    m_boltHoleDepthPending = false;
    for (std::size_t index = 0; index < MANUAL_AXES; ++index) {
        if (m_manualGoTos.at(index)) {
            MGOLOG(fmt::format("{}: go to cancelled", m_manualAxes.at(index)->label()));
            m_manualGoTos.at(index).reset();
        }
    }
}

void Model::checkManualAxes()
{
    for (std::size_t index = 0; index < MANUAL_AXES; ++index) {
        const ManualAxis* axis = m_manualAxes.at(index).get();
        if (!axis) {
            continue;
        }
        auto& goTo = m_manualGoTos.at(index);
        // Zeroed, set, halved, or started again: positions kept from before
        // would be somewhere else now
        if (axis->originChanges() != m_manualOriginsSeen.at(index)) {
            m_manualOriginsSeen.at(index) = axis->originChanges();
            clearMemory(static_cast<int>(index) + FIRST_MANUAL_AXIS);
            if (goTo) {
                MGOLOG(fmt::format("{}: go to cancelled, as its zero changed", axis->label()));
                goTo.reset();
            }
        }
        if (!goTo) {
            continue;
        }
        const int axisNumber = static_cast<int>(index) + FIRST_MANUAL_AXIS;
        const auto status = manualAxisGoTo(axisNumber);
        if (!status) {
            // (Its readings were lost, so it can't be followed)
            MGOLOG(fmt::format("{}: go to cancelled, with no reading", axis->label()));
            m_warning = fmt::format("{} has no reading from its scale", axis->label());
            goTo.reset();
            continue;
        }
        if (status->there && axis->isSettled()) {
            MGOLOG(
                fmt::format(
                    "{}: at {:.4f} ({:+.4f} mm)",
                    axis->label(),
                    status->targetMm,
                    -status->toGoMm));
            goTo.reset();
        }
    }
}

#ifdef FAKE
void Model::fakeMoveManualAxis(int axisNumber, double mm)
{
    m_fakeManualAxisMm.at(static_cast<std::size_t>(axisNumber - FIRST_MANUAL_AXIS)) = mm;
}
#endif

bool Model::limitSwitchTripped() const
{
    // (Not before initialise(), which makes the motors)
    if (!motor(1) || !motor(2)) {
        return false;
    }
    return motor(1)->isLimitTripped() || motor(2)->isLimitTripped();
}

bool Model::limitSwitchPressed() const
{
    // (Both motors are given the same pin, so either will do)
    return motor(1) && motor(1)->isLimitSwitchActive();
}

void Model::clearLimitSwitchTrip()
{
    if (!limitSwitchTripped()) {
        return;
    }
    motor(1)->clearLimitTrip();
    motor(2)->clearLimitTrip();
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
            axisLabel(1),
            motor(1)->getPosition(),
            axisLabel(2),
            motor(2)->getPosition()));
    // e.g. a multi-pass, a motion sequence, or X synchronised to Z. Each motor
    // only stops itself when it sees the switch, which an idle one may not
    // until asked to move, so this stops them all at once too. (The position
    // isn't lost, so the mode and current feature are kept)
    stopAllMotors();
}

bool Model::lockRefused(int axisNumber) const
{
    // Not while anything is moving it, so nothing under way (e.g. a pass, or
    // a nudge's sequence) finds it locked part way through. (In a taper, X
    // follows Z, so is moving whenever Z is)
    const bool follows = axisNumber == 2 && m_mode == Mode::Taper;
    const bool moving
        = motor(static_cast<int>(axisNumber))->isRunning() || (follows && motor(1)->isRunning());
    const bool multiPassRunning
        = (m_multiPass.inProgress() && m_multiPass.state() != MultiPass::State::Paused)
        || m_multiPassMovingToStart;
    return moving || m_motionSequence.isRunning() || multiPassRunning;
}

void Model::lockAxis(int axisNumber)
{
    if (lockRefused(axisNumber)) {
        const std::string label = axisLabel(static_cast<int>(axisNumber));
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

void Model::unlockAxis(int axisNumber)
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

bool Model::moveRefused(std::initializer_list<int> axisNumbers)
{
    std::string which;
    for (const int n : axisNumbers) {
        if (isAxisLocked(n)) {
            which += (which.empty() ? "" : " and ") + axisLabel(n);
        }
    }
    if (which.empty()) {
        return false;
    }
    m_warning = "Unlock " + which + " first";
    m_lockWarning = m_warning;
    m_lockRefusedAxis.reset();
    MGOLOG(fmt::format("Move refused: {} locked", which));
    return true;
}

bool Model::isAxisLocked(int axisNumber) const
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
    const MultiPassResolved resolved
        = resolveMultiPass(setup, multiPassPositions(), axisLabel(1), axisLabel(2));
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
    const MultiPassResolved resolved
        = resolveMultiPass(s, multiPassPositions(), axisLabel(1), axisLabel(2));
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
        return "Set " + axisLabel(2) + " as a diameter first: the knurl's lead depends on it";
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
    const MultiPassResolved resolved
        = resolveMultiPass(s, multiPassPositions(), axisLabel(1), axisLabel(2));
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
        // (The first time: afterwards, as last time. The starts and angle are
        // suggested by the dialog, unless chosen)
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
        return "Gear cutting needs " + axisLabel(2)
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
    const GearCutResolved resolved = resolveGearCut(setup, multiPassPositions(), axisLabel(1));
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
    const double a = motor(2)->getPosition();
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
    return std::abs(motor(2)->getPosition() - gearStartTooth())
        < indexStepSize() / 2.0 + MultiPass::TOLERANCE;
}

GearSetup Model::gearDialogSetup() const
{
    return defaultGearSetup(m_gearSetup, multiPassPositions());
}

bool Model::scaleAtRestMode() const
{
    // (Bolt holes' moves are go-tos, as with no mode, so finish on the scale
    // as those do)
    return m_mode == Mode::None || m_mode == Mode::GearCutting || m_mode == Mode::BoltHoles;
}

std::string Model::boltHolesUnavailable() const
{
    if (!m_config.readBool("BoltHoles", false)) {
        return "Bolt holes aren't turned on (BoltHoles in the config file)";
    }
    if (m_config.readBool("DisableAxis1", false)) {
        return "Bolt holes need " + axisLabel(1);
    }
    if (m_config.read("Axis1DisplayUnits", "mm") != "mm") {
        return "Bolt holes need " + axisLabel(1) + " to be a length (Axis1DisplayUnits = mm)";
    }
    if (!manualAxis(3)) {
        return "Bolt holes need a manual axis 3 (e.g. Y: Axis3Kind = manual)";
    }
    return {};
}

BoltHolePositions Model::boltHolePositions() const
{
    BoltHolePositions p;
    p.xHere = motor(1)->getPosition();
    if (const ManualAxis* y = manualAxis(3)) {
        p.yHere = y->positionMm();
    }
    for (std::size_t slot = 0; slot < MEMORY_SLOTS; ++slot) {
        p.xMemory.push_back(getMemory(1, slot));
        p.yMemory.push_back(getMemory(3, slot));
    }
    if (const ManualAxis* z = manualAxis(4)) {
        p.hasZ = true;
        p.zHere = z->positionMm();
    }
    return p;
}

BoltHoleSetup Model::boltHoleDialogSetup() const
{
    return defaultBoltHoleSetup(m_boltHoleSetup, boltHolePositions());
}

std::string Model::setBoltHoleSetup(const BoltHoleSetup& setup)
{
    if (m_mode != Mode::BoltHoles) {
        return "Bolt holes are only in bolt hole mode";
    }
    if (const std::string error = boltHolesUnavailable(); !error.empty()) {
        return error;
    }
    BoltHolePlan plan = planBoltHoles(
        setup, boltHolePositions(), axisLabel(1), axisLabel(3), manualAxis(4) ? axisLabel(4) : "Z");
    if (!plan.error.empty()) {
        return plan.error;
    }
    m_boltHoleSetup = setup;
    m_boltHolePlan = std::move(plan);
    m_boltHolesSet = true;
    m_boltHole = 0;
    m_boltHoleGoneTo = false;
    m_boltHoleDepthPending = false;
    MGOLOG(
        fmt::format(
            "Bolt holes set up: {} on PCD {}, the first at {} deg, {}, centre {} {}, {} {}",
            setup.holes,
            setup.pcd,
            setup.firstAngleDeg,
            setup.clockwise ? "clockwise" : "anticlockwise",
            axisLabel(1),
            m_boltHolePlan.centreX,
            axisLabel(3),
            m_boltHolePlan.centreY));
    if (setup.depth) {
        MGOLOG(
            fmt::format(
                "Bolt holes: depth {} {}, starting from {}",
                axisLabel(4),
                *setup.depth,
                *m_boltHolePlan.zStart));
    }
    return {};
}

const BoltHoleSetup& Model::getBoltHoleSetup() const
{
    return m_boltHoleSetup;
}

bool Model::boltHolesSet() const
{
    return m_mode == Mode::BoltHoles && m_boltHolesSet;
}

const BoltHolePlan& Model::boltHolePlan() const
{
    return m_boltHolePlan;
}

int Model::currentBoltHole() const
{
    return m_boltHole;
}

bool Model::boltHoleGoneTo() const
{
    return m_boltHoleGoneTo;
}

void Model::goToBoltHole()
{
    goToBoltHole(m_boltHole);
}

void Model::nextBoltHole()
{
    // (Before any hole has been gone to, the first is next)
    goToBoltHole(m_boltHoleGoneTo ? m_boltHole + 1 : m_boltHole);
}

void Model::previousBoltHole()
{
    // (Before any hole has been gone to, the last is previous)
    goToBoltHole(m_boltHoleGoneTo ? m_boltHole - 1 : -1);
}

void Model::goToBoltHole(int index)
{
    if (!boltHolesSet() || m_boltHolePlan.holes.empty()) {
        return;
    }
    // With a depth, not while the drill may be in the work
    if (m_boltHoleSetup.depth) {
        const std::string z = axisLabel(4);
        const auto below = boltHoleZBelowStartMm();
        if (!below) {
            m_warning = z + " has no reading: can't tell the drill is clear of the work";
            return;
        }
        if (*below > BOLT_HOLE_Z_CLEAR_MM) {
            m_warning = fmt::format(
                "Raise {} first: it's {:.{}f} below where it started ({:.{}f})",
                z,
                toDisplayUnits(4, *below),
                axisDecimals(4),
                toDisplayUnits(4, *m_boltHolePlan.zStart),
                axisDecimals(4));
            return;
        }
    }
    // X first, as it can refuse (e.g. locked): then nothing moves
    if (axisMoveRefused(1)) {
        return;
    }
    const int n = static_cast<int>(m_boltHolePlan.holes.size());
    m_boltHole = ((index % n) + n) % n;
    m_boltHoleGoneTo = true;
    const BoltHole& hole = m_boltHolePlan.holes.at(static_cast<std::size_t>(m_boltHole));
    MGOLOG(
        fmt::format(
            "Bolt hole {} of {} ({} deg): {} {:.4f}, {} {:.4f}",
            m_boltHole + 1,
            n,
            hole.angleDeg,
            axisLabel(1),
            hole.x,
            axisLabel(3),
            hole.y));
    // (A move already under way is stopped first, as for a memory slot)
    stopAxis(1);
    if (std::lround(hole.x / motor(1)->getConversionFactor()) != motorStep(1)) {
        axis1GoToPosition(hole.x);
    }
    manualAxisGoToPosition(3, hole.y);
    // (Z's go-to to the depth, from the last hole, is done with: the next
    // starts once X and Y are at this one)
    if (m_boltHoleSetup.depth) {
        m_manualGoTos.at(4 - FIRST_MANUAL_AXIS).reset();
        m_boltHoleDepthPending = true;
    }
}

// (Two of the mill scale's counts: a re-base at rest can leave X a count
// from where the move was going)
constexpr double BOLT_HOLE_X_TOLERANCE_MM = 0.02;

bool Model::boltHoleXThere() const
{
    if (!boltHolesSet() || isMotorRunning(1) || isMotionSequenceRunning()
        || isFinishingOnScale(1)) {
        return false;
    }
    const BoltHole& hole = m_boltHolePlan.holes.at(static_cast<std::size_t>(m_boltHole));
    return std::abs(motorPosition(1) - hole.x) <= BOLT_HOLE_X_TOLERANCE_MM;
}

bool Model::boltHoleYThere() const
{
    const ManualAxis* y = manualAxis(3);
    if (!boltHolesSet() || !y || manualAxisGoTo(3)) {
        return false;
    }
    const auto position = y->positionMm();
    const BoltHole& hole = m_boltHolePlan.holes.at(static_cast<std::size_t>(m_boltHole));
    return position && std::abs(*position - hole.y) <= 2.0 * y->countMm();
}

std::optional<double> Model::boltHoleZBelowStartMm() const
{
    const ManualAxis* z = manualAxis(4);
    const auto position = z ? z->positionMm() : std::nullopt;
    if (!m_boltHoleSetup.depth || !position) {
        return std::nullopt;
    }
    return mgo::boltHoleZBelowStartMm(m_boltHoleSetup, m_boltHolePlan, *position);
}

void Model::checkBoltHoleDepth()
{
    if (!m_boltHoleDepthPending || !boltHolesSet() || !m_boltHoleSetup.depth) {
        return;
    }
    if (boltHoleXThere() && boltHoleYThere()) {
        m_boltHoleDepthPending = false;
        manualAxisGoToPosition(4, *m_boltHoleSetup.depth);
    }
}

std::string Model::radiusUnavailable() const
{
    const std::string z = axisLabel(1);
    const std::string x = axisLabel(2);
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
        return axisLabel(2) + " goes the wrong way into the work for a radius on its outside";
    }
    RadiusPlan plan = planRadius(setup, work);
    if (!plan.error.empty()) {
        return plan.error;
    }
    m_radiusSetup = setup;
    m_radiusPasses = std::move(plan.passes);
    m_radiusArcs = std::move(plan.arcs);
    m_multiPassRange = plan.range;
    m_radiusMultiPassSetup = {};
    m_radiusMultiPassSetup.retractBeforeMoveToStart = true;
    m_radiusSet = true;
    MGOLOG(
        fmt::format(
            "Radius set up: {} {} on diameter {}, end Z {}, {} per pass, nose {}: {} passes, Z "
            "{} to {}, X {} to {}",
            setup.kind == RadiusKind::Shoulder
                ? fmt::format(
                      "a shoulder {} from the end, down to diameter {}, with a fillet of radius",
                      setup.shoulderLength,
                      setup.finishedDiameter)
                : std::string("radius"),
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

const std::vector<double>& Model::radiusPassArcs() const
{
    return m_radiusArcs;
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
    p.cutHere = motor(1)->getPosition();
    p.feedHere = motor(2)->getPosition();
    for (std::size_t slot = 0; slot < MEMORY_SLOTS; ++slot) {
        p.cutMemory.push_back(getMemory(1, slot));
        p.feedMemory.push_back(getMemory(2, slot));
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
    const double retractMm = retractSteps * motor(2)->getConversionFactor();
    return retractMm > 0.0 ? -1.0 : 1.0;
}

bool Model::multiPassAtStart() const
{
    if (m_mode == Mode::GearCutting) {
        return atMultiPassPosition(motor(1)->getPosition(), m_multiPassRange.cutStart)
            && gearAOnTooth();
    }
    return atMultiPassPosition(motor(1)->getPosition(), m_multiPassRange.cutStart)
        && atMultiPassPosition(motor(2)->getPosition(), m_multiPassRange.feedStart);
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
        const double oldSpeed = motor(1)->getSpeed();
        std::vector<MotionSequence::Step> steps;
        steps.push_back(
            { [this]() {
                 axisState(1).status = "to start";
                 motor(1)->setSpeed(motor(1)->getMaxRpm());
                 motor(1)->goToPosition(m_multiPassRange.cutStart);
             },
              [this]() { return !motor(1)->isRunning(); } });
        if (!gearAOnTooth()) {
            steps.push_back(
                { [this, tooth]() {
                     axisState(2).status = "to tooth";
                     // (At full speed, as when indexing)
                     motor(2)->setSpeed(motor(2)->getMaxRpm());
                     motor(2)->goToPosition(tooth);
                 },
                  [this]() { return !motor(2)->isRunning(); } });
        }
        MGOLOG(
            fmt::format(
                "Gear cutting: moving to the start, axis 1 to {}, axis 2 to {}",
                m_multiPassRange.cutStart,
                tooth));
        m_motionSequence.start(std::move(steps), [this, oldSpeed]() {
            motor(1)->setSpeed(oldSpeed);
            m_multiPassMovingToStart = false;
            saveBreadcrumbPosition(1);
            saveBreadcrumbPosition(2);
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
    const bool zToMove = !atMultiPassPosition(motor(1)->getPosition(), zTarget);
    const bool xToMove = !atMultiPassPosition(motor(2)->getPosition(), xTarget);
    const double oldZSpeed = motor(1)->getSpeed();
    const double oldXSpeed = motor(2)->getSpeed();

    const auto zStopped = [this]() { return !motor(1)->isRunning(); };
    const auto xStopped = [this]() { return !motor(2)->isRunning(); };
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
                                          axisState(1).status = status;
                                          if (m_mode == Mode::Threading) {
                                              // (Z doesn't ramp while threading,
                                              // but this is at full speed. Off
                                              // again at the end, or on a stop)
                                              motor(1)->enableRamping(true);
                                          }
                                          motor(1)->setSpeed(motor(1)->getMaxRpm());
                                          motor(1)->goToPosition(zTarget);
                                      },
                                       zStopped };
    // (At the retract speed. Once X is moving to its target, it's no longer
    // at a retracted position, so isn't counted as retracted)
    const MotionSequence::Step xStep { [this, xTarget, status]() {
                                          m_axis2Retracted = false;
                                          axisState(2).status = status;
                                          motor(2)->setSpeed(1.0); // (so it ramps)
                                          motor(2)->setSpeed(100.0);
                                          motor(2)->goToPosition(xTarget);
                                      },
                                       xStopped };

    // X moves away from the work before Z moves, and towards it after, so
    // that Z doesn't move with the tool deeper in than it need be. With a
    // retract (only needed if Z moves), X starts by moving away anyway
    std::vector<MotionSequence::Step> steps;
    const bool retractFirst = zToMove && retract && !m_axis2Retracted;
    const bool xAwayFromWork = (xTarget - motor(2)->getPosition()) * axis2IntoWorkSign() < 0.0;
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
                motor(1)->enableRamping(false);
            }
            motor(1)->setSpeed(oldZSpeed);
            motor(2)->setSpeed(oldXSpeed);
            if (onDone) {
                onDone();
            }
            saveBreadcrumbPosition(1);
            saveBreadcrumbPosition(2);
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
    if (moveRefused({ 1, 2 })) {
        return;
    }
    if (m_multiPass.state() == MultiPass::State::Paused && m_springPasses
        && (motor(1)->isRunning() || motor(2)->isRunning())) {
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
    if (motor(1)->isRunning() || motor(2)->isRunning()) {
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
        // Each pass is an arc, as a series of tapers (or, for a shoulder, a
        // straight pass). On the end, the return is along the work's
        // diameter, which is clear of the work once the pass has cut (the
        // radius is all inside it). At a shoulder, it's along the height the
        // pass ended at, which the pass (or the passes down to the fillet's
        // top) cut. So there's no retract
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
                : radius   ? fmt::format(
                                 "{} {} (nose {})",
                                 m_radiusSetup.kind == RadiusKind::Shoulder ? "Shoulder with a fillet"
                                                                            : "Radius",
                                 m_radiusSetup.radius,
                                 m_radiusSetup.noseRadius)
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
    return motor(1)->getPosition();
}

bool Model::cutMoving() const
{
    // (Including taking up the backlash first, and X catching up with Z)
    return axisState(1).wasRunning || m_motionSequence.isRunning() || m_multiPassXFollowing
        || m_multiPassXSettling || m_approaches[0].has_value();
}

void Model::cutMoveTo(double position, double feedFollows)
{
    axisState(1).status = "multi-pass";
    if (spindleSynchronised()) {
        // A threading multi-pass's return (retracted, so not locked to the
        // spindle). Z doesn't ramp while threading, but this is at full
        // speed, so it does here. (cutThreadTo() turns it off again)
        motor(1)->enableRamping(true);
    }
    // (While retracted, X doesn't follow: the correction was taken off where
    // it unretracts to when it retracted. See retract())
    if (cutApproaches(feedFollows)) {
        const double from = motor(1)->getPosition();
        startApproach(1, position, position >= from ? 1 : -1);
        axisState(1).wasRunning = motor(1)->isRunning();
        return;
    }
    if (feedFollows == 0.0 || m_axis2Retracted) {
        motor(1)->goToPosition(position);
        // (So a move that's over by the next update is still seen to have ended)
        axisState(1).wasRunning = motor(1)->isRunning();
        return;
    }
    // As for a taper (the backlash taken up first, so X moves as soon as Z
    // does). X's speed is restored once it's caught up (see checkStatus())
    m_multiPassXFollowing = true;
    axisState(2).previousSpeed = motor(2)->getSpeed();
    if (atMultiPassPosition(motor(1)->getPosition(), m_multiPass.plan().cutStart)) {
        // A cut (not a return): where X is, so it can unretract back to it
        m_multiPassCutStartXStep = motor(2)->getCurrentStep();
    }
    const long step = std::lround(position / motor(1)->getConversionFactor());
    m_motionSequence.start(xFollowsZSteps(step, feedFollows, [this, step]() {
        motor(1)->goToStep(step);
        axisState(1).wasRunning = true;
    }));
}

double Model::cutSpeed() const
{
    // (While creeping, the speed it'll go back to)
    if (m_approaches[0] && m_approaches[0]->phase == AxisApproach::Phase::Creeping) {
        return m_approaches[0]->speedBeforeCreep;
    }
    return motor(1)->getSpeed();
}

void Model::setCutSpeed(double speed)
{
    if (m_approaches[0] && m_approaches[0]->phase == AxisApproach::Phase::Creeping) {
        m_approaches[0]->speedBeforeCreep = speed;
        return;
    }
    motor(1)->setSpeed(speed);
}

double Model::cutMaxSpeed() const
{
    return motor(1)->getMaxRpm();
}

double Model::cutStepSize() const
{
    return std::abs(motor(1)->getConversionFactor());
}

double Model::feedStepSize() const
{
    return std::abs(motor(2)->getConversionFactor());
}

std::optional<std::size_t> Model::threadStartChosen() const
{
    return m_threadStartChosen;
}

std::string Model::cutThreadTo(double position, const std::vector<double>& revolutionOffsets)
{
    m_threadStartChosen.reset();
    if (!spindleSynchronised()) {
        return "Threading passes are only in threading or knurling mode";
    }
    if (const auto error = threadingPassError(); !error.message.empty()) {
        setThreadingWarning(error.message, error.sticky);
        return error.message;
    }
    axisState(1).status = m_mode == Mode::Knurling ? "knurling" : "threading";
    // (Z mustn't ramp while it follows the spindle: the return turned it on,
    // and in knurling mode, it's on but for the passes)
    motor(1)->enableRamping(false);
    // As a threading pass by hand: Z's backlash is taken up first if it
    // reverses (it does, after the return), then the pass waits for the
    // spindle to come round to the first of the starts (chosen then, as the
    // take-up takes a while)
    auto steps = threadingPassSteps(
        std::lround(position / motor(1)->getConversionFactor()), revolutionOffsets);
    steps.back().action = [this, move = std::move(steps.back().action)]() {
        move();
        // (So a pass that's over by the next update is still seen to have
        // ended, as in cutMoveTo())
        axisState(1).wasRunning = motor(1)->isRunning();
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
    return axisState(2).wasRunning || m_approaches[1].has_value();
}

void Model::feedMoveTo(double position)
{
    const double target = position + multiPassXCorrection();
    if (feedApproaches()) {
        // (The way the feed goes in, from feedStart to feedEnd: if past it,
        // it backs off and comes in again)
        const MultiPassPlan& plan = m_multiPass.plan();
        startApproach(2, target, plan.feedEnd >= plan.feedStart ? 1 : -1);
    } else {
        motor(2)->goToPosition(target);
    }
    axisState(2).wasRunning = motor(2)->isRunning();
}

bool Model::feedApproaches() const
{
    // (As feedReady() re-bases, and only the feed before a pass: not, e.g.,
    // a radius's path, whose feed moves are cuts)
    return scaleApproachMm(2) > 0.0 && scale(2) && scale(2)->isAligned()
        && m_config.readBool("Axis2ScaleRebaseBetweenPasses", false)
        && (m_mode == Mode::MultiPass || m_mode == Mode::GearCutting)
        && m_multiPass.state() == MultiPass::State::Feeding;
}

bool Model::cutApproaches(double feedFollows) const
{
    // (As cutReady() re-bases. Not with X following Z, as re-basing Z part
    // way would move X: see StepperMotor::synchronise(). Nor a radius's path,
    // whose points are short moves along the arc)
    using State = MultiPass::State;
    const State state = m_multiPass.state();
    return scaleApproachMm(1) > 0.0 && scale(1) && scale(1)->isAligned()
        && m_config.readBool("Axis1ScaleRebaseBetweenPasses", false)
        && (m_mode == Mode::MultiPass || m_mode == Mode::GearCutting)
        && (feedFollows == 0.0 || m_axis2Retracted) && m_multiPass.plan().paths.empty()
        && (state == State::Cutting || state == State::CuttingBack || state == State::Returning
            || state == State::Positioning);
}

void Model::startApproach(int axisNumber, double targetMm, int direction)
{
    StepperMotor* m = motor(axisNumber);
    const double margin = scaleApproachMm(axisNumber);
    const double toGo = (targetMm - m->getPosition()) * direction;
    const double halfStep = std::abs(m->getConversionFactor()) / 2.0;
    auto& approach = m_approaches.at(static_cast<std::size_t>(axisNumber - 1));
    approach = AxisApproach { .targetMm = targetMm, .direction = direction };
    if (toGo > margin) {
        // Short of it by the approach, then (once the scale has settled)
        // creep the rest
        approach->phase = AxisApproach::Phase::MovingShort;
        approach->settles = scale(axisNumber)->settleCount();
        m->goToPosition(targetMm - direction * margin);
    } else if (toGo > halfStep) {
        // (All of it is the approach)
        approach->phase = AxisApproach::Phase::Creeping;
        approach->speedBeforeCreep = m->getSpeed();
        m->setSpeed(creepSpeed(axisNumber));
        m->goToPosition(targetMm);
    } else if (toGo < -halfStep) {
        // Past it (e.g. fed again after a re-base): back off past the
        // backlash, then approach again the same way, rather than reverse
        // onto it, which would leave the slack the wrong way, for the cut to
        // push the tool back
        const double backOff = std::max(0.5, 2.0 * backlashCompensationMm(axisNumber));
        MGOLOG(
            fmt::format(
                "{} {:.4f} mm past {:.4f}: backing off {:.3f} mm to approach again",
                axisLabel(axisNumber),
                -toGo,
                targetMm,
                backOff));
        approach->phase = AxisApproach::Phase::BackingOff;
        approach->backedOff = true;
        m->goToPosition(targetMm - direction * (margin + backOff));
    } else {
        // (There already)
        approach.reset();
    }
}

void Model::advanceApproach(int axisNumber)
{
    auto& approach = m_approaches.at(static_cast<std::size_t>(axisNumber - 1));
    StepperMotor* m = motor(axisNumber);
    if (!approach || m->isRunning()) {
        return;
    }
    AxisApproach& a = *approach;
    switch (a.phase) {
        case AxisApproach::Phase::BackingOff:
            a.phase = AxisApproach::Phase::MovingShort;
            a.settles = scale(axisNumber)->settleCount();
            m->goToPosition(a.targetMm - a.direction * scaleApproachMm(axisNumber));
            return;
        case AxisApproach::Phase::MovingShort:
            a.phase = AxisApproach::Phase::Settling;
            return;
        case AxisApproach::Phase::Settling:
            // (Without a reading, it won't settle: creep the rest anyway)
            if (scale(axisNumber)->hasReading() && scale(axisNumber)->settleCount() == a.settles) {
                return;
            }
            rebaseIfApart(axisNumber, "re-based before creeping the rest");
            if (const double past = (m->getPosition() - a.targetMm) * a.direction;
                past > std::abs(m->getConversionFactor()) / 2.0) {
                // Past it already, as the scale says: it overshot by more than
                // the approach. Back off past the backlash and approach again
                // (once: if it's past again, the creep goes back to it), rather
                // than reverse onto it
                warnApproachTooShort(axisNumber, past);
                if (!a.backedOff) {
                    a.backedOff = true;
                    a.phase = AxisApproach::Phase::BackingOff;
                    m->goToPosition(
                        a.targetMm
                        - a.direction
                            * (scaleApproachMm(axisNumber)
                               + std::max(0.5, 2.0 * backlashCompensationMm(axisNumber))));
                    return;
                }
            }
            if (axisNumber == 1) {
                m_returnApproachRebased = m_multiPass.state() == MultiPass::State::Returning;
            }
            a.phase = AxisApproach::Phase::Creeping;
            a.speedBeforeCreep = m->getSpeed();
            MGOLOG(
                fmt::format(
                    "{} {:.4f} mm short of {:.4f}: creeping the rest",
                    axisLabel(axisNumber),
                    (a.targetMm - m->getPosition()) * a.direction,
                    a.targetMm));
            m->setSpeed(creepSpeed(axisNumber));
            m->goToPosition(a.targetMm);
            return;
        case AxisApproach::Phase::Creeping:
            endApproach(axisNumber);
            return;
    }
}

void Model::warnApproachTooShort(int axisNumber, double pastMm)
{
    // It aimed to stop the approach short of the target, so it overshot
    // where it aimed by the approach as well: that's what the approach has to
    // be more than. (On the mill, 0.01 mm past the target with a 0.2 mm
    // approach once read as if 0.01 mm would do)
    const double overshootMm = scaleApproachMm(axisNumber) + pastMm;
    m_warning = fmt::format(
        "{} went {:.3f} {} past its target ({:.3f} past where it aimed): make "
        "Axis{}ScaleApproachMm more than {:.3f} mm",
        axisLabel(axisNumber),
        toDisplayUnits(axisNumber, pastMm),
        axisUnitName(axisNumber),
        toDisplayUnits(axisNumber, overshootMm),
        axisNumber,
        overshootMm);
    MGOLOG(
        fmt::format(
            "*** Warning *** {} went {:.4f} mm past its target, {:.4f} mm past where it aimed "
            "(its approach is {:.3f} mm)",
            axisLabel(axisNumber),
            pastMm,
            overshootMm,
            scaleApproachMm(axisNumber)));
}

void Model::endApproach(int axisNumber)
{
    auto& approach = m_approaches.at(static_cast<std::size_t>(axisNumber - 1));
    if (approach && approach->phase == AxisApproach::Phase::Creeping) {
        // (The multi-pass sets the speeds itself: no reset)
        motor(axisNumber)->setSpeed(approach->speedBeforeCreep);
    }
    approach.reset();
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
    axisState(2).wasRunning = motor(2)->isRunning();
}

void Model::unretract()
{
    axis2Unretract();
    axisState(2).wasRunning = motor(2)->isRunning();
}

void Model::multiPassXFollowEnded()
{
    // After following Z back to the start, X should be where the cut
    // started. It can lag Z a little as it starts following (more so on the
    // cut than the return), so may be a step or so short: if so, it finishes
    // the move, so that nothing builds up from pass to pass
    if (m_axis2Retracted || !m_multiPass.inProgress()
        || !atMultiPassPosition(motor(1)->getPosition(), m_multiPass.plan().cutStart)
        || motor(2)->getCurrentStep() == m_multiPassCutStartXStep) {
        return;
    }
    MGOLOG(
        fmt::format(
            "Multi-pass: X ended the return at step {}, not {}: moving there",
            motor(2)->getCurrentStep(),
            m_multiPassCutStartXStep));
    motor(2)->goToStep(m_multiPassCutStartXStep);
    m_multiPassXSettling = true;
    axisState(2).wasRunning = true;
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
        * (motor(1)->getPosition() - m_multiPass.plan().cutStart);
}

bool Model::isRetracted() const
{
    return m_axis2Retracted;
}

double Model::indexPosition() const
{
    return motor(2)->getPosition();
}

bool Model::indexMoving() const
{
    // (Including backing off first)
    return axisState(2).wasRunning || m_motionSequence.isRunning();
}

void Model::indexMoveTo(double position)
{
    axisState(2).status = "indexing";
    // Forwards is towards larger positions, which may be fewer steps
    const double conversion = motor(2)->getConversionFactor();
    const long forwards = conversion > 0.0 ? 1 : -1;
    const long target = std::lround(position / conversion);
    const long current = motor(2)->getCurrentStep();
    MGOLOG(fmt::format("Indexing to {} (step {})", position, target));
    // (At full speed, as for a return: the cutter is clear of the blank. A
    // set speed would do, but isn't kept: a fast move resets it, between
    // runs)
    motor(2)->setSpeed(motor(2)->getMaxRpm());
    if ((target - current) * forwards > 0) {
        motor(2)->goToStep(target);
        // (So a move that's over by the next update is still seen to have ended)
        axisState(2).wasRunning = motor(2)->isRunning();
        return;
    }
    // Already there (the first tooth), or (never, in gear cutting) behind:
    // back off and come forwards to it, so the table's backlash is taken up
    // the same way as for every other tooth, however it last moved (which
    // includes any backlash compensation). A degree is plenty: it only has
    // to be more than a step, as the motor takes up the backlash it's set to
    const long backOff = std::max(1L, std::lround(1.0 / std::abs(conversion)));
    const auto stopped = [this]() { return !motor(2)->isRunning(); };
    m_motionSequence.start(
        { { [this, target, backOff, forwards]() {
               motor(2)->goToStep(target - forwards * backOff);
           },
            stopped },
          { [this, target]() { motor(2)->goToStep(target); }, stopped } });
}

double Model::indexStepSize() const
{
    return std::abs(motor(2)->getConversionFactor());
}

void Model::gearCuttingFinished()
{
    // (Whole turns only: it ends a whole number of turns from the start, but
    // within a step of it)
    const double turns
        = std::round((motor(2)->getPosition() - m_multiPass.plan().indexStart) / 360.0);
    if (turns == 0.0) {
        return;
    }
    const long shift = std::lround(turns * 360.0 / motor(2)->getConversionFactor());
    const long step = motor(2)->getCurrentStep() - shift;
    // (The motor's backlash is unchanged: it's still taken up forwards.) The
    // memory slots are left as they are: whole turns don't change where on
    // the table a slot is, and a slot stored before the passes is then
    // that position again, not whole turns behind
    MGOLOG(
        fmt::format(
            "{} renumbered by {} whole turns, to {:.4f}",
            axisLabel(2),
            -turns,
            motor(2)->getPosition(step)));
    motor(2)->setPosition(motor(2)->getPosition(step));
    axisState(2).motorLog.step = motor(2)->getCurrentStep();
    if (scale(2)) {
        scale(2)->realign();
    }
    // (Going back to them would take whole turns)
    clearBreadcrumbs(2);
    MGOLOG(
        fmt::format(
            "Gear cutting finished: {} {} turn(s), so its position is back to {}",
            axisLabel(2),
            turns,
            motor(2)->getPosition()));
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
        || !atMultiPassPosition(motor(1)->getPosition(), m_springPassZ)) {
        return false;
    }
    // X at the last pass's depth, or deeper (nudged in on purpose). Not
    // shallower: e.g. moved out to try the nut on, so it goes back first.
    // (Within half a step, as it can only stop on a whole one)
    const double deeper = (getAxis2Position() - *m_springPassX) * axis2IntoWorkSign();
    return deeper > -(MultiPass::TOLERANCE + std::abs(motor(2)->getConversionFactor()) / 2.0);
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
        m_springPassZ = motor(1)->getPosition();
    }
    // If it cut from the work's end, the feature's diameter is now the one it
    // finished at. (Not if it cut part way along, which would leave the
    // diameter at the end as it was: a start from the feature assumes the
    // diameter runs from the end)
    // (Not for a thread: its depth isn't a surface's diameter)
    // (Nor for paths, e.g. a radius, which doesn't change the diameter,
    // unless it turned a shoulder: from the end, to the finished diameter)
    const MultiPassPlan& plan = m_multiPass.plan();
    if (m_mode == Mode::Radius && m_radiusSet && m_radiusSetup.kind == RadiusKind::Shoulder
        && m_featureX && !plan.paths.empty()) {
        m_featureX = (*m_featureX < 0.0 ? -0.5 : 0.5) * m_radiusSetup.finishedDiameter;
        MGOLOG(fmt::format("Feature diameter now {}", *getFeatureDiameterMm()));
        return;
    }
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
