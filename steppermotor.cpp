#include "steppermotor.h"

#include "igpio.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <system_error>

#ifdef __linux__
#include <sched.h> // for cpu_set_t
#endif

namespace mgo {

namespace {
// Stepper drivers need the direction pin to be stable for a short time before
// the step pulse's rising edge (e.g. 5µs for a DM542). Without this, the first
// step after a reversal can be taken in the old direction.
constexpr uint32_t DIRECTION_SETUP_MICROSECONDS = 5;

// An idle motor thread is woken as soon as there's something to do; this
// timeout is only a safety net, so it can be fairly long
constexpr auto IDLE_WAKE_TIMEOUT = std::chrono::milliseconds(10);

// Longest stop() will wait for the motor thread to act on a stop. The thread
// checks at the start of each step period, which at the very slowest speeds
// can be tens of milliseconds
constexpr auto STOP_ACKNOWLEDGE_TIMEOUT = std::chrono::milliseconds(100);

// How long a driver's alarm output must be continuously active before we act
// on it (to ignore electrical noise)
constexpr uint32_t ALARM_FILTER_MICROSECONDS = 2'000;

// Likewise for a limit switch (also long enough for most switches' contact
// bounce to settle)
constexpr uint32_t LIMIT_FILTER_MICROSECONDS = 2'000;
}

PriorityInheritanceMutex::PriorityInheritanceMutex()
{
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT);
    int rc = pthread_mutex_init(&m_mutex, &attr);
    pthread_mutexattr_destroy(&attr);
    if (rc != 0) {
        // Priority inheritance not supported; use an ordinary mutex
        rc = pthread_mutex_init(&m_mutex, nullptr);
    }
    if (rc != 0) {
        throw std::system_error(rc, std::generic_category(), "pthread_mutex_init");
    }
}

PriorityInheritanceMutex::~PriorityInheritanceMutex()
{
    pthread_mutex_destroy(&m_mutex);
}

void PriorityInheritanceMutex::lock()
{
    const int rc = pthread_mutex_lock(&m_mutex);
    if (rc != 0) {
        throw std::system_error(rc, std::generic_category(), "pthread_mutex_lock");
    }
}

void PriorityInheritanceMutex::unlock()
{
    pthread_mutex_unlock(&m_mutex);
}

bool PriorityInheritanceMutex::try_lock()
{
    return pthread_mutex_trylock(&m_mutex) == 0;
}

StepperMotor::StepperMotor(
    IGpio& gpio,
    int stepPin,
    int reversePin,
    int enablePin,
    long stepsPerRevolution,
    double conversionFactor,
    double maxRpm,
    double rampingSpeed,
    bool usingMockLinearScale, /* = true */
    uint32_t mockLinearScaleStepsPerMm /* = 200 */)
    : m_gpio(gpio)
    , m_stepsPerRevolution(stepsPerRevolution)
    , m_conversionFactor(conversionFactor)
    , m_maxRpm(maxRpm)
    , m_rampingSpeed(rampingSpeed / 1000.0)
    , m_usingMockLinearScale(usingMockLinearScale)
    , m_mockLinearScaleStepsPerMm(mockLinearScaleStepsPerMm)

