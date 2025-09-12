#include "sfml_dialog.h"

// Note this has its own event loop so acts as a modal dialog

namespace mgo {

std::string getInputFromDialog(
    sf::RenderWindow& window,
    sf::Font& font,
    const std::string& prompt,
    const std::string& defaultEntry, /* ="" */
    InputType inputType /* = InputType::string */)
{
    // Save the existing main window's contents so we're not displaying on a black screen
    sf::Texture windowContent(sf::Vector2u(window.getSize().x, window.getSize().y));
    windowContent.update(window);

    sf::Vector2f screenCentre;
    screenCentre.x = window.getSize().x / 2.f;
    screenCentre.y = window.getSize().y / 2.f;

    // Background box
    sf::RectangleShape backgroundBox(sf::Vector2f(600, 300));
    backgroundBox.setFillColor({ 40, 40, 40 });
    backgroundBox.setOutlineColor({ 128, 128, 128 });
    backgroundBox.setOutlineThickness(2);
    backgroundBox.setPosition({ screenCentre.x - 300.f, screenCentre.y - 150.f });

    const auto bgPos = backgroundBox.getPosition();

    // Text and input box elements
    sf::Text promptText(font, prompt, 20);
    promptText.setFillColor(sf::Color::Green);
    promptText.setPosition({ bgPos.x + 25.f, bgPos.y + 20.f });

    sf::RectangleShape inputBox(sf::Vector2f(480, 40));
    inputBox.setFillColor(sf::Color::White);
    inputBox.setOutlineColor({ 192, 0, 0 });
    inputBox.setOutlineThickness(2);
    inputBox.setPosition({ bgPos.x + 20.f, bgPos.y + 80.f });

    sf::Text inputText(font, "", 20);
    inputText.setFillColor(sf::Color::Black);
    inputText.setPosition({ bgPos.x + 25.f, bgPos.y + 85.f });
    inputText.setString(defaultEntry + "_");

    std::string input = defaultEntry;
    bool enterPressed = false;
    bool firstKeyPress = true;

    // Dialog loop
    std::optional event = window.pollEvent(); // Clear the keypress that got us here
    while (!enterPressed) {
        for (;;) {
            event = window.pollEvent();
            if (!event.has_value()) {
                break;
            }
            if (event->is<sf::Event::Closed>()) {
                window.close();
            } else if (event->is<sf::Event::TextEntered>()) {
                auto keyPress = event->getIf<sf::Event::TextEntered>()->unicode;
                // If the user doesn't press enter then clear the entry
                if (keyPress != '\r' && keyPress != '\n' && firstKeyPress) {
                    input.clear();
                }
                if (keyPress == '\b') {
                    if (!input.empty()) {
                        input.pop_back();
                    }
                } else if (keyPress == '\r' || keyPress == '\n') {
                    enterPressed = true;
                } else if (keyPress > 31 && keyPress < 128) { // ASCII characters
                    auto c = static_cast<char>(keyPress);
                    if (inputType == InputType::numeric) {
                        if (std::isdigit(c) || c == '.' || (input.length() == 0 && c == '-')) {
                            firstKeyPress = false;
                            input += c;
                        }
                    } else {
                        firstKeyPress = false;
                        input += c;
                    }
                }
                inputText.setString(input + "_");
            } else if (event->is<sf::Event::KeyPressed>()) {
                if (event->getIf<sf::Event::KeyPressed>()->scancode
                    == sf::Keyboard::Scancode::Escape) { // Escape to cancel
                    return "";
                }
            }
        }

        // Clear and redraw the window
        window.clear(sf::Color::Black);
        sf::Sprite sprite(windowContent);
        sprite.setColor({ 120, 120, 120 }); // dim it a bit
        window.draw(sprite); // draw the static main window content

        window.draw(backgroundBox);
        window.draw(promptText);
        window.draw(inputBox);
        window.draw(inputText);
        window.display();
    }
    return input;
}

} // end namespace mgo