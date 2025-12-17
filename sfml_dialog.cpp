#include "sfml_dialog.h"

#include <unordered_map>

// Note this has its own event loop so acts as a modal dialog

namespace {
char convertScanCodeToChar(sf::Keyboard::Scancode scancode)
{
    // Scancodes can't readily be converted to a char hence
    // this...
    switch (scancode) {
        case sf::Keyboard::Scancode::A:
            return 'a';
        case sf::Keyboard::Scancode::B:
            return 'b';
        case sf::Keyboard::Scancode::C:
            return 'c';
        case sf::Keyboard::Scancode::D:
            return 'd';
        case sf::Keyboard::Scancode::E:
            return 'e';
        case sf::Keyboard::Scancode::F:
            return 'f';
        case sf::Keyboard::Scancode::G:
            return 'g';
        case sf::Keyboard::Scancode::H:
            return 'h';
        case sf::Keyboard::Scancode::I:
            return 'i';
        case sf::Keyboard::Scancode::J:
            return 'j';
        case sf::Keyboard::Scancode::K:
            return 'k';
        case sf::Keyboard::Scancode::L:
            return 'l';
        case sf::Keyboard::Scancode::M:
            return 'm';
        case sf::Keyboard::Scancode::N:
            return 'n';
        case sf::Keyboard::Scancode::O:
            return 'o';
        case sf::Keyboard::Scancode::P:
            return 'p';
        case sf::Keyboard::Scancode::Q:
            return 'q';
        case sf::Keyboard::Scancode::R:
            return 'r';
        case sf::Keyboard::Scancode::S:
            return 's';
        case sf::Keyboard::Scancode::T:
            return 't';
        case sf::Keyboard::Scancode::U:
            return 'u';
        case sf::Keyboard::Scancode::V:
            return 'v';
        case sf::Keyboard::Scancode::W:
            return 'w';
        case sf::Keyboard::Scancode::X:
            return 'x';
        case sf::Keyboard::Scancode::Y:
            return 'y';
        case sf::Keyboard::Scancode::Z:
            return 'z';
        default:
            return 0;
    }
}
} // anonymous namespace

