#pragma once

#include <SFML/Graphics.hpp>
#include <string>

namespace mgo {

enum class InputType {
    string,
    numeric
};

std::string getInputFromDialog(
    sf::RenderWindow& window,
    sf::Font& font,
    const std::string& prompt,
    const std::string& defaultEntry = "",
    InputType inputType = InputType::string,
    const std::string& additionalText1 = "",
    const std::string& additionalText2 = "",
    const std::string& additionalText3 = "",
    const std::string& additionalText4 = "");

} // end namespace mgo