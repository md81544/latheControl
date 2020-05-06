#pragma once

#include "model.h"

// The "view" object encapsulates the the graphical toolkit being used.
// Abstracting this will mean the switch from curses to SFML will be easier.

namespace mgo
{

class IView
{
public:
    virtual void initialise() = 0;
    virtual void close() = 0;
    // keypresses should be returned as ASCII codes. Should not block.
    virtual int getInput() = 0;
    virtual void updateDisplay( const Model& ) = 0;
    virtual ~IView(){};
};

} // namespace mgo