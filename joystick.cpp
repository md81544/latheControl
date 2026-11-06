#include "joystick.h"

#include "keycodes.h"
#include "log.h"

#include <cmath>

#ifdef __linux__
#include <cerrno>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/joystick.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace mgo {

namespace {

// Holding this button acts as a "shift" for the D-pad and left stick
constexpr unsigned JOYSTICK_SHIFT = 9;

} // end anonymous namespace

// ---------------------------------------------------------------------------
// JoystickKeyMapper
// ---------------------------------------------------------------------------

std::vector<int> JoystickKeyMapper::process(const std::vector<JoystickEvent>& events)
{
    std::vector<int> keys;
    bool anyLiveEvents = false;
    for (const auto& e : events) {
        if (e.type == JoystickEvent::Type::Button) {
            if (e.number < m_buttons.size()) {
                const bool pressed = e.value != 0.f;
                const bool wasPressed = m_buttons.at(e.number);
                m_buttons.at(e.number) = pressed;
                if (pressed && !wasPressed && !e.initial) {
                    const int k = buttonKey(e.number);
                    if (k != key::None) {
                        keys.push_back(k);
                    }
                }
            }
        } else if (e.number < m_axes.size()) {
            m_axes.at(e.number) = e.value;
        }
        if (!e.initial) {
            anyLiveEvents = true;
        }
    }
    // As with SFML, the sticks' state is only examined when there's been a
    // joystick event (so holding the D-pad nudges once, not continuously)
    if (anyLiveEvents) {
        const int k = stateKey();
        if (k != key::None) {
            keys.push_back(k);
        }
    }
    return keys;
}

void JoystickKeyMapper::reset()
{
    m_buttons.fill(false);
    m_axes.fill(0.f);
    m_previousX = 0.f;
    m_previousY = 0.f;
    m_previousZ = 0.f;
    m_previousR = 0.f;
}

