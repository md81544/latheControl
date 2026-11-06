#pragma once

#include <array>
#include <chrono>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace mgo {

// Joystick axes, named as SFML named them (the button and axis mappings here
// were originally written for SFML's joystick support)
enum class JoystickAxis {
    X, // left stick, horizontal
    Y, // left stick, vertical
    Z, // right stick, horizontal (on most gamepads)
    R, // right stick, vertical (on most gamepads)
    U,
    V,
    PovX, // D-pad, horizontal
    PovY, // D-pad, vertical
    Count
};

struct JoystickEvent {
    enum class Type {
        Button,
        Axis
    };
    Type type;
    // Button number, or static_cast<unsigned>(JoystickAxis)
    unsigned number;
    // Buttons: 1 pressed, 0 released. Axes: -100 to 100
    float value;
    // True for the events reporting the initial state when the device is opened
    bool initial { false };
};

// Converts joystick events into our key:: codes. This has no platform-specific
// code, so it can be tested anywhere.
class JoystickKeyMapper {
public:
    using Clock = std::chrono::steady_clock;

    // Pressing these two buttons together opens (or closes) the menu, rather
    // than doing what either does by itself. To tell a chord from a single
    // press, these buttons' own actions are held back until the button is
    // released, or for CHORD_WINDOW if it's held down
    void setMenuChord(unsigned button1, unsigned button2);
    static constexpr auto CHORD_WINDOW = std::chrono::milliseconds(150);

    // Processes a batch of events (i.e. all those that have arrived since the
    // last call; possibly none) and returns any resulting key codes. now is
    // the current time; this should be called regularly, even if there are
    // no events, so held-back chord buttons can be acted on
    std::vector<int> process(
        const std::vector<JoystickEvent>& events,
        Clock::time_point now = Clock::now());
    // Forget all state (e.g. when the joystick is disconnected)
    void reset();

private:
    int buttonKey(unsigned button) const;
    int stateKey();
    bool isChordButton(unsigned button) const;

    std::optional<std::pair<unsigned, unsigned>> m_menuChord;
    // A chord button whose own action is being held back, and since when
    std::optional<unsigned> m_pendingChordButton;
    Clock::time_point m_pendingSince {};

    std::array<bool, 32> m_buttons {};
    std::array<float, static_cast<std::size_t>(JoystickAxis::Count)> m_axes {};
    float m_previousX { 0.f };
    float m_previousY { 0.f };
    float m_previousZ { 0.f };
    float m_previousR { 0.f };
};

// Reads a Linux joystick device (e.g. /dev/input/js0) without blocking, and
// converts its events to key codes. If the device isn't present, or is
// unplugged, it periodically tries to (re)open it. On platforms other than
// Linux, this does nothing.
class Joystick {
public:
    // menuChord: two buttons which, pressed together, open the menu (none if
    // not set). See JoystickKeyMapper::setMenuChord()
    explicit Joystick(
        std::string devicePath,
        std::optional<std::pair<unsigned, unsigned>> menuChord = std::nullopt);
    ~Joystick();
    Joystick(const Joystick&) = delete;
    Joystick& operator=(const Joystick&) = delete;

    // Returns the key codes for any joystick events since the last call
    std::vector<int> poll();

private:
    void tryOpen();
    void closeDevice();

    std::string m_devicePath;
    [[maybe_unused]] int m_fd { -1 }; // (unused other than on Linux)
    // Maps the device's axis numbers to our axes (-1 = not used)
    std::vector<int> m_axisMap;
    std::chrono::steady_clock::time_point m_nextOpenAttempt {};
    JoystickKeyMapper m_mapper;
};

} // namespace mgo
