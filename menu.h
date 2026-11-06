#pragma once

#include "commands.h"

#include <optional>
#include <string>
#include <vector>

namespace mgo {

class Model;

// The main menu: a list of categories, each with its commands. This holds the
// menu's state and handles navigation; the view draws it.
//
// The menu never blocks anything. While it's open, the controller carries on
// as normal (motors keep running, the display updates), and only the keys
// used for navigating the menu are taken by it. Everything else is passed
// straight through, in particular Space (stop all motors) and the Alt-arrow
// rapids (and their "dead man" stops). Letters typed on the keyboard are menu
// hotkeys while it's open (see Command::hotkey and categoryHotkey()).
class Menu {
public:
    explicit Menu(std::vector<Command> commands);

    // What the controller should do with a key press while the menu is open
    struct Action {
        enum class Type {
            None, // the menu used the key (for navigation)
            PassThrough, // act on the key as normal
            Command // act on the chosen command's key, as if its shortcut was pressed
        };
        Type type { Type::None };
        int key { -1 };
    };

    bool isOpen() const;
    void open();
    void close();
    // Only call while the menu is open. fromJoystick: whether the key came from
    // the gamepad (whose D-pad keys w/a/s/d navigate the menu, and whose
    // buttons are never treated as hotkeys)
    Action handleKey(int key, const Model& model, bool fromJoystick = false);

    // For drawing:
    const std::vector<std::string>& categories() const;
    // The category being shown, or nullopt if showing the list of categories
    std::optional<std::size_t> openCategory() const;
    // The highlighted entry in whichever list is showing
    std::size_t selection() const;
    // The commands in the open category (empty if none is open)
    std::vector<const Command*> currentCommands() const;

private:
    std::size_t listSize() const;
    Action handleHotkey(char letter, const Model& model);

    std::vector<Command> m_commands;
    std::vector<std::string> m_categories; // in order of first appearance
    bool m_open { false };
    std::optional<std::size_t> m_openCategory;
    std::size_t m_selection { 0 };
};

} // namespace mgo
