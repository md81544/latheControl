#include "menu.h"

#include "keycodes.h"

#include <algorithm>

namespace mgo {

namespace {

// Navigation keys. The gamepad's D-pad produces w/a/s/d (and, with its
// "shift" button, W/A/S/D), and its Enter button produces Enter, so it can
// drive the menu too
bool isUp(int k)
{
    return k == key::UP || k == key::w || k == key::W;
}

bool isDown(int k)
{
    return k == key::DOWN || k == key::s || k == key::S;
}

bool isChoose(int k)
{
    return k == key::ENTER || k == key::RIGHT || k == key::d || k == key::D;
}

bool isBack(int k)
{
    return k == key::LEFT || k == key::a || k == key::A || k == key::ESC
        || k == key::BACKSPACE;
}

} // end anonymous namespace

Menu::Menu(std::vector<Command> commands)
    : m_commands(std::move(commands))
{
    for (const auto& c : m_commands) {
        if (std::find(m_categories.begin(), m_categories.end(), c.category)
            == m_categories.end()) {
            m_categories.push_back(c.category);
        }
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

Menu::Action Menu::handleKey(int k, const Model& model)
{
    if (k == key::None) {
        return {};
    }
    if (k == key::TAB) {
        close();
        return {};
    }
    if (isUp(k)) {
        if (m_selection > 0) {
            --m_selection;
        }
        return {};
    }
    if (isDown(k)) {
        if (m_selection + 1 < listSize()) {
            ++m_selection;
        }
        return {};
    }
    if (isBack(k)) {
        if (m_openCategory) {
            m_selection = *m_openCategory;
            m_openCategory.reset();
        } else {
            close();
        }
        return {};
    }
    if (isChoose(k)) {
        if (!m_openCategory) {
            if (!m_categories.empty()) {
                m_openCategory = m_selection;
                m_selection = 0;
            }
            return {};
        }
        const auto commands = currentCommands();
        if (m_selection < commands.size() && isAvailable(*commands.at(m_selection), model)) {
            close();
            return { Action::Type::Command, commands.at(m_selection)->key };
        }
        return {}; // not available at the moment
    }
    // Anything else (in particular Space, to stop all motors, and rapids)
    // carries on working as normal
    return { Action::Type::PassThrough, k };
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

std::size_t Menu::listSize() const
{
    return m_openCategory ? currentCommands().size() : m_categories.size();
}

} // namespace mgo
