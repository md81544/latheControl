#pragma once

#include <atomic>
#include <mutex>
#include <thread>

namespace mgo
{

class IGpio;

enum class Direction
{
    forward,
    reverse
};

class StepperMotor
{
public:
    StepperMotor( IGpio& gpio, long stepsPerRevolution );
    ~StepperMotor();
    bool isRunning() const;
    Direction getDirection() const;
    long getCurrentStep() const;
    // Call the current step position zero
    void zeroPosition();
    // Go to a specific step
    void goToStep( long step );
    // Set motor speed
    void setRpm( double rpm );
    // Get current delay value. Can't think of a
    // purpose for this apart from unit testing :)
    int getDelay();
    // Stop any motion
    void stop();
    // Block until the current operation completes
    void wait();
private:
    IGpio& m_gpio;
    long m_stepsPerRevolution;
    std::thread m_thread;
    std::atomic<bool>      m_terminateThread{ false };
    std::atomic<long>      m_targetStep{ 0 };
    std::atomic<bool>      m_busy{ false };
    std::atomic<long>      m_currentStep{ 0 };
    std::atomic<bool>      m_stop{ false };
    std::atomic<int>       m_delay{ 500 }; // µsecs
    std::atomic<Direction> m_direction{ Direction::forward };
    // lock should be taken before any code outside the
    // background thread changes any member variables
    std::mutex  m_mtx;
};

} // end namespace

