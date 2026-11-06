#include "configreader.h"
#include "log.h"
#include "model.h"
#include "rotaryencoder.h"
#include "stepperControl/mockgpio.h"
#include "stepperControl/steppermotor.h"

#include <chrono>
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

TEST_CASE("Stepper: Rotary Encoder Position Callback")
{
    mgo::MockGpio gpio(false);
    mgo::RotaryEncoder re(gpio, 23, 24, 2000, 35.f / 30.f);
    bool called = false;
    while (re.warmingUp()) {
        gpio.delayMicroSeconds(1'000);
    }
    re.callbackAtZeroDegrees([&]() {
        called = true;
    });
    REQUIRE(re.warmingUp() == false);
    REQUIRE(called == true);
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
    model.axis1Wait();
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
    model.axis1Wait();
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
    model.axis1Wait();
    model.axis2Wait();

    double pos = model.getAxis2MotorPosition();
    REQUIRE(pos < 1.05);
    REQUIRE(pos > 0.95);

    model.axis1GoToPosition(0.0);
    model.axis1Wait();

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

TEST_CASE("Encoder: Zero degrees callback with spindle stopped doesn't hang")
{
    FakeTickGpio gpio(1'000'000);
    mgo::RotaryEncoder re(gpio, RE_PIN_A, RE_PIN_B, 4, 1.f);
    feedEncoderPulse(re, 100); // ends warm-up, but long ago
    REQUIRE(re.warmingUp() == false);
    bool called = false;
    re.callbackAtZeroDegrees([&]() { called = true; });
    REQUIRE(called == false);
}

TEST_CASE("Encoder: Zero degrees callback before a full revolution doesn't hang")
{
    const uint32_t now = 1'000'000;
    FakeTickGpio gpio(now);
    mgo::RotaryEncoder re(gpio, RE_PIN_A, RE_PIN_B, 4, 1.f);
    // Enough pulses to see zero degrees, but not to measure a revolution
    for (uint32_t k = 0; k < 3; ++k) {
        feedEncoderPulse(re, now - 30'000 + k * 10'000);
    }
    bool called = false;
    re.callbackAtZeroDegrees([&]() { called = true; });
    REQUIRE(called == false);
}

TEST_CASE("Encoder: Zero degrees callback across tick wraparound")
{
    // Start the clock 20ms before the 32-bit tick counter wraps
    const uint32_t now = std::numeric_limits<uint32_t>::max() - 20'000 + 1;
    FakeTickGpio gpio(now);
    // 4 pulses per spindle rev, one pulse every 10ms => one rev = 40ms
    mgo::RotaryEncoder re(gpio, RE_PIN_A, RE_PIN_B, 4, 1.f);
    const uint32_t firstPulse = now - 60'000;
    for (uint32_t k = 0; k < 6; ++k) {
        feedEncoderPulse(re, firstPulse + k * 10'000);
    }
    const uint32_t lastZero = firstPulse + 50'000;
    REQUIRE(re.getRpm() == Approx(1'500.f));
    // The next zero degrees is at lastZero + 40ms, which is after the wrap
    uint32_t firedAt = 0;
    re.callbackAtZeroDegrees([&]() { firedAt = gpio.peekTick(); });
    const uint32_t sinceLastZero = firedAt - lastZero; // modular arithmetic
    REQUIRE(sinceLastZero >= 40'000);
    REQUIRE(sinceLastZero < 40'010);
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
