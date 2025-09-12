#include "view_sfml.h"

#include "keycodes.h"
#include "model.h"
#include "sfml_dialog.h"
#include "threadpitches.h"

#include <cassert>
#include <iomanip>
#include <sstream>

#include <fmt/format.h>

namespace mgo {

namespace {

std::string formatMotorPosition(double mm)
{
    if (std::abs(mm) < 0.001) {
        mm = 0.0;
    }
    return fmt::format("{: >8.2f}", mm);
}

int convertKeyCode(sf::Event event)
{
    const auto k = event.getIf<sf::Event::KeyPressed>();
    if (!k) {
        return -1;
    }
    sf::Keyboard::Key sfEnumKey = event.getIf<sf::Event::KeyPressed>()->code;
    int sfKey = static_cast<int>(sfEnumKey);
    // Letters
    if (sfKey >= static_cast<int>(sf::Keyboard::Key::A)
        && sfKey <= static_cast<int>(sf::Keyboard::Key::Z)) {
        if (k->control) {
            // Ctrl-letter has 0x10000 ORed to it
            return (key::a + sfKey) | 0x10000;
        }
        if (k->alt) {
            // Alt-letter has 0x100000 ORed to it
            return (key::a + sfKey) | 0x100000;
        }
        // A is defined in SFML's enum as zero
        if (k->shift) {
            return key::A + sfKey;
        }
        return key::a + sfKey;
    }
    // Number keys
    if (sfKey >= static_cast<int>(sf::Keyboard::Key::Num0)
        && sfKey <= static_cast<int>(sf::Keyboard::Key::Num9)) {
        // Check for shift-8 (asterisk)
        if (sfKey == static_cast<int>(sf::Keyboard::Key::Num8) && k->shift) {
            return key::ASTERISK;
        }
        return 22 + sfKey;
    }
    // Function keys
    if (sfKey >= static_cast<int>(sf::Keyboard::Key::F1)
        && sfKey <= static_cast<int>(sf::Keyboard::Key::F15)) {
        // F1 = 85 for SFML
        return 180 + sfKey;
    }
    // Misc
    switch (sfKey) {
        case static_cast<int>(sf::Keyboard::Key::Enter):
            return key::ENTER;
        case static_cast<int>(sf::Keyboard::Key::LBracket): // [
            return key::LBRACKET;
        case static_cast<int>(sf::Keyboard::Key::RBracket): // ]
            return key::RBRACKET;
        case static_cast<int>(sf::Keyboard::Key::Comma):
            return key::COMMA;
        case static_cast<int>(sf::Keyboard::Key::Period):
            return key::FULLSTOP;
        case static_cast<int>(sf::Keyboard::Key::Semicolon):
            if (k->shift) {
                return key::COLON;
            }
            break;
        case static_cast<int>(sf::Keyboard::Key::Right):
            if (k->alt || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RAlt)) {
                return key::AltRight;
            }
            return key::RIGHT;
        case static_cast<int>(sf::Keyboard::Key::Left):
            if (k->alt || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RAlt)) {
                return key::AltLeft;
            }
            return key::LEFT;
        case static_cast<int>(sf::Keyboard::Key::Up):
            if (k->alt || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RAlt)) {
                return key::AltUp;
            }
            return key::UP;
        case static_cast<int>(sf::Keyboard::Key::Down):
            if (k->alt || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RAlt)) {
                return key::AltDown;
            }
            return key::DOWN;
        case static_cast<int>(sf::Keyboard::Key::Backslash):
            if (k->shift) {
                return key::PIPE;
            }
            return key::BACKSLASH;
        case static_cast<int>(sf::Keyboard::Key::Hyphen):
            return key::MINUS;
        case static_cast<int>(sf::Keyboard::Key::Equal):
            return key::EQUALS;
        case static_cast<int>(sf::Keyboard::Key::Space):
            return key::SPACE;
        case static_cast<int>(sf::Keyboard::Key::Escape):
            return key::ESC;
        case static_cast<int>(sf::Keyboard::Key::Backspace):
            return key::BACKSPACE;
        case static_cast<int>(sf::Keyboard::Key::Delete):
            return key::DELETE;
        default:
            return -1;
    }
    return -1;
}

} // end anonymous namespace

