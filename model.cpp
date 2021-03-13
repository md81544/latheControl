#include "model.h"


namespace mgo
{

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

void Model::startSynchronisedXMotor( ZDirection direction, double zSpeed )
{
    // Make sure X isn't already running first
    m_axis2Motor->stop();
    m_axis2Motor->wait();

    int target = INF_OUT;
    int stepAdd = -1;
    if( ( direction == ZDirection::Left  && m_taperAngle < 0.0 ) ||
        ( direction == ZDirection::Right && m_taperAngle > 0.0 ) )
    {
        target = INF_IN;
        stepAdd = 1;
    }
    // As this is called just before the Z motor starts moving, we take
    // up any backlash first.
    m_axis2Motor->setSpeed( 100.0 );
    m_axis2Motor->goToStep( m_axis2Motor->getCurrentStep() + stepAdd );
    m_axis2Motor->wait();
    // What speed will we need for the angle required?
    m_axis2Motor->setSpeed(
        zSpeed  * std::abs( std::tan( m_taperAngle * DEG_TO_RAD ) ) );
    m_axis2Motor->goToStep( target );
}

}