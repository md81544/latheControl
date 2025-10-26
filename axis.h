#pragma once

#include "configreader.h"
#include "stepperControl/igpio.h"
#include "stepperControl/steppermotor.h"

#include <string>

namespace mgo {

class Axis {
public:
    explicit Axis(
        mgo::IConfigReader& config,
        unsigned number,
        const std::string& name,
        const std::string& units,
        IGpio& gpio,
        long stepsPerRevolution,
        double conversionFactor,
        double maxRpm,
        bool usingMockLinearScale);

    unsigned number() const;
    std::string name() const;
    std::string units() const;
    double maxRpm() const;
    void goToPreviousPosition() const;
    void repeatLastRelativeMove() const;
    void setSpeed(double speed);
    void speedIncrease();
    void speedDecrease();
    void nudge(Direction) const;
    void storePosition();
    void goToCurrentMemory() const;
    void move(Direction) const;
    void rapid(Direction) const;
    bool isMotorRunning() const;
    void fastReturn() const;
    void zero();
    void retract();

private:
    unsigned m_number;
    std::string m_name;
    std::string m_units;
    double m_maxRpm;
    std::unique_ptr<mgo::StepperMotor> m_stepper;
};

} // namespace mgo