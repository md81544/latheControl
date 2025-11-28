#pragma once

#include <SFML/Graphics.hpp>
#include <string>

namespace mgo {
namespace dialog {

enum class InputType {
    string,
    numeric
};

std::tuple<std::string, std::string> getInput(
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

void pressAnyKey(
    sf::RenderWindow& window,
    sf::Font& font,
    std::string_view prompt = "Press any key to continue");

} // end namespace dialog
} // end namespace mgo