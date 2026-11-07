#include "controller.h"

#include "fmt/format.h"
#include "iview.h" // For Input::*;
#include "keycodes.h"
#include "log.h"
#include "view_qt.h"

#include <cassert>
#include <chrono>
#include <utility>

namespace mgo {

Controller::Controller(Model* model)
    : m_model(model)
    , m_menu(buildCommands(model->config()))
{
    m_view = std::make_unique<ViewQt>();
    m_view->setMenu(&m_menu);
    m_view->initialise(*m_model);
    m_view->updateDisplay(*m_model); // get the UI running before we start the motor threads
    m_model->initialise();
}

void Controller::run()
{
    m_model->setAxis1MotorSpeed(m_model->config().readDouble("Axis1SpeedPreset2", 40.0));
    m_model->setAxis2MotorSpeed(m_model->config().readDouble("Axis2SpeedPreset2", 20.0));

    if (m_model->isQuitting()) {
        return;
    }
    m_view->runEventLoop(TICK_INTERVAL, [this]() { tick(); });
    // (Rethrown here, as exceptions mustn't pass through Qt's event loop)
    if (m_tickException) {
        std::rethrow_exception(std::exchange(m_tickException, nullptr));
    }
}

void Controller::tick()
{
    // Nothing in a tick blocks or processes Qt's events (dialogs don't
    // block), so the timer can't call us again before we've finished. This
    // guard is a safety net, should something ever do so
    if (m_inTick) {
        return;
    }
    m_inTick = true;
    try {
        tickBody();
    } catch (...) {
        m_inTick = false;
        m_tickException = std::current_exception();
        m_model->stopAllMotors();
        m_view->quitEventLoop();
        return;
    }
    m_inTick = false;

    if (m_model->isShuttingDown()) {
        MGOLOG("Shutting down");
        // Stop the motor threads. (We must leave the loop afterwards, as
        // the model can't be used once its motors have been destroyed.)
        m_model->stopAllMotors();
        m_model->resetMotorThreads();
        // Note the command used for shutdown should be made passwordless
        // in the /etc/sudoers files
        [[maybe_unused]] auto rc = system("sudo shutdown -h now &");
        m_view->quitEventLoop();
        return;
    }
    if (m_model->isQuitting()) {
        m_view->quitEventLoop();
    }
}

void Controller::tickBody()
{
    processKeyPress();
    m_model->checkStatus();
    m_view->updateDisplay(*m_model);
}

std::string Controller::unitsSuffix(int axisNumber) const
{
    return " (" + m_model->axisUnitName(axisNumber) + ")";
}

bool Controller::isMenuOpen() const
{
    return m_menu.isOpen();
}

void Controller::processKeyPress()
{
    int k = m_view->getEvents();
    if (m_view->isDialogOpen()) {
        // The dialog has the keyboard, so any key here is from the gamepad,
        // which isn't used for dialogs: ignore it. Except Ctrl-Q (e.g. from
        // closing the window), which cancels the dialog and quits
        if (k == key::CtrlQ) {
            m_view->cancelDialog();
            processKey(k);
        }
        return;
    }
    if (m_model->isMotionSequenceRunning()) {
        // Keys wait until the sequence (e.g. a nudge) has finished, as they
        // did when it blocked, so e.g. a quick series of nudges each moves
        // the full amount. Except Space, which stops everything at once
        if ((k & ~key::JOYSTICK) == key::SPACE) {
            MGOLOG("Stopping all motors: Space during a motion sequence");
            m_model->stopAllMotors();
            m_heldKeys.clear();
        } else if (k != key::None) {
            m_heldKeys.push_back(k);
        }
        return;
    }
    if (!m_heldKeys.empty()) {
        if (k != key::None) {
            m_heldKeys.push_back(k);
        }
        k = m_heldKeys.front();
        m_heldKeys.pop_front();
    }
    if (k == key::None) {
        return;
    }
    // Only the menu cares whether a key came from the gamepad
    const bool fromJoystick = k & key::JOYSTICK;
    k &= ~key::JOYSTICK;
    // F1 (help) opens and closes the menu, just as Tab does
    if (k == key::F1) {
        k = key::TAB;
    }
    if (m_menu.isOpen()) {
        // The menu only takes the keys it needs (navigation, and letters for
        // its hotkeys); everything else (e.g. Space to stop all motors)
        // carries on working as normal
        const Menu::Action action = m_menu.handleKey(k, *m_model, fromJoystick);
        switch (action.type) {
            case Menu::Action::Type::None:
                return;
            case Menu::Action::Type::Command:
                // Chosen from the menu: discard any half-entered leader key
                // sequence, then act as if the command's shortcut was pressed
                m_model->setKeyMode(KeyMode::None);
                processKey(action.key);
                return;
            case Menu::Action::Type::PassThrough:
                break;
        }
    } else if (k == key::TAB) {
        m_model->setKeyMode(KeyMode::None);
        m_menu.open();
        return;
    }
    processKey(k);
}

void Controller::processKey(int t)
{
    t = checkKeyAllowedForMode(t);
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
                if (m_model->getMode() == Mode::MultiPass) {
                    // Starts the multi-pass, or continues it after a pause
                    m_model->multiPassGo();
                    break;
                }
                [[fallthrough]];
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
                    // Note all motors will be stopped when a dialog is displayed
                    const std::string axisName = m_model->config().read("Axis1Label", "Z");
                    getNumericInput(
                        "Go to " + axisName + " absolute position" + unitsSuffix(1),
                        { "Specify a value" },
                        0.0,
                        [this](const NumericInputReturn& rc) {
                            if (!rc.cancelled) {
                                m_model->axis1GoToPosition(m_model->fromDisplayUnits(1, rc.value));
                            }
                        });
                    break;
                }
            case key::a2_g:
                {
                    // Note all motors will be stopped when a dialog is displayed
                    const std::string axisName = m_model->config().read("Axis2Label", "X");
                    getNumericInput(
                        "Go to " + axisName + " absolute position" + unitsSuffix(2),
                        { "Specify a value" },
                        0.0,
                        [this](const NumericInputReturn& rc) {
                            if (!rc.cancelled) {
                                m_model->axis2GoToPosition(m_model->fromDisplayUnits(2, rc.value));
                            }
                        });
                    break;
                }
            case key::a1_r:
                {
                    // Relative motion
                    // Note all motors will be stopped when a dialog is displayed
                    const std::string axisName = m_model->config().read("Axis1Label", "Z");
                    getNumericInput(
                        "Go to " + axisName + " relative position" + unitsSuffix(1),
                        { "Specify a RELATIVE offset value" },
                        0.0,
                        [this](const NumericInputReturn& rc) {
                            if (!rc.cancelled) {
                                m_model->axis1GoToOffset(m_model->fromDisplayUnits(1, rc.value));
                            }
                        });
                    break;
                }
            case key::a2_r:
                {
                    // Relative motion
                    // Note all motors will be stopped when a dialog is displayed
                    const std::string axisName = m_model->config().read("Axis2Label", "X");
                    getNumericInput(
                        "Go to " + axisName + " relative position" + unitsSuffix(2),
                        { "Specify a RELATIVE offset value" },
                        0.0,
                        [this](const NumericInputReturn& rc) {
                            if (!rc.cancelled) {
                                m_model->axis2GoToOffset(m_model->fromDisplayUnits(2, rc.value));
                            }
                        });
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
            case key::F1: // help: the menu, which shows every command's shortcut
            case key::f2h:
                {
                    m_model->setKeyMode(KeyMode::None);
                    m_menu.open();
                    break;
                }
            case key::f2s: // setup mode
                {
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
                    if (m_model->getThreads().empty()) {
                        pressAnyKey(
                            "Threading",
                            { "No thread sizes were read at start-up: see "
                                  + m_model->config().read("ThreadsFile", "threadpitches.cfg"),
                              "(the log says why)",
                              "",
                              "Press a key" },
                            [] {});
                        break;
                    }
                    // (As with all dialogs, stop all motors first)
                    m_model->stopAllMotors();
                    m_view->openThreadDialog(
                        m_model->getThreads(),
                        m_model->getThreadChoice(),
                        [this](std::optional<ThreadChoice> choice) {
                            if (choice) {
                                m_model->setThread(*choice);
                                m_model->changeMode(Mode::Threading);
                            }
                        });
                    break;
                }
            case key::f2m: // multi-pass mode
                {
                    if (m_model->config().readBool("DisableAxis2", false)) {
                        break;
                    }
                    if (m_model->getAxis1Memory(0) == AXIS1_UNSET
                        || m_model->getAxis1Memory(1) == AXIS1_UNSET
                        || m_model->getAxis2Memory(0) == AXIS2_UNSET
                        || m_model->getAxis2Memory(1) == AXIS2_UNSET) {
                        pressAnyKey(
                            "Multi-pass",
                            { "Store the start of the cut (both axes) in Mem 1, and the end",
                              "in Mem 2, before using Multi-pass mode",
                              "",
                              "Press a key",
                              "" },
                            [] {});
                        break;
                    }
                    const std::string axis1Name = m_model->config().read("Axis1Label", "Z");
                    const std::string axis2Name = m_model->config().read("Axis2Label", "X");
                    getNumericInput(
                        "Multi-pass",
                        { "Enter the " + axis2Name + " step-over per pass" + unitsSuffix(2) + ".",
                          "",
                          "Each pass cuts " + axis1Name + " from Mem 1 to Mem 2, then returns. "
                              + axis2Name + " steps over",
                          "between passes, from Mem 1's " + axis2Name + " to Mem 2's.",
                          "If zero, the same cut is repeated until stopped.",
                          "",
                          "In the mode, go to Mem 1 and press Enter to start. Space stops.",
                          "",
                          "[&P]: pause between passes (Enter continues)",
                          "[&R] retract between passes" },
                        0.0,
                        [this](const NumericInputReturn& rc) {
                            if (!rc.cancelled) {
                                m_model->changeMode(Mode::MultiPass);
                                m_model->setMultiPassOptions(
                                    { .feedPerPass
                                      = std::abs(m_model->fromDisplayUnits(2, rc.value)),
                                      .pause = rc.optionsSelected.contains('p'),
                                      .retract = rc.optionsSelected.contains('r') });
                            }
                        });
                    break;
                }
            case key::f2p: // taper mode
                {
                    if (m_model->config().readBool("DisableAxis2", false)) {
                        break;
                    }
                    // As with all dialogs, stop all motors first
                    m_model->stopAllMotors();
                    m_view->openTaperDialog(
                        m_model->getTaperAngle(), [this](std::optional<double> angle) {
                            if (angle) {
                                m_model->setTaperAngle(*angle);
                                m_model->changeMode(Mode::Taper);
                            }
                        });
                    break;
                }
            case key::f2r: // X retraction setup
                {
                    if (m_model->config().readBool("DisableAxis2", false)) {
                        break;
                    }
                    listPicker(
                        "Choose retraction direction",
                        { "Normal (outwards)", "Boring (inwards)" },
                        [this](std::optional<std::size_t> rc) {
                            if (rc.has_value()) {
                                m_model->setRetractionDirection(
                                    rc.value() == 0 ? XDirection::Outwards : XDirection::Inwards);
                            }
                        });
                    break;
                }
            case key::f2o: // Radius mode
                {
                    if (m_model->config().readBool("DisableAxis2", false)) {
                        break;
                    }
                    const std::string axis1Name = m_model->config().read("Axis1Label", "Z");
                    const std::string axis2Name = m_model->config().read("Axis2Label", "X");
                    getNumericInput(
                        "Enter radius value required" + unitsSuffix(1) + ":",
                        { "Important! Ensure the tool is at the radius of the workpiece,",
                          "near the end, and set axes to ZERO (this drives the operation).",
                          fmt::format(
                              "Keep "
                              "{} "
                              "constant.",
                              axis2Name),
                          fmt::format(
                              "Then cut away from chuck. Return to zero, then nudge {} inwards, "
                              "re-zero, repeat.",
                              axis1Name),
                          "Memorising the new zero position each time will make returning "
                          "easier." },
                        m_model->toDisplayUnits(1, m_model->getRadius()),
                        [this](const NumericInputReturn& rc) {
                            if (!rc.cancelled) {
                                m_model->setRadius(m_model->fromDisplayUnits(1, rc.value));
                                m_model->changeMode(Mode::Radius);
                            }
                        });
                    break;
                }
            case key::a1_s: // Axis1 position set
                {
                    const std::string axisName = m_model->config().read("Axis1Label", "Z");
                    getNumericInput(
                        axisName + " position set" + unitsSuffix(1),
                        { "[&A] adjust (keeps memory slots)" },
                        0.0,
                        [this](const NumericInputReturn& result) {
                            if (result.cancelled) {
                                return;
                            }
                            m_model->setAxis1Position(m_model->fromDisplayUnits(1, result.value));
                            // This will invalidate any memorised Z positions, so we clear them
                            // unless the user specified not to with 'A'
                            if (!result.optionsSelected.contains('a')) {
                                m_model->clearAllAxis1Memories();
                            }
                        });
                    break;
                }
            case key::a2_s: // Axis2 position set
                {
                    // Note all motors will be stopped when a dialog is displayed
                    const std::string axisName = m_model->config().read("Axis2Label", "X");
                    getNumericInput(
                        axisName + " position set" + unitsSuffix(2),
                        { "[&D] set as diameter", "[&A] adjusts (keeps memory slots)" },
                        0.0,
                        [this](const NumericInputReturn& result) {
                            if (result.cancelled) {
                                return;
                            }
                            double position = m_model->fromDisplayUnits(2, result.value);
                            const bool diameter = result.optionsSelected.contains('d');
                            if (diameter) {
                                position /= 2;
                            }
                            m_model->setAxis2Position(position);
                            // (After setting the position, which clears any diameter)
                            if (diameter) {
                                m_model->diameterIsSet();
                            }
                            // This will invalidate any memorised X positions, so we clear them
                            // unless the user specified not to with 'A'
                            if (!result.optionsSelected.contains('a')) {
                                m_model->clearAllAxis2Memories();
                            }
                        });
                    break;
                }
            case key::i: // Input axis 1 memory value directly
            case key::I:
            case key::a1_i:
                {
                    // Note all motors will be stopped when a dialog is displayed
                    const std::string axisName = m_model->config().read("Axis1Label", "Z");
                    getNumericInput(
                        "Enter " + axisName + " memory value" + unitsSuffix(1),
                        {},
                        0.0,
                        [this](const NumericInputReturn& rc) {
                            if (!rc.cancelled) {
                                m_model->axis1StorePosition(m_model->fromDisplayUnits(1, rc.value));
                            }
                        });
                    break;
                }
            case key::a2_i: // Input axis 2 memory value directly
                {
                    // Note all motors will be stopped when a dialog is displayed
                    const std::string axisName = m_model->config().read("Axis2Label", "X");
                    getNumericInput(
                        "Enter " + axisName + " memory value" + unitsSuffix(2),
                        {},
                        0.0,
                        [this](const NumericInputReturn& rc) {
                            if (!rc.cancelled) {
                                m_model->axis2StorePosition(m_model->fromDisplayUnits(2, rc.value));
                            }
                        });
                    break;
                }
            case key::u: // toggle display units: mm / inches
            case key::U:
                {
                    m_model->toggleDisplayUnits();
                    // The menu's wording (e.g. nudge amounts) follows the units
                    m_menu = Menu(buildCommands(m_model->config(), m_model->getDisplayUnits()));
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
                    // (A taper like any other, so through changeMode(), which
                    // e.g. turns ramping off, as X is synchronised to Z)
                    m_model->setTaperAngle(-45.0);
                    m_model->changeMode(Mode::Taper);
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
                    if (m_model->isAxis1MotorRunning() || m_model->isAxis2MotorRunning()) {
                        // (Logged, as stray keys - e.g. from a gamepad - stop motion)
                        MGOLOG("Stopping all motors: key " << kp);
                    }
                    m_model->stopAllMotors();
                    break;
                }
        }
    }
}

