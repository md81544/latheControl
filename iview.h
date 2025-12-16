#pragma once

#include "model.h"
#include <optional>
#include <set>
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
    PressAnyKey,
    OkCancel
};

// Return type of getInput()
struct Return {
    bool cancelled {true};
    std::string text;
    double number;
    std::set<char> optionsSelected;
    std::optional<std::size_t> pickedItem = std::nullopt;
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
    // It is expected that the additionalText strings can contain
    // ampersands, which, if directly preceding a non-space character,
    // constitutes a "hot key". So for example if the string is "&Hello"
    // then "Hello" is displayed and an option (say) can be checked
    // or acted upon by the user pressing a modifier (e.g. Alt) and H
    // (in this example).
    virtual Input::Return getInput(
        Input::Type type,
        std::string_view prompt,
        std::vector<std::string> additionalText,
        std::string_view defaultEntry = "",
        std::optional<std::vector<std::string>> listItems = std::nullopt
    ) = 0;
    virtual void updateDisplay(const Model&) = 0;
    virtual ~IView() { };
};

} // namespace mgo