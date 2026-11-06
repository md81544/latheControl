#pragma once

#include "units.h"

#include <functional>
#include <string>
#include <vector>

namespace mgo {

class IConfigReader;
class Model;
enum class KeyMode; // (see model.h)

// A command the user can give, as shown in the menu. Choosing it from the menu
// acts exactly as pressing its shortcut, as both produce the same key code.
struct Command {
    std::string category; // the menu section it appears in
    std::string description;
    int key; // the key code the controller acts on
    // Whether the command can be used at the moment (e.g. not while an axis is
    // locked). Unavailable commands are shown greyed out
    std::function<bool(const Model&)> available = [](const Model&) { return true; };
    // Optional: a letter (lower case) which chooses this command when its
    // category is open in the menu. 0 if none
    char hotkey { 0 };
};

// The letter (lower case) which opens a menu category, or 0 if none
char categoryHotkey(const std::string& category);

// All the commands available from the menu, in menu order, worded for this
// config (e.g. axis names, and speed preset values). Commands for an axis
// disabled in the config are left out.
// units: the display units, used in commands' descriptions (e.g. nudge amounts)
std::vector<Command> buildCommands(
    const IConfigReader& config,
    DisplayUnits units = DisplayUnits::Metric);

// Whether a key is allowed in the current mode (e.g. only motion keys in
// Setup mode; no X motion while X is retracted). Used by the controller to
// filter key presses, and by the menu
bool keyAllowedInCurrentMode(int key, const Model& model);

// Whether a command can be used right now
bool isAvailable(const Command& command, const Model& model);

// The keys to press for a key code, e.g. "z g" (axis leader, then g), or "Alt+w".
// Axis leader keys are as set in the config
std::string shortcutText(int key, const IConfigReader& config);

// The commands which follow a leader key: e.g. for KeyMode::Axis1, those
// using the axis 1 leader key (such as "z g"); for KeyMode::Function, those
// using F2. In the order given
std::vector<const Command*> commandsForLeader(const std::vector<Command>& commands, KeyMode mode);

// The key to press after a leader key for a command, e.g. "g" for "z g", or
// "t" for "F2 t"
std::string followUpKeyName(int key);

} // namespace mgo