namespace mgo {
namespace dialog {

Input::Return getInput(
    sf::RenderWindow& window,
    sf::Font& font,
    Input::Type type,
    std::string_view prompt,
    std::vector<std::string> additionalText,
    std::string_view defaultEntry,
    std::optional<std::vector<std::string>> listItems)
{
    // Parameter checks, this shouldn't be an issue
    // as this function is designed to be called from
    // more specialised wrappers in controller. But just
    // to check...
    if (additionalText.size() > 0) {
        assert(!listItems.has_value()); // Can't have both
    }
    if (listItems.has_value()) {
        assert(additionalText.size() == 0);
    }

    // Save the existing main window's contents so we're not displaying on a black screen
    sf::Texture windowContent(sf::Vector2u(window.getSize().x, window.getSize().y));
    windowContent.update(window);

    sf::Vector2f screenCentre;
    screenCentre.x = window.getSize().x / 2.f;
    screenCentre.y = window.getSize().y / 2.f;

    float width = 550.f;
    float height = 60.f;

    // Create all the text items first so we can determine what size
    // we need for the background box:
    constexpr unsigned bodyTextFontSize = 16;
    constexpr float lineHeight = 16.f;
    if (type == Input::Type::Text || type == Input::Type::Numeric) {
        // Add space for the input box
        height += 120.f;
    }
    std::vector<char> hotkeys;
    std::vector<sf::Text> addText;
    std::unordered_map<std::size_t, char> hotkeyMap;
    std::size_t addTextCounter = 0;
    for (auto& a : additionalText) {
        // "Hotkeys" are defined as & + a non-whitespace character in
        // the string
        auto pos = a.find("&");
        if (pos != a.npos) {
            if (pos < a.size() - 1 && !(std::isspace(a.at(pos + 1)))) {
                char c = std::tolower(a.at(pos + 1));
                hotkeys.push_back(c);
                hotkeyMap[addTextCounter] = c;
            }
            a.erase(pos, 1);
        }
        sf::Text t(font, std::string(prompt), bodyTextFontSize);
        t.setString(a);
        auto sz = t.getGlobalBounds().size;
        if (sz.x + 50.f > width) {
            width = sz.x + 50.f;
        }
        height += sz.y + lineHeight;
        addText.push_back(t);
        ++addTextCounter;
    }
    constexpr unsigned maxListItemsInView = 8;
    std::size_t currentScrollPosition = 0;
    std::vector<sf::Text> listText;
    constexpr sf::Color addTextColour = { 192, 192, 192 };
    unsigned liCount = 0;
    if (listItems.has_value()) {
        for (const auto& li : listItems.value()) {
            sf::Text t(font, std::string(prompt), bodyTextFontSize);
            t.setString(li);
            auto sz = t.getGlobalBounds().size;
            if (sz.x + 50.f > width) {
                width = sz.x + 50.f;
            }
            if (liCount < maxListItemsInView) {
                height += sz.y + lineHeight;
            }
            t.setFillColor(addTextColour);
            listText.push_back(t);
            ++liCount;
        }
        height += 25.f; // Bit more for bottom margin
    }

    sf::Text promptText(font, std::string(prompt), 20);
    auto promptWidth = promptText.getGlobalBounds().size.x + 50.f;
    if (promptWidth > width) {
        width = promptWidth;
    }

    // Background box
    sf::RectangleShape backgroundBox(sf::Vector2f(width, height));

    backgroundBox.setFillColor({ 40, 40, 40 });
    backgroundBox.setOutlineColor({ 128, 128, 128 });
    backgroundBox.setOutlineThickness(2);
    backgroundBox.setPosition({ screenCentre.x - width / 2.f, screenCentre.y - height / 2.f });

    const auto bgPos = backgroundBox.getPosition();

    // Text and input box elements
    promptText.setFillColor(sf::Color::Green);
    promptText.setPosition({ bgPos.x + 25.f, bgPos.y + 20.f });

    sf::RectangleShape inputBox(sf::Vector2f(width - 50.f, 40.f));
    inputBox.setFillColor(sf::Color::White);
    inputBox.setOutlineColor({ 192, 0, 0 });
    inputBox.setOutlineThickness(2);
    inputBox.setPosition({ bgPos.x + 20.f, bgPos.y + 60.f });

    sf::Text inputText(font, "", 20);
    inputText.setFillColor(sf::Color::Black);
    inputText.setPosition({ bgPos.x + 25.f, bgPos.y + 65.f });
    inputText.setString(std::string(defaultEntry) + "_");

    float addTextYPos = 140.f;
    if (type == Input::Type::PressAnyKey) {
        addTextYPos = 60.f;
    }
    constexpr float lineSpacing = 28.f;

    for (auto& t : addText) {
        t.setFillColor(addTextColour);
        t.setPosition({ bgPos.x + 25.f, bgPos.y + addTextYPos });
        addTextYPos += lineSpacing;
    }

    sf::Texture downArrowTexture;
    if (!downArrowTexture.loadFromFile("img/downArrow.png")) {
        MGOLOG("Could not load img/downArrow.png");
        throw std::runtime_error("Could not load img/downArrow.png");
    }
    sf::Sprite downArrow(downArrowTexture);
    sf::Texture upArrowTexture;
    if (!upArrowTexture.loadFromFile("img/upArrow.png")) {
        MGOLOG("Could not load img/upArrow.png");
        throw std::runtime_error("Could not load img/upArrow.png");
    }
    sf::Sprite upArrow(upArrowTexture);

    std::string input = std::string(defaultEntry);
    bool enterPressed = false;
    bool firstKeyPress = true;
    std::size_t currentListSelection = 0; // for list picker
    Input::Return rc {};

    // Dialog loop
    std::optional event = window.pollEvent(); // Clear the keypress that got us here
    while (!enterPressed) {
        for (;;) {
            event = window.pollEvent();
            if (!event.has_value()) {
                break;
            }
            if (auto e = event->getIf<sf::Event::TextEntered>()) {
                auto keyPress = e->unicode;
                char c = static_cast<char>(keyPress);
                // If the user doesn't press enter or uses Alt then clear the entry
                if (firstKeyPress && keyPress != '\r' && keyPress != '\n'
                    && !sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RAlt)
                    && !sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LAlt)) {
                    {
                        input.clear();
                    }
                }
                if (keyPress == '\b') {
                    if (!input.empty()) {
                        input.pop_back();
                    }
                } else if (keyPress == '\r' || keyPress == '\n') {
                    enterPressed = true;
                } else if (keyPress > 31 && keyPress < 128) { // ASCII characters
                    if (type == Input::Type::Numeric) {
                        if (std::isdigit(c) || c == '.' || (input.length() == 0 && c == '-')) {
                            firstKeyPress = false;
                            input += c;
                        }
                    } else if (type == Input::Type::Text) {
                        firstKeyPress = false;
                        input += c;
                    } else if (type == Input::Type::PressAnyKey) {
                        enterPressed = true;
                    }
                }
                inputText.setString(input + "_");
            } else if (auto e = event->getIf<sf::Event::KeyPressed>()) {
                if (e->scancode == sf::Keyboard::Scancode::Escape) { // Escape to cancel
                    return Input::Return();
                }
                if (e->scancode == sf::Keyboard::Scancode::Down
                    && currentListSelection < listText.size() - 1) {
                    ++currentListSelection;
                    if (currentListSelection >= currentScrollPosition + maxListItemsInView) {
                        ++currentScrollPosition;
                    }
                }
                if (e->scancode == sf::Keyboard::Scancode::Up && currentListSelection > 0) {
                    --currentListSelection;
                    if (currentListSelection < currentScrollPosition) {
                        --currentScrollPosition;
                    }
                }
                if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LAlt)
                    || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RAlt)) {
                    if (char charPressed = convertScanCodeToChar(e->scancode)) {
                        if (std::find(hotkeys.begin(), hotkeys.end(), charPressed)
                            != hotkeys.end()) {
                            if (rc.optionsSelected.contains(charPressed)) {
                                rc.optionsSelected.erase(charPressed);
                            } else {
                                rc.optionsSelected.insert(charPressed);
                            }
                        }
                    }
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
        if (type == Input::Type::Numeric || type == Input::Type::Text) {
            window.draw(inputBox);
            window.draw(inputText);
        }
        addTextCounter = 0;
        for (auto& t : addText) {
            window.draw(t);
            auto it = hotkeyMap.find(addTextCounter);
            if (it != hotkeyMap.end()) {
                if (rc.optionsSelected.contains(it->second)) {
                    sf::Texture tx;
                    if (!tx.loadFromFile("img/tick.png")) {
                        MGOLOG("Could not load img/tick.png");
                    } else {
                        sf::Sprite s(tx);
                        auto p = t.getPosition();
                        s.setPosition({ p.x - 18.f, p.y });
                        window.draw(s);
                    }
                }
            }
            ++addTextCounter;
        }
        std::size_t count = 0;
        float listPickerYPos = 65.f;
        if (currentScrollPosition > 0) {
            upArrow.setPosition({ bgPos.x + 25.f, bgPos.y + listPickerYPos });
            window.draw(upArrow);
        }
        for (auto& li : listText) {
            if (count >= currentScrollPosition
                && count < currentScrollPosition + maxListItemsInView) {
                if (count == currentListSelection) {
                    li.setStyle(sf::Text::Bold);
                    li.setFillColor(sf::Color::White);
                } else {
                    li.setStyle(sf::Text::Regular);
                    li.setFillColor(addTextColour);
                }
                li.setPosition({ bgPos.x + 45.f, bgPos.y + listPickerYPos });
                listPickerYPos += lineSpacing;
                window.draw(li);
            }
            if (count >= currentScrollPosition + maxListItemsInView) {
                downArrow.setPosition({ bgPos.x + 25.f, bgPos.y + listPickerYPos - lineSpacing });
                window.draw(downArrow);
            }
            ++count;
        }
        window.display();
    }
    rc.cancelled = false;
    rc.pickedItem = currentListSelection;
    rc.text = input;
    if (type == Input::Type::Numeric) {
        try {
            rc.number = std::stod(input);
        } catch (...) {
            // Just leave as zero. We should never get here as the user is constrained
            // to enter just numeric characters in numeric entry
        }
    }
    return rc;
}

