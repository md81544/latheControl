#include "configreader.h"
#include "commands.h"
#include "joystick.h"
#include "keycodes.h"
#include "log.h"
#include "menu.h"
#include "model.h"
#include "rotaryencoder.h"
#include "stepperControl/mockgpio.h"
#include "stepperControl/steppermotor.h"

#include <algorithm>
#include <chrono>
#include <set>
#include <map>
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
    REQUIRE(menu.handleKey(mgo::key::H, model).key == mgo::key::F1);

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
