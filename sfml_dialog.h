#pragma once
#include "iview.h" // for Input::*
#include <SFML/Graphics.hpp>
#include <string>

namespace mgo {
namespace dialog {

enum class InputType {
    string,
    numeric
};

std::tuple<std::string, std::string> getInputOld(
    sf::RenderWindow& window,
    sf::Font& font,
    const std::string& prompt,
    const std::string& defaultEntry = "",
    InputType inputType = InputType::string,
    const std::string& additionalText1 = "",
    const std::string& additionalText2 = "",
    const std::string& additionalText3 = "",
    const std::string& additionalText4 = "",
    const std::string& hotkeys = "");

Input::Return getInput(
    sf::RenderWindow& window,
    sf::Font& font,
    Input::Type type,
    std::string_view prompt,
    std::vector<std::string> additionalText,
    std::string_view defaultEntry,
    std::optional<std::vector<std::string>> listItems = std::nullopt);

void pressAnyKey(
    sf::RenderWindow& window,
    sf::Font& font,
    std::string_view prompt = "Press any key to continue");

} // end namespace dialog
} // end namespace mgo