{
    if (m_rampingSpeed < 0.0) {
        m_rampingSpeed = 0.0;
    }
    if (m_rampingSpeed > 0.1) {
        m_rampingSpeed = 0.1;
    }
    if (m_rampingSpeed == 0.0) {
        m_useRamping = false;
    }
    m_motorNumber = m_gpio.addMotor(stepPin, reversePin, enablePin);
    // Ensure we start off with the right direction
    m_gpio.setReversePin(
        m_motorNumber, m_direction == Direction::forward ? PinState::low : PinState::high);

    // Start the thread
    std::thread t([&]() {
        Direction oldDirection = Direction::forward;
        for (;;) {
            int delay;
            uint32_t startTick = m_gpio.getTick();
            { // scope for lock
                // When in this scope we can assume all member
                // variables can be written and read from freely
                // without them being changed externally. Other
                // member functions must lock the mutex m_mtx
                // before changing any member variables.
                std::unique_lock lock(m_mtx);
                if (m_terminateThread) {
                    break;
                }
                checkAlarm();
                checkLimit();
                const bool blocked = isBlocked();
                if (blocked) {
                    // The driver is in alarm (so is ignoring step pulses), or
                    // a limit switch has tripped: stop, and don't move again
                    // until the fault or trip is cleared
                    m_targetStep = long(m_currentReportedStep);
                    m_busy = false;
                }
                if (blocked || m_stop) {
                    m_following = false;
                }
                if (m_stop || m_rpm == 0.0) {
                    m_targetStep = long(m_currentReportedStep);
                    m_busy = false;
                    m_following = false;
                    if (m_stop) {
                        m_stop = false;
                        m_stopAcknowledged.notify_all();
                    }
                } else if (m_following) {
                    follow();
                } else if (!m_busy && m_synchronise && m_synchroniseMotor && !blocked) {
                    synchronise();
                }
                if (!m_spinWhenIdle && !m_following && m_targetStep == m_currentReportedStep
                    && (blocked || !(m_synchronise && m_synchroniseMotor))) {
                    // Idle. Rather than spinning (which would use a whole CPU
                    // core at real-time priority), sleep until woken by
                    // goToStep(), stop() etc. This check is at the *start* of a
                    // step period, so the last step of a move still gets its
                    // full period. (A synchronised or following motor keeps
                    // spinning, as it must poll the other motor's position, or
                    // its target.)
                    m_wake.wait_for(lock, IDLE_WAKE_TIMEOUT, [this] {
                        return m_terminateThread || m_stop || m_targetStep != m_currentReportedStep
                            || (m_synchronise && m_synchroniseMotor);
                    });
                    continue; // start a fresh step period
                }
                if (m_targetStep != m_currentReportedStep) {
                    if (oldDirection != m_direction) {
                        // Ensure direction pin is set correctly
                        m_gpio.setReversePin(
                            m_motorNumber,
                            m_direction == Direction::forward ? PinState::low : PinState::high);
                        oldDirection = m_direction;
                        const uint32_t dirTick = m_gpio.getTick();
                        while (m_gpio.getTick() - dirTick < DIRECTION_SETUP_MICROSECONDS)
                            ;
                    }
                    // Do step pulse
                    m_gpio.setStepPin(m_motorNumber, PinState::high);
                    // Spin for first delay amount:
                    while (m_gpio.getTick() - startTick < static_cast<uint32_t>(m_delay))
                        ;
                    m_gpio.setStepPin(m_motorNumber, PinState::low);
                    if (m_targetStep < m_currentReportedStep) {
                        // we only start counting steps once the slack of the backlash
                        // compensation has been taken up
                        if (m_backlashPosition == 0) {
                            --m_currentReportedStep;
                        } else {
                            --m_backlashPosition;
                        }
                        --m_currentActualStep; // This is only used for testing
                    } else {
                        if (m_backlashPosition == m_backlashSize) {
                            ++m_currentReportedStep;
                        } else {
                            ++m_backlashPosition;
                        }
                        ++m_currentActualStep; // This is only used for testing
                    }
                    if (m_currentReportedStep == m_targetStep && !m_following) {
                        m_busy = false;
                    }
                }
                // Ramping must happen inside the lock: it reads m_rpm and
                // writes m_delay, both of which setRpm() also writes.
                if (m_busy && m_useRamping && m_rampedRpm < m_rpm) {
                    m_rampedRpm += m_rampingSpeed;
                    if (m_rampedRpm > m_rpm) {
                        m_rampedRpm = m_rpm.load();
                    }
                    m_delay = calculateDelayValue(m_rampedRpm);
                }
                delay = m_delay; // remember delay value while in mtx scope
            } // scope for lock

            // We always perform the second delay regardless of
            // whether we're stepping, to give the main thread a
            // chance to grab the mutex if needed

            // Spin until we get to the right time
            while (m_gpio.getTick() - startTick < static_cast<uint32_t>(2 * delay))
                ;
        }
    } // thread end
    );
    // Store the thread's handle in m_thread
    m_thread.swap(t);
    // Set the thread's scheduling to be real-time; cannot do this portably:
    const int requestedPriority = ::sched_get_priority_max(SCHED_RR);
    sched_param schedParams {};
    schedParams.sched_priority = requestedPriority;
    const int rc = ::pthread_setschedparam(m_thread.native_handle(), SCHED_RR, &schedParams);
    // Check we have set priority OK
    int policy = 0;
    sched_param actual {};
    ::pthread_getschedparam(m_thread.native_handle(), &policy, &actual);
    m_isRealTimeScheduled
        = rc == 0 && policy == SCHED_RR && actual.sched_priority == requestedPriority;
}