void ViewSfml::initialise(const Model& model)
{
#ifdef FAKE
    // run in a window in "fake" mode for manual testing
    m_window
        = std::make_unique<sf::RenderWindow>(sf::VideoMode({ 1280, 720 }, 32), "Lathe Control");
    m_window->setPosition({ 50, 50 });
#else
    m_window = std::make_unique<sf::RenderWindow>(
        sf::VideoMode::getDesktopMode(),
        "Lathe Control",
        sf::Style::Default,
        sf::State::Fullscreen);
#endif
    MGOLOG(
        fmt::format(
            "Desktop mode is {} x {}",
            sf::VideoMode::getDesktopMode().size.x,
            sf::VideoMode::getDesktopMode().size.y));
    m_window->setKeyRepeatEnabled(false);
    m_window->setMouseCursorVisible(false);
    m_font = std::make_unique<sf::Font>();
    if (!m_font->openFromFile("./lc_font.ttf")) {
        throw std::runtime_error("Could not load TTF font lc_font.ttf");
    }

    m_txtAxis1Label = std::make_unique<sf::Text>(*m_font, "", 60);
    m_txtAxis1Label->setPosition({ 20, 10 });
    m_txtAxis1Label->setFillColor({ 0, 127, 0 });
    m_txtAxis1Label->setString(model.config().read("Axis1Label", "Z") + ":");

    m_txtAxis1Pos = std::make_unique<sf::Text>(*m_font, "", 60);
    m_txtAxis1Pos->setPosition({ 110, 10 });
    m_txtAxis1Pos->setFillColor(sf::Color::Green);

    m_txtAxis1Units = std::make_unique<sf::Text>(*m_font, "", 30);
    m_txtAxis1Units->setPosition({ 430, 40 });
    m_txtAxis1Units->setFillColor({ 0, 127, 0 });
    m_txtAxis1Units->setString(model.config().read("Axis1DisplayUnits", "mm"));

    m_txtAxis1Speed = std::make_unique<sf::Text>(*m_font, "", 30);
    m_txtAxis1Speed->setPosition({ 550, 40 });
    m_txtAxis1Speed->setFillColor({ 209, 209, 50 });

    m_txtAxis2Label = std::make_unique<sf::Text>(*m_font, "", 60);
    m_txtAxis2Label->setPosition({ 20, 70 });
    m_txtAxis2Label->setFillColor({ 0, 127, 0 });
    m_txtAxis2Label->setString(model.config().read("Axis2Label", "X") + ":");

    m_txtAxis2Pos = std::make_unique<sf::Text>(*m_font, "", 60);
    m_txtAxis2Pos->setPosition({ 110, 70 });
    m_txtAxis2Pos->setFillColor(sf::Color::Green);

    m_txtAxis2Units = std::make_unique<sf::Text>(*m_font, "", 30);
    m_txtAxis2Units->setPosition({ 430, 100 });
    m_txtAxis2Units->setFillColor({ 0, 127, 0 });
    m_txtAxis2Units->setString(model.config().read("Axis1DisplayUnits", "mm"));

    m_txtAxis2Speed = std::make_unique<sf::Text>(*m_font, "", 30);
    m_txtAxis2Speed->setPosition({ 550, 100 });
    m_txtAxis2Speed->setFillColor({ 209, 209, 50 });

    m_txtRpmLabel = std::make_unique<sf::Text>(*m_font, "", 60);
    m_txtRpmLabel->setPosition({ 20, 130 });
    m_txtRpmLabel->setFillColor({ 0, 127, 0 });
    m_txtRpmLabel->setString("R:");

    m_txtRpm = std::make_unique<sf::Text>(*m_font, "", 60);
    m_txtRpm->setPosition({ 150, 130 });
    m_txtRpm->setFillColor(sf::Color::Green);

    m_txtRpmUnits = std::make_unique<sf::Text>(*m_font, "", 30);
    m_txtRpmUnits->setPosition({ 430, 160 });
    m_txtRpmUnits->setFillColor({ 0, 127, 0 });
    m_txtRpmUnits->setString("rpm");

    // Memory banks
    constexpr int MEMORY_Y = 250;
    for (unsigned n = 0; n < model.getMemorySize(); ++n) {
        auto lbl = std::make_unique<sf::Text>(*m_font, "", 30);
        lbl->setPosition({ 60.f + n * 180.f, MEMORY_Y });
        lbl->setFillColor({ 128, 128, 128 });
        lbl->setString(fmt::format("    Mem {}", n + 1));
        m_txtMemoryLabel.push_back(std::move(lbl));
        auto valZ = std::make_unique<sf::Text>(*m_font, "", 30);
        valZ->setPosition({ 60.f + n * 180.f, MEMORY_Y + 25 });
        valZ->setFillColor({ 128, 128, 128 });
        m_txtAxis1MemoryValue.push_back(std::move(valZ));
        auto valX = std::make_unique<sf::Text>(*m_font, "", 30);
        valX->setPosition({ 60.f + n * 180.f, MEMORY_Y + 55 });
        valX->setFillColor({ 128, 128, 128 });
        m_txtAxis2MemoryValue.push_back(std::move(valX));
    }
    std::string axis1Label = model.config().read("Axis1Label", "Z") + ":";
    m_txtAxis1MemoryLabel = std::make_unique<sf::Text>(*m_font, axis1Label, 30);
    m_txtAxis1MemoryLabel->setPosition({ 24.f, MEMORY_Y + 25 });
    m_txtAxis1MemoryLabel->setFillColor({ 128, 128, 128 });
    std::string axis2Label = model.config().read("Axis2Label", "X") + ":";
    m_txtAxis2MemoryLabel = std::make_unique<sf::Text>(*m_font, axis2Label, 30);
    m_txtAxis2MemoryLabel->setPosition({ 24.f, MEMORY_Y + 55 });
    m_txtAxis2MemoryLabel->setFillColor({ 128, 128, 128 });

    // STATUS BAR (at bottom of screen)
    constexpr int STATUS_BAR_Y = 670;
    m_txtGeneralStatus = std::make_unique<sf::Text>(*m_font, "", 20);
    m_txtGeneralStatus->setPosition({ 20, STATUS_BAR_Y });
    m_txtGeneralStatus->setFillColor(sf::Color::Green);

    m_txtAxis1Status = std::make_unique<sf::Text>(*m_font, "", 20);
    m_txtAxis1Status->setPosition({ 350, STATUS_BAR_Y });
    m_txtAxis1Status->setFillColor(sf::Color::Green);

    m_txtAxis2Status = std::make_unique<sf::Text>(*m_font, "", 20);
    m_txtAxis2Status->setPosition({ 550, STATUS_BAR_Y });
    m_txtAxis2Status->setFillColor(sf::Color::Green);

    m_txtChuckRpm = std::make_unique<sf::Text>(*m_font, "", 20);
    m_txtChuckRpm->setPosition({ 750, STATUS_BAR_Y });
    m_txtChuckRpm->setFillColor(sf::Color::Green);

    m_txtLeaderNotifier = std::make_unique<sf::Text>(*m_font, "", 20);
    m_txtLeaderNotifier->setPosition({ 1000, STATUS_BAR_Y });
    m_txtLeaderNotifier->setFillColor(sf::Color::Green);

    // Miscellaneous - this is the bank of text which contains, for example,
    // the help, or text for, say, threading
    constexpr int MISC_Y = 380;

    m_txtMode = std::make_unique<sf::Text>(*m_font, "", 25);
    m_txtMode->setPosition({ 20, MISC_Y });
    m_txtMode->setFillColor(sf::Color::Yellow);

    m_txtMisc1 = std::make_unique<sf::Text>(*m_font, "", 25);
    m_txtMisc1->setPosition({ 20, MISC_Y + 30 });
    m_txtMisc1->setFillColor(sf::Color::White);

    m_txtMisc2 = std::make_unique<sf::Text>(*m_font, "", 25);
    m_txtMisc2->setPosition({ 20, MISC_Y + 60 });
    m_txtMisc2->setFillColor(sf::Color::White);

    m_txtMisc3 = std::make_unique<sf::Text>(*m_font, "", 25);
    m_txtMisc3->setPosition({ 20, MISC_Y + 90 });
    m_txtMisc3->setFillColor(sf::Color::White);

    m_txtMisc4 = std::make_unique<sf::Text>(*m_font, "", 25);
    m_txtMisc4->setPosition({ 20, MISC_Y + 120 });
    m_txtMisc4->setFillColor(sf::Color::White);

    m_txtMisc5 = std::make_unique<sf::Text>(*m_font, "", 25);
    m_txtMisc5->setPosition({ 20, MISC_Y + 150 });
    m_txtMisc5->setFillColor(sf::Color::White);

    m_txtWarning = std::make_unique<sf::Text>(*m_font, "", 20);
    m_txtWarning->setPosition({ 20, MISC_Y + 200 });
    m_txtWarning->setFillColor(sf::Color::Red);

    m_txtTaperOrRadius = std::make_unique<sf::Text>(*m_font, "", 20);
    m_txtTaperOrRadius->setPosition({ 860, 75 });
    m_txtTaperOrRadius->setFillColor(sf::Color::Red);

    m_txtNotification = std::make_unique<sf::Text>(*m_font, "", 25);
    m_txtNotification->setPosition({ 860, 45 });
    m_txtNotification->setFillColor(sf::Color::Red);

    m_txtXRetracted = std::make_unique<sf::Text>(*m_font, "", 25);
    m_txtXRetracted->setPosition({ 860, 105 });
    m_txtXRetracted->setFillColor(sf::Color::Red);
    m_txtXRetracted->setString("RETRACTED");

    m_txtXRetractDirection = std::make_unique<sf::Text>(*m_font, "", 25);
    m_txtXRetractDirection->setPosition({ 860, 165 });
    m_txtXRetractDirection->setFillColor(sf::Color::Red);
    m_txtXRetractDirection->setString("-X RTRCT");

    m_txtAxis1LinearScalePos = std::make_unique<sf::Text>(*m_font, "", 20);
    m_txtAxis1LinearScalePos->setPosition({ 550, 170 });
    m_txtAxis1LinearScalePos->setFillColor({ 209, 209, 50 });
}

