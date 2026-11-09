#include "commands.h"

#include "configreader.h"
#include "keycodes.h"
#include "model.h"

#include <cctype>

#include <fmt/format.h>

namespace mgo {

namespace {

// Bits added to a key code by the axis leader keys (see
// Controller::processLeaderKeyModeKeyPress())
constexpr int AXIS1_LEADER_BIT = 0x1000;
constexpr int AXIS2_LEADER_BIT = 0x2000;
constexpr int ALL_AXES_LEADER_BIT = 0x4000;

// The name of a single key (without leader keys)
std::string keyName(int k)
{
    if (k >= key::a && k <= key::z) {
        return std::string(1, static_cast<char>(k));
    }
    if (k >= key::A && k <= key::Z) {
        return "Shift+" + std::string(1, static_cast<char>(k - key::A + key::a));
    }
    if (k >= key::ZERO && k <= key::NINE) {
        return std::string(1, static_cast<char>(k));
    }
    if (k >= key::F1 && k <= key::F12) {
        return fmt::format("F{}", k - key::F1 + 1);
    }
    switch (k) {
        case key::ENTER:
            return "Enter";
        case key::SPACE:
            return "Space";
        case key::ESC:
            return "Esc";
        case key::DELETE:
            return "Delete";
        case key::BACKSPACE:
            return "Backspace";
        case key::LEFT:
            return "Left";
        case key::RIGHT:
            return "Right";
        case key::UP:
            return "Up";
        case key::DOWN:
            return "Down";
        case key::ASTERISK:
            return "*";
        default:
            // Other keys are their own ASCII character ([ ] , . - = : \ |)
            if (k > 32 && k < 127) {
                return std::string(1, static_cast<char>(k));
            }
            return "?";
    }
}

std::string leaderKeyName(const IConfigReader& config, const std::string& setting, long defaultKey)
{
    const long k = static_cast<long>(config.readLong(setting, defaultKey));
    return keyName(static_cast<int>(k));
}

// Builds the command list, leaving out commands for disabled axes
class CommandListBuilder {
public:
    explicit CommandListBuilder(const IConfigReader& config)
        : m_axis1Enabled(!config.readBool("DisableAxis1", false))
        , m_axis2Enabled(!config.readBool("DisableAxis2", false))
    {
    }

    // axes: which axes the command needs (bit 0 = axis 1, bit 1 = axis 2)
    // hotkey: optional menu hotkey (lower case), or 0
    void add(
        const std::string& category,
        const std::string& description,
        int key,
        int axes,
        std::function<bool(const Model&)> available = nullptr,
        char hotkey = 0)
    {
        if (((axes & 1) && !m_axis1Enabled) || ((axes & 2) && !m_axis2Enabled)) {
            return;
        }
        Command c { category, description, key };
        if (available) {
            c.available = std::move(available);
        }
        c.hotkey = hotkey;
        m_commands.push_back(std::move(c));
    }

