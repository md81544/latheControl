// Tests of the Qt user interface. These use Qt's "offscreen" platform, so no
// windows appear on screen and no real keyboard input is involved: key presses
// are sent directly to the widgets with QTest.

#define CATCH_CONFIG_RUNNER
#include "catch.hpp"

#include "configreader.h"
#include "controller.h"
#include "keycodes.h"
#include "model.h"
#include "stepperControl/mockgpio.h"
#include "tapers.h"
#include "view_qt.h"

#include <QApplication>
#include <QFile>
#include <QFontDatabase>
#include <QTest>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <sstream>
#include <map>
#include <optional>
#include <string_view>
#include <vector>
#include <thread>

namespace {

QWidget* mainWindow()
{
    for (QWidget* w : QApplication::topLevelWidgets()) {
        if (w->windowTitle() == "Lathe Control" && w->isVisible()) {
            return w;
        }
    }
    return nullptr;
}

// Arranges for fn to be called with the modal dialog once it is showing.
// (Dialogs block in their own event loop, so this must be set up beforehand.)
void whenDialogShows(std::function<void(QWidget*)> fn)
{
    auto* timer = new QTimer();
    timer->setInterval(10);
    QObject::connect(timer, &QTimer::timeout, [timer, fn]() {
        if (QWidget* dialog = QApplication::activeModalWidget()) {
            timer->stop();
            timer->deleteLater();
            fn(dialog);
        }
    });
    timer->start();
}

// Opens a dialog and waits (running Qt's event loop) until it's closed,
// returning its result
mgo::Input::Return getInput(
    mgo::ViewQt& view,
    mgo::Input::Type type,
    std::string_view prompt,
    std::vector<std::string> additionalText,
    std::string_view defaultEntry = "",
    std::optional<std::vector<std::string>> listItems = std::nullopt)
{
    std::optional<mgo::Input::Return> result;
    view.openInput(
        type,
        prompt,
        std::move(additionalText),
        [&result](const mgo::Input::Return& rc) { result = rc; },
        defaultEntry,
        std::move(listItems));
    REQUIRE(view.isDialogOpen());
    REQUIRE(QTest::qWaitFor([&result] { return result.has_value(); }, 5'000));
    REQUIRE(!view.isDialogOpen());
    return *result;
}

std::optional<double> getTaperAngle(mgo::ViewQt& view, double currentAngle)
{
    bool done = false;
    std::optional<double> angle;
    view.openTaperDialog(currentAngle, [&](std::optional<double> a) {
        angle = a;
        done = true;
    });
    REQUIRE(QTest::qWaitFor([&done] { return done; }, 5'000));
    return angle;
}

// Dialogs don't block, so after a key which opens one, this waits (running
// Qt's event loop, so a whenDialogShows() callback can act) until it's closed
bool waitForDialogToClose()
{
    return QTest::qWaitFor([] { return QApplication::activeModalWidget() == nullptr; }, 5'000);
}

std::optional<mgo::ThreadChoice>
chooseThread(mgo::ViewQt& view, const mgo::ThreadTable& threads, mgo::ThreadChoice current)
{
    bool done = false;
    std::optional<mgo::ThreadChoice> choice;
    view.openThreadDialog(threads, current, [&](std::optional<mgo::ThreadChoice> c) {
        choice = c;
        done = true;
    });
    REQUIRE(QTest::qWaitFor([&done] { return done; }, 5'000));
    return choice;
}

// The widget within the dialog that keyboard input goes to
QWidget* inputWidget(QWidget* dialog)
{
    return dialog->focusWidget() ? dialog->focusWidget() : dialog;
}

int nextKey(mgo::ViewQt& view)
{
    return view.getEvents();
}

// All the key codes currently queued
std::vector<int> allKeys(mgo::ViewQt& view)
{
    std::vector<int> keys;
    for (int k = view.getEvents(); k != mgo::key::None; k = view.getEvents()) {
        keys.push_back(k);
    }
    return keys;
}

using Keys = std::vector<int>;

// A model with mocked hardware, for the view to display
struct TestModel {
    mgo::MockConfigReader config;
    mgo::MockGpio gpio { false };
    mgo::Model model { gpio, config };
};

// A config with the given values, and the defaults for everything else
class MapConfigReader final : public mgo::IConfigReader {
public:
    explicit MapConfigReader(std::map<std::string, std::string> values)
        : m_values(std::move(values))
    {
    }
    std::string read(const std::string& key, const std::string& defaultValue = "") const override
    {
        const auto it = m_values.find(key);
        return it == m_values.end() ? defaultValue : it->second;
    }
    unsigned long readLong(const std::string& key, unsigned long defaultValue = 0) const override
    {
        const auto it = m_values.find(key);
        return it == m_values.end() ? defaultValue : std::stoul(it->second);
    }
    double readDouble(const std::string& key, double defaultValue = 0.0) const override
    {
        const auto it = m_values.find(key);
        return it == m_values.end() ? defaultValue : std::stod(it->second);
    }
    bool readBool(const std::string& key, bool defaultValue) const override
    {
        const auto it = m_values.find(key);
        return it == m_values.end() ? defaultValue : it->second == "true";
    }

private:
    std::map<std::string, std::string> m_values;
};

// A slower Z, so that its nudges (at a tenth of the maximum speed) take long
// enough to press keys during them: 0.05mm in 0.3s
struct SlowZTestModel {
    MapConfigReader config { { { "Axis1MaxMotorRpm", "100" } } };
    mgo::MockGpio gpio { false };
    mgo::Model model { gpio, config };
};

// With Z's driver alarm wired (to a pin the mock GPIO can set)
constexpr int ALARM_PIN = 17;
struct AlarmTestModel {
    MapConfigReader config { { { "Axis1AlarmPin", "17" }, { "Axis1AlarmActiveLevel", "low" } } };
    mgo::MockGpio gpio { false };
    mgo::Model model { gpio, config };
};

} // end anonymous namespace

TEST_CASE("UI: keys are converted to key codes")
{
    TestModel tm;
    mgo::ViewQt view;
    view.initialise(tm.model);
    QWidget* w = mainWindow();
    REQUIRE(w);

    // NB the view ignores the same key pressed again within 100ms
    QTest::keyClick(w, Qt::Key_A);
    REQUIRE(nextKey(view) == mgo::key::a);
    QTest::qWait(120);
    QTest::keyClick(w, Qt::Key_A, Qt::ShiftModifier);
    REQUIRE(nextKey(view) == mgo::key::A);
    QTest::keyClick(w, Qt::Key_Q, Qt::ControlModifier);
    REQUIRE(nextKey(view) == mgo::key::CtrlQ);
    // (QTest presses and releases the Alt key itself around this, as a real
    // keyboard would; releasing Alt always sends a stop, as with SFML)
    QTest::keyClick(w, Qt::Key_W, Qt::AltModifier);
    REQUIRE(allKeys(view) == Keys { mgo::key::AltW, mgo::key::SPACE });
    QTest::keyClick(w, Qt::Key_F2);
    REQUIRE(nextKey(view) == mgo::key::F2);
    QTest::keyClick(w, Qt::Key_5);
    REQUIRE(nextKey(view) == mgo::key::FIVE);
    QTest::keyClick(w, Qt::Key_BracketLeft);
    REQUIRE(nextKey(view) == mgo::key::LBRACKET);
    QTest::keyClick(w, Qt::Key_Asterisk, Qt::ShiftModifier);
    REQUIRE(nextKey(view) == mgo::key::ASTERISK);
    QTest::keyClick(w, Qt::Key_Return);
    REQUIRE(nextKey(view) == mgo::key::ENTER);
    QTest::keyClick(w, Qt::Key_Space);
    REQUIRE(nextKey(view) == mgo::key::SPACE);
    // Nothing else should be queued
    REQUIRE(nextKey(view) == mgo::key::None);
}

TEST_CASE("UI: releasing Alt or an Alt-arrow stops (dead man)")
{
    TestModel tm;
    mgo::ViewQt view;
    view.initialise(tm.model);
    QWidget* w = mainWindow();
    REQUIRE(w);

    // Pressing Alt+Left starts a rapid (QTest presses Alt first, then Left)
    QTest::keyPress(w, Qt::Key_Left, Qt::AltModifier);
    REQUIRE(allKeys(view) == Keys { mgo::key::AltLeft });
    // Releasing it stops: once for the arrow (released with Alt still held)
    // and once for the Alt key itself
    QTest::keyRelease(w, Qt::Key_Left, Qt::AltModifier);
    REQUIRE(allKeys(view) == Keys { mgo::key::SPACE, mgo::key::SPACE });

    // Alt on its own does nothing until released
    QTest::keyPress(w, Qt::Key_Alt);
    REQUIRE(allKeys(view).empty());
    QTest::keyRelease(w, Qt::Key_Alt);
    REQUIRE(allKeys(view) == Keys { mgo::key::SPACE });

    // Right Alt is often AltGr on Linux, which Qt doesn't report as an Alt
    // modifier: AltGr + arrow must still be a rapid, and stop on release
    // (The AltGr events are sent directly, as QTest's helpers complain about
    // AltGr, having no character for it)
    QKeyEvent altGrPress(QEvent::KeyPress, Qt::Key_AltGr, Qt::NoModifier);
    QApplication::sendEvent(w, &altGrPress);
    QTest::keyPress(w, Qt::Key_Up);
    REQUIRE(allKeys(view) == Keys { mgo::key::AltUp });
    QTest::keyRelease(w, Qt::Key_Up);
    REQUIRE(allKeys(view) == Keys { mgo::key::SPACE });
    QKeyEvent altGrRelease(QEvent::KeyRelease, Qt::Key_AltGr, Qt::NoModifier);
    QApplication::sendEvent(w, &altGrRelease);
    REQUIRE(allKeys(view) == Keys { mgo::key::SPACE });

    // A plain arrow, without Alt, doesn't stop on release
    QTest::keyPress(w, Qt::Key_Right);
    REQUIRE(allKeys(view) == Keys { mgo::key::RIGHT });
    QTest::keyRelease(w, Qt::Key_Right);
    REQUIRE(allKeys(view).empty());
}

TEST_CASE("UI: auto-repeated keys are ignored")
{
    TestModel tm;
    mgo::ViewQt view;
    view.initialise(tm.model);
    QWidget* w = mainWindow();
    REQUIRE(w);

    QKeyEvent press(QEvent::KeyPress, Qt::Key_Up, Qt::AltModifier, QString(), true);
    QApplication::sendEvent(w, &press);
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_Up, Qt::AltModifier, QString(), true);
    QApplication::sendEvent(w, &release);
    // In particular, an auto-repeat release must not stop a rapid
    REQUIRE(nextKey(view) == mgo::key::None);
}

TEST_CASE("UI: numeric dialog, with an option hotkey")
{
    TestModel tm;
    mgo::ViewQt view;
    view.initialise(tm.model);

    whenDialogShows([](QWidget* dialog) {
        QWidget* input = inputWidget(dialog);
        // Typing replaces the default entry; letters can't be typed in a
        // numeric entry, so 'd' toggles the [&D] option
        QTest::keyClicks(input, "5.2");
        QTest::keyClick(input, Qt::Key_D);
        QTest::keyClick(input, Qt::Key_X); // not an option, so ignored
        QTest::keyClick(input, Qt::Key_Return);
    });
    const auto rc = getInput(view, 
        mgo::Input::Type::Numeric, "Position", { "[&D] set as diameter" }, "1.5");
    REQUIRE(!rc.cancelled);
    REQUIRE(rc.number == Approx(5.2));
    REQUIRE(rc.optionsSelected == std::set<char> { 'd' });
}

TEST_CASE("UI: Esc cancels a dialog")
{
    TestModel tm;
    mgo::ViewQt view;
    view.initialise(tm.model);

    whenDialogShows([](QWidget* dialog) {
        QTest::keyClicks(inputWidget(dialog), "42");
        QTest::keyClick(inputWidget(dialog), Qt::Key_Escape);
    });
    const auto rc = getInput(view, mgo::Input::Type::Numeric, "Position", {}, "1.5");
    REQUIRE(rc.cancelled);
}

TEST_CASE("UI: list picker")
{
    TestModel tm;
    mgo::ViewQt view;
    view.initialise(tm.model);

    whenDialogShows([](QWidget* dialog) {
        QWidget* input = inputWidget(dialog);
        QTest::keyClick(input, Qt::Key_Down);
        QTest::keyClick(input, Qt::Key_Down);
        QTest::keyClick(input, Qt::Key_Down);
        QTest::keyClick(input, Qt::Key_Up);
        QTest::keyClick(input, Qt::Key_Return);
    });
    const auto rc = getInput(view, 
        mgo::Input::Type::ListPicker,
        "Pick one",
        {},
        "",
        std::vector<std::string> { "zero", "one", "two", "three", "four" });
    REQUIRE(!rc.cancelled);
    REQUIRE(rc.pickedItem == 2);
}

TEST_CASE("UI: text dialog needs Alt for option hotkeys")
{
    TestModel tm;
    mgo::ViewQt view;
    view.initialise(tm.model);

    whenDialogShows([](QWidget* dialog) {
        QWidget* input = inputWidget(dialog);
        QTest::keyClicks(input, "hex");
        QTest::keyClick(input, Qt::Key_X, Qt::AltModifier);
        QTest::keyClick(input, Qt::Key_Return);
    });
    const auto rc
        = getInput(view, mgo::Input::Type::Text, "Name", { "[&X] an option" }, "default");
    REQUIRE(!rc.cancelled);
    REQUIRE(rc.text == "hex");
    REQUIRE(rc.optionsSelected == std::set<char> { 'x' });
}

TEST_CASE("UI: press any key")
{
    TestModel tm;
    mgo::ViewQt view;
    view.initialise(tm.model);

    whenDialogShows([](QWidget* dialog) { QTest::keyClick(inputWidget(dialog), Qt::Key_Q); });
    const auto rc = getInput(view, mgo::Input::Type::PressAnyKey, "Help", { "Some text" });
    REQUIRE(!rc.cancelled);
}

TEST_CASE("UI: Z go-to through the controller")
{
    // End to end: "z" then "g" opens the go-to dialog; entering 5 should move
    // the Z axis to 5.00
    TestModel tm;
    mgo::Controller controller(&tm.model); // creates the view and window
    tm.model.setAxis1MotorSpeed(1'000.0);
    QWidget* w = mainWindow();
    REQUIRE(w);

    QTest::keyClick(w, Qt::Key_Z);
    controller.processKeyPress();
    REQUIRE(tm.model.getKeyMode() == mgo::KeyMode::Axis1);

    bool dialogShown = false;
    whenDialogShows([&dialogShown](QWidget* dialog) {
        dialogShown = true;
        QTest::keyClicks(inputWidget(dialog), "5");
        QTest::keyClick(inputWidget(dialog), Qt::Key_Return);
    });
    QTest::keyClick(w, Qt::Key_G);
    controller.processKeyPress(); // opens the dialog
    REQUIRE(waitForDialogToClose());
    REQUIRE(dialogShown);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::abs(tm.model.getAxis1MotorPosition() - 5.0) > 0.001
           && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(tm.model.getAxis1MotorPosition() == Approx(5.0).margin(0.001));
}

namespace {

// Presses a key in the main window and lets the controller act on it. (Waits
// afterwards, as the view ignores the same key pressed again within 100ms)
void press(QWidget* w, mgo::Controller& controller, Qt::Key k)
{
    QTest::keyClick(w, k);
    controller.processKeyPress();
    REQUIRE(waitForDialogToClose());
    QTest::qWait(110);
}

bool waitUntil(const std::function<bool()>& condition)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!condition()) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return true;
}

} // end anonymous namespace

TEST_CASE("UI: menu through the controller")
{
    TestModel tm;
    mgo::Controller controller(&tm.model);
    tm.model.setAxis1MotorSpeed(1'000.0);
    QWidget* w = mainWindow();
    REQUIRE(w);

    // Move Z away from zero, then zero it from the menu
    tm.model.axis1GoToPosition(2.0);
    REQUIRE(waitUntil([&] { return std::abs(tm.model.getAxis1MotorPosition() - 2.0) < 0.001; }));
    press(w, controller, Qt::Key_Tab);
    REQUIRE(controller.isMenuOpen());
    // "Position" is the fifth category; "Zero Z" is its first command
    for (int n = 0; n < 4; ++n) {
        press(w, controller, Qt::Key_Down);
    }
    press(w, controller, Qt::Key_Return);
    press(w, controller, Qt::Key_Return);
    REQUIRE(!controller.isMenuOpen());
    REQUIRE(tm.model.getAxis1MotorPosition() == 0.0);

    // While the menu is open, Space still stops all motors, and the menu
    // stays open
    tm.model.axis1GoToPosition(50.0);
    REQUIRE(waitUntil([&] { return tm.model.isAxis1MotorRunning(); }));
    press(w, controller, Qt::Key_Tab);
    REQUIRE(controller.isMenuOpen());
    REQUIRE(tm.model.isAxis1MotorRunning()); // opening the menu doesn't stop anything
    press(w, controller, Qt::Key_Space);
    REQUIRE(!tm.model.isAxis1MotorRunning());
    REQUIRE(controller.isMenuOpen());

    // Esc closes it
    press(w, controller, Qt::Key_Escape);
    REQUIRE(!controller.isMenuOpen());

    // Hotkeys: Tab, M (Modes), H (Quick chamfer)
    press(w, controller, Qt::Key_Tab);
    press(w, controller, Qt::Key_M);
    press(w, controller, Qt::Key_H);
    REQUIRE(!controller.isMenuOpen());
    REQUIRE(tm.model.getMode() == mgo::Mode::Taper);
    REQUIRE(tm.model.getTaperAngle() == -45.0);

    // F1 (help) opens and closes the menu too
    press(w, controller, Qt::Key_F1);
    REQUIRE(controller.isMenuOpen());
    press(w, controller, Qt::Key_F1);
    REQUIRE(!controller.isMenuOpen());
}

namespace {

// Types into whichever widget in the dialog has the keyboard focus (as a real
// keyboard would), one character at a time
void typeInto(QWidget* dialog, const std::string& text)
{
    for (const char c : text) {
        QTest::keyClicks(inputWidget(dialog), QString(c));
    }
}

} // end anonymous namespace

TEST_CASE("UI: taper dialog through the controller")
{
    TestModel tm;
    mgo::Controller controller(&tm.model);
    QWidget* w = mainWindow();
    REQUIRE(w);

    // F2 then P opens it; choose MT2, tool moving in, and OK. (Found by name,
    // so the test doesn't break when the presets are reordered)
    const auto mt2 = std::ranges::find(
        mgo::taperPresets, std::string("Morse taper MT2"), &mgo::TaperPreset::name);
    REQUIRE(mt2 != mgo::taperPresets.end());
    const auto mt2Row = std::distance(mgo::taperPresets.begin(), mt2);
    whenDialogShows([mt2Row](QWidget* dialog) {
        for (auto i = 0; i < mt2Row; ++i) {
            QTest::keyClick(inputWidget(dialog), Qt::Key_Down);
        }
        QTest::keyClick(inputWidget(dialog), Qt::Key_I);
        QTest::keyClick(inputWidget(dialog), Qt::Key_Return);
    });
    press(w, controller, Qt::Key_F2);
    press(w, controller, Qt::Key_P);
    REQUIRE(tm.model.getMode() == mgo::Mode::Taper);
    REQUIRE(tm.model.getTaperAngle() == Approx(1.4307));
}

TEST_CASE("UI: taper dialog")
{
    TestModel tm;
    mgo::ViewQt view;
    view.initialise(tm.model);

    // A custom angle: typing a number switches to it. The tool moves out by
    // default, which is a negative angle (wider towards the chuck)
    whenDialogShows([](QWidget* dialog) {
        typeInto(dialog, "12.5");
        QTest::keyClick(inputWidget(dialog), Qt::Key_Return);
    });
    REQUIRE(getTaperAngle(view, 0.0) == Approx(-12.5));

    // Esc cancels
    whenDialogShows([](QWidget* dialog) { QTest::keyClick(inputWidget(dialog), Qt::Key_Escape); });
    REQUIRE(!getTaperAngle(view, 0.0).has_value());

    // Enter does nothing without a valid angle (here, just a ".")
    whenDialogShows([](QWidget* dialog) {
        typeInto(dialog, ".");
        QTest::keyClick(inputWidget(dialog), Qt::Key_Return);
        REQUIRE(dialog->isVisible());
        QTest::keyClick(inputWidget(dialog), Qt::Key_Escape);
    });
    REQUIRE(!getTaperAngle(view, 0.0).has_value());

    // It starts with the taper in use: a preset (with its direction)...
    whenDialogShows([](QWidget* dialog) { QTest::keyClick(inputWidget(dialog), Qt::Key_Return); });
    REQUIRE(getTaperAngle(view, -1.4377) == Approx(-1.4377));
    // ...or a custom angle
    whenDialogShows([](QWidget* dialog) { QTest::keyClick(inputWidget(dialog), Qt::Key_Return); });
    REQUIRE(getTaperAngle(view, 7.25) == Approx(7.25));
}

TEST_CASE("UI: quick chamfer")
{
    // H: a 45 degree taper, in taper mode as from the taper dialog (so, e.g.,
    // leaving it restores X's speed)
    TestModel tm;
    mgo::Controller controller(&tm.model);
    QWidget* w = mainWindow();
    REQUIRE(w);
    tm.model.setAxis2MotorSpeed(37.0);
    press(w, controller, Qt::Key_H);
    REQUIRE(tm.model.getMode() == mgo::Mode::Taper);
    REQUIRE(tm.model.getTaperAngle() == Approx(-45.0));
    tm.model.setAxis2MotorSpeed(100.0);
    press(w, controller, Qt::Key_Escape);
    REQUIRE(tm.model.getMode() == mgo::Mode::None);
    REQUIRE(tm.model.getAxis2MotorSpeed() == Approx(37.0));
}

TEST_CASE("UI: thread dialog")
{
    TestModel tm;
    mgo::ViewQt view;
    view.initialise(tm.model);
    std::istringstream in("[Metric | ISO]\nM6, 6, 1\nM8, 8, 1.25\n"
                          "[Whitworth | Whitworth]\n3/8\" BSW, 3/8\", 16 tpi\n");
    std::vector<std::string> errors;
    const auto threads = mgo::readThreads(in, errors);
    REQUIRE(errors.empty());
    const auto key = [](QWidget* dialog, Qt::Key k) { QTest::keyClick(inputWidget(dialog), k); };

    // It opens on the thread in use: Down to M8, M for female, and OK
    whenDialogShows([&](QWidget* dialog) {
        key(dialog, Qt::Key_Down);
        key(dialog, Qt::Key_M);
        key(dialog, Qt::Key_Return);
    });
    auto choice = chooseThread(view, threads, { 0, 0, false });
    REQUIRE(choice);
    REQUIRE(choice->category == 0);
    REQUIRE(choice->thread == 1);
    REQUIRE(choice->female);

    // Left goes back to the types; Down, Right (or Enter) opens one
    whenDialogShows([&](QWidget* dialog) {
        key(dialog, Qt::Key_Left);
        key(dialog, Qt::Key_Down);
        key(dialog, Qt::Key_Right);
        key(dialog, Qt::Key_Return);
    });
    choice = chooseThread(view, threads, { 0, 1, true });
    REQUIRE(choice);
    REQUIRE(choice->category == 1);
    REQUIRE(choice->thread == 0);
    REQUIRE(choice->female); // (kept from before)

    // Enter on a type opens it, rather than choosing
    whenDialogShows([&](QWidget* dialog) {
        key(dialog, Qt::Key_Left);
        key(dialog, Qt::Key_Return);
        REQUIRE(dialog->isVisible());
        key(dialog, Qt::Key_Return);
    });
    choice = chooseThread(view, threads, { 1, 0, false });
    REQUIRE(choice);
    REQUIRE(choice->category == 1);

    // Esc cancels
    whenDialogShows([&](QWidget* dialog) { key(dialog, Qt::Key_Escape); });
    REQUIRE(!chooseThread(view, threads, { 0, 0, false }));
}

TEST_CASE("UI: choosing a thread through the controller")
{
    // (The UI tests run from the source root, so the model reads the supplied
    // threadpitches.cfg)
    TestModel tm;
    mgo::Controller controller(&tm.model);
    QWidget* w = mainWindow();
    REQUIRE(w);
    REQUIRE(!tm.model.getThreads().empty());

    // F2, T: the first size of the first type, female
    whenDialogShows([](QWidget* dialog) {
        QTest::keyClick(inputWidget(dialog), Qt::Key_M);
        QTest::keyClick(inputWidget(dialog), Qt::Key_Return);
    });
    press(w, controller, Qt::Key_F2);
    press(w, controller, Qt::Key_T);
    REQUIRE(tm.model.getMode() == mgo::Mode::Threading);
    REQUIRE(tm.model.getThread()->name == tm.model.getThreads().at(0).threads.at(0).name);
    REQUIRE(tm.model.getThreadChoice().female);
    REQUIRE(tm.model.getRetractionDirection() == mgo::XDirection::Inwards);
}

TEST_CASE("UI: switching to inches, and typing a position in inches")
{
    TestModel tm;
    mgo::Controller controller(&tm.model);
    tm.model.setAxis1MotorSpeed(1'000.0);
    QWidget* w = mainWindow();
    REQUIRE(w);

    press(w, controller, Qt::Key_U);
    REQUIRE(tm.model.getDisplayUnits() == mgo::DisplayUnits::Imperial);

    // Z, G: go to 1 (inch), which is 25.4mm
    whenDialogShows([](QWidget* dialog) {
        typeInto(dialog, "1");
        QTest::keyClick(inputWidget(dialog), Qt::Key_Return);
    });
    press(w, controller, Qt::Key_Z);
    press(w, controller, Qt::Key_G);
    REQUIRE(waitUntil([&] { return std::abs(tm.model.getAxis1MotorPosition() - 25.4) < 0.001; }));

    // And back to mm
    press(w, controller, Qt::Key_U);
    REQUIRE(tm.model.getDisplayUnits() == mgo::DisplayUnits::Metric);
}

TEST_CASE("UI: setting X as a diameter, then zeroing X")
{
    TestModel tm;
    mgo::Controller controller(&tm.model);
    QWidget* w = mainWindow();
    REQUIRE(w);

    // X, S: set the position, as a diameter (D)
    whenDialogShows([](QWidget* dialog) {
        typeInto(dialog, "25.4");
        QTest::keyClick(inputWidget(dialog), Qt::Key_D);
        QTest::keyClick(inputWidget(dialog), Qt::Key_Return);
    });
    press(w, controller, Qt::Key_X);
    press(w, controller, Qt::Key_S);
    REQUIRE(tm.model.getDiameterMm() == Approx(25.4).margin(0.001));

    // X, Z: zeroing X means it's no longer measured from the centre
    press(w, controller, Qt::Key_X);
    press(w, controller, Qt::Key_Z);
    REQUIRE(!tm.model.getDiameterMm().has_value());
}

TEST_CASE("UI: the controller's event loop")
{
    // run() on Qt's event loop: keys typed while it runs are acted on, a
    // dialog opened from within a tick works (the timer carries on firing
    // while it's open), and closing the window quits the loop
    TestModel tm;
    mgo::Controller controller(&tm.model);
    QWidget* w = mainWindow();
    REQUIRE(w);

    bool dialogShown = false;
    whenDialogShows([&dialogShown](QWidget* dialog) {
        dialogShown = true;
        // (Let the main timer fire a few times while the dialog is open)
        QTest::qWait(200);
        typeInto(dialog, "0.5");
        QTest::keyClick(inputWidget(dialog), Qt::Key_Return);
    });

    // (Each key is a little apart, as the view ignores the same key pressed
    // again within 100ms, and each must be taken by a tick before the next)
    QTimer::singleShot(150, [w] { QTest::keyClick(w, Qt::Key_U); });
    QTimer::singleShot(300, [w] { QTest::keyClick(w, Qt::Key_U); });
    QTimer::singleShot(450, [w] { QTest::keyClick(w, Qt::Key_Z); });
    QTimer::singleShot(600, [w] { QTest::keyClick(w, Qt::Key_G); });
    // run() sets the speed from the config (40mm/min), so allow time for the move
    QTimer::singleShot(4'000, [w] { w->close(); });

    const auto start = std::chrono::steady_clock::now();
    controller.run();
    REQUIRE(std::chrono::steady_clock::now() - start < std::chrono::seconds(10));

    REQUIRE(tm.model.isQuitting());
    REQUIRE(dialogShown);
    REQUIRE(tm.model.getDisplayUnits() == mgo::DisplayUnits::Metric); // (toggled twice)
    REQUIRE(tm.model.getAxis1MotorPosition() == Approx(0.5).margin(0.001));
}

TEST_CASE("UI: keys wait while a nudge is moving, except Space")
{
    SlowZTestModel tm;
    mgo::Controller controller(&tm.model);
    QWidget* w = mainWindow();
    REQUIRE(w);
    const double speed = tm.model.getAxis1MotorSpeed();

    // A (nudge left 0.05mm), then Shift-D (right 0.01mm) while the first is
    // still moving: the second waits, so each moves its full amount
    QTest::keyClick(w, Qt::Key_A);
    controller.tick();
    REQUIRE(tm.model.isMotionSequenceRunning());
    QTest::keyClick(w, Qt::Key_D, Qt::ShiftModifier);
    controller.tick();
    REQUIRE(tm.model.isMotionSequenceRunning()); // (still the first nudge)
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::abs(std::abs(tm.model.getAxis1MotorPosition()) - 0.04) > 0.0001
           || tm.model.isMotionSequenceRunning()) {
        REQUIRE(std::chrono::steady_clock::now() < deadline);
        controller.tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(tm.model.getAxis1MotorSpeed() == Approx(speed));

    // Space, though, stops a nudge part way (and the speed is restored)
    const double position = tm.model.getAxis1MotorPosition();
    QTest::keyClick(w, Qt::Key_A);
    controller.tick();
    REQUIRE(tm.model.isMotionSequenceRunning());
    QTest::qWait(50);
    QTest::keyClick(w, Qt::Key_Space);
    controller.tick();
    REQUIRE(!tm.model.isMotionSequenceRunning());
    REQUIRE(!tm.model.isAxis1MotorRunning());
    const double moved = std::abs(tm.model.getAxis1MotorPosition() - position);
    REQUIRE(moved > 0.0);
    REQUIRE(moved < 0.05);
    REQUIRE(tm.model.getAxis1MotorSpeed() == Approx(speed));
}

TEST_CASE("UI: multi-pass through the controller")
{
    TestModel tm;
    mgo::Controller controller(&tm.model);
    QWidget* w = mainWindow();
    REQUIRE(w);
    // (run() would set these from the presets)
    tm.model.setAxis1MotorSpeed(500.0);
    tm.model.setAxis2MotorSpeed(20.0);
    // Mem 1 where the tool is (0, 0); Mem 2 at Z 0.3, X 0.02
    tm.model.axis1StorePosition(0.0);
    tm.model.axis2StorePosition(0.0);
    tm.model.selectNextMemorySlot();
    tm.model.axis1StorePosition(0.3);
    tm.model.axis2StorePosition(0.02);
    tm.model.selectPreviousMemorySlot();

    // F2, M: the dialog asks for the step-over
    whenDialogShows([](QWidget* dialog) {
        typeInto(dialog, "0.01");
        QTest::keyClick(inputWidget(dialog), Qt::Key_Return);
    });
    press(w, controller, Qt::Key_F2);
    press(w, controller, Qt::Key_M);
    REQUIRE(tm.model.getMode() == mgo::Mode::MultiPass);
    REQUIRE(tm.model.getMultiPassOptions().feedPerPass == Approx(0.01));
    REQUIRE(!tm.model.multiPass().inProgress());

    // Enter starts it
    press(w, controller, Qt::Key_Return);
    REQUIRE(tm.model.multiPass().inProgress());
    REQUIRE(tm.model.multiPass().totalPasses() == 3);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (tm.model.multiPass().inProgress()) {
        REQUIRE(std::chrono::steady_clock::now() < deadline);
        controller.tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    INFO("Stopped: " << tm.model.multiPass().message() << " / " << tm.model.getWarning());
    REQUIRE(tm.model.multiPass().state() == mgo::MultiPass::State::Finished);
    REQUIRE(tm.model.getAxis1MotorPosition() == Approx(0.0).margin(0.0001));
    REQUIRE(tm.model.getAxis2MotorPosition() == Approx(0.02).margin(0.0001));
}

TEST_CASE("UI: the controller carries on while a dialog is open")
{
    // e.g. a driver alarm is caught at once, rather than when the dialog closes
    AlarmTestModel tm;
    mgo::Controller controller(&tm.model);
    QWidget* w = mainWindow();
    REQUIRE(w);

    std::string faultWhileOpen;
    whenDialogShows([&](QWidget* dialog) {
        tm.gpio.setInputPinLevel(ALARM_PIN, mgo::PinState::low);
        QTest::qWait(300);
        faultWhileOpen = tm.model.getFaultMessage();
        QTest::keyClick(inputWidget(dialog), Qt::Key_Escape);
    });
    QTimer::singleShot(150, [w] { QTest::keyClick(w, Qt::Key_Z); });
    QTimer::singleShot(300, [w] { QTest::keyClick(w, Qt::Key_G); });
    QTimer::singleShot(2'000, [w] { w->close(); });
    controller.run();
    REQUIRE(faultWhileOpen.find("Z MOTOR FAULT") != std::string::npos);
}

TEST_CASE("UI: closing the window while a dialog is open cancels it, and quits")
{
    TestModel tm;
    mgo::Controller controller(&tm.model);
    QWidget* w = mainWindow();
    REQUIRE(w);

    bool dialogShown = false;
    whenDialogShows([&](QWidget*) {
        dialogShown = true;
        w->close();
    });
    QTimer::singleShot(150, [w] { QTest::keyClick(w, Qt::Key_Z); });
    QTimer::singleShot(300, [w] { QTest::keyClick(w, Qt::Key_G); });
    // (In case it doesn't quit)
    bool timedOut = false;
    QTimer::singleShot(5'000, [&] {
        timedOut = true;
        tm.model.quit();
    });
    controller.run();
    REQUIRE(dialogShown);
    REQUIRE(!timedOut);
    REQUIRE(tm.model.isQuitting());
    REQUIRE(QApplication::activeModalWidget() == nullptr);
}

TEST_CASE("UI: run() returns at once if already quitting")
{
    TestModel tm;
    mgo::Controller controller(&tm.model);
    tm.model.quit();
    controller.run();
    REQUIRE(tm.model.isQuitting());
}

int main(int argc, char* argv[])
{
    // No windows on screen, and no dependency on a display
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication::setAttribute(Qt::AA_MacDontSwapCtrlAndMeta);
    QApplication app(argc, argv);
    // The offscreen platform's default font ("Sans Serif") may not exist, which
    // makes Qt search for a substitute (and say so), so use the app's own font.
    // (The tests are run from the source root, where the font is)
    QFile fontFile("./lc_font.ttf");
    if (fontFile.open(QIODevice::ReadOnly)) {
        const int id = QFontDatabase::addApplicationFontFromData(fontFile.readAll());
        if (id >= 0 && !QFontDatabase::applicationFontFamilies(id).isEmpty()) {
            QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(id).at(0)));
        }
    }
    return Catch::Session().run(argc, argv);
}