StepperMotor::~StepperMotor()
{
    {
        std::lock_guard lock(m_mtx);
        m_terminateThread = true;
    }
    m_wake.notify_all();
    m_thread.join();
}

void StepperMotor::goToStep(long step, bool noLock)
{
    // noLock is only true when called from within the motor thread, which
    // already holds m_mtx. (NB a lock_guard inside an "if" block would be
    // released at the end of that block, so we use a deferred unique_lock.)
    std::unique_lock lock(m_mtx, std::defer_lock);
    if (!noLock) {
        lock.lock();
    }
    if (m_busy) {
        // We ignore any request to go to a location if
        // we are already stepping. The client code can
        // issue a stop if needed before changing target
        // step location.
        return;
    }
    if (m_faulted) {
        // The driver is in alarm, so we mustn't move. See setAlarmPin()
        return;
    }
    if (m_currentReportedStep == step) {
        return;
    }
    if (limitTrippedBeforeMove()) {
        // See setLimitPin()
        return;
    }
    m_direction = Direction::forward;
    if (step < m_currentReportedStep) {
        m_direction = Direction::reverse;
    }
    m_busy = true;
    m_stop = false;
    m_targetStep = step;
    m_wake.notify_all();
    if (m_usingMockLinearScale) {
        // This is solely to drive the mock linear scale in step with this motor
        // (the scale* functions are no-ops on real hardware)
        m_gpio.scaleGoToPositionMm(step * m_conversionFactor);
        double motorSpeedMmPerSec = getSpeed() / 60.0;
        // Say we are moving at 0.5 mm/sec then steps per sec should be 200 * 0.5
        // So motorSpeed * stepsPerMm.
        m_gpio.scaleSetSpeedStepsPerSec(motorSpeedMmPerSec * m_mockLinearScaleStepsPerMm);
    }
}

void StepperMotor::stop()
{
    std::unique_lock lock(m_mtx);
    m_stop = true;
    m_wake.notify_all();
    // Wait for the motor thread to act on the stop (it clears m_stop)
    m_stopAcknowledged.wait_for(lock, STOP_ACKNOWLEDGE_TIMEOUT, [this] { return !m_stop; });
}

