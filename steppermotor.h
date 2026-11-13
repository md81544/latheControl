#pragma once

#include "igpio.h" // for PinState

#include <atomic>
#include <condition_variable>
#include <functional>
#include <pthread.h>
#include <thread>

namespace mgo {

class IGpio;

// A mutex with priority inheritance. If a lower-priority thread (e.g. the UI)
// holds it when a real-time motor thread needs it, the holder is temporarily
// raised to the motor thread's priority, so it can't be pre-empted by other
// threads while the motor waits. Falls back to an ordinary mutex if the
// platform doesn't support priority inheritance.
class PriorityInheritanceMutex {
public:
    PriorityInheritanceMutex();
    ~PriorityInheritanceMutex();
    PriorityInheritanceMutex(const PriorityInheritanceMutex&) = delete;
    PriorityInheritanceMutex& operator=(const PriorityInheritanceMutex&) = delete;
    void lock();
    void unlock();
    bool try_lock();

private:
    pthread_mutex_t m_mutex;
};

enum class Direction {
    forward,
    reverse
};

class StepperMotor {
public:
    // With ramping, a move starts at up to this fraction of the maximum
    // speed, and ramps up from there to any speed above it
    static constexpr double RAMP_START_FRACTION = 0.4;

    StepperMotor(
        IGpio& gpio,
        int stepPin,
        int reversePin,
        int enablePin,
        long stepsPerRevolution,
        double conversionFactor,
        double maxRpm,
        double rampingSpeed = 0.1, // 0.1=fastest, 0.001=slowest, 0.0=off
        bool usingMockLinearScale = false,
        uint32_t mockLinearScaleStepsPerMm = 200);
    ~StepperMotor();
    bool isRunning() const;
    Direction getDirection() const;
    long getCurrentStep() const;
    long getTargetStep() const;
    // Call the current step position zero
    void zeroPosition();
    // Go to a specific step
    void goToStep(long step, bool noLock = false);
    // Set motor speed in RPM (i.e. doesn't care about gearing)
    void setRpm(double rpm, bool noLock = false);
    // Set motor speed in units/min, (i.e. after gearing)
    void setSpeed(double speed, bool noLock = false);
    // Get motor speed as set with setRpm()
    double getRpm();
    // Get motor speed as set with setRpm(), but in units/min
    double getSpeed() const;
    // Get speed limit as set by ctor
    double getMaxRpm();
    // Get current delay value. Can't think of a
    // purpose for this apart from unit testing :)
    int getDelay();
    // Stop any motion. Returns once the motor thread has acted on the stop
    // (normally well under a millisecond; at most 100ms)
    void stop();
    // Block until the current operation completes
    void wait();
    // Return position in real-world (but arbitrary) units,
    // for example mm along an axis. Simply returns the
    // supplied conversion factor multiplied by current step
    double getPosition() const;
    // Do conversion on a value which isn't the current step:
    double getPosition(long step) const;
    // Go to position in UNITS not steps
    void goToPosition(double mm, bool noLock = false);
    // Set current position (don't move) in UNITS not steps
    void setPosition(double mm);
    // Set backlash compensation. The number of steps the system has should be
    // measured. currentPosition should be either zero if slack has been taken
    // up moving negatively, or equal to steps if slack has been taken up by moving
    // in the positive direction of the axis ( i.e. outwards on X or Z ). If this is
    // never called then the motor works as if there is no backlash at all.
    void setBacklashCompensation(unsigned int steps, unsigned int currentPosition);
    // Only really needed for testing:
    long getCurrentStepWithoutBacklashCompensation() const;
    // Also only needed for testing:
    double getRampedSpeed();

