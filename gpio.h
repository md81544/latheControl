#pragma once

// Concrete implementation of the IGpio ABC

#include "igpio.h"

#include <stdexcept>
#include <vector>

namespace mgo {

class GpioException : public std::runtime_error {
public:
    explicit GpioException(const std::string& message)
        : std::runtime_error(message)
    {
    }
};

class Gpio final : public IGpio {
public:
    Gpio();
    virtual ~Gpio();
    int addMotor(int stepPin, int reversePin, int enablePin) override;
    void setStepPin(int motor, PinState) override;
    void setReversePin(int motor, PinState) override;
    void setEnablePin(int motor, PinState) override;
    void setRotaryEncoderCallback(
        int pinA,
        int pinB,
        void (*callback)(int, int, uint32_t, void*),
        void* userData) override;
    void setLinearScaleAxis1Callback(
        int pinA,
        int pinB,
        void (*callback)(int, int, uint32_t, void*),
        void* userData) override;
    void clearRotaryEncoderCallback() override;
    void clearLinearScaleAxis1Callback() override;
    void setInputPin(int pin, bool pullUp) override;
    PinState readPin(int pin) override;
    void delayMicroSeconds(long usecs) override;
    uint32_t getTick() override;

private:
    struct CallbackPins {
        int pinA { -1 };
        int pinB { -1 };
    };
    void clearCallback(CallbackPins& pins);
    // The following scale* functions are deliberately no-op as they are only used in the
    // mock gpio class to simulate the scale moving.
    void scaleGoToPositionMm(double) override { };
    void scaleSetSpeedStepsPerSec(double) override { };
    void scaleStop() override { };
    void setCallback(
        CallbackPins& pins,
        int pinA,
        int pinB,
        void (*callback)(int, int, uint32_t, void*),
        void* userData);
    std::vector<int> m_stepPins;
    std::vector<int> m_reversePins;
    std::vector<int> m_enablePins;
    CallbackPins m_rotaryEncoderPins;
    CallbackPins m_linearScaleAxis1Pins;
};

} // end namespace