void ViewSfml::close()
{
    m_window->close();
}

int ViewSfml::getInput()
{
    static sf::Keyboard::Key lastKey = sf::Keyboard::Key::F15;
    static sf::Clock clock;
    clock.start();
    static auto lastTime = clock.getElapsedTime();
    // This loop is to quickly discard anything that's not a keypress
    // e.g. mouse movement, which we're not interested in (currently)
    std::optional<sf::Event> event;
    for (;;) {
        event = m_window->pollEvent();
        if (!event) {
            // No events in the queue
            return key::None;
        }
        if (event->is<sf::Event::KeyReleased>()) {
            // quick check for rapid cancellation
            // TODO this fails if the alt key is released first
            auto e = event->getIf<sf::Event::KeyReleased>();
            if ((e->code == sf::Keyboard::Key::Left || e->code == sf::Keyboard::Key::Right
                 || e->code == sf::Keyboard::Key::Up || e->code == sf::Keyboard::Key::Down)
                && (e->alt || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RAlt))) {
                return key::SPACE;
            }
        }
        if (event->is<sf::Event::JoystickButtonPressed>()) {
            auto key = processJoystickButton(*event);
            if (key != key::None) {
                return key;
            }
        }
        if (sf::Joystick::isConnected(0)) {
            // If an analogue stick is not centred, we return the appropriate key
            auto key = getJoystickState();
            if (key != key::None) {
                return key;
            }
        }
        if (event->is<sf::Event::KeyPressed>()) {
            break;
        }
    }

    // We only get here if a key was pressed
    if (lastKey == event->getIf<sf::Event::KeyPressed>()->code
        && clock.getElapsedTime().asMilliseconds() - lastTime.asMilliseconds() < 100) {
        // Debounce
        return -1;
    }
    lastKey = event->getIf<sf::Event::KeyPressed>()->code;
    lastTime = clock.getElapsedTime();
    return convertKeyCode(*event);
}