int Controller::checkKeyAllowedForMode(int key)
{
    // (The rules are shared with the menu, which greys out keys not allowed)
    return keyAllowedInCurrentMode(key, *m_model) ? key : key::None;
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

void Controller::getNumericInput(
    std::string_view prompt,
    std::vector<std::string> additionalText,
    double defaultEntry,
    std::function<void(const NumericInputReturn&)> onDone)
{
    // (As with all dialogs, stop all motors first)
    m_model->stopAllMotors();
    std::string defaultEntryAsString = std::to_string(defaultEntry);
    // Remove excess trailing zeros after decimal point (if it exists)
    if (defaultEntryAsString.contains(".")) {
        defaultEntryAsString.erase(defaultEntryAsString.find_last_not_of('0') + 1);
        if (defaultEntryAsString.back() == '.') {
            defaultEntryAsString += "00";
        }
    }
    m_view->openInput(
        Input::Type::Numeric,
        prompt,
        std::move(additionalText),
        [onDone = std::move(onDone)](const Input::Return& rc) {
            onDone({ rc.cancelled, rc.number, rc.optionsSelected });
        },
        defaultEntryAsString);
}

void Controller::listPicker(
    std::string_view prompt,
    std::vector<std::string> listItems,
    std::function<void(std::optional<std::size_t>)> onDone)
{
    m_model->stopAllMotors();
    m_view->openInput(
        Input::Type::ListPicker,
        prompt,
        {},
        [onDone = std::move(onDone)](const Input::Return& rc) { onDone(rc.pickedItem); },
        "",
        std::move(listItems));
}

void Controller::pressAnyKey(
    std::string_view prompt,
    std::vector<std::string> additionalText,
    std::function<void()> onDone)
{
    m_model->stopAllMotors();
    m_view->openInput(
        Input::Type::PressAnyKey,
        prompt,
        std::move(additionalText),
        [onDone = std::move(onDone)](const Input::Return&) { onDone(); });
}

} // end namespace