int JoystickKeyMapper::buttonKey(unsigned button) const
{
    switch (button) {
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
            return key::a1_f;
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

int JoystickKeyMapper::stateKey()
{
    const auto axis = [this](JoystickAxis a) { return m_axes.at(static_cast<std::size_t>(a)); };
    const bool shift = m_buttons.at(JOYSTICK_SHIFT);

    // Small Nudge via D-pad
    {
        constexpr float deadzone = 50.f;
        const float value = axis(JoystickAxis::PovX);
        if (value > deadzone) {
            return shift ? key::D : key::d;
        }
        if (value < -deadzone) {
            return shift ? key::A : key::a;
        }
    }
    {
        constexpr float deadzone = 40.f;
        const float value = axis(JoystickAxis::PovY);
        if (value > deadzone) {
            return shift ? key::S : key::s;
        }
        if (value < -deadzone) {
            return shift ? key::W : key::w;
        }
    }
    // Normal move via left stick. (Only acts when the stick is first pushed
    // fully over, not while it's held there)
    {
        constexpr float deadzone = 99.f;
        const float value = axis(JoystickAxis::X);
        if (!(m_previousX > deadzone && value > deadzone)
            && !(m_previousX < -deadzone && value < -deadzone)) {
            m_previousX = value;
            if (value > deadzone) {
                return key::js_right;
            }
            if (value < -deadzone) {
                return key::js_left;
            }
        }
    }
    {
        constexpr float deadzone = 99.f;
        const float value = axis(JoystickAxis::Y);
        if (!(m_previousY > deadzone && value > deadzone)
            && !(m_previousY < -deadzone && value < -deadzone)) {
            m_previousY = value;
            if (value > deadzone) {
                // "shift" plus down = retract
                if (shift) {
                    return key::r;
                }
                return key::js_down;
            }
            if (value < -deadzone) {
                return key::js_up;
            }
        }
    }
    // Rapid motion via right stick. Letting go of the stick stops the motion
    // (a "dead man" control)
    {
        constexpr float deadzone = 99.f;
        const float value = axis(JoystickAxis::Z);
        if (std::abs(m_previousZ) > deadzone && std::abs(value) <= deadzone) {
            // stop rapid
            m_previousZ = value;
            return key::SPACE;
        }
        if (!(m_previousZ > deadzone && value > deadzone)
            && !(m_previousZ < -deadzone && value < -deadzone)) {
            m_previousZ = value;
            if (value > deadzone) {
                return key::js_rapid_right;
            }
            if (value < -deadzone) {
                return key::js_rapid_left;
            }
        }
    }
    {
        constexpr float deadzone = 99.f;
        const float value = axis(JoystickAxis::R);
        if (std::abs(m_previousR) > deadzone && std::abs(value) <= deadzone) {
            // stop rapid
            m_previousR = value;
            return key::SPACE;
        }
        if (!(m_previousR > deadzone && value > deadzone)
            && !(m_previousR < -deadzone && value < -deadzone)) {
            m_previousR = value;
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

// ---------------------------------------------------------------------------
// Joystick (Linux joystick device)
// ---------------------------------------------------------------------------

Joystick::Joystick(std::string devicePath)
    : m_devicePath(std::move(devicePath))
{
}

Joystick::~Joystick()
{
    closeDevice();
}

#ifdef __linux__

void Joystick::tryOpen()
{
    constexpr auto retryInterval = std::chrono::seconds(2);
    const auto now = std::chrono::steady_clock::now();
    if (now < m_nextOpenAttempt) {
        return;
    }
    m_nextOpenAttempt = now + retryInterval;
    m_fd = ::open(m_devicePath.c_str(), O_RDONLY | O_NONBLOCK);
    if (m_fd < 0) {
        return; // not plugged in; try again later
    }
    // The driver numbers the axes it has consecutively; the axis map tells us
    // which physical axis (ABS_X etc) each one is. We name them as SFML did.
    __u8 axisCount = 0;
    ::ioctl(m_fd, JSIOCGAXES, &axisCount);
    std::array<__u8, ABS_CNT> axisMap {};
    ::ioctl(m_fd, JSIOCGAXMAP, axisMap.data());
    m_axisMap.assign(axisCount, -1);
    for (unsigned n = 0; n < axisCount && n < axisMap.size(); ++n) {
        JoystickAxis axis = JoystickAxis::Count;
        switch (axisMap.at(n)) {
            case ABS_X:
                axis = JoystickAxis::X;
                break;
            case ABS_Y:
                axis = JoystickAxis::Y;
                break;
            case ABS_Z:
            case ABS_THROTTLE:
                axis = JoystickAxis::Z;
                break;
            case ABS_RZ:
            case ABS_RUDDER:
                axis = JoystickAxis::R;
                break;
            case ABS_RX:
                axis = JoystickAxis::U;
                break;
            case ABS_RY:
                axis = JoystickAxis::V;
                break;
            case ABS_HAT0X:
                axis = JoystickAxis::PovX;
                break;
            case ABS_HAT0Y:
                axis = JoystickAxis::PovY;
                break;
            default:
                break;
        }
        if (axis != JoystickAxis::Count) {
            m_axisMap.at(n) = static_cast<int>(axis);
        }
    }
    std::array<char, 128> name {};
    ::ioctl(m_fd, JSIOCGNAME(name.size() - 1), name.data());
    MGOLOG("Joystick connected: " << name.data() << " (" << m_devicePath << ")");
}

void Joystick::closeDevice()
{
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}

std::vector<int> Joystick::poll()
{
    if (m_fd < 0) {
        tryOpen();
        if (m_fd < 0) {
            return {};
        }
    }
    std::vector<JoystickEvent> events;
    js_event e {};
    for (;;) {
        const ssize_t n = ::read(m_fd, &e, sizeof(e));
        if (n == static_cast<ssize_t>(sizeof(e))) {
            const bool initial = e.type & JS_EVENT_INIT;
            const auto type = e.type & ~JS_EVENT_INIT;
            if (type == JS_EVENT_BUTTON) {
                events.push_back({ JoystickEvent::Type::Button, e.number, e.value ? 1.f : 0.f, initial });
            } else if (type == JS_EVENT_AXIS && e.number < m_axisMap.size()
                       && m_axisMap.at(e.number) >= 0) {
                // Scaled to -100 to 100, as SFML did
                events.push_back({ JoystickEvent::Type::Axis,
                                   static_cast<unsigned>(m_axisMap.at(e.number)),
                                   e.value * 100.f / 32767.f,
                                   initial });
            }
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break; // no more events for now
        }
        // The device has gone (e.g. unplugged). Stop any motion it may have
        // started, as we'll no longer see the stick being released
        MGOLOG("Joystick disconnected (" << m_devicePath << ")");
        closeDevice();
        auto keys = m_mapper.process(events);
        m_mapper.reset();
        keys.push_back(key::SPACE);
        return keys;
    }
    return m_mapper.process(events);
}

#else // not Linux: no joystick support

void Joystick::tryOpen() { }

void Joystick::closeDevice() { }

std::vector<int> Joystick::poll()
{
    return {};
}

#endif

} // namespace mgo
