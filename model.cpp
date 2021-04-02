#include "model.h"
#include "threadpitches.h"  // for ThreadPitch, threadPitches

#include "fmt/format.h"

namespace mgo
{
void Model::initialise()
{
    // TODO: all motor initialisation should happen here,
    // rather than in the controller's constructor (but
    // still called from there).
    m_axis1PreviousPositions.push( m_axis1Motor->getPosition() );
}


void Model::checkStatus()
{
    float chuckRpm = m_rotaryEncoder->getRpm();
    #ifndef FAKE
    if( m_spindleWasRunning && chuckRpm < 30.f )
    {
        // If the chuck has stopped, we stop X/Z motors as a safety
        // feature, just in case the motor has stalled or the operator
        // has turned it off because of some issue.
        // This won't stop the motors being started again even
        // if the chuck isn't moving.
        stopAllMotors();
    }
    #endif
    m_spindleWasRunning = chuckRpm > 30.f;

    if( m_enabledFunction == Mode::Threading )
    {
        // We are cutting threads, so the stepper motor's speed
        // is dependent on the spindle's RPM and the thread pitch.
        float pitch = threadPitches.at( m_threadPitchIndex ).pitchMm;
        // because my stepper motor / leadscrew does one mm per
        // revolution, there is a direct correlation between spindle
        // rpm and stepper motor rpm for a 1mm thread pitch.
        float speed = pitch * m_rotaryEncoder->getRpm();
        double maxZSpeed = m_config->readDouble( "Axis1MaxMotorSpeed", 700.0 );
        if( speed > maxZSpeed * 0.8 )
        {
            m_axis1Motor->stop();
            m_axis1Motor->wait();
            m_warning = "RPM too high for threading";
        }
        else
        {
            m_warning = "";
        }
        m_axis1Motor->setSpeed( speed );
    }

    if( m_currentDisplayMode == Mode::Taper )
    {
        if( m_input.empty() )
        {
            m_taperAngle = 0.0;
        }
        else
        {
            if( m_input != "-" )
            {
                try
                {
                    m_taperAngle = std::stod( m_input );
                    if( m_taperAngle > 60.0 )
                    {
                        m_taperAngle = 60.0;
                        m_input = "60.0";
                    }
                    else if( m_taperAngle < -60.0 )
                    {
                        m_taperAngle = -60.0;
                        m_input = "-60.0";
                    }
                }
                catch( const std::exception& )
                {
                    m_taperAngle = 0.0;
                    m_input = "";
                }
            }
        }
    }
    if( m_xDiameterSet )
    {
        m_generalStatus = fmt::format("Diameter: {: .3f} mm",
            std::abs( m_axis2Motor->getPosition() * 2 ) );
    }
    if ( ! m_axis2Motor->isRunning() )
    {
        m_axis2Status = "stopped";
    }
    if ( ! m_axis1Motor->isRunning() )
    {
        m_axis1Status = "stopped";
        if( m_zWasRunning )
        {
            // We see that axis1 has stopped. We save the position
            // in case the user wants to return to it without
            // explicitly having saved it.
            m_axis1PreviousPositions.push( m_axis1Motor->getPosition() );
        }
        if( m_axis1FastReturning )
        {
            m_axis1Motor->setSpeed( m_previousZSpeed );
            m_axis1FastReturning = false;
        }
        if( m_enabledFunction == Mode::Taper && m_zWasRunning )
        {
            m_axis2Motor->stop();
        }
        if( m_zWasRunning && m_axis1Motor->getRpm() >= 100.0 )
        {
            // We don't allow faster speeds to "stick" to avoid accidental
            // fast motion after a long fast movement
            m_axis1Motor->setSpeed(
                m_config->readDouble( "Axis1SpeedPreset2", 40.0 ) );
        }
        m_zWasRunning = false;
    }
    else
    {
        m_zWasRunning = true;
    }

    if ( ! m_axis2Motor->isRunning() )
    {
        if( m_fastRetracting )
        {
            m_axis2Motor->setSpeed( m_previousXSpeed );
            m_fastRetracting = false;
            m_axis2Retracted = false;
        }
        if( m_axis2FastReturning )
        {
            m_axis2Motor->setSpeed( m_previousXSpeed );
            m_axis2FastReturning = false;
        }
        if( ! ( m_enabledFunction == Mode::Taper ) &&
                m_xWasRunning &&
                m_axis2Motor->getSpeed() >= 80.0
            )
        {
            // We don't allow faster speeds to "stick" to avoid accidental
            // fast motion after a long fast movement
            m_axis2Motor->setSpeed(
                m_config->readDouble( "Axis2SpeedPreset2", 20.0 ) );
        }
        m_xWasRunning = false;
    }
    else
    {
        m_xWasRunning = true;
    }
}


void Model::changeMode( Mode mode )
{
    stopAllMotors();
    if( mode == Mode::Threading || mode == Mode::Taper )
    {
        // We do not want motor speed ramping on tapering or threading
        m_axis1Motor->enableRamping( false );
    }
    else
    {
        m_axis1Motor->enableRamping( true );
    }
    if( m_enabledFunction == Mode::Taper && mode != Mode::Taper )
    {
        m_axis2Motor->setSpeed( m_taperPreviousXSpeed );
    }
    m_warning = "";
    m_currentDisplayMode = mode;
    m_enabledFunction = mode;
    m_input="";

    if( mode == Mode::Taper )
    {
        // reset any taper start points
        m_taperPreviousXSpeed = m_axis2Motor->getSpeed();
        if( m_taperAngle != 0.0 )
        {
            m_input = std::to_string( m_taperAngle );
        }
    }
}

void Model::stopAllMotors()
{
    m_axis2Motor->synchroniseOff();
    m_axis1Motor->stop();
    m_axis2Motor->stop();
    m_axis1Motor->wait();
    m_axis2Motor->wait();
    m_axis1Status = "stopped";
    m_axis2Status = "stopped";
}

void Model::takeUpZBacklash( ZDirection direction )
{
    if( direction == ZDirection::Right )
    {
        m_axis1Motor->goToStep( m_axis1Motor->getCurrentStep() - 1 );
    }
    else if( direction == ZDirection::Left )
    {
        m_axis1Motor->goToStep( m_axis1Motor->getCurrentStep() + 1 );
    }
    m_axis1Motor->wait();
}

void Model::startSynchronisedXMotor( ZDirection direction )
{
    // Make sure X isn't already running first
    m_axis2Motor->synchroniseOff();
    m_axis2Motor->stop();
    m_axis2Motor->wait();

    int stepAdd = -1;
    if( ( direction == ZDirection::Left  && m_taperAngle < 0.0 ) ||
        ( direction == ZDirection::Right && m_taperAngle > 0.0 ) )
    {
        stepAdd = 1;
    }
    // As this is called just before the Z motor starts moving, we take
    // up any backlash first.
    m_axis2Motor->setSpeed( 100.0 );
    m_axis2Motor->goToStep( m_axis2Motor->getCurrentStep() + stepAdd );
    m_axis2Motor->wait();
    double angleConversion = std::tan( m_taperAngle * DEG_TO_RAD );
    m_axis2Motor->synchroniseOn(
        m_axis1Motor.get(),
        [angleConversion]( double zPosDelta )
            {
                return zPosDelta * angleConversion;
            }
        );
}

void Model::axis1GoToStep( long step )
{
    m_axis1Motor->goToStep( step );
}

void Model::axis1GoToPosition( double pos )
{
    m_axis1Motor->goToPosition( pos );
}

void Model::axis1GoToPreviousPosition()
{
    axis1Stop();
    while( ! m_axis1PreviousPositions.empty()
        && ( std::abs(m_axis1PreviousPositions.top() - m_axis1Motor->getPosition() ) < 0.0001 ) )
    {
        if( m_axis1PreviousPositions.size() == 1 )
        {
            return;
        }
        m_axis1PreviousPositions.pop();
    }
    if( m_axis1PreviousPositions.empty() )
    {
        return;
    }
    axis1GoToPosition( m_axis1PreviousPositions.top() );
    m_axis1PreviousPositions.pop();
}

void Model::axis1MoveLeft()
{
    if( ! m_config->readBool( "Axis1MotorFlipDirection", false ) )
    {
        m_axis1Motor->goToStep( INF_LEFT );
    }
    else
    {
        m_axis1Motor->goToStep( INF_RIGHT );
    }
}

void Model::axis1MoveRight()
{
    if( ! m_config->readBool( "Axis1MotorFlipDirection", false ) )
    {
        m_axis1Motor->goToStep( INF_RIGHT );
    }
    else
    {
        m_axis1Motor->goToStep( INF_LEFT );
    }
}

void Model::axis1Stop()
{
    m_axis1Motor->stop();
    m_axis1Motor->wait();
}

void Model::axis2Stop()
{
    m_axis2Motor->stop();
    m_axis2Motor->wait();
}

}