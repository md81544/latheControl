#include "gpio.h"

#include <pigpio.h>

namespace mgo {

Gpio::Gpio()
{
    if (gpioInitialise() < 0) {
        throw GpioException("Could not initialise pigpio library");
    }
}

Gpio::~Gpio()
{
    clearRotaryEncoderCallback();
    clearLinearScaleAxis1Callback();
    gpioTerminate();
}

int Gpio::addMotor(int stepPin, int reversePin, int enablePin)
{
    m_stepPins.push_back(stepPin);
    m_reversePins.push_back(reversePin);
    m_enablePins.push_back(enablePin);
    return static_cast<int>(m_stepPins.size() - 1);
}

void Gpio::setStepPin(int motor, PinState state)
{
    gpioWrite(m_stepPins.at(motor), state == PinState::high ? 1 : 0);
}

void Gpio::setReversePin(int motor, PinState state)
{
    gpioWrite(m_reversePins.at(motor), state == PinState::high ? 1 : 0);
}

void Gpio::setEnablePin(int motor, PinState state)
{
    gpioWrite(m_enablePins.at(motor), state == PinState::high ? 1 : 0);
}

void Gpio::setRotaryEncoderCallback(
    int pinA,
    int pinB,
    void (*callback)(int, int, uint32_t, void*),
    void* user)
{
    setCallback(m_rotaryEncoderPins, pinA, pinB, callback, user);
}

void Gpio::setLinearScaleAxis1Callback(
    int pinA,
    int pinB,
    void (*callback)(int, int, uint32_t, void*),
    void* user)
{
    setCallback(m_linearScaleAxis1Pins, pinA, pinB, callback, user);
}

void Gpio::clearRotaryEncoderCallback()
{
    clearCallback(m_rotaryEncoderPins);
}

void Gpio::clearLinearScaleAxis1Callback()
{
    clearCallback(m_linearScaleAxis1Pins);
}

void Gpio::clearCallback(CallbackPins& pins)
{
    if (pins.pinA < 0) {
        return; // not set
    }
    gpioSetAlertFuncEx(pins.pinA, nullptr, nullptr);
    gpioSetAlertFuncEx(pins.pinB, nullptr, nullptr);
    pins = CallbackPins {};
    // pigpio's alert thread doesn't synchronise with gpioSetAlertFuncEx, so a
    // callback may already be in flight. It dispatches roughly every
    // millisecond, so wait long enough for any such call to have finished.
    gpioDelay(10'000);
}

void Gpio::setCallback(
    CallbackPins& pins,
    int pinA,
    int pinB,
    void (*callback)(int, int, uint32_t, void*),
    void* user)
{
    pins.pinA = pinA;
    pins.pinB = pinB;
    // Set up callbacks for rotary encoder
    gpioSetMode(pinA, PI_INPUT);
    gpioSetMode(pinB, PI_INPUT);
    // pull up is needed as encoder common is grounded
    gpioSetPullUpDown(pinA, PI_PUD_UP);
    gpioSetPullUpDown(pinB, PI_PUD_UP);
    // monitor encoder level changes
    gpioSetAlertFuncEx(pinA, callback, user);
    gpioSetAlertFuncEx(pinB, callback, user);
}

void Gpio::setInputPin(int pin, bool pullUp)
{
    gpioSetMode(pin, PI_INPUT);
    gpioSetPullUpDown(pin, pullUp ? PI_PUD_UP : PI_PUD_OFF);
}

PinState Gpio::readPin(int pin)
{
    // gpioRead() just reads a register, so is cheap enough to call every step
    return gpioRead(pin) ? PinState::high : PinState::low;
}

void Gpio::delayMicroSeconds(long usecs)
{
    gpioDelay(usecs);
}

uint32_t Gpio::getTick()
{
    return gpioTick();
}

} // end namespace
