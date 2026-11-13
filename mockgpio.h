#pragma once

// Concrete implementation of the IGpio ABC,
// but mocked. Includes ability to write
// diags messages.

#include "igpio.h"

#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
#include <unistd.h>

namespace mgo {

// Settings for the mock's simulated hardware. The defaults are suitable
// for most testing.
struct MockGpioOptions {
    // Swap the mock rotary encoder's pins, i.e. reverse its direction
    bool rotaryDirectionNormal { false };
    // Delay between each simulated rotary encoder pulse
    long rotaryEncoderDelayMicroseconds { 250 };
    // Resolution of the simulated linear scale
    long linearScaleAxis1StepsPerMm { 200 };
};

class MockGpio final : public IGpio {
public:
    explicit MockGpio(bool printDiags, MockGpioOptions options = {})
        : m_print(printDiags)
        , m_options(options)
    {
        for (auto& high : m_inputPinHigh) {
            high = true;
        }
        print() << "Initialising GPIO library\n";
    }

    ~MockGpio()
    {
        print() << "Terminating GPIO library\n";
        clearRotaryEncoderCallback();
        clearLinearScaleAxis1Callback();
    }

    void clearRotaryEncoderCallback() override
    {
        m_terminateRotary = true;
        if (m_rotaryEncoderCallbackerThread.joinable()) {
            print() << "Waiting for rotary encoder callbacker thread to terminate\n";
            m_rotaryEncoderCallbackerThread.join();
        }
        m_terminateRotary = false;
    }

    void clearLinearScaleAxis1Callback() override
    {
        m_terminateScale = true;
        if (m_linearScaleAxis1CallbackerThread.joinable()) {
            print() << "Waiting for linear scale callbacker thread to terminate\n";
            m_linearScaleAxis1CallbackerThread.join();
        }
        m_terminateScale = false;
    }

    int addMotor(int, int, int) override
    {
        return m_motorCount++;
    }

    void setStepPin(int motor, PinState state) override
    {
        if (state == PinState::high) {
            print() << "Setting step pin HIGH for motor " << motor << "\n";
        } else {
            print() << "Setting step pin LOW for motor " << motor << "\n";
        }
    }

    void setReversePin(int motor, PinState state) override
    {
        if (state == PinState::high) {
            print() << "Setting reverse pin HIGH for motor " << motor << "\n";
        } else {
            print() << "Setting reverse pin LOW for motor " << motor << "\n";
        }
    }

    void setEnablePin(int motor, PinState state) override
    {
        if (state == PinState::high) {
            print() << "Setting enable pin HIGH for motor " << motor << "\n";
        } else {
            print() << "Setting enable pin LOW for motor " << motor << "\n";
        }
    }

