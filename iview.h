#pragma once

#include "model.h"

// The "view" object abstracts the the graphical toolkit being used.
// This allows for easier switching of UI libraries used - for instance,
// this program originally used curses as a textual interface

namespace mgo {

class IView {
public:
    virtual void initialise(const Model&) = 0;
    virtual void close() = 0;
    // keypresses should be returned as ASCII codes. Should not block.
    virtual int getInput() = 0;
    virtual void updateDisplay(const Model&) = 0;
    virtual ~IView() {};
};

} // namespace mgo