std::string ViewSfml::getTextInput(const std::string& prompt, const std::string& defaultEntry)
{
    return getInputFromDialog(*m_window, *m_font, prompt, defaultEntry);
}

double ViewSfml::getNumericInput(const std::string& prompt, double defaultEntry)
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << defaultEntry;
    const std::string defaultValueString = oss.str();
    std::string result;
    try {
        result = getInputFromDialog(
            *m_window, *m_font, prompt, defaultValueString, InputType::numeric);
        return std::stod(result);
    } catch (const std::exception& e) {
        MGOLOG("Error converting string \"" + result + "\" to double");
        return defaultEntry;
    }
}

void ViewSfml::updateDisplay(const Model& model)
{
    m_window->clear();
    updateTextFromModel(model);

    if (!model.isShuttingDown()) {
        if (!model.config().readBool("DisableAxis1", false)) {
            m_window->draw(*m_txtAxis1Label);
            m_window->draw(*m_txtAxis1Pos);
            m_window->draw(*m_txtAxis1Units);
            m_window->draw(*m_txtAxis1Speed);
            m_window->draw(*m_txtAxis1Status);
            m_window->draw(*m_txtAxis1MemoryLabel);
            m_window->draw(*m_txtAxis1LinearScalePos);
        }
        if (!model.config().readBool("DisableAxis2", false)) {
            m_window->draw(*m_txtAxis2Label);
            m_window->draw(*m_txtAxis2Pos);
            m_window->draw(*m_txtAxis2Units);
            m_window->draw(*m_txtAxis2Speed);
            m_window->draw(*m_txtAxis2Status);
            m_window->draw(*m_txtAxis2MemoryLabel);
        }
        if (!model.config().readBool("DisableRpm", false)) {
            m_window->draw(*m_txtRpmLabel);
            m_window->draw(*m_txtRpm);
            m_window->draw(*m_txtRpmUnits);
        }
        m_window->draw(*m_txtLeaderNotifier);
        m_window->draw(*m_txtChuckRpm);
        m_window->draw(*m_txtGeneralStatus);
        m_window->draw(*m_txtWarning);
        m_window->draw(*m_txtNotification);
        if (model.getEnabledFunction() == Mode::Taper
            || model.getEnabledFunction() == Mode::Radius) {
            m_window->draw(*m_txtTaperOrRadius);
        }
        if (model.getRetractionDirection() == XDirection::Inwards) {
            m_window->draw(*m_txtXRetractDirection);
        }
        if (model.getIsAxis2Retracted()) {
            m_window->draw(*m_txtXRetracted);
        }
        for (std::size_t n = 0; n < m_txtMemoryLabel.size(); ++n) {
            m_window->draw(*m_txtMemoryLabel.at(n));
            if (!model.config().readBool("DisableAxis1", false)) {
                m_window->draw(*m_txtAxis1MemoryValue.at(n));
            }
            if (!model.config().readBool("DisableAxis2", false)) {
                m_window->draw(*m_txtAxis2MemoryValue.at(n));
            }
        }
        // Z/X labels - make red if keyMode corresponds
        if (model.getKeyMode() == KeyMode::Axis1 || model.getKeyMode() == KeyMode::AxisAll) {
            m_txtAxis1MemoryLabel->setFillColor(sf::Color::Red);
        } else {
            m_txtAxis1MemoryLabel->setFillColor({ 128, 128, 128 });
        }
        if (model.getKeyMode() == KeyMode::Axis2 || model.getKeyMode() == KeyMode::AxisAll) {
            m_txtAxis2MemoryLabel->setFillColor(sf::Color::Red);
        } else {
            m_txtAxis2MemoryLabel->setFillColor({ 128, 128, 128 });
        }
        if (model.getCurrentDisplayMode() != Mode::None) {
            m_window->draw(*m_txtMode);
        }
        m_window->draw(*m_txtMisc1);
        m_window->draw(*m_txtMisc2);
        m_window->draw(*m_txtMisc3);
        m_window->draw(*m_txtMisc4);
        m_window->draw(*m_txtMisc5);
    }
    m_window->display();
}