    double getConversionFactor() const;
    void enableRamping(bool flag);
    // Synchronise this motor with another. The function object will be called
    // with the *delta* of the other motor's position since start. We return
    // our new expected delta. Speed is handled automatically by the motor object
    // and will track the "other" motor's speed, proportionally.
    void synchroniseOn(
        const StepperMotor* const other,
        std::function<double(double posDelta, double pos)> func,
        bool useZeroAsStartPos = false);
    void synchroniseOff();
    // Follow a moving target, e.g. a position worked out from a spindle's
    // encoder (as for an "electronic leadscrew"). Before each step period, the
    // motor thread calls targetStep() and, if the motor isn't there, takes one
    // step towards it. So the motor keeps up with a target moving at up to the
    // speed set (with setSpeed(); ramping, if enabled, applies as usual), and
    // the speed should be set a little higher than the target needs.
    // The target is kept between the step the motor is on now and endStep
    // (it may move back and forth between them). Following ends when the
    // motor reaches endStep, or on stop(); isRunning() is true meanwhile, and
    // goToStep() is ignored. Does nothing if the motor is already running.
    // targetStep() is called from the motor thread, with the motor's lock
    // held, so must be quick, and mustn't call this motor's functions.
    void followTo(long endStep, std::function<long()> targetStep);
    bool isFollowing() const;
    // A return of false here indicates an error setting thread
    // scheduling (probably the binary wasn't run as root). We shouldn't
    // get here on production hardware as the pigpio *requires* root.
    // For dev hardware with mocked-out pigpio, it will be sufficient
    // to set the capabilities of the binary with "sudo setcap cap_sys_nice+ep build/lc"
    bool isRunningRealTimeScheduled();

    // Run the motor thread only on the given CPU core (numbered from 0). This
    // stops it sharing a core with other threads, which (as it runs at
    // real-time priority, and spins between steps) it could otherwise starve,
    // e.g. pigpio's callback thread. Linux only: returns false if the core
    // couldn't be set (e.g. it doesn't exist, or on another OS)
    bool setCpuCore(int core);
    // By default, an idle motor thread sleeps until it has something to do.
    // With this set, it keeps spinning instead, so its core stays busy with
    // it, and a move can start without waiting for the thread to be woken.
    // Best combined with setCpuCore(), on a machine with cores to spare
    void setSpinWhenIdle(bool flag);

    // Optional: the motor driver's alarm (fault) output, e.g. on a closed-loop
    // driver which goes into alarm when the motor stalls. Such a driver then
    // ignores step pulses, so the motor's position is no longer known.
    // The pin is set as an input with the internal pull-up enabled (suitable
    // for an opto-isolated or open-collector output). If it reads activeLevel
    // continuously for a short time (2ms, to ignore electrical noise), the motor
    // becomes "faulted": it stops at once, and ignores all requests to move
    // until clearFault() succeeds.
    void setAlarmPin(int pin, PinState activeLevel);
    bool isFaulted() const;
    // Clears a fault, but only if the driver's alarm is no longer active.
    // Returns true if the motor is no longer faulted. NB the position is
    // still wrong after a fault; the caller must deal with that (e.g. by
    // re-zeroing)
    bool clearFault();

