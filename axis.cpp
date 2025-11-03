#include "axis.h"

#include "fmt/format.h"

namespace mgo {

Axis::Axis(
    mgo::IConfigReader& config,
    unsigned number,
    const std::string& name,
    const std::string& units,
    IGpio& gpio,
    long stepsPerRevolution,
    double conversionFactor,
    double maxRpm,
    bool supportsRetract,
    bool usingMockLinearScale)
    : m_number(number)
    , m_name(name)
    , m_units(units)
    , m_maxRpm(maxRpm)
    , m_supportsRetract(supportsRetract)
{
    const std::string configPrefix = fmt::format("Axis{}", number);
    const double maxSpeed
        = config.readDouble(fmt::format("{}MaxMotorSpeed", configPrefix), 1'000.0);
    m_stepper = std::make_unique<mgo::StepperMotor>(
        gpio,
        config.readLong(fmt::format("{}GpioStepPin", configPrefix), 8),
        config.readLong(fmt::format("{}GpioReversePin", configPrefix), 7),
        config.readLong(fmt::format("{}GpioEnablePin", configPrefix), 0),
        stepsPerRevolution,
        conversionFactor,
        std::abs(maxSpeed / conversionFactor / stepsPerRevolution),
        config.readDouble(fmt::format("{}RampingSpeed", configPrefix), 100.0),
        usingMockLinearScale,
        config.readLong(fmt::format("LinearScale{}StepsPerMM", configPrefix), 200));
}

unsigned Axis::number() const
{
    return m_number;
}

std::string Axis::name() const
{
    return m_name;
}

std::string Axis::units() const
{
    return m_units;
}

double Axis::maxRpm() const
{
    return m_maxRpm;
}

void Axis::goToPreviousPosition() const { }

void Axis::repeatLastRelativeMove() const { }

void Axis::setSpeed(double speed)
{
    m_stepper->setSpeed(speed);
}

void Axis::speedIncrease() { }

void Axis::speedDecrease() { }

void Axis::nudge(Direction) const { }

void Axis::storePosition() { }

void Axis::goToCurrentMemory() const { }

void Axis::move(Direction) const { }

void Axis::rapid(Direction) const { }

bool Axis::isMotorRunning() const
{
    return false;
}

void Axis::fastReturn() const { }

void Axis::zero() { }

void Axis::retract()
{
    if (!m_supportsRetract) {
        return;
    }
}

bool Axis::isRetracted()
{
    return m_retracted;
}

} // namespace mgo