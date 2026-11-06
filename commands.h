#pragma once

#include <functional>
#include <string>
#include <vector>

namespace mgo {

class IConfigReader;
class Model;

// A command the user can give, as shown in the menu. Choosing it from the menu
// acts exactly as pressing its shortcut, as both produce the same key code.
struct Command {
    std::string category; // the menu section it appears in
    std::string description;
    int key; // the key code the controller acts on
    // Whether the command can be used at the moment (e.g. not while an axis is
    // locked). Unavailable commands are shown greyed out
    std::function<bool(const Model&)> available = [](const Model&) { return true; };
};

// All the commands available from the menu, in menu order, worded for this
// config (e.g. axis names, and speed preset values). Commands for an axis
// disabled in the config are left out.
std::vector<Command> buildCommands(const IConfigReader& config);

// Whether a key is allowed in the current mode (e.g. only motion keys in
// Setup mode; no X motion while X is retracted). Used by the controller to
// filter key presses, and by the menu
bool keyAllowedInCurrentMode(int key, const Model& model);

// Whether a command can be used right now
bool isAvailable(const Command& command, const Model& model);

// The keys to press for a key code, e.g. "z g" (axis leader, then g), or "Alt+w".
// Axis leader keys are as set in the config
std::string shortcutText(int key, const IConfigReader& config);

} // namespace mgo