void ViewSfml::updateTextFromModel(const Model& model)
{
    // Updates all the text objects with data in the model
    if (model.isShuttingDown()) {
        m_txtAxis1Pos->setString("SHUTTING DOWN");
        return;
    }

    m_txtAxis1Pos->setString(formatMotorPosition(model.getAxis1MotorPosition()));

    m_txtAxis1Speed->setString(fmt::format("{:<.2f} mm/min", model.getAxis1MotorSpeed()));

    if (!model.getIsAxis2Retracted()) {
        m_txtAxis2Pos->setString(formatMotorPosition(model.getAxis2MotorPosition()));
    } else {
        m_txtAxis2Pos->setString("     ---");
    }
    m_txtAxis2Speed->setString(fmt::format("{:<.2f} mm/min", model.getAxis2MotorSpeed()));
    m_txtRpm->setString(fmt::format("{: >7}", static_cast<int>(model.getRotaryEncoderRpm())));

    m_txtGeneralStatus->setString(model.getGeneralStatus());
    std::string status
        = fmt::format("{}: {}", model.config().read("Axis1Label", "Z"), model.getAxis1Status());
    m_txtAxis1Status->setString(status);
    status = fmt::format("{}: {}", model.config().read("Axis2Label", "X"), model.getAxis2Status());
    m_txtAxis2Status->setString(status);
    if (model.getEnabledFunction() == Mode::Taper) {
        m_txtTaperOrRadius->setString(fmt::format("Angle: {}", model.getTaperAngle()));
    } else if (model.getEnabledFunction() == Mode::Radius) {
        m_txtTaperOrRadius->setString(fmt::format("Radius: {}", model.getRadius()));
    }
    if (model.getKeyMode() == KeyMode::Function) {
        m_txtLeaderNotifier->setString(": (select function)");
    } else {
        m_txtLeaderNotifier->setString("");
    }
    m_txtChuckRpm->setString(fmt::format("C: {: >5.1f} deg", model.getChuckAngle()));

    m_txtAxis1LinearScalePos->setString(
        fmt::format(
            "{} Scale: {:<.3f} mm",
            model.config().read("Axis1Label", "Z"),
            model.getAxis1LinearScalePosMm()));

    for (std::size_t n = 0; n < m_txtMemoryLabel.size(); ++n) {
        if (model.getCurrentMemorySlot() == n) {
            m_txtMemoryLabel.at(n)->setFillColor({ 255, 255, 255 });
            m_txtAxis1MemoryValue.at(n)->setFillColor({ 255, 255, 255 });
            m_txtAxis2MemoryValue.at(n)->setFillColor({ 255, 255, 255 });
        } else {
            m_txtMemoryLabel.at(n)->setFillColor({ 100, 100, 100 });
            m_txtAxis1MemoryValue.at(n)->setFillColor({ 100, 100, 100 });
            m_txtAxis2MemoryValue.at(n)->setFillColor({ 100, 100, 100 });
        }
        if (model.getAxis1Memory(n) == INF_RIGHT) {
            m_txtAxis1MemoryValue.at(n)->setString("        -");
        } else {
            m_txtAxis1MemoryValue.at(n)->setString(
                fmt::format("{: >9}", model.convertAxis1StepToPosition(model.getAxis1Memory(n))));
        }
        if (model.getAxis2Memory(n) == INF_OUT) {
            m_txtAxis2MemoryValue.at(n)->setString("        -");
        } else {
            m_txtAxis2MemoryValue.at(n)->setString(
                fmt::format("{: >9}", model.convertAxis2StepToPosition(model.getAxis2Memory(n))));
        }
    }

    switch (model.getEnabledFunction()) {
        case Mode::Threading:
            m_txtNotification->setString("THREADING");
            break;
        case Mode::Taper:
            m_txtNotification->setString("TAPERING");
            break;
        case Mode::Radius:
            m_txtNotification->setString("RADIUS");
            break;
        case Mode::MultiPass:
            m_txtNotification->setString("MULTI-PASS");
            break;
        default:
            m_txtNotification->setString("");
    }

    switch (model.getCurrentDisplayMode()) {
        case Mode::Help:
            {
                m_txtMode->setString("Help");
                m_txtMisc1->setString("Modes: (F2=Leader) s=Setup t=Thread p=taPer r=Retract");
                m_txtMisc2->setString("");
                m_txtMisc3->setString("Z axis speed: 1-5, X axis speed: 6-0");
                m_txtMisc4->setString("[ and ] select mem to use. M store, Enter return (F fast).");
                m_txtMisc5->setString("WASD = nudge 0.025mm. Space to stop all motors. R retract.");
                m_txtWarning->setString("Press Esc to exit help");
                break;
            }
        case Mode::Setup:
            {
                m_txtMode->setString("Setup");
                m_txtMisc1->setString("This mode allows you to determine backlash compensation");
                m_txtMisc2->setString(
                    "Use a dial indicator to find number of steps "
                    "backlash per axis");
                m_txtMisc3->setString(
                    "REMEMBER to unset any previous-set backlash figures in "
                    "config!");
                m_txtMisc4->setString("");
                m_txtMisc5->setString(
                    fmt::format(
                        "Z step: {}   X step: {}",
                        model.getAxis1MotorCurrentStep(),
                        model.getAxis2MotorCurrentStep()));
                m_txtWarning->setString("Press Esc to exit setup");
                break;
            }
        case Mode::Taper:
            {
                m_txtMode->setString("Taper");
                m_txtMisc1->setString(
                    fmt::format("Taper angle (degrees from centre): {}_", model.getInputString()));
                m_txtMisc2->setString("");
                m_txtMisc3->setString("MT1 = -1.4287, MT2 = -1.4307, MT3 = -1.4377, MT4 = -1.4876");
                m_txtMisc4->setString("(negative angle means piece gets wider towards chuck)");
                m_txtMisc5->setString("");
                m_txtWarning->setString("Enter to keep enabled, Esc to disable, Del to clear");
                break;
            }
        case Mode::Radius:
            {
                m_txtMode->setString("Radius");
                m_txtMisc1->setString(fmt::format("Radius required: {}_", model.getInputString()));
                m_txtMisc2->setString(
                    "Important! Ensure the tool is at the radius of the workpiece,");
                m_txtMisc3->setString(
                    "near the end, and set axes to ZERO (this drives the operation).");
                m_txtMisc4->setString(
                    "Then cut OUTWARDS, and move INWARDS gradually for subsequent cuts.");
                m_txtMisc5->setString("Re-zero each time and use relative movement for each cut.");
                m_txtWarning->setString("Enter to keep enabled, Esc to disable, Del to clear");
                break;
            }
        case Mode::Threading:
            {
                updateThreadData(model);
                m_txtMisc4->setString("");
                m_txtMisc5->setString("Press Up/Down to change.");
                m_txtWarning->setString("Enter to keep enabled, Esc to disable");
                break;
            }
        case Mode::Axis2RetractSetup:
            {
                m_txtMode->setString("X Axis retraction mode");
                m_txtMisc1->setString("Normal X retraction is 2mm outwards");
                m_txtMisc2->setString("In a boring operation, retraction should be INWARDS.");
                m_txtMisc3->setString("Current setting: ");
                if (model.getRetractionDirection() == XDirection::Inwards) {
                    m_txtMisc4->setString("Inwards (i.e. away from you, for boring)");
                } else {
                    m_txtMisc4->setString("Normal (i.e. towards you)");
                }
                m_txtMisc5->setString("(Press up / down to change)");
                m_txtWarning->setString("Enter to close screen");
                break;
            }
        case Mode::Axis2PositionSetup:
            {
                m_txtMode->setString(
                    fmt::format("{} Position Set", model.config().read("Axis2Label", "X")));
                m_txtMisc1->setString("");
                m_txtMisc2->setString(
                    fmt::format(
                        "Specify a value for {} here", model.config().read("Axis2Label", "X")));
                m_txtMisc3->setString("");
                m_txtMisc4->setString("");
                m_txtMisc5->setString(
                    fmt::format(
                        "Current {} position: {}_",
                        model.config().read("Axis2Label", "X"),
                        model.getInputString()));
                m_txtWarning->setString("Enter to set, 'D' to enter as diameter, Esc to cancel");
                break;
            }
        case Mode::Axis1PositionSetup:
            {
                m_txtMode->setString(
                    fmt::format("{} Position Set", model.config().read("Axis1Label", "Z")));
                m_txtMisc1->setString("");
                m_txtMisc2->setString(
                    fmt::format(
                        "Specify a value for {} here", model.config().read("Axis1Label", "Z")));
                m_txtMisc3->setString("");
                m_txtMisc4->setString("");
                m_txtMisc5->setString(
                    fmt::format(
                        "Current {} position: {}_",
                        model.config().read("Axis1Label", "Z"),
                        model.getInputString()));
                m_txtWarning->setString("Enter to set, Esc to cancel");
                break;
            }
        case Mode::Axis1GoTo:
            {
                m_txtMode->setString(
                    fmt::format(
                        "Go To {} Absolute Position ", model.config().read("Axis1Label", "Z")));
                m_txtMisc1->setString("");
                m_txtMisc2->setString("Specify a value");
                m_txtMisc3->setString("");
                m_txtMisc4->setString("");
                m_txtMisc5->setString(fmt::format("Position: {}_", model.getInputString()));
                m_txtWarning->setString("Enter to set, Esc to cancel");
                break;
            }
        case Mode::Axis2GoTo:
            {
                m_txtMode->setString(
                    fmt::format(
                        "Go To {} Absolute Position ", model.config().read("Axis2Label", "Z")));
                m_txtMisc1->setString("");
                m_txtMisc2->setString("Specify a value");
                m_txtMisc3->setString("");
                m_txtMisc4->setString("");
                m_txtMisc5->setString(fmt::format("Position: {}_", model.getInputString()));
                m_txtWarning->setString("Enter to set, Esc to cancel");
                break;
            }
        case Mode::Axis1GoToOffset:
            {
                m_txtMode->setString(
                    fmt::format(
                        "Go To {} Relative Position ", model.config().read("Axis1Label", "Z")));
                m_txtMisc1->setString("");
                m_txtMisc2->setString("Specify a RELATIVE value");
                m_txtMisc3->setString("");
                m_txtMisc4->setString("");
                m_txtMisc5->setString(fmt::format("Offset: {}_", model.getInputString()));
                m_txtWarning->setString("Enter to set, Esc to cancel");
                break;
            }
        case Mode::Axis2GoToOffset:
            {
                m_txtMode->setString(
                    fmt::format(
                        "Go To {} Relative Position ", model.config().read("Axis2Label", "Z")));
                m_txtMisc1->setString("");
                m_txtMisc2->setString("Specify a RELATIVE value");
                m_txtMisc3->setString("");
                m_txtMisc4->setString("");
                m_txtMisc5->setString(fmt::format("Offset: {}_", model.getInputString()));
                m_txtWarning->setString("Enter to set, Esc to cancel");
                break;
            }
        case Mode::MultiPass:
            {
                m_txtMode->setString(
                    fmt::format(
                        "{} axis step-over per pass ", model.config().read("Axis2Label", "Z")));
                m_txtMisc1->setString("");
                m_txtMisc2->setString(
                    "This mode will automatically move from m1.x,m1.y to m2.x,m2.y");
                m_txtMisc3->setString("with the step-over value you specify below:");
                m_txtMisc4->setString("");
                m_txtMisc5->setString(fmt::format("Step-over (mm): {}_", model.getInputString()));
                m_txtWarning->setString("Enter to set, Esc to cancel");
                break;
            }
        case Mode::None:
            {
                m_txtMode->setString("");
                m_txtMisc1->setString("");
                m_txtMisc2->setString("");
                m_txtMisc3->setString("");
                m_txtMisc4->setString("");
                m_txtMisc5->setString("");
                m_txtWarning->setString(model.getWarning());
                if (!model.getWarning().empty()) {
                    if (++m_iteration % 6 < 3) {
                        m_txtWarning->setFillColor(sf::Color::Red);
                    } else {
                        m_txtWarning->setFillColor(sf::Color::Yellow);
                    }
                }
                break;
            }
        default:
            {
                assert(false);
            }
    }
    // Finally keep threading data on screen if in threading mode
    if (model.getEnabledFunction() == Mode::Threading) {
        updateThreadData(model);
    }
}

