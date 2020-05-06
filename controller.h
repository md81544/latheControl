#pragma once

#include "iview.h"
#include "model.h"

namespace mgo
{

class IView;

class Controller
{
public:
    explicit Controller( IGpio& gpio );
    // run() is the main loop. When this returns,
    // the application can quit.
    void run();
    void processKeyPress();
private:
    Model m_model;
    std::unique_ptr<IView> m_view;
};

} // end namespace
