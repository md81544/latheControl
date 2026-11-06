#pragma once

#include "iview.h" // for Input::*

#include <optional>
#include <string>
#include <string_view>
#include <vector>

class QString;
class QWidget;

namespace mgo {
namespace dialog {

// Shows a modal dialog over parent, styled like the original SFML dialogs, and
// returns what the user entered. Behaves as the SFML version did:
// - Text / Numeric: an entry box pre-filled with defaultEntry (typing replaces
//   it), Enter to accept, Esc to cancel. Numeric only accepts a number.
// - ListPicker: Up/Down to choose from listItems, Enter to accept, Esc to cancel.
// - PressAnyKey: any key closes it.
// Lines of additionalText containing "&x" define option hotkeys: pressing x
// (or Alt-x, which is required in a Text dialog) toggles that option, shown
// with a tick, and selected options are returned in optionsSelected.
Input::Return getInput(
    QWidget* parent,
    const QString& fontFamily,
    Input::Type type,
    std::string_view prompt,
    std::vector<std::string> additionalText,
    std::string_view defaultEntry,
    std::optional<std::vector<std::string>> listItems = std::nullopt);

// The taper mode dialog: choose a preset taper (e.g. a Morse taper) or enter
// an angle, and whether the tool moves in or out as it approaches the chuck,
// with a diagram of the tool's path. currentAngle is the taper angle in use
// (as Model::getTaperAngle(): negative means the piece gets wider towards
// the chuck). Returns the angle chosen, with that sign, or nullopt if
// cancelled.
std::optional<double> getTaperAngle(QWidget* parent, const QString& fontFamily, double currentAngle);

} // end namespace dialog
} // end namespace mgo
