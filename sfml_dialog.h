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