    // Optional: limit switches, e.g. at the ends of an axis's travel. Several
    // switches can share one pin (e.g. normally-closed switches wired in
    // series, so that any of them opening, or a broken wire, reads as active),
    // and several motors can be given the same pin, so any switch stops them
    // all. The pin is set as an input with the internal pull-up enabled. If
    // it reads activeLevel continuously for a short time (2ms, to ignore
    // electrical noise and contact bounce), the motor "trips": it stops at
    // once, and ignores all requests to move until clearLimitTrip(). (Unlike
    // a fault, the position is still right.) A motor which is asked to move
    // while the pin is already active trips at once instead (e.g. an idle
    // motor, whose thread may not have seen the switch close)
    void setLimitPin(int pin, PinState activeLevel);
    bool isLimitTripped() const;
    // Whether the limit pin reads active now (e.g. a switch is still
    // pressed). False if there's no limit pin
    bool isLimitSwitchActive();
    // Clears a trip, so the motor can move again (e.g. off the switch). If the
    // pin is still active, it won't trip again until the pin has gone
    // inactive (the switch released), so it can be moved off the switch; once
    // released, the switch trips it again as usual
    void clearLimitTrip();

private:
    // Called by the motor thread (with the lock held) to monitor the alarm pin
    void checkAlarm();
    // Called by the motor thread (with the lock held) to monitor the limit pin
    void checkLimit();
    // Called with the lock held before a move starts: trips at once if the
    // limit pin is active (and armed). Returns true if the motor is tripped
    bool limitTrippedBeforeMove();
    // Whether the motor mustn't move (faulted, or a limit switch tripped)
    bool isBlocked() const;
    // Internal, only to be used from within an existing lock scope:
    void synchronise();
    int m_motorNumber { 0 };
    IGpio& m_gpio;
    long m_stepsPerRevolution;
    std::thread m_thread;
    std::atomic<bool> m_terminateThread { false };
    std::atomic<long> m_targetStep { 0 };
    std::atomic<bool> m_busy { false };
    // What step the motor reports that it is on (this is adjusted for backlash):
    std::atomic<long> m_currentReportedStep { 0 };
    // Actual current step regardless of backlash compensation (only used for testing):
    std::atomic<long> m_currentActualStep { 0 };
    std::atomic<bool> m_stop { false };
    std::atomic<int> m_delay { 500 }; // µsecs
    std::atomic<Direction> m_direction { Direction::forward };
    // lock should be taken before any code outside the
    // background thread changes any member variables
    PriorityInheritanceMutex m_mtx;
    // Wakes the motor thread when it is idle (see the thread loop), so must be
    // notified whenever there may be something new for it to do
    std::condition_variable_any m_wake;
    // Notified by the motor thread when it has acted on m_stop
    std::condition_variable_any m_stopAcknowledged;
    double m_conversionFactor;
    // Atomic as these are read without the lock by getSpeed() etc (including
    // from another motor's thread when synchronised); writes happen under m_mtx
    std::atomic<double> m_rpm { 0.0 };
    std::atomic<double> m_rampedRpm { 0.0 };
    double m_maxRpm;
    unsigned int m_backlashSize { 0 };
    unsigned int m_backlashPosition { 0 };
    bool m_useRamping { true };
    double m_rampingSpeed { 0.1 };
    double calculateDelayValue(double rpm);
    bool m_synchronise { false };
    const StepperMotor* m_synchroniseMotor { nullptr };
    std::function<double(double, double)> m_synchroniseFunction;
    // See followTo(). Guarded by m_mtx (m_following is also read without it)
    std::atomic<bool> m_following { false };
    std::function<long()> m_followTarget;
    long m_followStart { 0 };
    long m_followEnd { 0 };
    // Internal, only to be used from within an existing lock scope:
    void follow();

    bool m_syncFirstCall { true };
    double m_syncOtherStartPos;
    double m_syncStartPos;
    bool m_useZeroAsSyncStartPos { false };
    bool m_usingMockLinearScale { false };
    uint32_t m_mockLinearScaleStepsPerMm { 200 };

    bool m_isRealTimeScheduled { false };
    std::atomic<bool> m_spinWhenIdle { false };

    // Alarm (fault) monitoring. See setAlarmPin()
    std::atomic<int> m_alarmPin { -1 }; // -1 = none
    std::atomic<PinState> m_alarmActiveLevel { PinState::low };
    std::atomic<bool> m_faulted { false };
    // These are guarded by m_mtx:
    bool m_alarmActive { false };
    uint32_t m_alarmActiveSince { 0 };

    // Limit switch monitoring. See setLimitPin()
    std::atomic<int> m_limitPin { -1 }; // -1 = none
    std::atomic<PinState> m_limitActiveLevel { PinState::high };
    std::atomic<bool> m_limitTripped { false };
    // These are guarded by m_mtx:
    bool m_limitActive { false };
    uint32_t m_limitActiveSince { 0 };
    // False after a trip (or a clear while the pin is active) until the pin
    // has gone inactive, so a switch still pressed doesn't trip it again
    bool m_limitArmed { true };
};

} // end namespace
