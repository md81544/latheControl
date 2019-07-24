#pragma once

#include "igpio.h"

namespace mgo
{

class Ui
{
public:
    Ui( IGpio& gpio );
    // Run the main loop. When this returns,
    // the application can quit.
    void run();
private:
    IGpio& m_gpio;
};

} // end namespace
