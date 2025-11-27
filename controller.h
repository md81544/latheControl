#pragma once

#include "iview.h"
#include "model.h"

#include <memory>

namespace mgo {

class IView;

class Controller {
public:
    explicit Controller(Model* model);
    // run() is the main loop. When this returns,
    // the application can quit.
    void run();
    void processKeyPress();

private:
    Model* m_model; // non-owning
    std::unique_ptr<IView> m_view;
    int checkKeyAllowedForMode(int key);
    int processModeInputKeys(int key);
    int processLeaderKeyModeKeyPress(int key);
    int checkForAxisLeaderKeys(int key);
    std::tuple<double, std::string> getNumericInput(
        const std::string& prompt,
        double defaultEntry,
        const std::string& additionalText1 = "",
        const std::string& additionalText2 = "",
        const std::string& additionalText3 = "",
        const std::string& additionalText4 = "",
        const std::string& hotkeys = "");
    std::string getTextInput(
        const std::string& prompt,
        const std::string& defaultEntry,
        const std::string& additionalText1 = "",
        const std::string& additionalText2 = "",
        const std::string& additionalText3 = "",
        const std::string& additionalText4 = "");
};

} // end namespace
