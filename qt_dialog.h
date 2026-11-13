#pragma once

#include "iview.h" // for Input::*
#include "threadpitches.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class QDialog;
class QString;
class QWidget;

namespace mgo {
namespace dialog {

// These open a dialog over parent, without waiting for it: they return at
// once, and onDone is called with the result when the dialog closes. The
// dialog is window modal (input to parent is blocked meanwhile, but Qt's
// event loop, and so the controller's updates, carry on). It deletes itself
// once closed, after calling onDone.

// A dialog styled like the original SFML dialogs, which behaves as the SFML
// version did:
// - Text / Numeric: an entry box pre-filled with defaultEntry (typing replaces
//   it), Enter to accept, Esc to cancel. Numeric only accepts a number.
// - ListPicker: Up/Down to choose from listItems, Enter to accept, Esc to cancel.
// - PressAnyKey: any key closes it.
// - OkCancel: Enter or Y accepts, Esc or N cancels.
// Lines of additionalText containing "&x" define option hotkeys: pressing x
// (or Alt-x, which is required in a Text dialog) toggles that option, shown
// with a tick, and selected options are returned in optionsSelected.
QDialog* openInput(
    QWidget* parent,
    const QString& fontFamily,
    Input::Type type,
    std::string_view prompt,
    std::vector<std::string> additionalText,
    std::string_view defaultEntry,
    std::optional<std::vector<std::string>> listItems,
    std::function<void(const Input::Return&)> onDone);

// The taper mode dialog: choose a preset taper (e.g. a Morse taper) or enter
// an angle, and whether the tool moves in or out as it approaches the chuck,
// with a diagram of the tool's path. currentAngle is the taper angle in use
// (as Model::getTaperAngle(): negative means the piece gets wider towards
// the chuck). Gives onDone the angle chosen, with that sign, or nullopt if
// cancelled.
QDialog* openTaperDialog(
    QWidget* parent,
    const QString& fontFamily,
    double currentAngle,
    std::function<void(std::optional<double>)> onDone);

// The thread dialog: choose a type of thread (e.g. metric coarse), then a
// size, and whether it's male (external) or female (internal), with a diagram
// of its profile and its figures. Starts on current. Gives onDone the choice,
// or nullopt if cancelled. threads must outlive the dialog, and not be empty
QDialog* openThreadDialog(
    QWidget* parent,
    const QString& fontFamily,
    const ThreadTable& threads,
    ThreadChoice current,
    std::function<void(std::optional<ThreadChoice>)> onDone);

// The multi-pass dialog: where the cut starts (where the axes are now, or a
// memory slot), where each axis ends (a distance from the start, or a memory
// slot), the step-over per pass, and the options, showing the positions
// these come to. Large text, to be readable on the machine's small screen.
// Gives onDone the setup chosen, or nullopt if cancelled. It can only be
// accepted with a setup which resolveMultiPass() accepts
QDialog* openMultiPassDialog(
    QWidget* parent,
    const QString& fontFamily,
    const MultiPassDialogInfo& info,
    std::function<void(std::optional<MultiPassSetup>)> onDone);

// The gear cutting dialog: the gear's module and number of teeth, where the
// cut starts (where the cut axis is now, or a memory slot) and ends (a
// distance from the start, or a memory slot), the depth (the whole depth for
// the module, unless typed) and the depth per pass, showing the blank's
// diameter, the angle per tooth and which cutter to use. Gives onDone the
// setup chosen, or nullopt if cancelled. It can only be accepted with a
// setup which resolveGearCut() accepts
QDialog* openGearDialog(
    QWidget* parent,
    const QString& fontFamily,
    const GearDialogInfo& info,
    std::function<void(std::optional<GearSetup>)> onDone);

// The radius dialog: the radius, the depth per pass and the tool's nose
// radius, showing the work it's cut on (the current feature's diameter and
// end) and how many passes it takes. Gives onDone the setup chosen, or
// nullopt if cancelled. It can only be accepted with a setup which
// planRadius() accepts
QDialog* openRadiusDialog(
    QWidget* parent,
    const QString& fontFamily,
    const RadiusDialogInfo& info,
    std::function<void(std::optional<RadiusSetup>)> onDone);

} // end namespace dialog
} // end namespace mgo
