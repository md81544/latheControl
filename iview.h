#pragma once

#include "model.h"
#include <optional>
#include <tuple>

// The "view" object abstracts the the graphical toolkit being used.
// This allows for easier switching of UI libraries used - for instance,
// this program originally used curses as a textual interface

namespace mgo {

namespace Input {

enum class Type {
    Text,
    Numeric,
    ListPicker,
    PressAnyKey
};

// Return type of getInput()
struct Return {
    std::optional<std::string> text;
    std::optional<double> number;
    std::optional<std::set<char>> options;
    std::optional<std::size_t> pickedItem;
};

} // namedpace Input

class IView {
public:
    virtual void initialise(const Model&) = 0;
    virtual void close() = 0;
    // keypresses should be returned as ASCII codes. Should not block.
    virtual int getEvents() = 0;
    // getInput should be able to handle text entry, number entry,
    // selecting one option from a list, and a simple "Press a key
    // to continue". The user should be able to select options when
    // entering text or numbers.
    virtual Input::Return getInput(
        Input::Return type,
        std::string_view prompt,
        std::vector<std::string>& additionalText,
        std::optional<std::vector<char>> hotkeys = std::nullopt,
        std::optional<std::vector<std::string>> listItems = std::nullopt
    ) = 0;
    virtual std::string getTextInput(
        const std::string& prompt,
        const std::string& defaultEntry,
        const std::string& additionalText1 = "",
        const std::string& additionalText2 = "",
        const std::string& additionalText3 = "",
        const std::string& additionalText4 = "")
        = 0;
    virtual std::tuple<double, std::string> getNumericInput(
        const std::string& prompt,
        double defaultEntry,
        const std::string& additionalText1 = "",
        const std::string& additionalText2 = "",
        const std::string& additionalText3 = "",
        const std::string& additionalText4 = "",
        const std::string& hotkeys = "")
        = 0;
    virtual void pressAnyKey(std::string_view prompt) = 0;
    virtual void updateDisplay(const Model&) = 0;
    virtual ~IView() { };
};

} // namespace mgo