void ViewSfml::updateThreadData(const mgo::Model& model)
{
    m_txtMode->setString("Thread");
    ThreadPitch tp = threadPitches.at(model.getCurrentThreadPitchIndex());
    m_txtMisc1->setString(fmt::format("Thread required: {}", tp.name));
    m_txtMisc2->setString(
        fmt::format(
            "Male   OD: {} mm, cut: {} mm, pitch: {} mm", tp.maleOd, tp.cutDepthMale, tp.pitchMm));
    m_txtMisc3->setString(
        fmt::format(
            "Female ID: {} mm, cut: {} mm, pitch {} mm",
            tp.femaleId,
            tp.cutDepthFemale,
            tp.pitchMm));
}

int ViewSfml::processJoystickButton(const sf::Event& e)
{
    auto event = e.getIf<sf::Event::JoystickButtonPressed>();
    if (!event) {
        return key::None;
    }
    switch (event->button) {
        case 0:
            // big nudge
            return key::AltW;
        case 1:
            // big nudge
            return key::AltD;
        case 2:
            // big nudge
            return key::AltS;
        case 3:
            // big nudge
            return key::AltA;
        case 4:
            return key::LBRACKET;
        case 5:
            return key::SPACE;
        case 6:
            return key::RBRACKET;
        case 7:
            return key::ENTER;
        case 8:
            return key::f;
        case 9:
            // This acts as a modifier, so we ignore this event here
            break;
        case 10:
            break;
        case 11:
            break;
        case 12:
            return key::m;
    }
    return key::None;
}

