#pragma once

// Abstract base "interface" class
// to allow mocking out the hardware
// interface to allow the StepperMotor
// class to be used for tests
#include <stdint.h>

namespace mgo {

enum class PinState {
    high,
    low
};

// concrete classes can initialise / terminate
// the GPIO library in their ctors / dtors

class IGpio {
public:
    // Support for stepper motors:
    virtual int addMotor(int stepPin, int reversePin, int enablePin) = 0;
    virtual void setStepPin(int motor, PinState) = 0;
    virtual void setReversePin(int motor, PinState) = 0;
    virtual void setEnablePin(int motor, PinState) = 0;
    virtual void setRotaryEncoderCallback(
        int pinA,
        int pinB,
        void (*callback)(int, int, uint32_t, void*),
        void* userData) = 0;
    virtual void setLinearScaleAxis1Callback(
        int pinA,
        int pinB,
        void (*callback)(int, int, uint32_t, void*),
        void* userData) = 0;
    // Stop calling back. Once these return, the callback will not be called
    // again, so the userData object can safely be destroyed. (No-op by default
    // so that existing implementations don't have to provide them.)
    virtual void clearRotaryEncoderCallback() { }
    virtual void clearLinearScaleAxis1Callback() { }
    // General-purpose inputs (e.g. a motor driver's alarm output). Must be
    // called for a pin before reading it. pullUp enables the internal pull-up
    // resistor (e.g. for an open-collector or opto-isolated output)
    virtual void setInputPin(int pin, bool pullUp) = 0;
    // Safe to call from a motor thread
    virtual PinState readPin(int pin) = 0;
    // General:
    virtual void delayMicroSeconds(long) = 0;
    virtual uint32_t getTick() = 0;
    // These functions are for TESTING/FAKE only (to make the mocked linear scale appear to be
    // working) and should be a no-op on non-fake IGpio-derived classes:
    virtual void scaleGoToPositionMm(double mm) = 0;
    virtual void scaleSetSpeedStepsPerSec(double speed) = 0;
    virtual void scaleStop() = 0;
    virtual ~IGpio() = default;
};

} // end namespace