    std::vector<Command> take()
    {
        return std::move(m_commands);
    }

private:
    bool m_axis1Enabled;
    bool m_axis2Enabled;
    std::vector<Command> m_commands;
};

constexpr int AXIS1 = 1;
constexpr int AXIS2 = 2;
constexpr int BOTH = AXIS1 | AXIS2;
constexpr int NONE = 0;

} // end anonymous namespace

char categoryHotkey(const std::string& category)
{
    // Modes is M (it's likely to be the most used); the others use a letter
    // from their name not taken by another
    static const std::vector<std::pair<std::string, char>> hotkeys {
        { "Move", 'v' },
        { "Nudge", 'n' },
        { "Speed", 's' },
        { "Memory", 'e' },
        { "Position", 'p' },
        { "Modes", 'm' },
        { "Feature", 'f' },
        { "Axis locks", 'l' },
        { "System", 'y' },
    };
    for (const auto& [name, hotkey] : hotkeys) {
        if (name == category) {
            return hotkey;
        }
    }
    return 0;
}

namespace {

// Whether a command is only chosen from the menu (it has no shortcut)
bool menuOnly(int k)
{
    switch (k) {
        case key::f2s:
        case key::f2t:
        case key::f2p:
        case key::f2r:
        case key::f2o:
        case key::f2m:
        case key::clearFeature:
            return true;
        default:
            return false;
    }
}

} // end anonymous namespace

std::string shortcutText(int k, const IConfigReader& config)
{
    if (menuOnly(k)) {
        return "";
    }
    if (k & key::ALT) {
        return "Alt+" + keyName(k & ~key::ALT);
    }
    if (k & key::CTRL) {
        return "Ctrl+" + keyName(k & ~key::CTRL);
    }
    if (k & ALL_AXES_LEADER_BIT) {
        return "\\ " + keyName(k & ~ALL_AXES_LEADER_BIT);
    }
    if (k & AXIS2_LEADER_BIT) {
        return leaderKeyName(config, "Axis2Leader", 120) + " " + keyName(k & ~AXIS2_LEADER_BIT);
    }
    if (k & AXIS1_LEADER_BIT) {
        return leaderKeyName(config, "Axis1Leader", 122) + " " + keyName(k & ~AXIS1_LEADER_BIT);
    }
    return keyName(k);
}

std::string followUpKeyName(int k)
{
    return keyName(k & ~(AXIS1_LEADER_BIT | AXIS2_LEADER_BIT | ALL_AXES_LEADER_BIT));
}

std::vector<const Command*> commandsForLeader(const std::vector<Command>& commands, KeyMode mode)
{
    std::vector<const Command*> result;
    for (const auto& c : commands) {
        const int k = c.key;
        // (Alt and Ctrl combinations never follow a leader key)
        if (k & (key::ALT | key::CTRL)) {
            continue;
        }
        // (Nor do the menu-only commands, whose codes can look as if they
        // have an axis leader's bit set)
        if (menuOnly(k)) {
            continue;
        }
        bool follows = false;
        switch (mode) {
            case KeyMode::Axis1:
                follows = (k & AXIS1_LEADER_BIT) && !(k & (AXIS2_LEADER_BIT | ALL_AXES_LEADER_BIT));
                break;
            case KeyMode::Axis2:
                follows = (k & AXIS2_LEADER_BIT) && !(k & (AXIS1_LEADER_BIT | ALL_AXES_LEADER_BIT));
                break;
            case KeyMode::AxisAll:
                follows = (k & ALL_AXES_LEADER_BIT) && !(k & (AXIS1_LEADER_BIT | AXIS2_LEADER_BIT));
                break;
            case KeyMode::None:
                break;
        }
        if (follows) {
            result.push_back(&c);
        }
    }
    return result;
}

bool keyAllowedInCurrentMode(int k, const Model& model)
{
    // The mode means some keys are ignored, for instance in
    // threading, you cannot change the speed of the z-axis as
    // that would affect the thread pitch
    // Always allow certain keys:
    if (k == key::CtrlQ || k == key::ESC || k == key::ENTER) {
        return true;
    }
    // While a multi-pass is running (or paused between passes), anything else
    // moving an axis would upset it. Space stops it; the speeds can be changed
    // (Z's is used for the next cut); and the units shown. While it's paused,
    // X can be nudged: the next pass steps over from there. (Not the Alt
    // nudges: releasing Alt stops everything, which would end the multi-pass)
    if (model.getMode() == Mode::MultiPass && model.multiPass().inProgress()) {
        if (model.multiPass().state() == MultiPass::State::Paused
            && (k == key::w || k == key::W || k == key::s || k == key::S)) {
            return true;
        }
        switch (k) {
            case key::SPACE:
            case key::EQUALS:
            case key::MINUS:
            case key::ONE:
            case key::TWO:
            case key::THREE:
            case key::FOUR:
            case key::FIVE:
            case key::SIX:
            case key::SEVEN:
            case key::EIGHT:
            case key::NINE:
            case key::ZERO:
            case key::u:
            case key::U:
                return true;
            default:
                return false;
        }
    }
    // Don't allow any x movement when retracted
    if (model.getIsAxis2Retracted()
        && (k == key::UP || k == key::DOWN || k == key::W || k == key::w || k == key::s
            || k == key::S)) {
        return false;
    }
    if (model.getMode() == Mode::Setup) {
        return k == key::LEFT || k == key::RIGHT || k == key::UP || k == key::DOWN
            || k == key::SPACE;
    }
    return true;
}

bool isAvailable(const Command& command, const Model& model)
{
    return keyAllowedInCurrentMode(command.key, model) && command.available(model);
}

std::vector<Command> buildCommands(const IConfigReader& config, DisplayUnits units)
{
    const std::string a1 = config.read("Axis1Label", "Z");
    const std::string a2 = config.read("Axis2Label", "X");
    const std::string u1 = config.read("Axis1DisplayUnits", "mm");
    const std::string u2 = config.read("Axis2DisplayUnits", "mm");
    const bool imperial = units == DisplayUnits::Imperial;
    // An amount in mm on an axis (whose units are axisUnits), as displayed:
    // axes measured in mm are shown in the display units
    const auto amount = [imperial](double mm, const std::string& axisUnits) {
        if (axisUnits == "mm" && imperial) {
            return fmt::format("{:.4f}in", mm / MM_PER_INCH);
        }
        return fmt::format("{}{}", mm, axisUnits);
    };
    const auto speed = [imperial](double mmPerMin, const std::string& axisUnits) {
        if (axisUnits == "mm" && imperial) {
            return fmt::format("{:.3g} in/min", mmPerMin / MM_PER_INCH);
        }
        return fmt::format("{} {}/min", mmPerMin, axisUnits);
    };

    const auto axis1Free = [](const Model& m) { return !m.isAxisLocked(1); };
    const auto axis2Free = [](const Model& m) { return !m.isAxisLocked(2); };
    // Axis 1's speed is driven by the spindle when threading
    const auto axis1SpeedAdjustable
        = [](const Model& m) { return m.getMode() != Mode::Threading; };
    const auto speedPresetsAllowed
        = [](const Model& m) { return m.getMode() != Mode::Threading; };

    CommandListBuilder b(config);

    std::string c = "Move";
    b.add(c, fmt::format("Move {} left (again to stop)", a1), key::LEFT, AXIS1, axis1Free);
    b.add(c, fmt::format("Move {} right (again to stop)", a1), key::RIGHT, AXIS1, axis1Free);
    b.add(c, fmt::format("Move {} in (again to stop)", a2), key::UP, AXIS2, axis2Free);
    b.add(c, fmt::format("Move {} out (again to stop)", a2), key::DOWN, AXIS2, axis2Free);
    b.add(c, "Stop all motors", key::SPACE, NONE);

    c = "Nudge";
    b.add(c, fmt::format("{} left {}", a1, amount(0.05, u1)), key::a, AXIS1, axis1Free);
    b.add(c, fmt::format("{} left {}", a1, amount(0.01, u1)), key::A, AXIS1, axis1Free);
    b.add(c, fmt::format("{} left {}", a1, amount(0.1, u1)), key::AltA, AXIS1, axis1Free);
    b.add(c, fmt::format("{} right {}", a1, amount(0.05, u1)), key::d, AXIS1, axis1Free);
    b.add(c, fmt::format("{} right {}", a1, amount(0.01, u1)), key::D, AXIS1, axis1Free);
    b.add(c, fmt::format("{} right {}", a1, amount(0.1, u1)), key::AltD, AXIS1, axis1Free);
    b.add(c, fmt::format("{} in {}", a2, amount(0.05, u2)), key::w, AXIS2, axis2Free);
    b.add(c, fmt::format("{} in {}", a2, amount(0.01, u2)), key::W, AXIS2, axis2Free);
    b.add(c, fmt::format("{} in {}", a2, amount(0.1, u2)), key::AltW, AXIS2, axis2Free);
    b.add(c, fmt::format("{} out {}", a2, amount(0.05, u2)), key::s, AXIS2, axis2Free);
    b.add(c, fmt::format("{} out {}", a2, amount(0.01, u2)), key::S, AXIS2, axis2Free);
    b.add(c, fmt::format("{} out {}", a2, amount(0.1, u2)), key::AltS, AXIS2, axis2Free);

    c = "Speed";
    b.add(c, fmt::format("{} faster", a1), key::EQUALS, AXIS1, axis1SpeedAdjustable);
    b.add(c, fmt::format("{} slower", a1), key::MINUS, AXIS1, axis1SpeedAdjustable);
    const std::vector<double> axis1Defaults { 20.0, 40.0, 100.0, 250.0, 1'000.0 };
    for (int n = 0; n < 5; ++n) {
        const double presetSpeed
            = config.readDouble(fmt::format("Axis1SpeedPreset{}", n + 1), axis1Defaults.at(n));
        b.add(
            c,
            fmt::format("{} speed preset {}: {}", a1, n + 1, speed(presetSpeed, u1)),
            key::ONE + n,
            AXIS1,
            speedPresetsAllowed);
    }
    b.add(c, fmt::format("{} faster", a2), key::a2_EQUALS, AXIS2);
    b.add(c, fmt::format("{} slower", a2), key::a2_MINUS, AXIS2);
    const std::vector<double> axis2Defaults { 5.0, 20.0, 40.0, 80.0, 100.0 };
    const std::vector<int> axis2PresetKeys { key::SIX, key::SEVEN, key::EIGHT, key::NINE, key::ZERO };
    for (int n = 0; n < 5; ++n) {
        const double presetSpeed
            = config.readDouble(fmt::format("Axis2SpeedPreset{}", n + 1), axis2Defaults.at(n));
        b.add(
            c,
            fmt::format("{} speed preset {}: {}", a2, n + 1, speed(presetSpeed, u2)),
            axis2PresetKeys.at(n),
            AXIS2,
            speedPresetsAllowed);
    }

    c = "Memory";
    b.add(c, "Previous memory slot", key::LBRACKET, NONE);
    b.add(c, "Next memory slot", key::RBRACKET, NONE);
    b.add(c, fmt::format("Store {} and {} in slot", a1, a2), key::m, BOTH);
    b.add(c, fmt::format("Store {} in slot", a1), key::a1_m, AXIS1);
    b.add(c, fmt::format("Store {} in slot", a2), key::a2_m, AXIS2);
    b.add(c, fmt::format("Go to slot: {}", a1), key::a1_ENTER, AXIS1);
    b.add(c, fmt::format("Go to slot: {}", a2), key::a2_ENTER, AXIS2);
    b.add(c, fmt::format("Go to slot: {} and {}", a1, a2), key::aAll_ENTER, BOTH);
    b.add(c, fmt::format("Fast return to slot: {}", a1), key::a1_f, AXIS1);
    b.add(c, fmt::format("Fast return to slot: {}", a2), key::a2_f, AXIS2);
    b.add(c, fmt::format("Fast return to slot: {} and {}", a1, a2), key::aAll_f, BOTH);
    b.add(c, fmt::format("Enter a {} value for slot...", a1), key::a1_i, AXIS1);
    b.add(c, fmt::format("Enter a {} value for slot...", a2), key::a2_i, AXIS2);
    b.add(c, fmt::format("Clear slot: {}", a1), key::a1_DELETE, AXIS1);
    b.add(c, fmt::format("Clear slot: {}", a2), key::a2_DELETE, AXIS2);
    b.add(c, fmt::format("Clear slot: {} and {}", a1, a2), key::DELETE, BOTH);

    c = "Position";
    b.add(c, fmt::format("Zero {}", a1), key::a1_z, AXIS1);
    b.add(c, fmt::format("Zero {}", a2), key::a2_z, AXIS2);
    b.add(c, fmt::format("Zero {} and {}", a1, a2), key::aAll_z, BOTH);
    b.add(c, fmt::format("Set {} position...", a1), key::a1_s, AXIS1);
    b.add(c, fmt::format("Set {} position (or diameter)...", a2), key::a2_s, AXIS2);
    b.add(c, fmt::format("Go to {} position...", a1), key::a1_g, AXIS1);
    b.add(c, fmt::format("Go to {} position...", a2), key::a2_g, AXIS2);
    b.add(c, fmt::format("Move {} by an offset...", a1), key::a1_r, AXIS1);
    b.add(c, fmt::format("Move {} by an offset...", a2), key::a2_r, AXIS2);
    b.add(c, fmt::format("Repeat offset move: {}", a1), key::a1_FULLSTOP, AXIS1);
    b.add(c, fmt::format("Repeat offset move: {}", a2), key::a2_FULLSTOP, AXIS2);
    b.add(c, fmt::format("Repeat offset move: {} and {}", a1, a2), key::aAll_FULLSTOP, BOTH);
    b.add(c, fmt::format("Previous position: {}", a1), key::a1_l, AXIS1);
    b.add(c, fmt::format("Previous position: {}", a2), key::a2_l, AXIS2);
    b.add(c, fmt::format("Previous position: {} and {}", a1, a2), key::aAll_l, BOTH);
    b.add(c, fmt::format("Retract / unretract {}", a2), key::r, AXIS2);

    // (The controller requires axis 2 for these modes)
    // (Most have no shortcut, so are chosen here, by their hotkeys)
    c = "Modes";
    b.add(c, "Threading...", key::f2t, BOTH, nullptr, 't');
    b.add(c, "Taper...", key::f2p, BOTH, nullptr, 'p');
    b.add(c, "Quick chamfer (45 deg taper)", key::h, BOTH, nullptr, 'h');
    b.add(c, "Radius...", key::f2o, BOTH, nullptr, 'o');
    b.add(c, "Multi-pass (experimental)...", key::f2m, BOTH, nullptr, 'm');
    b.add(
        c,
        "Start / continue / pause multi-pass",
        key::ENTER,
        BOTH,
        [](const Model& m) { return m.getMode() == Mode::MultiPass; },
        'g');
    b.add(c, fmt::format("{} retraction direction...", a2), key::f2r, BOTH, nullptr, 'r');
    b.add(c, "Setup (measure backlash)", key::f2s, NONE, nullptr, 's');
    b.add(c, "Exit mode", key::ESC, NONE, nullptr, 'x');

    // The hotkeys are the axes' names (e.g. z and x on the lathe)
    const auto axisHotkey = [](const std::string& label) -> char {
        if (!label.empty() && std::isalpha(static_cast<unsigned char>(label.front()))) {
            return static_cast<char>(std::tolower(static_cast<unsigned char>(label.front())));
        }
        return 0;
    };
    const char a1Hotkey = axisHotkey(a1);
    const char a2Hotkey = axisHotkey(a2);
    if (config.readBool("CurrentFeature", false)) {
        c = "Feature";
        b.add(c, fmt::format("Mark the work's end ({} here)", a1), key::a1_e, BOTH, nullptr, 'e');
        b.add(
            c,
            "Record the diameter here as the feature's",
            key::a2_d,
            BOTH,
            [](const Model& m) { return m.getDiameterMm().has_value(); },
            'd');
        b.add(
            c,
            "Clear the feature",
            key::clearFeature,
            BOTH,
            [](const Model& m) {
                return m.getFeatureDiameterMm().has_value() || m.getWorkEndZ().has_value();
            },
            'c');
    }

    c = "Axis locks";
    b.add(c, fmt::format("Lock / unlock {}", a1), key::a1_k, AXIS1, nullptr, a1Hotkey);
    b.add(c,
          fmt::format("Lock / unlock {}", a2),
          key::a2_k,
          AXIS2,
          nullptr,
          a2Hotkey != a1Hotkey ? a2Hotkey : 0);

    c = "System";
    b.add(c, imperial ? "Show mm" : "Show inches", key::u, NONE, nullptr, 'u');
    b.add(c, "Quit", key::CtrlQ, NONE, nullptr, 'q');
    // In mock builds (FAKE) this is shown, so the menu looks the same as on
    // the Pi, but greyed out: shutting down isn't done in mock builds
#ifdef FAKE
    const auto shutdownAvailable = [](const Model&) { return false; };
#else
    const auto shutdownAvailable = [](const Model&) { return true; };
#endif
    b.add(c, "Shut down the computer", key::ASTERISK, NONE, shutdownAvailable, 'd');

    return b.take();
}

} // namespace mgo
