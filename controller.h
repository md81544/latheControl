#pragma once

#include "iview.h"
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

private:
    Model* m_model; // non-owning
    std::unique_ptr<IView> m_view;
    int checkKeyAllowedForMode(int key);
    int processModeInputKeys(int key);
    int processLeaderKeyModeKeyPress(int key);
    int checkForAxisLeaderKeys(int key);
    // While sfml_dialog getInput() is multi-functional, for
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
