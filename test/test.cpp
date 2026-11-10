#include "configreader.h"
#include "commands.h"
#include "joystick.h"
#include "keycodes.h"
#include "menu.h"
#include "model.h"
#include "motionsequence.h"
#include "multipass.h"
#include "rotaryencoder.h"
#include "stepperControl/mockgpio.h"
#include "stepperControl/steppermotor.h"
#include "threadpitches.h"

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <set>
#include <sstream>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include <cmath>
#include <ctime>
#include <thread>

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

namespace {

// A minimal IGpio with a controllable clock and no background threads, so
// tests can drive callbacks directly and deterministically. Each getTick()
// call advances time by 1µs, so code which spins on getTick() terminates.
class FakeTickGpio final : public mgo::IGpio {
public:
    explicit FakeTickGpio(uint32_t startTick)
        : m_tick(startTick)
    {
    }
    int addMotor(int, int, int) override { return 0; }
    void setStepPin(int, mgo::PinState) override { }
    void setReversePin(int, mgo::PinState) override { }
    void setEnablePin(int, mgo::PinState) override { }
    void setRotaryEncoderCallback(int, int, void (*)(int, int, uint32_t, void*), void*) override
    {
    }
    void setLinearScaleAxis1Callback(int, int, void (*)(int, int, uint32_t, void*), void*) override
    {
    }
    void delayMicroSeconds(long usecs) override { m_tick += static_cast<uint32_t>(usecs); }
    uint32_t getTick() override { return m_tick++; }
    void setInputPin(int, bool) override { }
    mgo::PinState readPin(int) override { return mgo::PinState::high; }
    void scaleGoToPositionMm(double) override { }
    void scaleSetSpeedStepsPerSec(double) override { }
    void scaleStop() override { }
    uint32_t peekTick() const { return m_tick; }

private:
    std::atomic<uint32_t> m_tick;
};

constexpr int RE_PIN_A = 23;
constexpr int RE_PIN_B = 24;

// Feed one full quadrature cycle (in the encoder's "normal" direction) whose
// pin A rising edge is at the given tick
void feedEncoderPulse(mgo::RotaryEncoder& re, uint32_t tick)
{
    re.callback(RE_PIN_B, 1, tick - 6'000);
    re.callback(RE_PIN_A, 1, tick);
    re.callback(RE_PIN_B, 0, tick + 2'000);
    re.callback(RE_PIN_A, 0, tick + 4'000);
}

// Waits (up to a time limit) for a condition to become true
template <typename F>
bool waitFor(F condition, std::chrono::milliseconds limit = std::chrono::milliseconds(1'000))
{
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (!condition()) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

// Runs the model's updates (as the controller's tick does) until any motion
// sequence (e.g. a taper's backlash take-up) has finished and both motors
// have stopped, as seen on two updates in a row. (An axis synchronised to
// another, as X is in a taper, can be stopped for a moment between its moves
// while still catching up.) Returns false if that took too long
bool runUntilStopped(mgo::Model& model)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    bool stoppedBefore = false;
    for (;;) {
        model.checkStatus();
        const bool stopped = !model.isMotionSequenceRunning() && !model.isAxis1MotorRunning()
            && !model.isAxis2MotorRunning();
        if (stopped && stoppedBefore) {
            return true;
        }
        stoppedBefore = stopped;
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

} // end anonymous namespace

TEST_CASE("Stepper: Step once")
{
    mgo::MockGpio gpio(false);
    mgo::StepperMotor motor(gpio, 0, 0, 0, 1'000, 1.0, 10'000.0);
    motor.setRpm(500.0);
    motor.goToStep(3);
    motor.wait();

    REQUIRE(motor.getCurrentStep() == 3);
}

TEST_CASE("Stepper: Stop motor")
{
    mgo::MockGpio gpio(false);
    mgo::StepperMotor motor(gpio, 0, 0, 0, 1'000, 1.0, 10'000.0);
    // High number of steps:
    motor.setRpm(500.0);
    motor.goToStep(1'000'000);
    gpio.delayMicroSeconds(1'000);
    REQUIRE(motor.isRunning());
    motor.stop();
    motor.wait();
    REQUIRE(!motor.isRunning());
    REQUIRE(motor.getCurrentStep() > 0);
}

TEST_CASE("Stepper: Follow a moving target")
{
    mgo::MockGpio gpio(false);
    mgo::StepperMotor motor(gpio, 0, 0, 0, 1'000, 1.0, 10'000.0);
    motor.setRpm(500.0);
    std::atomic<long> target { 0 };
    motor.followTo(1'000, [&target] { return target.load(); });
    REQUIRE(motor.isRunning());
    REQUIRE(motor.isFollowing());
    const auto at = [&motor](long step) {
        return waitFor([&motor, step] { return motor.getCurrentStep() == step; });
    };
    // It follows the target, forwards and back, still running when it's there
    target = 100;
    REQUIRE(at(100));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    REQUIRE(motor.getCurrentStep() == 100);
    REQUIRE(motor.isRunning());
    target = 40;
    REQUIRE(at(40));
    // goToStep() is ignored meanwhile
    motor.goToStep(500);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    REQUIRE(motor.getCurrentStep() == 40);
    // The target is kept between the start and the end...
    target = -50;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    REQUIRE(motor.getCurrentStep() == 0);
    REQUIRE(motor.isRunning());
    // ...and following ends at the end
    target = 2'000;
    REQUIRE(waitFor([&motor] { return !motor.isRunning(); }));
    REQUIRE(motor.getCurrentStep() == 1'000);
    REQUIRE(!motor.isFollowing());
    // (After which it moves normally again)
    motor.goToStep(990);
    motor.wait();
    REQUIRE(motor.getCurrentStep() == 990);
}

TEST_CASE("Stepper: Stop while following")
{
    mgo::MockGpio gpio(false);
    mgo::StepperMotor motor(gpio, 0, 0, 0, 1'000, 1.0, 10'000.0);
    motor.setRpm(500.0);
    std::atomic<long> target { 50 };
    motor.followTo(-1'000, [&target] { return target.load(); }); // (backwards)
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    REQUIRE(motor.getCurrentStep() == 0); // (50 is beyond the start)
    target = -30;
    REQUIRE(waitFor([&motor] { return motor.getCurrentStep() == -30; }));
    motor.stop();
    REQUIRE(!motor.isRunning());
    REQUIRE(!motor.isFollowing());
    target = -60;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    REQUIRE(motor.getCurrentStep() == -30);

    // It won't start following while running, or to where it is already
    motor.goToStep(100'000);
    motor.followTo(-1'000, [] { return -1'000L; });
    REQUIRE(!motor.isFollowing());
    motor.stop();
    motor.followTo(motor.getCurrentStep(), [] { return 0L; });
    REQUIRE(!motor.isFollowing());
}

TEST_CASE("Stepper: Stop stopped motor")
{
    mgo::MockGpio gpio(false);
    mgo::StepperMotor motor(gpio, 0, 0, 0, 1'000, 1.0, 10'000.0);
    motor.stop();
    motor.wait();
    REQUIRE(!motor.isRunning());
}

TEST_CASE("Stepper: Move then move again")
{
    mgo::MockGpio gpio(false);
    mgo::StepperMotor motor(gpio, 0, 0, 0, 1'000, 1.0, 10'000.0);
    motor.setRpm(500.0);
    motor.goToStep(50);
    motor.wait();
    motor.goToStep(100);
    motor.wait();
    motor.goToStep(150);
    motor.wait();
    REQUIRE(!motor.isRunning());
    REQUIRE(motor.getCurrentStep() == 150);
}

TEST_CASE("Stepper: Forward and reverse")
{
    mgo::MockGpio gpio(false);
    mgo::StepperMotor motor(gpio, 0, 0, 0, 1'000, 1.0, 10'000.0);
    motor.setRpm(500.0);
    motor.goToStep(100);
    motor.wait();
    REQUIRE(motor.getCurrentStep() == 100);
    motor.goToStep(50); // This is a reverse now
    motor.wait();
    REQUIRE(motor.getCurrentStep() == 50);
}

TEST_CASE("Stepper: Check direction")
{
    mgo::MockGpio gpio(false);
    mgo::StepperMotor motor(gpio, 0, 0, 0, 1'000, 1.0, 10'000.0);
    motor.setRpm(500.0);
    REQUIRE(motor.getDirection() == mgo::Direction::forward);
    motor.goToStep(100);
    motor.wait();
    REQUIRE(motor.getDirection() == mgo::Direction::forward);
    motor.goToStep(50); // This is a reverse now
    motor.wait();
    REQUIRE(motor.getDirection() == mgo::Direction::reverse);
    motor.goToStep(100);
    motor.wait();
    REQUIRE(motor.getDirection() == mgo::Direction::forward);
}

TEST_CASE("Stepper: RPM")
{
    mgo::MockGpio gpio(false);
    mgo::StepperMotor motor(gpio, 0, 0, 0, 1'000, 1.0, 10'000.0);
    motor.setRpm(60);
    // That's one revolution per second, and we've set steps per
    // revolution to be 1000, so delay (which is used TWICE per
    // thread loop), should be 500 usecs
    REQUIRE(motor.getDelay() == 500);
    motor.setRpm(120);
    REQUIRE(motor.getDelay() == 250);
    motor.setRpm(2'000);
    REQUIRE(motor.getDelay() == 15);
    motor.setRpm(1);
    REQUIRE(motor.getDelay() == 30'000);
}

TEST_CASE("Stepper: RPM Limits")
{
    mgo::MockGpio gpio(false);
    mgo::StepperMotor motor(gpio, 0, 0, 0, 1'000, 1.0, 10'000.0);
    motor.setRpm(0);
    motor.goToStep(10); // Shouldn't take infinite time :)
    motor.wait();
    motor.setRpm(9'999'999); // bit too fast :)
    REQUIRE(motor.getDelay() > 5);
}

TEST_CASE("Stepper: Change target step while busy")
{
    mgo::MockGpio gpio(false);
    mgo::StepperMotor motor(gpio, 0, 0, 0, 1'000, 1.0, 10'000.0);
    motor.setRpm(2'000);
    motor.goToStep(1'000);
    motor.goToStep(2'000);
    motor.wait();
    // Check the second "go to" was ignored
    REQUIRE(motor.getCurrentStep() == 1'000);
}

TEST_CASE("Stepper: Rotary Encoder RPM")
{
    mgo::MockGpio gpio(false);
    mgo::RotaryEncoder re(gpio, 23, 24, 2000, 35.f / 30.f);
    while (re.warmingUp()) {
        gpio.delayMicroSeconds(1'000);
    }
    gpio.delayMicroSeconds(2'000'000);
    REQUIRE(re.getRpm() > 0.f);
}

TEST_CASE("Stepper: Check backlash compensation")
{
    mgo::MockGpio gpio(false);
    mgo::StepperMotor motor(gpio, 0, 0, 0, 1'000, 1.0, 10'000.0);
    // Set backlash compensation. This sets our backlash slop to be ten
    // steps which means if we move one step in a positive manner the
    // motor should really have to move eleven "real" steps
    motor.setRpm(500.0);
    motor.setBacklashCompensation(10, 0);
    motor.goToStep(1);
    motor.wait();
    REQUIRE(motor.getCurrentStep() == 1);
    REQUIRE(motor.getCurrentStepWithoutBacklashCompensation() == 11);

    // Now we are at the far end of the backlash "window" so moving the next
    // one step should only move one "real" step
    motor.goToStep(2);
    motor.wait();
    REQUIRE(motor.getCurrentStep() == 2);
    REQUIRE(motor.getCurrentStepWithoutBacklashCompensation() == 12);

    // Now as we are pushed up against one end of the backlash window, if
    // we want to move BACK one step, the motor will actually need to do
    // another eleven steps:
    motor.goToStep(1);
    motor.wait();
    REQUIRE(motor.getCurrentStep() == 1);
    REQUIRE(motor.getCurrentStepWithoutBacklashCompensation() == 1);
}

TEST_CASE("Stepper: Check motor speed ramping")
{
    mgo::MockGpio gpio(false);
    double maxSpeed = 1'000.0; // mm/sec, not rpm
    long stepsPerRev = 4'000;
    double conversionFactor = 0.0005;
    double maxRpm = maxSpeed / conversionFactor / stepsPerRev;
    mgo::StepperMotor motor(gpio, 0, 0, 0, stepsPerRev, conversionFactor, maxRpm);
    motor.setSpeed(maxSpeed);
    motor.goToStep(999'999'999);
    // Motor should not be at full speed immediately:
    double previousRampedRpm = 0.0;
    for (int n = 0; n < 5; ++n) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        double rampedSpeed = motor.getRampedSpeed();
        REQUIRE(rampedSpeed < maxSpeed);
        REQUIRE(rampedSpeed > previousRampedRpm);
        previousRampedRpm = rampedSpeed;
    }
}

TEST_CASE("Stepper: Check motor synchronisation")
{
    mgo::MockGpio gpio(false);
    mgo::StepperMotor motor1(gpio, 0, 0, 0, 1'000, 0.01, 10'000.0);
    mgo::StepperMotor motor2(gpio, 0, 0, 0, 1'000, 0.01, 10'000.0);
    motor1.setRpm(500.0);
    motor2.setRpm(500.0);
    REQUIRE(motor2.getPosition() == 0.0);
    motor2.synchroniseOn(&motor1, [](double pos, double) {
        return pos / 2.0;
    });
    motor1.setSpeed(1'000.0);
    motor1.goToPosition(2.4);
    motor1.wait();
    motor2.wait();
    REQUIRE(motor2.getPosition() == Approx(1.2));
}

TEST_CASE("Model:   check tapering")
{
    mgo::MockConfigReader config;
    mgo::MockGpio gpio(false);
    // The mock config reader just returns whatever you specify
    // as the default return value
    mgo::Model model(gpio, config);
    model.initialise();
    model.changeMode(mgo::Mode::Taper);
    model.setTaperAngle(45.0);
    model.axis1SetSpeed(200.0);
    model.axis1GoToPosition(-0.5);
    REQUIRE(runUntilStopped(model));
    double pos = model.getAxis2MotorPosition();
    REQUIRE(pos < -0.495);
    REQUIRE(pos > -0.505);
}

TEST_CASE("Model:   check tapering (small angle)")
{
    mgo::MockConfigReader config;
    mgo::MockGpio gpio(false);
    // The mock config reader just returns whatever you specify
    // as the default return value
    mgo::Model model(gpio, config);
    model.initialise();
    model.changeMode(mgo::Mode::Taper);
    model.setTaperAngle(-1.42);
    model.axis1SetSpeed(200.0);
    model.axis1GoToPosition(-0.5);
    REQUIRE(runUntilStopped(model));
    double pos = model.getAxis2MotorPosition();
    REQUIRE(pos != 0.0);
}

TEST_CASE("Model:   check radius")
{
    mgo::MockConfigReader config;
    mgo::MockGpio gpio(false);
    // The mock config reader just returns whatever you specify
    // as the default return value
    mgo::Model model(gpio, config);
    model.initialise();
    model.changeMode(mgo::Mode::Radius);
    model.setRadius(1.0);
    model.axis1SetSpeed(60.0);
    model.axis1GoToPosition(1.0);
    REQUIRE(runUntilStopped(model));

    double pos = model.getAxis2MotorPosition();
    REQUIRE(pos < 1.05);
    REQUIRE(pos > 0.95);

    model.axis1GoToPosition(0.0);
    REQUIRE(runUntilStopped(model));

    // X position should be zero
    pos = model.getAxis2MotorPosition();
    REQUIRE(pos < 0.05);
}

TEST_CASE("Scale:   Forward and reverse")
{
    mgo::MockGpio gpio(false);
    // Important that we specify two fictitious but distinct pin numbers for the
    // scale object so that phase detection actually happens:
    mgo::LinearScale scale(gpio, 1, 2, 200);
    // The scale class doesn't have any functions to move (obviously)
    // but the mock gpio class has some mock-only commands to fake the motion
    // similar to the mock rotary encoder callbacker
    REQUIRE(scale.getPositionInMm() == 0.0);
    gpio.scaleSetSpeedStepsPerSec(400.0);
    gpio.scaleGoToPositionMm(1.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    // We expect to see a non-zero value. The value returned is timing dependent so
    // we just expect it to be greater than it was before.
    double mm = scale.getPositionInMm();
    REQUIRE(mm > 0.0);
    // Now reverse:
    gpio.scaleGoToPositionMm(0.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    REQUIRE(scale.getPositionInMm() < mm);
}

TEST_CASE("Scale:   Zeroing")
{
    mgo::MockGpio gpio(false);
    // Important that we specify two fictitious but distinct pin numbers for the
    // scale object so that phase detection actually happens:
    mgo::LinearScale scale(gpio, 1, 2, 200);
    // The scale class doesn't have any functions to move (obvioulsy)
    // but the mock gpio class has some mock-only commands to fake the motion
    // similar to the mock rotary encoder callbacker
    REQUIRE(scale.getPositionInMm() == 0.0);
    gpio.scaleSetSpeedStepsPerSec(400.0);
    gpio.scaleGoToPositionMm(1.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    double mm = scale.getPositionInMm();
    REQUIRE(mm != 0.0);
    scale.setZeroMm();
    REQUIRE(scale.getPositionInMm() == 0.0);
    gpio.scaleGoToPositionMm(0.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    double newMm = scale.getPositionInMm();
    REQUIRE(newMm < 0.0);
}

TEST_CASE("Scale:   Sync with motor")
{
    mgo::MockGpio gpio(false);
    // Note motor's conversion factor is 0.01, so 100 steps is 1.0mm
    mgo::StepperMotor motor(gpio, 0, 0, 0, 1'000, 0.01, 10'000.0, 0.1, true, 200);
    mgo::LinearScale scale(gpio, 1, 2, 200);
    motor.setRpm(500.0);
    REQUIRE(scale.getPositionInMm() == 0.0);
    motor.goToStep(100);
    motor.wait();
    REQUIRE(motor.getCurrentStep() == 100);
    // Small wait just to let the mock linear scale move
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    double mm = scale.getPositionInMm();
    REQUIRE(mm > 0.0);
    motor.goToStep(50); // This is a reverse now
    motor.wait();
    REQUIRE(motor.getCurrentStep() == 50);
    // Small wait just to let the mock linear scale move
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    mm = scale.getPositionInMm();
    REQUIRE(std::abs(mm - 0.5) < 0.1);
}

TEST_CASE("Scale:   Dithering across an edge doesn't drift")
{
    FakeTickGpio gpio(0);
    mgo::LinearScale scale(gpio, 1, 2, 200);
    // First edge only establishes the phase, so no movement is counted
    scale.callback(2, 1, 0);
    REQUIRE(scale.getPositionInMm() == 0.f);
    // Pin A dithering back and forth (pin B steady). Consecutive edges are on
    // the same pin, and each pair should cancel out.
    for (int i = 0; i < 10; ++i) {
        scale.callback(1, 1, 0);
        REQUIRE(scale.getPositionInMm() == -1.f / 200.f);
        scale.callback(1, 0, 0);
        REQUIRE(scale.getPositionInMm() == 0.f);
    }
    // A callback that doesn't change the level is ignored
    scale.callback(1, 0, 0);
    REQUIRE(scale.getPositionInMm() == 0.f);
}

TEST_CASE("Scale:   Full cycle forwards then backwards")
{
    FakeTickGpio gpio(0);
    mgo::LinearScale scale(gpio, 1, 2, 200);
    scale.callback(2, 1, 0); // establish phase (A low, B high)
    // One full quadrature cycle in one direction: four steps
    scale.callback(1, 1, 0);
    scale.callback(2, 0, 0);
    scale.callback(1, 0, 0);
    scale.callback(2, 1, 0);
    REQUIRE(scale.getPositionInMm() == -4.f / 200.f);
    // The same cycle in the opposite direction brings us back to zero
    scale.callback(2, 0, 0);
    scale.callback(1, 1, 0);
    scale.callback(2, 1, 0);
    scale.callback(1, 0, 0);
    REQUIRE(scale.getPositionInMm() == 0.f);
}

TEST_CASE("Encoder: spindle pulses count up, and down when reversed")
{
    FakeTickGpio gpio(1'000'000);
    mgo::RotaryEncoder re(gpio, RE_PIN_A, RE_PIN_B, 2'000, 0.35f);
    REQUIRE(re.getPulsesPerSpindleRev() == 700);
    // (The first pulse ends the warm-up)
    for (uint32_t k = 0; k < 6; ++k) {
        feedEncoderPulse(re, 500'000 + k * 1'000);
    }
    REQUIRE(re.getSpindlePulses() == 5);
    // Reversed: pin A leads pin B. (The first cycle or two may still count
    // forwards, while the direction change is seen)
    const auto reversedPulse = [&re](uint32_t tick) {
        re.callback(RE_PIN_A, 1, tick);
        re.callback(RE_PIN_B, 1, tick + 200);
        re.callback(RE_PIN_A, 0, tick + 400);
        re.callback(RE_PIN_B, 0, tick + 600);
    };
    reversedPulse(510'000);
    reversedPulse(511'000);
    REQUIRE(re.getRotationDirection() == mgo::RotationDirection::reversed);
    const int64_t before = re.getSpindlePulses();
    for (uint32_t k = 0; k < 4; ++k) {
        reversedPulse(512'000 + k * 1'000);
    }
    REQUIRE(re.getSpindlePulses() == before - 4);
}

TEST_CASE("Encoder: spindle position carries on from the last pulse, up to a limit")
{
    // Pulses every 1ms, the last one half a millisecond before "now"
    const uint32_t now = 1'000'000;
    FakeTickGpio gpio(now);
    mgo::RotaryEncoder re(gpio, RE_PIN_A, RE_PIN_B, 2'000, 0.35f);
    for (uint32_t k = 0; k <= 10; ++k) {
        feedEncoderPulse(re, now - 10'500 + k * 1'000);
    }
    REQUIRE(re.getSpindlePulses() == 10);
    // So half a pulse on. (Each getTick() moves the fake clock on by 1µs)
    REQUIRE(re.getSpindlePosition() == Approx(10.5).margin(0.01));
    // ...but no more than the limit's worth, e.g. if the spindle has stopped
    gpio.delayMicroSeconds(100'000);
    const double limit
        = static_cast<double>(mgo::RotaryEncoder::MAX_EXTRAPOLATION_MICROSECONDS) / 1'000.0;
    REQUIRE(re.getSpindlePosition() == Approx(10.0 + limit));
    // A new pulse starts again from there
    feedEncoderPulse(re, gpio.peekTick() - 4'000);
    REQUIRE(re.getSpindlePulses() == 11);
}

TEST_CASE("Encoder: Callbacks stop when encoder is destroyed")
{
    mgo::MockGpio gpio(false);
    {
        mgo::RotaryEncoder re(gpio, 23, 24, 2000, 35.f / 30.f);
        while (re.warmingUp()) {
            gpio.delayMicroSeconds(1'000);
        }
    }
    // The mock's callbacker thread must no longer be calling into the
    // destroyed encoder. (AddressSanitizer will report it if it is.)
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    // ...and we can register a new encoder with the same GPIO object
    mgo::RotaryEncoder re2(gpio, 23, 24, 2000, 35.f / 30.f);
    while (re2.warmingUp()) {
        gpio.delayMicroSeconds(1'000);
    }
    REQUIRE(re2.warmingUp() == false);
}

TEST_CASE("Stepper: Idle motor thread doesn't spin")
{
    mgo::MockGpio gpio(false);
    mgo::StepperMotor motor(gpio, 0, 0, 0, 1'000, 0.01, 1'000.0);
    motor.setRpm(100.0);
    motor.goToStep(10);
    motor.wait();
    REQUIRE(motor.getCurrentStep() == 10);
    // While idle, the motor thread should be asleep rather than spinning
    // (previously this used a whole core: ~300ms of CPU time here)
    const std::clock_t start = std::clock();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const double cpuMs = 1'000.0 * static_cast<double>(std::clock() - start) / CLOCKS_PER_SEC;
    REQUIRE(cpuMs < 50.0);
    // ...and must wake up promptly when given something to do
    motor.goToStep(20);
    motor.wait();
    REQUIRE(motor.getCurrentStep() == 20);
}

TEST_CASE("Stepper: stop() returns promptly")
{
    mgo::MockGpio gpio(false);
    mgo::StepperMotor motor(gpio, 0, 0, 0, 1'000, 0.01, 1'000.0);
    motor.setRpm(100.0);
    motor.goToStep(1'000'000);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    REQUIRE(motor.isRunning());
    const auto start = std::chrono::steady_clock::now();
    motor.stop();
    const auto elapsed = std::chrono::steady_clock::now() - start;
    REQUIRE(!motor.isRunning());
    // Previously stop() always took 50ms
    REQUIRE(elapsed < std::chrono::milliseconds(20));
}

namespace {

mgo::JoystickEvent button(unsigned n, bool pressed, bool initial = false)
{
    return { mgo::JoystickEvent::Type::Button, n, pressed ? 1.f : 0.f, initial };
}

mgo::JoystickEvent axis(mgo::JoystickAxis a, float value, bool initial = false)
{
    return { mgo::JoystickEvent::Type::Axis, static_cast<unsigned>(a), value, initial };
}

using Keys = std::vector<int>;

} // end anonymous namespace

TEST_CASE("Joystick: buttons")
{
    mgo::JoystickKeyMapper m;
    REQUIRE(m.process({ button(0, true) }) == Keys { mgo::key::AltW });
    REQUIRE(m.process({ button(0, false) }).empty()); // release does nothing
    REQUIRE(m.process({ button(5, true), button(5, false) }) == Keys { mgo::key::SPACE });
    REQUIRE(m.process({ button(8, true) }) == Keys { mgo::key::a1_f });
    // Button 9 is a "shift", so does nothing by itself
    REQUIRE(m.process({ button(9, true) }).empty());
}

TEST_CASE("Joystick: D-pad nudges, with shift")
{
    mgo::JoystickKeyMapper m;
    REQUIRE(m.process({ axis(mgo::JoystickAxis::PovX, 100.f) }) == Keys { mgo::key::d });
    REQUIRE(m.process({ axis(mgo::JoystickAxis::PovX, 0.f) }).empty());
    REQUIRE(m.process({ axis(mgo::JoystickAxis::PovY, -100.f) }) == Keys { mgo::key::w });
    REQUIRE(m.process({ axis(mgo::JoystickAxis::PovY, 0.f) }).empty());
    // With button 9 held, the nudges are the shifted (finer) ones
    REQUIRE(m.process({ button(9, true), axis(mgo::JoystickAxis::PovX, -100.f) })
            == Keys { mgo::key::A });
    // Holding the D-pad doesn't repeat by itself: only another joystick event
    // causes the state to be looked at again
    REQUIRE(m.process({}).empty());
}

TEST_CASE("Joystick: left stick moves once per push")
{
    mgo::JoystickKeyMapper m;
    REQUIRE(m.process({ axis(mgo::JoystickAxis::X, 100.f) }) == Keys { mgo::key::js_right });
    // Still held fully over: no repeat
    REQUIRE(m.process({ axis(mgo::JoystickAxis::X, 100.f) }).empty());
    REQUIRE(m.process({ axis(mgo::JoystickAxis::X, 0.f) }).empty());
    REQUIRE(m.process({ axis(mgo::JoystickAxis::X, -100.f) }) == Keys { mgo::key::js_left });
    REQUIRE(m.process({ axis(mgo::JoystickAxis::X, 0.f) }).empty());
    REQUIRE(m.process({ axis(mgo::JoystickAxis::Y, -100.f) }) == Keys { mgo::key::js_up });
    REQUIRE(m.process({ axis(mgo::JoystickAxis::Y, 0.f) }).empty());
    // Shift + down = retract
    REQUIRE(m.process({ button(9, true), axis(mgo::JoystickAxis::Y, 100.f) })
            == Keys { mgo::key::r });
}

TEST_CASE("Joystick: right stick rapids stop when released (dead man)")
{
    mgo::JoystickKeyMapper m;
    REQUIRE(m.process({ axis(mgo::JoystickAxis::Z, 100.f) }) == Keys { mgo::key::js_rapid_right });
    REQUIRE(m.process({ axis(mgo::JoystickAxis::Z, 100.f) }).empty());
    // Letting the stick come back from fully over stops the motion
    REQUIRE(m.process({ axis(mgo::JoystickAxis::Z, 30.f) }) == Keys { mgo::key::SPACE });
    REQUIRE(m.process({ axis(mgo::JoystickAxis::Z, 0.f) }).empty());
    REQUIRE(m.process({ axis(mgo::JoystickAxis::R, -100.f) }) == Keys { mgo::key::js_rapid_up });
    REQUIRE(m.process({ axis(mgo::JoystickAxis::R, 0.f) }) == Keys { mgo::key::SPACE });
}

TEST_CASE("Joystick: initial state doesn't trigger anything")
{
    // When the device is opened, the driver reports each control's state
    mgo::JoystickKeyMapper m;
    REQUIRE(m.process({ button(0, true, true), axis(mgo::JoystickAxis::PovX, 100.f, true) })
                .empty());

    // A button already held at start-up isn't treated as a new press
    mgo::JoystickKeyMapper m2;
    REQUIRE(m2.process({ button(0, true, true) }).empty());
    REQUIRE(m2.process({ button(0, true) }).empty());
    REQUIRE(m2.process({ button(0, false) }).empty());
    REQUIRE(m2.process({ button(0, true) }) == Keys { mgo::key::AltW });
}

namespace {

// A config reader which returns the given values, and defaults for anything else
class TestConfigReader final : public mgo::IConfigReader {
public:
    explicit TestConfigReader(std::map<std::string, std::string> values)
        : m_values(std::move(values))
    {
    }
    std::string read(const std::string& key, const std::string& defaultValue = "") const override
    {
        const auto it = m_values.find(key);
        return it == m_values.end() ? defaultValue : it->second;
    }
    unsigned long readLong(const std::string& key, unsigned long defaultValue = 0) const override
    {
        const auto it = m_values.find(key);
        return it == m_values.end() ? defaultValue : std::stoul(it->second);
    }
    double readDouble(const std::string& key, double defaultValue = 0.0) const override
    {
        const auto it = m_values.find(key);
        return it == m_values.end() ? defaultValue : std::stod(it->second);
    }
    bool readBool(const std::string& key, bool defaultValue) const override
    {
        const auto it = m_values.find(key);
        return it == m_values.end() ? defaultValue : it->second == "true";
    }

private:
    std::map<std::string, std::string> m_values;
};

constexpr int ALARM_PIN = 17;

} // end anonymous namespace

TEST_CASE("Stepper: driver alarm stops the motor until cleared")
{
    mgo::MockGpio gpio(false);
    mgo::StepperMotor motor(gpio, 0, 0, 0, 1'000, 0.01, 1'000.0);
    motor.setAlarmPin(ALARM_PIN, mgo::PinState::low);
    motor.setRpm(100.0);
    motor.goToStep(1'000'000);
    REQUIRE(waitFor([&] { return motor.getCurrentStep() > 10; }));
    REQUIRE(!motor.isFaulted());

    // The driver goes into alarm (e.g. the motor has stalled)
    gpio.setInputPinLevel(ALARM_PIN, mgo::PinState::low);
    REQUIRE(waitFor([&] { return motor.isFaulted(); }));
    REQUIRE(waitFor([&] { return !motor.isRunning(); }));
    const long stoppedAt = motor.getCurrentStep();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    REQUIRE(motor.getCurrentStep() == stoppedAt);

    // While faulted, the motor refuses to move
    motor.goToStep(0);
    REQUIRE(!motor.isRunning());
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    REQUIRE(motor.getCurrentStep() == stoppedAt);

    // The fault can't be cleared while the driver is still in alarm
    REQUIRE(!motor.clearFault());
    REQUIRE(motor.isFaulted());

    // Once the driver's alarm is cleared, so can the fault be, and it moves again
    gpio.setInputPinLevel(ALARM_PIN, mgo::PinState::high);
    REQUIRE(motor.clearFault());
    REQUIRE(!motor.isFaulted());
    motor.goToStep(stoppedAt + 10);
    motor.wait();
    REQUIRE(motor.getCurrentStep() == stoppedAt + 10);
}

TEST_CASE("Stepper: driver alarm is detected while idle")
{
    mgo::MockGpio gpio(false);
    mgo::StepperMotor motor(gpio, 0, 0, 0, 1'000, 0.01, 1'000.0);
    motor.setAlarmPin(ALARM_PIN, mgo::PinState::high); // (active high, this time)
    gpio.setInputPinLevel(ALARM_PIN, mgo::PinState::low);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    REQUIRE(!motor.isFaulted());
    gpio.setInputPinLevel(ALARM_PIN, mgo::PinState::high);
    REQUIRE(waitFor([&] { return motor.isFaulted(); }));
}

TEST_CASE("Stepper: no alarm pin means no faults")
{
    mgo::MockGpio gpio(false);
    mgo::StepperMotor motor(gpio, 0, 0, 0, 1'000, 0.01, 1'000.0);
    // Even if a pin reads as it would in alarm, it isn't being monitored
    gpio.setInputPinLevel(ALARM_PIN, mgo::PinState::low);
    motor.setRpm(100.0);
    motor.goToStep(20);
    motor.wait();
    REQUIRE(motor.getCurrentStep() == 20);
    REQUIRE(!motor.isFaulted());
}

TEST_CASE("Model:   motor fault stops all axes, and re-zeroing clears it")
{
    TestConfigReader config({ { "Axis1AlarmPin", "17" }, { "Axis1AlarmActiveLevel", "low" } });
    mgo::MockGpio gpio(false);
    mgo::Model model(gpio, config);
    model.initialise();
    model.setAxis1MotorSpeed(100.0);
    model.setAxis2MotorSpeed(100.0);
    model.axis1GoToPosition(50.0);
    model.axis2GoToPosition(50.0);
    REQUIRE(waitFor([&] { return model.isAxis1MotorRunning() && model.isAxis2MotorRunning(); }));
    model.checkStatus();
    REQUIRE(model.getFaultMessage().empty());

    // Axis 1's driver goes into alarm: it stops by itself...
    gpio.setInputPinLevel(ALARM_PIN, mgo::PinState::low);
    REQUIRE(waitFor([&] { return !model.isAxis1MotorRunning(); }));
    // ...and the model then stops the other axis, and reports the fault
    model.checkStatus();
    REQUIRE(!model.isAxis2MotorRunning());
    REQUIRE(model.getFaultMessage().find("Z MOTOR FAULT") != std::string::npos);

    // Axis 1 won't move while faulted
    const double position = model.getAxis1MotorPosition();
    model.axis1GoToPosition(0.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    REQUIRE(model.getAxis1MotorPosition() == position);

    // Re-zeroing while the driver is still in alarm doesn't clear the fault
    model.axis1Zero();
    REQUIRE(!model.getFaultMessage().empty());
    REQUIRE(model.getWarning().find("still in alarm") != std::string::npos);

    // Once the driver's alarm is cleared, re-zeroing clears the fault
    gpio.setInputPinLevel(ALARM_PIN, mgo::PinState::high);
    model.axis1Zero();
    REQUIRE(model.getFaultMessage().empty());
    REQUIRE(model.getWarning().empty());
    model.axis1GoToPosition(0.5);
    model.axis1Wait();
    REQUIRE(model.getAxis1MotorPosition() == Approx(0.5));
}

TEST_CASE("Commands: shortcut text")
{
    mgo::MockConfigReader lathe; // defaults: leader keys z and x
    REQUIRE(mgo::shortcutText(mgo::key::a1_g, lathe) == "z g");
    REQUIRE(mgo::shortcutText(mgo::key::a2_s, lathe) == "x s");
    REQUIRE(mgo::shortcutText(mgo::key::aAll_z, lathe) == "\\ z");
    REQUIRE(mgo::shortcutText(mgo::key::a1_ENTER, lathe) == "z Enter");
    REQUIRE(mgo::shortcutText(mgo::key::AltW, lathe) == "Alt+w");
    REQUIRE(mgo::shortcutText(mgo::key::CtrlQ, lathe) == "Ctrl+q");
    REQUIRE(mgo::shortcutText(mgo::key::A, lathe) == "Shift+a");
    REQUIRE(mgo::shortcutText(mgo::key::f2t, lathe).empty()); // menu only
    REQUIRE(mgo::shortcutText(mgo::key::LBRACKET, lathe) == "[");
    REQUIRE(mgo::shortcutText(mgo::key::SIX, lathe) == "6");
    REQUIRE(mgo::shortcutText(mgo::key::F1, lathe) == "F1");

    // The mill's config uses x and c as its axis leader keys
    TestConfigReader mill({ { "Axis1Leader", "120" }, { "Axis2Leader", "99" } });
    REQUIRE(mgo::shortcutText(mgo::key::a1_g, mill) == "x g");
    REQUIRE(mgo::shortcutText(mgo::key::a2_g, mill) == "c g");
}

TEST_CASE("Commands: each command appears once")
{
    mgo::MockConfigReader config;
    const auto commands = mgo::buildCommands(config);
    REQUIRE(commands.size() > 50);
    std::set<int> keys;
    for (const auto& c : commands) {
        REQUIRE(keys.insert(c.key).second); // no key listed twice
        REQUIRE(!c.description.empty());
        REQUIRE(!c.category.empty());
    }
}

TEST_CASE("Commands: commands for a disabled axis are left out")
{
    // e.g. the rotary table config, which only has axis 1
    TestConfigReader config({ { "DisableAxis2", "true" }, { "Axis1Label", "C" } });
    const auto commands = mgo::buildCommands(config);
    for (const auto& c : commands) {
        REQUIRE((c.key & 0x2000) == 0); // no axis 2 leader commands
        REQUIRE(c.key != mgo::key::f2t); // modes need axis 2
        REQUIRE(c.key != mgo::key::UP);
    }
    // But axis 1's commands are there, using its name
    const auto zero = std::find_if(commands.begin(), commands.end(), [](const mgo::Command& c) {
        return c.key == mgo::key::a1_z;
    });
    REQUIRE(zero != commands.end());
    REQUIRE(zero->description == "Zero C");
}

TEST_CASE("Menu: navigation, choosing, and keys passed through")
{
    mgo::MockConfigReader config;
    mgo::MockGpio gpio(false);
    mgo::Model model(gpio, config);
    model.initialise();
    std::vector<mgo::Command> commands {
        { "First", "one", mgo::key::a1_z },
        { "First", "two", mgo::key::a2_z },
        { "Second", "three", mgo::key::LEFT,
          [](const mgo::Model& m) { return !m.isAxisLocked(1); } },
    };
    mgo::Menu menu(commands);
    using Type = mgo::Menu::Action::Type;
    REQUIRE(menu.categories() == std::vector<std::string> { "First", "Second" });

    menu.open();
    REQUIRE(menu.isOpen());
    REQUIRE(!menu.openCategory());
    // Into the first category, down to "two", and choose it
    REQUIRE(menu.handleKey(mgo::key::ENTER, model).type == Type::None);
    REQUIRE(menu.openCategory() == 0u);
    REQUIRE(menu.handleKey(mgo::key::DOWN, model).type == Type::None);
    REQUIRE(menu.selection() == 1);
    REQUIRE(menu.handleKey(mgo::key::DOWN, model).type == Type::None); // already at the end
    REQUIRE(menu.selection() == 1);
    const auto chosen = menu.handleKey(mgo::key::ENTER, model);
    REQUIRE(chosen.type == Type::Command);
    REQUIRE(chosen.key == mgo::key::a2_z);
    REQUIRE(!menu.isOpen()); // choosing closes the menu

    // The gamepad's D-pad keys (w/a/s/d) navigate too; Left goes back
    menu.open();
    menu.handleKey(mgo::key::s, model, true);
    menu.handleKey(mgo::key::d, model, true);
    REQUIRE(menu.openCategory() == 1u);
    menu.handleKey(mgo::key::a, model, true);
    REQUIRE(!menu.openCategory());
    REQUIRE(menu.selection() == 1); // back on the category we came from

    // Space (stop all motors), and other keys, carry on working as normal,
    // and the menu stays open
    auto action = menu.handleKey(mgo::key::SPACE, model);
    REQUIRE(action.type == Type::PassThrough);
    REQUIRE(action.key == mgo::key::SPACE);
    action = menu.handleKey(mgo::key::AltLeft, model);
    REQUIRE(action.type == Type::PassThrough);
    REQUIRE(menu.isOpen());

    // A command that isn't available can't be chosen
    model.lockAxis(1);
    menu.handleKey(mgo::key::ENTER, model);
    REQUIRE(menu.handleKey(mgo::key::ENTER, model).type == Type::None);
    REQUIRE(menu.isOpen());
    model.unlockAxis(1);
    REQUIRE(menu.handleKey(mgo::key::ENTER, model).key == mgo::key::LEFT);

    // Esc from the top level, or Tab from anywhere, closes it
    menu.open();
    menu.handleKey(mgo::key::ESC, model);
    REQUIRE(!menu.isOpen());
    menu.open();
    menu.handleKey(mgo::key::ENTER, model);
    menu.handleKey(mgo::key::TAB, model);
    REQUIRE(!menu.isOpen());
}

TEST_CASE("Commands: keys allowed in the current mode")
{
    mgo::MockConfigReader config;
    mgo::MockGpio gpio(false);
    mgo::Model model(gpio, config);
    model.initialise();
    REQUIRE(mgo::keyAllowedInCurrentMode(mgo::key::a1_g, model));
    // In Setup mode, only motion and stop keys (and Esc etc) are allowed
    model.changeMode(mgo::Mode::Setup);
    REQUIRE(!mgo::keyAllowedInCurrentMode(mgo::key::a1_g, model));
    REQUIRE(mgo::keyAllowedInCurrentMode(mgo::key::LEFT, model));
    REQUIRE(mgo::keyAllowedInCurrentMode(mgo::key::ESC, model));
    model.changeMode(mgo::Mode::None);
    // No X motion while X is retracted
    model.setIsAxis2Retracted(true);
    REQUIRE(!mgo::keyAllowedInCurrentMode(mgo::key::UP, model));
    REQUIRE(mgo::keyAllowedInCurrentMode(mgo::key::LEFT, model));
}

TEST_CASE("Joystick: menu chord")
{
    using namespace std::chrono_literals;
    const auto t0 = mgo::JoystickKeyMapper::Clock::now();
    mgo::JoystickKeyMapper m;
    m.setMenuChord(4, 6); // previous + next memory slot

    // Both pressed together toggles the menu, and neither does its own thing
    REQUIRE(m.process({ button(4, true) }, t0).empty());
    REQUIRE(m.process({ button(6, true) }, t0 + 50ms) == Keys { mgo::key::TAB });
    REQUIRE(m.process({ button(4, false), button(6, false) }, t0 + 300ms).empty());
    REQUIRE(m.process({}, t0 + 600ms).empty());

    // Either order works
    REQUIRE(m.process({ button(6, true) }, t0 + 1s).empty());
    REQUIRE(m.process({ button(4, true) }, t0 + 1s + 20ms) == Keys { mgo::key::TAB });
    m.process({ button(4, false), button(6, false) }, t0 + 1500ms);

    // A quick tap of one acts as soon as it's released
    REQUIRE(m.process({ button(4, true) }, t0 + 2s).empty());
    REQUIRE(m.process({ button(4, false) }, t0 + 2s + 80ms) == Keys { mgo::key::LBRACKET });
    REQUIRE(m.process({}, t0 + 3s).empty()); // and only once

    // Held down without the other, it acts once the chord window has passed
    REQUIRE(m.process({ button(6, true) }, t0 + 4s).empty());
    REQUIRE(m.process({}, t0 + 4s + 100ms).empty());
    REQUIRE(m.process({}, t0 + 4s + 150ms) == Keys { mgo::key::RBRACKET });
    // ...and the other pressed after that is just itself, not the chord
    REQUIRE(m.process({ button(4, true) }, t0 + 4s + 400ms).empty());
    REQUIRE(m.process({ button(4, false) }, t0 + 4s + 450ms) == Keys { mgo::key::LBRACKET });
    REQUIRE(m.process({ button(6, false) }, t0 + 5s).empty());

    // Other buttons are unaffected, including while a chord button is held back
    REQUIRE(m.process({ button(4, true), button(5, true) }, t0 + 6s) == Keys { mgo::key::SPACE });
    REQUIRE(m.process({ button(4, false), button(5, false) }, t0 + 6s + 50ms)
            == Keys { mgo::key::LBRACKET });
}

TEST_CASE("Joystick: without a menu chord, buttons act at once")
{
    mgo::JoystickKeyMapper m;
    REQUIRE(m.process({ button(4, true) }) == Keys { mgo::key::LBRACKET });
    REQUIRE(m.process({ button(6, true) }) == Keys { mgo::key::RBRACKET });
}

TEST_CASE("Menu: hotkeys")
{
    mgo::MockConfigReader config;
    mgo::MockGpio gpio(false);
    mgo::Model model(gpio, config);
    model.initialise();
    mgo::Menu menu(mgo::buildCommands(config));
    using Type = mgo::Menu::Action::Type;

    // M (Modes), then T (Threading)
    menu.open();
    REQUIRE(menu.handleKey(mgo::key::m, model).type == Type::None);
    REQUIRE(menu.categories().at(*menu.openCategory()) == "Modes");
    auto action = menu.handleKey(mgo::key::t, model);
    REQUIRE(action.type == Type::Command);
    REQUIRE(action.key == mgo::key::f2t);
    REQUIRE(!menu.isOpen());

    // Upper case works too
    menu.open();
    menu.handleKey(mgo::key::Y, model);
    REQUIRE(menu.handleKey(mgo::key::Q, model).key == mgo::key::CtrlQ);

    // A letter which isn't a hotkey does nothing while the menu is open (it
    // doesn't act as its usual shortcut)
    menu.open();
    REQUIRE(menu.handleKey(mgo::key::z, model).type == Type::None);
    REQUIRE(!menu.openCategory());
    REQUIRE(menu.isOpen());

    // Axis locks' hotkeys are the axes' names
    menu.handleKey(mgo::key::l, model);
    REQUIRE(menu.handleKey(mgo::key::x, model).key == mgo::key::a2_k);

    // Letters from the gamepad are never hotkeys: its D-pad (w/a/s/d)
    // navigates, and its buttons (e.g. m: store memory) work as normal
    menu.open();
    action = menu.handleKey(mgo::key::m, model, true);
    REQUIRE(action.type == Type::PassThrough);
    REQUIRE(action.key == mgo::key::m);
    REQUIRE(!menu.openCategory());
    menu.handleKey(mgo::key::s, model, true);
    REQUIRE(menu.selection() == 1);

    // Ctrl and Alt + letter aren't hotkeys either (e.g. Ctrl+Q still quits)
    REQUIRE(menu.handleKey(mgo::key::CtrlQ, model).type == Type::PassThrough);
    REQUIRE(menu.handleKey(mgo::key::AltW, model).type == Type::PassThrough);

    // A hotkey for a command which isn't available selects it, but doesn't
    // choose it
    model.lockAxis(1);
    menu.handleKey(mgo::key::ESC, model);
    menu.open();
    menu.handleKey(mgo::key::m, model);
    model.changeMode(mgo::Mode::Setup); // only motion allowed
    REQUIRE(menu.handleKey(mgo::key::t, model).type == Type::None);
    REQUIRE(menu.isOpen());
    REQUIRE(menu.selection() == 0);
}

TEST_CASE("Commands: menu hotkeys are unique within each list")
{
    TestConfigReader mill({ { "Axis1Label", "X" }, { "Axis2Label", "C" } });
    mgo::MockConfigReader lathe;
    for (const mgo::IConfigReader* config :
         std::vector<const mgo::IConfigReader*> { &lathe, &mill }) {
        const auto commands = mgo::buildCommands(*config);
        std::map<std::string, std::set<char>> used;
        std::set<char> categoryKeys;
        for (const auto& c : commands) {
            if (c.hotkey) {
                REQUIRE(used[c.category].insert(c.hotkey).second);
            }
            const char ck = mgo::categoryHotkey(c.category);
            REQUIRE(ck != 0);
            categoryKeys.insert(ck);
        }
        // Each category has its own
        std::set<std::string> categories;
        for (const auto& c : commands) {
            categories.insert(c.category);
        }
        REQUIRE(categoryKeys.size() == categories.size());
    }
}

TEST_CASE("Commands: hints after a leader key")
{
    mgo::MockConfigReader config;
    const auto commands = mgo::buildCommands(config);
    const auto keysFor = [&commands](mgo::KeyMode mode) {
        std::set<int> keys;
        for (const mgo::Command* c : mgo::commandsForLeader(commands, mode)) {
            keys.insert(c->key);
        }
        return keys;
    };

    const auto axis1 = keysFor(mgo::KeyMode::Axis1);
    REQUIRE(axis1.contains(mgo::key::a1_g));
    REQUIRE(axis1.contains(mgo::key::a1_z));
    REQUIRE(!axis1.contains(mgo::key::a2_g));
    // The menu-only codes look as if they have the axis 1 leader's bit set
    REQUIRE(!axis1.contains(mgo::key::f2t));

    const auto axis2 = keysFor(mgo::KeyMode::Axis2);
    REQUIRE(axis2.contains(mgo::key::a2_s));
    REQUIRE(!axis2.contains(mgo::key::a1_s));

    const auto both = keysFor(mgo::KeyMode::AxisAll);
    REQUIRE(both.contains(mgo::key::aAll_z));
    REQUIRE(!both.contains(mgo::key::a1_z));

    REQUIRE(keysFor(mgo::KeyMode::None).empty());

    // The key to press after the leader
    REQUIRE(mgo::followUpKeyName(mgo::key::a1_g) == "g");
    REQUIRE(mgo::followUpKeyName(mgo::key::a2_ENTER) == "Enter");
    REQUIRE(mgo::followUpKeyName(mgo::key::aAll_DELETE) == "Delete");
    REQUIRE(mgo::followUpKeyName(mgo::key::a1_FULLSTOP) == ".");
}

TEST_CASE("Commands: shut down is shown, but unavailable, in mock builds")
{
    mgo::MockConfigReader config;
    mgo::MockGpio gpio(false);
    mgo::Model model(gpio, config);
    const auto commands = mgo::buildCommands(config);
    const auto shutdown = std::find_if(commands.begin(), commands.end(), [](const mgo::Command& c) {
        return c.key == mgo::key::ASTERISK;
    });
    REQUIRE(shutdown != commands.end());
    REQUIRE(shutdown->category == "System");
    // (The tests are always built with FAKE)
    REQUIRE(!mgo::isAvailable(*shutdown, model));
}

TEST_CASE("Model:   diameter, for the surface speed")
{
    mgo::MockConfigReader config;
    mgo::MockGpio gpio(false);
    mgo::Model model(gpio, config);
    model.initialise();
    // Not known until the X position has been set as a diameter
    REQUIRE(!model.getDiameterMm().has_value());
    model.setAxis2Position(12.7); // as setting X to a diameter of 25.4 does
    model.diameterIsSet();
    REQUIRE(model.getDiameterMm() == Approx(25.4).margin(0.001));
}

TEST_CASE("Model:   display units")
{
    // Axis 2 in degrees, as on the rotary table setups
    TestConfigReader config({ { "Axis2DisplayUnits", "deg" } });
    mgo::MockGpio gpio(false);
    mgo::Model model(gpio, config);
    model.initialise();
    REQUIRE(model.getDisplayUnits() == mgo::DisplayUnits::Metric);
    REQUIRE(model.axisUnitName(1) == "mm");
    REQUIRE(model.toDisplayUnits(1, 25.4) == 25.4);

    model.toggleDisplayUnits();
    REQUIRE(model.getDisplayUnits() == mgo::DisplayUnits::Imperial);
    // Lengths are converted...
    REQUIRE(model.isLengthAxis(1));
    REQUIRE(model.axisUnitName(1) == "in");
    REQUIRE(model.toDisplayUnits(1, 25.4) == Approx(1.0));
    REQUIRE(model.fromDisplayUnits(1, 0.5) == Approx(12.7));
    REQUIRE(model.axisDecimals(1) == 4);
    // (Axis 1's conversion factor is -1/1000 by default, so -25400 steps is 25.4mm)
    REQUIRE(model.formatAxis1Position(-25'400) == " 1.0000");
    // ...but an axis in degrees isn't
    REQUIRE(!model.isLengthAxis(2));
    REQUIRE(model.axisUnitName(2) == "deg");
    REQUIRE(model.toDisplayUnits(2, 90.0) == 90.0);
    REQUIRE(model.fromDisplayUnits(2, 90.0) == 90.0);

    model.toggleDisplayUnits();
    REQUIRE(model.getDisplayUnits() == mgo::DisplayUnits::Metric);
}

TEST_CASE("Commands: wording follows the display units")
{
    mgo::MockConfigReader config;
    const auto find = [](const std::vector<mgo::Command>& commands, int key) {
        return std::find_if(commands.begin(), commands.end(), [key](const mgo::Command& c) {
            return c.key == key;
        });
    };
    const auto metric = mgo::buildCommands(config, mgo::DisplayUnits::Metric);
    REQUIRE(find(metric, mgo::key::a)->description == "Z left 0.05mm");
    REQUIRE(find(metric, mgo::key::u)->description == "Show inches");
    const auto imperial = mgo::buildCommands(config, mgo::DisplayUnits::Imperial);
    REQUIRE(find(imperial, mgo::key::a)->description == "Z left 0.0020in");
    REQUIRE(find(imperial, mgo::key::ONE)->description == "Z speed preset 1: 0.787 in/min");
    REQUIRE(find(imperial, mgo::key::u)->description == "Show mm");
}

TEST_CASE("Model:   zeroing or setting X clears the diameter")
{
    mgo::MockConfigReader config;
    mgo::MockGpio gpio(false);
    mgo::Model model(gpio, config);
    model.initialise();
    const auto setDiameter = [&model] {
        model.setAxis2Position(12.7);
        model.diameterIsSet();
        model.checkStatus();
        REQUIRE(model.getDiameterMm().has_value());
        REQUIRE(model.getGeneralStatus().find("Diameter") != std::string::npos);
    };

    setDiameter();
    model.axis2Zero();
    REQUIRE(!model.getDiameterMm().has_value());
    REQUIRE(model.getGeneralStatus() == mgo::DEFAULT_GENERAL_STATUS);

    setDiameter();
    model.setAxis2Position(3.0); // (not as a diameter)
    REQUIRE(!model.getDiameterMm().has_value());

    // Moving X doesn't affect it
    setDiameter();
    model.setAxis2MotorSpeed(100.0);
    model.axis2GoToPosition(10.0);
    model.axis2Wait();
    REQUIRE(model.getDiameterMm() == Approx(20.0).margin(0.01));
}

TEST_CASE("Model:   the diameter doesn't change as X retracts and unretracts")
{
    mgo::MockConfigReader config;
    mgo::MockGpio gpio(false);
    mgo::Model model(gpio, config);
    model.initialise();
    model.setAxis2Position(10.0);
    model.diameterIsSet();
    model.checkStatus();
    const std::string status = model.getGeneralStatus();
    REQUIRE(model.getDiameterMm() == Approx(20.0).margin(0.01));

    // Throughout the retract and the unretract
    bool unchanged = true;
    const auto check = [&] {
        unchanged = unchanged && std::abs(*model.getDiameterMm() - 20.0) < 0.01
            && model.getGeneralStatus() == status;
    };
    const auto runAndCheck = [&] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        do {
            REQUIRE(std::chrono::steady_clock::now() < deadline);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            model.checkStatus();
            check();
        } while (model.isAxis2MotorRunning());
        model.checkStatus();
        check();
    };
    model.axis2Retract();
    REQUIRE(model.getIsAxis2Retracted());
    runAndCheck();
    REQUIRE(std::abs(model.getAxis2MotorPosition() - 10.0) > 1.0); // (it did retract)
    model.axis2Retract(); // (unretract)
    runAndCheck();
    REQUIRE(!model.getIsAxis2Retracted());
    REQUIRE(model.getAxis2MotorPosition() == Approx(10.0).margin(0.01));
    REQUIRE(unchanged);
}

TEST_CASE("Threading: auto-retract at the end of a pass")
{
    using mgo::ThreadingStopAction;
    using mgo::threadingStopAction;
    // A cut (tool in) reaching its end retracts; a return (tool out) reaching
    // its end brings the tool back in
    REQUIRE(threadingStopAction(true, false, false, true) == ThreadingStopAction::RetractOut);
    REQUIRE(threadingStopAction(true, true, true, true) == ThreadingStopAction::RetractIn);
    // Stopped short (e.g. the spindle sped up too much): retract a tool that's
    // in, but never bring one in
    REQUIRE(threadingStopAction(false, false, false, true) == ThreadingStopAction::RetractOut);
    REQUIRE(threadingStopAction(false, true, true, true) == ThreadingStopAction::None);
    // ...and don't move X at all if the spindle has stopped
    REQUIRE(threadingStopAction(false, false, false, false) == ThreadingStopAction::None);
    // If the user retracted or unretracted X during the move, leave it alone
    REQUIRE(threadingStopAction(true, false, true, true) == ThreadingStopAction::None);
    REQUIRE(threadingStopAction(true, true, false, true) == ThreadingStopAction::None);
}

TEST_CASE("Encoder: Lost pulses are detected")
{
    FakeTickGpio gpio(0);
    mgo::RotaryEncoder re(gpio, RE_PIN_A, RE_PIN_B, 4, 1.f);
    uint32_t tick = 100'000;
    const auto feed = [&](uint32_t gap) {
        tick += gap;
        feedEncoderPulse(re, tick);
    };
    // A few revolutions at a steady speed, so the speed is known
    for (int n = 0; n < 12; ++n) {
        feed(20'000);
    }
    REQUIRE(re.getLostPulseEvents() == 0);

    // A gap, then pulses at the same rate as before: pulses were lost
    feed(300'000);
    REQUIRE(re.getLostPulseEvents() == 0); // (can't tell until the next pulse)
    feed(20'000);
    REQUIRE(re.getLostPulseEvents() == 1);
    REQUIRE(re.getLastLostPulseGapMicroseconds() == 300'000);
    for (int n = 0; n < 12; ++n) {
        feed(20'000);
    }

    // A gap, then slow pulses: the spindle really is slowing (or stopping)
    feed(300'000);
    feed(200'000);
    REQUIRE(re.getLostPulseEvents() == 1);
    for (int n = 0; n < 12; ++n) {
        feed(20'000);
    }

    // A long gap is the spindle being stopped and started again
    feed(3'000'000);
    feed(20'000);
    REQUIRE(re.getLostPulseEvents() == 1);
}

TEST_CASE("Stepper: Spinning when idle")
{
    mgo::MockGpio gpio(false);
    mgo::StepperMotor motor(gpio, 0, 0, 0, 1'000, 1.0, 10'000.0);
    motor.setSpinWhenIdle(true);
    motor.setRpm(500.0);
    motor.goToStep(50);
    motor.wait();
    REQUIRE(motor.getCurrentStep() == 50);
    motor.goToStep(1'000'000);
    gpio.delayMicroSeconds(1'000);
    REQUIRE(motor.isRunning());
    motor.stop();
    REQUIRE(!motor.isRunning());
    // Back to sleeping when idle, it still moves
    motor.setSpinWhenIdle(false);
    const long step = motor.getCurrentStep();
    motor.goToStep(step + 20);
    motor.wait();
    REQUIRE(motor.getCurrentStep() == step + 20);
}

TEST_CASE("Sequence: runs each step once the one before is done")
{
    std::vector<std::string> log;
    bool firstDone = false;
    mgo::MotionSequence seq;
    seq.start(
        { { [&] { log.push_back("first"); }, [&] { return firstDone; } },
          { [&] { log.push_back("second"); }, {} } },
        [&] { log.push_back("end"); });
    // The first step's action runs at once, then we wait for it
    REQUIRE(log == std::vector<std::string> { "first" });
    REQUIRE(seq.isRunning());
    REQUIRE(seq.advance());
    REQUIRE(log == std::vector<std::string> { "first" });
    // Once it's done, the second (which is done at once) runs, and that's the end
    firstDone = true;
    REQUIRE(!seq.advance());
    REQUIRE(!seq.isRunning());
    REQUIRE(log == std::vector<std::string> { "first", "second", "end" });
    // (Nothing more happens)
    REQUIRE(!seq.advance());
    REQUIRE(log.size() == 3);
}

TEST_CASE("Sequence: steps which are done at once all run in start()")
{
    int actions = 0;
    int ends = 0;
    mgo::MotionSequence seq;
    seq.start({ { [&] { ++actions; }, [] { return true; } }, { [&] { ++actions; }, {} } },
              [&] { ++ends; });
    REQUIRE(actions == 2);
    REQUIRE(ends == 1);
    REQUIRE(!seq.isRunning());

    // An empty sequence just ends
    seq.start({}, [&] { ++ends; });
    REQUIRE(ends == 2);
    REQUIRE(!seq.isRunning());
}

TEST_CASE("Sequence: cancelling")
{
    int secondRan = 0;
    int ends = 0;
    mgo::MotionSequence seq;
    seq.start({ { [] {}, [] { return false; } }, { [&] { ++secondRan; }, {} } }, [&] { ++ends; });
    seq.cancel();
    REQUIRE(!seq.isRunning());
    REQUIRE(ends == 1); // (onEnd is called when cancelled, too)
    REQUIRE(!seq.advance());
    REQUIRE(secondRan == 0);
    seq.cancel(); // (does nothing now)
    REQUIRE(ends == 1);

    // Starting another sequence cancels the one running
    int firstEnds = 0;
    seq.start({ { [] {}, [] { return false; } } }, [&] { ++firstEnds; });
    seq.start({ { [] {}, [] { return false; } } }, [&] { ++ends; });
    REQUIRE(firstEnds == 1);
    REQUIRE(seq.isRunning());

    // A step's action can cancel its own sequence (e.g. by stopping all motors)
    bool stepDone = false;
    seq.start({ { [] {}, [&] { return stepDone; } }, { [&] { seq.cancel(); }, {} },
                { [&] { ++secondRan; }, {} } });
    stepDone = true;
    REQUIRE(!seq.advance());
    REQUIRE(secondRan == 0);
}

TEST_CASE("Model:   a nudge doesn't block, and restores the speed when done")
{
    mgo::MockConfigReader config;
    mgo::MockGpio gpio(false);
    mgo::Model model(gpio, config);
    model.initialise();
    model.axis1SetSpeed(40.0);
    // (At the nudge speed, 1mm takes over half a second)
    model.axis1Nudge(mgo::ZDirection::Left, 1.0);
    REQUIRE(model.isMotionSequenceRunning());
    REQUIRE(model.isAxis1MotorRunning());
    REQUIRE(runUntilStopped(model));
    REQUIRE(std::abs(model.getAxis1MotorPosition()) == Approx(1.0));
    REQUIRE(model.getAxis1MotorSpeed() == Approx(40.0));

    // Stopping all motors part way cancels it, and still restores the speed
    model.axis2SetSpeed(20.0);
    model.axis2Nudge(mgo::XDirection::Inwards, 1.0);
    REQUIRE(model.isMotionSequenceRunning());
    REQUIRE(waitFor([&] { return std::abs(model.getAxis2MotorPosition()) > 0.01; }));
    model.stopAllMotors();
    REQUIRE(!model.isMotionSequenceRunning());
    REQUIRE(!model.isAxis2MotorRunning());
    REQUIRE(std::abs(model.getAxis2MotorPosition()) < 1.0);
    REQUIRE(model.getAxis2MotorSpeed() == Approx(20.0));
}

TEST_CASE("Model:   a taper's backlash take-up doesn't block")
{
    // With backlash compensation, reversing Z first takes up the slack (here
    // 200 steps, at 10mm/min: over a second)
    TestConfigReader config({ { "Axis1BacklashCompensationSteps", "200" },
                              { "Axis2BacklashCompensationSteps", "200" } });
    mgo::MockGpio gpio(false);
    mgo::Model model(gpio, config);
    model.initialise();
    model.changeMode(mgo::Mode::Taper);
    model.setTaperAngle(45.0);
    model.axis1SetSpeed(10.0);
    const auto start = std::chrono::steady_clock::now();
    model.axis1GoToPosition(0.05);
    REQUIRE(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(100));
    REQUIRE(model.isMotionSequenceRunning());
    // Updates meanwhile don't take the take-up's end for the end of the cut
    REQUIRE(runUntilStopped(model));
    REQUIRE(model.getAxis1MotorPosition() == Approx(0.05));
    REQUIRE(std::abs(model.getAxis2MotorPosition()) == Approx(0.05).margin(0.002));
}

namespace {

// A machine for the multi-pass engine whose moves finish when the test says,
// and which logs them
class FakeMultiPassMachine final : public mgo::IMultiPassMachine {
public:
    double cutPosition() const override { return m_cut; }
    bool cutMoving() const override { return m_cutTarget.has_value(); }
    void cutMoveTo(double position, double feedFollows) override
    {
        log.push_back(fmt::format("cut {:.6g} at {:.6g}", position, m_cutSpeed));
        if (feedFollows != 0.0) {
            log.back() += fmt::format(" following {:.6g}", feedFollows);
        }
        m_cutTarget = position;
        m_cutFollows = feedFollows;
    }
    double cutSpeed() const override { return m_cutSpeed; }
    void setCutSpeed(double speed) override { m_cutSpeed = speed; }
    double cutMaxSpeed() const override { return 1'000.0; }
    std::string cutThreadTo(double position) override
    {
        if (!threadError.empty()) {
            log.push_back("thread refused");
            return threadError;
        }
        log.push_back(fmt::format("thread {:.6g}", position));
        m_cutTarget = position;
        m_cutFollows = 0.0;
        return {};
    }
    double feedPosition() const override { return m_feed; }
    bool feedMoving() const override { return m_feedTarget.has_value(); }
    void feedMoveTo(double position) override
    {
        log.push_back(fmt::format("feed {:.6g}", position));
        // (A motor stops on the nearest whole step)
        m_feedTarget = feedStep == 0.0 ? position : std::round(position / feedStep) * feedStep;
    }
    double feedStepSize() const override { return feedStep; }
    void retract() override
    {
        if (m_retracted) {
            log.push_back("unretract");
            m_feedTarget = m_feed - RETRACT;
            m_retracted = false;
        } else {
            log.push_back("retract");
            m_feedTarget = m_feed + RETRACT;
            m_retracted = true;
        }
    }
    bool isRetracted() const override { return m_retracted; }

    // Finishes any moves in progress (or, if given, stops the cut short there)
    void finishMoves(std::optional<double> cutStopsAt = std::nullopt)
    {
        if (m_cutTarget) {
            const double from = m_cut;
            m_cut = cutStopsAt.value_or(*m_cutTarget);
            m_cutTarget.reset();
            // (Retracting is relative here, so following while retracted
            // moves where it unretracts to, too)
            m_feed += m_cutFollows * (m_cut - from);
        }
        if (m_feedTarget) {
            m_feed = *m_feedTarget;
            m_feedTarget.reset();
        }
    }
    // Updates the multi-pass, finishing each move, until it's paused, finished
    // or stopped (or 100 updates, if it's repeating)
    void run(mgo::MultiPass& mp)
    {
        for (int n = 0; n < 100 && mp.inProgress() && mp.state() != mgo::MultiPass::State::Paused;
             ++n) {
            finishMoves();
            mp.update();
        }
    }

    static constexpr double RETRACT = 2.0;
    std::vector<std::string> log;
    double m_cut { 0.0 };
    double m_feed { 5.0 };
    double m_cutSpeed { 40.0 };
    bool m_retracted { false };
    // If set, cutThreadTo() refuses with this
    std::string threadError;
    // The feed axis's step size (0 to move anywhere)
    double feedStep { 0.0 };

private:
    std::optional<double> m_cutTarget;
    double m_cutFollows { 0.0 };
    std::optional<double> m_feedTarget;
};

using Log = std::vector<std::string>;

mgo::MultiPassPlan basicPlan()
{
    mgo::MultiPassPlan plan;
    plan.cutStart = 0.0;
    plan.cutEnd = -10.0;
    plan.feedStart = 5.0;
    plan.feedEnd = 4.75;
    plan.feedPerPass = 0.1;
    return plan;
}

} // end anonymous namespace

TEST_CASE("MultiPass: cuts, returns and steps over until the end")
{
    FakeMultiPassMachine machine;
    mgo::MultiPass mp(machine);
    REQUIRE(mp.start(basicPlan()).empty());
    // The first pass is a step in from the start (where the tool touched the
    // work)
    REQUIRE(mp.state() == mgo::MultiPass::State::Feeding);
    REQUIRE(mp.pass() == 1);
    // 0.25 in steps of 0.1: three passes, the last step smaller
    REQUIRE(mp.totalPasses() == 3);
    machine.run(mp);
    REQUIRE(mp.state() == mgo::MultiPass::State::Finished);
    REQUIRE(!mp.inProgress());
    REQUIRE(mp.pass() == 3);
    REQUIRE(mp.fed() == Approx(0.25));
    REQUIRE(mp.message().empty());
    // (Cuts at the cut speed; returns at full speed)
    REQUIRE(machine.log
            == Log { "feed 4.9", "cut -10 at 40", "cut 0 at 1000",
                     "feed 4.8", "cut -10 at 40", "cut 0 at 1000",
                     "feed 4.75", "cut -10 at 40", "cut 0 at 1000" });
    REQUIRE(machine.cutSpeed() == Approx(40.0));
    REQUIRE(machine.feedPosition() == Approx(4.75));

    // It can be started again (e.g. for another part), from the start
    machine.m_feed = 5.0;
    REQUIRE(mp.start(basicPlan()).empty());
}

TEST_CASE("MultiPass: a speed set during a cut is used for the next")
{
    FakeMultiPassMachine machine;
    mgo::MultiPass mp(machine);
    REQUIRE(mp.start(basicPlan()).empty());
    machine.finishMoves();
    mp.update();
    REQUIRE(machine.log.back() == "cut -10 at 40");
    machine.setCutSpeed(25.0);
    machine.run(mp);
    REQUIRE(machine.log.at(4) == "cut -10 at 25");
    REQUIRE(machine.cutSpeed() == Approx(25.0));
}

TEST_CASE("MultiPass: stepping over outwards, and in one pass")
{
    FakeMultiPassMachine machine;
    mgo::MultiPass mp(machine);
    auto plan = basicPlan();
    plan.feedEnd = 5.2; // (e.g. boring)
    REQUIRE(mp.start(plan).empty());
    REQUIRE(mp.totalPasses() == 2);
    machine.run(mp);
    REQUIRE(machine.log.at(0) == "feed 5.1");
    REQUIRE(machine.log.at(3) == "feed 5.2");
    REQUIRE(mp.state() == mgo::MultiPass::State::Finished);

    // If the feed's start and end are the same, it's a single pass, at the
    // start
    machine.log.clear();
    machine.m_feed = 5.0;
    plan.feedEnd = 5.0;
    REQUIRE(mp.start(plan).empty());
    REQUIRE(mp.totalPasses() == 1);
    machine.run(mp);
    REQUIRE(machine.log == Log { "cut -10 at 40", "cut 0 at 1000" });
}

TEST_CASE("MultiPass: retracting between passes")
{
    FakeMultiPassMachine machine;
    mgo::MultiPass mp(machine);
    auto plan = basicPlan();
    plan.feedEnd = 4.8;
    plan.retract = true;
    REQUIRE(mp.start(plan).empty());
    machine.run(mp);
    REQUIRE(mp.state() == mgo::MultiPass::State::Finished);
    // (Unretracted at the end, so the feed axis shows the final position)
    REQUIRE(machine.log
            == Log { "feed 4.9", "cut -10 at 40", "retract", "cut 0 at 1000", "unretract",
                     "feed 4.8", "cut -10 at 40", "retract", "cut 0 at 1000", "unretract" });
    REQUIRE(!machine.isRetracted());
    REQUIRE(machine.feedPosition() == Approx(4.8));
}

TEST_CASE("MultiPass: retracting only on the last passes")
{
    FakeMultiPassMachine machine;
    mgo::MultiPass mp(machine);
    auto plan = basicPlan();
    plan.feedEnd = 4.6; // (four passes)
    plan.retract = true;
    plan.retractLastPasses = 2;
    REQUIRE(mp.start(plan).empty());
    REQUIRE(mp.totalPasses() == 4);
    machine.run(mp);
    REQUIRE(mp.state() == mgo::MultiPass::State::Finished);
    // The roughing passes return without retracting; the last two retract
    REQUIRE(machine.log
            == Log { "feed 4.9", "cut -10 at 40", "cut 0 at 1000",
                     "feed 4.8", "cut -10 at 40", "cut 0 at 1000",
                     "feed 4.7", "cut -10 at 40", "retract", "cut 0 at 1000", "unretract",
                     "feed 4.6", "cut -10 at 40", "retract", "cut 0 at 1000", "unretract" });
    REQUIRE(!machine.isRetracted());
    REQUIRE(machine.feedPosition() == Approx(4.6));

    // Fewer passes than that: they all retract
    machine.log.clear();
    machine.m_feed = 5.0;
    plan.feedEnd = 4.9;
    plan.retractLastPasses = 3;
    REQUIRE(mp.start(plan).empty());
    machine.run(mp);
    REQUIRE(machine.log
            == Log { "feed 4.9", "cut -10 at 40", "retract", "cut 0 at 1000", "unretract" });

    // A nudge in while paused can make fewer passes: the count is worked out
    // again for each pass
    machine.log.clear();
    machine.m_feed = 5.0;
    plan.feedEnd = 4.6;
    plan.retractLastPasses = 2;
    plan.pause = true;
    REQUIRE(mp.start(plan).empty());
    machine.run(mp);
    REQUIRE(mp.state() == mgo::MultiPass::State::Paused);
    REQUIRE(machine.log == Log { "feed 4.9", "cut -10 at 40", "cut 0 at 1000" });
    machine.m_feed = 4.8; // (nudged in by 0.1: the next pass is at 4.7, of three)
    mp.continueAfterPause();
    REQUIRE(mp.totalPasses() == 3);
    machine.run(mp);
    REQUIRE(machine.log.at(3) == "feed 4.7");
    REQUIRE(machine.log.at(5) == "retract");

    // Repeating the same cut until stopped: there's no last pass, so every
    // pass retracts
    machine.log.clear();
    mp.stop("stopped");
    machine.finishMoves();
    if (machine.isRetracted()) {
        machine.retract();
        machine.finishMoves();
    }
    machine.m_cut = 0.0;
    machine.m_feed = 5.0;
    machine.log.clear();
    plan.feedPerPass = 0.0;
    plan.pause = false;
    REQUIRE(mp.start(plan).empty());
    machine.finishMoves();
    mp.update();
    REQUIRE(machine.log == Log { "cut -10 at 40", "retract" });
    mp.stop("stopped");

    // Not negative, nor when threading
    machine.finishMoves();
    machine.retract();
    machine.finishMoves();
    machine.m_cut = 0.0;
    plan = basicPlan();
    plan.retract = true;
    plan.retractLastPasses = -1;
    REQUIRE(!mp.start(plan).empty());
}

TEST_CASE("MultiPass: pausing between passes")
{
    FakeMultiPassMachine machine;
    mgo::MultiPass mp(machine);
    auto plan = basicPlan();
    plan.pause = true;
    REQUIRE(mp.start(plan).empty());
    machine.run(mp);
    REQUIRE(mp.state() == mgo::MultiPass::State::Paused);
    REQUIRE(mp.inProgress());
    REQUIRE(mp.nextFeed() == Approx(0.1));
    // Nothing happens until it's continued
    mp.update();
    REQUIRE(machine.log.size() == 3);
    mp.continueAfterPause();
    REQUIRE(mp.pass() == 2);
    REQUIRE(machine.log.back() == "feed 4.8");
    machine.run(mp);
    REQUIRE(mp.state() == mgo::MultiPass::State::Paused);
    REQUIRE(mp.nextFeed() == Approx(0.05)); // (the last step: smaller, to end at 4.75)
    mp.continueAfterPause();
    machine.run(mp);
    // (No pause after the last pass)
    REQUIRE(mp.state() == mgo::MultiPass::State::Finished);
}

TEST_CASE("MultiPass: asking for a pause before the next pass")
{
    FakeMultiPassMachine machine;
    mgo::MultiPass mp(machine);
    auto plan = basicPlan(); // (not pausing)
    plan.feedEnd = 4.7;
    // (Not before it's started)
    mp.togglePauseRequest();
    REQUIRE(!mp.pauseRequested());
    REQUIRE(mp.start(plan).empty());
    REQUIRE(mp.totalPasses() == 3);
    REQUIRE(mp.state() == mgo::MultiPass::State::Feeding);
    // Asked during the first pass, it pauses after it, once
    mp.togglePauseRequest();
    REQUIRE(mp.pauseRequested());
    machine.run(mp);
    REQUIRE(mp.state() == mgo::MultiPass::State::Paused);
    REQUIRE(mp.pass() == 1);
    REQUIRE(!mp.pauseRequested());
    // (Not while paused)
    mp.togglePauseRequest();
    REQUIRE(!mp.pauseRequested());
    mp.continueAfterPause();
    REQUIRE(mp.pass() == 2);
    // Asked and cancelled: no pause
    mp.togglePauseRequest();
    mp.togglePauseRequest();
    REQUIRE(!mp.pauseRequested());
    for (int n = 0; n < 100 && mp.pass() == 2; ++n) {
        machine.finishMoves();
        mp.update();
        REQUIRE(mp.state() != mgo::MultiPass::State::Paused);
    }
    REQUIRE(mp.pass() == 3);
    // Asked during the last pass: it just finishes
    mp.togglePauseRequest();
    REQUIRE(mp.pauseRequested());
    machine.run(mp);
    REQUIRE(mp.state() == mgo::MultiPass::State::Finished);
    REQUIRE(!mp.pauseRequested());
}

TEST_CASE("MultiPass: a pause request does nothing if pausing anyway")
{
    FakeMultiPassMachine machine;
    mgo::MultiPass mp(machine);
    auto plan = basicPlan();
    plan.pause = true;
    REQUIRE(mp.start(plan).empty());
    mp.togglePauseRequest();
    REQUIRE(!mp.pauseRequested());
}

TEST_CASE("MultiPass: correcting a taper, the feed axis follows each cut and back")
{
    FakeMultiPassMachine machine;
    mgo::MultiPass mp(machine);
    auto plan = basicPlan();
    plan.feedEnd = 4.8; // (two passes)
    plan.feedCorrection = 0.01;
    REQUIRE(mp.start(plan).empty());
    machine.run(mp);
    REQUIRE(mp.state() == mgo::MultiPass::State::Finished);
    REQUIRE(machine.log
            == Log { "feed 4.9", "cut -10 at 40 following 0.01", "cut 0 at 1000 following 0.01",
                     "feed 4.8", "cut -10 at 40 following 0.01", "cut 0 at 1000 following 0.01" });
    // (Back where the last cut started)
    REQUIRE(machine.feedPosition() == Approx(4.8));
}

TEST_CASE("MultiPass: correcting a taper, with a retract")
{
    FakeMultiPassMachine machine;
    mgo::MultiPass mp(machine);
    auto plan = basicPlan();
    plan.feedEnd = 4.8;
    plan.feedCorrection = -0.01;
    plan.retract = true;
    REQUIRE(mp.start(plan).empty());
    machine.run(mp);
    REQUIRE(mp.state() == mgo::MultiPass::State::Finished);
    REQUIRE(machine.log
            == Log { "feed 4.9", "cut -10 at 40 following -0.01", "retract",
                     "cut 0 at 1000 following -0.01", "unretract", "feed 4.8",
                     "cut -10 at 40 following -0.01", "retract", "cut 0 at 1000 following -0.01",
                     "unretract" });
    REQUIRE(machine.feedPosition() == Approx(4.8));
}

TEST_CASE("MultiPass: no taper correction with a manual feed")
{
    FakeMultiPassMachine machine;
    mgo::MultiPass mp(machine);
    auto plan = basicPlan();
    plan.feedManual = true;
    plan.feedCorrection = 0.01;
    REQUIRE(!mp.start(plan).empty());
}

TEST_CASE("MultiPass: a manual feed pauses for the user each pass")
{
    FakeMultiPassMachine machine;
    mgo::MultiPass mp(machine);
    auto plan = basicPlan();
    plan.feedManual = true;
    plan.feedStart = 0.0;
    plan.feedEnd = 0.25;
    machine.m_feed = 99.0; // (not used: there's no feed motor)
    REQUIRE(mp.start(plan).empty());
    // (Paused first, for the user to feed in for the first pass)
    REQUIRE(mp.state() == mgo::MultiPass::State::Paused);
    REQUIRE(mp.pass() == 0);
    REQUIRE(mp.totalPasses() == 3);
    for (const double feed : { 0.1, 0.1, 0.05 }) {
        machine.run(mp);
        REQUIRE(mp.state() == mgo::MultiPass::State::Paused);
        REQUIRE(mp.nextFeed() == Approx(feed));
        mp.continueAfterPause();
    }
    machine.run(mp);
    REQUIRE(mp.state() == mgo::MultiPass::State::Finished);
    REQUIRE(mp.fed() == Approx(0.25));
    for (const auto& entry : machine.log) {
        REQUIRE(entry.starts_with("cut"));
    }

    // (Retracting needs a motorised feed)
    plan.retract = true;
    REQUIRE(!mp.start(plan).empty());
}

TEST_CASE("MultiPass: zero step-over repeats the same cut until stopped")
{
    FakeMultiPassMachine machine;
    mgo::MultiPass mp(machine);
    auto plan = basicPlan();
    plan.feedPerPass = 0.0;
    REQUIRE(mp.start(plan).empty());
    REQUIRE(mp.totalPasses() == 0);
    machine.run(mp);
    REQUIRE(mp.inProgress());
    REQUIRE(mp.pass() > 10);
    for (const auto& entry : machine.log) {
        REQUIRE(entry.starts_with("cut"));
    }
    mp.stop("stopped");
    REQUIRE(mp.state() == mgo::MultiPass::State::Idle);
    REQUIRE(mp.message() == "stopped");
    // (Updates do nothing now)
    const auto moves = machine.log.size();
    machine.run(mp);
    REQUIRE(machine.log.size() == moves);
}

TEST_CASE("MultiPass: won't start unless ready")
{
    FakeMultiPassMachine machine;
    mgo::MultiPass mp(machine);
    auto plan = basicPlan();

    machine.m_cut = 1.0;
    REQUIRE(!mp.start(plan).empty()); // (not at the start)
    machine.m_cut = 0.0;
    machine.m_feed = 4.0;
    REQUIRE(!mp.start(plan).empty());
    machine.m_feed = 5.0;
    machine.m_retracted = true;
    REQUIRE(!mp.start(plan).empty());
    machine.m_retracted = false;
    plan.cutEnd = plan.cutStart;
    REQUIRE(!mp.start(plan).empty());
    plan = basicPlan();
    plan.feedPerPass = -0.1;
    REQUIRE(!mp.start(plan).empty());
    REQUIRE(mp.state() == mgo::MultiPass::State::Idle);
    REQUIRE(machine.log.empty());

    REQUIRE(mp.start(basicPlan()).empty());
    REQUIRE(!mp.start(basicPlan()).empty()); // (already running)
}

TEST_CASE("MultiPass: stops if a move stops short")
{
    FakeMultiPassMachine machine;
    mgo::MultiPass mp(machine);
    REQUIRE(mp.start(basicPlan()).empty());
    machine.finishMoves(); // (the first feed in)
    mp.update();
    machine.finishMoves(-6.0);
    mp.update();
    REQUIRE(mp.state() == mgo::MultiPass::State::Idle);
    REQUIRE(!mp.message().empty());
    REQUIRE(machine.log.size() == 2); // (no return)
}

TEST_CASE("MultiPass: an end that isn't a whole number of steps")
{
    // e.g. M8's depth (0.7668 mm) with X's steps of 1/2400 mm (1840.3 of
    // them): the last feed stops on the nearest step, 0.3 of a step short
    FakeMultiPassMachine machine;
    machine.feedStep = 1.0 / 2'400.0;
    machine.m_feed = 5.0 - 2'000 * machine.feedStep; // (on a step)
    mgo::MultiPass mp(machine);
    auto plan = basicPlan();
    plan.feedStart = machine.m_feed;
    plan.feedEnd = plan.feedStart - 0.7668;
    plan.feedPerPass = 0.05;
    REQUIRE(mp.start(plan).empty());
    REQUIRE(mp.totalPasses() == 16);
    machine.run(mp);
    INFO(mp.message());
    REQUIRE(mp.state() == mgo::MultiPass::State::Finished);
    REQUIRE(mp.pass() == 16);
    REQUIRE(machine.feedPosition() == Approx(plan.feedEnd).margin(machine.feedStep / 2.0));
}

namespace {

mgo::MultiPassPlan threadingPlan()
{
    auto plan = basicPlan();
    plan.cut = mgo::MultiPassCut::Thread;
    plan.retract = true;
    return plan;
}

} // end anonymous namespace

TEST_CASE("MultiPass: threading, each cut is a threading pass")
{
    FakeMultiPassMachine machine;
    mgo::MultiPass mp(machine);
    REQUIRE(mp.start(threadingPlan()).empty());
    REQUIRE(mp.totalPasses() == 3);
    machine.run(mp);
    REQUIRE(mp.state() == mgo::MultiPass::State::Finished);
    // (The returns are plain moves at full speed, retracted)
    REQUIRE(machine.log
            == Log { "feed 4.9",  "thread -10", "retract", "cut 0 at 1000", "unretract",
                     "feed 4.8",  "thread -10", "retract", "cut 0 at 1000", "unretract",
                     "feed 4.75", "thread -10", "retract", "cut 0 at 1000", "unretract" });
    REQUIRE(machine.feedPosition() == Approx(4.75));
    REQUIRE(!machine.isRetracted());
}

TEST_CASE("MultiPass: a constant area per pass")
{
    // Pass n goes in to 0.2 x sqrt(n) (0.2, 0.283, 0.346, 0.4...), but at
    // least 0.05 more each time, to 0.5 in all
    FakeMultiPassMachine machine;
    mgo::MultiPass mp(machine);
    auto plan = basicPlan();
    plan.feedEnd = 4.5;
    plan.feedPerPass = 0.2;
    plan.constantArea = true;
    plan.feedMinimum = 0.05;
    // (0.2, 0.2828, 0.3464, 0.4, then 0.45 (the minimum: 0.2 x sqrt(5) is
    // only 0.447), and 0.5)
    const int expected = 6;
    REQUIRE(mgo::multiPassCount({ 0.0, -10.0, 5.0, 4.5 }, 0.2, true, 0.05) == expected);
    REQUIRE(mp.start(plan).empty());
    REQUIRE(mp.totalPasses() == expected);
    machine.run(mp);
    REQUIRE(mp.state() == mgo::MultiPass::State::Finished);
    REQUIRE(mp.pass() == expected);
    std::vector<std::string> feeds;
    for (const auto& entry : machine.log) {
        if (entry.starts_with("feed")) {
            feeds.push_back(entry);
        }
    }
    REQUIRE(feeds
            == Log { "feed 4.8", "feed 4.71716", "feed 4.65359", "feed 4.6", "feed 4.55",
                     "feed 4.5" });

    // Without a minimum, it won't start (the passes could get ever smaller)
    machine.m_feed = 5.0;
    plan.feedMinimum = 0.0;
    REQUIRE(!mp.start(plan).empty());
}

TEST_CASE("MultiPass: a constant area per pass, nudged in while paused")
{
    // A nudge deeper than the next pass would go: the next goes at least the
    // minimum further
    FakeMultiPassMachine machine;
    mgo::MultiPass mp(machine);
    auto plan = basicPlan();
    plan.feedEnd = 4.5;
    plan.feedPerPass = 0.2;
    plan.constantArea = true;
    plan.feedMinimum = 0.05;
    plan.pause = true;
    REQUIRE(mp.start(plan).empty());
    machine.run(mp);
    REQUIRE(mp.state() == mgo::MultiPass::State::Paused);
    REQUIRE(machine.feedPosition() == Approx(4.8));
    machine.m_feed = 4.7; // (0.3 in: deeper than pass 2's 0.283)
    REQUIRE(mp.nextFeed() == Approx(0.05));
    mp.continueAfterPause();
    REQUIRE(machine.log.back() == "feed 4.65");
}

TEST_CASE("MultiPass: a compound infeed moves each cut's start along")
{
    // Z moves along by 0.5 per mm X goes in (in the cut's direction, so
    // negative here), at full speed before each cut, and returns there. The
    // end stays put
    FakeMultiPassMachine machine;
    mgo::MultiPass mp(machine);
    auto plan = threadingPlan();
    plan.feedEnd = 4.8;
    plan.cutShiftPerFeed = -0.5;
    REQUIRE(mp.start(plan).empty());
    machine.run(mp);
    REQUIRE(mp.state() == mgo::MultiPass::State::Finished);
    REQUIRE(machine.log
            == Log { "feed 4.9", "cut -0.05 at 1000", "thread -10", "retract", "cut -0.05 at 1000",
                     "unretract", "feed 4.8", "cut -0.1 at 1000", "thread -10", "retract",
                     "cut -0.1 at 1000", "unretract" });
    // (The cut's speed is kept)
    REQUIRE(machine.cutSpeed() == Approx(40.0));
    REQUIRE(mp.passCutStart() == Approx(-0.1));
}

TEST_CASE("MultiPass: stopping after a pass")
{
    // Asked during a cut: the cut finishes, then it retracts and returns as
    // usual, and stops there, still retracted (clear of the work)
    FakeMultiPassMachine machine;
    mgo::MultiPass mp(machine);
    REQUIRE(mp.start(threadingPlan()).empty());
    machine.finishMoves(); // (the first feed in)
    mp.update();
    REQUIRE(mp.state() == mgo::MultiPass::State::Cutting);
    mp.stopAfterPass("lost pulses");
    REQUIRE(mp.inProgress());
    machine.run(mp);
    REQUIRE(mp.state() == mgo::MultiPass::State::Idle);
    REQUIRE(mp.message() == "lost pulses");
    REQUIRE(machine.log == Log { "feed 4.9", "thread -10", "retract", "cut 0 at 1000" });
    REQUIRE(machine.isRetracted());

    // Paused (or before a cut starts): at once
    machine.finishMoves();
    machine.retract();
    machine.finishMoves();
    machine.m_feed = 5.0;
    auto plan = threadingPlan();
    plan.pause = true;
    REQUIRE(mp.start(plan).empty());
    machine.run(mp);
    REQUIRE(mp.state() == mgo::MultiPass::State::Paused);
    mp.stopAfterPass("lost pulses");
    REQUIRE(mp.state() == mgo::MultiPass::State::Idle);
    REQUIRE(mp.message() == "lost pulses");

    // Starting again forgets it
    machine.m_feed = 5.0;
    machine.log.clear();
    REQUIRE(mp.start(threadingPlan()).empty());
    machine.run(mp);
    REQUIRE(mp.state() == mgo::MultiPass::State::Finished);
}

TEST_CASE("MultiPass: no compound infeed with a taper correction")
{
    FakeMultiPassMachine machine;
    mgo::MultiPass mp(machine);
    auto plan = basicPlan();
    plan.cutShiftPerFeed = -0.5;
    plan.feedCorrection = 0.01;
    REQUIRE(!mp.start(plan).empty());
}

TEST_CASE("MultiPass: threading needs a retract, a motorised feed and no taper correction")
{
    FakeMultiPassMachine machine;
    mgo::MultiPass mp(machine);
    auto plan = threadingPlan();
    plan.retract = false;
    REQUIRE(!mp.start(plan).empty());
    plan = threadingPlan();
    plan.retract = false;
    plan.feedManual = true;
    REQUIRE(!mp.start(plan).empty());
    plan = threadingPlan();
    plan.feedCorrection = 0.01;
    REQUIRE(!mp.start(plan).empty());
    // (Every pass retracts)
    plan = threadingPlan();
    plan.retractLastPasses = 2;
    REQUIRE(!mp.start(plan).empty());
    REQUIRE(mp.state() == mgo::MultiPass::State::Idle);
    REQUIRE(machine.log.empty());
}

TEST_CASE("MultiPass: threading stops if a pass can't start")
{
    FakeMultiPassMachine machine;
    mgo::MultiPass mp(machine);
    REQUIRE(mp.start(threadingPlan()).empty());
    machine.finishMoves(); // (the first feed in)
    machine.threadError = "Start the spindle before threading";
    mp.update();
    REQUIRE(mp.state() == mgo::MultiPass::State::Idle);
    REQUIRE(mp.message() == "Start the spindle before threading");
    REQUIRE(machine.log == Log { "feed 4.9", "thread refused" });

    // With a step-over of zero, the first pass starts at once, so start()
    // says why it didn't
    machine.m_feed = 5.0;
    machine.log.clear();
    auto plan = threadingPlan();
    plan.feedPerPass = 0.0;
    REQUIRE(mp.start(plan) == "Start the spindle before threading");
    REQUIRE(!mp.inProgress());
}

TEST_CASE("MultiPass: threading, repeating the last pass with a step-over of zero and a pause")
{
    // e.g. carrying on past the thread's depth until the nut fits: each pass
    // is at the start's X, which may be nudged in while paused
    FakeMultiPassMachine machine;
    mgo::MultiPass mp(machine);
    auto plan = threadingPlan();
    plan.feedPerPass = 0.0;
    plan.pause = true;
    REQUIRE(mp.start(plan).empty());
    machine.run(mp);
    REQUIRE(mp.state() == mgo::MultiPass::State::Paused);
    machine.m_feed = 4.98; // (nudged in)
    mp.continueAfterPause();
    machine.run(mp);
    REQUIRE(mp.state() == mgo::MultiPass::State::Paused);
    REQUIRE(machine.log
            == Log { "thread -10", "retract", "cut 0 at 1000", "unretract",
                     "thread -10", "retract", "cut 0 at 1000", "unretract" });
}

TEST_CASE("Model:   a multi-pass from Mem 1 to Mem 2")
{
    mgo::MockConfigReader config;
    mgo::MockGpio gpio(false);
    mgo::Model model(gpio, config);
    model.initialise();
    model.axis1SetSpeed(200.0);
    model.axis2SetSpeed(100.0);
    // Mem 1: the start, where the tool is now (Z 0, X 0). Mem 2: Z 0.2, X 0.025
    model.axis1StorePosition(0.0);
    model.axis2StorePosition(0.0);
    model.selectNextMemorySlot();
    model.axis1StorePosition(0.2);
    model.axis2StorePosition(0.025);
    model.selectPreviousMemorySlot();

    model.changeMode(mgo::Mode::MultiPass);
    // (From Mem 1 to Mem 2)
    REQUIRE(model
                .setMultiPassSetup({ .startMemory = 0,
                                     .cutEndMemory = 1,
                                     .feedEndMemory = 1,
                                     .feedPerPass = 0.01,
                                     .pause = false,
                                     .retract = true })
                .empty());
    // Keys which would move an axis aren't allowed while it runs
    REQUIRE(mgo::keyAllowedInCurrentMode(mgo::key::a, model));
    model.multiPassGo();
    REQUIRE(model.getWarning().empty());
    REQUIRE(model.multiPass().inProgress());
    REQUIRE(model.multiPass().totalPasses() == 3);
    REQUIRE(!mgo::keyAllowedInCurrentMode(mgo::key::a, model));
    REQUIRE(!mgo::keyAllowedInCurrentMode(mgo::key::r, model));
    REQUIRE(mgo::keyAllowedInCurrentMode(mgo::key::SPACE, model));
    REQUIRE(mgo::keyAllowedInCurrentMode(mgo::key::EQUALS, model));

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (model.multiPass().inProgress()) {
        REQUIRE(std::chrono::steady_clock::now() < deadline);
        model.checkStatus();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(model.multiPass().state() == mgo::MultiPass::State::Finished);
    REQUIRE(model.multiPass().pass() == 3);
    REQUIRE(model.getAxis1MotorPosition() == Approx(0.0).margin(0.0001));
    REQUIRE(model.getAxis2MotorPosition() == Approx(0.025).margin(0.0001));
    REQUIRE(!model.getIsAxis2Retracted());
    REQUIRE(model.getAxis1MotorSpeed() == Approx(200.0));
    REQUIRE(mgo::keyAllowedInCurrentMode(mgo::key::a, model));
}

TEST_CASE("Model:   a multi-pass whose X end isn't a whole number of steps")
{
    // (X's steps are 1 um here: 16.6 of them)
    mgo::MockConfigReader config;
    mgo::MockGpio gpio(false);
    mgo::Model model(gpio, config);
    model.initialise();
    model.axis1SetSpeed(200.0);
    model.axis2SetSpeed(100.0);
    model.changeMode(mgo::Mode::MultiPass);
    REQUIRE(model
                .setMultiPassSetup({ .cutDistance = 0.1,
                                     .feedInfeed = 0.0166,
                                     .feedPerPass = 0.01,
                                     .pause = false,
                                     .retract = false })
                .empty());
    model.multiPassGo();
    REQUIRE(model.multiPass().totalPasses() == 2);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (model.multiPass().inProgress()) {
        REQUIRE(std::chrono::steady_clock::now() < deadline);
        model.checkStatus();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    INFO(model.multiPass().message());
    REQUIRE(model.multiPass().state() == mgo::MultiPass::State::Finished);
    REQUIRE(model.getWarning().empty());
}

namespace {

// Runs a two-pass multi-pass (Z 0 to 0.2, X 0 to 0.02) with a (large) taper
// correction, checking X is shown as not moving during each cut and return
// while it actually follows Z
void runCorrectedMultiPass(bool retract)
{
    TestConfigReader config({ { "TaperCorrection", "5" } });
    mgo::MockGpio gpio(false);
    mgo::Model model(gpio, config);
    model.initialise();
    model.axis1SetSpeed(200.0);
    model.axis2SetSpeed(100.0);
    model.changeMode(mgo::Mode::MultiPass);
    REQUIRE(model
                .setMultiPassSetup({ .cutDistance = 0.2,
                                     .feedInfeed = 0.02,
                                     .feedPerPass = 0.01,
                                     .pause = false,
                                     .retract = retract })
                .empty());
    const double xEnd = model.getMultiPassRange().feedEnd;
    model.multiPassGo();
    REQUIRE(model.getWarning().empty());
    REQUIRE(model.multiPass().totalPasses() == 2);
    REQUIRE(model.multiPass().plan().feedCorrection == Approx(std::tan(5.0 * M_PI / 180.0)));

    // (The tan of 5 degrees: about 0.0175 of X for 0.2 of Z)
    double mostCorrected = 0.0;
    double mostShownMoved = 0.0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (model.multiPass().inProgress()) {
        REQUIRE(std::chrono::steady_clock::now() < deadline);
        model.checkStatus();
        const auto state = model.multiPass().state();
        if ((state == mgo::MultiPass::State::Cutting || state == mgo::MultiPass::State::Returning)
            && !model.getIsAxis2Retracted()) {
            // Where this pass cuts: halfway in, then xEnd
            const double passX = model.multiPass().pass() == 1 ? xEnd / 2.0 : xEnd;
            mostShownMoved
                = std::max(mostShownMoved, std::abs(model.getAxis2Position() - passX));
            mostCorrected = std::max(
                mostCorrected,
                std::abs(model.getAxis2MotorPosition() - model.getAxis2Position()));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    REQUIRE(model.multiPass().state() == mgo::MultiPass::State::Finished);
    REQUIRE(mostCorrected > 0.01);
    REQUIRE(mostShownMoved < 0.002);
    // Back where the last cut started, with nothing left over
    REQUIRE(model.getAxis1MotorPosition() == Approx(0.0).margin(0.0001));
    REQUIRE(model.getAxis2MotorPosition() == Approx(xEnd).margin(0.0001));
    REQUIRE(model.getAxis2Position() == model.getAxis2MotorPosition());
    REQUIRE(!model.getIsAxis2Retracted());
    REQUIRE(model.getAxis2MotorSpeed() == Approx(100.0));
}

} // end anonymous namespace

TEST_CASE("Model:   a multi-pass correcting a taper")
{
    runCorrectedMultiPass(false);
}

TEST_CASE("Model:   a multi-pass correcting a taper, retracting")
{
    runCorrectedMultiPass(true);
}

TEST_CASE("Model:   stopping a multi-pass")
{
    mgo::MockConfigReader config;
    mgo::MockGpio gpio(false);
    mgo::Model model(gpio, config);
    model.initialise();
    model.axis1SetSpeed(20.0);
    model.axis1StorePosition(0.0);
    model.axis2StorePosition(0.0);
    model.selectNextMemorySlot();
    model.axis1StorePosition(1.0);
    model.axis2StorePosition(0.1);
    model.selectPreviousMemorySlot();
    model.changeMode(mgo::Mode::MultiPass);
    REQUIRE(model
                .setMultiPassSetup(
                    { .startMemory = 0, .cutEndMemory = 1, .feedEndMemory = 1, .feedPerPass = 0.01 })
                .empty());

    model.multiPassGo();
    REQUIRE(model.multiPass().inProgress());
    REQUIRE(model.getWarning().empty());
    model.checkStatus();
    // (X feeding in for the first pass)
    REQUIRE(model.isAxis2MotorRunning());
    // Stopping the motors (e.g. Space) stops it
    model.stopAllMotors();
    REQUIRE(model.multiPass().state() == mgo::MultiPass::State::Idle);
    model.checkStatus();
    REQUIRE(!model.isAxis1MotorRunning());
    REQUIRE(!model.isAxis2MotorRunning());

    // As does leaving the mode part way
    model.axis1GoToPosition(0.0);
    REQUIRE(runUntilStopped(model));
    model.multiPassGo();
    REQUIRE(model.multiPass().inProgress());
    model.changeMode(mgo::Mode::None);
    REQUIRE(!model.multiPass().inProgress());
    REQUIRE(!model.isAxis1MotorRunning());
}

namespace {

// Here: Z 10, X 5 (into the work is -X). Mem 1: Z 2, X 4.5. Mem 2: Z 10, X 5.
// Mem 3: Z only, at 4 (e.g. a shoulder)
mgo::MultiPassPositions testPositions()
{
    mgo::MultiPassPositions p;
    p.cutHere = 10.0;
    p.feedHere = 5.0;
    p.cutMemory = { 2.0, 10.0, 4.0, std::nullopt, std::nullopt, std::nullopt };
    p.feedMemory = { 4.5, 5.0, std::nullopt, std::nullopt, std::nullopt, std::nullopt };
    p.feedIntoWork = -1.0;
    return p;
}

// Runs the model's updates until it's no longer moving to a multi-pass's start
bool runUntilAtStart(mgo::Model& model, std::function<void()> eachUpdate = {})
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (model.multiPassMovingToStart()) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        model.checkStatus();
        if (eachUpdate) {
            eachUpdate();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    model.checkStatus();
    return true;
}

} // end anonymous namespace

TEST_CASE("MultiPass: positions from here, by distances")
{
    mgo::MultiPassSetup setup;
    setup.cutDistance = -7.0;
    setup.feedInfeed = 0.5;
    auto r = mgo::resolveMultiPass(setup, testPositions());
    REQUIRE(r.error.empty());
    REQUIRE(r.range.cutStart == Approx(10.0));
    REQUIRE(r.range.cutEnd == Approx(3.0));
    REQUIRE(r.range.feedStart == Approx(5.0));
    REQUIRE(r.range.feedEnd == Approx(4.5));

    // Boring: X goes into the work the other way
    auto positions = testPositions();
    positions.feedIntoWork = 1.0;
    r = mgo::resolveMultiPass(setup, positions);
    REQUIRE(r.range.feedEnd == Approx(5.5));
}

TEST_CASE("MultiPass: positions from memory slots, either way round")
{
    // Mem 1 to Mem 2
    mgo::MultiPassSetup setup { .startMemory = 0, .cutEndMemory = 1, .feedEndMemory = 1 };
    auto r = mgo::resolveMultiPass(setup, testPositions());
    REQUIRE(r.error.empty());
    REQUIRE(r.range.cutStart == Approx(2.0));
    REQUIRE(r.range.cutEnd == Approx(10.0));
    REQUIRE(r.range.feedStart == Approx(4.5));
    REQUIRE(r.range.feedEnd == Approx(5.0));
    // Mem 2 to Mem 1
    setup = { .startMemory = 1, .cutEndMemory = 0, .feedEndMemory = 0 };
    r = mgo::resolveMultiPass(setup, testPositions());
    REQUIRE(r.error.empty());
    REQUIRE(r.range.cutStart == Approx(10.0));
    REQUIRE(r.range.cutEnd == Approx(2.0));
    REQUIRE(r.range.feedStart == Approx(5.0));
    REQUIRE(r.range.feedEnd == Approx(4.5));
}

TEST_CASE("MultiPass: Z to a shoulder in a memory slot, X by an infeed")
{
    mgo::MultiPassSetup setup { .cutEndMemory = 2, .feedInfeed = 0.25 };
    const auto r = mgo::resolveMultiPass(setup, testPositions());
    REQUIRE(r.error.empty());
    REQUIRE(r.range.cutStart == Approx(10.0));
    REQUIRE(r.range.cutEnd == Approx(4.0));
    REQUIRE(r.range.feedEnd == Approx(4.75));
}

TEST_CASE("MultiPass: positions which can't be used")
{
    const auto error = [](const mgo::MultiPassSetup& setup) {
        return mgo::resolveMultiPass(setup, testPositions()).error;
    };
    // Mem 3 has no X, so can't be a start
    REQUIRE(error({ .startMemory = 2, .cutDistance = 1.0 }) == "Mem 3 isn't set on both Z and X");
    REQUIRE(error({ .cutEndMemory = 3 }) == "Mem 4 has no Z position");
    REQUIRE(error({ .feedEndMemory = 2, .cutDistance = 1.0 }) == "Mem 3 has no X position");
    // Here is Mem 2's Z
    REQUIRE(error({ .cutEndMemory = 1 }) == "The Z start and end are the same");
    REQUIRE(error({}) == "The Z start and end are the same");
    REQUIRE(!error({ .cutDistance = 1.0, .feedInfeed = -0.1 }).empty());
    REQUIRE(!error({ .cutDistance = 1.0, .feedPerPass = -0.1 }).empty());
    REQUIRE(error({ .cutDistance = 1.0 }).empty()); // (an infeed of zero is fine)
}

TEST_CASE("MultiPass: the dialog's defaults")
{
    auto positions = testPositions();
    mgo::MultiPassSetup previous { .cutDistance = -7.0,
                                   .feedInfeed = 0.5,
                                   .feedPerPass = 0.1,
                                   .pause = true,
                                   .retractBeforeMoveToStart = false };

    // At Mem 2 (Z 10, X 5): start there, ending at the other slots set
    auto s = mgo::defaultMultiPassSetup(previous, positions);
    REQUIRE(s.startMemory == 1u);
    REQUIRE(s.cutEndMemory == 0u);
    REQUIRE(s.feedEndMemory == 0u);
    REQUIRE(s.cutDistance == Approx(-7.0));
    REQUIRE(s.feedInfeed == Approx(0.5));
    REQUIRE(s.feedPerPass == Approx(0.1));
    REQUIRE(s.pause);
    REQUIRE(s.retractBeforeMoveToStart); // (always)

    // Not at a memory slot: from here, by the distances used last time
    positions.cutHere = 11.0;
    s = mgo::defaultMultiPassSetup(previous, positions);
    REQUIRE(!s.startMemory);
    REQUIRE(!s.cutEndMemory);
    REQUIRE(!s.feedEndMemory);

    // ...but a memory slot used for an end last time is kept (e.g. a shoulder)
    previous.cutEndMemory = 2;
    s = mgo::defaultMultiPassSetup(previous, positions);
    REQUIRE(!s.startMemory);
    REQUIRE(s.cutEndMemory == 2u);
    REQUIRE(!s.feedEndMemory);

    // ...unless it's now the start (the cut can't end there)
    positions.cutHere = 2.0;
    positions.feedHere = 4.5; // (Mem 1)
    previous.cutEndMemory = 0;
    s = mgo::defaultMultiPassSetup(previous, positions);
    REQUIRE(s.startMemory == 0u);
    REQUIRE(s.cutEndMemory == 1u);
}

TEST_CASE("MultiPass: the number of passes")
{
    mgo::MultiPassRange range { .cutStart = 0.0, .cutEnd = 5.0, .feedStart = 5.0, .feedEnd = 4.5 };
    // Each pass a step in from the last, the first a step in from the start:
    // five steps of 0.1
    REQUIRE(mgo::multiPassCount(range, 0.1) == 5);
    REQUIRE(mgo::multiPassCount(range, 0.3) == 2); // (the last step smaller)
    REQUIRE(mgo::multiPassCount(range, 0.5) == 1); // (all in one)
    REQUIRE(mgo::multiPassCount(range, 0.0) == 0); // (repeats until stopped)
    range.feedEnd = 5.0;
    REQUIRE(mgo::multiPassCount(range, 0.1) == 1); // (one, at the start)
}

TEST_CASE("Model:   X's infeed is the opposite way to its retract")
{
    for (const auto direction : { mgo::XDirection::Outwards, mgo::XDirection::Inwards }) {
        mgo::MockConfigReader config;
        mgo::MockGpio gpio(false);
        mgo::Model model(gpio, config);
        model.initialise();
        model.setRetractionDirection(direction);
        const double intoWork = model.multiPassPositions().feedIntoWork;
        const double before = model.getAxis2MotorPosition();
        model.axis2Retract();
        REQUIRE(runUntilStopped(model));
        const double retracted = model.getAxis2MotorPosition() - before;
        REQUIRE(retracted != Approx(0.0));
        REQUIRE(retracted * intoWork < 0.0);
    }
}

TEST_CASE("Model:   Enter moves to a multi-pass's start, retracting first; Enter again cuts")
{
    mgo::MockConfigReader config;
    mgo::MockGpio gpio(false);
    mgo::Model model(gpio, config);
    model.initialise();
    model.axis1SetSpeed(200.0);
    model.axis2SetSpeed(50.0);
    // From here (Z 0, X 0): Z by 0.2, X in by 0.02
    model.changeMode(mgo::Mode::MultiPass);
    REQUIRE(model
                .setMultiPassSetup(
                    { .cutDistance = 0.2, .feedInfeed = 0.02, .feedPerPass = 0.01 })
                .empty());
    const mgo::MultiPassRange range = model.getMultiPassRange();
    REQUIRE(model.multiPassAtStart());

    // Moved away (as after a multi-pass, or setting up)
    model.axis1GoToPosition(0.5);
    model.axis2GoToPosition(0.1);
    REQUIRE(runUntilStopped(model));
    REQUIRE(!model.multiPassAtStart());

    // The first Enter moves to the start: X retracts first, so is further
    // out than both where it was and the start while Z moves
    const double intoWork = model.multiPassPositions().feedIntoWork;
    // (The moves above may have reset the speeds, as usual after a fast move)
    const double zSpeed = model.getAxis1MotorSpeed();
    const double xSpeed = model.getAxis2MotorSpeed();
    model.multiPassGo();
    REQUIRE(model.multiPassMovingToStart());
    REQUIRE(!model.multiPass().inProgress());
    bool zMovedWithXOut = true;
    REQUIRE(runUntilAtStart(model, [&] {
        if (model.isAxis1MotorRunning()) {
            const double x = model.getAxis2MotorPosition();
            zMovedWithXOut = zMovedWithXOut && (x - 0.1) * intoWork < -1.0;
        }
    }));
    REQUIRE(zMovedWithXOut);
    REQUIRE(model.multiPassAtStart());
    REQUIRE(model.getAxis1MotorPosition() == Approx(range.cutStart).margin(0.0001));
    REQUIRE(model.getAxis2MotorPosition() == Approx(range.feedStart).margin(0.0001));
    REQUIRE(!model.getIsAxis2Retracted());
    REQUIRE(!model.multiPass().inProgress()); // (waiting for the next Enter)
    // (The speeds are as they were, not the full speed Z moved at)
    REQUIRE(model.getAxis1MotorSpeed() == Approx(zSpeed));
    REQUIRE(model.getAxis2MotorSpeed() == Approx(xSpeed));

    // The second starts cutting
    model.multiPassGo();
    REQUIRE(model.multiPass().inProgress());
    REQUIRE(model.multiPass().totalPasses() == 2);
    model.stopAllMotors();
}

TEST_CASE("Model:   without a retract, X moves away from the work before Z, and in after")
{
    for (const bool startFurtherOut : { true, false }) {
        mgo::MockConfigReader config;
        mgo::MockGpio gpio(false);
        mgo::Model model(gpio, config);
        model.initialise();
        model.axis1SetSpeed(200.0);
        const double intoWork = model.multiPassPositions().feedIntoWork;
        // The start is here: Z 0, X 0
        model.changeMode(mgo::Mode::MultiPass);
        REQUIRE(model
                    .setMultiPassSetup({ .cutDistance = 0.2,
                                         .feedInfeed = 0.02,
                                         .feedPerPass = 0.01,
                                         .retractBeforeMoveToStart = false })
                    .empty());
        // Away along Z, and in or out from the start along X
        model.axis1GoToPosition(0.3);
        model.axis2GoToPosition((startFurtherOut ? 0.05 : -0.05) * intoWork);
        REQUIRE(runUntilStopped(model));

        model.multiPassGo();
        bool bothMoved = false;
        bool xAtStartWhenZMoved = true;
        bool zAtStartWhenXMoved = true;
        REQUIRE(runUntilAtStart(model, [&] {
            const bool z = model.isAxis1MotorRunning();
            const bool x = model.isAxis2MotorRunning();
            bothMoved = bothMoved || (z && x);
            if (z) {
                xAtStartWhenZMoved = xAtStartWhenZMoved
                    && std::abs(model.getAxis2MotorPosition()) < 0.0001;
            }
            if (x) {
                zAtStartWhenXMoved
                    = zAtStartWhenXMoved && std::abs(model.getAxis1MotorPosition()) < 0.0001;
            }
        }));
        REQUIRE(!bothMoved); // (one at a time)
        INFO("Start further out: " << startFurtherOut);
        if (startFurtherOut) {
            // X out to the start first, then Z along
            REQUIRE(xAtStartWhenZMoved);
        } else {
            // Z along first, then X in to the start
            REQUIRE(zAtStartWhenXMoved);
        }
        REQUIRE(model.multiPassAtStart());
        REQUIRE(!model.getIsAxis2Retracted());
    }
}

TEST_CASE("Model:   Space stops a move to a multi-pass's start")
{
    mgo::MockConfigReader config;
    mgo::MockGpio gpio(false);
    mgo::Model model(gpio, config);
    model.initialise();
    model.axis1SetSpeed(20.0);
    model.changeMode(mgo::Mode::MultiPass);
    REQUIRE(model.setMultiPassSetup({ .cutDistance = 0.2, .feedPerPass = 0.01 }).empty());
    model.axis1GoToPosition(2.0);
    REQUIRE(runUntilStopped(model));
    model.multiPassGo();
    model.checkStatus();
    REQUIRE(model.multiPassMovingToStart());
    model.stopAllMotors();
    REQUIRE(!model.multiPassMovingToStart());
    model.checkStatus();
    REQUIRE(!model.isAxis1MotorRunning());
    REQUIRE(!model.isAxis2MotorRunning());
    REQUIRE(!model.multiPass().inProgress());
}

namespace {

// A model whose mock spindle turns, with 700 encoder pulses per revolution
// (as on the lathe), set up for threading at 1mm pitch: 1000 Z steps per rev
struct ThreadingTestModel {
    explicit ThreadingTestModel(
        long pulseDelayMicroseconds, std::map<std::string, std::string> extraConfig = {})
        : config([&] {
            extraConfig["RotaryEncoderGearingDivisor"] = "100"; // (35:100)
            return extraConfig;
        }())
        , gpio(false, [&] {
            mgo::MockGpioOptions options;
            options.rotaryDirectionNormal = true;
            options.rotaryEncoderDelayMicroseconds = pulseDelayMicroseconds;
            return options;
        }())
        , model(gpio, config)
    {
        model.initialise();
        std::istringstream threads("[Test | ISO]\nM6, 6, 1\n");
        std::vector<std::string> errors;
        model.setThreads(mgo::readThreads(threads, errors));
        model.setThread({ 0, 0, false });
        REQUIRE(model.getThread()->pitchMm == Approx(1.0));
        model.changeMode(mgo::Mode::Threading);
        // (The spindle's speed is known once it's done a revolution)
        REQUIRE(waitFor([this] { return model.getRotaryEncoderRpm() > 30.f; },
                        std::chrono::seconds(5)));
        model.checkStatus();
    }
    // How far a pulse is from a whole revolution of the encoder's count
    static int64_t pulsesFromWholeRev(int64_t pulses)
    {
        const int64_t r = ((pulses % PULSES_PER_REV) + PULSES_PER_REV) % PULSES_PER_REV;
        return std::min(r, PULSES_PER_REV - r);
    }
    // Starts a pass to the given step, running the model's updates until the
    // backlash has been taken up and the pass itself has started
    void startPass(long endStep)
    {
        model.axis1GoToStep(endStep);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (model.isMotionSequenceRunning()) {
            REQUIRE(std::chrono::steady_clock::now() < deadline);
            model.checkStatus();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        REQUIRE(model.isAxis1MotorRunning());
    }
    // Waits for Z to leave its current step, returning the pulse count then
    int64_t pulsesWhenZStarts()
    {
        const long start = model.getAxis1MotorCurrentStep();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (model.getAxis1MotorCurrentStep() == start) {
            REQUIRE(std::chrono::steady_clock::now() < deadline);
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        return model.getSpindlePulses();
    }
    // Runs the model's updates for a while (as the controller's ticks would)
    void runFor(std::chrono::milliseconds time)
    {
        const auto end = std::chrono::steady_clock::now() + time;
        while (std::chrono::steady_clock::now() < end) {
            model.checkStatus();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    static constexpr int64_t PULSES_PER_REV = 700;
    static constexpr double STEPS_PER_PULSE = 1'000.0 / 700.0;
    TestConfigReader config;
    mgo::MockGpio gpio;
    mgo::Model model;
};

} // end anonymous namespace

TEST_CASE("Threading: Z follows the spindle's position, even as its speed changes")
{
    ThreadingTestModel t(400);
    t.startPass(5'000); // (5mm, i.e. five revolutions)
    // The pass starts at a whole revolution of the encoder's count
    const int64_t started = t.pulsesWhenZStarts();
    REQUIRE(ThreadingTestModel::pulsesFromWholeRev(started) <= 5);
    const int64_t startPulse = (started + 350) / 700 * 700;

    // Z is where the spindle's position says, i.e. the pitch per revolution
    // since the start. (Read one after the other, so allow a few steps)
    const auto checkPosition = [&] {
        const int64_t pulses = t.model.getSpindlePulses();
        const long expected
            = std::lround((pulses - startPulse) * ThreadingTestModel::STEPS_PER_PULSE);
        const long z = t.model.getAxis1MotorCurrentStep();
        INFO("pulses " << pulses << ", Z step " << z << ", expected " << expected);
        REQUIRE(std::abs(z - expected) <= 6);
    };
    t.runFor(std::chrono::milliseconds(300));
    checkPosition();
    t.runFor(std::chrono::milliseconds(300));
    checkPosition();
    // The spindle slows (e.g. under the cutting load), to half speed: Z keeps
    // to the pitch, rather than running on at the old speed
    t.gpio.setRotaryEncoderDelayMicroseconds(800);
    t.runFor(std::chrono::milliseconds(300));
    checkPosition();
    t.runFor(std::chrono::milliseconds(300));
    checkPosition();

    // It ends exactly at the end
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (t.model.isAxis1MotorRunning()) {
        REQUIRE(std::chrono::steady_clock::now() < deadline);
        t.runFor(std::chrono::milliseconds(50));
    }
    REQUIRE(t.model.getAxis1MotorCurrentStep() == 5'000);
    REQUIRE(t.model.getWarning().empty());
}

TEST_CASE("Threading: every pass starts at the same spindle angle")
{
    ThreadingTestModel t(300);
    // Out, back and out again: each starts at a whole revolution
    for (const long end : { 1'000L, 0L, 1'000L }) {
        t.startPass(end);
        const int64_t started = t.pulsesWhenZStarts();
        INFO("Pass to " << end << " started at pulse " << started);
        REQUIRE(ThreadingTestModel::pulsesFromWholeRev(started) <= 5);
        REQUIRE(waitFor([&t] { return !t.model.isAxis1MotorRunning(); }, std::chrono::seconds(5)));
        REQUIRE(t.model.getAxis1MotorCurrentStep() == end);
        t.model.checkStatus();
    }
}

TEST_CASE("Threading: Z's backlash is taken up before a pass reverses")
{
    // More backlash than half the pitch (500 steps): taken up while following
    // the spindle, Z would fall far enough behind for the pass to be stopped
    ThreadingTestModel t(300, { { "Axis1BacklashCompensationSteps", "600" } });
    REQUIRE(waitFor([&t] { return !t.model.isAxis1MotorRunning(); }, std::chrono::seconds(5)));
    const long home = t.model.getAxis1MotorCurrentStep();
    for (const long end : { home + 1'000, home, home + 1'000 }) {
        t.startPass(end);
        const int64_t started = t.pulsesWhenZStarts();
        INFO("Pass to " << end << " started at pulse " << started);
        REQUIRE(ThreadingTestModel::pulsesFromWholeRev(started) <= 5);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (t.model.isAxis1MotorRunning()) {
            REQUIRE(std::chrono::steady_clock::now() < deadline);
            t.runFor(std::chrono::milliseconds(20));
        }
        REQUIRE(t.model.getAxis1MotorCurrentStep() == end);
        INFO("Warning: " << t.model.getWarning());
        REQUIRE(t.model.getWarning().empty());
    }
}

TEST_CASE("Threading: a pass stops if Z can't keep up with the spindle")
{
    ThreadingTestModel t(1'000);
    t.startPass(20'000);
    t.pulsesWhenZStarts();
    t.runFor(std::chrono::milliseconds(100));
    REQUIRE(t.model.isAxis1MotorRunning());
    // Z may follow at up to twice the speed it needed at the start, so a
    // spindle going (much) faster than that leaves it behind
    t.gpio.setRotaryEncoderDelayMicroseconds(100);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (t.model.isAxis1MotorRunning()) {
        REQUIRE(std::chrono::steady_clock::now() < deadline);
        t.runFor(std::chrono::milliseconds(20));
    }
    REQUIRE(t.model.getAxis1MotorCurrentStep() < 20'000);
    REQUIRE(t.model.getWarning().find("keep up") != std::string::npos);
}

TEST_CASE("Threads: reading the table")
{
    std::istringstream in(R"(# A comment
[Metric | ISO]
M6, 6, 1
M10 x 1.25, 10mm, 1.25 mm
bad line
, 3, 0.5
M4, four, 0.7
M4, 4, 0

  # (indented comment)
[Whitworth | whitworth]
3/8" BSW, 3/8", 16 tpi
No. 10 thing, 0.19in, 32TPI
[Empty | BA]
[Bad form | Acme]
M3, 3, 0.5
[No bar]
M3, 3, 0.5
)");
    std::vector<std::string> errors;
    const mgo::ThreadTable table = mgo::readThreads(in, errors);
    // (The empty category is left out, and the threads in the bad ones)
    REQUIRE(table.size() == 2);
    REQUIRE(table.at(0).name == "Metric");
    REQUIRE(table.at(0).threads.size() == 2);
    REQUIRE(table.at(0).threads.at(1).name == "M10 x 1.25");
    REQUIRE(table.at(0).threads.at(1).majorDiameterMm == Approx(10.0));
    REQUIRE(table.at(0).threads.at(1).pitchMm == Approx(1.25));
    REQUIRE(!table.at(0).threads.at(1).tpi);

    const auto& whit = table.at(1);
    REQUIRE(whit.form == mgo::ThreadForm::Whitworth);
    REQUIRE(whit.threads.size() == 2);
    const auto& bsw = whit.threads.at(0);
    REQUIRE(bsw.name == "3/8\" BSW");
    REQUIRE(bsw.inchDiameter);
    REQUIRE(bsw.majorDiameterMm == Approx(9.525));
    REQUIRE(bsw.tpi == Approx(16.0));
    REQUIRE(bsw.pitchMm == Approx(1.5875));
    REQUIRE(whit.threads.at(1).majorDiameterMm == Approx(0.19 * 25.4));
    REQUIRE(whit.threads.at(1).pitchMm == Approx(25.4 / 32.0));

    // Each bad line is reported, by number
    REQUIRE(errors.size() == 6);
    REQUIRE(errors.at(0).starts_with("line 5:")); // (bad line)
    REQUIRE(errors.at(3).starts_with("line 8:")); // (zero pitch)
    REQUIRE(errors.at(4).starts_with("line 15:")); // (Acme)
    REQUIRE(errors.at(5).starts_with("line 17:")); // (no bar)

    // A thread before any category
    std::istringstream early("M6, 6, 1\n");
    errors.clear();
    REQUIRE(mgo::readThreads(early, errors).empty());
    REQUIRE(errors.size() == 1);
}

TEST_CASE("Threads: figures from the forms")
{
    std::istringstream in("[M | ISO]\nM6, 6, 1\n[W | Whitworth]\n3/8\" BSW, 3/8\", 16 tpi\n"
                          "[B | BA]\n0BA, 6, 1\n");
    std::vector<std::string> errors;
    const auto table = mgo::readThreads(in, errors);
    REQUIRE(errors.empty());
    const auto& m6 = table.at(0).threads.at(0);
    REQUIRE(m6.maleOd() == Approx(6.0));
    REQUIRE(m6.maleDepth() == Approx(0.613).margin(0.0005));
    REQUIRE(m6.femaleBore() == Approx(4.917).margin(0.0005));
    REQUIRE(m6.femaleDepth() == Approx(0.541).margin(0.0005));
    const auto& bsw = table.at(1).threads.at(0);
    REQUIRE(bsw.maleDepth() == Approx(1.016).margin(0.001));
    REQUIRE(bsw.femaleBore() == Approx(7.492).margin(0.001));
    REQUIRE(bsw.femaleDepth() == Approx(bsw.maleDepth()));
    const auto& ba = table.at(2).threads.at(0);
    REQUIRE(ba.maleDepth() == Approx(0.6));
    REQUIRE(ba.femaleBore() == Approx(4.8));
    REQUIRE(mgo::threadFormInfo(mgo::ThreadForm::Ba).angle == Approx(47.5));

    REQUIRE(mgo::describePitch(m6) == "1 mm");
    REQUIRE(mgo::describePitch(bsw) == "16 tpi (1.5875 mm)");
}

TEST_CASE("Threads: the supplied threadpitches.cfg")
{
    std::vector<std::string> errors;
    const auto table = mgo::readThreadsFile(LC_SOURCE_DIR "/threadpitches.cfg", errors);
    INFO((errors.empty() ? "" : errors.front()));
    REQUIRE(errors.empty());
    REQUIRE(table.size() >= 8);
    for (const auto& category : table) {
        for (const auto& thread : category.threads) {
            INFO(thread.name);
            REQUIRE(thread.pitchMm > 0.0);
            REQUIRE(thread.pitchMm < 4.0);
            REQUIRE(thread.majorDiameterMm > thread.femaleBore());
            REQUIRE(thread.femaleBore() > 0.0);
        }
    }
    // e.g. M12, as the old built-in table had it
    const auto& coarse = table.at(0);
    REQUIRE(coarse.name == "Metric coarse");
    const auto m12 = std::ranges::find(coarse.threads, std::string("M12"), &mgo::Thread::name);
    REQUIRE(m12 != coarse.threads.end());
    REQUIRE(m12->pitchMm == Approx(1.75));
    REQUIRE(m12->femaleBore() == Approx(10.106).margin(0.001));
}

TEST_CASE("Model:   choosing a thread")
{
    mgo::MockConfigReader config;
    mgo::MockGpio gpio(false);
    mgo::Model model(gpio, config);
    model.initialise();
    std::istringstream in("[M | ISO]\nM6, 6, 1\nM8, 8, 1.25\n");
    std::vector<std::string> errors;
    model.setThreads(mgo::readThreads(in, errors));
    REQUIRE(model.getThread()->name == "M6");
    // A female thread retracts X inwards (boring), a male one outwards
    model.setThread({ 0, 1, true });
    REQUIRE(model.getThread()->name == "M8");
    REQUIRE(model.getThreadChoice().female);
    REQUIRE(model.getRetractionDirection() == mgo::XDirection::Inwards);
    model.setThread({ 0, 0, false });
    REQUIRE(model.getRetractionDirection() == mgo::XDirection::Outwards);
    // (Out of range is ignored)
    model.setThread({ 3, 0, true });
    REQUIRE(model.getThread()->name == "M6");
    // No threads: none chosen
    model.setThreads({});
    REQUIRE(model.getThread() == nullptr);
}

TEST_CASE("Model:   threading passes are set up only in threading mode")
{
    mgo::MockConfigReader config;
    mgo::MockGpio gpio(false);
    mgo::Model model(gpio, config);
    model.initialise();
    std::istringstream in("[M | ISO]\nM6, 6, 1\nM8, 8, 1.25\n");
    std::vector<std::string> errors;
    model.setThreads(mgo::readThreads(in, errors));
    model.setThread({ 0, 0, false });
    const mgo::MultiPassSetup setup { .cutDistance = 1.0, .feedInfeed = 0.6, .feedPerPass = 0.1 };
    REQUIRE(!model.setThreadingPassesSetup(setup).empty());
    REQUIRE(!model.threadingPassesSet());

    model.changeMode(mgo::Mode::Threading);
    // The dialog starts at the thread's depth (M6: 0.613 x 1 mm)
    REQUIRE(
        model.threadingPassesDialogSetup().feedInfeed == Approx(model.getThread()->maleDepth()));
    REQUIRE(model.getThread()->maleDepth() == Approx(0.613).epsilon(0.001));
    REQUIRE(model.threadingPassesDialogSetup().retract);
    REQUIRE(model.setThreadingPassesSetup(setup).empty());
    REQUIRE(model.threadingPassesSet());
    // (Always retracting)
    REQUIRE(model.getThreadingPassesSetup().retract);
    REQUIRE(model.activeMultiPassSetup().feedInfeed == Approx(0.6));
    REQUIRE(mgo::keyAllowedInCurrentMode(mgo::key::a, model));
    // The same thread again: as last time
    REQUIRE(model.threadingPassesDialogSetup().feedInfeed == Approx(0.6));

    // Choosing a thread again (which changes mode) clears them; a different
    // thread starts at its own depth (M8: 0.613 x 1.25 mm)
    model.setThread({ 0, 1, false });
    model.changeMode(mgo::Mode::Threading);
    REQUIRE(!model.threadingPassesSet());
    REQUIRE(
        model.threadingPassesDialogSetup().feedInfeed == Approx(model.getThread()->maleDepth()));
    REQUIRE(model.getThread()->pitchMm == Approx(1.25));
    model.multiPassGo(); // (does nothing: not set up)
    REQUIRE(!model.multiPass().inProgress());
    REQUIRE(!model.multiPassMovingToStart());
}

TEST_CASE("Model:   threading passes, each locked to the spindle")
{
    ThreadingTestModel t(300);
    REQUIRE(waitFor(
        [&t] {
            return !t.model.isAxis1MotorRunning();
        },
        std::chrono::seconds(5)));
    t.model.checkStatus();
    t.model.axis2SetSpeed(100.0);
    // Z 1 mm (a revolution, at M6's pitch), X in 0.02 by 0.01: two passes
    REQUIRE(
        t.model
            .setThreadingPassesSetup(
                { .cutDistance = 1.0, .feedInfeed = 0.02, .feedPerPass = 0.01, .retract = false })
            .empty());
    const double zStart = t.model.getMultiPassRange().cutStart;
    const double xEnd = t.model.getMultiPassRange().feedEnd;
    t.model.multiPassGo();
    REQUIRE(t.model.getWarning().empty());
    REQUIRE(t.model.multiPass().inProgress());
    REQUIRE(t.model.multiPass().plan().cut == mgo::MultiPassCut::Thread);
    // (Retracting, though the setup said not to)
    REQUIRE(t.model.multiPass().plan().retract);
    REQUIRE(t.model.multiPass().totalPasses() == 2);
    // Only Space, speeds and units while it runs
    REQUIRE(!mgo::keyAllowedInCurrentMode(mgo::key::a, t.model));

    // Each pass starts at a whole revolution of the spindle. While Z follows
    // the spindle, its steps from the start give the pulses since the pass
    // started, so any reading during the cut shows where it started (to
    // within a few steps, as Z and the spindle are read one after the other).
    // A pass that wasn't synchronised could start up to half a revolution
    // (350 pulses) away
    const long startStep = t.model.getAxis1MotorCurrentStep();
    std::map<int, int64_t> passStarts; // (by pass)
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (t.model.multiPass().inProgress()) {
        REQUIRE(std::chrono::steady_clock::now() < deadline);
        t.model.checkStatus();
        const int pass = t.model.multiPass().pass();
        const int64_t pulses = t.model.getSpindlePulses();
        const long steps = std::abs(t.model.getAxis1MotorCurrentStep() - startStep);
        if (t.model.multiPass().state() == mgo::MultiPass::State::Cutting && steps > 50
            && steps < 950 && !passStarts.contains(pass)) {
            passStarts[pass] = pulses
                - std::lround(static_cast<double>(steps) / ThreadingTestModel::STEPS_PER_PULSE);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    INFO(t.model.multiPass().message());
    REQUIRE(t.model.multiPass().state() == mgo::MultiPass::State::Finished);
    REQUIRE(passStarts.size() == 2);
    for (const auto& [pass, pulses] : passStarts) {
        INFO("Pass " << pass << " started at pulse " << pulses);
        REQUIRE(ThreadingTestModel::pulsesFromWholeRev(pulses) <= 10);
    }
    REQUIRE(t.model.getAxis1MotorPosition() == Approx(zStart).margin(0.0001));
    REQUIRE(t.model.getAxis2MotorPosition() == Approx(xEnd).margin(0.0001));
    REQUIRE(!t.model.getIsAxis2Retracted());
    REQUIRE(t.model.getWarning().empty());
    // Still in threading mode, ready to go again
    REQUIRE(t.model.getMode() == mgo::Mode::Threading);
    REQUIRE(t.model.threadingPassesSet());
}

namespace {

// Runs a threading model's updates until its multi-pass is paused, finished
// or stopped
void runThreadingPasses(ThreadingTestModel& t)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (t.model.multiPass().inProgress()
           && t.model.multiPass().state() != mgo::MultiPass::State::Paused) {
        REQUIRE(std::chrono::steady_clock::now() < deadline);
        t.model.checkStatus();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

// Runs a threading model's updates until a move (e.g. to the start, or a
// nudge) is over
void runUntilMoved(ThreadingTestModel& t)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (t.model.multiPassMovingToStart() || t.model.isMotionSequenceRunning()
           || t.model.isAxis1MotorRunning() || t.model.isAxis2MotorRunning()) {
        REQUIRE(std::chrono::steady_clock::now() < deadline);
        t.model.checkStatus();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

} // end anonymous namespace

TEST_CASE("Model:   spring passes after the threading passes")
{
    ThreadingTestModel t(300);
    REQUIRE(waitFor([&t] { return !t.model.isAxis1MotorRunning(); }, std::chrono::seconds(5)));
    t.model.checkStatus();
    t.model.axis2SetSpeed(100.0);
    // One pass, in 0.01
    REQUIRE(t.model
                .setThreadingPassesSetup(
                    { .cutDistance = 1.0, .feedInfeed = 0.01, .feedPerPass = 0.01 })
                .empty());
    const double zStart = t.model.getMultiPassRange().cutStart;
    const double xEnd = t.model.getMultiPassRange().feedEnd;
    REQUIRE(!t.model.springPassesAvailable());
    t.model.multiPassGo();
    runThreadingPasses(t);
    REQUIRE(t.model.multiPass().state() == mgo::MultiPass::State::Finished);
    REQUIRE(t.model.springPassesAvailable());
    REQUIRE(t.model.springPassAtStart());
    REQUIRE(!t.model.springPasses());

    // Enter takes a spring pass at the same depth, then pauses
    t.model.multiPassGo();
    REQUIRE(t.model.multiPass().inProgress());
    REQUIRE(t.model.springPasses());
    REQUIRE(t.model.multiPass().plan().cut == mgo::MultiPassCut::Thread);
    runThreadingPasses(t);
    REQUIRE(t.model.multiPass().state() == mgo::MultiPass::State::Paused);
    REQUIRE(t.model.multiPass().pass() == 1);
    REQUIRE(t.model.getAxis2MotorPosition() == Approx(xEnd).margin(0.0001));

    // A fine nudge in (W) is allowed while paused, and the next pass is there
    REQUIRE(mgo::keyAllowedInCurrentMode(mgo::key::W, t.model));
    REQUIRE(mgo::keyAllowedInCurrentMode(mgo::key::w, t.model));
    REQUIRE(!mgo::keyAllowedInCurrentMode(mgo::key::AltW, t.model));
    const double intoWork = xEnd < t.model.getMultiPassRange().feedStart ? -1.0 : 1.0;
    t.model.axis2Nudge(intoWork < 0.0 ? mgo::XDirection::Inwards : mgo::XDirection::Outwards, 0.01);
    runUntilMoved(t);
    const double nudged = t.model.getAxis2MotorPosition();
    REQUIRE(nudged == Approx(xEnd + intoWork * 0.01).margin(0.0001));
    t.model.multiPassGo();
    runThreadingPasses(t);
    REQUIRE(t.model.multiPass().state() == mgo::MultiPass::State::Paused);
    REQUIRE(t.model.multiPass().pass() == 2);
    REQUIRE(t.model.getAxis2MotorPosition() == Approx(nudged).margin(0.0001));
    REQUIRE(t.model.getAxis1MotorPosition() == Approx(zStart).margin(0.0001));

    const mgo::XDirection in = intoWork < 0.0 ? mgo::XDirection::Inwards : mgo::XDirection::Outwards;
    const mgo::XDirection out
        = intoWork < 0.0 ? mgo::XDirection::Outwards : mgo::XDirection::Inwards;

    // X moved out while paused (e.g. to try the nut on): Enter takes it back
    // to the last depth, rather than cutting from there; Enter again cuts
    t.model.axis2Nudge(out, 0.05);
    runUntilMoved(t);
    REQUIRE(!t.model.springPassAtStart());
    t.model.multiPassGo();
    REQUIRE(!t.model.multiPass().inProgress());
    REQUIRE(t.model.multiPass().message().empty()); // (not "stopped")
    REQUIRE(t.model.multiPassMovingToStart());
    runUntilMoved(t);
    REQUIRE(t.model.getAxis2MotorPosition() == Approx(nudged).margin(0.0001));
    REQUIRE(t.model.springPassAtStart());
    t.model.multiPassGo();
    REQUIRE(t.model.multiPass().inProgress());
    runThreadingPasses(t);
    REQUIRE(t.model.multiPass().state() == mgo::MultiPass::State::Paused);
    REQUIRE(t.model.getAxis2MotorPosition() == Approx(nudged).margin(0.0001));

    // Z moved along while paused (e.g. off the end): allowed between spring
    // passes. Enter while it's moving waits; once it's stopped, Enter takes it
    // back to the last pass's start, then Enter again cuts
    REQUIRE(mgo::keyAllowedInCurrentMode(mgo::key::RIGHT, t.model));
    REQUIRE(mgo::keyAllowedInCurrentMode(mgo::key::LEFT, t.model));
    REQUIRE(!mgo::keyAllowedInCurrentMode(mgo::key::AltRight, t.model));
    t.model.axis1GoToPosition(zStart - 0.5);
    REQUIRE(waitFor([&t] { return t.model.isAxis1MotorRunning(); }, std::chrono::seconds(3)));
    t.model.multiPassGo();
    REQUIRE(t.model.getWarning() == "Stop the motors first");
    REQUIRE(t.model.multiPass().state() == mgo::MultiPass::State::Paused);
    runUntilMoved(t);
    REQUIRE(t.model.getAxis1MotorPosition() == Approx(zStart - 0.5).margin(0.0001));
    REQUIRE(!t.model.springPassAtStart());
    t.model.multiPassGo();
    REQUIRE(!t.model.multiPass().inProgress());
    REQUIRE(t.model.multiPassMovingToStart());
    runUntilMoved(t);
    REQUIRE(t.model.getAxis1MotorPosition() == Approx(zStart).margin(0.0001));
    REQUIRE(t.model.getAxis2MotorPosition() == Approx(nudged).margin(0.0001));
    REQUIRE(t.model.springPassAtStart());
    t.model.multiPassGo();
    runThreadingPasses(t);
    REQUIRE(t.model.multiPass().state() == mgo::MultiPass::State::Paused);
    REQUIRE(t.model.getWarning().empty());

    // Space ends them; Enter takes another
    t.model.stopAllMotors();
    REQUIRE(!t.model.multiPass().inProgress());
    REQUIRE(t.model.springPassAtStart());
    t.model.multiPassGo();
    REQUIRE(t.model.springPasses());
    REQUIRE(t.model.multiPass().pass() == 1);
    t.model.stopAllMotors();
    runUntilMoved(t);

    // Stopped, X moved out without retracting: Enter goes back to the last
    // depth first
    t.model.axis2Nudge(out, 0.05);
    runUntilMoved(t);
    REQUIRE(!t.model.springPassAtStart());
    t.model.multiPassGo();
    REQUIRE(t.model.multiPassMovingToStart());
    runUntilMoved(t);
    REQUIRE(t.model.getAxis2MotorPosition() == Approx(nudged).margin(0.0001));

    // Stopped, X nudged in: deeper is on purpose, so Enter cuts from there
    t.model.axis2Nudge(in, 0.01);
    runUntilMoved(t);
    const double deeper = t.model.getAxis2MotorPosition();
    REQUIRE(deeper == Approx(nudged + intoWork * 0.01).margin(0.0001));
    REQUIRE(t.model.springPassAtStart());
    t.model.multiPassGo();
    INFO("Warning: " << t.model.getWarning());
    REQUIRE(t.model.multiPass().inProgress());
    runThreadingPasses(t);
    REQUIRE(t.model.multiPass().state() == mgo::MultiPass::State::Paused);
    REQUIRE(t.model.getAxis2MotorPosition() == Approx(deeper).margin(0.0001));
    t.model.stopAllMotors();
    runUntilMoved(t);

    // Away from the start (e.g. retracted, or stopped part way): Enter moves
    // back to the start of the cut, at the last depth, then takes a pass
    t.model.axis2Retract();
    runUntilMoved(t);
    REQUIRE(!t.model.springPassAtStart());
    t.model.multiPassGo();
    REQUIRE(t.model.multiPassMovingToStart());
    runUntilMoved(t);
    REQUIRE(t.model.springPassAtStart());
    REQUIRE(t.model.getAxis1MotorPosition() == Approx(zStart).margin(0.0001));
    REQUIRE(t.model.getAxis2MotorPosition() == Approx(deeper).margin(0.0001));
    t.model.multiPassGo();
    runThreadingPasses(t);
    REQUIRE(t.model.multiPass().state() == mgo::MultiPass::State::Paused);
    REQUIRE(t.model.getWarning().empty());

    // Setting the passes up again (or changing mode) ends spring passes
    t.model.stopAllMotors();
    runUntilMoved(t);
    REQUIRE(t.model
                .setThreadingPassesSetup(
                    { .cutDistance = 1.0, .feedInfeed = 0.01, .feedPerPass = 0.01 })
                .empty());
    REQUIRE(!t.model.springPassesAvailable());
}

TEST_CASE("Model:   threading passes with a constant area and a compound infeed")
{
    ThreadingTestModel t(300);
    REQUIRE(waitFor([&t] { return !t.model.isAxis1MotorRunning(); }, std::chrono::seconds(5)));
    t.model.checkStatus();
    t.model.axis2SetSpeed(100.0);
    // Both on the first time
    REQUIRE(t.model.threadingPassesDialogSetup().constantArea);
    REQUIRE(t.model.threadingPassesDialogSetup().compoundInfeed);
    REQUIRE(t.model.getCompoundInfeedAngleDeg() == Approx(29.5));
    // In 0.03, the first pass 0.02: then the minimum (0.02) takes it to the
    // end
    REQUIRE(t.model
                .setThreadingPassesSetup({ .cutDistance = 1.0,
                                           .feedInfeed = 0.03,
                                           .feedPerPass = 0.02,
                                           .constantArea = true,
                                           .compoundInfeed = true })
                .empty());
    const double zStart = t.model.getMultiPassRange().cutStart;
    t.model.multiPassGo();
    REQUIRE(t.model.multiPass().inProgress());
    REQUIRE(t.model.multiPass().plan().constantArea);
    REQUIRE(t.model.multiPass().plan().feedMinimum == Approx(0.02));
    // (The cut goes the positive way, so Z moves along that way)
    const double perX = std::tan(29.5 * M_PI / 180.0);
    REQUIRE(t.model.multiPass().plan().cutShiftPerFeed == Approx(perX));
    REQUIRE(t.model.multiPass().totalPasses() == 2);

    // Where Z is as each pass starts cutting, and how far X has gone in
    std::vector<std::pair<double, double>> starts;
    bool wasCutting = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (t.model.multiPass().inProgress()) {
        REQUIRE(std::chrono::steady_clock::now() < deadline);
        t.model.checkStatus();
        const bool cutting = t.model.multiPass().state() == mgo::MultiPass::State::Cutting;
        if (cutting && !wasCutting) {
            starts.emplace_back(t.model.getAxis1MotorPosition(), t.model.multiPass().fed());
        }
        wasCutting = cutting;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    INFO(t.model.multiPass().message());
    REQUIRE(t.model.multiPass().state() == mgo::MultiPass::State::Finished);
    REQUIRE(starts.size() == 2);
    REQUIRE(starts[0].second == Approx(0.02));
    REQUIRE(starts[1].second == Approx(0.03));
    for (const auto& [z, fed] : starts) {
        INFO("X in " << fed << ", Z at " << z);
        // (To the nearest step: 1 um here)
        REQUIRE(z == Approx(zStart + fed * perX).margin(0.0006));
    }
    REQUIRE(t.model.getWarning().empty());

    // Spring passes start where the last pass did
    REQUIRE(t.model.springPassAtStart());
    t.model.multiPassGo();
    REQUIRE(t.model.springPasses());
    REQUIRE(t.model.multiPass().plan().cutStart == Approx(starts[1].first).margin(0.0006));
    REQUIRE(t.model.multiPass().plan().cutShiftPerFeed == 0.0);
    runThreadingPasses(t);
    REQUIRE(t.model.multiPass().state() == mgo::MultiPass::State::Paused);
    t.model.stopAllMotors();
}

TEST_CASE("Model:   threading passes stop after a pass if the encoder loses pulses")
{
    ThreadingTestModel t(300);
    REQUIRE(waitFor([&t] { return !t.model.isAxis1MotorRunning(); }, std::chrono::seconds(5)));
    t.model.checkStatus();
    t.model.axis2SetSpeed(100.0);
    REQUIRE(t.model
                .setThreadingPassesSetup({ .cutDistance = 1.0,
                                           .feedInfeed = 0.03,
                                           .feedPerPass = 0.01,
                                           .constantArea = false,
                                           .compoundInfeed = false })
                .empty());
    const double zStart = t.model.getMultiPassRange().cutStart;
    t.model.multiPassGo();
    REQUIRE(t.model.multiPass().totalPasses() == 3);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!(t.model.multiPass().state() == mgo::MultiPass::State::Cutting
             && t.model.isAxis1MotorRunning())) {
        REQUIRE(std::chrono::steady_clock::now() < deadline);
        t.model.checkStatus();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    // During the first pass, the spindle's pulses stop for a moment (over ten
    // times the usual gap), then carry on as before: pulses were lost
    t.gpio.setRotaryEncoderDelayMicroseconds(5'000);
    std::this_thread::sleep_for(std::chrono::milliseconds(8));
    t.gpio.setRotaryEncoderDelayMicroseconds(300);
    while (t.model.multiPass().inProgress()) {
        REQUIRE(std::chrono::steady_clock::now() < deadline);
        t.model.checkStatus();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    // It finished that pass, retracted and returned, then stopped there
    REQUIRE(t.model.multiPass().state() == mgo::MultiPass::State::Idle);
    REQUIRE(t.model.multiPass().pass() == 1);
    REQUIRE(t.model.getWarning().find("lost pulses") != std::string::npos);
    REQUIRE(t.model.getIsAxis2Retracted());
    REQUIRE(t.model.getAxis1MotorPosition() == Approx(zStart).margin(0.0001));

    // Enter won't start them again (nor spring passes) on this thread
    t.model.multiPassGo();
    REQUIRE(!t.model.multiPass().inProgress());
    REQUIRE(!t.model.multiPassMovingToStart());
    REQUIRE(t.model.getWarning().find("lost pulses") != std::string::npos);

    // Choosing the thread again (e.g. for new work) clears it
    t.model.changeMode(mgo::Mode::Threading);
    REQUIRE(t.model
                .setThreadingPassesSetup({ .cutDistance = 1.0, .feedInfeed = 0.03, .feedPerPass = 0.01 })
                .empty());
    // (From here, so Enter's move to the start just counts X as unretracted;
    // then Enter starts them)
    t.model.multiPassGo();
    REQUIRE(t.model.getWarning().empty());
    runUntilMoved(t);
    REQUIRE(!t.model.getIsAxis2Retracted());
    t.model.multiPassGo();
    REQUIRE(t.model.getWarning().empty());
    REQUIRE(t.model.multiPass().inProgress());
    t.model.stopAllMotors();
}

TEST_CASE("Model:   threading passes stop if a pass can't start")
{
    ThreadingTestModel t(300);
    REQUIRE(t.model
                .setThreadingPassesSetup(
                    { .cutDistance = 1.0, .feedInfeed = 0.02, .feedPerPass = 0.01 })
                .empty());
    t.model.axis2SetSpeed(100.0);
    // Too fast for Z to follow at this pitch (over 800 rpm)
    t.gpio.setRotaryEncoderDelayMicroseconds(50);
    REQUIRE(waitFor(
        [&t] {
            return t.model.getRotaryEncoderRpm() > 1'000.f;
        },
        std::chrono::seconds(5)));
    t.model.multiPassGo();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (t.model.multiPass().inProgress()) {
        REQUIRE(std::chrono::steady_clock::now() < deadline);
        t.model.checkStatus();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    REQUIRE(t.model.multiPass().state() == mgo::MultiPass::State::Idle);
    INFO(t.model.getWarning());
    REQUIRE(t.model.getWarning().find("too fast") != std::string::npos);
}

TEST_CASE("MultiPass: unretracts before pausing")
{
    FakeMultiPassMachine machine;
    mgo::MultiPass mp(machine);
    auto plan = basicPlan();
    plan.retract = true;
    plan.pause = true;
    REQUIRE(mp.start(plan).empty());
    machine.run(mp);
    REQUIRE(mp.state() == mgo::MultiPass::State::Paused);
    // (So the feed axis shows where the pass cut, and can be nudged)
    REQUIRE(machine.log
            == Log { "feed 4.9", "cut -10 at 40", "retract", "cut 0 at 1000", "unretract" });
    REQUIRE(!machine.isRetracted());
    REQUIRE(machine.feedPosition() == Approx(4.9));
}

TEST_CASE("MultiPass: moving the feed axis while paused")
{
    FakeMultiPassMachine machine;
    mgo::MultiPass mp(machine);
    auto plan = basicPlan();
    plan.pause = true;

    // With a step-over: the next pass steps on from where it was moved to
    REQUIRE(mp.start(plan).empty());
    machine.run(mp);
    REQUIRE(mp.totalPasses() == 3);
    machine.m_feed = 4.85; // (nudged in 0.05)
    REQUIRE(mp.nextFeed() == Approx(0.1));
    mp.continueAfterPause();
    REQUIRE(machine.log.back() == "feed 4.75");
    REQUIRE(mp.fed() == Approx(0.25));
    // (One fewer pass to go now)
    REQUIRE(mp.totalPasses() == 2);
    machine.run(mp);
    REQUIRE(mp.state() == mgo::MultiPass::State::Finished);
    REQUIRE(machine.feedPosition() == Approx(4.75));

    // With no step-over (repeating a cut): each pass is where it was moved to,
    // e.g. nudging in a little each time
    machine.log.clear();
    machine.m_feed = 5.0;
    plan.feedPerPass = 0.0;
    REQUIRE(mp.start(plan).empty());
    for (const double depth : { 4.98, 4.96 }) {
        machine.run(mp);
        REQUIRE(mp.state() == mgo::MultiPass::State::Paused);
        machine.m_feed = depth;
        mp.continueAfterPause();
        REQUIRE(mp.state() == mgo::MultiPass::State::Cutting);
        REQUIRE(mp.fed() == Approx(5.0 - depth));
    }
    for (const auto& entry : machine.log) {
        REQUIRE(entry.starts_with("cut")); // (no feed moves of its own)
    }
    mp.stop("stopped");
}

TEST_CASE("Model:   nudging X while a multi-pass is paused")
{
    mgo::MockConfigReader config;
    mgo::MockGpio gpio(false);
    mgo::Model model(gpio, config);
    model.initialise();
    model.axis1SetSpeed(200.0);
    model.axis2SetSpeed(100.0);
    model.axis1StorePosition(0.0);
    model.axis2StorePosition(0.0);
    model.selectNextMemorySlot();
    model.axis1StorePosition(0.2);
    model.axis2StorePosition(0.05);
    model.selectPreviousMemorySlot();
    model.changeMode(mgo::Mode::MultiPass);
    REQUIRE(model
                .setMultiPassSetup({ .startMemory = 0,
                                     .cutEndMemory = 1,
                                     .feedEndMemory = 1,
                                     .feedPerPass = 0.01,
                                     .pause = true,
                                     .retract = true })
                .empty());
    model.multiPassGo();
    REQUIRE(model.multiPass().inProgress());
    // Not while cutting...
    REQUIRE(!mgo::keyAllowedInCurrentMode(mgo::key::w, model));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (model.multiPass().state() != mgo::MultiPass::State::Paused) {
        REQUIRE(std::chrono::steady_clock::now() < deadline);
        model.checkStatus();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    // ...but while paused (unretracted), X's nudges are allowed, except the
    // Alt ones (as releasing Alt stops everything)
    REQUIRE(!model.getIsAxis2Retracted());
    for (const int k : { mgo::key::w, mgo::key::W, mgo::key::s, mgo::key::S }) {
        REQUIRE(mgo::keyAllowedInCurrentMode(k, model));
    }
    REQUIRE(!mgo::keyAllowedInCurrentMode(mgo::key::AltW, model));
    REQUIRE(!mgo::keyAllowedInCurrentMode(mgo::key::a, model)); // (nor Z's)
    // (Z only moves between spring passes, in threading mode)
    REQUIRE(!mgo::keyAllowedInCurrentMode(mgo::key::RIGHT, model));
    REQUIRE(!mgo::keyAllowedInCurrentMode(mgo::key::LEFT, model));
    model.stopAllMotors();
}

TEST_CASE("Model:   leaving threading mode resets Z's speed")
{
    mgo::MockConfigReader config;
    mgo::MockGpio gpio(false);
    mgo::Model model(gpio, config);
    model.initialise();
    model.changeMode(mgo::Mode::Threading);
    model.axis1SetSpeed(500.0);
    model.changeMode(mgo::Mode::None);
    REQUIRE(model.getAxis1MotorSpeed() == Approx(40.0)); // (Axis1SpeedPreset2)
    // (Other modes leave it alone)
    model.changeMode(mgo::Mode::MultiPass);
    model.axis1SetSpeed(70.0);
    model.changeMode(mgo::Mode::None);
    REQUIRE(model.getAxis1MotorSpeed() == Approx(70.0));
}

TEST_CASE("Model:   X's retraction direction from the config")
{
    mgo::MockGpio gpio(false);
    {
        TestConfigReader config({ { "Axis2RetractionDirection", "inwards" } });
        mgo::Model model(gpio, config);
        model.initialise();
        REQUIRE(model.getRetractionDirection() == mgo::XDirection::Inwards);
    }
    {
        TestConfigReader config({ { "Axis2RetractionDirection", "sideways" } });
        mgo::Model model(gpio, config);
        model.initialise();
        REQUIRE(model.getRetractionDirection() == mgo::XDirection::Outwards);
    }
    {
        mgo::MockConfigReader config;
        mgo::Model model(gpio, config);
        model.initialise();
        REQUIRE(model.getRetractionDirection() == mgo::XDirection::Outwards);
    }
}

// ===== The current feature =====

namespace {

// testPositions(), with the feature set: the work's end at Z 0, its surface
// at X 7.5 (so 15 diameter). (Into the work is -X)
mgo::MultiPassPositions featurePositions()
{
    mgo::MultiPassPositions p = testPositions();
    p.featureWorkEnd = 0.0;
    p.featureFeed = 7.5;
    p.featureClearance = 0.5;
    return p;
}

TestConfigReader featureConfig()
{
    return TestConfigReader({ { "CurrentFeature", "true" } });
}

} // end anonymous namespace

TEST_CASE("MultiPass: a start from the feature")
{
    // Z towards the chuck (-): the start is off the work's end the other way
    mgo::MultiPassSetup setup { .startFeature = true, .cutDistance = -20.0, .feedInfeed = 0.5 };
    auto r = mgo::resolveMultiPass(setup, featurePositions());
    REQUIRE(r.error.empty());
    REQUIRE(r.range.cutStart == Approx(0.5));
    REQUIRE(r.range.cutEnd == Approx(-20.0)); // (the distance is from the work's end)
    REQUIRE(r.range.feedStart == Approx(7.5));
    REQUIRE(r.range.feedEnd == Approx(7.0));

    // The other way
    setup.cutDistance = 20.0;
    r = mgo::resolveMultiPass(setup, featurePositions());
    REQUIRE(r.range.cutStart == Approx(-0.5));
    REQUIRE(r.range.cutEnd == Approx(20.0));

    // To a shoulder in a memory slot (Mem 1, Z 2): the direction is from that
    setup = { .startFeature = true, .cutEndMemory = 0, .feedInfeed = 0.5 };
    r = mgo::resolveMultiPass(setup, featurePositions());
    REQUIRE(r.error.empty());
    REQUIRE(r.range.cutStart == Approx(-0.5));
    REQUIRE(r.range.cutEnd == Approx(2.0));

    // A different clearance
    auto positions = featurePositions();
    positions.featureClearance = 1.0;
    r = mgo::resolveMultiPass(setup, positions);
    REQUIRE(r.range.cutStart == Approx(-1.0));

    // Can't be used without the feature (both values), or ending at the work's end
    positions = featurePositions();
    positions.featureWorkEnd.reset();
    REQUIRE(!mgo::resolveMultiPass(setup, positions).error.empty());
    positions = featurePositions();
    positions.featureFeed.reset();
    REQUIRE(!mgo::resolveMultiPass(setup, positions).error.empty());
    setup = { .startFeature = true, .cutDistance = 0.0 };
    REQUIRE(mgo::resolveMultiPass(setup, featurePositions()).error
            == "The Z end is at the work's end");
}

TEST_CASE("MultiPass: the dialog starts from the feature, if it's set")
{
    mgo::MultiPassSetup previous { .cutDistance = -7.0 };
    // At Mem 2: a memory slot still comes first
    auto s = mgo::defaultMultiPassSetup(previous, featurePositions());
    REQUIRE(s.startMemory == 1u);
    REQUIRE(!s.startFeature);
    // Elsewhere: the feature
    auto positions = featurePositions();
    positions.cutHere = 11.0;
    s = mgo::defaultMultiPassSetup(previous, positions);
    REQUIRE(!s.startMemory);
    REQUIRE(s.startFeature);
    // Not without the feature (even if used last time)
    positions.featureWorkEnd.reset();
    previous.startFeature = true;
    s = mgo::defaultMultiPassSetup(previous, positions);
    REQUIRE(!s.startFeature);
}

TEST_CASE("Model:   the current feature is off unless enabled")
{
    mgo::MockConfigReader config;
    mgo::MockGpio gpio(false);
    mgo::Model model(gpio, config);
    model.initialise();
    REQUIRE(!model.currentFeatureEnabled());
    model.setAxis2Position(7.5);
    model.diameterIsSet();
    model.markWorkEnd();
    REQUIRE(!model.getFeatureDiameterMm());
    REQUIRE(!model.getWorkEndZ());
    REQUIRE(!model.recordFeatureDiameter());
    REQUIRE(model.getWarning().empty());
    REQUIRE(!model.multiPassPositions().featureFeed);
    // Nor in the menu
    for (const auto& c : mgo::buildCommands(config)) {
        REQUIRE(c.category != "Feature");
    }
}

TEST_CASE("Model:   setting and clearing the current feature")
{
    auto config = featureConfig();
    mgo::MockGpio gpio(false);
    mgo::Model model(gpio, config);
    model.initialise();
    REQUIRE(model.currentFeatureEnabled());
    REQUIRE(!model.getFeatureDiameterMm());
    REQUIRE(!model.getWorkEndZ());

    // Its diameter needs X set as a diameter
    REQUIRE(!model.recordFeatureDiameter());
    REQUIRE(!model.getWarning().empty());

    // Setting the diameter records it, and moving X doesn't change it
    model.setAxis2Position(7.5);
    model.diameterIsSet();
    REQUIRE(model.getFeatureDiameterMm() == Approx(15.0));
    model.setAxis2MotorSpeed(100.0);
    model.axis2GoToPosition(5.0);
    model.axis2Wait();
    REQUIRE(model.getDiameterMm() == Approx(10.0).margin(0.01));
    REQUIRE(model.getFeatureDiameterMm() == Approx(15.0));
    // ...until recorded again
    REQUIRE(model.recordFeatureDiameter());
    REQUIRE(model.getFeatureDiameterMm() == Approx(10.0).margin(0.01));

    // The work's end is Z here
    model.setAxis1MotorSpeed(100.0);
    model.axis1GoToPosition(3.0);
    model.axis1Wait();
    model.markWorkEnd();
    REQUIRE(model.getWorkEndZ() == Approx(3.0).margin(0.01));
    const auto positions = model.multiPassPositions();
    REQUIRE(positions.featureWorkEnd == Approx(3.0).margin(0.01));
    REQUIRE(positions.featureFeed == Approx(5.0).margin(0.01));
    REQUIRE(positions.featureClearance == Approx(0.5));

    // Each axis's value goes when its position does
    model.axis1Zero();
    REQUIRE(!model.getWorkEndZ());
    REQUIRE(model.getFeatureDiameterMm());
    model.axis2Zero();
    REQUIRE(!model.getFeatureDiameterMm());

    model.setAxis2Position(7.5);
    model.diameterIsSet();
    model.setAxis2Position(7.0); // (not as a diameter)
    REQUIRE(!model.getFeatureDiameterMm());

    // Clearing it
    model.setAxis2Position(7.5);
    model.diameterIsSet();
    model.markWorkEnd();
    model.clearFeature();
    REQUIRE(!model.getFeatureDiameterMm());
    REQUIRE(!model.getWorkEndZ());
}

TEST_CASE("Model:   a motor fault clears that axis's part of the current feature")
{
    TestConfigReader config({ { "CurrentFeature", "true" },
                              { "Axis1AlarmPin", "17" },
                              { "Axis1AlarmActiveLevel", "low" } });
    mgo::MockGpio gpio(false);
    mgo::Model model(gpio, config);
    model.initialise();
    model.setAxis2Position(7.5);
    model.diameterIsSet();
    model.markWorkEnd();
    gpio.setInputPinLevel(ALARM_PIN, mgo::PinState::low);
    model.setAxis1MotorSpeed(100.0);
    model.axis1GoToPosition(1.0);
    REQUIRE(waitFor([&] {
        model.checkStatus();
        return !model.getFaultMessage().empty();
    }));
    REQUIRE(!model.getWorkEndZ());
    REQUIRE(model.getFeatureDiameterMm() == Approx(15.0));
}

TEST_CASE("Model:   a multi-pass from the work's end sets the feature's diameter")
{
    auto config = featureConfig();
    mgo::MockGpio gpio(false);
    mgo::Model model(gpio, config);
    model.initialise();
    model.axis1SetSpeed(200.0);
    model.axis2SetSpeed(100.0);
    // The work's end at Z 0, 1.0 diameter (X 0.5)
    model.setAxis2Position(0.5);
    model.diameterIsSet();
    model.markWorkEnd();

    // From the feature: Z 0.2 into the work (-Z), X in by 0.02
    model.changeMode(mgo::Mode::MultiPass);
    REQUIRE(model
                .setMultiPassSetup({ .startFeature = true,
                                     .cutDistance = -0.2,
                                     .feedInfeed = 0.02,
                                     .feedPerPass = 0.01 })
                .empty());
    REQUIRE(model.getMultiPassRange().cutStart == Approx(0.5));
    model.multiPassGo(); // (to the start)
    REQUIRE(runUntilAtStart(model));
    model.multiPassGo();
    REQUIRE(model.multiPass().inProgress());
    REQUIRE(runUntilStopped(model));
    REQUIRE(model.multiPass().state() == mgo::MultiPass::State::Finished);
    REQUIRE(model.getFeatureDiameterMm() == Approx(0.96).margin(0.0001));

    // Part way along (starting in the work, from Z -0.1): it's unchanged
    model.axis1GoToPosition(-0.1);
    model.axis1Wait();
    REQUIRE(model
                .setMultiPassSetup(
                    { .cutDistance = -0.1, .feedInfeed = 0.02, .feedPerPass = 0.02 })
                .empty());
    model.multiPassGo();
    REQUIRE(model.multiPass().inProgress());
    REQUIRE(runUntilStopped(model));
    REQUIRE(model.multiPass().state() == mgo::MultiPass::State::Finished);
    REQUIRE(model.getAxis2MotorPosition() == Approx(0.46).margin(0.0001));
    REQUIRE(model.getFeatureDiameterMm() == Approx(0.96).margin(0.0001));
}

TEST_CASE("Commands: the current feature's")
{
    auto config = featureConfig();
    const auto commands = mgo::buildCommands(config);
    const auto find = [&commands](int k) {
        return std::find_if(
            commands.begin(), commands.end(), [k](const mgo::Command& c) { return c.key == k; });
    };
    REQUIRE(find(mgo::key::a1_e) != commands.end());
    REQUIRE(find(mgo::key::a2_d) != commands.end());
    REQUIRE(find(mgo::key::clearFeature) != commands.end());
    REQUIRE(find(mgo::key::clearFeature)->category == "Feature");
    REQUIRE(mgo::categoryHotkey("Feature") == 'f');
    REQUIRE(mgo::shortcutText(mgo::key::a1_e, config) == "z e");
    REQUIRE(mgo::shortcutText(mgo::key::a2_d, config) == "x d");
    REQUIRE(mgo::shortcutText(mgo::key::clearFeature, config).empty()); // (menu only)
    // Clear (menu only) isn't among the keys following a leader
    for (const auto mode : { mgo::KeyMode::Axis1, mgo::KeyMode::Axis2, mgo::KeyMode::AxisAll }) {
        for (const auto* c : mgo::commandsForLeader(commands, mode)) {
            REQUIRE(c->key != mgo::key::clearFeature);
        }
    }

    // What's available depends on the model
    mgo::MockGpio gpio(false);
    mgo::Model model(gpio, config);
    model.initialise();
    REQUIRE(!mgo::isAvailable(*find(mgo::key::a2_d), model));
    REQUIRE(!mgo::isAvailable(*find(mgo::key::clearFeature), model));
    model.setAxis2Position(7.5);
    model.diameterIsSet();
    REQUIRE(mgo::isAvailable(*find(mgo::key::a2_d), model));
    REQUIRE(mgo::isAvailable(*find(mgo::key::clearFeature), model));
}

namespace {

// A model with the current feature: the work's end at Z 0, 1.0 diameter
// (X 0.5)
struct FeatureModel {
    TestConfigReader config;
    mgo::MockGpio gpio { false };
    mgo::Model model { gpio, config };
    explicit FeatureModel(std::map<std::string, std::string> extra = {})
        : config([&extra] {
            extra["CurrentFeature"] = "true";
            return extra;
        }())
    {
        model.initialise();
        model.axis1SetSpeed(200.0);
        model.axis2SetSpeed(100.0);
        model.setAxis2Position(0.5);
        model.diameterIsSet();
        model.markWorkEnd();
    }
    // X's position this far deeper into the work than the feature's surface
    double deeper(double mm)
    {
        return 0.5 + model.multiPassPositions().feedIntoWork * mm;
    }
    void moveTo(double z, double x)
    {
        model.axis1GoToPosition(z);
        model.axis2GoToPosition(x);
        REQUIRE(runUntilStopped(model));
    }
};

} // end anonymous namespace

TEST_CASE("Model:   where the work's end is, clear of it or exactly")
{
    FeatureModel f;
    REQUIRE(f.model.featureWorkEndTarget(false) == Approx(0.0));
    // Away from the chuck, which is -Z unless set otherwise
    REQUIRE(f.model.featureWorkEndTarget(true) == Approx(0.5));
    FeatureModel other({ { "Axis1TowardsChuck", "positive" }, { "FeatureStartClearance", "1" } });
    REQUIRE(other.model.featureWorkEndTarget(true) == Approx(-1.0));
    other.model.clearWorkEnd();
    REQUIRE(!other.model.featureWorkEndTarget(true));
}

TEST_CASE("Model:   going to the feature's start, X clear of the work while Z moves")
{
    FeatureModel f;
    // Part way along, deeper than the feature's surface
    f.moveTo(-0.3, f.deeper(0.2));
    REQUIRE(f.model.featureGoToAllowed());
    const double intoWork = f.model.multiPassPositions().feedIntoWork;
    f.model.featureGoToStart(true);
    REQUIRE(f.model.getWarning().empty());
    REQUIRE(f.model.isMotionSequenceRunning());
    bool zMovedWithXOut = true;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (f.model.isMotionSequenceRunning()) {
        REQUIRE(std::chrono::steady_clock::now() < deadline);
        f.model.checkStatus();
        if (f.model.isAxis1MotorRunning()) {
            // (Further out than the surface)
            zMovedWithXOut
                = zMovedWithXOut && (f.model.getAxis2MotorPosition() - 0.5) * intoWork < 0.0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    REQUIRE(runUntilStopped(f.model));
    REQUIRE(zMovedWithXOut);
    REQUIRE(f.model.getAxis1MotorPosition() == Approx(0.5).margin(0.0001));
    REQUIRE(f.model.getAxis2MotorPosition() == Approx(0.5).margin(0.0001));
    REQUIRE(!f.model.getIsAxis2Retracted());

    // At the end exactly
    f.moveTo(-0.3, f.deeper(0.2));
    f.model.featureGoToStart(false);
    REQUIRE(runUntilStopped(f.model));
    REQUIRE(f.model.getAxis1MotorPosition() == Approx(0.0).margin(0.0001));
    REQUIRE(f.model.getAxis2MotorPosition() == Approx(0.5).margin(0.0001));
}

TEST_CASE("Model:   X to the feature's diameter, or Z to the work's end")
{
    FeatureModel f;
    // X only
    f.moveTo(-0.3, f.deeper(0.2));
    f.model.featureGoToDiameter();
    REQUIRE(runUntilStopped(f.model));
    REQUIRE(f.model.getAxis1MotorPosition() == Approx(-0.3).margin(0.0001));
    REQUIRE(f.model.getAxis2MotorPosition() == Approx(0.5).margin(0.0001));

    // Z only, but not with X deeper than the surface
    f.moveTo(-0.3, f.deeper(0.2));
    f.model.featureGoToWorkEnd(true);
    REQUIRE(f.model.getWarning().find("clear of the work") != std::string::npos);
    REQUIRE(runUntilStopped(f.model));
    REQUIRE(f.model.getAxis1MotorPosition() == Approx(-0.3).margin(0.0001));
    // On the surface, or further out, it goes (and X stays put)
    f.moveTo(-0.3, 0.5);
    f.model.featureGoToWorkEnd(true);
    REQUIRE(f.model.getWarning().empty());
    REQUIRE(runUntilStopped(f.model));
    REQUIRE(f.model.getAxis1MotorPosition() == Approx(0.5).margin(0.0001));
    REQUIRE(f.model.getAxis2MotorPosition() == Approx(0.5).margin(0.0001));
    f.moveTo(-0.3, f.deeper(-0.2));
    f.model.featureGoToWorkEnd(false);
    REQUIRE(runUntilStopped(f.model));
    REQUIRE(f.model.getAxis1MotorPosition() == Approx(0.0).margin(0.0001));
    REQUIRE(f.model.getAxis2MotorPosition() == Approx(f.deeper(-0.2)).margin(0.0001));
}

TEST_CASE("Model:   going to the feature needs it, and isn't allowed in some modes")
{
    FeatureModel f;
    f.model.changeMode(mgo::Mode::Threading);
    REQUIRE(!f.model.featureGoToAllowed());
    f.model.changeMode(mgo::Mode::Taper);
    REQUIRE(!f.model.featureGoToAllowed());
    f.model.changeMode(mgo::Mode::None);
    REQUIRE(f.model.featureGoToAllowed());

    // The menu's items are available only with what they go to
    const auto commands = mgo::buildCommands(f.config);
    const auto available = [&](int k) {
        const auto it = std::find_if(
            commands.begin(), commands.end(), [k](const mgo::Command& c) { return c.key == k; });
        REQUIRE(it != commands.end());
        return mgo::isAvailable(*it, f.model);
    };
    REQUIRE(available(mgo::key::featureStart));
    REQUIRE(available(mgo::key::featureEndExact));
    f.model.clearWorkEnd();
    REQUIRE(!available(mgo::key::featureStart));
    REQUIRE(!available(mgo::key::featureEnd));
    REQUIRE(available(mgo::key::featureDiameter));
    f.model.featureGoToStart(true);
    REQUIRE(!f.model.getWarning().empty());
    REQUIRE(!f.model.isMotionSequenceRunning());
}
