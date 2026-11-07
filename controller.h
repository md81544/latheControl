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
    bool m_inTick { false };
    // Keys pressed while a motion sequence was running, to act on afterwards
    std::deque<int> m_heldKeys;
    // Thrown by a tick, to be rethrown from run()
    std::exception_ptr m_tickException;
    Model* m_model; // non-owning
    Menu m_menu; // (declared before m_view, which refers to it, so outlives it)
    std::unique_ptr<IView> m_view;
    // Acts on a key code (whether from a key press or chosen from the menu)
    void processKey(int key);
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
    void pressAnyKey(
        std::string_view prompt,
        std::vector<std::string> additionalText,
        std::function<void()> onDone);
};

} // end namespace
