#include "model.h"
#include "keycodes.h"
#include "threadpitches.h" // for ThreadPitch, threadPitches

#include <cassert>
#include <sstream>

#include <fmt/format.h>

namespace {

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

} // anonymous namepace

namespace mgo {

void Model::initialise()
{
    double axis1ConversionFactor = m_config.readDouble("Axis1ConversionNumerator", -1.0)
        / m_config.readDouble("Axis1ConversionDivisor", 1'000.0);
    double maxZSpeed = m_config.readDouble("Axis1MaxMotorSpeed", 1'000.0);
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
        std::abs(maxZSpeed / axis1ConversionFactor / axis1StepsPerRevolution),
        m_config.readDouble("Axis1RampingSpeed", 100.0),
        usingMockLinearScale,
        m_config.readLong("LinearScaleAxis1StepsPerMM", 200));

    double axis2ConversionFactor = m_config.readDouble("Axis2ConversionNumerator", -1.0)
        / m_config.readDouble("Axis2ConversionDivisor", 1'000.0);
    double maxXSpeed = m_config.readDouble("Axis2MaxMotorSpeed", 1'000.0);
    long axis2StepsPerRevolution = m_config.readLong("Axis2StepsPerRev", 800);
    m_axis2Motor = std::make_unique<mgo::StepperMotor>(
        m_gpio,
        m_config.readLong("Axis2GpioStepPin", 20),
        m_config.readLong("Axis2GpioReversePin", 21),
        m_config.readLong("Axis2GpioEnablePin", 0),
        axis2StepsPerRevolution,
        axis2ConversionFactor,
        std::abs(maxXSpeed / axis2ConversionFactor / axis2StepsPerRevolution),
        m_config.readDouble("Axis2RampingSpeed", 100.0));

    m_rotaryEncoder = std::make_unique<mgo::RotaryEncoder>(
        m_gpio,
        m_config.readLong("RotaryEncoderGpioPinA", 23),
        m_config.readLong("RotaryEncoderGpioPinB", 24),
        m_config.readLong("RotaryEncoderPulsesPerRev", 2'000),
        m_config.readDouble("RotaryEncoderGearingNumerator", 35.0)
            / m_config.readDouble("RotaryEncoderGearingDivisor", 30.0));

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
    float chuckRpm = m_rotaryEncoder->getRpm();

    if (limitSwitchTriggered()) {
        stopAllMotors();
    }

#ifndef FAKE
    if (m_spindleWasRunning && chuckRpm < 30.f) {
        // If the chuck has stopped, we stop X/Z motors as a safety
        // feature, just in case the motor has stalled or the operator
        // has turned it off because of some issue.
        // This won't stop the motors being started again even
        // if the chuck isn't moving.
        stopAllMotors();
    }
#endif
    m_spindleWasRunning = chuckRpm > 30.f;

    if (m_enabledFunction == Mode::Threading) {
        // We are cutting threads, so the stepper motor's speed
        // is dependent on the spindle's RPM and the thread pitch.
        float pitch = threadPitches.at(m_threadPitchIndex).pitchMm;
        // because my stepper motor / leadscrew does one mm per
        // revolution, there is a direct correlation between spindle
        // rpm and stepper motor rpm for a 1mm thread pitch.
        float speed = pitch * m_rotaryEncoder->getRpm();
#ifndef FAKE
        double maxZSpeed = m_config.readDouble("Axis1MaxMotorSpeed", 700.0);
        if (speed > maxZSpeed * 0.8) {
            m_axis1Motor->stop();
            m_axis1Motor->wait();
            m_warning = "RPM too high for threading";
        } else
#endif
        {
            m_warning = "";
        }
        m_axis1Motor->setSpeed(speed);
    }
    if (m_xDiameterSet) {
        m_generalStatus
            = fmt::format("Diameter: {: .2f} mm", std::abs(m_axis2Motor->getPosition() * 2));
    }
    if (m_enabledFunction == Mode::MultiPass && m_axis1Motor->isRunning()
        && m_multiPassStage == MultiPassStage::NotStarted) {
        // The user has started motion of the tool
        m_multiPassStage = MultiPassStage::Cutting;
    }
    if (m_enabledFunction == Mode::MultiPass && !m_axis1Motor->isRunning()
        && !m_axis2Motor->isRunning()) {
        switch (m_multiPassStage) {
            case MultiPassStage::Cutting:
                // We have come to the end of a cut
                m_currentMemory = 0;
                // Fast return unless it's the last pass
                if (!((m_axis2Memory[0] > m_axis2Memory[1]
                       && m_axis2Motor->getCurrentStep() <= m_axis2Memory[1])
                      || (m_axis2Memory[0] < m_axis2Memory[1]
                          && m_axis2Motor->getCurrentStep() >= m_axis2Memory[1]))) {
                    axis1FastReturn();
                }
                m_multiPassStage = MultiPassStage::StepOver;
                break;
            case MultiPassStage::StepOver:
                {
                    double stepOver = m_stepOver;
                    // Note! steps are reversed, higher = nearer axis. Position (in mm) is reported
                    // the other way around because Axis2ConversionNumerator is negative
                    if (m_axis2Memory[1] > m_axis2Motor->getCurrentStep()) {
                        stepOver = -stepOver;
                    }
                    // Check we don't go over the target step
                    if (m_axis2Motor->getCurrentStep()
                            + stepOver / m_axis2Motor->getConversionFactor()
                        > m_axis2Memory[1]) {
                        stepOver = (m_axis2Memory[1] - m_axis2Motor->getCurrentStep())
                            * m_axis2Motor->getConversionFactor();
                    }
                    if ((m_axis2Memory[0] > m_axis2Memory[1]
                         && m_axis2Motor->getCurrentStep() <= m_axis2Memory[1])
                        || (m_axis2Memory[0] < m_axis2Memory[1]
                            && m_axis2Motor->getCurrentStep() >= m_axis2Memory[1])) {
                        m_multiPassStage = MultiPassStage::Finished;
                    } else {
                        m_multiPassStage = MultiPassStage::NextCut;
                        double targetPos = m_axis2Motor->getPosition() + stepOver;
                        m_axis2Motor->goToPosition(targetPos);
                    }
                }
                break;
            case MultiPassStage::NextCut:
                m_axis1Motor->goToStep(m_axis1Memory[1]);
                m_axis1Status = "next pass";
                m_multiPassStage = MultiPassStage::Cutting;
                break;
            case MultiPassStage::Finished:
                m_multiPassStage = MultiPassStage::NotStarted;
                m_enabledFunction = Mode::None;
                break;
            case MultiPassStage::NotStarted:
                // Do nothing until the user initiates the first cut
                break;
            default:
                assert(false); // Unhandled MultiPassStage item
        }
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
            if (m_enabledFunction == Mode::Threading
                && m_config.readBool("ThreadingAutoRetract", false)) {
                axis2Retract();
                // We may want to automate the return to the start position as well
            }
        }
        if (m_axis1FastReturning || m_axis1RapidInProgress) {
            m_axis1Motor->setSpeed(m_previousAxis1Speed);
            m_axis1FastReturning = false;
            m_axis1RapidInProgress = false;
        }
        if ((m_enabledFunction == Mode::Taper || m_enabledFunction == Mode::Radius)
            && m_axis1WasRunning) {
            m_axis2Motor->setSpeed(m_previousAxis2Speed);
            axis2SynchroniseOff();
        }
        if (m_axis1WasRunning
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
        if (!(m_enabledFunction == Mode::Taper) && m_axis2WasRunning
            && m_axis2Motor->getSpeed() >= m_config.readDouble("Axis2SpeedResetAbove", 80.0)) {
            // We don't allow faster speeds to "stick" to avoid accidental
            // fast motion after a long fast movement
            m_axis2Motor->setSpeed(m_config.readDouble("Axis2SpeedResetTo", 20.0));
        }
        m_axis2WasRunning = false;
    } else {
        m_axis2WasRunning = true;
    }
    if (m_enabledFunction == Mode::Taper || m_enabledFunction == Mode::Radius) {
        m_axis2Status = "synchronised";
    }
}

void Model::changeMode(Mode mode)
{
    stopAllMotors();
    axis2SynchroniseOff();
    if (mode == Mode::Threading || mode == Mode::Taper || mode == Mode::Radius) {
        // We do not want motor speed ramping on tapering or threading
        m_axis1Motor->enableRamping(false);
        m_axis2Motor->enableRamping(false);
    } else {
        m_axis1Motor->enableRamping(true);
        m_axis2Motor->enableRamping(true);
    }
    if (m_enabledFunction == Mode::Taper && mode != Mode::Taper) {
        m_axis2Motor->setSpeed(m_taperPreviousXSpeed);
    }
    if (mode == Mode::MultiPass) {
        if (m_axis1Memory[0] == AXIS1_UNSET || m_axis1Memory[1] == AXIS1_UNSET) {
            // Mode is invalid, we need the first two axis1 memories to specify
            // the from->to positions on the primary axis.
            return;
        }
    }
    m_warning = "";
    m_currentDisplayMode = mode;
    m_enabledFunction = mode;
    m_input = "";

    if (mode == Mode::Taper) {
        m_taperPreviousXSpeed = m_axis2Motor->getSpeed();
        if (m_taperAngle != 0.0) {
            m_input = convertToString(m_taperAngle, 4);
        }
    }

    if (mode == Mode::Radius) {
        axis1SetSpeed(10.0);
        if (m_radius != 0.0) {
            m_input = convertToString(m_radius, 4);
        }
    }
}

void Model::stopAllMotors()
{
    m_axis1Motor->stop();
    m_axis2Motor->stop();
    m_axis1Motor->wait();
    m_axis2Motor->wait();
    m_axis1Status = "stopped";
    m_axis2Status = "stopped";
#ifdef FAKE
    m_gpio.scaleStop();
#endif
}

void Model::takeUpZBacklash(ZDirection direction)
{
    axis1Stop();
    if (direction == ZDirection::Right) {
        m_axis1Motor->goToStep(getAxis1MotorCurrentStep() - 1);
    } else if (direction == ZDirection::Left) {
        m_axis1Motor->goToStep(getAxis1MotorCurrentStep() + 1);
    }
    m_axis1Motor->wait();
}

void Model::startSynchronisedXMotorForTaper(ZDirection direction)
{
    // Make sure X isn't already running first
    axis2SynchroniseOff();
    m_axis2Motor->stop();
    m_axis2Motor->wait();

    int stepAdd = -1;
    if ((direction == ZDirection::Left && m_taperAngle < 0.0)
        || (direction == ZDirection::Right && m_taperAngle > 0.0)) {
        stepAdd = 1;
    }
    // As this is called just before the Z motor starts moving, we take
    // up any backlash first.
    m_axis2Motor->setSpeed(100.0);
    m_axis2Motor->goToStep(m_axis2Motor->getCurrentStep() + stepAdd);
    m_axis2Motor->wait();
    double angleConversion = std::tan(m_taperAngle * DEG_TO_RAD);
    m_axis2Motor->synchroniseOn(
        m_axis1Motor.get(), [angleConversion](double zPosDelta, double) -> double {
            return zPosDelta * angleConversion;
        });
}

void Model::startSynchronisedXMotorForRadius(ZDirection direction)
{
    // To cut a radius, we need a defined start point for both
    // axes. We do this by assuming zero is the start point for
    // both (the change mode screen tells the user to zero out,
    // such that the tool is on the outermost apex of the radius).
    // We can then determine at any time whene axis2 should be
    // in relation to axis1.

    axis2SynchroniseOff();
    m_axis2Motor->stop();
    m_axis2Motor->wait();

    int stepAdd = 1;
    if (direction == ZDirection::Left) {
        stepAdd = -1;
    }
    // As this is called just before the Z motor starts moving, we take
    // up any backlash first.
    m_axis2Motor->setSpeed(100.0);
    m_axis2Motor->goToStep(m_axis2Motor->getCurrentStep() + stepAdd);
    m_axis2Motor->wait();
    double radius = m_radius;
    m_axis2Motor->synchroniseOn(
        m_axis1Motor.get(),
        [radius](double /*zPosDelta*/, double zCurrentPos) {
            // Note, we only cut a radius if z is positive.
            //
            // To solve for X, given Z, we can use Pythagoras
            // as we have a right angle with known hypoteneuse
            // (i.e. the radius): z^2 + x^2 = r^2, so
            // sqrt( r^2 - z^2 ) = our x position

            if (zCurrentPos <= 0) {
                return 0.0;
            }
            if (zCurrentPos > radius) {
                return radius;
            }
            double xPos = std::sqrt(radius * radius - zCurrentPos * zCurrentPos);
            // We need to return a delta
            return radius - xPos;
        },
        true // always use zero as sync start pos
    );
}

void Model::axis1GoToStep(long step)
{
    axis1CheckForSynchronisation(step);
    if (m_enabledFunction == Mode::Threading) {
        m_rotaryEncoder->callbackAtZeroDegrees([&]() {
            m_axis1Motor->goToStep(step);
        });
    } else {
        m_axis1Motor->goToStep(step);
    }
}

void Model::axis1GoToPosition(double pos)
{
    // TODO - if the linear scale is enabled, we need to perhaps go to a step close to
    // the desired position, then start nudging while monitoring the linear scale
    // until we get to the desired position. This could be implemented in the motor
    // perhaps by supplying a callback that is called after each step is taken.
    axis1CheckForSynchronisation(pos / m_axis1Motor->getConversionFactor());
    if (m_enabledFunction == Mode::Threading) {
        m_rotaryEncoder->callbackAtZeroDegrees([&]() {
            m_axis1Motor->goToPosition(pos);
        });
    } else {
        m_axis1Motor->goToPosition(pos);
    }
    if (std::abs(pos) < 0.00001) {
        // Round to zero for display purposes, just to
        // avoid having "-0.000" displayed
        pos = 0.0;
    }
    m_axis1Status = fmt::format("Going to {:.3f}", pos);
}

void Model::axis1GoToOffset(double offset)
{
    m_axis1LastRelativeMove = offset;
    m_lastRelativeMoveAxis = Axis::Axis1;
    axis1GoToPosition(m_axis1Motor->getPosition() + offset);
    m_axis1Status = fmt::format("To offset {}", offset);
}

void Model::axis1GoToPreviousPosition()
{
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

void Model::axis1CheckForSynchronisation(ZDirection direction)
{
    if (m_enabledFunction == Mode::Taper) {
        takeUpZBacklash(direction);
        startSynchronisedXMotorForTaper(direction);
    } else if (m_enabledFunction == Mode::Radius) {
        takeUpZBacklash(direction);
        startSynchronisedXMotorForRadius(direction);
    }
}

void Model::axis1CheckForSynchronisation(long step)
{
    if (m_enabledFunction != Mode::Taper && m_enabledFunction != Mode::Radius) {
        return;
    }
    ZDirection direction;
    if (step < getAxis1MotorCurrentStep()) {
        direction = ZDirection::Right;
    } else {
        direction = ZDirection::Left;
    }
    axis1CheckForSynchronisation(direction);
}

void Model::axis1GoToCurrentMemory()
{
    if (m_axis1Memory.at(m_currentMemory) == AXIS1_UNSET) {
        return;
    }
    if (m_axis1Memory.at(m_currentMemory) == getAxis1MotorCurrentStep()) {
        return;
    }
    axis1Stop();
    m_axis1Status = "returning";
    axis1CheckForSynchronisation(m_axis1Memory.at(m_currentMemory));
    // If threading, we need to start at the same point each time - we
    // wait for zero degrees on the chuck before starting:
    if (m_enabledFunction == Mode::Threading) {
        m_rotaryEncoder->callbackAtZeroDegrees([&]() {
            axis1GoToStep(m_axis1Memory.at(m_currentMemory));
        });
    } else {
        axis1GoToStep(m_axis1Memory.at(m_currentMemory));
    }
}

void Model::axis1Nudge(ZDirection direction, double nudgeAmountMm)
{
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
    m_axis1Motor->wait();
    axis1GoToStep(getAxis1MotorCurrentStep() + steps);
    m_axis1Motor->wait();
    m_axis1Motor->setSpeed(oldSpeed);

    // Save position
    axis1SaveBreadcrumbPosition();
}

void Model::axis1Zero()
{
    if (m_enabledFunction == Mode::Taper) {
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
}

void Model::axis1SpeedDecrease()
{
    if (m_enabledFunction == Mode::Threading) {
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
    if (m_enabledFunction == Mode::Threading) {
        return;
    }
    if (m_axis1Motor->getSpeed() < 20.0) {
        m_axis1Motor->setSpeed(m_axis1Motor->getSpeed() + 1.0);
    } else {
        if (m_axis1Motor->getSpeed() <= m_config.readDouble("Axis1MaxMotorSpeed", 1'000.0) - 20) {
            m_axis1Motor->setSpeed(m_axis1Motor->getSpeed() + 20.0);
        }
    }
}

void Model::axis1FastReturn()
{
    if (m_axis1Memory.at(m_currentMemory) == AXIS1_UNSET || m_axis1FastReturning) {
        return;
    }
    m_previousAxis1Speed = m_axis1Motor->getSpeed();
    m_axis1FastReturning = true;
    axis1Stop();
    if (m_enabledFunction == Mode::Taper) {
        // If we are tapering, we need to set a speed the x-axis motor can keep up with
        m_axis1Motor->setSpeed(100.0);
        m_previousAxis2Speed = m_axis2Motor->getSpeed();
        ZDirection direction = ZDirection::Left;
        if (m_axis1Memory.at(m_currentMemory) < getAxis1MotorCurrentStep()) {
            direction = ZDirection::Right;
        }
        takeUpZBacklash(direction);
        startSynchronisedXMotorForTaper(direction);
    } else {
        m_axis1Motor->setSpeed(m_axis1Motor->getMaxRpm());
    }
    m_axis1Status = "fast returning";
    axis1GoToStep(m_axis1Memory.at(m_currentMemory));
}

void Model::axis2GoToPosition(double pos)
{
    m_axis2Motor->goToPosition(pos);
    m_axis2Status = fmt::format("Going to {:.3f}", pos);
}

void Model::axis2GoToOffset(double offset)
{
    m_axis2LastRelativeMove = offset;
    m_lastRelativeMoveAxis = Axis::Axis2;
    axis2GoToPosition(m_axis2Motor->getPosition() + offset);
    m_axis2Status = fmt::format("To offset {}", offset);
}

void Model::axis2GoToPreviousPosition()
{
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
    if (m_enabledFunction != Mode::None) {
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
    if (m_currentDisplayMode == Mode::Threading) {
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
    if (m_enabledFunction == Mode::Taper) {
        changeMode(Mode::None);
    }
    m_axis2Motor->zeroPosition();
    // Zeroing will invalidate any memorised X positions, so we clear them
    for (auto& m : m_axis2Memory) {
        m = AXIS2_UNSET;
    }
    // Also clear any "breadcrumb" positions
    axis2ClearBreadcrumbs();
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
    } else if (m_axis2Motor->getSpeed() < m_config.readDouble("Axis2MaxMotorSpeed", 1'000.0)) {
        m_axis2Motor->setSpeed(m_axis2Motor->getSpeed() + 10.0);
    }
}

void Model::axis2Nudge(XDirection direction, double nudgeAmountMm)
{
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
    m_axis2Motor->goToStep(m_axis2Motor->getCurrentStep() + steps);
    m_axis2Motor->wait();
    m_axis2Motor->setSpeed(oldSpeed);

    // Save position
    axis2SaveBreadcrumbPosition();
}

void Model::axis2FastReturn()
{
    if (m_axis2Memory.at(m_currentMemory) == AXIS2_UNSET || m_axis2FastReturning) {
        return;
    }
    m_previousAxis2Speed = m_axis2Motor->getSpeed();
    m_axis2FastReturning = true;
    axis2Stop();
    m_axis2Motor->setSpeed(m_axis2Motor->getMaxRpm());
    m_axis2Status = "fast returning";
    m_axis2Motor->goToStep(m_axis2Memory.at(m_currentMemory));
}

void Model::axis2Retract()
{
    if (m_config.readBool("DisableAxis2", false) || m_axis2Motor->isRunning()
        || m_enabledFunction == Mode::Taper) {
        return;
    }
    if (m_axis2Retracted) {
        // Unretract
        m_axis2Motor->setSpeed(100.0);
        m_axis2Motor->goToStep(m_xOldPosition);
        m_axis2Status = "Unretracting";
        m_fastRetracting = true;
    } else {
        m_xOldPosition = m_axis2Motor->getCurrentStep();
        m_previousAxis2Speed = m_axis2Motor->getSpeed();
        m_axis2Motor->setSpeed(100.0);
        int direction = -1;
        if (m_xRetractionDirection == XDirection::Inwards) {
            direction = 1;
        }
        long stepsForRetraction = 2.0 / std::abs(m_axis2Motor->getConversionFactor());
        if (m_config.readBool("Axis2MotorFlipDirection", false)) {
            stepsForRetraction = -stepsForRetraction;
        }
        m_axis2Motor->goToStep(m_axis2Motor->getCurrentStep() + stepsForRetraction * direction);
        m_axis2Retracted = true;
        m_axis2Status = "Retracting";
    }
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
    if (m_enabledFunction != Mode::None) {
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
    if (m_currentDisplayMode == Mode::Threading) {
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

Mode Model::getEnabledFunction() const
{
    return m_enabledFunction;
}

void Model::setEnabledFunction(Mode mode)
{
    m_enabledFunction = mode;
}

Mode Model::getCurrentDisplayMode() const
{
    return m_currentDisplayMode;
}

void Model::setCurrentDisplayMode(Mode mode)
{
    m_currentDisplayMode = mode;
}

double Model::getRadius() const
{
    return m_radius;
}

void Model::setRadius(double radius)
{
    m_radius = radius;
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

void Model::selectPreviousThreadPitch()
{
    if (m_threadPitchIndex == 0) {
        m_threadPitchIndex = threadPitches.size() - 1;
    } else {
        --m_threadPitchIndex;
    }
}

void Model::selectNextThreadPitch()
{
    if (m_threadPitchIndex == threadPitches.size() - 1) {
        m_threadPitchIndex = 0;
    } else {
        ++m_threadPitchIndex;
    }
}

std::size_t Model::getCurrentThreadPitchIndex() const
{
    return m_threadPitchIndex;
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

long Model::getAxis2Memory(std::size_t index) const
{
    return m_axis2Memory.at(index);
}

unsigned Model::getMemorySize() const
{
    return m_axis1Memory.size();
}

void Model::clearAllAxis2Memories()
{
    for (auto& m : m_axis2Memory) {
        m = INF_OUT;
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
}

void Model::setAxis2Position(double mm)
{
    m_axis2Motor->setPosition(mm);
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

std::string Model::convertAxis1StepToPosition(long step) const
{
    if (!m_axis1Motor) {
        return std::string();
    }
    double mm = m_axis1Motor->getPosition(step);
    if (std::abs(mm) < 0.001) {
        mm = 0.0;
    }
    return fmt::format("{: .2f}", mm);
}

std::string Model::convertAxis2StepToPosition(long step) const
{
    if (!m_axis2Motor) {
        return std::string();
    }
    double mm = m_axis2Motor->getPosition(step);
    if (std::abs(mm) < 0.001) {
        mm = 0.0;
    }
    return fmt::format("{: .2f}", mm);
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

void Model::clearCurrentMemorySlot(Axis axis) {
    if (axis == Axis::Axis1) {
        m_axis1Memory.at(m_currentMemory) = AXIS1_UNSET;
    }
    if (axis == Axis::Axis2) {
        m_axis2Memory.at(m_currentMemory) = AXIS2_UNSET;
    }
}

void Model::acceptInputValue()
{
    double inputValue = 0.0;
    bool valid = true;
    try {
        inputValue = std::stod(m_input);
    } catch (...) {
        valid = false;
    }

    switch (m_currentDisplayMode) {
        case Mode::Axis2PositionSetup:
            {
                m_axis2Motor->setPosition(inputValue);
                // This will invalidate any memorised X positions, so we clear them
                for (auto& m : m_axis2Memory) {
                    m = INF_OUT;
                }
                break;
            }
        case Mode::Axis1PositionSetup:
            {
                m_axis1Motor->setPosition(inputValue);
                // This will invalidate any memorised Z positions, so we clear them
                for (auto& m : m_axis1Memory) {
                    m = AXIS1_UNSET;
                }
                break;
            }
        case Mode::Axis1GoTo:
            {
                if (valid) {
                    axis1GoToPosition(inputValue);
                }
                break;
            }
        case Mode::Axis2GoTo:
            {
                if (valid) {
                    axis2GoToPosition(inputValue);
                }
                break;
            }
        case Mode::Axis1GoToOffset:
            {
                if (valid) {
                    axis1GoToOffset(inputValue);
                }
                break;
            }
        case Mode::Axis2GoToOffset:
            {
                if (valid) {
                    axis2GoToOffset(inputValue);
                }
                break;
            }
        case Mode::Taper:
            {
                m_taperAngle = inputValue;
                break;
            }
        case Mode::Radius:
            {
                m_radius = inputValue;
                break;
            }
        case Mode::MultiPass:
            {
                m_stepOver = inputValue;
                break;
            }
        case Mode::Axis2RetractSetup:
            // no processing required for these modes
            break;
        case Mode::Threading:
            {
                // m_threadPitchIndex will have been set
                break;
            }
        default:
            // unhandled mode
            assert(false);
    }
    m_currentDisplayMode = Mode::None;
}

bool Model::limitSwitchTriggered() const
{
    // TODO monitor any limit switches - needs config option for pin
    return false;
}

void Model::lockAxis(unsigned axisNumber)
{
    m_axisLocks.insert(axisNumber);
}

void Model::unlockAxis(unsigned axisNumber)
{
    auto it = m_axisLocks.find(axisNumber);
    if (it != m_axisLocks.end()) {
        m_axisLocks.erase(it);
    }
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

}