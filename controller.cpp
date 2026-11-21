#include "controller.h"

#include "fmt/format.h"
#include "iview.h" // For Input::*;
#include "keycodes.h"
#include "log.h"
#include "view_qt.h"

#include <QProcess>
#include <QStringList>

#include <cassert>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <utility>

#include <unistd.h>

namespace mgo {

Controller::Controller(Model* model)
    : m_model(model)
    , m_menu(buildCommands(
          model->config(),
          model->getDisplayUnits(),
          model->threadingAutoRetract(),
          model->finishesOnScale(1),
          model->finishesOnScale(2)))
{
    // (Sounds are off unless their files are set)
    m_alertSound = model->config().read("SoundAlert", "");
    m_alarmSound = model->config().read("SoundAlarm", "");
#ifdef __APPLE__
    m_soundPlayer = model->config().read("SoundPlayer", "afplay");
#else
    m_soundPlayer = model->config().read("SoundPlayer", "aplay -q");
#endif
    // lc runs as root (via sudo, for pigpio), but the sound server
    // (PipeWire) runs in the user's session, which root's processes can't
    // find. So the player runs as the user who ran sudo, pointed at their
    // session
    const char* sudoUser = std::getenv("SUDO_USER");
    const char* sudoUid = std::getenv("SUDO_UID");
    if (geteuid() == 0 && sudoUser && sudoUid) {
        m_soundPlayer = fmt::format(
            "sudo -u {} env XDG_RUNTIME_DIR=/run/user/{} {}", sudoUser, sudoUid, m_soundPlayer);
    }
    for (const std::string* file : { &m_alertSound, &m_alarmSound }) {
        if (!file->empty() && !std::filesystem::exists(*file)) {
            MGOLOG("Sound file not found: " << *file);
        }
    }
    // The first sound after the Pi boots is lost entirely (tried on the
    // machine, 2026-11-20: even aplay run by hand plays nothing), but every
    // one after it plays, even after 30 minutes' quiet. So one is played now,
    // to wake the output: it's usually not heard, and it does no harm if it is
    playSound(Sound::Alert);
    m_view = std::make_unique<ViewQt>();
    m_view->setMenu(&m_menu);
    m_view->initialise(*m_model);
    m_view->updateDisplay(*m_model); // get the UI running before we start the motor threads
    m_model->initialise();
}

void Controller::rebuildMenu()
{
    // (Keeping its place, as it may be open: e.g. after a toggle: see Command::isToggle)
    m_menu.setCommands(buildCommands(
        m_model->config(),
        m_model->getDisplayUnits(),
        m_model->threadingAutoRetract(),
        m_model->finishesOnScale(1),
        m_model->finishesOnScale(2)));
}

void Controller::run()
{
    // (The model sets the motors' starting speeds, in initialise())
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
    const SoundState afterKeys = soundStateOf(*m_model);
    m_model->checkStatus();
    checkLimitSwitch();
    checkScaleMismatch();
    showBacklashReport();
    if (const auto sound
        = m_soundCues.update(afterKeys, soundStateOf(*m_model), SoundCues::Clock::now())) {
        playSound(*sound);
    }
    m_view->updateDisplay(*m_model);
}

void Controller::playSound(Sound sound)
{
    const std::string& file = sound == Sound::Alert ? m_alertSound : m_alarmSound;
    if (file.empty() || m_soundPlayerFailed) {
        return;
    }
    // In its own process, detached, so nothing here waits for it to finish
    // (and it's kept off the motor threads' cores, as it inherits this
    // thread's). With PipeWire's suspend turned off, the HDMI output stays
    // awake, so the start of the sound isn't lost: see TODO.md, "Investigate Sound Output"
    QStringList args = QProcess::splitCommand(QString::fromStdString(m_soundPlayer));
    if (args.isEmpty()) {
        return;
    }
    const QString program = args.takeFirst();
    args << QString::fromStdString(file);
    if (!QProcess::startDetached(program, args)) {
        // (Once: e.g. the player isn't installed)
        MGOLOG("Couldn't run the sound player: " << m_soundPlayer << " (no more sounds)");
        m_soundPlayerFailed = true;
    }
}

void Controller::confirmMeasureBacklash()
{
    if (!m_model->canMeasureAxis1Backlash()) {
        return;
    }
    const std::string label = m_model->axisLabel(1);
    const int direction = m_model->axis1BacklashMeasureFirstDirection();
    okCancel(
        fmt::format("Measure {}'s backlash", label),
        { fmt::format(
              "{} will move {} {}, then back, three times, ending where it is now.",
              label,
              fmt::format(
                  "{:.{}f} {}",
                  m_model->toDisplayUnits(1, m_model->axis1BacklashMeasureDistanceMm()),
                  m_model->axisDecimals(1),
                  m_model->axisUnitName(1)),
              direction > 0 ? "right" : "left"),
          "Make sure the tool is clear of the work, and nothing is in the way.",
          "It compares each move with the scale, so takes about 20 seconds.",
          "Space stops it." },
        [this]() { m_model->axis1MeasureBacklash(); });
}

void Controller::checkScaleMismatch()
{
    // Every motorised axis whose scale and motor disagree is asked about in
    // one dialog. A mismatch stops everything, and the other axis's scale
    // settles a little later (they stop at different moments), so the
    // dialog waits for it (see Model::scalesStillSettling()), rather than
    // asking about one and then, a moment later, the other
    int asks = 0;
    std::vector<int> axes;
    for (const int n : { 1, 2 }) {
        asks += m_model->scaleMismatchAsks(n);
        if (m_model->scaleMismatchMm(n)) {
            axes.push_back(n);
        }
    }
    // Once per ask, and not over another dialog (it waits until that's
    // closed; nothing can move meanwhile)
    if (axes.empty() || asks == m_scaleAsksShown || m_view->isDialogOpen()
        || m_model->scalesStillSettling()) {
        return;
    }
    m_scaleAsksShown = asks;
    m_model->setKeyMode(KeyMode::None);
    m_heldKeys.clear();
    const auto length = [this](int n, double mm) {
        return fmt::format(
            "{:.{}f} {}",
            m_model->toDisplayUnits(n, mm),
            m_model->axisDecimals(n),
            m_model->axisUnitName(n));
    };
    const auto misread = [this](int n) { return m_model->scaleMisreadJumpMm(n).has_value(); };
    const auto labels = [this](const std::vector<int>& numbers) {
        std::string text;
        for (const int n : numbers) {
            text += (text.empty() ? "" : " and ") + m_model->axisLabel(n);
        }
        return text;
    };
    // A misreading scale's axis keeps its motor's position, whatever's
    // chosen for any other
    std::vector<int> misreading;
    std::vector<int> disagreeing;
    for (const int n : axes) {
        (misread(n) ? misreading : disagreeing).push_back(n);
    }
    const auto keepMisreading = [this, misreading]() {
        for (const int n : misreading) {
            m_model->keepMotorPosition(n);
        }
    };
    std::vector<std::string> lines;
    for (const int n : misreading) {
        const std::string label = m_model->axisLabel(n);
        lines.push_back(
            fmt::format(
                "{}'s reading jumped by {} with {} at rest, more than the {} allowed,",
                label,
                length(n, std::abs(*m_model->scaleMisreadJumpMm(n))),
                label,
                length(n, m_model->scaleMismatchLimitMm(n))));
        lines.push_back(
            "which the table can't have moved. Interference, or a loose connection, can do "
            "this.");
    }
    if (!misreading.empty() && !disagreeing.empty()) {
        lines.push_back("");
    }
    for (const int n : disagreeing) {
        const std::string label = m_model->axisLabel(n);
        const double position = m_model->motorPosition(n);
        const double mismatch = *m_model->scaleMismatchMm(n);
        lines.push_back(
            fmt::format(
                "{}'s scale says {}, but {}'s position is {}: {} apart,",
                label,
                length(n, position + mismatch),
                label,
                length(n, position),
                length(n, std::abs(mismatch))));
        lines.push_back(
            fmt::format("more than the {} allowed.", length(n, m_model->scaleMismatchLimitMm(n))));
    }
    lines.push_back("So everything has stopped.");
    if (disagreeing.empty()) {
        lines.push_back("");
        lines.push_back(
            fmt::format(
                "Any key: keep {}'s position{}, and line {} up with it again.",
                labels(misreading),
                misreading.size() > 1 ? "s" : "",
                misreading.size() > 1 ? "the scales" : "the scale"));
        pressAnyKey(
            fmt::format(
                "{}'s scale{} misreading",
                labels(misreading),
                misreading.size() > 1 ? "s are" : " is"),
            std::move(lines),
            keepMisreading);
        return;
    }
    const bool several = disagreeing.size() > 1;
    lines.push_back(
        fmt::format(
            "The motor{} may have lost steps (e.g. a stall), or the scale{} may have misread.",
            several ? "s" : "",
            several ? "s" : ""));
    lines.push_back("");
    if (!misreading.empty()) {
        lines.push_back(
            fmt::format(
                "{} keeps its position, whichever you choose, as its scale is misreading.",
                labels(misreading)));
    }
    lines.push_back(
        fmt::format(
            "Y or Enter: use {} position{} (usually right after a stall).",
            several ? "the scales'" : "the scale's",
            several ? "s" : ""));
    lines.push_back(
        fmt::format(
            "N or Esc: keep {}'s position{}, and line {} up with {} again.",
            labels(disagreeing),
            several ? "s" : "",
            several ? "the scales" : "the scale",
            several ? "them" : "it"));
    std::vector<int> unsteady;
    for (const int n : disagreeing) {
        if (!m_model->scaleIsSteady(n)) {
            unsteady.push_back(n);
        }
    }
    if (!unsteady.empty()) {
        lines.insert(
            lines.end() - 2,
            fmt::format(
                "{}'s reading is still changing: Y waits until it settles, and asks again.",
                labels(unsteady)));
    }
    m_view->openInput(
        Input::Type::OkCancel,
        fmt::format(
            "{}'s scale{} and position{} disagree",
            labels(disagreeing),
            several ? "s" : "",
            several ? "s" : ""),
        std::move(lines),
        [this, disagreeing, keepMisreading](const Input::Return& rc) {
            keepMisreading();
            for (const int n : disagreeing) {
                if (rc.cancelled) {
                    m_model->keepMotorPosition(n);
                } else {
                    m_model->acceptScalePosition(n);
                }
            }
        });
}

void Controller::showBacklashReport()
{
    // (Not over another dialog: it waits until that's closed)
    if (m_view->isDialogOpen()) {
        return;
    }
    if (auto report = m_model->takeBacklashReport()) {
        pressAnyKey(
            fmt::format("{}'s backlash", m_model->axisLabel(1)), std::move(*report), []() { });
    }
}

void Controller::checkLimitSwitch()
{
    if (!m_model->limitSwitchTripped()) {
        m_limitDialogShown = false;
        return;
    }
    // Once per stop, and not over another dialog (the flashing message says
    // what to do meanwhile, and the menu can clear it)
    if (m_limitDialogShown || m_view->isDialogOpen()) {
        return;
    }
    m_limitDialogShown = true;
    m_model->setKeyMode(KeyMode::None);
    m_heldKeys.clear();
    std::vector<std::string> lines {
        "A limit switch has been pressed (or its wiring is broken),",
        "so all the motors have stopped, and won't move until it's cleared.",
        "",
        "Once cleared, move off the switch carefully: the program can't tell",
        "which switch it is, so moving further into it isn't prevented.",
        "",
        "Enter (or Y): clear it   Esc (or N): leave it (clear it later from",
        "the menu: System)"
    };
    okCancel("Limit switch: all motors stopped", std::move(lines), [this] {
        m_model->clearLimitSwitchTrip();
    });
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
    if ((k & ~key::JOYSTICK) == key::RAPID_RELEASED) {
        // A rapid's key (or stick) let go: the rapid slows down to a stop,
        // if it can. Otherwise it's Space, as it always was: it stops
        // everything at once
        if (!m_view->isDialogOpen() && m_model->rapidReleased()) {
            MGOLOG("Rapid released: slowing down to a stop");
            return;
        }
        k = (k & key::JOYSTICK) | key::SPACE;
    }
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
    if (m_model->isMotionSequenceRunning() || m_model->anyFinishingOnScale()) {
        // Keys wait until the sequence (e.g. a nudge) has finished, as they
        // did when it blocked, so e.g. a quick series of nudges each moves
        // the full amount. Likewise while a move is finished on the scale.
        // Except Space, which stops everything at once
        if ((k & ~key::JOYSTICK) == key::SPACE) {
            MGOLOG("Stopping all motors: Space during a motion sequence");
            m_model->stopAllMotors();
            m_model->cancelManualAxisGoTos();
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

void Controller::logKey(int key) const
{
    // (By its command's description, if it has one, e.g. "z g: Go to a Z
    // position", so lc.log shows what was asked for)
    std::string description;
    for (const Command& c : m_menu.commands()) {
        if (c.key == key) {
            description = ": " + c.description;
            break;
        }
    }
    const std::string shortcut = shortcutText(key, m_model->config());
    MGOLOG(
        fmt::format(
            "Key {}{}", shortcut.empty() ? fmt::format("{:#x}", key) : shortcut, description));
}

void Controller::processKey(int t)
{
    if (m_model->getKeyMode() != KeyMode::None) {
        // After a leader key (e.g. z), the key becomes that axis's (e.g. z h,
        // halve Z), and it's that which is checked against the mode below, as
        // the menu does: not the key on its own (h, the quick chamfer, which
        // the mill's config turns off). Space after a leader is still Space,
        // which always stops everything, and Esc just cancels the leader
        if (t == key::ESC) {
            m_model->setKeyMode(KeyMode::None);
            return;
        }
        if (t != key::SPACE) {
            t = processLeaderKeyModeKeyPress(t);
        }
        m_model->setKeyMode(KeyMode::None);
    }
    t = checkKeyAllowedForMode(t);
    if (t != key::None) {
        m_model->setKeyPressed(t);
        // Modify key press if it is a known axis leader key:
        m_model->setKeyPressed(checkForAxisLeaderKeys(m_model->getKeyPressed()));
        int kp = m_model->getKeyPressed();
        if (kp != key::None) {
            logKey(kp);
        }
        switch (kp) {
            case key::None:
                {
                    break;
                }
            case key::CtrlQ:
                {
                    m_model->stopAllMotors();
                    m_model->quit();
                    break;
                }
            case key::l:
            case key::L:
            case key::a1_l:
                {
                    m_model->goToPreviousPosition(1);
                    break;
                }
            case key::a2_l:
                {
                    m_model->goToPreviousPosition(2);
                    break;
                }
            case key::aAll_l:
                {
                    m_model->goToPreviousPosition(1);
                    m_model->goToPreviousPosition(2);
                    break;
                }
            case key::FULLSTOP:
            case key::a1_FULLSTOP:
                {
                    m_model->repeatLastRelativeMove(1);
                    break;
                }
            case key::a2_FULLSTOP:
                {
                    m_model->repeatLastRelativeMove(2);
                    break;
                }
            case key::aAll_FULLSTOP:
                {
                    m_model->repeatLastRelativeMove(1);
                    m_model->repeatLastRelativeMove(2);
                    break;
                }
            // Nudge in X axis
            case key::W:
                {
                    m_model->nudge(2, axisDirection(XDirection::Inwards), 0.01);
                    break;
                }
            case key::w:
                {
                    m_model->nudge(2, axisDirection(XDirection::Inwards), 0.05);
                    break;
                }
            case key::AltW:
                {
                    m_model->nudge(2, axisDirection(XDirection::Inwards), 0.1);
                    break;
                }
            // Nudge out X axis
            case key::S:
                {
                    m_model->nudge(2, axisDirection(XDirection::Outwards), 0.01);
                    break;
                }
            case key::s:
                {
                    m_model->nudge(2, axisDirection(XDirection::Outwards), 0.05);
                    break;
                }
            case key::AltS:
                {
                    m_model->nudge(2, axisDirection(XDirection::Outwards), 0.1);
                    break;
                }
            // Faster and slower: = (i.e. plus) and - for axis 1, or after
            // either motorised axis's leader key
            case key::EQUALS:
            case key::a1_EQUALS:
            case key::a2_EQUALS:
                {
                    m_model->speedIncrease(kp == key::EQUALS ? 1 : key::axisOfKey(kp));
                    break;
                }
            case key::MINUS:
            case key::a1_MINUS:
            case key::a2_MINUS:
                {
                    m_model->speedDecrease(kp == key::MINUS ? 1 : key::axisOfKey(kp));
                    break;
                }
            case key::a1_m:
            case key::a2_m:
            case key::a3_m:
            case key::a4_m:
                {
                    m_model->storePosition(key::axisOfKey(kp));
                    break;
                }
            case key::m:
            case key::M:
                {
                    // (Every axis is stored: a manual axis with no reading
                    // keeps what it had)
                    for (int n = 1; n <= Model::AXES; ++n) {
                        const ManualAxis* axis = m_model->manualAxis(n);
                        if (m_model->hasAxis(n) && (!axis || axis->hasReading())) {
                            m_model->storePosition(n);
                        }
                    }
                    break;
                }
            case key::a2_ENTER:
            case key::a3_ENTER:
            case key::a4_ENTER:
                {
                    m_model->goToCurrentMemory(key::axisOfKey(kp));
                    break;
                }
            case key::a1_DELETE:
            case key::a2_DELETE:
            case key::a3_DELETE:
            case key::a4_DELETE:
                {
                    m_model->clearCurrentMemorySlot(key::axisOfKey(kp));
                    break;
                }
            case key::aAll_ENTER:
                {
                    // Every axis in the go-to (so not, e.g., the mill's Z)
                    // goes there at the same time: the motorised ones move,
                    // and the manual ones are wound there, if they're set
                    for (int n = 1; n <= Model::AXES; ++n) {
                        if (m_model->inSlotGoTo(n)
                            && (m_model->manualAxis(n) == nullptr
                                || m_model->getMemory(n, m_model->getCurrentMemorySlot()))) {
                            m_model->goToCurrentMemory(n);
                        }
                    }
                    break;
                }
            case key::ENTER:
                // (While measuring a tool, Enter records each touch)
                if (m_model->isMeasuringTool()) {
                    recordToolTouch();
                    break;
                }
                if (m_model->getMode() == Mode::MultiPass
                    || (m_model->getMode() == Mode::Threading && m_model->threadingPassesSet())
                    || m_model->getMode() == Mode::Knurling || m_model->gearCuttingSet()
                    || m_model->radiusSet()) {
                    // Starts the multi-pass, or continues it after a pause
                    m_model->multiPassGo();
                    break;
                }
                [[fallthrough]];
            case key::a1_ENTER:
                {
                    // (If axis 1 isn't set in the slot, axis 2 goes there instead)
                    if (!m_model->getMemory(1, m_model->getCurrentMemorySlot())) {
                        if (m_model->config().readBool("DisableAxis2", false)) {
                            break;
                        }
                        m_model->goToCurrentMemory(2);
                    }
                    m_model->goToCurrentMemory(1);
                    break;
                }
            case key::UP:
                {
                    m_model->move(2, axisDirection(XDirection::Inwards));
                    break;
                }
            case key::DOWN:
                {
                    m_model->move(2, axisDirection(XDirection::Outwards));
                    break;
                }
            case key::LEFT:
                {
                    m_model->move(1, axisDirection(ZDirection::Left));
                    break;
                }
            case key::RIGHT:
                {
                    m_model->move(1, axisDirection(ZDirection::Right));
                    break;
                }
            case key::AltUp:
                {
                    m_model->rapid(2, axisDirection(XDirection::Inwards));
                    break;
                }
            case key::AltDown:
                {
                    m_model->rapid(2, axisDirection(XDirection::Outwards));
                    break;
                }
            case key::AltLeft:
                {
                    m_model->rapid(1, axisDirection(ZDirection::Left));
                    break;
                }
            case key::AltRight:
                {
                    m_model->rapid(1, axisDirection(ZDirection::Right));
                    break;
                }
            // Joystick-specific rapids
            // Note we disallow motion on both axes at once to avoid
            // accidental triggering of both with the stick
            case key::js_rapid_up:
                {
                    if (!m_model->isMotorRunning(1)) {
                        m_model->rapid(2, axisDirection(XDirection::Inwards));
                    }
                    break;
                }
            case key::js_rapid_down:
                {
                    if (!m_model->isMotorRunning(1)) {
                        m_model->rapid(2, axisDirection(XDirection::Outwards));
                    }
                    break;
                }
            case key::js_rapid_left:
                {
                    if (!m_model->isMotorRunning(2)) {
                        m_model->rapid(1, axisDirection(ZDirection::Left));
                    }
                    break;
                }
            case key::js_rapid_right:
                {
                    if (!m_model->isMotorRunning(2)) {
                        m_model->rapid(1, axisDirection(ZDirection::Right));
                    }
                    break;
                }
            // Joystick-specific motion
            // Note we disallow motion on both axes at once to avoid
            // accidental triggering of both with the stick
            case key::js_up:
                {
                    if (!m_model->isMotorRunning(1)) {
                        m_model->move(2, axisDirection(XDirection::Inwards));
                    }
                    break;
                }
            case key::js_down:
                {
                    if (!m_model->isMotorRunning(1)) {
                        m_model->move(2, axisDirection(XDirection::Outwards));
                    }
                    break;
                }
            case key::js_left:
                {
                    if (!m_model->isMotorRunning(2)) {
                        m_model->move(1, axisDirection(ZDirection::Left));
                    }
                    break;
                }
            case key::js_right:
                {
                    if (!m_model->isMotorRunning(2)) {
                        m_model->move(1, axisDirection(ZDirection::Right));
                    }
                    break;
                }

            case key::a:
                {
                    m_model->nudge(1, axisDirection(ZDirection::Left), 0.05);
                    break;
                }
            case key::A:
                {
                    m_model->nudge(1, axisDirection(ZDirection::Left), 0.01);
                    break;
                }
            case key::AltA:
                {
                    m_model->nudge(1, axisDirection(ZDirection::Left), 0.1);
                    break;
                }
            case key::d:
                {
                    m_model->nudge(1, axisDirection(ZDirection::Right), 0.05);
                    break;
                }
            case key::D:
                {
                    m_model->nudge(1, axisDirection(ZDirection::Right), 0.01);
                    break;
                }
            case key::AltD:
                {
                    m_model->nudge(1, axisDirection(ZDirection::Right), 0.1);
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
                    m_model->selectSpeedPreset(1, kp - key::ONE + 1);
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
                    // (0 is the fifth, after 9)
                    const int preset = kp >= key::a2_1 ? kp - key::a2_1 + 1
                        : kp == key::ZERO              ? 5
                                                       : kp - key::SIX + 1;
                    m_model->selectSpeedPreset(2, preset);
                    break;
                }
            case key::a1_f:
            case key::a2_f:
                {
                    // Fast return to the current memory slot
                    m_model->fastReturn(key::axisOfKey(kp));
                    break;
                }
            case key::aAll_f:
                {
                    m_model->fastReturn(1);
                    m_model->fastReturn(2);
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
            case key::a2_z:
            case key::a3_z:
            case key::a4_z:
                {
                    // (Axis 1's position is the work's end's, and axis 2's
                    // the diameter's. The manual axes move nothing)
                    const int n = key::axisOfKey(kp);
                    confirmFeatureLoss(
                        "Zeroing " + m_model->axisLabel(n), n == 2, n == 1, [this, n] {
                            m_model->zero(n);
                        });
                    break;
                }
            case key::aAll_z:
                {
                    // (Including the manual axes, if any: see zeroAllAxes())
                    const bool manualAxes = m_model->manualAxis(Model::FIRST_MANUAL_AXIS)
                        || m_model->manualAxis(Model::LAST_MANUAL_AXIS);
                    confirmFeatureLoss(
                        manualAxes ? "Zeroing all axes" : "Zeroing both axes", true, true, [this] {
                            m_model->zeroAllAxes();
                        });
                    break;
                }
            case key::a1_g:
                {
                    // Note all motors will be stopped when a dialog is displayed
                    const std::string axisName = m_model->axisLabel(1);
                    getNumericInput(
                        "Go to " + axisName + " absolute position" + unitsSuffix(1),
                        { "Specify a value" },
                        0.0,
                        m_model->axisUnitName(1),
                        [this](const NumericInputReturn& rc) {
                            if (!rc.cancelled) {
                                m_model->goToPosition(1, m_model->fromDisplayUnits(1, rc.value));
                            }
                        });
                    break;
                }
            case key::a2_g:
                {
                    // Note all motors will be stopped when a dialog is displayed
                    const std::string axisName = m_model->axisLabel(2);
                    getNumericInput(
                        "Go to " + axisName + " absolute position" + unitsSuffix(2),
                        { "Specify a value" },
                        0.0,
                        m_model->axisUnitName(2),
                        [this](const NumericInputReturn& rc) {
                            if (!rc.cancelled) {
                                m_model->goToPosition(2, m_model->fromDisplayUnits(2, rc.value));
                            }
                        });
                    break;
                }
            case key::a1_r:
                {
                    // Relative motion
                    // Note all motors will be stopped when a dialog is displayed
                    const std::string axisName = m_model->axisLabel(1);
                    getNumericInput(
                        "Go to " + axisName + " relative position" + unitsSuffix(1),
                        { "Specify a RELATIVE offset value" },
                        0.0,
                        m_model->axisUnitName(1),
                        [this](const NumericInputReturn& rc) {
                            if (!rc.cancelled) {
                                m_model->goToOffset(1, m_model->fromDisplayUnits(1, rc.value));
                            }
                        });
                    break;
                }
            case key::a2_r:
                {
                    // Relative motion
                    // Note all motors will be stopped when a dialog is displayed
                    const std::string axisName = m_model->axisLabel(2);
                    getNumericInput(
                        "Go to " + axisName + " relative position" + unitsSuffix(2),
                        { "Specify a RELATIVE offset value" },
                        0.0,
                        m_model->axisUnitName(2),
                        [this](const NumericInputReturn& rc) {
                            if (!rc.cancelled) {
                                m_model->goToOffset(2, m_model->fromDisplayUnits(2, rc.value));
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
                {
                    m_model->setKeyMode(KeyMode::None);
                    m_menu.open();
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
                            [] { });
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
                    // As with all dialogs, stop all motors first
                    m_model->stopAllMotors();
                    MultiPassDialogInfo info = multiPassDialogInfo();
                    info.setup
                        = defaultMultiPassSetup(m_model->getMultiPassSetup(), info.positions);
                    m_view->openMultiPassDialog(info, [this](std::optional<MultiPassSetup> setup) {
                        if (!setup) {
                            return;
                        }
                        m_model->changeMode(Mode::MultiPass);
                        // (The dialog only accepts a setup which can be
                        // used, and nothing has moved since)
                        if (const std::string error = m_model->setMultiPassSetup(*setup);
                            !error.empty()) {
                            m_model->changeMode(Mode::None);
                            pressAnyKey("Multi-pass", { error, "", "Press a key", "" }, [] { });
                        }
                    });
                    break;
                }
            case key::threadingPasses: // a multi-pass of threading passes
                {
                    const Thread* thread = m_model->getThread();
                    if (m_model->getMode() != Mode::Threading || !thread) {
                        break;
                    }
                    // As with all dialogs, stop all motors first
                    m_model->stopAllMotors();
                    MultiPassDialogInfo info = multiPassDialogInfo();
                    info.setup = m_model->threadingPassesDialogSetup();
                    info.threading = MultiPassDialogInfo::Threading {
                        .thread = fmt::format(
                            "{} {}, pitch {}",
                            thread->name,
                            m_model->getThreadChoice().female ? "female" : "male",
                            describePitch(*thread)),
                        .depthMm = *m_model->getThreadDepthMm(),
                        .compoundAngleDeg = m_model->getCompoundInfeedAngleDeg(),
                        .minimumInfeedMm
                        = m_model->config().readDouble("ThreadingMinimumInfeed", 0.02)
                    };
                    m_view->openMultiPassDialog(info, [this](std::optional<MultiPassSetup> setup) {
                        if (!setup) {
                            return;
                        }
                        // (The dialog only accepts a setup which can be
                        // used, and nothing has moved since)
                        if (const std::string error = m_model->setThreadingPassesSetup(*setup);
                            !error.empty()) {
                            const std::vector<std::string> text { error, "", "Press a key", "" };
                            pressAnyKey("Threading passes", text, [] { });
                        }
                    });
                    break;
                }
            case key::f2k: // cut knurling mode
                {
                    if (m_model->config().readBool("DisableAxis2", false)) {
                        break;
                    }
                    // As with all dialogs, stop all motors first
                    m_model->stopAllMotors();
                    if (!m_model->getDiameterMm()) {
                        const std::string x = m_model->axisLabel(2);
                        pressAnyKey(
                            "Cut knurling",
                            { "Set " + x + " as a diameter first (with the tool on the work's",
                              "surface): the knurl's lead depends on the diameter",
                              "",
                              "Press a key",
                              "" },
                            [] { });
                        break;
                    }
                    MultiPassDialogInfo info = multiPassDialogInfo();
                    info.setup = m_model->knurlingDialogSetup();
                    info.knurling = MultiPassDialogInfo::Knurling {
                        .maxCutSpeed = m_model->spindlePassSpeedLimit(),
                        .minimumSpindleRpm = Model::KNURLING_MIN_SPINDLE_RPM,
                        .spindleRpm = m_model->config().readDouble("KnurlingSpindleRpm", 0.0),
                        .minimumInfeedMm
                        = m_model->config().readDouble("ThreadingMinimumInfeed", 0.02)
                    };
                    m_view->openMultiPassDialog(info, [this](std::optional<MultiPassSetup> setup) {
                        if (!setup) {
                            return;
                        }
                        m_model->changeMode(Mode::Knurling);
                        // (The dialog only accepts a setup which can be
                        // used, and nothing has moved since)
                        if (const std::string error = m_model->setKnurlingSetup(*setup);
                            !error.empty()) {
                            m_model->changeMode(Mode::None);
                            pressAnyKey("Cut knurling", { error, "", "Press a key", "" }, [] { });
                        }
                    });
                    break;
                }
            case key::f2g: // gear cutting mode
                {
                    // As with all dialogs, stop all motors first
                    m_model->stopAllMotors();
                    if (const std::string error = m_model->gearCuttingUnavailable();
                        !error.empty()) {
                        pressAnyKey("Gear cutting", { error, "", "Press a key", "" }, [] { });
                        break;
                    }
                    const MultiPassDialogInfo mp = multiPassDialogInfo();
                    GearDialogInfo info { .cut = mp.cut,
                                          .indexLabel = mp.feed.label,
                                          .positions = mp.positions,
                                          .setup = m_model->gearDialogSetup(),
                                          .calculatorMemory = mp.calculatorMemory };
                    m_view->openGearDialog(info, [this](std::optional<GearSetup> setup) {
                        if (!setup) {
                            return;
                        }
                        m_model->changeMode(Mode::GearCutting);
                        // (The dialog only accepts a setup which can be
                        // used, and nothing has moved since)
                        if (const std::string error = m_model->setGearSetup(*setup);
                            !error.empty()) {
                            m_model->changeMode(Mode::None);
                            pressAnyKey("Gear cutting", { error, "", "Press a key", "" }, [] { });
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
            case key::f2o: // radius mode: a radius on the work's end
            case key::f2f: // radius mode: a shoulder with a fillet
                {
                    if (m_model->config().readBool("DisableAxis2", false)) {
                        break;
                    }
                    // As with all dialogs, stop all motors first
                    m_model->stopAllMotors();
                    if (const std::string error = m_model->radiusUnavailable(); !error.empty()) {
                        pressAnyKey("Radius", { error, "", "Press a key", "" }, [] { });
                        break;
                    }
                    const MultiPassDialogInfo mp = multiPassDialogInfo();
                    RadiusDialogInfo info { .cut = mp.cut,
                                            .feed = mp.feed,
                                            .work = m_model->radiusWork(),
                                            .setup = m_model->getRadiusSetup(),
                                            .calculatorMemory = mp.calculatorMemory };
                    // (The radius, depth and nose radius carry over from the
                    // other kind, if that was used last)
                    info.setup.kind = kp == key::f2f ? RadiusKind::Shoulder : RadiusKind::End;
                    // With the tool library, the nose radius is the current tool's
                    if (m_model->toolLibraryEnabled() && m_model->toolLibrary().currentIsKnown()) {
                        info.setup.noseRadius = m_model->toolLibrary().current().noseRadiusMm;
                    }
                    m_view->openRadiusDialog(info, [this](std::optional<RadiusSetup> setup) {
                        if (!setup) {
                            return;
                        }
                        m_model->changeMode(Mode::Radius);
                        // (The dialog only accepts a setup which can be
                        // used, and nothing has moved since)
                        if (const std::string error = m_model->setRadiusSetup(*setup);
                            !error.empty()) {
                            m_model->changeMode(Mode::None);
                            pressAnyKey("Radius", { error, "", "Press a key", "" }, [] { });
                        }
                    });
                    break;
                }
            case key::a1_e: // mark the work's end (current feature)
                {
                    m_model->markWorkEnd();
                    break;
                }
            case key::a1_b: // re-base an axis from its scale
            case key::a2_b:
                {
                    m_model->rebaseFromScaleByHand(key::axisOfKey(kp));
                    break;
                }
            case key::a1_h: // halve an axis's position (to find a centre)
            case key::a2_h:
            case key::a3_h:
            case key::a4_h:
                {
                    const int n = key::axisOfKey(kp);
                    confirmFeatureLoss(
                        "Halving " + m_model->axisLabel(n), n == 2, n == 1, [this, n] {
                            m_model->half(n);
                        });
                    break;
                }
            case key::a3_s:
            case key::a4_s:
                {
                    // (A manual axis's has no options: its memory slots are
                    // always cleared, as its zero has moved)
                    const int axisNumber = key::axisOfKey(kp);
                    if (!m_model->hasAxis(axisNumber)) {
                        break;
                    }
                    getNumericInput(
                        m_model->axisLabel(axisNumber) + " position set" + unitsSuffix(axisNumber),
                        {},
                        0.0,
                        m_model->axisUnitName(axisNumber),
                        [this, axisNumber](const NumericInputReturn& result) {
                            if (!result.cancelled) {
                                m_model->setPosition(
                                    axisNumber,
                                    m_model->fromDisplayUnits(axisNumber, result.value));
                            }
                        });
                    break;
                }
            case key::a2_d: // record the feature's diameter (current feature)
                {
                    m_model->recordFeatureDiameter();
                    break;
                }
            case key::clearFeature:
                {
                    m_model->clearFeature();
                    break;
                }
            case key::featureStart:
            case key::featureStartExact:
                {
                    m_model->featureGoToStart(kp == key::featureStart);
                    break;
                }
            case key::featureDiameter:
                {
                    m_model->featureGoToDiameter();
                    break;
                }
            case key::featureEnd:
            case key::featureEndExact:
                {
                    m_model->featureGoToWorkEnd(kp == key::featureEnd);
                    break;
                }
            case key::a1_s: // Axis1 position set
                {
                    const std::string axisName = m_model->axisLabel(1);
                    getNumericInput(
                        axisName + " position set" + unitsSuffix(1),
                        { "[&A] adjust (keeps memory slots)" },
                        0.0,
                        m_model->axisUnitName(1),
                        [this, axisName](const NumericInputReturn& result) {
                            if (result.cancelled) {
                                return;
                            }
                            // (Adjusting keeps the memory slots, and the work's end)
                            const bool adjust = result.optionsSelected.contains('a');
                            confirmFeatureLoss(
                                "Setting " + axisName + "'s position",
                                false,
                                !adjust,
                                [this, result, adjust] {
                                    m_model->setPosition(
                                        1, m_model->fromDisplayUnits(1, result.value));
                                    // This will invalidate any memorised Z positions, so we
                                    // clear them unless the user specified not to with 'A'
                                    if (!adjust) {
                                        m_model->clearMemory(1);
                                        m_model->clearWorkEnd();
                                    }
                                });
                        });
                    break;
                }
            case key::a2_s: // Axis2 position set
                {
                    // Note all motors will be stopped when a dialog is displayed
                    const std::string axisName = m_model->axisLabel(2);
                    getNumericInput(
                        axisName + " position set" + unitsSuffix(2),
                        { "[&D] set as diameter", "[&A] adjusts (keeps memory slots)" },
                        0.0,
                        m_model->axisUnitName(2),
                        [this, axisName](const NumericInputReturn& result) {
                            if (result.cancelled) {
                                return;
                            }
                            const bool diameter = result.optionsSelected.contains('d');
                            // (Set as a diameter, the feature's is replaced by the
                            // one just measured, rather than lost)
                            confirmFeatureLoss(
                                "Setting " + axisName + "'s position (not as a diameter)",
                                !diameter,
                                false,
                                [this, result, diameter] {
                                    double position = m_model->fromDisplayUnits(2, result.value);
                                    if (diameter) {
                                        position /= 2;
                                    }
                                    m_model->setPosition(2, position);
                                    // (After setting the position, which clears any diameter)
                                    if (diameter) {
                                        m_model->diameterIsSet();
                                    }
                                    // This will invalidate any memorised X positions, so we
                                    // clear them unless the user specified not to with 'A'
                                    if (!result.optionsSelected.contains('a')) {
                                        m_model->clearMemory(2);
                                    }
                                });
                        });
                    break;
                }
            case key::i: // Input axis 1 memory value directly
            case key::I:
            case key::a1_i:
                {
                    // Note all motors will be stopped when a dialog is displayed
                    const std::string axisName = m_model->axisLabel(1);
                    getNumericInput(
                        "Enter " + axisName + " memory value" + unitsSuffix(1),
                        {},
                        0.0,
                        m_model->axisUnitName(1),
                        [this](const NumericInputReturn& rc) {
                            if (!rc.cancelled) {
                                m_model->storePosition(1, m_model->fromDisplayUnits(1, rc.value));
                            }
                        });
                    break;
                }
            case key::a2_i: // Input axis 2 memory value directly
                {
                    // Note all motors will be stopped when a dialog is displayed
                    const std::string axisName = m_model->axisLabel(2);
                    getNumericInput(
                        "Enter " + axisName + " memory value" + unitsSuffix(2),
                        {},
                        0.0,
                        m_model->axisUnitName(2),
                        [this](const NumericInputReturn& rc) {
                            if (!rc.cancelled) {
                                m_model->storePosition(2, m_model->fromDisplayUnits(2, rc.value));
                            }
                        });
                    break;
                }
            case key::u: // toggle display units: mm / inches
            case key::U:
                {
                    m_model->toggleDisplayUnits();
                    // The menu's wording (e.g. nudge amounts) follows the units
                    rebuildMenu();
                    break;
                }
            case key::toggleThreadingAutoRetract:
                {
                    m_model->toggleThreadingAutoRetract();
                    // (The menu shows whether it's on)
                    rebuildMenu();
                    break;
                }
            case key::toggleAxis1FinishOnScale:
            case key::toggleAxis2FinishOnScale:
                {
                    m_model->toggleFinishOnScale(kp == key::toggleAxis1FinishOnScale ? 1 : 2);
                    // (The menu shows whether it's on)
                    rebuildMenu();
                    break;
                }
            case key::ESC: // return to normal mode
                {
                    // (While measuring a tool, Esc only cancels that)
                    if (m_model->isMeasuringTool()) {
                        m_model->cancelToolMeasurement();
                        break;
                    }
                    // Cancel any retract as well
                    m_model->setIsAxis2Retracted(false);
                    m_model->changeMode(Mode::None);
                    m_model->cancelManualAxisGoTos();
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
            case key::t: // change tool (tool library)
            case key::T:
                {
                    chooseTool();
                    break;
                }
            case key::toolNew:
                {
                    confirmMeasureTool(m_model->toolLibrary().nextFreeNumber(), true);
                    break;
                }
            case key::toolMeasureAgain:
                {
                    pickToolToMeasure();
                    break;
                }
            case key::toolTouch:
                {
                    recordToolTouch();
                    break;
                }
            case key::toolCancelMeasuring:
                {
                    m_model->cancelToolMeasurement();
                    break;
                }
            case key::toolEdit:
                {
                    describeCurrentTool();
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
            case key::clearLimitTrip:
                {
                    m_model->clearLimitSwitchTrip();
                    break;
                }
            case key::F9: // clear the warning (nothing stops)
                {
                    m_model->clearWarning();
                    break;
                }
            case key::measureAxis1Backlash:
                {
                    confirmMeasureBacklash();
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
            case key::DELETE:
            case key::aAll_DELETE:
                {
                    for (int n = 1; n <= Model::AXES; ++n) {
                        m_model->clearCurrentMemorySlot(n);
                    }
                    break;
                }
            default: // e.g. key::SPACE to stop all motors
                {
                    if (m_model->isMotorRunning(1) || m_model->isMotorRunning(2)) {
                        // (Logged, as stray keys - e.g. from a gamepad - stop motion)
                        MGOLOG("Stopping all motors: key " << kp);
                    }
                    m_model->stopAllMotors();
                    // (Only Space ends a go-to on a manual axis: not a stray
                    // key, e.g. from the gamepad)
                    if ((kp & ~key::JOYSTICK) == key::SPACE) {
                        m_model->cancelManualAxisGoTos();
                    }
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
    switch (m_model->getKeyMode()) {
        case KeyMode::Axis1:
            return keyPress + 0x1000; // bit 12
        case KeyMode::Axis2:
            return keyPress + 0x2000; // bit 13
        case KeyMode::AxisAll:
            return keyPress + 0x4000; // bit 14
        case KeyMode::Axis3:
            return keyPress + 0x8000; // bit 15
        case KeyMode::Axis4:
            return keyPress + 0x20000; // bit 17 (16 is Ctrl)
        case KeyMode::None:
            break;
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
    // (The manual axes have no default leader keys)
    for (int n = Model::FIRST_MANUAL_AXIS; n <= Model::LAST_MANUAL_AXIS; ++n) {
        const auto leader = m_model->config().readLong(fmt::format("Axis{}Leader", n), 0);
        if (m_model->manualAxis(n) && leader != 0 && key == static_cast<int>(leader)) {
            m_model->setKeyMode(n == 3 ? KeyMode::Axis3 : KeyMode::Axis4);
            return key::None;
        }
    }
    return key;
}

void Controller::getNumericInput(
    std::string_view prompt,
    std::vector<std::string> additionalText,
    double defaultEntry,
    std::string unit,
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
        defaultEntryAsString,
        std::nullopt,
        calc::Context { .memory = calculatorMemory(), .unit = std::move(unit) });
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

MultiPassDialogInfo Controller::multiPassDialogInfo() const
{
    const auto axis = [this](int n, const char* defaultLabel) {
        return MultiPassDialogInfo::Axis { .label = m_model->config().read(
                                               fmt::format("Axis{}Label", n), defaultLabel),
                                           .unit = m_model->axisUnitName(n),
                                           .decimals = m_model->axisDecimals(n),
                                           .displayPerMm = m_model->toDisplayUnits(n, 1.0) };
    };
    MultiPassDialogInfo info;
    info.cut = axis(1, "Z");
    info.feed = axis(2, "X");
    info.positions = m_model->multiPassPositions();
    info.diameterKnown = m_model->getDiameterMm().has_value();
    info.feedRetractsOutwards = m_model->getRetractionDirection() == XDirection::Outwards;
    info.retractLastPasses = m_model->retractLastPasses();
    info.calculatorMemory = calculatorMemory();
    return info;
}

calc::Memory Controller::calculatorMemory() const
{
    // e.g. "m1x", for slot 1 (the first) on X: as displayed, so a sum gives
    // what would be typed in from the screen
    calc::Memory memory;
    for (int axis = 1; axis <= Model::AXES; ++axis) {
        if (!m_model->hasAxis(axis)) {
            continue;
        }
        std::string label = m_model->axisLabel(axis);
        for (char& c : label) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        for (std::size_t slot = 0; slot < m_model->getMemorySize(); ++slot) {
            const auto mm = m_model->getMemory(axis, slot);
            memory[fmt::format("m{}{}", slot + 1, label)]
                = mm ? std::optional(m_model->toDisplayUnits(axis, *mm)) : std::nullopt;
        }
    }
    return memory;
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

void Controller::okCancel(
    std::string_view prompt,
    std::vector<std::string> additionalText,
    std::function<void()> onOk)
{
    m_model->stopAllMotors();
    m_view->openInput(
        Input::Type::OkCancel,
        prompt,
        std::move(additionalText),
        [onOk = std::move(onOk)](const Input::Return& rc) {
            if (!rc.cancelled) {
                onOk();
            }
        });
}

void Controller::confirmFeatureLoss(
    const std::string& what,
    bool diameter,
    bool workEnd,
    std::function<void()> action)
{
    const auto featureDiameter = diameter ? m_model->getFeatureDiameterMm() : std::nullopt;
    const auto featureWorkEnd = workEnd ? m_model->getWorkEndZ() : std::nullopt;
    if (!featureDiameter && !featureWorkEnd) {
        action();
        return;
    }
    const auto length = [this](int axis, double mm) {
        return fmt::format(
            "{:.{}f} {}",
            m_model->toDisplayUnits(axis, mm),
            m_model->axisDecimals(axis),
            m_model->axisUnitName(axis));
    };
    std::vector<std::string> lines { what + " clears the current feature's:" };
    if (featureDiameter) {
        lines.push_back("    Ø " + length(2, *featureDiameter));
    }
    if (featureWorkEnd) {
        lines.push_back("    end, " + m_model->axisLabel(1) + " " + length(1, *featureWorkEnd));
    }
    lines.push_back("");
    lines.push_back("Enter (or Y): go ahead   Esc (or N): cancel");
    okCancel("Clear the current feature?", std::move(lines), std::move(action));
}

namespace {

// About as many characters as fit across a dialog's lines (see qt_dialog.cpp)
constexpr std::size_t DIALOG_LINE_LENGTH = 46;

// text split into lines of up to width characters, at spaces (a word longer
// than that has a line of its own)
std::vector<std::string> wrapText(const std::string& text, std::size_t width)
{
    std::vector<std::string> lines;
    std::string line;
    std::size_t start = 0;
    while (start < text.size()) {
        auto end = text.find(' ', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        const std::string word = text.substr(start, end - start);
        if (!line.empty() && line.size() + 1 + word.size() > width) {
            lines.push_back(line);
            line.clear();
        }
        line += (line.empty() ? "" : " ") + word;
        start = end + 1;
    }
    if (!line.empty()) {
        lines.push_back(line);
    }
    return lines;
}

} // end anonymous namespace

std::string Controller::offsetText(int axis, double mm) const
{
    double value = m_model->toDisplayUnits(axis, mm);
    const int decimals = m_model->axisDecimals(axis) + 1;
    // (Not "-0.000")
    if (std::abs(value) < 0.5 * std::pow(10.0, -decimals)) {
        value = 0.0;
    }
    return fmt::format(
        "{} {:+.{}f} {}",
        m_model->config().read(axis == 1 ? "Axis1Label" : "Axis2Label", axis == 1 ? "Z" : "X"),
        value,
        decimals,
        m_model->axisUnitName(axis));
}

void Controller::chooseTool()
{
    if (!m_model->toolLibraryEnabled()) {
        // (As for any other key without a use)
        m_model->stopAllMotors();
        return;
    }
    if (const std::string why = m_model->toolChangeUnavailable(); !why.empty()) {
        auto lines = wrapText(why, DIALOG_LINE_LENGTH);
        lines.insert(lines.end(), { "", "Press a key" });
        pressAnyKey("Change tool", std::move(lines), [] { });
        return;
    }
    const ToolLibrary& library = m_model->toolLibrary();
    std::vector<std::string> items;
    std::vector<int> numbers;
    for (const Tool& t : library.tools()) {
        items.push_back(
            fmt::format(
                "T{} {}{}",
                t.number,
                t.description,
                t.number == library.current().number ? " (in use)" : ""));
        numbers.push_back(t.number);
    }
    // (Last, as it's used least: e.g. for a tool not measured yet)
    items.push_back(fmt::format("T? Unknown tool{}", library.currentIsKnown() ? "" : " (in use)"));
    numbers.push_back(ToolLibrary::UNKNOWN_TOOL);
    listPicker(
        "Which tool is in the toolpost?",
        std::move(items),
        [this, numbers](std::optional<std::size_t> picked) {
            if (!picked || *picked >= numbers.size()) {
                return;
            }
            const int number = numbers.at(*picked);
            // (Positions set with the unknown tool can't be shifted to the
            // new tool's, so what was set on the work with them goes)
            const auto [diameter, workEnd] = m_model->toolChangeClears(number);
            confirmFeatureLoss(
                "Changing from positions set with an unknown tool",
                diameter,
                workEnd,
                [this, number] { m_model->selectTool(number); });
        });
}

void Controller::confirmMeasureTool(int number, bool isNew)
{
    const std::string title = isNew ? fmt::format("Define a new tool, T{}", number)
                                    : fmt::format("Measure T{} again", number);
    if (const std::string why = m_model->toolMeasurementUnavailable(number); !why.empty()) {
        auto lines = wrapText(why, DIALOG_LINE_LENGTH);
        lines.insert(lines.end(), { "", "Press a key" });
        pressAnyKey(title, std::move(lines), [] { });
        return;
    }
    const Tool& from = m_model->toolLibrary().current();
    const std::string z = m_model->axisLabel(1);
    const std::string x = m_model->axisLabel(2);
    okCancel(
        title,
        { "Its offsets are measured from the tool in use,",
          fmt::format("T{} ({}), by touching the same", from.number, from.description),
          "surfaces with each: the work's diameter and end.",
          "",
          "ONLY EXTERNAL TOOLS can be measured so far",
          "(e.g. turning, facing or chamfering tools):",
          "not boring bars, which can't reach the outside.",
          "",
          "Then, pressing Enter after each touch:",
          fmt::format("1. T{} on the diameter, moving {} in", from.number, x),
          fmt::format("2. T{} on the end, moving {} towards the chuck", from.number, z),
          fmt::format("3. Change to T{}: on the same diameter", number),
          fmt::format("4. T{} on the same end", number),
          "",
          "Only scratch the surfaces: a cut would change",
          "them. The axes move as usual meanwhile.",
          "",
          "Enter (or Y): start   Esc (or N): cancel" },
        [this, number] { m_model->startToolMeasurement(number); });
}

void Controller::pickToolToMeasure()
{
    const ToolLibrary& library = m_model->toolLibrary();
    std::vector<std::string> items;
    std::vector<int> numbers;
    for (const Tool& t : library.tools()) {
        // (The tool in use is what the others are measured from)
        if (t.number == library.current().number) {
            continue;
        }
        items.push_back(fmt::format("T{} {}", t.number, t.description));
        numbers.push_back(t.number);
    }
    if (numbers.empty()) {
        return;
    }
    listPicker(
        "Measure which tool again?",
        std::move(items),
        [this, numbers](std::optional<std::size_t> picked) {
            if (picked && *picked < numbers.size()) {
                confirmMeasureTool(numbers.at(*picked), false);
            }
        });
}

void Controller::recordToolTouch()
{
    // (If it's refused, the warning says why)
    if (m_model->recordToolTouch() && m_model->toolTouchStep() == ToolTouchStep::Finished) {
        finishToolMeasurement();
    }
}

void Controller::finishToolMeasurement()
{
    const auto offsets = m_model->toolMeasurementOffsets();
    if (!offsets) {
        return;
    }
    const int number = m_model->toolMeasurementNumber();
    const std::string measured = fmt::format(
        "Its offsets from T{}: {}, {}",
        m_model->toolLibrary().current().number,
        offsetText(1, offsets->axis1Mm),
        offsetText(2, offsets->axis2Mm));
    m_model->stopAllMotors();
    if (const Tool* existing = m_model->toolLibrary().find(number)) {
        // Measured again: only its offsets change
        const Tool tool = *existing;
        m_view->openInput(
            Input::Type::OkCancel,
            fmt::format("T{} measured again", number),
            { measured,
              fmt::format(
                  "(They were {}, {},",
                  offsetText(1, tool.axis1OffsetMm),
                  offsetText(2, tool.axis2OffsetMm)),
              fmt::format(
                  "measured {}.)", tool.measured.empty() ? "on an unknown date" : tool.measured),
              "",
              fmt::format("Enter (or Y): keep them, with T{} in use", number),
              "Esc (or N): discard them" },
            [this, tool](const Input::Return& rc) {
                if (rc.cancelled) {
                    m_model->cancelToolMeasurement();
                    return;
                }
                m_model->finishToolMeasurement(tool.description, tool.noseRadiusMm);
            });
        return;
    }
    // A new tool: what it is, then its nose radius
    m_view->openInput(
        Input::Type::Text,
        fmt::format("T{} measured: describe it", number),
        { measured, "", "Enter: next (its nose radius)", "Esc: discard the measurement" },
        [this, number](const Input::Return& rc) {
            if (rc.cancelled) {
                m_model->cancelToolMeasurement();
                return;
            }
            const std::string description = rc.text;
            getNumericInput(
                fmt::format("T{}'s nose radius (mm)", number),
                { "0 for a sharp tool",
                  "",
                  fmt::format("Enter: store T{}, in use from now", number),
                  "Esc: discard the measurement" },
                0.0,
                "mm",
                [this, description](const NumericInputReturn& radius) {
                    if (radius.cancelled) {
                        m_model->cancelToolMeasurement();
                        return;
                    }
                    m_model->finishToolMeasurement(description, radius.value);
                });
        });
}

void Controller::describeCurrentTool()
{
    if (!m_model->toolLibraryEnabled() || m_model->isMeasuringTool()
        || !m_model->toolLibrary().currentIsKnown()) {
        return;
    }
    const Tool tool = m_model->toolLibrary().current();
    m_model->stopAllMotors();
    m_view->openInput(
        Input::Type::Text,
        fmt::format("T{}'s description", tool.number),
        { "Enter: next (its nose radius)   Esc: cancel" },
        [this, tool](const Input::Return& rc) {
            if (rc.cancelled) {
                return;
            }
            const std::string description = rc.text;
            getNumericInput(
                fmt::format("T{}'s nose radius (mm)", tool.number),
                { "0 for a sharp tool" },
                tool.noseRadiusMm,
                "mm",
                [this, tool, description](const NumericInputReturn& radius) {
                    if (!radius.cancelled) {
                        m_model->editTool(tool.number, description, radius.value);
                    }
                });
        },
        tool.description);
}

} // end namespace