void StepperMotor::setRpm(double rpm, bool noLock)
{
    if (rpm < 0) {
        rpm = 0;
    }
    if (rpm > m_maxRpm) {
        rpm = m_maxRpm;
    }
    // NB m_delay (in µsecs) is used TWICE per thread loop
    // See goToStep() regarding noLock
    std::unique_lock lock(m_mtx, std::defer_lock);
    if (!noLock) {
        lock.lock();
    }
    if (rpm < 0.00001) {
        // Arbitrarily anything lower than this
        // and we assune zero was required
        m_rpm = 0;
        m_rampedRpm = 0;
        m_stop = true;
        m_wake.notify_all();
        return;
    }
    if (m_useRamping && rpm > m_rpm) {
        // We don't start ramping from zero
        m_rampedRpm = m_maxRpm * RAMP_START_FRACTION;
        if (m_rampedRpm > rpm) {
            m_rampedRpm = rpm;
        }
    }
    m_rpm = rpm;
    m_delay = calculateDelayValue(m_rpm);
    if (m_usingMockLinearScale) {
        // Keep the mock linear scale's speed in step with this motor
        const double motorSpeedMmPerSec = getSpeed() / 60.0;
        m_gpio.scaleSetSpeedStepsPerSec(motorSpeedMmPerSec * m_mockLinearScaleStepsPerMm);
    }
}

void StepperMotor::setSpeed(double speed, bool noLock)
{
    const double rpm = std::abs(speed / m_conversionFactor / m_stepsPerRevolution);
    setRpm(rpm, noLock);
}

double StepperMotor::getRpm()
{
    return m_rpm;
}

double StepperMotor::getSpeed() const
{
    return std::abs(m_rpm * m_stepsPerRevolution * m_conversionFactor);
}

double StepperMotor::getMaxRpm()
{
    return m_maxRpm;
}

int StepperMotor::getDelay()
{
    std::lock_guard lock(m_mtx);
    return m_delay;
}

long StepperMotor::getCurrentStep() const
{
    return m_currentReportedStep;
}

long StepperMotor::getTargetStep() const
{
    return m_targetStep;
}

void StepperMotor::zeroPosition()
{
    {
        std::lock_guard lock(m_mtx);
        m_currentReportedStep = 0L;
        m_targetStep = 0L;
    }
    stop();
}

