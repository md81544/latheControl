#include "view_curses.h"

#include "curses.h"
#include "model.h"
#include "threadpitches.h"

#include <fmt/format.h>

namespace mgo
{

namespace
{

std::string cnv( const mgo::StepperMotor* motor, long step )
{
    double mm = motor->getPosition( step );
    if( std::abs( mm ) < 0.001 )
    {
        mm = 0.0;
    }
    return fmt::format( "{:.3f} mm", mm );
}

std::string cnv( const mgo::StepperMotor* motor )
{
    return cnv( motor, motor->getCurrentStep() );
}

} // end anonymous namespace

void ViewCurses::initialise()
{
    using namespace mgo::Curses;
    m_wnd.cursor( Cursor::off );
    m_wnd << "Up/down (or Fn keys) to set speed\n";
    m_wnd << "Left and right arrow to move, comma and full stop to nudge\n";
    m_wnd << "Space to stop\n";
    m_wnd << "Square brackets [ ] select memory store to use\n";
    m_wnd << "M remembers pos, and Enter returns to it (F "
             "for fast return)\n";
    m_wnd << "T - toggle thread cutting mode, P to choose thread pitch\n";
    m_wnd << fmt::format( "\\ - advance thread cut by {:.2f} mm (suitable "
                          "for {:.2f} mm in-feed)\n", SIDEFEED, INFEED );
    m_wnd << "Escape or Q to quit\n\n";
    m_wnd.setBlocking( Input::nonBlocking );
}

void ViewCurses::close()
{
    // Nothing to do as m_wnd's dtor cleans up
}

int  ViewCurses::getInput()
{
    using namespace mgo::Curses;
    return m_wnd.getKey();
}

void ViewCurses::updateDisplay( const Model& model )
{
    using namespace mgo::Curses;
    std::string targetString = cnv( model.m_zAxisMotor.get() );

    if ( model.m_zAxisMotor->getTargetStep() == INF_LEFT  ) targetString = "<----";
    if ( model.m_zAxisMotor->getTargetStep() == INF_RIGHT ) targetString = "---->";

    m_wnd.move( 9, 0 );
    m_wnd.setColour( Colours::yellowOnBlack );
    m_wnd.clearToEol();
    m_wnd << fmt::format( "Tool speed:  {:<3.0f} mm/min   {}\n",  model.m_zAxisMotor->getRpm(), model.m_status );
    m_wnd.clearToEol();
    m_wnd << "Target:      " << targetString << ", current: "
        << cnv( model.m_zAxisMotor.get() ) << "\n";
    m_wnd.clearToEol();
    m_wnd << "X: " << cnv( model.m_xAxisMotor.get() ) << "  "
        << static_cast<int>( model.m_xAxisMotor->getRpm() ) << " rpm \n";
    m_wnd.clearToEol();
    m_wnd << "Spindle RPM: " << static_cast<int>( model.m_rotaryEncoder->getRpm() ) << "\n";
    if( model.m_currentMode == Mode::Threading )
    {
        ThreadPitch tp = threadPitches.at( model.m_threadPitchIndex );
        m_wnd.clearToEol();
        m_wnd << "Pitch:       " << tp.pitchMm << " mm (" << tp.name << ")\n";
        m_wnd << "\n";
        m_wnd.setColour( Colours::cyanOnBlack );
        m_wnd.clearToEol();
        m_wnd << "    ( Male   OD: " << tp.maleOd << " mm, cut: " << tp.cutDepthMale << " mm )\n";
        m_wnd.clearToEol();
        m_wnd << "    ( Female ID: " << tp.femaleId << " mm, cut: " << tp.cutDepthFemale
            << " mm )\n";
        m_wnd.clearToEol();
        m_wnd.setColour( Colours::yellowOnBlack );
    }

    m_wnd << "\n";
    // uncomment for keycodes m_wnd << m_keyPressed << "\n";

    // Memory labels
    m_wnd.clearToEol();
    for ( int n = 0; n < 4; ++n )
    {
        highlightCheck( model, n );
        m_wnd << fmt::format( "Memory {:<5}", n + 1 );
    }
    m_wnd << "\n";

    // Memory values
    m_wnd.clearToEol();
    for ( int n = 0; n < 4; ++n )
    {
        highlightCheck( model, n );
        if ( model.m_memory.at( n ) != INF_RIGHT )
        {
            m_wnd << fmt::format( "{:<12}", cnv( model.m_zAxisMotor.get(), model.m_memory.at( n ) ) );
        }
        else
        {
            m_wnd << "not set     ";
        }
    }

    // Clear a few extra lines as pitch information can push things up/down
    for( int n = 0; n < 6; ++n )
    {
        m_wnd << "\n";
        m_wnd.clearToEol();
    }

    m_wnd.refresh();
}

void ViewCurses::highlightCheck( const Model& model, std::size_t memoryLocation )
{
    using namespace mgo::Curses;
    if ( memoryLocation == model.m_currentMemory )
    {
        m_wnd.setColour( Colours::whiteOnBlack );
    }
    else
    {
        m_wnd.setColour( Colours::redOnBlack );
    }
}

} // namespace mgo