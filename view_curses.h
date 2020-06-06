#pragma once

#include "curses.h"
#include "iview.h"

namespace mgo
{

struct Model;

class ViewCurses : public IView
{
public:
    void initialise() override;
    void close() override;
    int  getInput() override;
    void updateDisplay( const Model& ) override;
    // non-overrides:
    void highlightCheck( const Model& model, std::size_t memoryLocation );
private:
    mgo::Curses::Window m_wnd;
};

} // namespace mgo