#include "configreader.h"
#include "commands.h"
#include "joystick.h"
#include "keycodes.h"
#include "log.h"
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
// have stopped. Returns false if that took too long
bool runUntilStopped(mgo::Model& model)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (;;) {
        model.checkStatus();
        if (!model.isMotionSequenceRunning() && !model.isAxis1MotorRunning()
            && !model.isAxis2MotorRunning()) {
            return true;
        }
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
    REQUIRE(mgo::shortcutText(mgo::key::f2t, lathe) == "F2 t");
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
    model.setCurrentDisplayMode(mgo::Mode::Setup);
    REQUIRE(!mgo::keyAllowedInCurrentMode(mgo::key::a1_g, model));
    REQUIRE(mgo::keyAllowedInCurrentMode(mgo::key::LEFT, model));
    REQUIRE(mgo::keyAllowedInCurrentMode(mgo::key::ESC, model));
    model.setCurrentDisplayMode(mgo::Mode::None);
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

    // M (Modes), then T (Threading): as F2 T
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
    model.setCurrentDisplayMode(mgo::Mode::Setup); // only motion allowed
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
    // The F2 codes look as if they have the axis 1 leader's bit set
    REQUIRE(!axis1.contains(mgo::key::f2t));

    const auto axis2 = keysFor(mgo::KeyMode::Axis2);
    REQUIRE(axis2.contains(mgo::key::a2_s));
    REQUIRE(!axis2.contains(mgo::key::a1_s));

    const auto both = keysFor(mgo::KeyMode::AxisAll);
    REQUIRE(both.contains(mgo::key::aAll_z));
    REQUIRE(!both.contains(mgo::key::a1_z));

    const auto function = keysFor(mgo::KeyMode::Function);
    REQUIRE(function.contains(mgo::key::f2t));
    REQUIRE(function.contains(mgo::key::f2p));
    REQUIRE(!function.contains(mgo::key::a1_g));

    REQUIRE(keysFor(mgo::KeyMode::None).empty());

    // The key to press after the leader
    REQUIRE(mgo::followUpKeyName(mgo::key::a1_g) == "g");
    REQUIRE(mgo::followUpKeyName(mgo::key::a2_ENTER) == "Enter");
    REQUIRE(mgo::followUpKeyName(mgo::key::aAll_DELETE) == "Delete");
    REQUIRE(mgo::followUpKeyName(mgo::key::a1_FULLSTOP) == ".");
    REQUIRE(mgo::followUpKeyName(mgo::key::f2t) == "t");
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
    void cutMoveTo(double position) override
    {
        log.push_back(fmt::format("cut {:.6g} at {:.6g}", position, m_cutSpeed));
        m_cutTarget = position;
    }
    double cutSpeed() const override { return m_cutSpeed; }
    void setCutSpeed(double speed) override { m_cutSpeed = speed; }
    double cutMaxSpeed() const override { return 1'000.0; }
    double feedPosition() const override { return m_feed; }
    bool feedMoving() const override { return m_feedTarget.has_value(); }
    void feedMoveTo(double position) override
    {
        log.push_back(fmt::format("feed {:.6g}", position));
        m_feedTarget = position;
    }
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
            m_cut = cutStopsAt.value_or(*m_cutTarget);
            m_cutTarget.reset();
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

private:
    std::optional<double> m_cutTarget;
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
    REQUIRE(mp.state() == mgo::MultiPass::State::Cutting);
    REQUIRE(mp.pass() == 1);
    // 0.25 in steps of 0.1: three steps over, the last one smaller
    REQUIRE(mp.totalPasses() == 4);
    machine.run(mp);
    REQUIRE(mp.state() == mgo::MultiPass::State::Finished);
    REQUIRE(!mp.inProgress());
    REQUIRE(mp.pass() == 4);
    REQUIRE(mp.fed() == Approx(0.25));
    REQUIRE(mp.message().empty());
    // (Cuts at the cut speed; returns at full speed)
    REQUIRE(machine.log
            == Log { "cut -10 at 40", "cut 0 at 1000", "feed 4.9",
                     "cut -10 at 40", "cut 0 at 1000", "feed 4.8",
                     "cut -10 at 40", "cut 0 at 1000", "feed 4.75",
                     "cut -10 at 40", "cut 0 at 1000" });
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
    machine.setCutSpeed(25.0);
    machine.run(mp);
    REQUIRE(machine.log.at(3) == "cut -10 at 25");
    REQUIRE(machine.cutSpeed() == Approx(25.0));
}

TEST_CASE("MultiPass: stepping over outwards, and in one pass")
{
    FakeMultiPassMachine machine;
    mgo::MultiPass mp(machine);
    auto plan = basicPlan();
    plan.feedEnd = 5.2; // (e.g. boring)
    REQUIRE(mp.start(plan).empty());
    REQUIRE(mp.totalPasses() == 3);
    machine.run(mp);
    REQUIRE(machine.log.at(2) == "feed 5.1");
    REQUIRE(machine.log.at(5) == "feed 5.2");
    REQUIRE(mp.state() == mgo::MultiPass::State::Finished);

    // If the feed's start and end are the same, it's a single pass
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
    plan.feedEnd = 4.9;
    plan.retract = true;
    REQUIRE(mp.start(plan).empty());
    machine.run(mp);
    REQUIRE(mp.state() == mgo::MultiPass::State::Finished);
    // (Unretracted at the end, so the feed axis shows the final position)
    REQUIRE(machine.log
            == Log { "cut -10 at 40", "retract", "cut 0 at 1000", "unretract", "feed 4.9",
                     "cut -10 at 40", "retract", "cut 0 at 1000", "unretract" });
    REQUIRE(!machine.isRetracted());
    REQUIRE(machine.feedPosition() == Approx(4.9));
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
    REQUIRE(machine.log.size() == 2);
    mp.continueAfterPause();
    REQUIRE(mp.pass() == 2);
    REQUIRE(machine.log.back() == "feed 4.9");
    machine.run(mp);
    REQUIRE(mp.state() == mgo::MultiPass::State::Paused);
    mp.continueAfterPause();
    machine.run(mp);
    mp.continueAfterPause();
    REQUIRE(mp.nextFeed() == Approx(0.0)); // (the last pass: no more to feed)
    machine.run(mp);
    // (No pause after the last pass)
    REQUIRE(mp.state() == mgo::MultiPass::State::Finished);
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
    REQUIRE(mp.totalPasses() == 4);
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
    machine.finishMoves(-6.0);
    mp.update();
    REQUIRE(mp.state() == mgo::MultiPass::State::Idle);
    REQUIRE(!mp.message().empty());
    REQUIRE(machine.log.size() == 1); // (no return)
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
    model.setMultiPassOptions({ .feedPerPass = 0.01, .pause = false, .retract = true });
    // Keys which would move an axis aren't allowed while it runs
    REQUIRE(mgo::keyAllowedInCurrentMode(mgo::key::a, model));
    model.multiPassGo();
    REQUIRE(model.getWarning().empty());
    REQUIRE(model.multiPass().inProgress());
    REQUIRE(model.multiPass().totalPasses() == 4);
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
    REQUIRE(model.multiPass().pass() == 4);
    REQUIRE(model.getAxis1MotorPosition() == Approx(0.0).margin(0.0001));
    REQUIRE(model.getAxis2MotorPosition() == Approx(0.025).margin(0.0001));
    REQUIRE(!model.getIsAxis2Retracted());
    REQUIRE(model.getAxis1MotorSpeed() == Approx(200.0));
    REQUIRE(mgo::keyAllowedInCurrentMode(mgo::key::a, model));
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
    model.setMultiPassOptions({ .feedPerPass = 0.01 });

    // Not at Mem 1: it won't start
    model.setAxis1Position(0.5);
    model.multiPassGo();
    REQUIRE(!model.multiPass().inProgress());
    REQUIRE(!model.getWarning().empty());
    model.setAxis1Position(0.0);

    model.multiPassGo();
    REQUIRE(model.multiPass().inProgress());
    REQUIRE(model.getWarning().empty());
    model.checkStatus();
    REQUIRE(model.isAxis1MotorRunning());
    // Stopping the motors (e.g. Space) stops it
    model.stopAllMotors();
    REQUIRE(model.multiPass().state() == mgo::MultiPass::State::Idle);
    model.checkStatus();
    REQUIRE(!model.isAxis1MotorRunning());

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
    t.model.axis1GoToStep(5'000); // (5mm, i.e. five revolutions)
    REQUIRE(t.model.isAxis1MotorRunning());
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
        t.model.axis1GoToStep(end);
        REQUIRE(t.model.isAxis1MotorRunning());
        const int64_t started = t.pulsesWhenZStarts();
        INFO("Pass to " << end << " started at pulse " << started);
        REQUIRE(ThreadingTestModel::pulsesFromWholeRev(started) <= 5);
        REQUIRE(waitFor([&t] { return !t.model.isAxis1MotorRunning(); }, std::chrono::seconds(5)));
        REQUIRE(t.model.getAxis1MotorCurrentStep() == end);
        t.model.checkStatus();
    }
}

TEST_CASE("Threading: a pass stops if Z can't keep up with the spindle")
{
    ThreadingTestModel t(1'000);
    t.model.axis1GoToStep(20'000);
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
