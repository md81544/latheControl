#pragma once

#include "iview.h"
#include "menu.h"
#include "model.h"

#include <memory>

namespace mgo {

struct NumericInputReturn {
    bool cancelled;
    double value;
    std::set<char> optionsSelected;
};

struct TextInputReturn {
    bool cancelled;
    std::string value;
    std::set<char> optionsSelected;
};

class Controller {
public:
    explicit Controller(Model* model);
    // run() is the main loop. When this returns,
    // the application can quit.
    void run();
    void processKeyPress();
    // Returns false if the user aborted the wait with a key press, in which
    // case all motors will have been stopped
    bool waitForAxisToStop(uint8_t axis);
    bool isMenuOpen() const;

private:
    Model* m_model; // non-owning
    Menu m_menu; // (declared before m_view, which refers to it, so outlives it)
    std::unique_ptr<IView> m_view;
    // Acts on a key code (whether from a key press or chosen from the menu)
    void processKey(int key);
    // e.g. " (mm)" or " (in)": the units an axis's values are typed in
    std::string unitsSuffix(int axisNumber) const;
    int checkKeyAllowedForMode(int key);
    int processModeInputKeys(int key);
    int processLeaderKeyModeKeyPress(int key);
    int checkForAxisLeaderKeys(int key);
    // While the view's getInput() is multi-functional, for
    // ease of use we specialise calls to it by these functions.
    NumericInputReturn getNumericInput(
        std::string_view prompt,
        std::vector<std::string> additionalText,
        double defaultEntry);
    TextInputReturn getTextInput(
        std::string_view prompt,
        std::vector<std::string> additionalText,
        std::string_view defaultEntry = "");
    std::optional<std::size_t>
    listPicker(std::string_view prompt, std::vector<std::string> listItems);
    void pressAnyKey(std::string_view prompt, std::vector<std::string> additionalText);
};

} // end namespace
