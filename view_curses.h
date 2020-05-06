#pragma once

#include "iview.h"

namespace mgo
{

class Model;

class ViewCurses : public IView
{
    virtual void initialise() override;
    virtual void close() override;
    virtual int getInput() override;
    virtual void updateDisplay( const Model& ) override;
};

} // namespace mgo