int ViewSfml::getJoystickState()
{
    constexpr int JOYSTICK_SHIFT = 9;
    // Small Nudge via D-pad
    if (sf::Joystick::hasAxis(0, sf::Joystick::Axis::PovX)) {
        constexpr float deadzone = 50.f;
        float value = sf::Joystick::getAxisPosition(0, sf::Joystick::Axis::PovX);
        if (value > deadzone) {
            if (sf::Joystick::isButtonPressed(0, JOYSTICK_SHIFT)) {
                return key::D;
            }
            return key::d;
        }
        if (value < -deadzone) {
            if (sf::Joystick::isButtonPressed(0, JOYSTICK_SHIFT)) {
                return key::A;
            }
            return key::a;
        }
    }
    if (sf::Joystick::hasAxis(0, sf::Joystick::Axis::PovY)) {
        constexpr float deadzone = 40.f;
        float value = sf::Joystick::getAxisPosition(0, sf::Joystick::Axis::PovY);
        if (value > deadzone) {
            if (sf::Joystick::isButtonPressed(0, JOYSTICK_SHIFT)) {
                return key::S;
            }
            return key::s;
        }
        if (value < -deadzone) {
            if (sf::Joystick::isButtonPressed(0, JOYSTICK_SHIFT)) {
                return key::W;
            }
            return key::w;
        }
    }
    // Normal move via left stick.
    static float previousPovXValue = 0.f;
    static float previousPovYValue = 0.f;
    if (sf::Joystick::hasAxis(0, sf::Joystick::Axis::X)) {
        constexpr float deadzone = 99.f;
        float value = sf::Joystick::getAxisPosition(0, sf::Joystick::Axis::X);
        if (!(previousPovXValue > deadzone && value > deadzone)
            && !(previousPovXValue < -deadzone && value < -deadzone)) {
            previousPovXValue = value;
            if (value > deadzone) {
                return key::js_right;
            }
            if (value < -deadzone) {
                return key::js_left;
            }
        }
    }
    if (sf::Joystick::hasAxis(0, sf::Joystick::Axis::Y)) {
        constexpr float deadzone = 99.f;
        float value = sf::Joystick::getAxisPosition(0, sf::Joystick::Axis::Y);
        if (!(previousPovYValue > deadzone && value > deadzone)
            && !(previousPovYValue < -deadzone && value < -deadzone)) {
            previousPovYValue = value;
            if (value > deadzone) {
                // "shift" plus down = retract
                if (sf::Joystick::isButtonPressed(0, JOYSTICK_SHIFT)) {
                    return key::r;
                }
                return key::js_down;
            }
            if (value < -deadzone) {
                return key::js_up;
            }
        }
    }
    // Rapid motion via Right stick.
    static float previousZValue = 0.f;
    static float previousRValue = 0.f;
    if (sf::Joystick::hasAxis(0, sf::Joystick::Axis::Z)) {
        constexpr float deadzone = 99.f;
        float value = sf::Joystick::getAxisPosition(0, sf::Joystick::Axis::Z);
        if (std::abs(previousZValue) > deadzone && std::abs(value) <= deadzone) {
            // stop rapid
            previousZValue = value;
            return key::SPACE;
        }
        if (!(previousZValue > deadzone && value > deadzone)
            && !(previousZValue < -deadzone && value < -deadzone)) {
            previousZValue = value;
            if (value > deadzone) {
                return key::js_rapid_right;
            }
            if (value < -deadzone) {
                return key::js_rapid_left;
            }
        }
    }
    if (sf::Joystick::hasAxis(0, sf::Joystick::Axis::R)) {
        constexpr float deadzone = 99.f;
        float value = sf::Joystick::getAxisPosition(0, sf::Joystick::Axis::R);
        if (std::abs(previousRValue) > deadzone && std::abs(value) <= deadzone) {
            // stop rapid
            previousRValue = value;
            return key::SPACE;
        }
        if (!(previousRValue > deadzone && value > deadzone)
            && !(previousRValue < -deadzone && value < -deadzone)) {
            previousRValue = value;
            if (value > deadzone) {
                return key::js_rapid_down;
            }
            if (value < -deadzone) {
                return key::js_rapid_up;
            }
        }
    }
    return key::None;
}

} // namespace mgo
