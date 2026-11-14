#pragma once

#include "iview.h"
#include "menu.h"
#include "model.h"

#include <chrono>
#include <deque>
#include <exception>
#include <memory>

namespace mgo {

struct NumericInputReturn {
    bool cancelled;
    double value;
    std::set<char> optionsSelected;
};

class Controller {
public:
    explicit Controller(Model* model);
    // run() runs the view's event loop, calling tick() every TICK_INTERVAL.
    // When this returns, the application can quit.
    void run();
    // One update: acts on any key pressed, checks the model's status and
    // redraws the display. Quits the event loop once the model is quitting
    void tick();
    void processKeyPress();
    bool isMenuOpen() const;

    // How often tick() is called. (The threading speed is updated each tick)
    static constexpr std::chrono::milliseconds TICK_INTERVAL { 50 };

private:
    void tickBody();
    // Opens a dialog to clear a limit switch's stop, once per stop
    void checkLimitSwitch();
    bool m_limitDialogShown { false };
    bool m_scaleDialogShown { false };
    bool m_inTick { false };
    // Keys pressed while a motion sequence was running, to act on afterwards
    std::deque<int> m_heldKeys;
    // Thrown by a tick, to be rethrown from run()
    std::exception_ptr m_tickException;
    Model* m_model; // non-owning
    Menu m_menu; // (declared before m_view, which refers to it, so outlives it)
    std::unique_ptr<IView> m_view;
    // Builds the menu again, for wording which follows the model's state
    // (the display units, and whether threading auto-retract is on)
    void rebuildMenu();
    // Acts on a key code (whether from a key press or chosen from the menu)
    void processKey(int key);
    // Logs a key code the controller is about to act on
    void logKey(int key) const;
    // Asks before measuring axis 1's backlash, as it moves the axis
    void confirmMeasureBacklash();
    // Shows the result once a backlash measurement has finished
    void showBacklashReport();
    // Asks which to believe when axis 1's scale and motor disagree (see
    // Model::axis1ScaleMismatchMm())
    void checkScaleMismatch();
    // e.g. " (mm)" or " (in)": the units an axis's values are typed in
    std::string unitsSuffix(int axisNumber) const;
    int checkKeyAllowedForMode(int key);
    int processLeaderKeyModeKeyPress(int key);
    int checkForAxisLeaderKeys(int key);
    // Wrappers for the view's dialogs, which don't wait: onDone is called
    // with the result when the user closes the dialog. (Until then, the
    // controller's ticks carry on.) All motors are stopped first
    void getNumericInput(
        std::string_view prompt,
        std::vector<std::string> additionalText,
        double defaultEntry,
        std::function<void(const NumericInputReturn&)> onDone);
    void listPicker(
        std::string_view prompt,
        std::vector<std::string> listItems,
        std::function<void(std::optional<std::size_t>)> onDone);
    // The multi-pass dialog's axes, positions and directions (not its setup)
    MultiPassDialogInfo multiPassDialogInfo() const;
    void pressAnyKey(
        std::string_view prompt,
        std::vector<std::string> additionalText,
        std::function<void()> onDone);
    // Enter (or Y) calls onOk; Esc (or N) cancels
    void okCancel(
        std::string_view prompt,
        std::vector<std::string> additionalText,
        std::function<void()> onOk);
    // Does action at once if it wouldn't clear any of the current feature (its
    // diameter, if diameter, and the work's end, if workEnd, as far as either
    // is set); otherwise only once the user agrees. what (e.g. "Zeroing X")
    // says what's about to clear it
    void confirmFeatureLoss(
        const std::string& what,
        bool diameter,
        bool workEnd,
        std::function<void()> action);
};

} // end namespace
