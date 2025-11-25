#include "controller.h"

#include "fmt/format.h"
#include "keycodes.h"
#include "log.h"
#include "stepperControl/igpio.h"
#include "view_sfml.h"

#include <cassert>
#include <chrono>

namespace mgo {

namespace {

void yieldSleep(std::chrono::microseconds microsecs)
{
    auto start = std::chrono::high_resolution_clock::now();
    auto end = start + microsecs;
    while (std::chrono::high_resolution_clock::now() < end) {
        std::this_thread::yield();
    }
}

} // end anonymous namespace

Controller::Controller(Model* model)
    : m_model(model)
{
    m_view = std::make_unique<ViewSfml>();
    m_view->initialise(*m_model);
    m_view->updateDisplay(*m_model); // get SFML running before we start the motor threads
    m_model->initialise();
}

void Controller::run()
{
    m_model->setAxis1MotorSpeed(m_model->config().readDouble("Axis1SpeedPreset2", 40.0));
    m_model->setAxis2MotorSpeed(m_model->config().readDouble("Axis2SpeedPreset2", 20.0));

    while (!m_model->isQuitting()) {
        processKeyPress();

        m_model->checkStatus();

        m_view->updateDisplay(*m_model);

        if (m_model->isShuttingDown()) {
            MGOLOG("Shutting down");
            // Stop the motor threads
            m_model->resetMotorThreads();
            // clang-format off
            // Note the command used for shutdown should be made passwordless
            // in the /etc/sudoers files
            #pragma GCC diagnostic push
            #pragma GCC diagnostic ignored "-Wunused-result"
            system("sudo shutdown -h now &");
            #pragma GCC diagnostic pop
            // clang-format on
        }

        // Small delay just to avoid the UI loop spinning
        yieldSleep(std::chrono::microseconds(50'000));
    }
}

void Controller::processKeyPress()
{
    int t = m_view->getInput();
    t = checkKeyAllowedForMode(t);
    t = processModeInputKeys(t);
    if (t != key::None) {
        if (m_model->getKeyMode() != KeyMode::None) {
            // If we are in a "leader key mode" (i.e. the first "prefix" key has
            // already been pressed) then we can modify t here. If the key
            // combination is recognised, we get a "chord" keypress e.g. key::xz.
            t = processLeaderKeyModeKeyPress(t);
            m_model->setKeyMode(KeyMode::None);
        }
        m_model->setKeyPressed(t);
        // Modify key press if it is a known axis leader key:
        m_model->setKeyPressed(checkForAxisLeaderKeys(m_model->getKeyPressed()));
        int kp = m_model->getKeyPressed();
        switch (kp) {
            case key::None:
                {
                    break;
                }
            case key::COLON:
            case key::F2:
                {
                    m_model->setKeyMode(KeyMode::Function);
                    break;
                }
            // End of "Leader" keys ==============
            case key::CtrlQ:
            case key::f2q:
                {
                    m_model->stopAllMotors();
                    m_model->quit();
                    break;
                }
            case key::l:
            case key::L:
            case key::a1_l:
                {
                    m_model->axis1GoToPreviousPosition();
                    break;
                }
            case key::a2_l:
                {
                    m_model->axis2GoToPreviousPosition();
                    break;
                }
            case key::aAll_l:
                {
                    m_model->axis1GoToPreviousPosition();
                    m_model->axis2GoToPreviousPosition();
                    break;
                }
            case key::FULLSTOP:
            case key::a1_FULLSTOP:
                {
                    m_model->repeatLastRelativeMove(Axis::Axis1);
                    break;
                }
            case key::a2_FULLSTOP:
                {
                    m_model->repeatLastRelativeMove(Axis::Axis2);
                    break;
                }
            case key::aAll_FULLSTOP:
                {
                    m_model->repeatLastRelativeMove(Axis::Axis1);
                    m_model->repeatLastRelativeMove(Axis::Axis2);
                    break;
                }
            case key::a2_MINUS:
                {
                    // X-axis speed decrease
                    m_model->axis2SpeedDecrease();
                    break;
                }
            case key::a2_EQUALS:
                {
                    // X-axis speed increase
                    m_model->axis2SpeedIncrease();
                    break;
                }
            // Nudge in X axis
            case key::W:
                {
                    if (m_model->isAxisLocked(2)) {
                        break;
                    }
                    m_model->axis2Nudge(XDirection::Inwards, 0.01);
                    break;
                }
            case key::w:
                {
                    if (m_model->isAxisLocked(2)) {
                        break;
                    }
                    m_model->axis2Nudge(XDirection::Inwards, 0.05);
                    break;
                }
            case key::AltW:
                {
                    if (m_model->isAxisLocked(2)) {
                        break;
                    }
                    m_model->axis2Nudge(XDirection::Inwards, 0.1);
                    break;
                }
            // Nudge out X axis
            case key::S:
                {
                    if (m_model->isAxisLocked(2)) {
                        break;
                    }
                    m_model->axis2Nudge(XDirection::Outwards, 0.01);
                    break;
                }
            case key::s:
                {
                    if (m_model->isAxisLocked(2)) {
                        break;
                    }
                    m_model->axis2Nudge(XDirection::Outwards, 0.05);
                    break;
                }
            case key::AltS:
                {
                    if (m_model->isAxisLocked(2)) {
                        break;
                    }
                    m_model->axis2Nudge(XDirection::Outwards, 0.1);
                    break;
                }
            case key::a1_EQUALS:
            case key::EQUALS: // (i.e. plus)
                {
                    m_model->axis1SpeedIncrease();
                    break;
                }
            case key::a1_MINUS:
            case key::MINUS:
                {
                    m_model->axis1SpeedDecrease();
                    break;
                }
            case key::a1_m:
                {
                    m_model->axis1StorePosition();
                    break;
                }
            case key::a2_m:
                {
                    m_model->axis2StorePosition();
                    break;
                }
            case key::m:
            case key::M:
                {
                    m_model->axis1StorePosition();
                    m_model->axis2StorePosition();
                    break;
                }
            case key::a2_ENTER:
                {
                    m_model->axis2GoToCurrentMemory();
                    break;
                }
            case key::aAll_ENTER:
                {
                    m_model->axis1GoToCurrentMemory();
                    m_model->axis2GoToCurrentMemory();
                    break;
                }
            case key::ENTER:
            case key::a1_ENTER:
                {
                    if (m_model->getAxis1Memory(m_model->getCurrentMemorySlot()) == AXIS1_UNSET) {
                        if (m_model->config().readBool("DisableAxis2", false)) {
                            break;
                        }
                        if (m_model->getAxis2Memory(m_model->getCurrentMemorySlot())
                            != AXIS2_UNSET) {
                            m_model->axis2GoToCurrentMemory();
                        }
                    }
                    m_model->axis1GoToCurrentMemory();
                    break;
                }
            case key::UP:
                {
                    if (m_model->isAxisLocked(2)) {
                        break;
                    }
                    m_model->axis2Move(XDirection::Inwards);
                    break;
                }
            case key::DOWN:
                {
                    if (m_model->isAxisLocked(2)) {
                        break;
                    }
                    m_model->axis2Move(XDirection::Outwards);
                    break;
                }
            case key::LEFT:
                {
                    if (m_model->isAxisLocked(1)) {
                        break;
                    }
                    m_model->axis1Move(ZDirection::Left);
                    break;
                }
            case key::RIGHT:
                {
                    if (m_model->isAxisLocked(1)) {
                        break;
                    }
                    m_model->axis1Move(ZDirection::Right);
                    break;
                }
            case key::AltUp:
                {
                    m_model->axis2Rapid(XDirection::Inwards);
                    break;
                }
            case key::AltDown:
                {
                    m_model->axis2Rapid(XDirection::Outwards);
                    break;
                }
            case key::AltLeft:
                {
                    m_model->axis1Rapid(ZDirection::Left);
                    break;
                }
            case key::AltRight:
                {
                    m_model->axis1Rapid(ZDirection::Right);
                    break;
                }
            // Joystick-specific rapids
            // Note we disallow motion on both axes at once to avoid
            // accidental triggering of both with the stick
            case key::js_rapid_up:
                {
                    if (m_model->isAxisLocked(2)) {
                        break;
                    }
                    if (!m_model->isAxis1MotorRunning()) {
                        m_model->axis2Rapid(XDirection::Inwards);
                    }
                    break;
                }
            case key::js_rapid_down:
                {
                    if (m_model->isAxisLocked(2)) {
                        break;
                    }
                    if (!m_model->isAxis1MotorRunning()) {
                        m_model->axis2Rapid(XDirection::Outwards);
                    }
                    break;
                }
            case key::js_rapid_left:
                {
                    if (m_model->isAxisLocked(1)) {
                        break;
                    }
                    if (!m_model->isAxis2MotorRunning()) {
                        m_model->axis1Rapid(ZDirection::Left);
                    }
                    break;
                }
            case key::js_rapid_right:
                {
                    if (m_model->isAxisLocked(1)) {
                        break;
                    }
                    if (!m_model->isAxis2MotorRunning()) {
                        m_model->axis1Rapid(ZDirection::Right);
                    }
                    break;
                }
            // Joystick-specific motion
            // Note we disallow motion on both axes at once to avoid
            // accidental triggering of both with the stick
            case key::js_up:
                {
                    if (m_model->isAxisLocked(2)) {
                        break;
                    }
                    if (!m_model->isAxis1MotorRunning()) {
                        m_model->axis2Move(XDirection::Inwards);
                    }
                    break;
                }
            case key::js_down:
                {
                    if (m_model->isAxisLocked(2)) {
                        break;
                    }
                    if (!m_model->isAxis1MotorRunning()) {
                        m_model->axis2Move(XDirection::Outwards);
                    }
                    break;
                }
            case key::js_left:
                {
                    if (m_model->isAxisLocked(1)) {
                        break;
                    }
                    if (!m_model->isAxis2MotorRunning()) {
                        m_model->axis1Move(ZDirection::Left);
                    }
                    break;
                }
            case key::js_right:
                {
                    if (m_model->isAxisLocked(1)) {
                        break;
                    }
                    if (!m_model->isAxis2MotorRunning()) {
                        m_model->axis1Move(ZDirection::Right);
                    }
                    break;
                }

            case key::a:
                {
                    if (m_model->isAxisLocked(1)) {
                        break;
                    }
                    m_model->axis1Nudge(ZDirection::Left, 0.05);
                    break;
                }
            case key::A:
                {
                    if (m_model->isAxisLocked(1)) {
                        break;
                    }
                    m_model->axis1Nudge(ZDirection::Left, 0.01);
                    break;
                }
            case key::AltA:
                {
                    if (m_model->isAxisLocked(1)) {
                        break;
                    }
                    m_model->axis1Nudge(ZDirection::Left, 0.1);
                    break;
                }
            case key::d:
                {
                    if (m_model->isAxisLocked(1)) {
                        break;
                    }
                    m_model->axis1Nudge(ZDirection::Right, 0.05);
                    break;
                }
            case key::D:
                {
                    if (m_model->isAxisLocked(1)) {
                        break;
                    }
                    m_model->axis1Nudge(ZDirection::Right, 0.01);
                    break;
                }
            case key::AltD:
                {
                    if (m_model->isAxisLocked(1)) {
                        break;
                    }
                    m_model->axis1Nudge(ZDirection::Right, 0.1);
                    break;
                }
            case key::LBRACKET: // [
                {
                    m_model->selectPreviousMemorySlot();
                    break;
                }
            case key::RBRACKET: // ]
                {
                    m_model->selectNextMemorySlot();
                    break;
                }

            // Speed presets for Z with number keys 1-5
            case key::ONE:
            case key::TWO:
            case key::THREE:
            case key::FOUR:
            case key::FIVE:
                {
                    m_model->axis1SpeedPreset();
                    break;
                }
            // Speed presets for X with number keys 6-0 or X leader + 1-5
            case key::SIX:
            case key::SEVEN:
            case key::EIGHT:
            case key::NINE:
            case key::ZERO:
            case key::a2_1:
            case key::a2_2:
            case key::a2_3:
            case key::a2_4:
            case key::a2_5:
                {
                    m_model->axis2SpeedPreset();
                    break;
                }
            case key::a1_f:
                {
                    // Fast return to point
                    m_model->axis1FastReturn();
                    break;
                }
            case key::a2_f:
                {
                    // Fast return to point
                    m_model->axis2FastReturn();
                    break;
                }
            case key::aAll_f:
                {
                    m_model->axis1FastReturn();
                    m_model->axis2FastReturn();
                    break;
                }
            case key::r:
            case key::R:
                {
                    // X retraction
                    m_model->axis2Retract();
                    break;
                }
            case key::a1_z:
                {
                    m_model->axis1Zero();
                    break;
                }
            case key::a2_z:
                {
                    m_model->axis2Zero();
                    break;
                }
            case key::aAll_z:
                {
                    m_model->axis1Zero();
                    m_model->axis2Zero();
                    break;
                }
            case key::a1_g:
                {
                    m_model->changeMode(Mode::Axis1GoTo);
                    break;
                }
            case key::a2_g:
                {
                    m_model->changeMode(Mode::Axis2GoTo);
                    break;
                }
            case key::a1_r:
                {
                    // Relative motion
                    m_model->changeMode(Mode::Axis1GoToOffset);
                    break;
                }
            case key::a2_r:
                {
                    m_model->changeMode(Mode::Axis2GoToOffset);
                    break;
                }
            case key::ASTERISK: // shutdown
                {
                    // clang-format off
                    #ifndef FAKE
                    m_model->stopAllMotors();
                    m_model->quit();
                    m_model->shutDown();
                    #endif
                    // clang-format on
                    break;
                }
            case key::F1: // help mode
            case key::f2h:
                {
                    m_model->changeMode(Mode::Help);
                    break;
                }
            case key::f2s: // setup mode
                {
                    m_model->setEnabledFunction(Mode::None);
                    m_model->setAxis1MotorSpeed(0.8f);
                    m_model->setAxis2MotorSpeed(1.f);
                    m_model->changeMode(Mode::Setup);
                    break;
                }
            case key::f2t: // threading mode
                {
                    if (m_model->config().readBool("DisableAxis2", false)) {
                        break;
                    }
                    m_model->changeMode(Mode::Threading);
                    break;
                }
            case key::f2m: // multi-pass mode
                {
                    if (m_model->config().readBool("DisableAxis2", false)) {
                        break;
                    }
                    m_model->changeMode(Mode::MultiPass);
                    break;
                }
            case key::f2p: // taper mode
                {
                    if (m_model->config().readBool("DisableAxis2", false)) {
                        break;
                    }
                    m_model->changeMode(Mode::Taper);
                    break;
                }
            case key::f2r: // X retraction setup
                {
                    if (m_model->config().readBool("DisableAxis2", false)) {
                        break;
                    }
                    m_model->changeMode(Mode::Axis2RetractSetup);
                    break;
                }
            case key::f2o: // Radius mode
                {
                    if (m_model->config().readBool("DisableAxis2", false)) {
                        break;
                    }
                    m_model->changeMode(Mode::Radius);
                    break;
                }
            case key::a2_s: // X position set
                {
                    m_model->changeMode(Mode::Axis2PositionSetup);
                    break;
                }
            case key::a1_s: // Z position set
                {
                    m_model->changeMode(Mode::Axis1PositionSetup);
                    break;
                }
            case key::i: // Input axis 1 memory value directly
            case key::I:
            case key::a1_i:
                {
                    // Note all motors will be stopped when a dialog is displayed
                    const std::string axisName = m_model->config().read("Axis1Label", "Z");
                    const double entry
                        = getNumericInput("Enter " + axisName + " memory value", 0.0);
                    m_model->axis1StorePosition(entry);
                    break;
                }
            case key::a2_i: // Input axis 2 memory value directly
                {
                    // Note all motors will be stopped when a dialog is displayed
                    const std::string axisName = m_model->config().read("Axis2Label", "X");
                    const double entry
                        = getNumericInput("Enter " + axisName + " memory value", 0.0);
                    m_model->axis2StorePosition(entry);
                    break;
                }
            case key::ESC: // return to normal mode
                {
                    // Cancel any retract as well
                    m_model->setIsAxis2Retracted(false);
                    m_model->changeMode(Mode::None);
                    break;
                }
            case key::h:
            case key::H: // quick chamfer
                {
                    m_model->setTaperAngle(-45.0);
                    m_model->setEnabledFunction(Mode::Taper);
                    break;
                }
            case key::a1_k: // lock axis 1
                {
                    if (m_model->isAxisLocked(1)) {
                        m_model->unlockAxis(1);
                    } else {
                        m_model->lockAxis(1);
                    }
                    break;
                }
            case key::a2_k: // lock axis 2
                {
                    if (m_model->isAxisLocked(2)) {
                        m_model->unlockAxis(2);
                    } else {
                        m_model->lockAxis(2);
                    }
                    break;
                }
            case key::a1_DELETE:
                {
                    m_model->clearCurrentMemorySlot(Axis::Axis1);
                    break;
                }
            case key::a2_DELETE:
                {
                    m_model->clearCurrentMemorySlot(Axis::Axis2);
                    break;
                }
            case key::DELETE:
            case key::aAll_DELETE:
                {
                    m_model->clearCurrentMemorySlot(Axis::Axis1);
                    m_model->clearCurrentMemorySlot(Axis::Axis2);
                    break;
                }
            default: // e.g. key::SPACE to stop all motors
                {
                    m_model->stopAllMotors();
                    break;
                }
        }
    }
}

int Controller::checkKeyAllowedForMode(int key)
{
    // The mode means some keys are ignored, for instance in
    // threading, you cannot change the speed of the z-axis as
    // that would affect the thread pitch
    // Always allow certain keys:
    if (key == key::CtrlQ || key == key::ESC || key == key::ENTER) {
        return key;
    }
    // Don't allow any x movement when retracted
    if (m_model->getIsAxis2Retracted()
        && (key == key::UP || key == key::DOWN || key == key::W || key == key::w || key == key::s
            || key == key::S)) {
        return -1;
    }
    switch (m_model->getCurrentDisplayMode()) {
        case Mode::None:
            return key;
        case Mode::Help:
            return key;
        case Mode::Setup:
            if (key == key::LEFT || key == key::RIGHT || key == key::UP || key == key::DOWN) {
                return key;
            }
            if (key == key::SPACE) {
                return key;
            }
            return -1;
        case Mode::Threading:
            if (key == key::UP || key == key::DOWN) {
                return key;
            }
            return -1;
        case Mode::Axis2RetractSetup:
            if (key == key::UP || key == key::DOWN) {
                return key;
            }
            return -1;
        // Any modes that have numerical input:
        case Mode::Axis1PositionSetup:
        case Mode::Axis2PositionSetup:
            if (key >= key::ZERO && key <= key::NINE) {
                return key;
            }
            if (key == key::FULLSTOP || key == key::BACKSPACE || key == key::DELETE) {
                return key;
            }
            if (key == key::MINUS) {
                return key;
            }
            if (key == key::d || key == key::D || key == key::a || key == key::A) {
                return key;
            }
            return -1;
        case Mode::Taper:
        case Mode::Axis1GoTo:
        case Mode::Axis2GoTo:
        case Mode::Axis1GoToOffset:
        case Mode::Axis2GoToOffset:
        case Mode::Radius:
        case Mode::MultiPass:
            if (key >= key::ZERO && key <= key::NINE) {
                return key;
            }
            if (key == key::FULLSTOP || key == key::BACKSPACE || key == key::DELETE) {
                return key;
            }
            if (key == key::MINUS) {
                return key;
            }
            return -1;
        default:
            // unhandled mode
            assert(false);
            return -1;
    }
}

int Controller::processModeInputKeys(int key)
{
    // If we are in a "mode" then certain keys (e.g. the number keys) are used for input
    // so are processed here before allowing them to fall through to the main key processing
    if (m_model->getCurrentDisplayMode() == Mode::Taper
        || m_model->getCurrentDisplayMode() == Mode::Axis2PositionSetup
        || m_model->getCurrentDisplayMode() == Mode::Axis1PositionSetup
        || m_model->getCurrentDisplayMode() == Mode::Axis1GoTo
        || m_model->getCurrentDisplayMode() == Mode::Axis2GoTo
        || m_model->getCurrentDisplayMode() == Mode::Axis1GoToOffset
        || m_model->getCurrentDisplayMode() == Mode::Axis2GoToOffset
        || m_model->getCurrentDisplayMode() == Mode::Radius
        || m_model->getCurrentDisplayMode() == Mode::MultiPass) {
        if (key >= key::ZERO && key <= key::NINE) {
            m_model->getInputString() += static_cast<char>(key);
            return -1;
        }
        if (key == key::FULLSTOP) {
            if (m_model->getInputString().find(".") == std::string::npos) {
                m_model->getInputString() += static_cast<char>(key);
            }
            return -1;
        }
        if (key == key::DELETE) {
            m_model->setInputString("");
        }
        if (key == key::BACKSPACE) {
            if (!m_model->getInputString().empty()) {
                m_model->getInputString().pop_back();
            }
            return -1;
        }
        if (key == key::MINUS && m_model->getInputString().empty()) {
            m_model->setInputString("-");
            return -1;
        }
    }
    if (m_model->getCurrentDisplayMode() == Mode::Threading) {
        if (key == key::UP) {
            m_model->selectNextThreadPitch();
            return -1;
        }
        if (key == key::DOWN) {
            m_model->selectPreviousThreadPitch();
            return -1;
        }
        if (key == key::ESC) {
            // Reset motor speed to something sane
            m_model->setAxis1MotorSpeed(m_model->config().readDouble("Axis1SpeedPreset2", 40.0));
            // fall through...
        }
    }
    if (m_model->getCurrentDisplayMode() == Mode::Axis2RetractSetup) {
        if (key == key::UP) {
            m_model->setRetractionDirection(XDirection::Inwards);
            return -1;
        }
        if (key == key::DOWN) {
            m_model->setRetractionDirection(XDirection::Outwards);
            return -1;
        }
    }

    // Diameter set
    if (m_model->getCurrentDisplayMode() == Mode::Axis2PositionSetup
        && (key == key::d || key == key::D)) {
        float xPos = 0;
        try {
            xPos = std::abs(std::stof(m_model->getInputString()));
        } catch (...) {
        }
        m_model->setAxis2Position(xPos / 2);
        // This will invalidate any memorised X positions, so we clear them
        m_model->clearAllAxis2Memories();
        m_model->setCurrentDisplayMode(Mode::None);
        m_model->diameterIsSet();
        m_model->changeMode(Mode::None);
        return -1;
    }

    // Adjust axis value without affecting memory slots (useful for
    // adjusting manually from a DRO if we have lost steps)
    if (m_model->getCurrentDisplayMode() == Mode::Axis1PositionSetup
        && (key == key::a || key == key::A)) {
        float pos = 0;
        try {
            pos = std::abs(std::stof(m_model->getInputString()));
        } catch (...) {
        }
        m_model->setAxis1Position(pos);
        m_model->changeMode(Mode::None);
        return -1;
    }
    if (m_model->getCurrentDisplayMode() == Mode::Axis2PositionSetup
        && (key == key::a || key == key::A)) {
        float pos = 0;
        try {
            pos = std::abs(std::stof(m_model->getInputString()));

        } catch (...) {
        }
        m_model->setAxis2Position(pos);
        m_model->changeMode(Mode::None);
        return -1;
    }

    if (m_model->getCurrentDisplayMode() != Mode::None && key == key::ENTER) {
        m_model->acceptInputValue();
        m_model->setCurrentDisplayMode(Mode::None);
        return -1;
    }
    return key;
}

int Controller::processLeaderKeyModeKeyPress(int keyPress)
{
    if (m_model->getKeyMode() == KeyMode::Axis1 || m_model->getKeyMode() == KeyMode::Axis2
        || m_model->getKeyMode() == KeyMode::AxisAll) {
        int bitFlip = 0x1000; // bit 12
        if (m_model->getKeyMode() == KeyMode::Axis2) {
            bitFlip = 0x2000; // bit 13
        }
        if (m_model->getKeyMode() == KeyMode::AxisAll) {
            bitFlip = 0x4000; // bit 14
        }
        return keyPress + bitFlip;
    }

    if (m_model->getKeyMode() == KeyMode::Function) {
        switch (keyPress) {
            case key::h:
            case key::H:
                keyPress = key::f2h;
                break;
            case key::s:
            case key::S:
                keyPress = key::f2s;
                break;
            case key::t:
            case key::T:
                keyPress = key::f2t;
                break;
            case key::p:
            case key::P:
            case key::FULLSTOP: // > - looks like a taper
            case key::COMMA: // < - looks like a taper
                keyPress = key::f2p;
                break;
            case key::r:
            case key::R:
                keyPress = key::f2r;
                break;
            case key::o:
            case key::O:
                keyPress = key::f2o;
                break;
            case key::m:
            case key::M:
                keyPress = key::f2m;
                break;
            case key::q:
            case key::Q:
                keyPress = key::f2q;
                break;
            default:
                keyPress = key::None;
        }
    }
    m_model->setKeyMode(KeyMode::None);
    return keyPress;
}

int Controller::checkForAxisLeaderKeys(int key)
{
    // We determine whether the key pressed is a leader key for
    // an axis (note the key can be remapped in config) and sets
    // the model's keyMode if so. Returns true if one was pressed, false if not.
    if (key == static_cast<int>(m_model->config().readLong("Axis1Leader", 122L))) {
        m_model->setKeyMode(KeyMode::Axis1);
        return key::None;
    }
    if (key == static_cast<int>(m_model->config().readLong("Axis2Leader", 120L))) {
        m_model->setKeyMode(KeyMode::Axis2);
        return key::None;
    }
    if (key == key::BACKSLASH) {
        m_model->setKeyMode(KeyMode::AxisAll);
        return key::None;
    }
    return key;
}

double Controller::getNumericInput(const std::string& prompt, double defaultEntry)
{
    m_model->stopAllMotors();
    return m_view->getNumericInput(prompt, defaultEntry);
}

std::string Controller::getTextInput(const std::string& prompt, const std::string& defaultEntry)
{
    m_model->stopAllMotors();
    return m_view->getTextInput(prompt, defaultEntry);
}

} // end namespace