void pressAnyKey(sf::RenderWindow& window, sf::Font& font, std::string_view prompt)
{
    // Save the existing main window's contents so we're not displaying on a black screen
    sf::Texture windowContent(sf::Vector2u(window.getSize().x, window.getSize().y));
    windowContent.update(window);

    sf::Vector2f screenCentre;
    screenCentre.x = window.getSize().x / 2.f;
    screenCentre.y = window.getSize().y / 2.f;

    // Background box
    sf::RectangleShape backgroundBox(sf::Vector2f(400, 100));
    backgroundBox.setFillColor({ 40, 40, 40 });
    backgroundBox.setOutlineColor({ 128, 128, 128 });
    backgroundBox.setOutlineThickness(2);
    backgroundBox.setPosition(
        { screenCentre.x - backgroundBox.getSize().x / 2, screenCentre.y - 150.f });

    const auto bgPos = backgroundBox.getPosition();

    // Text and input box elements
    sf::Text promptText(font, std::string(prompt), 20);
    promptText.setFillColor(sf::Color::White);
    promptText.setPosition({ bgPos.x + 25.f, bgPos.y + 20.f });

    // Dialog loop
    std::optional event = window.pollEvent(); // Clear the keypress that got us here
    for (;;) {
        event = window.pollEvent();
        if (!event.has_value()) {
            continue;
        }
        if (event->is<sf::Event::KeyPressed>()) {
            return;
        }
        // Clear and redraw the window
        window.clear(sf::Color::Black);
        sf::Sprite sprite(windowContent);
        sprite.setColor({ 120, 120, 120 }); // dim it a bit
        window.draw(sprite); // draw the static main window content

        window.draw(backgroundBox);
        window.draw(promptText);
        window.display();
    }
}

} // end namespace dialog
} // end namespace mgo