void StepperMotor::wait()
{
    while (m_busy) {
        m_gpio.delayMicroSeconds(10'000);
    }
}

bool StepperMotor::isRunning() const
{
    return m_busy;
}

Direction StepperMotor::getDirection() const
{
    return m_direction;
}

double StepperMotor::getPosition() const
{
    return m_currentReportedStep * m_conversionFactor;
}

double StepperMotor::getPosition(long step) const
{
    return step * m_conversionFactor;
}

void StepperMotor::goToPosition(double mm, bool noLock)
{
    goToStep(std::lround(mm / m_conversionFactor), noLock);
}

void StepperMotor::setPosition(double mm)
{
    {
        std::lock_guard lock(m_mtx);
        long step = std::lround(mm / m_conversionFactor);
        m_currentReportedStep = step;
        m_targetStep = step;
    }
    stop();
}

void StepperMotor::setBacklashCompensation(unsigned int compensation, unsigned int currentPosition)
{
    std::lock_guard lock(m_mtx);
    m_backlashSize = compensation;
    if (currentPosition > compensation) {
        m_backlashPosition = compensation;
    } else {
        m_backlashPosition = currentPosition;
    }
}

long StepperMotor::getCurrentStepWithoutBacklashCompensation() const
{
    return m_currentActualStep;
}

double StepperMotor::getRampedSpeed()
{
    return std::abs(m_rampedRpm * m_stepsPerRevolution * m_conversionFactor);
}

double StepperMotor::getConversionFactor() const
{
    return m_conversionFactor;
}

double StepperMotor::calculateDelayValue(double rpm)
{
    int delay = std::round(500'000.0 / (static_cast<double>(m_stepsPerRevolution) * (rpm / 60.0)));
    if (delay < 10) {
        // Avoid very small delay values as the stepper
        // motor won't be able to keep up
        delay = 10;
    }
    return delay;
}

void StepperMotor::enableRamping(bool flag)
{
    std::lock_guard lock(m_mtx);
    if (m_rampingSpeed != 0.0) {
        m_useRamping = flag;
    }
}

void StepperMotor::synchroniseOn(
    const StepperMotor* other,
    std::function<double(double, double)> func,
    bool useZeroAsSyncStartPos // = false
)
{
    std::lock_guard lock(m_mtx);
    m_synchronise = true;
    m_synchroniseMotor = other;
    m_synchroniseFunction = func;
    m_useZeroAsSyncStartPos = useZeroAsSyncStartPos;
    m_wake.notify_all();
}

void StepperMotor::synchroniseOff()
{
    std::lock_guard lock(m_mtx);
    m_synchronise = false;
    m_syncFirstCall = true;
}

void StepperMotor::followTo(long endStep, std::function<long()> targetStep)
{
    std::lock_guard lock(m_mtx);
    if (m_busy || m_faulted || m_currentReportedStep == endStep || limitTrippedBeforeMove()) {
        return;
    }
    m_followTarget = std::move(targetStep);
    m_followStart = m_currentReportedStep;
    m_followEnd = endStep;
    m_following = true;
    m_busy = true;
    m_stop = false;
    m_wake.notify_all();
}

bool StepperMotor::isFollowing() const
{
    return m_following;
}

void StepperMotor::follow()
{
    const long current = m_currentReportedStep;
    if (current == m_followEnd) {
        m_following = false;
        m_busy = false;
        m_targetStep = current;
        m_followTarget = nullptr;
        return;
    }
    // (Kept between the start and the end)
    const long low = std::min(m_followStart, m_followEnd);
    const long high = std::max(m_followStart, m_followEnd);
    const long target = std::clamp(m_followTarget(), low, high);
    if (m_usingMockLinearScale && target != m_targetStep) {
        // (As in goToStep(), to drive the mock linear scale with this motor)
        m_gpio.scaleGoToPositionMm(target * m_conversionFactor);
        m_gpio.scaleSetSpeedStepsPerSec(getSpeed() / 60.0 * m_mockLinearScaleStepsPerMm);
    }
    m_targetStep = target;
    if (target > current) {
        m_direction = Direction::forward;
    } else if (target < current) {
        m_direction = Direction::reverse;
    }
}

bool StepperMotor::isRunningRealTimeScheduled()
{
    return m_isRealTimeScheduled;
}

bool StepperMotor::setCpuCore(int core)
{
#ifdef __linux__
    if (core < 0 || core >= CPU_SETSIZE) {
        return false;
    }
    cpu_set_t cpus;
    CPU_ZERO(&cpus);
    CPU_SET(core, &cpus);
    return ::pthread_setaffinity_np(m_thread.native_handle(), sizeof(cpus), &cpus) == 0;
#else
    (void)core;
    return false;
#endif
}

void StepperMotor::setSpinWhenIdle(bool flag)
{
    m_spinWhenIdle = flag;
    m_wake.notify_all(); // if it's sleeping, it starts spinning now
}

void StepperMotor::setAlarmPin(int pin, PinState activeLevel)
{
    std::lock_guard lock(m_mtx);
    m_gpio.setInputPin(pin, true);
    m_alarmActiveLevel = activeLevel;
    m_alarmActive = false;
    m_alarmPin = pin;
}

bool StepperMotor::isFaulted() const
{
    return m_faulted;
}

bool StepperMotor::clearFault()
{
    std::lock_guard lock(m_mtx);
    const int pin = m_alarmPin;
    if (pin >= 0 && m_gpio.readPin(pin) == m_alarmActiveLevel) {
        return false; // the driver is still in alarm
    }
    m_faulted = false;
    m_alarmActive = false;
    return true;
}

void StepperMotor::checkAlarm()
{
    const int pin = m_alarmPin;
    if (pin < 0) {
        return;
    }
    if (m_gpio.readPin(pin) != m_alarmActiveLevel) {
        m_alarmActive = false;
        return;
    }
    const uint32_t now = m_gpio.getTick();
    if (!m_alarmActive) {
        // Only act if it stays active, so a brief spike of electrical noise
        // can't stop the motor (e.g. part way through cutting a thread)
        m_alarmActive = true;
        m_alarmActiveSince = now;
        return;
    }
    if (!m_faulted && now - m_alarmActiveSince >= ALARM_FILTER_MICROSECONDS) {
        m_faulted = true;
    }
}

void StepperMotor::setLimitPin(int pin, PinState activeLevel)
{
    std::lock_guard lock(m_mtx);
    m_gpio.setInputPin(pin, true);
    m_limitActiveLevel = activeLevel;
    m_limitActive = false;
    m_limitArmed = true;
    m_limitTripped = false;
    m_limitPin = pin;
}

bool StepperMotor::isLimitTripped() const
{
    return m_limitTripped;
}

bool StepperMotor::isLimitSwitchActive()
{
    const int pin = m_limitPin;
    return pin >= 0 && m_gpio.readPin(pin) == m_limitActiveLevel;
}

void StepperMotor::clearLimitTrip()
{
    std::lock_guard lock(m_mtx);
    m_limitTripped = false;
    m_limitActive = false;
    // (Read here, rather than relying on the motor thread having seen it: an
    // idle motor's thread may not have)
    if (isLimitSwitchActive()) {
        m_limitArmed = false;
    }
}

void StepperMotor::checkLimit()
{
    const int pin = m_limitPin;
    if (pin < 0) {
        return;
    }
    if (m_gpio.readPin(pin) != m_limitActiveLevel) {
        m_limitActive = false;
        m_limitArmed = true;
        return;
    }
    if (!m_limitArmed || m_limitTripped) {
        return;
    }
    const uint32_t now = m_gpio.getTick();
    if (!m_limitActive) {
        // Only act if it stays active, so a brief spike of electrical noise
        // can't stop the motor (e.g. part way through cutting a thread)
        m_limitActive = true;
        m_limitActiveSince = now;
        return;
    }
    if (now - m_limitActiveSince >= LIMIT_FILTER_MICROSECONDS) {
        m_limitTripped = true;
        m_limitArmed = false;
    }
}

bool StepperMotor::limitTrippedBeforeMove()
{
    if (m_limitTripped) {
        return true;
    }
    // (Not filtered, unlike checkLimit(): at worst, noise refuses a move,
    // which is safe)
    if (m_limitArmed && isLimitSwitchActive()) {
        m_limitTripped = true;
        m_limitArmed = false;
        m_limitActive = false;
    }
    return m_limitTripped;
}

bool StepperMotor::isBlocked() const
{
    return m_faulted || m_limitTripped;
}

void StepperMotor::synchronise()
{
    // If we know the speed of the other motor, and can
    // determine the delta of its position from last time,
    // we can set our speed as a ratio of the other's speed
    // to our distance to be moved.

    if (m_syncFirstCall) {
        m_syncFirstCall = false;
        m_syncOtherStartPos = m_synchroniseMotor->getPosition();
        if (m_useZeroAsSyncStartPos) {
            m_syncStartPos = 0.0;
        } else {
            m_syncStartPos = getPosition();
        }
        return;
    }
    double otherCurrentPos = m_synchroniseMotor->getPosition();
    double otherPositionDelta = otherCurrentPos - m_syncOtherStartPos;
    double newPosDelta = m_synchroniseFunction(otherPositionDelta, otherCurrentPos);
    // We set the speed higher than it needs to be to ensure
    // that this motor can keep up with the synced motor
    double otherSpeed = 10 * m_synchroniseMotor->getSpeed();
    double deltaRatio = newPosDelta;
    if (otherPositionDelta != 0.0) {
        // otherPositionDelta shouldn't ever be zero, but we check
        // nevertheless to avoid divide by zero
        deltaRatio /= otherPositionDelta;
    }
    setSpeed(otherSpeed * std::abs(deltaRatio) + 10.0, true);
    goToPosition(m_syncStartPos + newPosDelta, true);
}

} // end namespace