    uint32_t getTick() override
    {
        // (steady_clock, as the system clock can be adjusted while we run)
        return std::chrono::duration_cast<std::chrono::microseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    void setInputPin(int pin, bool) override
    {
        print() << "Setting pin " << pin << " as an input\n";
    }

    PinState readPin(int pin) override
    {
        if (pin < 0 || pin >= static_cast<int>(m_inputPinHigh.size())) {
            return PinState::high;
        }
        return m_inputPinHigh[pin] ? PinState::high : PinState::low;
    }

    // Mock-only: set the level an input pin reads as (e.g. to simulate a
    // motor driver's alarm output). Inputs read high until set, as a
    // pulled-up input with nothing driving it would
    void setInputPinLevel(int pin, PinState state)
    {
        if (pin >= 0 && pin < static_cast<int>(m_inputPinHigh.size())) {
            m_inputPinHigh[pin] = state == PinState::high;
        }
    }

    // Changes the delay between the simulated rotary encoder's pulses (i.e.
    // the spindle's speed) while it runs
    void setRotaryEncoderDelayMicroseconds(long delay)
    {
        m_rotaryEncoderDelayMicroseconds = delay;
    }

    void stopRotaryEncoderCallback()
    {
        // class-specific function to allow us to stop the
        // callbacker manually
        print() << "Stopping callbacker\n";
        m_terminateRotary = true;
    }

    void scaleGoToPositionMm(double mm) override
    {
        m_targetPosition = mm * m_options.linearScaleAxis1StepsPerMm;
    }

    void scaleSetSpeedStepsPerSec(double speed) override
    {
        // Note the final multiplication by a "fudge factor" to account for delays
        // introduced by the rest of the code. As this is for testing/faking it's not
        // really important, but it just brings the results from the mock linear scale
        // closer to the dead reckoning values.
        double nonZeroSpeed = speed > 0.0 ? speed : 10.0;
        m_microsecsPerStep = ((1.0 / nonZeroSpeed) * 1'000'000) * 0.855;
    }

    void scaleStop() override
    {
        m_targetPosition = m_stepCount;
    }

    void setRotaryEncoderCallback(
        int pinA,
        int pinB,
        void (*callback)(int, int, uint32_t, void*),
        void* userData) override
    {
        // Replacing a running thread would otherwise std::terminate()
        clearRotaryEncoderCallback();
        int pin1 = pinA;
        int pin2 = pinB;
        if (m_options.rotaryDirectionNormal) {
            pin1 = pinB;
            pin2 = pinA;
        }
        m_rotaryEncoderDelayMicroseconds = m_options.rotaryEncoderDelayMicroseconds;
        auto t = std::thread([=, this]() {
            using namespace std::chrono;
            // Each pulse is due a delay after the one before was due, and is
            // given that time as its tick, whenever this thread gets to send
            // it. That's as pigpio does: it samples the pins, and its
            // callbacks pass the time of each edge, however late they run.
            // Otherwise a late wake-up would look like a gap in the pulses,
            // which the rotary encoder takes as lost pulses; and as a sleep
            // can overshoot (on Linux, a 50 us sleep takes over 100 us, and a
            // busy desktop can delay a wake-up by a few ms), the spindle would
            // run slower than asked for, by an amount varying from machine to
            // machine. A late pulse is followed at once by any others that are
            // due, so the average speed is the one asked for
            auto due = steady_clock::now();
            for (;;) {
                try {
                    const auto tick = static_cast<uint32_t>(
                        duration_cast<microseconds>(due.time_since_epoch()).count());
                    if (m_terminateRotary) {
                        break;
                    }
                    callback(pin1, 1, tick, userData);
                    if (m_terminateRotary) {
                        break;
                    }
                    callback(pin2, 1, tick, userData);
                    if (m_terminateRotary) {
                        break;
                    }
                    callback(pin1, 0, tick, userData);
                    if (m_terminateRotary) {
                        break;
                    }
                    callback(pin2, 0, tick, userData);
                    due += microseconds(m_rotaryEncoderDelayMicroseconds.load());
                    const auto now = steady_clock::now();
                    if (now - due > milliseconds(100)) {
                        // Far behind (this thread wasn't run for a long
                        // time): carry on from now, rather than send a
                        // long burst of pulses, all at once, to catch up
                        due = now;
                    } else if (due > now) {
                        std::this_thread::sleep_until(due);
                    }
                } catch (const std::exception& e) {
                    print() << e.what() << std::endl;
                    break;
                }
            }
            print() << "Rotary encoder callbacker thread ended\n";
        });
        m_rotaryEncoderCallbackerThread.swap(t);
    }

    void setLinearScaleAxis1Callback(
        int pinA,
        int pinB,
        void (*callback)(int, int, uint32_t, void*),
        void* userData) override
    {
        // Replacing a running thread would otherwise std::terminate()
        clearLinearScaleAxis1Callback();
        auto t = std::thread([=, this]() {
            using namespace std::chrono;
            for (;;) {
                try {
                    if (m_terminateScale) {
                        break;
                    }
                    // Each pin change (which doesn't replicate the previous state) counts as a
                    // step. We keep track of steps internally in the mock (the real object leaves
                    // its owner to do that) so we can make the mock 'move' programmatically.
                    if (m_stepCount < m_targetPosition) {
                        callback(pinA, 1, getTick(), userData);
                        if (++m_stepCount == m_targetPosition) {
                            continue;
                        }
                        if (m_terminateScale) {
                            break;
                        }
                        std::this_thread::sleep_for(microseconds(m_microsecsPerStep));
                        callback(pinB, 1, getTick(), userData);
                        if (++m_stepCount == m_targetPosition) {
                            continue;
                        }
                        if (m_terminateScale) {
                            break;
                        }
                        std::this_thread::sleep_for(microseconds(m_microsecsPerStep));
                        callback(pinA, 0, getTick(), userData);
                        if (++m_stepCount == m_targetPosition) {
                            continue;
                        }
                        if (m_terminateScale) {
                            break;
                        }
                        std::this_thread::sleep_for(microseconds(m_microsecsPerStep));
                        callback(pinB, 0, getTick(), userData);
                        if (++m_stepCount == m_targetPosition) {
                            continue;
                        }
                        if (m_terminateScale) {
                            break;
                        }
                        std::this_thread::sleep_for(microseconds(m_microsecsPerStep));
                    } else if (m_stepCount > m_targetPosition) {
                        callback(pinB, 0, getTick(), userData);
                        if (--m_stepCount == m_targetPosition) {
                            continue;
                        }
                        if (m_terminateScale) {
                            break;
                        }
                        std::this_thread::sleep_for(microseconds(m_microsecsPerStep));
                        callback(pinA, 0, getTick(), userData);
                        if (--m_stepCount == m_targetPosition) {
                            continue;
                        }
                        if (m_terminateScale) {
                            break;
                        }
                        std::this_thread::sleep_for(microseconds(m_microsecsPerStep));
                        callback(pinB, 1, getTick(), userData);
                        if (--m_stepCount == m_targetPosition) {
                            continue;
                        }
                        if (m_terminateScale) {
                            break;
                        }
                        std::this_thread::sleep_for(microseconds(m_microsecsPerStep));
                        callback(pinA, 1, getTick(), userData);
                        if (--m_stepCount == m_targetPosition) {
                            continue;
                        }
                        if (m_terminateScale) {
                            break;
                        }
                        std::this_thread::sleep_for(microseconds(m_microsecsPerStep));
                    } else {
                        // Not moving so small-ish sleep just to avoid spinning
                        std::this_thread::sleep_for(milliseconds(50));
                        if (m_terminateScale) {
                            break;
                        }
                    }
                } catch (const std::exception& e) {
                    print() << e.what() << std::endl;
                    break;
                }
            }
            print() << "Linear scale axis 1 callbacker thread ended\n";
        });
        m_linearScaleAxis1CallbackerThread.swap(t);
    }

    void delayMicroSeconds(long usecs) override
    {
        print() << "Sleeping for " << usecs << " usecs\n";
        usleep(usecs);
    }

private:
    // Simulated input pin levels (atomic, as read from motor threads)
    std::array<std::atomic<bool>, 64> m_inputPinHigh;
    std::atomic<bool> m_terminateRotary { false };
    std::atomic<bool> m_terminateScale { false };
    std::atomic<double> m_targetPosition { 0.0 };
    std::atomic<double> m_microsecsPerStep { 0.01 };
    std::thread m_rotaryEncoderCallbackerThread;
    std::atomic<long> m_rotaryEncoderDelayMicroseconds { 250 };
    std::thread m_linearScaleAxis1CallbackerThread;
    int32_t m_stepCount {
        0
    }; // We keep count of steps in this mock to allow us to move to a specific location for testing

    int m_motorCount { 0 };

    bool m_print;
    MockGpioOptions m_options;
    std::fstream m_devNull;
    std::ostream& print()
    {
        if (m_print) {
            return std::cout;
        }
        return m_devNull;
    }
};

} // end namespace
