#pragma once

#include "approachticks.h"
#include "iview.h"
#include "menu.h"
#include "model.h"
#include "soundcues.h"

#include <array>
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
    // Model::axis1ScaleMismatchAsks() when the scale's dialog was last shown.
    // (A count rather than a flag: a new mismatch can be found in the tick
    // straight after one is answered, before the flag could be cleared, and
    // would then never be asked about, leaving every key refused)
    int m_scaleAsksShown { 0 };
    bool m_inTick { false };
    // Keys pressed while a motion sequence was running, to act on afterwards
    std::deque<int> m_heldKeys;
    // Sounds (see SoundCues): the WAV files to play (none if empty), and the
    // command which plays them (the file is added at the end)
    SoundCues m_soundCues;
    std::string m_alertSound;
    std::string m_alarmSound;
    std::string m_tickSound;
    std::string m_soundPlayer;
    bool m_soundPlayerFailed { false };
    // The ticks as each manual axis nears its go-to's target (only with
    // m_tickSound set)
    std::array<ApproachTicks, Model::LAST_MANUAL_AXIS - Model::FIRST_MANUAL_AXIS + 1>
        m_approachTicks;
    // The most important sound due from the ticks, given the one SoundCues
    // chose (if any)
    std::optional<Sound> approachTickSound(std::optional<Sound> sound);
    // Plays a sound in the background, without waiting for it
    void playSound(Sound sound);
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
    // Model::axis1ScaleMismatchMm()), or says its scale is misreading
    void checkScaleMismatch();
    // The tool library (see Model::toolLibrary()):
    // Asks which tool is now in the toolpost, and changes to it
    void chooseTool();
    // Explains measuring a tool (number: a new tool's, or isNew false, one
    // already in the library), and starts it if the user agrees
    void confirmMeasureTool(int number, bool isNew);
    // Asks which tool to measure again
    void pickToolToMeasure();
    // Enter while measuring: records the touch, and once all are in, asks
    // for what's still needed to store the tool (see finishToolMeasurement())
    void recordToolTouch();
    void finishToolMeasurement();
    // Asks for the current tool's description and nose radius
    void describeCurrentTool();
    // e.g. "Z -1.234 mm", for an offset on an axis (1 or 2)
    std::string offsetText(int axis, double mm) const;
    // e.g. " (mm)" or " (in)": the units an axis's values are typed in
    std::string unitsSuffix(int axisNumber) const;
    int checkKeyAllowedForMode(int key);
    int processLeaderKeyModeKeyPress(int key);
    int checkForAxisLeaderKeys(int key);
    // Wrappers for the view's dialogs, which don't wait: onDone is called
    // with the result when the user closes the dialog. (Until then, the
    // controller's ticks carry on.) All motors are stopped first
    // calculator is for sums typed in the entry (see calculator.h): at least
    // its unit, the value's as typed (e.g. "mm", "in", "deg"), for in and mm.
    // The memory slots are filled in here
    void getNumericInput(
        std::string_view prompt,
        std::vector<std::string> additionalText,
        double defaultEntry,
        calc::Context calculator,
        std::function<void(const NumericInputReturn&)> onDone);
    // For a go-to or setting a position: in a sum, p is where the axis is
    // now, as shown (rather than the entry's 0)
    calc::Context positionCalculator(int axisNumber) const;
    // A line for those dialogs, saying so
    std::string pHint(int axisNumber) const;
    void listPicker(
        std::string_view prompt,
        std::vector<std::string> listItems,
        std::function<void(std::optional<std::size_t>)> onDone);
    // The multi-pass dialog's axes, positions and directions (not its setup)
    MultiPassDialogInfo multiPassDialogInfo() const;
    // Each memory slot's value on each axis, as displayed, for the calculator
    // in numeric entries (see calculator.h)
    calc::Memory calculatorMemory() const;
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
