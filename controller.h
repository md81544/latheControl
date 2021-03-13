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
    int checkKeyAllowedForMode( int key );
    int processModeInputKeys( int key );
    int processLeaderKeyModeKeyPress( int key );
    // Cache of config values for speed
    double m_axis1MaxMotorSpeed;
    double m_axis2MaxMotorSpeed;
    int checkForAxisLeaderKeys( int key );
};

} // end namespace
