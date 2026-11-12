#include "menu.h"

#include "keycodes.h"

#include <algorithm>

namespace mgo {

namespace {

// Navigation keys. The gamepad's D-pad produces w/a/s/d (and, with its
// "shift" button, W/A/S/D), and its Enter button produces Enter, so it can
// drive the menu too. (From the keyboard, those letters are hotkeys instead)
bool isUp(int k, bool fromJoystick)
{
    return k == key::UP || (fromJoystick && (k == key::w || k == key::W));
}

bool isDown(int k, bool fromJoystick)
{
    return k == key::DOWN || (fromJoystick && (k == key::s || k == key::S));
}

bool isChoose(int k, bool fromJoystick)
{
    return k == key::ENTER || k == key::RIGHT || (fromJoystick && (k == key::d || k == key::D));
}

bool isBack(int k, bool fromJoystick)
{
    return k == key::LEFT || k == key::ESC || k == key::BACKSPACE
        || (fromJoystick && (k == key::a || k == key::A));
}

// A plain or shifted letter (not Ctrl or Alt + letter), as a lower case char,
// or 0 if not a letter
char letter(int k)
{
    if (k >= key::a && k <= key::z) {
        return static_cast<char>(k);
    }
    if (k >= key::A && k <= key::Z) {
        return static_cast<char>(k - key::A + key::a);
    }
    return 0;
}

} // end anonymous namespace

Menu::Menu(std::vector<Command> commands)
    : m_commands(std::move(commands))
{
    for (const auto& c : m_commands) {
        if (std::find(m_categories.begin(), m_categories.end(), c.category) == m_categories.end()) {
            m_categories.push_back(c.category);
        }
    }
}

void Menu::setCommands(std::vector<Command> commands)
{
    m_commands = std::move(commands);
    // (In case a category has fewer commands than before)
    const std::size_t size = listSize();
    if (m_selection >= size) {
        m_selection = size > 0 ? size - 1 : 0;
    }
}

bool Menu::isOpen() const
{
    return m_open;
}

void Menu::open()
{
    m_open = true;
    m_openCategory.reset();
    m_selection = 0;
}

void Menu::close()
{
    m_open = false;
}

Menu::Action Menu::handleKey(int k, const Model& model, bool fromJoystick)
{
    if (k == key::None) {
        return {};
    }
    if (k == key::TAB) {
        close();
        return {};
    }
    // Up from the top item goes round to the bottom, and Down from the
    // bottom to the top
    if (isUp(k, fromJoystick)) {
        if (const std::size_t size = listSize(); size > 0) {
            m_selection = m_selection > 0 ? m_selection - 1 : size - 1;
        }
        return {};
    }
    if (isDown(k, fromJoystick)) {
        if (const std::size_t size = listSize(); size > 0) {
            m_selection = m_selection + 1 < size ? m_selection + 1 : 0;
        }
        return {};
    }
    if (const char c = letter(k); c && !fromJoystick) {
        return handleHotkey(c, model);
    }
    if (isBack(k, fromJoystick)) {
        if (m_openCategory) {
            m_selection = *m_openCategory;
            m_openCategory.reset();
        } else {
            close();
        }
        return {};
    }
    if (isChoose(k, fromJoystick)) {
        if (!m_openCategory) {
            if (!m_categories.empty()) {
                m_openCategory = m_selection;
                m_selection = 0;
            }
            return {};
        }
        const auto commands = currentCommands();
        // On a toggle, Enter is done with the menu, leaving the setting as
        // it is (its hotkey or Right toggles it). From the gamepad too, whose
        // D-pad right toggles it
        if (m_selection < commands.size() && commands.at(m_selection)->isToggle
            && k == key::ENTER) {
            close();
            return {};
        }
        if (m_selection < commands.size() && isAvailable(*commands.at(m_selection), model)) {
            if (!commands.at(m_selection)->isToggle) {
                close();
            }
            return { Action::Type::Command, commands.at(m_selection)->key };
        }
        return {}; // not available at the moment
    }
    // Anything else (in particular Space, to stop all motors, and rapids)
    // carries on working as normal
    return { Action::Type::PassThrough, k };
}

Menu::Action Menu::handleHotkey(char c, const Model& model)
{
    // While the menu is open, letters typed are its hotkeys: a letter which
    // isn't one does nothing (rather than its usual shortcut)
    if (!m_openCategory) {
        for (std::size_t n = 0; n < m_categories.size(); ++n) {
            if (categoryHotkey(m_categories.at(n)) == c) {
                m_openCategory = n;
                m_selection = 0;
                break;
            }
        }
        return {};
    }
    const auto commands = currentCommands();
    for (std::size_t n = 0; n < commands.size(); ++n) {
        if (commands.at(n)->hotkey == c) {
            m_selection = n;
            if (isAvailable(*commands.at(n), model)) {
                if (!commands.at(n)->isToggle) {
                    close();
                }
                return { Action::Type::Command, commands.at(n)->key };
            }
            break; // (not available at the moment; left highlighted)
        }
    }
    return {};
}

const std::vector<std::string>& Menu::categories() const
{
    return m_categories;
}

std::optional<std::size_t> Menu::openCategory() const
{
    return m_openCategory;
}

std::size_t Menu::selection() const
{
    return m_selection;
}

std::vector<const Command*> Menu::currentCommands() const
{
    std::vector<const Command*> result;
    if (m_openCategory) {
        const std::string& category = m_categories.at(*m_openCategory);
        for (const auto& c : m_commands) {
            if (c.category == category) {
                result.push_back(&c);
            }
        }
    }
    return result;
}

const std::vector<Command>& Menu::commands() const
{
    return m_commands;
}

std::size_t Menu::listSize() const
{
    return m_openCategory ? currentCommands().size() : m_categories.size();
}

} // namespace mgo
