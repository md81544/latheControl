#pragma once

#include "units.h"

#include <functional>
#include <string>
#include <string_view>
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
    // A setting turned on and off, whose description says how it's set now.
    // Its hotkey (or Right) toggles it, and the menu stays open so the new
    // state can be seen; Enter closes the menu, leaving it as it is
    bool isToggle { false };
};

// The letter (lower case) which opens a menu category, or 0 if none
char categoryHotkey(const std::string& category);

// All the commands available from the menu, in menu order, worded for this
// config (e.g. axis names, and speed preset values). Commands for an axis
// disabled in the config are left out, as are those of a disabled mode
// (DisabledModes).
// units: the display units, used in commands' descriptions (e.g. nudge amounts)
// threadingAutoRetract: whether it's on, as the menu shows (see
// Model::threadingAutoRetract()); likewise axis1FinishOnScale (see
// Model::axis1FinishesOnScale())
std::vector<Command> buildCommands(
    const IConfigReader& config,
    DisplayUnits units = DisplayUnits::Metric,
    bool threadingAutoRetract = false,
    bool axis1FinishOnScale = false,
    bool axis2FinishOnScale = false);

// Whether a key is allowed in the current mode (e.g. no X motion while X is
// retracted, nor a mode's keys if the config disables it). Used by the
// controller to filter key presses, and by the menu
bool keyAllowedInCurrentMode(int key, const Model& model);

// Whether the config's DisabledModes (e.g. "threading, taper, chamfer,
// knurling" on the mill) names a mode: threading, taper, chamfer (quick
// chamfer), multipass or knurling. Case doesn't matter
bool modeDisabled(const IConfigReader& config, std::string_view mode);

// Whether a key starts (or belongs to) a mode the config disables. Such
// commands are left out of the menu, and their keys ignored
bool keyDisabledByConfig(int key, const IConfigReader& config);

// Whether a command can be used right now
bool isAvailable(const Command& command, const Model& model);

// The keys to press for a key code, e.g. "z g" (axis leader, then g), or "Alt+w".
// Axis leader keys are as set in the config. Empty for a command chosen only
// from the menu (e.g. threading)
std::string shortcutText(int key, const IConfigReader& config);

// The commands which follow a leader key: e.g. for KeyMode::Axis1, those
// using the axis 1 leader key (such as "z g"). In the order given
std::vector<const Command*> commandsForLeader(const std::vector<Command>& commands, KeyMode mode);

// The key to press after a leader key for a command, e.g. "g" for "z g"
std::string followUpKeyName(int key);

} // namespace mgo
