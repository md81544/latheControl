#pragma once

#include <atomic>
#include <memory>
#include <thread>

namespace mgo
{

enum class Direction
{
    forward,
    reverse
};

class StepperMotor : public IStepperMotor
{
public:
    StepperMotor( const &IGpio gpio );
    ~StepperMotor();
    bool isRunning() override;
    Direction getDirection() override;
    long getCurrentStep() override;
    // Go to a specific step
    void goToStep( long step ) override;
    // Set motor speed
    void setSpeedPercent( int percent ) override;
    // Stop any motion
    void stop() override;
    // Block until the current operation completes
    void wait() override;
private:
    const &IGpio& m_gpio;
};

} // end namespace

