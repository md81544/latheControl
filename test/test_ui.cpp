// Tests of the Qt user interface. These use Qt's "offscreen" platform, so no
// windows appear on screen and no real keyboard input is involved: key presses
// are sent directly to the widgets with QTest.

#define CATCH_CONFIG_RUNNER
#include "catch.hpp"

#include "configreader.h"
#include "controller.h"
#include "keycodes.h"
#include "log.h"
#include "menu.h"
#include "mockgpio.h"
#include "model.h"
#include "tapers.h"
#include "view_qt.h"

#include <QApplication>
#include <QFile>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QTest>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string_view>
#include <thread>
#include <vector>

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

// Arranges for fn to be called with the modal dialog once it is showing,
// and active (until then, its keyboard focus may not be where it will be).
// (Dialogs block in their own event loop, so this must be set up beforehand.)
void whenDialogShows(std::function<void(QWidget*)> fn)
{
    auto* timer = new QTimer();
    timer->setInterval(10);
    QObject::connect(timer, &QTimer::timeout, [timer, fn]() {
        QWidget* dialog = QApplication::activeModalWidget();
        if (dialog && dialog->isActiveWindow()) {
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
    std::optional<std::vector<std::string>> listItems = std::nullopt,
    mgo::calc::Context calculator = {})
{
    std::optional<mgo::Input::Return> result;
    view.openInput(
        type,
        prompt,
        std::move(additionalText),
        [&result](const mgo::Input::Return& rc) { result = rc; },
        defaultEntry,
        std::move(listItems),
        std::move(calculator));
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

// All the text a dialog shows, one label per line
QString dialogText(QWidget* dialog)
{
    QStringList lines;
    for (const QLabel* label : dialog->findChildren<QLabel*>()) {
        lines << label->text();
    }
    return lines.join('\n');
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
    // keyboard would; releasing Alt always sends a rapid's release, which the
    // controller treats as Space, a stop, unless a rapid is running)
    QTest::keyClick(w, Qt::Key_W, Qt::AltModifier);
    REQUIRE(allKeys(view) == Keys { mgo::key::AltW, mgo::key::RAPID_RELEASED });
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
    // Releasing it stops (the controller slows it down if it can): once for
    // the Alt key itself, at once, and once for the arrow, a moment later (in
    // case it was a repeat: see the next test)
    QTest::keyRelease(w, Qt::Key_Left, Qt::AltModifier);
    REQUIRE(allKeys(view) == Keys { mgo::key::RAPID_RELEASED });
    QTest::qWait(100);
    REQUIRE(allKeys(view) == Keys { mgo::key::RAPID_RELEASED });

    // Alt on its own does nothing until released
    QTest::keyPress(w, Qt::Key_Alt);
    REQUIRE(allKeys(view).empty());
    QTest::keyRelease(w, Qt::Key_Alt);
    REQUIRE(allKeys(view) == Keys { mgo::key::RAPID_RELEASED });

    // Right Alt is often AltGr on Linux, which Qt doesn't report as an Alt
    // modifier: AltGr + arrow must still be a rapid, and stop on release
    // (The AltGr events are sent directly, as QTest's helpers complain about
    // AltGr, having no character for it)
    QKeyEvent altGrPress(QEvent::KeyPress, Qt::Key_AltGr, Qt::NoModifier);
    QApplication::sendEvent(w, &altGrPress);
    QTest::keyPress(w, Qt::Key_Up);
    REQUIRE(allKeys(view) == Keys { mgo::key::AltUp });
    QTest::keyRelease(w, Qt::Key_Up);
    REQUIRE(allKeys(view).empty());
    QTest::qWait(100);
    REQUIRE(allKeys(view) == Keys { mgo::key::RAPID_RELEASED });
    QKeyEvent altGrRelease(QEvent::KeyRelease, Qt::Key_AltGr, Qt::NoModifier);
    QApplication::sendEvent(w, &altGrRelease);
    REQUIRE(allKeys(view) == Keys { mgo::key::RAPID_RELEASED });

    // A plain arrow, without Alt, doesn't stop on release
    QTest::keyPress(w, Qt::Key_Right);
    REQUIRE(allKeys(view) == Keys { mgo::key::RIGHT });
    QTest::keyRelease(w, Qt::Key_Right);
    REQUIRE(allKeys(view).empty());
}

TEST_CASE("UI: a rapid's key repeating, unmarked, doesn't stop it")
{
    TestModel tm;
    mgo::ViewQt view;
    view.initialise(tm.model);
    QWidget* w = mainWindow();
    REQUIRE(w);

    QTest::keyPress(w, Qt::Key_Alt);
    QTest::keyPress(w, Qt::Key_Left, Qt::AltModifier);
    REQUIRE(allKeys(view) == Keys { mgo::key::AltLeft });
    // A held key's repeats, under X11, are a release and a press. If Qt
    // doesn't mark them as repeats (seen on the Pi), they must still neither
    // stop the rapid nor start it again
    for (int n = 0; n < 5; ++n) {
        QKeyEvent release(QEvent::KeyRelease, Qt::Key_Left, Qt::AltModifier);
        QApplication::sendEvent(w, &release);
        QKeyEvent press(QEvent::KeyPress, Qt::Key_Left, Qt::AltModifier);
        QApplication::sendEvent(w, &press);
        QTest::qWait(30);
    }
    QTest::qWait(100);
    REQUIRE(allKeys(view).empty());
    // Letting go of Alt stops it straight away. Then the arrow's repeats (no longer
    // with Alt) mustn't start a jog
    QTest::keyRelease(w, Qt::Key_Alt);
    REQUIRE(allKeys(view) == Keys { mgo::key::RAPID_RELEASED });
    for (int n = 0; n < 3; ++n) {
        QKeyEvent release(QEvent::KeyRelease, Qt::Key_Left, Qt::NoModifier);
        QApplication::sendEvent(w, &release);
        QKeyEvent press(QEvent::KeyPress, Qt::Key_Left, Qt::NoModifier);
        QApplication::sendEvent(w, &press);
        QTest::qWait(30);
    }
    REQUIRE(allKeys(view).empty());
    // Letting go of the arrow, for real: (another) stop, and it's over, so
    // the arrow is an ordinary key again
    QTest::keyRelease(w, Qt::Key_Left);
    QTest::qWait(100);
    REQUIRE(allKeys(view) == Keys { mgo::key::RAPID_RELEASED });
    QTest::keyPress(w, Qt::Key_Left);
    REQUIRE(allKeys(view) == Keys { mgo::key::LEFT });
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

TEST_CASE("UI: Up and Down auto-repeat while the menu is open")
{
    TestModel tm;
    mgo::Menu menu({ { "Cat", "One", mgo::key::a }, { "Cat", "Two", mgo::key::b } });
    mgo::ViewQt view;
    view.setMenu(&menu);
    view.initialise(tm.model);
    QWidget* w = mainWindow();
    REQUIRE(w);

    const auto repeat = [w](int qtKey, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        QKeyEvent press(QEvent::KeyPress, qtKey, modifiers, QString(), true);
        QApplication::sendEvent(w, &press);
    };
    // Closed, held arrows mustn't keep jogging
    repeat(Qt::Key_Down);
    REQUIRE(nextKey(view) == mgo::key::None);

    menu.open();
    repeat(Qt::Key_Down);
    repeat(Qt::Key_Down);
    repeat(Qt::Key_Up);
    REQUIRE(allKeys(view) == Keys { mgo::key::DOWN, mgo::key::DOWN, mgo::key::UP });
    // Only Up and Down, and not rapids
    repeat(Qt::Key_Right);
    repeat(Qt::Key_Down, Qt::AltModifier);
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
    const auto rc
        = getInput(view, mgo::Input::Type::Numeric, "Position", { "[&D] set as diameter" }, "1.5");
    REQUIRE(!rc.cancelled);
    REQUIRE(rc.number == Approx(5.2));
    REQUIRE(rc.optionsSelected == std::set<char> { 'd' });
}

namespace {

// The text of the label in a dialog showing text which starts with start
// ("" if there's none)
std::string labelStartingWith(QWidget* dialog, const std::string& start)
{
    for (const QLabel* label : dialog->findChildren<QLabel*>()) {
        const std::string text = label->text().toStdString();
        if (text.starts_with(start)) {
            return text;
        }
    }
    return "";
}

QLineEdit* lineEdit(QWidget* dialog)
{
    return qobject_cast<QLineEdit*>(inputWidget(dialog));
}

} // end anonymous namespace

TEST_CASE("UI: numeric dialog, with a sum typed after =")
{
    TestModel tm;
    mgo::ViewQt view;
    view.initialise(tm.model);
    const mgo::calc::Context calculator { .memory = { { "m1x", 4.5 }, { "m2x", std::nullopt } },
                                          .unit = "mm" };

    SECTION("Enter puts the value in, and Enter again accepts it")
    {
        whenDialogShows([](QWidget* dialog) {
            QTest::keyClicks(inputWidget(dialog), "=3/8in");
            REQUIRE(labelStartingWith(dialog, "=3/8in = 9.525") != "");
            QTest::keyClick(inputWidget(dialog), Qt::Key_Return);
            REQUIRE(dialog->isVisible());
            REQUIRE(lineEdit(dialog)->text() == "9.525");
            REQUIRE(labelStartingWith(dialog, "=3/8in gave 9.525") != "");
            QTest::keyClick(inputWidget(dialog), Qt::Key_Return);
        });
        const auto rc = getInput(
            view, mgo::Input::Type::Numeric, "Position", {}, "0", std::nullopt, calculator);
        REQUIRE(!rc.cancelled);
        REQUIRE(rc.number == Approx(9.525));
    }
    SECTION("\" is inches, as in is")
    {
        whenDialogShows([](QWidget* dialog) {
            QTest::keyClicks(inputWidget(dialog), "=1/8\"");
            REQUIRE(lineEdit(dialog)->text() == "=1/8\"");
            QTest::keyClick(inputWidget(dialog), Qt::Key_Return);
            QTest::keyClick(inputWidget(dialog), Qt::Key_Return);
        });
        const auto rc = getInput(
            view, mgo::Input::Type::Numeric, "Position", {}, "0", std::nullopt, calculator);
        REQUIRE(rc.number == Approx(3.175));
    }
    SECTION("p is the value shown before, which typing = replaced")
    {
        whenDialogShows([](QWidget* dialog) {
            QTest::keyClicks(inputWidget(dialog), "=p/4");
            QTest::keyClick(inputWidget(dialog), Qt::Key_Return);
            QTest::keyClick(inputWidget(dialog), Qt::Key_Return);
        });
        const auto rc = getInput(
            view, mgo::Input::Type::Numeric, "Position", {}, "10", std::nullopt, calculator);
        REQUIRE(rc.number == Approx(2.5));
    }
    SECTION("Letters in a sum are typed, not options; Alt toggles an option")
    {
        whenDialogShows([](QWidget* dialog) {
            QTest::keyClicks(inputWidget(dialog), "=m1x*2");
            REQUIRE(lineEdit(dialog)->text() == "=m1x*2");
            QTest::keyClick(inputWidget(dialog), Qt::Key_M, Qt::AltModifier);
            QTest::keyClick(inputWidget(dialog), Qt::Key_Return);
            QTest::keyClick(inputWidget(dialog), Qt::Key_Return);
        });
        const auto rc = getInput(
            view,
            mgo::Input::Type::Numeric,
            "Position",
            { "[&M] an option" },
            "0",
            std::nullopt,
            calculator);
        REQUIRE(rc.number == Approx(9.0));
        REQUIRE(rc.optionsSelected == std::set<char> { 'm' });
    }
    SECTION("A sum which can't be worked out stays, saying why, and isn't accepted")
    {
        whenDialogShows([](QWidget* dialog) {
            QTest::keyClicks(inputWidget(dialog), "=m2x+1");
            QTest::keyClick(inputWidget(dialog), Qt::Key_Return);
            REQUIRE(dialog->isVisible());
            REQUIRE(lineEdit(dialog)->text() == "=m2x+1");
            REQUIRE(labelStartingWith(dialog, "Mem 2 has no X") != "");
            // (Spaces, and other characters, can't be typed in a sum)
            QTest::keyClicks(inputWidget(dialog), " x");
            REQUIRE(lineEdit(dialog)->text() == "=m2x+1x");
            QTest::keyClick(inputWidget(dialog), Qt::Key_Escape);
        });
        const auto rc = getInput(
            view, mgo::Input::Type::Numeric, "Position", {}, "0", std::nullopt, calculator);
        REQUIRE(rc.cancelled);
    }
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
    const auto rc = getInput(
        view,
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
    const auto rc = getInput(view, mgo::Input::Type::Text, "Name", { "[&X] an option" }, "default");
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

TEST_CASE("UI: press any key isn't closed by a key held from before (e.g. a rapid's)")
{
    TestModel tm;
    mgo::ViewQt view;
    view.initialise(tm.model);

    bool stillOpen = false;
    whenDialogShows([&stillOpen](QWidget* dialog) {
        QWidget* input = inputWidget(dialog);
        // A rapid (Alt + an arrow), the arrow on its own (as its repeats can
        // be, on the Pi), and a key marked as a repeat
        QTest::keyClick(input, Qt::Key_Right, Qt::AltModifier);
        QTest::keyClick(input, Qt::Key_Right);
        QKeyEvent repeat(QEvent::KeyPress, Qt::Key_Q, Qt::NoModifier, "q", true);
        QApplication::sendEvent(input, &repeat);
        stillOpen = dialog->isVisible();
        // A fresh key closes it
        QTest::keyClick(input, Qt::Key_Q);
    });
    const auto rc = getInput(view, mgo::Input::Type::PressAnyKey, "Warning", { "Some text" });
    REQUIRE(stillOpen);
    REQUIRE(!rc.cancelled);
}

TEST_CASE("UI: Z go-to through the controller")
{
    // End to end: "z" then "g" opens the go-to dialog; entering 5 should move
    // the Z axis to 5.00
    TestModel tm;
    mgo::Controller controller(&tm.model); // creates the view and window
    tm.model.setMotorSpeed(1, 1'000.0);
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
    while (std::abs(tm.model.motorPosition(1) - 5.0) > 0.001
           && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(tm.model.motorPosition(1) == Approx(5.0).margin(0.001));
}

TEST_CASE("UI: a go-to with a sum, through the controller")
{
    // Mem 1's Z is 3, so =m1z+2 goes to 5. Or, with Z at 3, p is 3 (where Z
    // is), not the entry's 0
    TestModel tm;
    mgo::Controller controller(&tm.model);
    tm.model.setMotorSpeed(1, 1'000.0);
    std::string sum;
    SECTION("A memory slot")
    {
        tm.model.storePosition(1, 3.0);
        sum = "=M1Z+2";
    }
    SECTION("p, where the axis is")
    {
        tm.model.setPosition(1, 3.0);
        sum = "=p+2";
    }
    QWidget* w = mainWindow();
    REQUIRE(w);

    QTest::keyClick(w, Qt::Key_Z);
    controller.processKeyPress();
    bool dialogShown = false;
    whenDialogShows([&dialogShown, sum](QWidget* dialog) {
        dialogShown = true;
        REQUIRE(labelStartingWith(dialog, "(In a sum, after =, p is where Z is now)") != "");
        QTest::keyClicks(inputWidget(dialog), QString::fromStdString(sum));
        QTest::keyClick(inputWidget(dialog), Qt::Key_Return);
        REQUIRE(lineEdit(dialog)->text() == "5");
        QTest::keyClick(inputWidget(dialog), Qt::Key_Return);
    });
    QTest::keyClick(w, Qt::Key_G);
    controller.processKeyPress(); // opens the dialog
    REQUIRE(waitForDialogToClose());
    REQUIRE(dialogShown);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::abs(tm.model.motorPosition(1) - 5.0) > 0.001
           && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(tm.model.motorPosition(1) == Approx(5.0).margin(0.001));
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

TEST_CASE("UI: letting go of Alt with no rapid running stops everything, as Space")
{
    TestModel tm;
    mgo::Controller controller(&tm.model);
    tm.model.setMotorSpeed(1, 100.0);
    QWidget* w = mainWindow();
    REQUIRE(w);

    // A jog (not a rapid), then Alt pressed and let go
    tm.model.move(1, mgo::axisDirection(mgo::ZDirection::Left));
    REQUIRE(tm.model.isMotorRunning(1));
    QTest::keyPress(w, Qt::Key_Alt);
    QTest::keyRelease(w, Qt::Key_Alt);
    controller.processKeyPress();
    REQUIRE(!tm.model.isMotorRunning(1));
}

TEST_CASE("UI: menu through the controller")
{
    TestModel tm;
    mgo::Controller controller(&tm.model);
    tm.model.setMotorSpeed(1, 1'000.0);
    QWidget* w = mainWindow();
    REQUIRE(w);

    // Move Z away from zero, then zero it from the menu
    tm.model.goToPosition(1, 2.0);
    REQUIRE(waitUntil([&] { return std::abs(tm.model.motorPosition(1) - 2.0) < 0.001; }));
    press(w, controller, Qt::Key_Tab);
    REQUIRE(controller.isMenuOpen());
    // "Position" is the fifth category; "Zero Z" is its first command
    for (int n = 0; n < 4; ++n) {
        press(w, controller, Qt::Key_Down);
    }
    press(w, controller, Qt::Key_Return);
    press(w, controller, Qt::Key_Return);
    REQUIRE(!controller.isMenuOpen());
    REQUIRE(tm.model.motorPosition(1) == 0.0);

    // While the menu is open, Space still stops all motors, and the menu
    // stays open
    tm.model.goToPosition(1, 50.0);
    REQUIRE(waitUntil([&] { return tm.model.isMotorRunning(1); }));
    press(w, controller, Qt::Key_Tab);
    REQUIRE(controller.isMenuOpen());
    REQUIRE(tm.model.isMotorRunning(1)); // opening the menu doesn't stop anything
    press(w, controller, Qt::Key_Space);
    REQUIRE(!tm.model.isMotorRunning(1));
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

TEST_CASE("UI: F9 clears the warning, and nothing else")
{
    // (On the mill, Esc was pressed to clear one, which ended a multi-pass)
    TestModel tm;
    mgo::Controller controller(&tm.model);
    QWidget* w = mainWindow();
    REQUIRE(w);
    REQUIRE(!tm.model.warningClearable());
    tm.model.goToPosition(1, 50.0);
    REQUIRE(waitUntil([&] { return tm.model.isMotorRunning(1); }));
    // (Refused while Z moves: a warning)
    tm.model.lockAxis(1);
    REQUIRE(!tm.model.getWarning().empty());
    REQUIRE(tm.model.warningClearable());
    press(w, controller, Qt::Key_F9);
    REQUIRE(tm.model.getWarning().empty());
    REQUIRE(!tm.model.warningClearable());
    REQUIRE(tm.model.isMotorRunning(1));
    // And with the menu open (which it stays)
    tm.model.lockAxis(1);
    REQUIRE(!tm.model.getWarning().empty());
    press(w, controller, Qt::Key_Tab);
    REQUIRE(controller.isMenuOpen());
    press(w, controller, Qt::Key_F9);
    REQUIRE(tm.model.getWarning().empty());
    REQUIRE(controller.isMenuOpen());
    REQUIRE(tm.model.isMotorRunning(1));
    press(w, controller, Qt::Key_Space);
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

// Here: Z 12.5, X 10 (X measured from the centre; infeed is -X). Mem 1:
// Z 2, X 9.5. Mem 2: Z 12.5, X 10. Mem 3: Z 4 only
mgo::MultiPassDialogInfo multiPassInfo()
{
    mgo::MultiPassDialogInfo info;
    info.cut = { .label = "Z", .unit = "mm", .decimals = 2, .displayPerMm = 1.0 };
    info.feed = { .label = "X", .unit = "mm", .decimals = 2, .displayPerMm = 1.0 };
    info.positions.cutHere = 12.5;
    info.positions.feedHere = 10.0;
    info.positions.cutMemory = { 2.0, 12.5, 4.0, std::nullopt };
    info.positions.feedMemory = { 9.5, 10.0, std::nullopt, std::nullopt };
    info.positions.feedIntoWork = -1.0;
    info.diameterKnown = true;
    return info;
}

std::optional<mgo::MultiPassSetup>
multiPassDialog(mgo::ViewQt& view, const mgo::MultiPassDialogInfo& info)
{
    bool done = false;
    std::optional<mgo::MultiPassSetup> setup;
    view.openMultiPassDialog(info, [&](std::optional<mgo::MultiPassSetup> s) {
        setup = s;
        done = true;
    });
    REQUIRE(QTest::qWaitFor([&done] { return done; }, 5'000));
    return setup;
}

void key(QWidget* dialog, Qt::Key k, int times = 1)
{
    for (int n = 0; n < times; ++n) {
        QTest::keyClick(inputWidget(dialog), k);
    }
}

// With LC_SNAPSHOT_DIR set, saves an image of a dialog there (to look at)
void snapshot(QWidget* dialog, const std::string& name)
{
    if (const char* dir = std::getenv("LC_SNAPSHOT_DIR")) {
        QTest::qWait(50);
        dialog->grab().save(QString::fromStdString(std::string(dir) + "/" + name + ".png"));
    }
}

} // end anonymous namespace

TEST_CASE("UI: every menu row fits, without overlapping its shortcut")
{
    TestModel tm;
    mgo::ViewQt view;
    view.initialise(tm.model);
    // (The descriptions depend on the config, the units and the toggles)
    for (const char* file : { "lc.cfg", "lc_mill.cfg" }) {
        const mgo::ConfigReader config(file);
        for (const auto units : { mgo::DisplayUnits::Metric, mgo::DisplayUnits::Imperial }) {
            for (const bool on : { false, true }) {
                // (Threading's auto-retract and finishing on the scale)
                for (const auto& c : mgo::buildCommands(config, units, on, on)) {
                    INFO(file << ": " << c.description);
                    CHECK(view.menuRowFits(
                        c.description, c.hotkey, mgo::shortcutText(c.key, config)));
                }
            }
        }
    }
}

TEST_CASE("UI: threading auto-retract is turned on and off from the menu")
{
    // It starts as the config says, and Tab, M (Modes), A toggles it
    MapConfigReader config { { { "ThreadingAutoRetract", "true" } } };
    mgo::MockGpio gpio { false };
    mgo::Model model { gpio, config };
    mgo::Controller controller(&model);
    QWidget* w = mainWindow();
    REQUIRE(w);
    REQUIRE(model.threadingAutoRetract());
    // The menu stays open, so its wording shows the new state
    press(w, controller, Qt::Key_Tab);
    press(w, controller, Qt::Key_M);
    for (const bool expected : { false, true }) {
        press(w, controller, Qt::Key_A);
        REQUIRE(controller.isMenuOpen());
        REQUIRE(model.threadingAutoRetract() == expected);
    }
    // Enter, with it highlighted (as A left it), closes the menu, and leaves
    // it as it is
    press(w, controller, Qt::Key_Return);
    REQUIRE(!controller.isMenuOpen());
    REQUIRE(model.threadingAutoRetract());
    // Right toggles it too (as the gamepad's D-pad does), and the menu stays
    // open in Modes: another of its hotkeys chooses that command
    press(w, controller, Qt::Key_Tab);
    press(w, controller, Qt::Key_M);
    press(w, controller, Qt::Key_A);
    REQUIRE(!model.threadingAutoRetract());
    press(w, controller, Qt::Key_Right);
    REQUIRE(model.threadingAutoRetract());
    REQUIRE(controller.isMenuOpen());
    press(w, controller, Qt::Key_H);
    REQUIRE(!controller.isMenuOpen());
    REQUIRE(model.getMode() == mgo::Mode::Taper);
}

TEST_CASE("UI: multi-pass dialog, from here by distances")
{
    TestModel tm;
    mgo::ViewQt view;
    view.initialise(tm.model);
    const auto info = multiPassInfo();

    // It starts on Z's distance, as that has to be typed. Enter does nothing
    // until it is
    whenDialogShows([](QWidget* dialog) {
        snapshot(dialog, "multipass_empty");
        key(dialog, Qt::Key_Return);
        REQUIRE(dialog->isVisible());
        typeInto(dialog, "-7");
        key(dialog, Qt::Key_Down);
        typeInto(dialog, "0.5"); // (X's infeed)
        key(dialog, Qt::Key_Down, 2); // (past X's end diameter)
        typeInto(dialog, "0.1");
        key(dialog, Qt::Key_P); // (pause)
        snapshot(dialog, "multipass_here");
        key(dialog, Qt::Key_Return);
    });
    auto setup = multiPassDialog(view, info);
    REQUIRE(setup.has_value());
    REQUIRE(!setup->startMemory);
    REQUIRE(!setup->cutEndMemory);
    REQUIRE(!setup->feedEndMemory);
    REQUIRE(setup->cutDistance == Approx(-7.0));
    REQUIRE(setup->feedInfeed == Approx(0.5));
    REQUIRE(setup->feedPerPass == Approx(0.1));
    REQUIRE(setup->pause);
    REQUIRE(!setup->retract);
    REQUIRE(setup->retractBeforeMoveToStart);

    // Esc cancels
    whenDialogShows([](QWidget* dialog) { key(dialog, Qt::Key_Escape); });
    REQUIRE(!multiPassDialog(view, info).has_value());
}

TEST_CASE("UI: multi-pass dialog, a sum in an entry")
{
    TestModel tm;
    mgo::ViewQt view;
    view.initialise(tm.model);
    auto info = multiPassInfo();
    info.calculatorMemory = { { "m1z", 2.0 }, { "m2z", 12.5 }, { "m1x", 9.5 } };

    whenDialogShows([](QWidget* dialog) {
        // Z's distance, from memory slots: Enter puts it in, without
        // accepting the dialog
        typeInto(dialog, "=m1z-m2z");
        snapshot(dialog, "multipass_sum");
        key(dialog, Qt::Key_Return);
        REQUIRE(dialog->isVisible());
        REQUIRE(lineEdit(dialog)->text() == "-10.5");
        key(dialog, Qt::Key_Down);
        typeInto(dialog, "=1/2"); // (X's infeed)
        // Moving to another line puts it in, too
        key(dialog, Qt::Key_Down, 2); // (past X's end diameter)
        // The step-over can't be negative: refused, it stays
        typeInto(dialog, "=0-0.1");
        key(dialog, Qt::Key_Return);
        REQUIRE(lineEdit(dialog)->text() == "=0-0.1");
        REQUIRE(labelStartingWith(dialog, "-0.1: it can't be negative here") != "");
        snapshot(dialog, "multipass_sum_refused");
        key(dialog, Qt::Key_Up);
        REQUIRE(lineEdit(dialog)->text() == "=0-0.1"); // (still there)
        key(dialog, Qt::Key_Backspace, 6);
        typeInto(dialog, "=0.5/5");
        key(dialog, Qt::Key_Return);
        key(dialog, Qt::Key_Return);
    });
    auto setup = multiPassDialog(view, info);
    REQUIRE(setup.has_value());
    REQUIRE(setup->cutDistance == Approx(-10.5));
    REQUIRE(setup->feedInfeed == Approx(0.5));
    REQUIRE(setup->feedPerPass == Approx(0.1));
}

TEST_CASE("UI: multi-pass dialog, R cycles the retract")
{
    TestModel tm;
    mgo::ViewQt view;
    view.initialise(tm.model);
    // No, every pass, the last few, then no again
    const auto withPresses = [&view](int presses) {
        whenDialogShows([presses](QWidget* dialog) {
            typeInto(dialog, "-7");
            key(dialog, Qt::Key_R, presses);
            if (presses == 2) {
                snapshot(dialog, "multipass_retract_last");
            }
            key(dialog, Qt::Key_Return);
        });
        auto setup = multiPassDialog(view, multiPassInfo());
        REQUIRE(setup.has_value());
        return *setup;
    };
    auto setup = withPresses(1);
    REQUIRE(setup.retract);
    REQUIRE(!setup.retractLastOnly);
    setup = withPresses(2);
    REQUIRE(setup.retract);
    REQUIRE(setup.retractLastOnly);
    setup = withPresses(3);
    REQUIRE(!setup.retract);
    REQUIRE(!setup.retractLastOnly);
}

TEST_CASE("UI: multi-pass dialog, an end diameter")
{
    TestModel tm;
    mgo::ViewQt view;
    view.initialise(tm.model);
    // X 10 is a diameter of 20: turning down to 19.2 is an infeed of 0.4
    whenDialogShows([](QWidget* dialog) {
        typeInto(dialog, "-7");
        key(dialog, Qt::Key_Down, 2);
        typeInto(dialog, "19.2");
        snapshot(dialog, "multipass_diameter");
        key(dialog, Qt::Key_Return);
    });
    auto setup = multiPassDialog(view, multiPassInfo());
    REQUIRE(setup.has_value());
    REQUIRE(setup->feedInfeed == Approx(0.4));

    // A diameter bigger than the start's can't be used
    whenDialogShows([](QWidget* dialog) {
        typeInto(dialog, "-7");
        key(dialog, Qt::Key_Down, 2);
        typeInto(dialog, "21");
        key(dialog, Qt::Key_Return);
        REQUIRE(dialog->isVisible());
        key(dialog, Qt::Key_Escape);
    });
    REQUIRE(!multiPassDialog(view, multiPassInfo()).has_value());
}

TEST_CASE("UI: knurling dialog")
{
    TestModel tm;
    mgo::ViewQt view;
    view.initialise(tm.model);
    auto info = multiPassInfo();
    info.knurling = mgo::MultiPassDialogInfo::Knurling { .maxCutSpeed = 800.0,
                                                         .minimumSpindleRpm = 5.0,
                                                         .minimumInfeedMm = 0.02 };
    info.setup.knurlStarts = 63;
    info.setup.knurlHelixAngleDeg = 45.0;

    whenDialogShows([](QWidget* dialog) {
        typeInto(dialog, "-7");
        key(dialog, Qt::Key_Down);
        typeInto(dialog, "0.3"); // (X's infeed: the knurl's depth)
        key(dialog, Qt::Key_Down, 2); // (past X's end diameter)
        typeInto(dialog, "0.05");
        key(dialog, Qt::Key_Down);
        typeInto(dialog, "40"); // (starts)
        key(dialog, Qt::Key_Down);
        // At 10 degrees, Ø 20's lead is 356 mm, which Z could only follow
        // below 5 rpm: Enter does nothing
        typeInto(dialog, "10");
        key(dialog, Qt::Key_Return);
        REQUIRE(dialog->isVisible());
        // (Back to the angle, to type it again)
        key(dialog, Qt::Key_Up);
        key(dialog, Qt::Key_Down);
        typeInto(dialog, "30");
        key(dialog, Qt::Key_R); // (does nothing: knurling never retracts)
        key(dialog, Qt::Key_A); // (constant area)
        snapshot(dialog, "knurling");
        key(dialog, Qt::Key_Return);
    });
    const auto setup = multiPassDialog(view, info);
    REQUIRE(setup.has_value());
    REQUIRE(setup->cutDistance == Approx(-7.0));
    REQUIRE(setup->feedInfeed == Approx(0.3));
    REQUIRE(setup->feedPerPass == Approx(0.05));
    REQUIRE(setup->knurlStarts == 40);
    REQUIRE(setup->knurlHelixAngleDeg == Approx(30.0));
    REQUIRE(!setup->retract);
    REQUIRE(setup->constantArea);
}

TEST_CASE("UI: knurling dialog, the angle and starts suggested")
{
    TestModel tm;
    mgo::ViewQt view;
    view.initialise(tm.model);
    // Ø 20 (X 10), with the spindle knurling at 70 rpm and Z at up to 800
    // mm/min: 80 degrees is the least it can follow
    auto info = multiPassInfo();
    info.knurling = mgo::MultiPassDialogInfo::Knurling {
        .maxCutSpeed = 800.0, .minimumSpindleRpm = 5.0, .spindleRpm = 70.0, .minimumInfeedMm = 0.02
    };

    whenDialogShows([](QWidget* dialog) {
        typeInto(dialog, "-7");
        key(dialog, Qt::Key_Down);
        // 0.2 mm deep: grooves 0.4 mm apart across, π × 20 × cos 80° ÷ 0.4,
        // about 27 starts
        typeInto(dialog, "0.2");
        key(dialog, Qt::Key_Down, 2);
        typeInto(dialog, "0.05");
        key(dialog, Qt::Key_Down);
        REQUIRE(inputWidget(dialog)->property("text").toString() == "27");
        key(dialog, Qt::Key_Down);
        REQUIRE(inputWidget(dialog)->property("text").toString() == "80");
        // 0.3 deep instead: the starts follow (0.6 mm apart: 18)
        key(dialog, Qt::Key_Up, 4);
        typeInto(dialog, "0.3");
        key(dialog, Qt::Key_Down, 3);
        REQUIRE(inputWidget(dialog)->property("text").toString() == "18");
        // Typed in, the starts are kept, even if the depth changes
        typeInto(dialog, "16");
        key(dialog, Qt::Key_Up, 3);
        typeInto(dialog, "0.2");
        key(dialog, Qt::Key_Down, 3);
        REQUIRE(inputWidget(dialog)->property("text").toString() == "16");
        snapshot(dialog, "knurling_suggested");
        key(dialog, Qt::Key_Return);
    });
    auto setup = multiPassDialog(view, info);
    REQUIRE(setup.has_value());
    REQUIRE(setup->knurlStarts == 16);
    REQUIRE(setup->knurlStartsChosen);
    REQUIRE(setup->knurlHelixAngleDeg == Approx(80.0));
    REQUIRE(!setup->knurlHelixAngleChosen);

    // Next time, the starts typed are kept; cleared, they're suggested again
    info.setup = *setup;
    whenDialogShows([](QWidget* dialog) {
        // (From the start, as Z's distance is set this time)
        key(dialog, Qt::Key_Down, 5);
        REQUIRE(inputWidget(dialog)->property("text").toString() == "16");
        key(dialog, Qt::Key_Backspace);
        REQUIRE(inputWidget(dialog)->property("text").toString() == "27");
        key(dialog, Qt::Key_Return);
    });
    setup = multiPassDialog(view, info);
    REQUIRE(setup.has_value());
    REQUIRE(setup->knurlStarts == 27);
    REQUIRE(!setup->knurlStartsChosen);
}

TEST_CASE("UI: knurling through the controller")
{
    TestModel tm;
    mgo::Controller controller(&tm.model);
    QWidget* w = mainWindow();
    REQUIRE(w);

    // Without X set as a diameter, it says so, and stays out of the mode
    whenDialogShows([](QWidget* dialog) { QTest::keyClick(inputWidget(dialog), Qt::Key_Return); });
    press(w, controller, Qt::Key_Tab);
    press(w, controller, Qt::Key_M);
    press(w, controller, Qt::Key_K);
    REQUIRE(waitForDialogToClose());
    REQUIRE(tm.model.getMode() == mgo::Mode::None);

    // On Ø 10, by distances. (With no knurling speed in the config, the angle
    // suggested is 45 degrees, and 0.3 mm deep, the starts space the grooves
    // 0.6 mm apart across: π × 10 × cos 45° ÷ 0.6, about 37)
    tm.model.setPosition(2, 5.0);
    tm.model.diameterIsSet();
    whenDialogShows([](QWidget* dialog) {
        typeInto(dialog, "-7");
        key(dialog, Qt::Key_Down);
        typeInto(dialog, "0.3");
        key(dialog, Qt::Key_Down, 2);
        typeInto(dialog, "0.05");
        QTest::keyClick(inputWidget(dialog), Qt::Key_Return);
    });
    press(w, controller, Qt::Key_Tab);
    press(w, controller, Qt::Key_M);
    press(w, controller, Qt::Key_K);
    REQUIRE(waitForDialogToClose());
    REQUIRE(tm.model.getMode() == mgo::Mode::Knurling);
    REQUIRE(tm.model.getKnurlingSetup().knurlStarts == 37);
    REQUIRE(tm.model.getKnurlingSetup().knurlHelixAngleDeg == Approx(45.0));
    REQUIRE(!tm.model.getKnurlingSetup().knurlStartsChosen);
    REQUIRE(tm.model.getKnurlLeadMm() == Approx(M_PI * 10.0));
    REQUIRE(tm.model.getMultiPassRange().cutEnd == Approx(-7.0));
    controller.tick();
    snapshot(w, "knurling_mode");
}

TEST_CASE("UI: gear cutting through the controller")
{
    // The mill: X, and a rotary table as axis 2
    MapConfigReader config { { { "GearCutting", "true" },
                               { "Axis1Label", "X" },
                               { "Axis2Label", "A" },
                               { "Axis2DisplayUnits", "deg" },
                               { "Axis2ConversionNumerator", "5" },
                               { "Axis2ConversionDivisor", "4000" } } };
    mgo::MockGpio gpio { false };
    mgo::Model model { gpio, config };
    mgo::Controller controller(&model);
    QWidget* w = mainWindow();
    REQUIRE(w);

    // Module 0.5, 30 teeth, X from here by -40, the depth by 0.4 per pass
    whenDialogShows([](QWidget* dialog) {
        // (It starts on the teeth, as none have been chosen)
        typeInto(dialog, "30");
        key(dialog, Qt::Key_Up);
        typeInto(dialog, "0.5"); // (the depth follows the module)
        key(dialog, Qt::Key_Down, 3);
        typeInto(dialog, "-40");
        key(dialog, Qt::Key_Down, 2);
        // (Not without a depth per pass)
        key(dialog, Qt::Key_Return);
        REQUIRE(dialog->isVisible());
        typeInto(dialog, "0.4");
        snapshot(dialog, "gear_cutting");
        key(dialog, Qt::Key_Return);
    });
    press(w, controller, Qt::Key_Tab);
    press(w, controller, Qt::Key_M);
    press(w, controller, Qt::Key_C);
    REQUIRE(waitForDialogToClose());
    REQUIRE(model.getMode() == mgo::Mode::GearCutting);
    REQUIRE(model.gearCuttingSet());
    const mgo::GearSetup& setup = model.getGearSetup();
    REQUIRE(setup.module == Approx(0.5));
    REQUIRE(setup.teeth == 30);
    REQUIRE(setup.depth == Approx(1.0785));
    REQUIRE(setup.depthPerPass == Approx(0.4));
    REQUIRE(model.getMultiPassRange().cutStart == Approx(0.0));
    REQUIRE(model.getMultiPassRange().cutEnd == Approx(-40.0));

    // Enter starts it (X is at the start), pausing for the depth to be fed in
    press(w, controller, Qt::Key_Return);
    REQUIRE(model.multiPass().state() == mgo::MultiPass::State::Paused);
    REQUIRE(model.multiPass().totalPasses() == 3);
    controller.tick();
    snapshot(w, "gear_cutting_mode");
    // Esc asks first, saying the passes would end, and Esc again carries on
    QString text;
    whenDialogShows([&text](QWidget* dialog) {
        text = dialogText(dialog);
        key(dialog, Qt::Key_Escape);
    });
    press(w, controller, Qt::Key_Escape);
    REQUIRE(text.contains("Leave gear cutting mode?"));
    REQUIRE(text.contains("part way through"));
    REQUIRE(model.getMode() == mgo::Mode::GearCutting);
    REQUIRE(model.multiPass().state() == mgo::MultiPass::State::Paused);
    // Space stops it
    press(w, controller, Qt::Key_Space);
    REQUIRE(!model.multiPass().inProgress());
}

TEST_CASE("UI: gear cutting isn't in the menu unless the config has it")
{
    TestModel tm;
    mgo::Controller controller(&tm.model);
    QWidget* w = mainWindow();
    REQUIRE(w);
    press(w, controller, Qt::Key_Tab);
    press(w, controller, Qt::Key_M);
    press(w, controller, Qt::Key_C);
    press(w, controller, Qt::Key_Escape);
    REQUIRE(tm.model.getMode() == mgo::Mode::None);
}

TEST_CASE("UI: radius on the end through the controller")
{
    MapConfigReader config { { { "CurrentFeature", "true" } } };
    mgo::MockGpio gpio { false };
    mgo::Model model { gpio, config };
    mgo::Controller controller(&model);
    QWidget* w = mainWindow();
    REQUIRE(w);
    // (As Controller::run() sets them)
    model.setMotorSpeed(1, 40.0);
    model.setMotorSpeed(2, 20.0);

    // Without the work's diameter and end, it says what's needed
    bool told = false;
    whenDialogShows([&told](QWidget* dialog) {
        told = true;
        key(dialog, Qt::Key_Escape);
    });
    press(w, controller, Qt::Key_Tab);
    press(w, controller, Qt::Key_M);
    press(w, controller, Qt::Key_O);
    REQUIRE(waitForDialogToClose());
    REQUIRE(told);
    REQUIRE(model.getMode() == mgo::Mode::None);

    // A 10 mm bar, its end at Z 0. A 2 mm radius, 0.3 per pass, with a
    // 0.4 mm nose radius
    model.setPosition(2, 5.0);
    model.diameterIsSet();
    model.markWorkEnd();
    whenDialogShows([](QWidget* dialog) {
        typeInto(dialog, "2");
        key(dialog, Qt::Key_Down);
        // (Not without a depth per pass)
        key(dialog, Qt::Key_Return);
        REQUIRE(dialog->isVisible());
        typeInto(dialog, "0.3");
        key(dialog, Qt::Key_Down);
        typeInto(dialog, "0.4");
        snapshot(dialog, "radius");
        key(dialog, Qt::Key_Return);
    });
    press(w, controller, Qt::Key_Tab);
    press(w, controller, Qt::Key_M);
    press(w, controller, Qt::Key_O);
    REQUIRE(waitForDialogToClose());
    REQUIRE(model.getMode() == mgo::Mode::Radius);
    REQUIRE(model.radiusSet());
    const mgo::RadiusSetup& setup = model.getRadiusSetup();
    REQUIRE(setup.radius == Approx(2.0));
    REQUIRE(setup.depthPerPass == Approx(0.3));
    REQUIRE(setup.noseRadius == Approx(0.4));

    // Enter moves to the start: off the end, on the diameter
    press(w, controller, Qt::Key_Return);
    REQUIRE(model.multiPassMovingToStart());
    REQUIRE(
        QTest::qWaitFor(
            [&] {
                controller.tick();
                return !model.multiPassMovingToStart();
            },
            10'000));
    REQUIRE(model.multiPassAtStart());
    // Enter again starts the passes
    press(w, controller, Qt::Key_Return);
    REQUIRE(model.multiPass().inProgress());
    REQUIRE(model.multiPass().totalPasses() == 3);
    controller.tick();
    REQUIRE(model.multiPass().inProgress());
    snapshot(w, "radius_mode");
    // Space stops it
    press(w, controller, Qt::Key_Space);
    REQUIRE(!model.multiPass().inProgress());
}

TEST_CASE("UI: a shoulder with a fillet through the controller")
{
    MapConfigReader config { { { "CurrentFeature", "true" } } };
    mgo::MockGpio gpio { false };
    mgo::Model model { gpio, config };
    mgo::Controller controller(&model);
    QWidget* w = mainWindow();
    REQUIRE(w);
    model.setMotorSpeed(1, 40.0);
    model.setMotorSpeed(2, 20.0);

    // A 10 mm bar, its end at Z 0. Down to 8 mm, 3 mm from the end, with a
    // 0.5 mm fillet, 0.3 per pass, with a 0.1 mm nose radius
    model.setPosition(2, 5.0);
    model.diameterIsSet();
    model.markWorkEnd();
    whenDialogShows([](QWidget* dialog) {
        typeInto(dialog, "8");
        key(dialog, Qt::Key_Down);
        typeInto(dialog, "3");
        key(dialog, Qt::Key_Down);
        typeInto(dialog, "0.5");
        key(dialog, Qt::Key_Down);
        // (Not without a depth per pass)
        key(dialog, Qt::Key_Return);
        REQUIRE(dialog->isVisible());
        typeInto(dialog, "0.3");
        key(dialog, Qt::Key_Down);
        typeInto(dialog, "0.1");
        snapshot(dialog, "shoulder");
        key(dialog, Qt::Key_Return);
    });
    press(w, controller, Qt::Key_Tab);
    press(w, controller, Qt::Key_M);
    press(w, controller, Qt::Key_F);
    REQUIRE(waitForDialogToClose());
    REQUIRE(model.getMode() == mgo::Mode::Radius);
    REQUIRE(model.radiusSet());
    const mgo::RadiusSetup& setup = model.getRadiusSetup();
    REQUIRE(setup.kind == mgo::RadiusKind::Shoulder);
    REQUIRE(setup.finishedDiameter == Approx(8.0));
    REQUIRE(setup.shoulderLength == Approx(3.0));
    REQUIRE(setup.radius == Approx(0.5));
    REQUIRE(setup.depthPerPass == Approx(0.3));
    REQUIRE(setup.noseRadius == Approx(0.1));

    press(w, controller, Qt::Key_Return);
    REQUIRE(
        QTest::qWaitFor(
            [&] {
                controller.tick();
                return !model.multiPassMovingToStart();
            },
            10'000));
    REQUIRE(model.multiPassAtStart());
    press(w, controller, Qt::Key_Return);
    REQUIRE(model.multiPass().inProgress());
    REQUIRE(model.multiPass().totalPasses() == 6);
    controller.tick();
    snapshot(w, "shoulder_mode");
    press(w, controller, Qt::Key_Space);
    REQUIRE(!model.multiPass().inProgress());

    // The radius on the end starts from what was typed for the shoulder
    whenDialogShows([](QWidget* dialog) {
        snapshot(dialog, "radius_after_shoulder");
        key(dialog, Qt::Key_Return);
    });
    press(w, controller, Qt::Key_Tab);
    press(w, controller, Qt::Key_M);
    press(w, controller, Qt::Key_O);
    REQUIRE(waitForDialogToClose());
    REQUIRE(model.radiusSet());
    REQUIRE(model.getRadiusSetup().kind == mgo::RadiusKind::End);
    REQUIRE(model.getRadiusSetup().radius == Approx(0.5));
}

TEST_CASE("UI: radius isn't in the menu without the current feature")
{
    TestModel tm;
    mgo::Controller controller(&tm.model);
    QWidget* w = mainWindow();
    REQUIRE(w);
    press(w, controller, Qt::Key_Tab);
    press(w, controller, Qt::Key_M);
    press(w, controller, Qt::Key_O);
    press(w, controller, Qt::Key_F);
    press(w, controller, Qt::Key_Escape);
    REQUIRE(tm.model.getMode() == mgo::Mode::None);
}

TEST_CASE("UI: multi-pass dialog, memory slots either way round")
{
    TestModel tm;
    mgo::ViewQt view;
    view.initialise(tm.model);
    auto info = multiPassInfo();
    // At Mem 2, as the defaults would have it: Mem 2 to Mem 1
    info.setup = mgo::defaultMultiPassSetup({}, info.positions);
    whenDialogShows([](QWidget* dialog) {
        snapshot(dialog, "multipass_memories");
        key(dialog, Qt::Key_Return);
    });
    auto setup = multiPassDialog(view, info);
    REQUIRE(setup.has_value());
    REQUIRE(setup->startMemory == 1u);
    REQUIRE(setup->cutEndMemory == 0u);
    REQUIRE(setup->feedEndMemory == 0u);

    // Left/Right choose where each comes from. Starting from Mem 1 instead
    // (Here, Mem 1, Mem 2: left from Mem 2) swaps it round: both ends move
    // to Mem 2
    whenDialogShows([](QWidget* dialog) {
        key(dialog, Qt::Key_Left);
        key(dialog, Qt::Key_Return);
    });
    setup = multiPassDialog(view, info);
    REQUIRE(setup.has_value());
    REQUIRE(setup->startMemory == 0u);
    REQUIRE(setup->cutEndMemory == 1u);
    REQUIRE(setup->feedEndMemory == 1u);

    // Then Z's end at Mem 3, e.g. a shoulder (Distance, Mem 2, Mem 3, as
    // Mem 1 is the start: right from Mem 2)
    whenDialogShows([](QWidget* dialog) {
        key(dialog, Qt::Key_Left);
        key(dialog, Qt::Key_Down);
        key(dialog, Qt::Key_Right);
        key(dialog, Qt::Key_Return);
    });
    setup = multiPassDialog(view, info);
    REQUIRE(setup.has_value());
    REQUIRE(setup->startMemory == 0u);
    REQUIRE(setup->cutEndMemory == 2u);
    REQUIRE(setup->feedEndMemory == 1u);

    // Typing a number on an end from a memory slot makes it a distance
    whenDialogShows([](QWidget* dialog) {
        key(dialog, Qt::Key_Down, 2);
        typeInto(dialog, "0.3");
        key(dialog, Qt::Key_Return);
    });
    setup = multiPassDialog(view, info);
    REQUIRE(setup.has_value());
    REQUIRE(setup->cutEndMemory == 0u);
    REQUIRE(!setup->feedEndMemory);
    REQUIRE(setup->feedInfeed == Approx(0.3));
}

TEST_CASE("UI: taper dialog through the controller")
{
    TestModel tm;
    mgo::Controller controller(&tm.model);
    QWidget* w = mainWindow();
    REQUIRE(w);

    // The menu's Modes, Taper opens it; choose MT2, tool moving in, and OK. (Found by name,
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
    press(w, controller, Qt::Key_Tab);
    press(w, controller, Qt::Key_M);
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

    // A sum: = also switches to a custom angle, and I (in pi) is typed, not
    // the direction. Enter puts the value in, and Enter again accepts it
    whenDialogShows([](QWidget* dialog) {
        typeInto(dialog, "=pi*2");
        QTest::keyClick(inputWidget(dialog), Qt::Key_Return);
        REQUIRE(dialog->isVisible());
        REQUIRE(lineEdit(dialog)->text() == "6.283185");
        QTest::keyClick(inputWidget(dialog), Qt::Key_Return);
    });
    REQUIRE(getTaperAngle(view, 0.0) == Approx(-6.283185));

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
    tm.model.setMotorSpeed(2, 37.0);
    press(w, controller, Qt::Key_H);
    REQUIRE(tm.model.getMode() == mgo::Mode::Taper);
    REQUIRE(tm.model.getTaperAngle() == Approx(-45.0));
    tm.model.setMotorSpeed(2, 100.0);
    whenDialogShows([](QWidget* dialog) { key(dialog, Qt::Key_Return); });
    press(w, controller, Qt::Key_Escape);
    REQUIRE(tm.model.getMode() == mgo::Mode::None);
    REQUIRE(tm.model.motorSpeed(2) == Approx(37.0));
}

TEST_CASE("UI: Esc asks before leaving a mode")
{
    // (Esc is easily pressed by instinct, e.g. to clear a warning)
    TestModel tm;
    mgo::Controller controller(&tm.model);
    QWidget* w = mainWindow();
    REQUIRE(w);
    press(w, controller, Qt::Key_H);
    REQUIRE(tm.model.getMode() == mgo::Mode::Taper);
    QString text;
    bool running = false;
    const auto answer = [&](Qt::Key k) {
        text.clear();
        whenDialogShows([&, k](QWidget* dialog) {
            text = dialogText(dialog);
            running = tm.model.isMotorRunning(2);
            key(dialog, k);
        });
    };

    // Esc (or N) in the dialog stays in the mode
    answer(Qt::Key_Escape);
    press(w, controller, Qt::Key_Escape);
    REQUIRE(text.contains("Leave taper mode?"));
    REQUIRE(tm.model.getMode() == mgo::Mode::Taper);
    answer(Qt::Key_N);
    press(w, controller, Qt::Key_Escape);
    REQUIRE(tm.model.getMode() == mgo::Mode::Taper);

    // The dialog doesn't stop anything (X: a Z move in the taper stopped by
    // itself here, before Esc, for a reason not yet looked into)
    tm.model.goToPosition(2, 50.0);
    REQUIRE(waitUntil([&] { return tm.model.isMotorRunning(2); }));
    answer(Qt::Key_Escape);
    press(w, controller, Qt::Key_Escape);
    REQUIRE(running);
    REQUIRE(tm.model.isMotorRunning(2));
    press(w, controller, Qt::Key_Space);

    // Enter (or Y) leaves it
    answer(Qt::Key_Y);
    press(w, controller, Qt::Key_Escape);
    REQUIRE(tm.model.getMode() == mgo::Mode::None);

    // With no mode, Esc doesn't ask (press() would fail if a dialog stayed open)
    press(w, controller, Qt::Key_Escape);
    REQUIRE(tm.model.getMode() == mgo::Mode::None);
}

TEST_CASE("UI: thread dialog")
{
    TestModel tm;
    mgo::ViewQt view;
    view.initialise(tm.model);
    std::istringstream in(
        "[Metric | ISO]\nM6, 6, 1\nM8, 8, 1.25\n"
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

    // The menu's Modes, Threading: the first size of the first type, female
    whenDialogShows([](QWidget* dialog) {
        QTest::keyClick(inputWidget(dialog), Qt::Key_M);
        QTest::keyClick(inputWidget(dialog), Qt::Key_Return);
    });
    press(w, controller, Qt::Key_Tab);
    press(w, controller, Qt::Key_M);
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
    tm.model.setMotorSpeed(1, 1'000.0);
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
    REQUIRE(waitUntil([&] { return std::abs(tm.model.motorPosition(1) - 25.4) < 0.001; }));

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
    REQUIRE(tm.model.motorPosition(1) == Approx(0.5).margin(0.001));
}

TEST_CASE("UI: keys wait while a nudge is moving, except Space")
{
    SlowZTestModel tm;
    mgo::Controller controller(&tm.model);
    QWidget* w = mainWindow();
    REQUIRE(w);
    const double speed = tm.model.motorSpeed(1);

    // A (nudge left 0.05mm), then Shift-D (right 0.01mm) while the first is
    // still moving: the second waits, so each moves its full amount
    QTest::keyClick(w, Qt::Key_A);
    controller.tick();
    REQUIRE(tm.model.isMotionSequenceRunning());
    QTest::keyClick(w, Qt::Key_D, Qt::ShiftModifier);
    controller.tick();
    REQUIRE(tm.model.isMotionSequenceRunning()); // (still the first nudge)
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::abs(std::abs(tm.model.motorPosition(1)) - 0.04) > 0.0001
           || tm.model.isMotionSequenceRunning()) {
        REQUIRE(std::chrono::steady_clock::now() < deadline);
        controller.tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(tm.model.motorSpeed(1) == Approx(speed));

    // Space, though, stops a nudge part way (and the speed is restored)
    const double position = tm.model.motorPosition(1);
    QTest::keyClick(w, Qt::Key_A);
    controller.tick();
    REQUIRE(tm.model.isMotionSequenceRunning());
    QTest::qWait(50);
    QTest::keyClick(w, Qt::Key_Space);
    controller.tick();
    REQUIRE(!tm.model.isMotionSequenceRunning());
    REQUIRE(!tm.model.isMotorRunning(1));
    const double moved = std::abs(tm.model.motorPosition(1) - position);
    REQUIRE(moved > 0.0);
    REQUIRE(moved < 0.05);
    REQUIRE(tm.model.motorSpeed(1) == Approx(speed));
}

TEST_CASE("UI: multi-pass through the controller")
{
    TestModel tm;
    mgo::Controller controller(&tm.model);
    QWidget* w = mainWindow();
    REQUIRE(w);
    // (run() would set these from the presets)
    tm.model.setMotorSpeed(1, 500.0);
    tm.model.setMotorSpeed(2, 20.0);
    // Mem 1 where the tool is (0, 0); Mem 2 at Z 0.3, X 0.02
    tm.model.storePosition(1, 0.0);
    tm.model.storePosition(2, 0.0);
    tm.model.selectNextMemorySlot();
    tm.model.storePosition(1, 0.3);
    tm.model.storePosition(2, 0.02);
    tm.model.selectPreviousMemorySlot();

    // The menu's Modes, Multi-pass: as the tool's at Mem 1, the dialog starts there, ending at
    // Mem 2. Down to the step-over (past Z's and X's ends), and type it
    whenDialogShows([](QWidget* dialog) {
        for (int n = 0; n < 3; ++n) {
            QTest::keyClick(inputWidget(dialog), Qt::Key_Down);
        }
        typeInto(dialog, "0.01");
        QTest::keyClick(inputWidget(dialog), Qt::Key_Return);
    });
    press(w, controller, Qt::Key_Tab);
    press(w, controller, Qt::Key_M);
    press(w, controller, Qt::Key_M);
    REQUIRE(waitForDialogToClose());
    REQUIRE(tm.model.getMode() == mgo::Mode::MultiPass);
    REQUIRE(tm.model.getMultiPassSetup().feedPerPass == Approx(0.01));
    REQUIRE(tm.model.getMultiPassSetup().startMemory == 0u);
    REQUIRE(tm.model.getMultiPassRange().cutEnd == Approx(0.3));
    REQUIRE(!tm.model.multiPass().inProgress());
    controller.tick();
    snapshot(w, "multipass_mode");

    // Enter starts it (as it's at the start already)
    press(w, controller, Qt::Key_Return);
    REQUIRE(tm.model.multiPass().inProgress());
    // (X in by 0.02, 0.01 at a time)
    REQUIRE(tm.model.multiPass().totalPasses() == 2);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (tm.model.multiPass().inProgress()) {
        REQUIRE(std::chrono::steady_clock::now() < deadline);
        controller.tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    INFO("Stopped: " << tm.model.multiPass().message() << " / " << tm.model.getWarning());
    REQUIRE(tm.model.multiPass().state() == mgo::MultiPass::State::Finished);
    REQUIRE(tm.model.motorPosition(1) == Approx(0.0).margin(0.0001));
    REQUIRE(tm.model.motorPosition(2) == Approx(0.02).margin(0.0001));
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

namespace {

// With limit switches wired (to a pin the mock GPIO can set; active high, so
// it starts low: not pressed)
constexpr int LIMIT_PIN = 22;
struct LimitTestModel {
    MapConfigReader config { { { "LimitSwitchPin", "22" } } };
    mgo::MockGpio gpio { false };
    bool released { (gpio.setInputPinLevel(LIMIT_PIN, mgo::PinState::low), true) };
    mgo::Model model { gpio, config };
};

} // end anonymous namespace

TEST_CASE("UI: a limit switch opens a dialog to clear its stop")
{
    for (const bool clear : { true, false }) {
        LimitTestModel tm;
        mgo::Controller controller(&tm.model);
        QWidget* w = mainWindow();
        REQUIRE(w);

        int dialogsShown = 0;
        bool trippedWhileOpen = false;
        whenDialogShows([&](QWidget* dialog) {
            ++dialogsShown;
            trippedWhileOpen = tm.model.limitSwitchTripped();
            QTest::keyClick(inputWidget(dialog), clear ? Qt::Key_Return : Qt::Key_Escape);
        });
        QTimer::singleShot(150, [&] { tm.gpio.setInputPinLevel(LIMIT_PIN, mgo::PinState::high); });
        QTimer::singleShot(1'500, [w] { w->close(); });
        controller.run();
        INFO("Cleared: " << clear);
        // Once only, even if left tripped
        REQUIRE(dialogsShown == 1);
        REQUIRE(trippedWhileOpen);
        REQUIRE(tm.model.limitSwitchTripped() == !clear);
        // (Still pressed, either way)
        REQUIRE(!tm.model.getFaultMessage().empty());
    }
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

namespace {

// With the current feature enabled
struct FeatureTestModel {
    MapConfigReader config { { { "CurrentFeature", "true" } } };
    mgo::MockGpio gpio { false };
    mgo::Model model { gpio, config };
};

} // end anonymous namespace

TEST_CASE("UI: multi-pass dialog, from the current feature")
{
    TestModel tm;
    mgo::ViewQt view;
    view.initialise(tm.model);
    auto info = multiPassInfo();
    // Not at a memory slot, with the feature set (the work's end at Z 0, 15
    // diameter): the defaults start from it
    info.positions.cutHere = 20.0;
    info.positions.featureWorkEnd = 0.0;
    info.positions.featureFeed = 7.5;
    info.positions.featureClearance = 0.5;
    info.setup = mgo::defaultMultiPassSetup({}, info.positions);
    REQUIRE(info.setup.startFeature);

    // Z 20 into the work from its end, to 14 diameter, 0.1 per pass
    whenDialogShows([](QWidget* dialog) {
        typeInto(dialog, "-20");
        key(dialog, Qt::Key_Down, 2); // (to X's end diameter)
        typeInto(dialog, "14");
        key(dialog, Qt::Key_Down);
        typeInto(dialog, "0.1");
        snapshot(dialog, "multipass_feature");
        key(dialog, Qt::Key_Return);
    });
    auto setup = multiPassDialog(view, info);
    REQUIRE(setup.has_value());
    REQUIRE(setup->startFeature);
    REQUIRE(!setup->startMemory);
    REQUIRE(setup->cutDistance == Approx(-20.0));
    REQUIRE(setup->feedInfeed == Approx(0.5));
    const auto r = mgo::resolveMultiPass(*setup, info.positions);
    REQUIRE(r.error.empty());
    REQUIRE(r.range.cutStart == Approx(0.5));
    REQUIRE(r.range.cutEnd == Approx(-20.0));
    REQUIRE(r.range.feedEnd == Approx(7.0));

    // The start's choices: Here, Feature, then the memory slots
    info.setup = *setup;
    whenDialogShows([](QWidget* dialog) {
        key(dialog, Qt::Key_Up, 3); // (to the start)
        key(dialog, Qt::Key_Left);
        key(dialog, Qt::Key_Return);
    });
    setup = multiPassDialog(view, info);
    REQUIRE(setup.has_value());
    REQUIRE(!setup->startFeature);
    REQUIRE(!setup->startMemory);
    whenDialogShows([](QWidget* dialog) {
        key(dialog, Qt::Key_Up, 3);
        key(dialog, Qt::Key_Right);
        key(dialog, Qt::Key_Return);
    });
    setup = multiPassDialog(view, info);
    REQUIRE(setup.has_value());
    REQUIRE(!setup->startFeature);
    REQUIRE(setup->startMemory == 0u);
}

TEST_CASE("UI: the current feature through the controller")
{
    FeatureTestModel tm;
    mgo::Controller controller(&tm.model);
    QWidget* w = mainWindow();
    REQUIRE(w);

    // Set X as a 15 diameter: that's the feature's
    whenDialogShows([](QWidget* dialog) {
        typeInto(dialog, "15");
        QTest::keyClick(inputWidget(dialog), Qt::Key_D, Qt::AltModifier);
        QTest::keyClick(inputWidget(dialog), Qt::Key_Return);
    });
    press(w, controller, Qt::Key_X);
    press(w, controller, Qt::Key_S);
    REQUIRE(tm.model.getFeatureDiameterMm() == Approx(15.0));

    // Z E marks the work's end
    press(w, controller, Qt::Key_Z);
    press(w, controller, Qt::Key_E);
    REQUIRE(tm.model.getWorkEndZ() == Approx(0.0));

    // The menu's Feature, Clear. (With LC_SNAPSHOT_DIR set, an image of the
    // menu's Feature category, to look at)
    press(w, controller, Qt::Key_Tab);
    press(w, controller, Qt::Key_F);
    if (const char* dir = std::getenv("LC_SNAPSHOT_DIR")) {
        controller.tick();
        QTest::qWait(150);
        w->grab().save(QString::fromStdString(std::string(dir) + "/menu_feature.png"));
    }
    press(w, controller, Qt::Key_C);
    REQUIRE(!tm.model.getFeatureDiameterMm());
    REQUIRE(!tm.model.getWorkEndZ());
}

TEST_CASE("UI: the current feature is shown")
{
    FeatureTestModel tm;
    tm.model.initialise();
    // (Any main windows from earlier tests are hidden, so this one's found)
    for (QWidget* other : QApplication::topLevelWidgets()) {
        if (other->windowTitle() == "Lathe Control") {
            other->hide();
        }
    }
    mgo::ViewQt view;
    view.initialise(tm.model);
    tm.model.setPosition(2, 7.5);
    tm.model.diameterIsSet();
    tm.model.markWorkEnd();
    // In multi-pass mode, whose lines run alongside it
    tm.model.changeMode(mgo::Mode::MultiPass);
    REQUIRE(tm.model
                .setMultiPassSetup(
                    { .startFeature = true,
                      .cutDistance = -20.0,
                      .feedInfeed = 0.5,
                      .feedPerPass = 0.1,
                      .retract = true })
                .empty());
    tm.model.goToPosition(2, 9.0);
    tm.model.waitForMotor(2);
    view.updateDisplay(tm.model);
    QWidget* w = mainWindow();
    REQUIRE(w);
    // (With LC_SNAPSHOT_DIR set, an image of the main window, to look at)
    if (const char* dir = std::getenv("LC_SNAPSHOT_DIR")) {
        QTest::qWait(150);
        w->grab().save(QString::fromStdString(std::string(dir) + "/main_feature.png"));
    }
}

TEST_CASE("UI: an OK / Cancel dialog")
{
    TestModel tm;
    mgo::ViewQt view;
    view.initialise(tm.model);
    const auto ask = [&view](Qt::Key k) {
        whenDialogShows([k](QWidget* dialog) { QTest::keyClick(inputWidget(dialog), k); });
        return !getInput(view, mgo::Input::Type::OkCancel, "Sure?", { "Really?" }).cancelled;
    };
    REQUIRE(ask(Qt::Key_Return));
    REQUIRE(ask(Qt::Key_Y));
    REQUIRE(!ask(Qt::Key_Escape));
    REQUIRE(!ask(Qt::Key_N));
}

TEST_CASE("UI: zeroing or setting a position asks before clearing the current feature")
{
    FeatureTestModel tm;
    mgo::Controller controller(&tm.model);
    QWidget* w = mainWindow();
    REQUIRE(w);
    tm.model.setPosition(2, 7.5);
    tm.model.diameterIsSet();
    tm.model.setPosition(1, 3.0);
    tm.model.markWorkEnd();
    bool asked = false;
    const auto answer = [&asked](Qt::Key k) {
        asked = false;
        whenDialogShows([&asked, k](QWidget* dialog) {
            asked = true;
            QTest::keyClick(inputWidget(dialog), k);
        });
    };

    // X Z: cancelled, nothing changes
    answer(Qt::Key_Escape);
    press(w, controller, Qt::Key_X);
    press(w, controller, Qt::Key_Z);
    REQUIRE(asked);
    REQUIRE(tm.model.motorPosition(2) == Approx(7.5));
    REQUIRE(tm.model.getFeatureDiameterMm() == Approx(15.0));

    // Z Z: going ahead zeroes Z, clearing the work's end (but not the diameter)
    answer(Qt::Key_Return);
    press(w, controller, Qt::Key_Z);
    press(w, controller, Qt::Key_Z);
    REQUIRE(asked);
    REQUIRE(tm.model.motorPosition(1) == Approx(0.0));
    REQUIRE(!tm.model.getWorkEndZ());
    REQUIRE(tm.model.getFeatureDiameterMm() == Approx(15.0));

    // ...after which Z Z doesn't ask (there's nothing of the feature to
    // lose). (No answer is set up for it: press() would fail if it asked, as
    // the dialog would stay open)
    tm.model.setPosition(1, 2.0);
    press(w, controller, Qt::Key_Z);
    press(w, controller, Qt::Key_Z);
    REQUIRE(tm.model.motorPosition(1) == Approx(0.0));

    // X S, set as a diameter: the diameter's replaced, without asking (as
    // above, press() would fail if it asked)
    whenDialogShows([](QWidget* dialog) {
        typeInto(dialog, "14");
        QTest::keyClick(inputWidget(dialog), Qt::Key_D, Qt::AltModifier);
        QTest::keyClick(inputWidget(dialog), Qt::Key_Return);
    });
    press(w, controller, Qt::Key_X);
    press(w, controller, Qt::Key_S);
    REQUIRE(tm.model.getFeatureDiameterMm() == Approx(14.0));

    // X S, not as a diameter: asks; N cancels (nothing's set)
    whenDialogShows([&answer](QWidget* dialog) {
        typeInto(dialog, "3");
        answer(Qt::Key_N);
        QTest::keyClick(inputWidget(dialog), Qt::Key_Return);
    });
    press(w, controller, Qt::Key_X);
    press(w, controller, Qt::Key_S);
    REQUIRE(asked);
    REQUIRE(tm.model.motorPosition(2) == Approx(7.0));
    REQUIRE(tm.model.getFeatureDiameterMm() == Approx(14.0));
    // Y sets it, clearing the diameter
    whenDialogShows([&answer](QWidget* dialog) {
        typeInto(dialog, "3");
        answer(Qt::Key_Y);
        QTest::keyClick(inputWidget(dialog), Qt::Key_Return);
    });
    press(w, controller, Qt::Key_X);
    press(w, controller, Qt::Key_S);
    REQUIRE(asked);
    REQUIRE(tm.model.motorPosition(2) == Approx(3.0));
    REQUIRE(!tm.model.getFeatureDiameterMm());
}

TEST_CASE("UI: no scale unless the config has one")
{
    TestModel tm;
    mgo::Controller controller(&tm.model);
    REQUIRE(!tm.model.hasScale(1));
    REQUIRE(!tm.model.scalePositionMm(1));
}

TEST_CASE("UI: a serial scale (simulated) is lined up with the motor, and follows it")
{
    MapConfigReader config { { { "Axis1Scale", "serial" },
                               { "Axis1ScaleSerialLetter", "X" },
                               { "Axis1ScaleCountsPerMm", "100" },
                               { "Axis1ScaleSettleMs", "200" } } };
    mgo::MockGpio gpio { false };
    mgo::Model model { gpio, config };
    mgo::Controller controller(&model);
    REQUIRE(model.hasScale(1));
    // Runs the controller until the condition holds (or 5s pass)
    const auto tickUntil = [&controller](const std::function<bool()>& condition) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!condition() && std::chrono::steady_clock::now() < deadline) {
            controller.tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return condition();
    };
    // (One count is 0.01mm)
    const auto agrees = [&model]() {
        const auto difference = model.scaleDifferenceMm(1);
        return difference && std::abs(*difference) < 0.011;
    };
    REQUIRE(model.scaleHasReading(1));
    REQUIRE(tickUntil([&model] { return model.scalePositionMm(1).has_value(); }));
    REQUIRE(agrees());

    model.setMotorSpeed(1, 1'000.0);
    model.goToPosition(1, 1.0);
    REQUIRE(tickUntil([&] { return std::abs(model.motorPosition(1) - 1.0) < 0.0001 && agrees(); }));
    REQUIRE(tickUntil([&] { return std::abs(model.motorPosition(1) - 1.0) < 0.0001 && agrees(); }));
    REQUIRE(*model.scalePositionMm(1) == Approx(1.0).margin(0.011));

    // Zeroing lines it up again, at the new zero
    model.zero(1);
    REQUIRE(tickUntil([&model] { return model.scalePositionMm(1).has_value(); }));
    REQUIRE(*model.scalePositionMm(1) == Approx(0.0).margin(0.011));
}

TEST_CASE("UI: a manual axis (Y) is zeroed, set and halved with its leader key")
{
    MapConfigReader config { { { "Axis3Kind", "manual" },
                               { "Axis3Label", "Y" },
                               { "Axis3Leader", "121" },
                               { "Axis3Scale", "serial" },
                               { "Axis3ScaleSerialLetter", "Y" },
                               { "Axis3ScaleCountsPerMm", "100" },
                               // (As on the mill, where Y takes its line)
                               { "DisableRpm", "true" } } };
    mgo::MockGpio gpio { false };
    mgo::Model model { gpio, config };
    mgo::Controller controller(&model);
    QWidget* w = mainWindow();
    REQUIRE(w);
    const mgo::ManualAxis* y = model.manualAxis(3);
    REQUIRE(y);
    REQUIRE(*y->positionMm() == Approx(0.0).margin(0.005));
    // Wound along, then zeroed there
    model.fakeMoveManualAxis(3, 3.0);
    REQUIRE(QTest::qWaitFor([y] { return std::abs(*y->positionMm() - 3.0) < 0.005; }, 5'000));

    press(w, controller, Qt::Key_Y);
    REQUIRE(model.getKeyMode() == mgo::KeyMode::Axis3);
    press(w, controller, Qt::Key_Z);
    REQUIRE(model.getKeyMode() == mgo::KeyMode::None);
    REQUIRE(*y->positionMm() == Approx(0.0).margin(0.005));

    whenDialogShows([](QWidget* dialog) {
        QTest::keyClicks(inputWidget(dialog), "12");
        QTest::keyClick(inputWidget(dialog), Qt::Key_Return);
    });
    press(w, controller, Qt::Key_Y);
    press(w, controller, Qt::Key_S); // opens the dialog
    REQUIRE(waitForDialogToClose());
    REQUIRE(*y->positionMm() == Approx(12.0).margin(0.005));

    press(w, controller, Qt::Key_Y);
    press(w, controller, Qt::Key_H);
    REQUIRE(*y->positionMm() == Approx(6.0).margin(0.005));

    // (To look at the display, with LC_SNAPSHOT_DIR set)
    controller.tick();
    snapshot(w, "manual-axis");
}

TEST_CASE("UI: a manual axis's go-to position (Y G) is wound there by hand")
{
    MapConfigReader config { { { "Axis3Kind", "manual" },
                               { "Axis3Label", "Y" },
                               { "Axis3Leader", "121" },
                               { "Axis3Scale", "serial" },
                               { "Axis3ScaleSerialLetter", "Y" },
                               { "Axis3ScaleCountsPerMm", "100" },
                               { "Axis3ClockwiseIncreases", "false" },
                               { "DisableRpm", "true" } } };
    mgo::MockGpio gpio { false };
    mgo::Model model { gpio, config };
    mgo::Controller controller(&model);
    QWidget* w = mainWindow();
    REQUIRE(w);
    REQUIRE(model.manualAxis(3));

    // The dialog says it's wound by hand, and starts the go-to: nothing
    // moves, and its line shows the distance to go and which way to wind
    std::string text;
    whenDialogShows([&text](QWidget* dialog) {
        for (const QLabel* label : dialog->findChildren<QLabel*>()) {
            text += label->text().toStdString() + "\n";
        }
        QTest::keyClicks(inputWidget(dialog), "12");
        QTest::keyClick(inputWidget(dialog), Qt::Key_Return);
    });
    press(w, controller, Qt::Key_Y);
    press(w, controller, Qt::Key_G);
    REQUIRE(waitForDialogToClose());
    REQUIRE(text.find("Y is moved by hand") != std::string::npos);
    const auto goTo = model.manualAxisGoTo(3);
    REQUIRE(goTo);
    REQUIRE(goTo->targetMm == Approx(12.0));
    REQUIRE(goTo->toGoMm == Approx(12.0).margin(0.005));
    // (Clockwise winds this Y down, so up is anticlockwise)
    REQUIRE(goTo->clockwise == false);
    controller.tick();
    snapshot(w, "manual-axis-go-to-position");

    // Space cancels it
    press(w, controller, Qt::Key_Space);
    REQUIRE(!model.manualAxisGoTo(3));

    // Down from where it is is clockwise
    whenDialogShows([](QWidget* dialog) {
        QTest::keyClicks(inputWidget(dialog), "-3");
        QTest::keyClick(inputWidget(dialog), Qt::Key_Return);
    });
    press(w, controller, Qt::Key_Y);
    press(w, controller, Qt::Key_G);
    REQUIRE(waitForDialogToClose());
    REQUIRE(model.manualAxisGoTo(3)->clockwise == true);
}

TEST_CASE("UI: \\ z zeroes every axis, including the manual ones")
{
    MapConfigReader config { { { "Axis3Kind", "manual" },
                               { "Axis3Label", "Y" },
                               { "Axis3Leader", "121" },
                               { "Axis3Scale", "serial" },
                               { "Axis3ScaleSerialLetter", "Y" },
                               { "Axis3ScaleCountsPerMm", "100" },
                               { "DisableRpm", "true" } } };
    mgo::MockGpio gpio { false };
    mgo::Model model { gpio, config };
    mgo::Controller controller(&model);
    model.setMotorSpeed(1, 1'000.0);
    QWidget* w = mainWindow();
    REQUIRE(w);
    const mgo::ManualAxis* y = model.manualAxis(3);
    REQUIRE(y);
    // Z (axis 1) moved away from zero, and Y wound along
    model.goToPosition(1, 2.0);
    REQUIRE(waitUntil([&] { return std::abs(model.motorPosition(1) - 2.0) < 0.001; }));
    model.fakeMoveManualAxis(3, 3.0);
    REQUIRE(QTest::qWaitFor([y] { return std::abs(*y->positionMm() - 3.0) < 0.005; }, 5'000));

    press(w, controller, Qt::Key_Backslash);
    REQUIRE(model.getKeyMode() == mgo::KeyMode::AxisAll);
    press(w, controller, Qt::Key_Z);
    REQUIRE(model.motorPosition(1) == 0.0);
    REQUIRE(*y->positionMm() == Approx(0.0).margin(0.005));
}

namespace {

// As on the mill: Y and Z moved by hand, with Z left out of going to a slot
// (With extra, or different, values from extra)
MapConfigReader millLikeConfig(const std::map<std::string, std::string>& extra = {})
{
    std::map<std::string, std::string> values { { "Axis3Kind", "manual" },
                                                { "Axis3Label", "Y" },
                                                { "Axis3Leader", "121" },
                                                { "Axis3Scale", "serial" },
                                                { "Axis3ScaleSerialLetter", "Y" },
                                                { "Axis3ScaleCountsPerMm", "100" },
                                                { "Axis4Kind", "manual" },
                                                { "Axis4Label", "Z" },
                                                { "Axis4Leader", "122" },
                                                { "Axis4Scale", "serial" },
                                                { "Axis4ScaleSerialLetter", "Z" },
                                                { "Axis4ScaleCountsPerMm", "100" },
                                                { "Axis4InSlotGoTo", "false" },
                                                // (X and A, as the mill's axes 1 and 2)
                                                { "Axis1Label", "X" },
                                                { "Axis1Leader", "120" },
                                                { "Axis2Label", "A" },
                                                { "Axis2Leader", "97" },
                                                { "DisableRpm", "true" } };
    for (const auto& [key, value] : extra) {
        values[key] = value;
    }
    return MapConfigReader { values };
}

TEST_CASE("UI: a manual axis's go-to doesn't say which way to wind, unless it's set")
{
    MapConfigReader config = millLikeConfig();
    mgo::MockGpio gpio { false };
    mgo::Model model { gpio, config };
    mgo::Controller controller(&model);
    QWidget* w = mainWindow();
    REQUIRE(w);
    whenDialogShows([](QWidget* dialog) {
        QTest::keyClicks(inputWidget(dialog), "5");
        QTest::keyClick(inputWidget(dialog), Qt::Key_Return);
    });
    press(w, controller, Qt::Key_Z);
    press(w, controller, Qt::Key_G);
    REQUIRE(waitForDialogToClose());
    REQUIRE(model.manualAxisGoTo(4));
    REQUIRE(!model.manualAxisGoTo(4)->clockwise);
}

} // namespace

TEST_CASE("UI: speed keys: the presets, faster and slower, for each axis")
{
    // The lathe's keys, with the presets' defaults: 1-5 for Z, 6-0 or x then
    // 1-5 for X, = and - (or after a leader key) for faster and slower
    MapConfigReader config { { { "Axis2SpeedPreset5", "200" } } };
    mgo::MockGpio gpio { false };
    mgo::Model model { gpio, config };
    mgo::Controller controller(&model);
    QWidget* w = mainWindow();
    REQUIRE(w);

    press(w, controller, Qt::Key_3);
    REQUIRE(model.motorSpeed(1) == Approx(100.0));
    press(w, controller, Qt::Key_Equal);
    REQUIRE(model.motorSpeed(1) == Approx(120.0));
    press(w, controller, Qt::Key_Z);
    press(w, controller, Qt::Key_Minus);
    press(w, controller, Qt::Key_Minus);
    REQUIRE(model.motorSpeed(1) == Approx(80.0)); // (- alone is Z's too)
    press(w, controller, Qt::Key_Z);
    press(w, controller, Qt::Key_Equal);
    REQUIRE(model.motorSpeed(1) == Approx(100.0));

    press(w, controller, Qt::Key_9);
    REQUIRE(model.motorSpeed(2) == Approx(80.0));
    press(w, controller, Qt::Key_0);
    REQUIRE(model.motorSpeed(2) == Approx(200.0)); // (0 is X's fifth)
    press(w, controller, Qt::Key_X);
    press(w, controller, Qt::Key_2);
    REQUIRE(model.motorSpeed(2) == Approx(20.0));
    press(w, controller, Qt::Key_X);
    press(w, controller, Qt::Key_Equal);
    REQUIRE(model.motorSpeed(2) == Approx(30.0));
    press(w, controller, Qt::Key_X);
    press(w, controller, Qt::Key_Minus);
    REQUIRE(model.motorSpeed(2) == Approx(20.0));
    REQUIRE(model.motorSpeed(1) == Approx(100.0)); // (Z untouched)
}

TEST_CASE("UI: halving after a leader key, with the quick chamfer turned off")
{
    // As on the mill: h on its own is the quick chamfer, which DisabledModes
    // turns off. That once threw away the h after a leader key too, so
    // halving did nothing on any axis
    MapConfigReader config
        = millLikeConfig({ { "DisabledModes", "threading, taper, chamfer, knurling" } });
    mgo::MockGpio gpio { false };
    mgo::Model model { gpio, config };
    mgo::Controller controller(&model);
    QWidget* w = mainWindow();
    REQUIRE(w);
    const mgo::ManualAxis* y = model.manualAxis(3);
    REQUIRE(y);
    REQUIRE(waitUntil([&] {
        controller.tick();
        return y->hasReading();
    }));

    // Axis 1 (X)
    model.setPosition(1, 10.0);
    press(w, controller, Qt::Key_X);
    press(w, controller, Qt::Key_H);
    REQUIRE(model.motorPosition(1) == Approx(5.0).margin(0.01));
    // Y (manual)
    model.setPosition(3, 8.0);
    press(w, controller, Qt::Key_Y);
    press(w, controller, Qt::Key_H);
    REQUIRE(*y->positionMm() == Approx(4.0).margin(0.01));
    // h on its own is still refused
    press(w, controller, Qt::Key_H);
    REQUIRE(model.getMode() == mgo::Mode::None);
    // Esc after a leader cancels it, and nothing else
    press(w, controller, Qt::Key_X);
    REQUIRE(model.getKeyMode() == mgo::KeyMode::Axis1);
    press(w, controller, Qt::Key_Escape);
    REQUIRE(model.getKeyMode() == mgo::KeyMode::None);
    REQUIRE(model.motorPosition(1) == Approx(5.0).margin(0.01));
}

TEST_CASE("UI: memory slots for the manual axes, and going to them by hand", "[timing]")
{
    MapConfigReader config = millLikeConfig();
    mgo::MockGpio gpio { false };
    mgo::Model model { gpio, config };
    mgo::Controller controller(&model);
    QWidget* w = mainWindow();
    REQUIRE(w);
    const auto tickUntil = [&](const std::function<bool()>& condition) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!condition() && std::chrono::steady_clock::now() < deadline) {
            controller.tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return condition();
    };
    const mgo::ManualAxis* y = model.manualAxis(3);
    const mgo::ManualAxis* z = model.manualAxis(4);
    REQUIRE(y);
    REQUIRE(z);
    const auto windTo = [&](int axisNumber, double mm) {
        const mgo::ManualAxis* axis = model.manualAxis(axisNumber);
        model.fakeMoveManualAxis(axisNumber, mm);
        REQUIRE(tickUntil([&] { return std::abs(*axis->positionMm() - mm) < 0.005; }));
    };

    // M stores every axis, Y and Z included
    windTo(3, 3.0);
    windTo(4, -1.0);
    press(w, controller, Qt::Key_M);
    REQUIRE(*model.getMemory(3, 0) == Approx(3.0).margin(0.005));
    REQUIRE(*model.getMemory(4, 0) == Approx(-1.0).margin(0.005));
    REQUIRE(!model.getMemory(3, 1));

    // \ then Enter goes to the slot: Y is to be wound there (not Z, which
    // isn't in the go-to), showing the distance to go
    windTo(3, 10.0);
    windTo(4, -2.0);
    press(w, controller, Qt::Key_Backslash);
    press(w, controller, Qt::Key_Return);
    REQUIRE(model.manualAxisGoTo(3));
    REQUIRE(!model.manualAxisGoTo(4));
    REQUIRE(model.manualAxisGoTo(3)->targetMm == Approx(3.0).margin(0.005));
    REQUIRE(model.manualAxisGoTo(3)->toGoMm == Approx(-7.0).margin(0.01));
    REQUIRE(!model.manualAxisGoTo(3)->past);
    controller.tick();
    snapshot(w, "manual-axis-go-to");
    // Wound part way, then past it, then back to it: once the reading has
    // settled there, it's done
    windTo(3, 5.0);
    REQUIRE(model.manualAxisGoTo(3)->toGoMm == Approx(-2.0).margin(0.01));
    windTo(3, 2.5);
    REQUIRE(model.manualAxisGoTo(3)->past);
    windTo(3, 3.0);
    REQUIRE(tickUntil([&] { return !model.manualAxisGoTo(3); }));

    // Z by itself, with its leader then Enter, and Esc cancels it
    press(w, controller, Qt::Key_Z);
    press(w, controller, Qt::Key_Return);
    REQUIRE(model.manualAxisGoTo(4));
    press(w, controller, Qt::Key_Escape);
    REQUIRE(!model.manualAxisGoTo(4));
    // As does Space
    press(w, controller, Qt::Key_Z);
    press(w, controller, Qt::Key_Return);
    REQUIRE(model.manualAxisGoTo(4));
    press(w, controller, Qt::Key_Space);
    REQUIRE(!model.manualAxisGoTo(4));

    // Zeroing Y clears its slots (they were from the old zero), not Z's
    press(w, controller, Qt::Key_Y);
    press(w, controller, Qt::Key_Z);
    REQUIRE(!model.getMemory(3, 0));
    REQUIRE(model.getMemory(4, 0));
    // Its leader then M stores it alone, and then Delete clears it
    press(w, controller, Qt::Key_Y);
    press(w, controller, Qt::Key_M);
    REQUIRE(*model.getMemory(3, 0) == Approx(0.0).margin(0.005));
    press(w, controller, Qt::Key_Y);
    press(w, controller, Qt::Key_Delete);
    REQUIRE(!model.getMemory(3, 0));
    // An empty slot says so
    press(w, controller, Qt::Key_Y);
    press(w, controller, Qt::Key_Return);
    REQUIRE(!model.manualAxisGoTo(3));
    REQUIRE(model.getWarning() == "Y isn't set in this slot");
}

TEST_CASE("UI: start-up sets the motors' speeds before taking up the backlash")
{
    // (A motor at zero speed cancels any move, so the backlash was never
    // taken up while the speeds were set afterwards)
    MapConfigReader config { { { "Axis1BacklashCompensationSteps", "100" },
                               { "Axis2BacklashCompensationSteps", "50" },
                               { "Axis1SpeedPreset2", "250" },
                               { "Axis2SpeedPreset2", "150" } } };
    mgo::MockGpio gpio { false };
    mgo::Model model { gpio, config };
    mgo::Controller controller(&model);
    REQUIRE(model.motorSpeed(1) == Approx(250.0));
    REQUIRE(model.motorSpeed(2) == Approx(150.0));
    // Re-zeroed after the take-up
    REQUIRE(model.motorPosition(1) == 0.0);
    REQUIRE(model.motorPosition(2) == 0.0);
}

TEST_CASE("UI: keys, moves and zeroing are logged")
{
    const auto file = std::filesystem::temp_directory_path() / "lc_ui_test.log";
    std::filesystem::remove(file);
    mgo::g_logger = new mgo::Logger(file.string());
    {
        TestModel tm;
        mgo::Controller controller(&tm.model);
        QWidget* w = mainWindow();
        REQUIRE(w);
        // A (nudge Z left), then Z Z (zero Z) once it's stopped
        press(w, controller, Qt::Key_A);
        REQUIRE(waitUntil([&] {
            controller.tick();
            return !tm.model.isMotionSequenceRunning() && !tm.model.isMotorRunning(1);
        }));
        controller.tick();
        press(w, controller, Qt::Key_Z);
        press(w, controller, Qt::Key_Z);
    }
    delete mgo::g_logger;
    mgo::g_logger = nullptr;

    std::ifstream in(file);
    const std::string log { std::istreambuf_iterator<char>(in), {} };
    INFO(log);
    REQUIRE(log.find("Taking up the backlash: Z 0 steps, X 0 steps") != std::string::npos);
    REQUIRE(log.find("Key a: Z left 0.05mm") != std::string::npos);
    // (The nudge is quick enough to start and stop between updates)
    REQUIRE(
        (log.find("Z moved from 0.0000 to -0.0500 between updates") != std::string::npos
         || log.find("Z stopped at -0.0500") != std::string::npos));
    REQUIRE(log.find("Key z z:") != std::string::npos);
    REQUIRE(log.find("Z zeroed at -0.0500") != std::string::npos);
    std::filesystem::remove(file);
}

TEST_CASE("UI: measuring the backlash with a (simulated) scale")
{
    // The simulated table here has no backlash: so all of the compensation
    // set is too much, and it should measure no backlash
    MapConfigReader config { { { "Axis1Scale", "serial" },
                               { "Axis1ScaleCountsPerMm", "100" },
                               { "Axis1ScaleSettleMs", "200" },
                               { "Axis1BacklashCompensationSteps", "200" },
                               { "Axis1FakeBacklashSteps", "0" },
                               { "Axis1SpeedPreset2", "600" } } };
    mgo::MockGpio gpio { false };
    mgo::Model model { gpio, config };
    mgo::Controller controller(&model);
    // Runs the model (not the controller, which would show the report)
    const auto runUntil = [&model](const std::function<bool()>& condition, int seconds) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
        while (!condition() && std::chrono::steady_clock::now() < deadline) {
            model.checkStatus();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return condition();
    };
    REQUIRE(!model.canMeasureAxis1Backlash()); // (not lined up yet)
    REQUIRE(runUntil([&model] { return model.canMeasureAxis1Backlash(); }, 5));
    // Moves of 1mm (more than twice the 0.2mm compensation), right first
    REQUIRE(model.axis1BacklashMeasureDistanceMm() == Approx(1.0));
    REQUIRE(model.axis1BacklashMeasureFirstDirection() == 1);
    const double start = model.motorPosition(1);
    // (It measures at the second preset's speed, then puts this back)
    model.setMotorSpeed(1, 250.0);

    model.axis1MeasureBacklash();
    REQUIRE(model.isMeasuringBacklash());
    REQUIRE(model.axisStatus(1) == "Measuring backlash: move 1 of 6");
    // Nothing else may move meanwhile
    REQUIRE(!mgo::keyAllowedInCurrentMode(mgo::key::ENTER, model));
    REQUIRE(!mgo::keyAllowedInCurrentMode(mgo::key::LEFT, model));
    REQUIRE(mgo::keyAllowedInCurrentMode(mgo::key::SPACE, model));
    std::optional<std::vector<std::string>> report;
    REQUIRE(runUntil(
        [&] {
            // (Taken once only)
            if (!report) {
                report = model.takeBacklashReport();
            }
            return report.has_value();
        },
        20));
    REQUIRE(!model.isMeasuringBacklash());
    REQUIRE(model.motorPosition(1) == Approx(start).margin(0.0001));
    REQUIRE(model.motorSpeed(1) == Approx(250.0));
    INFO(report->front());
    // No backlash, to within a count of the scale (10 steps)
    const auto at = report->front().find(", which is ");
    REQUIRE(at != std::string::npos);
    const long steps = std::stol(report->front().substr(at + 11));
    REQUIRE(std::abs(steps) <= 10);

    // Space (stop all motors) stops it part way, and there's no report
    model.axis1MeasureBacklash();
    REQUIRE(model.isMeasuringBacklash());
    model.stopAllMotors();
    REQUIRE(!model.isMeasuringBacklash());
    REQUIRE(!model.takeBacklashReport());
}

namespace {

// A model with a simulated serial scale on axis 1 (1 count = 0.01mm), and
// the given extra config
struct ScaleTestModel {
    explicit ScaleTestModel(std::map<std::string, std::string> extra = {})
        : config([&] {
            extra.insert(
                { { "Axis1Scale", "serial" },
                  { "Axis1ScaleCountsPerMm", "100" },
                  { "Axis1ScaleSettleMs", "200" },
                  { "Axis1SpeedPreset2", "600" } });
            return extra;
        }())
    {
    }
    // Runs the model (not the controller, so no dialogs) until the
    // condition holds, or 10s pass
    bool runUntil(const std::function<bool()>& condition)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!condition() && std::chrono::steady_clock::now() < deadline) {
            model.checkStatus();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return condition();
    }
    MapConfigReader config;
    mgo::MockGpio gpio { false };
    mgo::Model model { gpio, config };
    mgo::Controller controller { &model };
};

} // namespace

namespace {

// Both axes with a (simulated) scale, as the lathe will have
const std::map<std::string, std::string> axis2Scale {
    { "Axis2Scale", "serial" },      { "Axis2ScaleCountsPerMm", "100" },
    { "Axis2ScaleSettleMs", "200" }, { "Axis1ScaleWarnMm", "0.2" },
    { "Axis2ScaleWarnMm", "0.2" },
};

std::map<std::string, std::string> withAxis2Scale(std::map<std::string, std::string> extra = {})
{
    extra.insert(axis2Scale.begin(), axis2Scale.end());
    return extra;
}

} // namespace

TEST_CASE("UI: re-basing from the scale at rest")
{
    ScaleTestModel t({ { "Axis1ScaleRebase", "true" }, { "Axis1ScaleWarnMm", "0.2" } });
    REQUIRE(t.runUntil([&] { return t.model.scalePositionMm(1).has_value(); }));
    // The table goes 0.05mm further than the motor says (e.g. its backlash
    // is less than the compensation, here)
    t.model.fakeScale(1)->setErrorMm(0.05);
    t.model.goToPosition(1, 1.0);
    // Once settled, the position is the scale's (to within a count)
    REQUIRE(t.runUntil([&] {
        return !t.model.isMotorRunning(1) && std::abs(t.model.motorPosition(1) - 1.05) < 0.011;
    }));
    REQUIRE(std::abs(*t.model.scaleDifferenceMm(1)) < 0.011);
    REQUIRE(!t.model.scaleMismatchMm(1));
    // Moves now go from the corrected position: to 2 is a move of 0.95, and
    // the table (still 0.05 ahead of the motor's steps) arrives at 2
    t.model.goToPosition(1, 2.0);
    REQUIRE(t.runUntil([&] {
        return !t.model.isMotorRunning(1) && std::abs(t.model.motorPosition(1) - 2.0) < 0.0001;
    }));
    // (The scale settles, still agreeing: nothing changes)
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    REQUIRE(t.runUntil([&] { return std::abs(*t.model.scaleDifferenceMm(1)) < 0.011; }));
    REQUIRE(t.model.motorPosition(1) == Approx(2.0).margin(0.011));
}

TEST_CASE("UI: re-basing when the scale changes at rest")
{
    ScaleTestModel t({ { "Axis1ScaleRebase", "true" }, { "Axis1ScaleWarnMm", "0.2" } });
    REQUIRE(t.runUntil([&] { return t.model.scalePositionMm(1).has_value(); }));
    t.model.goToPosition(1, 1.0);
    REQUIRE(t.runUntil([&] {
        const auto d = t.model.scaleDifferenceMm(1);
        return !t.model.isMotorRunning(1) && d && std::abs(*d) < 0.011;
    }));
    // Settled at 1. Then, with nothing moving, the table shifts 0.04mm (e.g.
    // pushed by a cut): the position follows, once the reading has settled
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    t.model.checkStatus();
    t.model.fakeScale(1)->setErrorMm(0.04);
    REQUIRE(t.runUntil([&] { return std::abs(t.model.motorPosition(1) - 1.04) < 0.011; }));
    REQUIRE(!t.model.scaleMismatchMm(1));
    // And far more (e.g. a knock): it stops and asks
    t.model.fakeScale(1)->setErrorMm(0.5);
    REQUIRE(t.runUntil([&] { return t.model.scaleMismatchMm(1).has_value(); }));
}

TEST_CASE("UI: no re-basing unless the config asks for it")
{
    ScaleTestModel t;
    REQUIRE(t.runUntil([&] { return t.model.scalePositionMm(1).has_value(); }));
    t.model.fakeScale(1)->setErrorMm(0.5);
    t.model.goToPosition(1, 1.0);
    REQUIRE(t.runUntil([&] {
        const auto d = t.model.scaleDifferenceMm(1);
        return !t.model.isMotorRunning(1) && d && std::abs(*d - 0.5) < 0.011;
    }));
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    for (int n = 0; n < 10; ++n) {
        t.model.checkStatus();
    }
    // Nothing done: no warning, and the position is the motor's
    REQUIRE(t.model.motorPosition(1) == Approx(1.0).margin(0.0001));
    REQUIRE(!t.model.scaleMismatchMm(1));
}

TEST_CASE("UI: re-basing from the scale by hand (Z then B)")
{
    // (No re-basing at rest, so the difference stays until it's done by hand)
    ScaleTestModel t;
    QWidget* w = mainWindow();
    REQUIRE(w);
    REQUIRE(t.runUntil([&] { return t.model.scalePositionMm(1).has_value(); }));
    t.model.fakeScale(1)->setErrorMm(0.08);
    t.model.goToPosition(1, 1.0);
    REQUIRE(t.runUntil([&] {
        const auto d = t.model.scaleDifferenceMm(1);
        return !t.model.isMotorRunning(1) && d && std::abs(*d - 0.08) < 0.011
            && t.model.scaleIsSteady(1);
    }));
    REQUIRE(t.model.canRebaseFromScale(1));
    press(w, t.controller, Qt::Key_Z);
    press(w, t.controller, Qt::Key_B);
    REQUIRE(t.model.motorPosition(1) == Approx(1.08).margin(0.011));
    REQUIRE(std::abs(*t.model.scaleDifferenceMm(1)) < 0.011);

    // Not while the axis is moving: it warns, and nothing changes
    t.model.move(1, mgo::axisDirection(mgo::ZDirection::Left));
    REQUIRE(t.model.isMotorRunning(1));
    REQUIRE(!t.model.canRebaseFromScale(1));
    t.model.rebaseFromScaleByHand(1);
    REQUIRE(t.model.getWarning() == "Stop the motors first");
    t.model.stopAllMotors();
}

namespace {

// Starts a two-pass multi-pass from where the axes are (Z by -1, X in by
// 0.2, 0.1 a pass)
void startMultiPass(ScaleTestModel& t, bool pause)
{
    t.model.setMotorSpeed(1, 500.0);
    t.model.setMotorSpeed(2, 100.0);
    t.model.changeMode(mgo::Mode::MultiPass);
    REQUIRE(t.model
                .setMultiPassSetup(
                    { .cutDistance = -1.0, .feedInfeed = 0.2, .feedPerPass = 0.1, .pause = pause })
                .empty());
    t.model.multiPassGo();
    REQUIRE(t.model.multiPass().inProgress());
}

bool multiPassIn(const mgo::Model& model, mgo::MultiPass::State state)
{
    return model.multiPass().state() == state;
}

} // namespace

TEST_CASE("UI: re-basing before each pass of a multi-pass", "[timing]")
{
    ScaleTestModel t(
        { { "Axis1ScaleRebaseBetweenPasses", "true" }, { "Axis1ScaleWarnMm", "0.2" } });
    REQUIRE(t.runUntil([&] { return t.model.scaleIsSteady(1); }));
    const bool pause = GENERATE(false, true);
    INFO((pause ? "Pausing" : "Not pausing"));
    startMultiPass(t, pause);
    // During the first pass, the table gets 0.05mm ahead of the motor (e.g.
    // carried on at a stop)
    t.model.fakeScale(1)->setErrorMm(0.05);
    // Before the next pass, it waits for the scale to settle, re-bases, and
    // goes back to the real start, so the table ends where it should, and
    // agrees with the motor
    bool settling = false;
    REQUIRE(t.runUntil([&] {
        settling = settling || multiPassIn(t.model, mgo::MultiPass::State::Settling);
        if (multiPassIn(t.model, mgo::MultiPass::State::Paused)) {
            // (Enter at once: the scale is probably still settling)
            t.model.multiPassGo();
        }
        return !t.model.multiPass().inProgress();
    }));
    INFO("Stopped: " << t.model.multiPass().message() << " / " << t.model.getWarning());
    REQUIRE(t.model.multiPass().state() == mgo::MultiPass::State::Finished);
    REQUIRE(settling);
    REQUIRE(t.runUntil([&] { return t.model.scaleIsSteady(1); }));
    REQUIRE(*t.model.scalePositionMm(1) == Approx(0.0).margin(0.011));
    REQUIRE(std::abs(*t.model.scaleDifferenceMm(1)) < 0.011);
}

TEST_CASE("UI: re-basing axis 2 after each feed of a multi-pass", "[timing]")
{
    // The lathe's X, the feed axis: each pass is cut at the depth its scale
    // says, not just where the motor's steps say
    ScaleTestModel t(withAxis2Scale({ { "Axis2ScaleRebaseBetweenPasses", "true" } }));
    REQUIRE(t.runUntil([&] { return t.model.scaleIsSteady(1) && t.model.scaleIsSteady(2); }));
    const bool pause = GENERATE(false, true);
    INFO((pause ? "Pausing" : "Not pausing"));
    startMultiPass(t, pause);
    // During the first pass, the table falls 0.03mm short of X's motor
    t.model.fakeScale(2)->setErrorMm(-0.03);
    bool feedSettling = false;
    std::vector<std::string> overlay;
    REQUIRE(t.runUntil([&] {
        if (multiPassIn(t.model, mgo::MultiPass::State::FeedSettling)) {
            feedSettling = true;
            overlay = mgo::settlingOverlayText(t.model);
        }
        if (multiPassIn(t.model, mgo::MultiPass::State::Paused)) {
            t.model.multiPassGo();
        }
        return !t.model.multiPass().inProgress();
    }));
    INFO("Stopped: " << t.model.multiPass().message() << " / " << t.model.getWarning());
    REQUIRE(t.model.multiPass().state() == mgo::MultiPass::State::Finished);
    REQUIRE(feedSettling);
    REQUIRE(!overlay.empty());
    REQUIRE(overlay.front() == "Waiting for X's scale");
    // The last pass was cut where X's scale says the last depth is
    REQUIRE(t.runUntil([&] { return t.model.scaleIsSteady(2); }));
    REQUIRE(
        *t.model.scalePositionMm(2) == Approx(t.model.multiPass().plan().feedEnd).margin(0.011));
    REQUIRE(std::abs(*t.model.scaleDifferenceMm(2)) < 0.011);
}

TEST_CASE("UI: a multi-pass's feed approaches its depth, never going past it", "[timing]")
{
    // As the last pass's feed starts, X's table gets 0.03mm ahead of its
    // motor, into the work (as an offset: the re-base then takes it out). Fed
    // straight to the depth, it goes 0.03 too deep, before the re-base brings
    // it back out (leaving the slack the wrong way)
    const bool approach = GENERATE(false, true);
    INFO((approach ? "With an approach" : "Without"));
    std::map<std::string, std::string> settings { { "Axis2ScaleRebaseBetweenPasses", "true" } };
    if (approach) {
        settings.insert(
            { { "Axis2ScaleApproachMm", "0.05" }, { "Axis2ScaleApproachSpeed", "20" } });
    }
    ScaleTestModel t(withAxis2Scale(settings));
    REQUIRE(t.runUntil([&] { return t.model.scaleIsSteady(1) && t.model.scaleIsSteady(2); }));
    startMultiPass(t, false);
    const double depth = t.model.multiPass().plan().feedEnd;
    const int in = depth < t.model.multiPass().plan().feedStart ? -1 : 1;
    bool shifted = false;
    double deepest = 0.0;
    REQUIRE(t.runUntil([&] {
        if (!shifted && t.model.multiPass().pass() == 2
            && multiPassIn(t.model, mgo::MultiPass::State::Feeding)) {
            t.model.fakeScale(2)->setErrorMm(0.03 * in);
            shifted = true;
        }
        if (const auto x = t.model.scalePositionMm(2)) {
            deepest = std::max(deepest, *x * in);
        }
        return !t.model.multiPass().inProgress();
    }));
    INFO("Stopped: " << t.model.multiPass().message() << " / " << t.model.getWarning());
    REQUIRE(t.model.multiPass().state() == mgo::MultiPass::State::Finished);
    REQUIRE(shifted);
    REQUIRE(t.runUntil([&] { return t.model.scaleIsSteady(2); }));
    REQUIRE(*t.model.scalePositionMm(2) == Approx(depth).margin(0.011));
    if (approach) {
        REQUIRE(deepest < depth * in + 0.011);
    } else {
        REQUIRE(deepest > depth * in + 0.02);
    }
    // (The feed's speed is put back after creeping)
    REQUIRE(t.model.motorSpeed(2) == Approx(100.0));
}

TEST_CASE("UI: a multi-pass's cut approaches its end, never going past it", "[timing]")
{
    // As the second pass's cut starts, Z's table gets 0.05mm ahead of its
    // motor, the way it's cutting (as the backlash compensation on the
    // reversal into the cut can do): cut straight to the end, it would end
    // 0.05 past it, e.g. into a shoulder. Then, as the return starts, it
    // gets 0.05 ahead the other way (from the frame the cut's approach
    // re-based it to): the return straight back would end 0.05 past the
    // start (e.g. into a pocket's other wall). Without the approach, that
    // second shift just cancels the first, so only the cut's end is checked
    // then. (Checked by hand: with the return left out of the approach, the
    // table goes back 0.05 past the start)
    const bool approach = GENERATE(false, true);
    INFO((approach ? "With an approach" : "Without"));
    std::map<std::string, std::string> settings {
        { "Axis1ScaleRebaseBetweenPasses", "true" },
        { "Axis1ScaleWarnMm", "0.2" },
    };
    if (approach) {
        settings.insert({ { "Axis1ScaleApproachMm", "0.2" }, { "Axis1ScaleApproachSpeed", "20" } });
    }
    ScaleTestModel t(settings);
    REQUIRE(t.runUntil([&] { return t.model.scaleIsSteady(1); }));
    startMultiPass(t, false);
    const double start = t.model.multiPass().plan().cutStart;
    const double end = t.model.multiPass().plan().cutEnd;
    const int way = end < start ? -1 : 1;
    bool shiftedCut = false;
    bool shiftedReturn = false;
    bool crept = false;
    double furthest = 0.0;
    double furthestBack = 0.0;
    REQUIRE(t.runUntil([&] {
        using State = mgo::MultiPass::State;
        if (!shiftedCut && t.model.multiPass().pass() == 2
            && multiPassIn(t.model, State::Cutting)) {
            t.model.fakeScale(1)->setErrorMm(0.05 * way);
            shiftedCut = true;
        }
        if (shiftedCut && !shiftedReturn && multiPassIn(t.model, State::Returning)) {
            t.model.fakeScale(1)->setErrorMm(0.05 * way - 0.05 * way);
            shiftedReturn = true;
        }
        crept = crept
            || (multiPassIn(t.model, State::Cutting) && t.model.isMotorRunning(1)
                && t.model.motorSpeed(1) == Approx(20.0));
        // (Where the table really is: the reading lags, and the return
        // starts before it would catch up)
        const double z = t.model.fakeScale(1)->tableMm();
        furthest = std::max(furthest, (z - start) * way);
        furthestBack = std::max(furthestBack, (start - z) * way);
        return !t.model.multiPass().inProgress();
    }));
    INFO("Stopped: " << t.model.multiPass().message() << " / " << t.model.getWarning());
    REQUIRE(t.model.multiPass().state() == mgo::MultiPass::State::Finished);
    REQUIRE(shiftedReturn);
    const double length = (end - start) * way;
    if (approach) {
        REQUIRE(crept);
        REQUIRE(furthest < length + 0.011);
        REQUIRE(furthestBack < 0.011);
    } else {
        REQUIRE(furthest > length + 0.03);
    }
    // (The cut speed is kept from pass to pass)
    REQUIRE(t.model.motorSpeed(1) == Approx(500.0));
}

TEST_CASE("UI: a cut's approach with backlash compensation (the fake build's mill)", "[timing]")
{
    // As found in the fake build with lc_mill.cfg (20 Nov 2026): 0.256mm of
    // backlash compensation, and an approach of 0.2. The simulated table
    // used to have no backlash, so each reversal into a cut took it 0.256
    // further than the motor said: past the stop-short point, and the end.
    // Now its backlash matches the compensation (as on a machine set up
    // well), so the cut stops short, creeps in, and ends at the end
    const bool tableHasBacklash = GENERATE(true, false);
    INFO((tableHasBacklash ? "The table's backlash as compensated" : "A table with no backlash"));
    std::map<std::string, std::string> settings {
        { "Axis1ScaleRebaseBetweenPasses", "true" }, { "Axis1ScaleWarnMm", "0.5" },
        { "Axis1ScaleApproachMm", "0.2" },           { "Axis1ScaleApproachSpeed", "20" },
        { "Axis1BacklashCompensationSteps", "256" },
    };
    if (!tableHasBacklash) {
        settings.insert({ "Axis1FakeBacklashSteps", "0" });
    }
    ScaleTestModel t(settings);
    REQUIRE(t.runUntil([&] { return t.model.scaleIsSteady(1); }));
    // (The simulated table follows the motor's actual steps, which the
    // start-up's backlash take-up moved before the position was zeroed: so
    // it's measured from where it was then)
    const double tableOffset = t.model.fakeScale(1)->tableMm() - t.model.motorPosition(1);
    startMultiPass(t, false);
    const double start = t.model.multiPass().plan().cutStart;
    const double end = t.model.multiPass().plan().cutEnd;
    const int way = end < start ? -1 : 1;
    double furthest = 0.0;
    std::string warning;
    REQUIRE(t.runUntil([&] {
        furthest
            = std::max(furthest, (t.model.fakeScale(1)->tableMm() - tableOffset - start) * way);
        if (warning.empty()) {
            warning = t.model.getWarning();
        }
        return !t.model.multiPass().inProgress();
    }));
    INFO("Stopped: " << t.model.multiPass().message() << " / " << warning);
    REQUIRE(t.model.multiPass().state() == mgo::MultiPass::State::Finished);
    const double length = (end - start) * way;
    if (tableHasBacklash) {
        REQUIRE(furthest < length + 0.011);
        REQUIRE(warning.empty());
    } else {
        // It's past the stop-short point already: it says so, rather than
        // just reversing onto the end
        REQUIRE(warning.find("past its target") != std::string::npos);
    }
}

TEST_CASE("UI: Space stops a multi-pass waiting for the scale to settle", "[timing]")
{
    ScaleTestModel t({ { "Axis1ScaleRebaseBetweenPasses", "true" } });
    REQUIRE(t.runUntil([&] { return t.model.scaleIsSteady(1); }));
    startMultiPass(t, false);
    REQUIRE(mgo::settlingOverlayText(t.model).empty());
    REQUIRE(t.runUntil([&] { return multiPassIn(t.model, mgo::MultiPass::State::Settling); }));
    // An overlay says why keys do nothing meanwhile
    const auto overlay = mgo::settlingOverlayText(t.model);
    REQUIRE(!overlay.empty());
    REQUIRE(overlay.front() == "Waiting for Z's scale");
    t.controller.tick();
    snapshot(mainWindow(), "settling_overlay");
    t.model.stopAllMotors();
    REQUIRE(mgo::settlingOverlayText(t.model).empty());
    REQUIRE(!t.model.multiPass().inProgress());
}

TEST_CASE("UI: no re-basing between passes unless the config asks for it", "[timing]")
{
    ScaleTestModel t({ { "Axis1ScaleRebase", "true" } });
    REQUIRE(t.runUntil([&] { return t.model.scaleIsSteady(1); }));
    startMultiPass(t, true);
    t.model.fakeScale(1)->setErrorMm(0.05);
    REQUIRE(t.runUntil([&] {
        return multiPassIn(t.model, mgo::MultiPass::State::Paused) && t.model.scaleIsSteady(1);
    }));
    REQUIRE(t.model.motorPosition(1) == Approx(0.0).margin(0.0001));
    // But it can be done by hand (Z then B: through the keys, as the leader
    // key was once refused while paused, so B never got there)
    REQUIRE(t.model.canRebaseFromScale(1));
    REQUIRE(mgo::keyAllowedInCurrentMode(mgo::key::a1_b, t.model));
    QWidget* w = mainWindow();
    REQUIRE(w);
    press(w, t.controller, Qt::Key_Z);
    press(w, t.controller, Qt::Key_B);
    REQUIRE(t.model.motorPosition(1) == Approx(0.05).margin(0.011));
    // And the next pass doesn't wait for the scale
    bool settling = false;
    REQUIRE(t.runUntil([&] {
        settling = settling || multiPassIn(t.model, mgo::MultiPass::State::Settling);
        if (multiPassIn(t.model, mgo::MultiPass::State::Paused)) {
            t.model.multiPassGo();
        }
        return !t.model.multiPass().inProgress();
    }));
    REQUIRE(!settling);
}

TEST_CASE("UI: the scale and motor too far apart stops everything and asks")
{
    ScaleTestModel t({ { "Axis1ScaleRebase", "true" }, { "Axis1ScaleWarnMm", "0.2" } });
    REQUIRE(t.runUntil([&] { return t.model.scalePositionMm(1).has_value(); }));
    // e.g. the motor stalled, losing 0.5mm
    t.model.fakeScale(1)->setErrorMm(-0.5);
    t.model.goToPosition(1, 1.0);
    REQUIRE(t.runUntil([&] { return t.model.scaleMismatchMm(1).has_value(); }));
    REQUIRE(*t.model.scaleMismatchMm(1) == Approx(-0.5).margin(0.011));
    REQUIRE(t.model.motorPosition(1) == Approx(1.0).margin(0.0001)); // (not re-based)
    // Nothing may move until it's dealt with
    REQUIRE(!mgo::keyAllowedInCurrentMode(mgo::key::ENTER, t.model));
    REQUIRE(!mgo::keyAllowedInCurrentMode(mgo::key::RIGHT, t.model));
    REQUIRE(mgo::keyAllowedInCurrentMode(mgo::key::SPACE, t.model));

    SECTION("Use the scale's position")
    {
        t.model.acceptScalePosition(1);
        REQUIRE(!t.model.scaleMismatchMm(1));
        REQUIRE(t.model.motorPosition(1) == Approx(0.5).margin(0.011));
        REQUIRE(mgo::keyAllowedInCurrentMode(mgo::key::ENTER, t.model));
    }
    SECTION("Keep the motor's position")
    {
        t.model.keepMotorPosition(1);
        REQUIRE(!t.model.scaleMismatchMm(1));
        REQUIRE(t.runUntil([&] {
            const auto d = t.model.scaleDifferenceMm(1);
            return d && std::abs(*d) < 0.011;
        }));
        REQUIRE(t.model.motorPosition(1) == Approx(1.0).margin(0.0001));
    }
}

namespace {

// (The simulated sender averages a third of a second's readings)
const std::map<std::string, std::string> movingChecks {
    { "Axis1ScaleWarnMm", "0.2" },
    { "Axis1ScaleLagMs", "336" },
    { "Axis1ScaleStallMm", "1.0" },
    { "Axis1ScaleMovingFaultMm", "1.0" },
};

} // namespace

TEST_CASE("UI: a fast move with the table keeping up isn't a stall", "[timing]")
{
    ScaleTestModel t(movingChecks);
    REQUIRE(t.runUntil([&] { return t.model.scaleIsSteady(1); }));
    t.model.setMotorSpeed(1, 600.0);
    t.model.goToPosition(1, 20.0);
    REQUIRE(t.runUntil([&] {
        return !t.model.isMotorRunning(1) && std::abs(t.model.motorPosition(1) - 20.0) < 0.0001;
    }));
    REQUIRE(t.runUntil([&] { return t.model.scaleIsSteady(1); }));
    INFO(t.model.getWarning());
    REQUIRE(t.model.getWarning().empty());
    REQUIRE(!t.model.scaleMismatchMm(1));
}

TEST_CASE("UI: a stall while moving stops everything at once, then asks", "[timing]")
{
    ScaleTestModel t(movingChecks);
    REQUIRE(t.runUntil([&] { return t.model.scaleIsSteady(1); }));
    t.model.setMotorSpeed(1, 600.0);
    t.model.goToPosition(1, 20.0);
    // Part way, the table falls 3mm behind (e.g. the motor stalled)
    REQUIRE(t.runUntil([&] { return t.model.motorPosition(1) > 5.0; }));
    t.model.fakeScale(1)->setErrorMm(-3.0);
    REQUIRE(t.runUntil([&] { return !t.model.isMotorRunning(1); }));
    INFO(t.model.getWarning());
    REQUIRE(t.model.getWarning().starts_with("Z stalled"));
    REQUIRE(t.model.motorPosition(1) < 15.0);
    // Once the reading has settled, it asks which to believe, as usual
    REQUIRE(t.runUntil([&] { return t.model.scaleMismatchMm(1).has_value(); }));
    REQUIRE(*t.model.scaleMismatchMm(1) == Approx(-3.0).margin(0.05));
}

TEST_CASE("UI: the dialog when the scale and motor disagree")
{
    ScaleTestModel t({ { "Axis1ScaleWarnMm", "0.2" } });
    REQUIRE(t.runUntil([&] { return t.model.scalePositionMm(1).has_value(); }));
    t.model.fakeScale(1)->setErrorMm(0.3);
    t.model.goToPosition(1, 1.0);
    REQUIRE(t.runUntil([&] { return t.model.scaleMismatchMm(1).has_value(); }));
    bool shown = false;
    whenDialogShows([&shown](QWidget* dialog) {
        shown = true;
        QTest::keyClick(inputWidget(dialog), Qt::Key_Y);
    });
    t.controller.tick();
    REQUIRE(waitForDialogToClose());
    REQUIRE(shown);
    REQUIRE(!t.model.scaleMismatchMm(1));
    REQUIRE(t.model.motorPosition(1) == Approx(1.3).margin(0.011));
}

TEST_CASE("UI: a mismatch found straight after one is answered is asked about too")
{
    // (On the mill, a second mismatch was found before the controller had
    // seen the first one cleared, so was never asked about, and every key
    // was refused from then on)
    ScaleTestModel t({ { "Axis1ScaleWarnMm", "0.2" } });
    REQUIRE(t.runUntil([&] { return t.model.scalePositionMm(1).has_value(); }));
    t.model.fakeScale(1)->setErrorMm(0.3);
    t.model.goToPosition(1, 1.0);
    REQUIRE(t.runUntil([&] { return t.model.scaleMismatchMm(1).has_value(); }));
    whenDialogShows([](QWidget* dialog) { QTest::keyClick(inputWidget(dialog), Qt::Key_Y); });
    t.controller.tick();
    REQUIRE(waitForDialogToClose());
    REQUIRE(!t.model.scaleMismatchMm(1));
    REQUIRE(t.model.motorPosition(1) == Approx(1.3).margin(0.011));
    // The table shifts again (the model runs, but the controller doesn't
    // tick in between)
    t.model.fakeScale(1)->setErrorMm(0.8);
    REQUIRE(t.runUntil([&] { return t.model.scaleMismatchMm(1).has_value(); }));
    bool shown = false;
    whenDialogShows([&shown](QWidget* dialog) {
        shown = true;
        QTest::keyClick(inputWidget(dialog), Qt::Key_N);
    });
    t.controller.tick();
    REQUIRE(waitForDialogToClose());
    REQUIRE(shown);
    REQUIRE(!t.model.scaleMismatchMm(1));
    REQUIRE(t.model.motorPosition(1) == Approx(1.3).margin(0.011));
}

TEST_CASE("UI: the scale's position isn't used until its reading is steady")
{
    ScaleTestModel t({ { "Axis1ScaleWarnMm", "0.2" }, { "Axis1ScaleMaxSettleMs", "1000" } });
    REQUIRE(t.runUntil([&] { return t.model.scalePositionMm(1).has_value(); }));
    t.model.goToPosition(1, 1.0);
    REQUIRE(t.runUntil([&] {
        const auto d = t.model.scaleDifferenceMm(1);
        return !t.model.isMotorRunning(1) && d && std::abs(*d) < 0.011;
    }));
    // The reading runs away (e.g. interference), never settling: after the
    // longest time, everything stops and asks
    double error = 0.0;
    REQUIRE(t.runUntil([&] {
        error += 0.5;
        t.model.fakeScale(1)->setErrorMm(error);
        return t.model.scaleMismatchMm(1).has_value();
    }));
    REQUIRE(!t.model.scaleIsSteady(1));
    QString text;
    whenDialogShows([&text](QWidget* dialog) {
        text = dialogText(dialog);
        QTest::keyClick(inputWidget(dialog), Qt::Key_Y);
    });
    t.controller.tick();
    REQUIRE(waitForDialogToClose());
    REQUIRE(text.contains("still changing"));
    // Not used: the position is still the motor's, and it still asks
    REQUIRE(t.model.scaleMismatchMm(1));
    REQUIRE(t.model.motorPosition(1) == Approx(1.0).margin(0.0001));
    REQUIRE(!mgo::keyAllowedInCurrentMode(mgo::key::ENTER, t.model));

    // Once the reading settles, it asks again, and then it's used
    const int asks = t.model.scaleMismatchAsks(1);
    t.model.fakeScale(1)->setErrorMm(0.3);
    REQUIRE(t.runUntil([&] { return t.model.scaleMismatchAsks(1) > asks; }));
    REQUIRE(t.model.scaleIsSteady(1));
    REQUIRE(*t.model.scaleMismatchMm(1) == Approx(0.3).margin(0.011));
    text.clear();
    whenDialogShows([&text](QWidget* dialog) {
        text = dialogText(dialog);
        QTest::keyClick(inputWidget(dialog), Qt::Key_Y);
    });
    t.controller.tick();
    REQUIRE(waitForDialogToClose());
    REQUIRE(!text.isEmpty());
    REQUIRE(!text.contains("still changing"));
    REQUIRE(!t.model.scaleMismatchMm(1));
    REQUIRE(t.model.motorPosition(1) == Approx(1.3).margin(0.011));
}

TEST_CASE("UI: a scale which jumps at rest is misreading, so only the position can be kept")
{
    ScaleTestModel t(
        { { "Axis1ScaleRebase", "true" },
          { "Axis1ScaleWarnMm", "0.2" },
          { "Axis1ScaleFaultMm", "5" } });
    REQUIRE(t.runUntil([&] { return t.model.scalePositionMm(1).has_value(); }));
    t.model.goToPosition(1, 1.0);
    REQUIRE(t.runUntil([&] {
        const auto d = t.model.scaleDifferenceMm(1);
        return !t.model.isMotorRunning(1) && d && std::abs(*d) < 0.011 && t.model.scaleIsSteady(1);
    }));
    // As on the mill, with the lathe running
    t.model.fakeScale(1)->setErrorMm(300.0);
    REQUIRE(t.runUntil([&] { return t.model.scaleMisreadJumpMm(1).has_value(); }));
    REQUIRE(t.model.scaleMismatchMm(1));
    REQUIRE(t.model.scaleMismatchLimitMm(1) == Approx(5.0));
    REQUIRE(!mgo::keyAllowedInCurrentMode(mgo::key::ENTER, t.model));
    // Y can't take the scale's position: any key keeps the motor's
    QString text;
    whenDialogShows([&text](QWidget* dialog) {
        text = dialogText(dialog);
        QTest::keyClick(inputWidget(dialog), Qt::Key_Y);
    });
    t.controller.tick();
    REQUIRE(waitForDialogToClose());
    REQUIRE(text.contains("misreading"));
    REQUIRE(!t.model.scaleMismatchMm(1));
    REQUIRE(!t.model.scaleMisreadJumpMm(1));
    REQUIRE(t.model.motorPosition(1) == Approx(1.0).margin(0.0001));
    // The scale is lined up with the motor again
    REQUIRE(t.runUntil([&] {
        const auto d = t.model.scaleDifferenceMm(1);
        return d && std::abs(*d) < 0.011;
    }));
    REQUIRE(t.model.motorPosition(1) == Approx(1.0).margin(0.011));
}

TEST_CASE("UI: a scale on axis 2 is re-based at rest")
{
    ScaleTestModel t(withAxis2Scale({ { "Axis2ScaleRebase", "true" } }));
    REQUIRE(t.model.hasScale(2));
    REQUIRE(t.runUntil([&] { return t.model.scalePositionMm(2).has_value(); }));
    // At rest, as for axis 1: the position becomes the scale's
    t.model.fakeScale(2)->setErrorMm(0.05);
    t.model.goToPosition(2, 1.0);
    REQUIRE(t.runUntil([&] {
        return !t.model.isMotorRunning(2) && std::abs(t.model.motorPosition(2) - 1.05) < 0.011;
    }));
    REQUIRE(!t.model.anyScaleMismatch());
    // Axis 1's scale is left alone
    REQUIRE(std::abs(*t.model.scaleDifferenceMm(1)) < 0.011);
}

TEST_CASE("UI: each axis's scale is shown on its line")
{
    // (With LC_SNAPSHOT_DIR set, to look at: alongside the taper's angle and
    // X's retraction, which are drawn to the right of X's line)
    ScaleTestModel t(withAxis2Scale());
    QWidget* w = mainWindow();
    REQUIRE(w);
    REQUIRE(t.runUntil([&] {
        return t.model.scalePositionMm(1).has_value() && t.model.scalePositionMm(2).has_value();
    }));
    t.model.axis2Retract();
    REQUIRE(t.runUntil([&] { return !t.model.isMotorRunning(2); }));
    t.controller.tick();
    snapshot(w, "scales_retracted");
    t.model.changeMode(mgo::Mode::Taper);
    t.controller.tick();
    snapshot(w, "scales_taper");
    t.model.stopAllMotors();
}

TEST_CASE("UI: re-basing axis 2 from its scale by hand (X then B)")
{
    // (With re-basing at rest off)
    ScaleTestModel u(withAxis2Scale());
    QWidget* w = mainWindow();
    REQUIRE(w);
    REQUIRE(u.runUntil([&] { return u.model.scalePositionMm(2).has_value(); }));
    u.model.fakeScale(2)->setErrorMm(0.08);
    u.model.goToPosition(2, 1.0);
    REQUIRE(u.runUntil([&] {
        const auto d = u.model.scaleDifferenceMm(2);
        return !u.model.isMotorRunning(2) && d && std::abs(*d - 0.08) < 0.011
            && u.model.scaleIsSteady(2);
    }));
    REQUIRE(u.model.canRebaseFromScale(2));
    REQUIRE(std::ranges::any_of(mgo::buildCommands(u.config), [](const auto& c) {
        return c.key == mgo::key::a2_b;
    }));
    press(w, u.controller, Qt::Key_X);
    press(w, u.controller, Qt::Key_B);
    REQUIRE(u.model.motorPosition(2) == Approx(1.08).margin(0.011));
    REQUIRE(std::abs(*u.model.scaleDifferenceMm(2)) < 0.011);
    // Zeroing X lines its scale up with it again
    u.model.zero(2);
    REQUIRE(u.runUntil([&] {
        const auto d = u.model.scaleDifferenceMm(2);
        return d && std::abs(*d) < 0.011;
    }));
}

TEST_CASE("UI: both axes disagreeing with their scales are asked about in one dialog")
{
    ScaleTestModel t(withAxis2Scale());
    REQUIRE(t.runUntil([&] {
        return t.model.scalePositionMm(1).has_value() && t.model.scalePositionMm(2).has_value();
    }));
    t.model.fakeScale(1)->setErrorMm(0.3);
    t.model.fakeScale(2)->setErrorMm(-0.4);
    t.model.goToPosition(1, 1.0);
    t.model.goToPosition(2, 1.0);
    // The first to settle stops everything; the other is checked once its
    // scale has settled too
    REQUIRE(t.runUntil([&] {
        return t.model.scaleMismatchMm(1).has_value() && t.model.scaleMismatchMm(2).has_value();
    }));
    const double z = t.model.motorPosition(1);
    const double x = t.model.motorPosition(2);
    int dialogs = 0;
    QString text;
    whenDialogShows([&](QWidget* dialog) {
        ++dialogs;
        text = dialogText(dialog);
        QTest::keyClick(inputWidget(dialog), Qt::Key_Y);
    });
    t.controller.tick();
    REQUIRE(waitForDialogToClose());
    REQUIRE(dialogs == 1);
    INFO(text.toStdString());
    REQUIRE(text.contains("Z and X's scales and positions disagree"));
    REQUIRE(text.contains("Z's scale says"));
    REQUIRE(text.contains("X's scale says"));
    // Y: both take their scale's position
    REQUIRE(!t.model.anyScaleMismatch());
    REQUIRE(t.model.motorPosition(1) == Approx(z + 0.3).margin(0.011));
    REQUIRE(t.model.motorPosition(2) == Approx(x - 0.4).margin(0.011));
}

TEST_CASE("UI: one dialog waits for the other axis's scale to settle")
{
    // Z's mismatch is found first; X is still moving when everything stops,
    // so the dialog waits for X's scale to settle, then asks about both
    ScaleTestModel t(withAxis2Scale({ { "Axis2ScaleSettleMs", "600" } }));
    REQUIRE(t.runUntil([&] {
        return t.model.scalePositionMm(1).has_value() && t.model.scalePositionMm(2).has_value();
    }));
    t.model.fakeScale(1)->setErrorMm(0.3);
    t.model.fakeScale(2)->setErrorMm(0.4);
    t.model.goToPosition(1, 0.5);
    t.model.setMotorSpeed(2, 20.0);
    t.model.goToPosition(2, 5.0);
    REQUIRE(t.runUntil([&] { return t.model.scaleMismatchMm(1).has_value(); }));
    REQUIRE(!t.model.scaleMismatchMm(2));
    REQUIRE(t.model.scalesStillSettling());
    int dialogs = 0;
    QString text;
    whenDialogShows([&](QWidget* dialog) {
        ++dialogs;
        text = dialogText(dialog);
        QTest::keyClick(inputWidget(dialog), Qt::Key_N);
    });
    // Not yet
    t.controller.tick();
    REQUIRE(dialogs == 0);
    REQUIRE(t.runUntil([&] { return !t.model.scalesStillSettling(); }));
    REQUIRE(t.model.scaleMismatchMm(2));
    t.controller.tick();
    REQUIRE(waitForDialogToClose());
    REQUIRE(dialogs == 1);
    INFO(text.toStdString());
    REQUIRE(text.contains("Z and X"));
    // N: both keep their motor's position
    REQUIRE(!t.model.anyScaleMismatch());
}

TEST_CASE("UI: one axis misreading and the other disagreeing, in one dialog")
{
    ScaleTestModel t(withAxis2Scale({ { "Axis1ScaleFaultMm", "5" } }));
    REQUIRE(t.runUntil(
        [&] { return t.model.scaleIsSteady(1) && t.model.scalePositionMm(2).has_value(); }));
    t.model.fakeScale(2)->setErrorMm(0.3);
    t.model.goToPosition(2, 1.0);
    REQUIRE(t.runUntil([&] { return t.model.scaleMismatchMm(2).has_value(); }));
    // Before it's asked about, Z's scale jumps at rest
    t.model.fakeScale(1)->setErrorMm(300.0);
    REQUIRE(t.runUntil([&] { return t.model.scaleMisreadJumpMm(1).has_value(); }));
    const double z = t.model.motorPosition(1);
    const double x = t.model.motorPosition(2);
    QString text;
    whenDialogShows([&](QWidget* dialog) {
        text = dialogText(dialog);
        QTest::keyClick(inputWidget(dialog), Qt::Key_Y);
    });
    t.controller.tick();
    REQUIRE(waitForDialogToClose());
    INFO(text.toStdString());
    REQUIRE(text.contains("Z keeps its position"));
    // Y: X takes its scale's position; Z keeps its motor's, whatever
    REQUIRE(!t.model.anyScaleMismatch());
    REQUIRE(t.model.motorPosition(1) == Approx(z).margin(0.0001));
    REQUIRE(t.model.motorPosition(2) == Approx(x + 0.3).margin(0.011));
}

TEST_CASE("UI: the status line's items move along to make room")
{
    // Each fits: where they usually go
    REQUIRE(
        mgo::lineUpItems({ { 20, 200 }, { 350, 150 }, { 550, 150 }, { 750, 100 } }, 30)
        == std::vector<float> { 20, 350, 550, 750 });
    // The second is long: those after it move along, each leaving the gap
    REQUIRE(
        mgo::lineUpItems({ { 20, 200 }, { 350, 400 }, { 550, 150 }, { 750, 100 } }, 30)
        == std::vector<float> { 20, 350, 780, 960 });
    // The first is long, but the last still fits where it usually goes
    REQUIRE(
        mgo::lineUpItems({ { 20, 340 }, { 350, 100 }, { 550, 100 }, { 750, 100 } }, 30)
        == std::vector<float> { 20, 390, 550, 750 });
}

TEST_CASE("UI: finishing moves on the scale")
{
    ScaleTestModel t(
        { { "Axis1ScaleRebase", "true" },
          { "Axis1ScaleWarnMm", "0.2" },
          { "Axis1ScaleFinishMoves", "true" } });
    REQUIRE(t.model.finishesOnScale(1));
    REQUIRE(t.runUntil([&] { return t.model.scalePositionMm(1).has_value(); }));
    const auto finishedAt = [&t](double target) {
        return t.runUntil([&] {
            const auto scale = t.model.scalePositionMm(1);
            return !t.model.isFinishingOnScale(1) && !t.model.isMotorRunning(1) && scale
                && std::abs(*scale - target) < 0.011;
        });
    };

    SECTION("Short of the target: moves on")
    {
        // The table falls 0.05mm short of where the motor's steps say
        t.model.fakeScale(1)->setErrorMm(-0.05);
        t.model.goToPosition(1, 1.0);
        REQUIRE(t.model.isFinishingOnScale(1));
        // (Keys wait meanwhile)
        REQUIRE(t.runUntil([&] {
            return !t.model.isMotorRunning(1) && t.model.isFinishingOnScale(1)
                && t.model.axisStatus(1).starts_with("Finishing on the scale");
        }));
        REQUIRE(finishedAt(1.0));
        REQUIRE(t.model.motorPosition(1) == Approx(1.0).margin(0.011));
        REQUIRE(t.model.getWarning().empty());
    }
    SECTION("Past the target: backs off and approaches again")
    {
        t.model.fakeScale(1)->setErrorMm(0.05);
        t.model.goToPosition(1, 1.0);
        // It goes back below 1 (backing off), then comes up to it again
        REQUIRE(t.runUntil([&] { return t.model.motorPosition(1) < 0.6; }));
        REQUIRE(finishedAt(1.0));
        REQUIRE(t.model.getWarning().empty());
    }
    SECTION("Both directions, after zeroing")
    {
        t.model.goToPosition(1, 2.0);
        REQUIRE(finishedAt(2.0));
        t.model.fakeScale(1)->setErrorMm(0.03);
        t.model.zero(1);
        REQUIRE(t.runUntil([&] { return t.model.scalePositionMm(1).has_value(); }));
        t.model.goToPosition(1, -1.0);
        REQUIRE(finishedAt(-1.0));
        t.model.goToPosition(1, 0.0);
        REQUIRE(finishedAt(0.0));
    }
    SECTION("Space stops it")
    {
        t.model.fakeScale(1)->setErrorMm(-0.05);
        t.model.goToPosition(1, 1.0);
        REQUIRE(t.model.isFinishingOnScale(1));
        t.model.stopAllMotors();
        REQUIRE(!t.model.isFinishingOnScale(1));
    }
    SECTION("Turned off: no correction (just the re-base)")
    {
        t.model.toggleFinishOnScale(1);
        REQUIRE(!t.model.finishesOnScale(1));
        t.model.fakeScale(1)->setErrorMm(-0.05);
        t.model.goToPosition(1, 1.0);
        REQUIRE(!t.model.isFinishingOnScale(1));
        REQUIRE(t.runUntil([&] {
            return !t.model.isMotorRunning(1) && std::abs(t.model.motorPosition(1) - 0.95) < 0.011;
        }));
    }
}

TEST_CASE("UI: finishing on the scale with an approach never goes past the target")
{
    // The table runs 0.05mm ahead of the motor's steps (as on the mill, where
    // the backlash is less than its compensation): a go-to straight there
    // would end past it, e.g. into a pocket's end wall
    const bool approach = GENERATE(false, true);
    INFO((approach ? "With an approach" : "Without"));
    std::map<std::string, std::string> settings { { "Axis1ScaleFinishMoves", "true" } };
    if (approach) {
        settings.insert({ { "Axis1ScaleApproachMm", "0.2" }, { "Axis1ScaleApproachSpeed", "20" } });
    }
    ScaleTestModel t(settings);
    REQUIRE(t.runUntil([&] { return t.model.scaleIsSteady(1); }));
    t.model.fakeScale(1)->setErrorMm(0.05);
    t.model.goToPosition(1, 1.0);
    REQUIRE(t.model.isFinishingOnScale(1));
    double furthest = 0.0;
    bool crept = false;
    REQUIRE(t.runUntil([&] {
        furthest = std::max(furthest, t.model.scalePositionMm(1).value_or(0.0));
        crept = crept || (t.model.isMotorRunning(1) && t.model.motorSpeed(1) == Approx(20.0));
        const auto scale = t.model.scalePositionMm(1);
        return !t.model.isFinishingOnScale(1) && !t.model.isMotorRunning(1) && scale
            && std::abs(*scale - 1.0) < 0.011;
    }));
    REQUIRE(t.model.getWarning().empty());
    if (approach) {
        // Stopped 0.2 short, then crept the rest: never past it
        REQUIRE(crept);
        REQUIRE(furthest < 1.011);
    } else {
        REQUIRE(furthest > 1.04);
    }
    // Either way the speed ends as a stop from it leaves it (Axis1SpeedPreset2,
    // 600, reset to 40 on stopping)
    REQUIRE(t.model.motorSpeed(1) == Approx(40.0));
}

TEST_CASE("UI: a move closer than the approach creeps all the way")
{
    ScaleTestModel t(
        { { "Axis1ScaleFinishMoves", "true" },
          { "Axis1ScaleApproachMm", "0.2" },
          { "Axis1ScaleApproachSpeed", "20" } });
    REQUIRE(t.runUntil([&] { return t.model.scaleIsSteady(1); }));
    t.model.setMotorSpeed(1, 60.0);
    t.model.goToPosition(1, 0.1);
    REQUIRE(t.model.isMotorRunning(1));
    REQUIRE(t.model.motorSpeed(1) == Approx(20.0));
    REQUIRE(
        t.runUntil([&] { return !t.model.isFinishingOnScale(1) && !t.model.isMotorRunning(1); }));
    REQUIRE(*t.model.scalePositionMm(1) == Approx(0.1).margin(0.011));
    // (Back to its speed, which is below the reset)
    REQUIRE(t.model.motorSpeed(1) == Approx(60.0));
}

TEST_CASE("UI: finishing axis 2's moves on its scale")
{
    ScaleTestModel t(withAxis2Scale({ { "Axis2ScaleFinishMoves", "true" } }));
    REQUIRE(t.model.finishesOnScale(2));
    REQUIRE(!t.model.finishesOnScale(1));
    // (Its own on and off in the menu, as for axis 1's)
    const auto commands
        = mgo::buildCommands(t.config, mgo::DisplayUnits::Metric, false, false, true);
    const auto toggle = std::ranges::find_if(
        commands, [](const auto& c) { return c.key == mgo::key::toggleAxis2FinishOnScale; });
    REQUIRE(toggle != commands.end());
    REQUIRE(toggle->description == "Finishing on the scale is ON (X)");
    REQUIRE(t.runUntil([&] { return t.model.scalePositionMm(2).has_value(); }));
    const auto finishedAt = [&t](double target) {
        return t.runUntil([&] {
            const auto scale = t.model.scalePositionMm(2);
            return !t.model.isFinishingOnScale(2) && !t.model.isMotorRunning(2) && scale
                && std::abs(*scale - target) < 0.011;
        });
    };

    SECTION("Short of the target: moves on")
    {
        t.model.fakeScale(2)->setErrorMm(-0.05);
        t.model.goToPosition(2, 1.0);
        REQUIRE(t.model.isFinishingOnScale(2));
        REQUIRE(t.model.anyFinishingOnScale());
        REQUIRE(t.runUntil([&] {
            return !t.model.isMotorRunning(2) && t.model.isFinishingOnScale(2)
                && t.model.axisStatus(2).starts_with("Finishing on the scale");
        }));
        REQUIRE(finishedAt(1.0));
        REQUIRE(t.model.getWarning().empty());
    }
    SECTION("Past the target: backs off and approaches again")
    {
        t.model.fakeScale(2)->setErrorMm(0.05);
        t.model.goToPosition(2, 1.0);
        REQUIRE(t.runUntil([&] { return t.model.motorPosition(2) < 0.6; }));
        REQUIRE(finishedAt(1.0));
        REQUIRE(t.model.getWarning().empty());
    }
    SECTION("Going to a memory slot")
    {
        t.model.storePosition(2, 2.0);
        t.model.fakeScale(2)->setErrorMm(-0.04);
        t.model.goToCurrentMemory(2);
        REQUIRE(t.model.isFinishingOnScale(2));
        REQUIRE(finishedAt(2.0));
    }
    SECTION("Space stops it")
    {
        t.model.fakeScale(2)->setErrorMm(-0.05);
        t.model.goToPosition(2, 1.0);
        REQUIRE(t.model.isFinishingOnScale(2));
        t.model.stopAllMotors();
        REQUIRE(!t.model.anyFinishingOnScale());
    }
}

TEST_CASE("UI: finishing on the scale is turned on and off from the menu")
{
    // It starts as the config says, and Tab, Y (System), F toggles it
    MapConfigReader config { { { "Axis1Scale", "serial" }, { "Axis1ScaleFinishMoves", "true" } } };
    mgo::MockGpio gpio { false };
    mgo::Model model { gpio, config };
    mgo::Controller controller(&model);
    QWidget* w = mainWindow();
    REQUIRE(w);
    REQUIRE(model.finishesOnScale(1));
    press(w, controller, Qt::Key_Tab);
    press(w, controller, Qt::Key_Y);
    for (const bool expected : { false, true }) {
        press(w, controller, Qt::Key_F);
        REQUIRE(controller.isMenuOpen());
        REQUIRE(model.finishesOnScale(1) == expected);
    }
    press(w, controller, Qt::Key_Tab);
    REQUIRE(!controller.isMenuOpen());
}

TEST_CASE("UI: the table carrying on into the slack is allowed for")
{
    // 0.3mm of backlash compensation (300 steps of 0.001mm)
    ScaleTestModel t(
        { { "Axis1ScaleRebase", "true" },
          { "Axis1ScaleWarnMm", "0.2" },
          { "Axis1BacklashCompensationSteps", "300" },
          // (A simulated table with no backlash: see below)
          { "Axis1FakeBacklashSteps", "0" } });
    REQUIRE(t.runUntil([&] { return t.model.scalePositionMm(1).has_value(); }));
    const auto stoppedAt = [&t](double position) {
        return t.runUntil([&] {
            return !t.model.isMotorRunning(1) && !t.model.scaleMismatchMm(1)
                && std::abs(t.model.motorPosition(1) - position) < 0.011;
        });
    };
    // (The simulated table has no backlash, so the first move to the right,
    // a reversal after the start-up take-up, ends 0.3 further right: within
    // what's allowed that way, so re-based. After that, moves to the right
    // aren't reversals)
    t.model.goToPosition(1, 0.5);
    REQUIRE(stoppedAt(0.8));

    // Moving right, the table ends 0.4mm further right than the motor (as
    // after stopping dead from speed): within 0.2 + 0.3, so just re-based.
    // (The error changes while moving: at rest, a change is just that)
    t.model.goToPosition(1, 1.8);
    t.model.fakeScale(1)->setErrorMm(0.4);
    REQUIRE(stoppedAt(2.2));
    // Moving right again, the table ends 0.3mm short (as if stalled): that
    // way, only 0.2 is allowed
    t.model.goToPosition(1, 3.2);
    t.model.fakeScale(1)->setErrorMm(0.1);
    REQUIRE(t.runUntil([&] { return t.model.scaleMismatchMm(1).has_value(); }));
    REQUIRE(*t.model.scaleMismatchMm(1) == Approx(-0.3).margin(0.011));
    REQUIRE(t.model.scaleMismatchLimitMm(1) == Approx(0.2));
}

TEST_CASE("UI: defining a new tool, then changing tool")
{
    const std::string path
        = (std::filesystem::temp_directory_path() / "lc_ui_test_tools.cfg").string();
    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(path + ".bak", ec);
    MapConfigReader config {
        { { "ToolLibrary", "true" }, { "ToolLibraryFile", path }, { "CurrentFeature", "true" } }
    };
    mgo::MockGpio gpio { false };
    mgo::Model model { gpio, config };
    mgo::Controller controller(&model);
    QWidget* w = mainWindow();
    REQUIRE(w);
    const auto snapshotWindow = [&](const std::string& name) {
        if (const char* dir = std::getenv("LC_SNAPSHOT_DIR")) {
            controller.tick();
            QTest::qWait(150);
            w->grab().save(QString::fromStdString(std::string(dir) + "/" + name + ".png"));
        }
    };
    model.setMotorSpeed(1, 500.0);
    model.setMotorSpeed(2, 500.0);
    model.setPosition(2, 10.0);
    model.diameterIsSet();
    model.markWorkEnd();
    const auto moveTo = [&model](double z, double x) {
        model.goToPosition(1, z);
        model.waitForMotor(1);
        model.goToPosition(2, x);
        model.waitForMotor(2);
    };

    // The menu's Tool library, then New, which explains and asks first
    press(w, controller, Qt::Key_Tab);
    press(w, controller, Qt::Key_T);
    snapshotWindow("menu_tools");
    bool explained = false;
    whenDialogShows([&explained](QWidget* dialog) {
        explained = dialogText(dialog).contains("ONLY EXTERNAL TOOLS");
        snapshot(dialog, "tool_new");
        QTest::keyClick(inputWidget(dialog), Qt::Key_Return);
    });
    press(w, controller, Qt::Key_N);
    REQUIRE(explained);
    REQUIRE(model.isMeasuringTool());
    REQUIRE(model.toolMeasurementNumber() == 2);

    // Keys which would change positions are refused meanwhile (Z Z would zero Z)
    press(w, controller, Qt::Key_Z);
    press(w, controller, Qt::Key_Z);
    REQUIRE(model.getWorkEndZ() == Approx(0.0));

    // Tool 1 on the diameter (X in) and the end (Z towards the chuck), each
    // recorded with Enter
    moveTo(0.2, 10.2);
    moveTo(0.2, 10.0);
    press(w, controller, Qt::Key_Return);
    moveTo(0.2, 9.0);
    moveTo(0.0, 9.0);
    press(w, controller, Qt::Key_Return);
    REQUIRE(model.toolTouchStep() == mgo::ToolTouchStep::ToolDiameter);
    snapshotWindow("tool_measuring");
    // Tool 2: 1 further towards the chuck, and 0.5 further in
    moveTo(1.2, 10.7);
    moveTo(1.2, 10.5);
    press(w, controller, Qt::Key_Return);
    moveTo(1.2, 9.0);
    // Then its description and nose radius
    whenDialogShows([](QWidget* dialog) {
        snapshot(dialog, "tool_describe");
        typeInto(dialog, "Left-hand turning tool");
        whenDialogShows([](QWidget* radius) {
            typeInto(radius, "0.2");
            QTest::keyClick(inputWidget(radius), Qt::Key_Return);
        });
        QTest::keyClick(inputWidget(dialog), Qt::Key_Return);
    });
    moveTo(1.0, 9.0);
    press(w, controller, Qt::Key_Return);
    REQUIRE(waitForDialogToClose());
    REQUIRE(!model.isMeasuringTool());
    REQUIRE(model.toolLibrary().current().number == 2);
    REQUIRE(model.toolLibrary().current().description == "Left-hand turning tool");
    REQUIRE(model.toolLibrary().current().noseRadiusMm == Approx(0.2));
    // Tool 2 is on the end, which reads Z 0 again
    REQUIRE(model.motorPosition(1) == Approx(0.0).margin(0.0011));
    snapshotWindow("tool_in_use");

    // T: back to tool 1 (the first in the list)
    whenDialogShows([](QWidget* dialog) {
        snapshot(dialog, "tool_change");
        QTest::keyClick(inputWidget(dialog), Qt::Key_Return);
    });
    press(w, controller, Qt::Key_T);
    REQUIRE(model.toolLibrary().current().number == 1);
    REQUIRE(model.motorPosition(1) == Approx(1.0).margin(0.0011));

    // T: the unknown tool (last in the list). Nothing shifts
    whenDialogShows([](QWidget* dialog) {
        key(dialog, Qt::Key_Down, 2);
        QTest::keyClick(inputWidget(dialog), Qt::Key_Return);
    });
    press(w, controller, Qt::Key_T);
    REQUIRE(!model.toolLibrary().currentIsKnown());
    REQUIRE(model.motorPosition(1) == Approx(1.0).margin(0.0011));
    snapshotWindow("tool_unknown");

    std::filesystem::remove(path, ec);
    std::filesystem::remove(path + ".bak", ec);
}
