#pragma once

#include "iview.h"
#include "model.h"

namespace mgo
{

class IView;

class Controller
{
public:
    explicit Controller( Model* model );
    // run() is the main loop. When this returns,
    // the application can quit.
    void run();
    void processKeyPress();
private:
    Model* m_model; // non-owning
    std::unique_ptr<IView> m_view;
    void stopAllMotors();
};

} // end namespace
