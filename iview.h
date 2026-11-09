#pragma once

#include "model.h"
#include <chrono>
#include <functional>
#include <optional>
#include <set>

// The "view" object abstracts the the graphical toolkit being used.
// This allows for easier switching of UI libraries used - for instance,
// this program originally used curses as a textual interface

namespace mgo {

class Menu;

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

// What the multi-pass dialog needs to know (see IView::openMultiPassDialog())
struct MultiPassDialogInfo {
    struct Axis {
        std::string label; // e.g. "Z"
        std::string unit; // as displayed, e.g. "mm"
        int decimals { 2 }; // as positions are displayed
        double displayPerMm { 1.0 }; // display units per mm
    };
    Axis cut;
    Axis feed;
    // Where the axes and memory slots are now
    MultiPassPositions positions;
    // Whether X is measured from the centre (it's been set as a diameter),
    // so the end diameter can be shown, and typed in
    bool diameterKnown { false };
    // Whether X retracts outwards (so infeeds towards the centre), as for
    // turning, or inwards (away from it), as for boring
    bool feedRetractsOutwards { true };
    // The setup to start with
    MultiPassSetup setup;
    // For threading passes (in threading mode): the thread, as shown (e.g.
    // "M14 male, pitch 2 mm"), and its depth in mm. The retract is then
    // always on
    struct Threading {
        std::string thread;
        double depthMm { 0.0 };
        // For the options (see MultiPassPlan::constantArea and
        // cutShiftPerFeed): the angle a compound infeed goes in at, and
        // the least a pass goes in with a constant area
        double compoundAngleDeg { 29.5 };
        double minimumInfeedMm { 0.02 };
    };
    std::optional<Threading> threading;
};

class IView {
public:
    virtual void initialise(const Model&) = 0;
    virtual void close() = 0;
    // Runs the UI toolkit's event loop, calling tick() every interval, until
    // quitEventLoop() is called (normally from within tick())
    virtual void runEventLoop(std::chrono::milliseconds interval, std::function<void()> tick) = 0;
    virtual void quitEventLoop() = 0;
    // keypresses should be returned as ASCII codes. Should not block.
    virtual int getEvents() = 0;
    // Opens a dialog, without waiting for it: onDone is called with the
    // result when the user closes it. Only one dialog can be open at a time.
    // It should be able to handle text entry, number entry,
    // selecting one option from a list, and a simple "Press a key
    // to continue". The user should be able to select options when
    // entering text or numbers.
    // It is expected that the additionalText strings can contain
    // ampersands, which, if directly preceding a non-space character,
    // constitutes a "hot key". So for example if the string is "&Hello"
    // then "Hello" is displayed and an option (say) can be checked
    // or acted upon by the user pressing a modifier (e.g. Alt) and H
    // (in this example).
    virtual void openInput(
        Input::Type type,
        std::string_view prompt,
        std::vector<std::string> additionalText,
        std::function<void(const Input::Return&)> onDone,
        std::string_view defaultEntry = "",
        std::optional<std::vector<std::string>> listItems = std::nullopt
    ) = 0;
    // The taper mode dialog (also without waiting): onDone gets the taper
    // angle chosen (negative if the piece gets wider towards the chuck), or
    // nullopt if cancelled
    virtual void openTaperDialog(
        double currentAngle, std::function<void(std::optional<double>)> onDone) = 0;
    // The thread dialog: onDone gets the thread chosen (see ThreadChoice), or
    // nullopt if cancelled. threads must outlive the dialog, and not be empty
    virtual void openThreadDialog(
        const ThreadTable& threads,
        ThreadChoice current,
        std::function<void(std::optional<ThreadChoice>)> onDone) = 0;
    // The multi-pass dialog: where the cut starts and ends, the step-over and
    // the options. onDone gets the setup chosen (which resolveMultiPass()
    // accepts with info's positions), or nullopt if cancelled
    virtual void openMultiPassDialog(
        const MultiPassDialogInfo& info,
        std::function<void(std::optional<MultiPassSetup>)> onDone) = 0;
    virtual bool isDialogOpen() const = 0;
    // Closes the open dialog (if any) as if cancelled, calling its onDone
    virtual void cancelDialog() = 0;
    virtual void updateDisplay(const Model&) = 0;
    // The menu to draw (when it's open) on each updateDisplay(). Not owned;
    // must outlive the view, or be reset to nullptr
    virtual void setMenu(const Menu* menu) = 0;
    virtual ~IView() { };
};

} // namespace mgo