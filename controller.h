#pragma once

#include "iview.h"
#include "model.h"

#include <memory>

namespace mgo {

struct NumericInputReturn{
    bool cancelled;
    double value;
    std::set<char> optionsSelected;
};

struct TextInputReturn{
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
    std::tuple<double, std::string> getNumericInputOld(
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

    // While sfml_dialog getInput() is multi-functional, for
    // ease of use we specialise calls to it by these functions.
    Input::Return getInput( // TODO remove & replace by more specialised call
        Input::Type type,
        std::string_view prompt,
        std::vector<std::string> additionalText,
        std::string_view defaultEntry = "",
        std::optional<std::vector<std::string>> listItems = std::nullopt);
    NumericInputReturn getNumericInput(
        std::string_view prompt,
        std::vector<std::string> additionalText,
        double defaultEntry);
    TextInputReturn getTextInputNew( // change name when getTextInput() is retired
        std::string_view prompt,
        std::vector<std::string> additionalText,
        std::string_view defaultEntry = "");
    std::optional<std::size_t> listPicker(
        std::string_view prompt,
        std::vector<std::string> listItems);
    void pressAnyKey( std::string_view prompt);
};

} // end namespace
