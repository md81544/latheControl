#pragma once

#include "iview.h"

#include <SFML/Audio.hpp>
#include <SFML/Graphics.hpp>

#include <memory>

namespace mgo
{

class ViewSfml : public IView
{
public:
    virtual void initialise() override;
    virtual void close() override;
    virtual int getInput() override;
    virtual void updateDisplay( const Model& ) override;
    // Non-overrides:
    void updateTextFromModel( const Model& );
private:
    std::unique_ptr<sf::RenderWindow> m_window;
    std::unique_ptr<sf::Font> m_font;

    // Main text items which are always displayed:
    std::unique_ptr<sf::Text> m_txtZPos;
    std::unique_ptr<sf::Text> m_txtZSpeed;
    std::unique_ptr<sf::Text> m_txtXPos;
    std::unique_ptr<sf::Text> m_txtXSpeed;
    std::unique_ptr<sf::Text> m_txtRpm;
    std::unique_ptr<sf::Text> m_txtStatus;
    std::unique_ptr<sf::Text> m_txtWarning;
    std::unique_ptr<sf::Text> m_txtTaperAngle;

    // Text items which are displayed sometimes:
    std::unique_ptr<sf::Text> m_txtMode;
    std::unique_ptr<sf::Text> m_txtMisc1;
    std::unique_ptr<sf::Text> m_txtMisc2;
    std::unique_ptr<sf::Text> m_txtMisc3;
    std::unique_ptr<sf::Text> m_txtMisc4;
    std::unique_ptr<sf::Text> m_txtMisc5;

    std::unique_ptr<sf::Text> m_txtNotification;
    std::unique_ptr<sf::Text> m_txtXRetract;

    std::vector<std::unique_ptr<sf::Text>> m_txtMemoryLabel;
    std::vector<std::unique_ptr<sf::Text>> m_txtMemoryValue;
};

} // namespace mgo