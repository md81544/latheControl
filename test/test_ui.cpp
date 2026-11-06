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
#include "view_qt.h"

#include <QApplication>
#include <QFile>
#include <QFontDatabase>
#include <QTest>
#include <QTimer>

#include <chrono>
#include <cmath>
#include <functional>
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
    const auto rc = view.getInput(
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
    const auto rc = view.getInput(mgo::Input::Type::Numeric, "Position", {}, "1.5");
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
    const auto rc = view.getInput(
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
        = view.getInput(mgo::Input::Type::Text, "Name", { "[&X] an option" }, "default");
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
    const auto rc = view.getInput(mgo::Input::Type::PressAnyKey, "Help", { "Some text" });
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
    controller.processKeyPress(); // runs the dialog
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
    REQUIRE(tm.model.getEnabledFunction() == mgo::Mode::Taper);
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

    // F2 then P opens it; choose MT2, tool moving in, and OK
    whenDialogShows([](QWidget* dialog) {
        QTest::keyClick(inputWidget(dialog), Qt::Key_Down);
        QTest::keyClick(inputWidget(dialog), Qt::Key_Down);
        QTest::keyClick(inputWidget(dialog), Qt::Key_I);
        QTest::keyClick(inputWidget(dialog), Qt::Key_Return);
    });
    press(w, controller, Qt::Key_F2);
    press(w, controller, Qt::Key_P);
    REQUIRE(tm.model.getEnabledFunction() == mgo::Mode::Taper);
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
    REQUIRE(view.getTaperAngle(0.0) == Approx(-12.5));

    // Esc cancels
    whenDialogShows([](QWidget* dialog) { QTest::keyClick(inputWidget(dialog), Qt::Key_Escape); });
    REQUIRE(!view.getTaperAngle(0.0).has_value());

    // Enter does nothing without a valid angle (here, just a ".")
    whenDialogShows([](QWidget* dialog) {
        typeInto(dialog, ".");
        QTest::keyClick(inputWidget(dialog), Qt::Key_Return);
        REQUIRE(dialog->isVisible());
        QTest::keyClick(inputWidget(dialog), Qt::Key_Escape);
    });
    REQUIRE(!view.getTaperAngle(0.0).has_value());

    // It starts with the taper in use: a preset (with its direction)...
    whenDialogShows([](QWidget* dialog) { QTest::keyClick(inputWidget(dialog), Qt::Key_Return); });
    REQUIRE(view.getTaperAngle(-1.4377) == Approx(-1.4377));
    // ...or a custom angle
    whenDialogShows([](QWidget* dialog) { QTest::keyClick(inputWidget(dialog), Qt::Key_Return); });
    REQUIRE(view.getTaperAngle(7.25) == Approx(7.25));
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
