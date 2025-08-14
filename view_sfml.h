#pragma once

#include "iview.h"

#include <SFML/Audio.hpp>
#include <SFML/Graphics.hpp>

#include <memory>

namespace mgo {

class ViewSfml final : public IView {
public:
    virtual void initialise(const Model&) override;
    virtual void close() override;
    virtual int getInput() override;
    virtual void updateDisplay(const Model&) override;
    // Non-overrides:
    void updateTextFromModel(const Model&);
    void updateThreadData(const mgo::Model& model);
    int processJoystickButton(const sf::Event& event);
    int getJoystickState();

private:
    std::unique_ptr<sf::RenderWindow> m_window;
    std::unique_ptr<sf::Font> m_font;

    // Main text items which are always displayed:
    std::unique_ptr<sf::Text> m_txtAxis1Label;
    std::unique_ptr<sf::Text> m_txtAxis1Pos;
    std::unique_ptr<sf::Text> m_txtAxis1Units;
    std::unique_ptr<sf::Text> m_txtAxis1Speed;
    std::unique_ptr<sf::Text> m_txtAxis2Label;
    std::unique_ptr<sf::Text> m_txtAxis2Pos;
    std::unique_ptr<sf::Text> m_txtAxis2Units;
    std::unique_ptr<sf::Text> m_txtAxis2Speed;
    std::unique_ptr<sf::Text> m_txtRpmLabel;
    std::unique_ptr<sf::Text> m_txtRpm;
    std::unique_ptr<sf::Text> m_txtRpmUnits;
    std::unique_ptr<sf::Text> m_txtGeneralStatus;
    std::unique_ptr<sf::Text> m_txtAxis1Status;
    std::unique_ptr<sf::Text> m_txtAxis2Status;
    std::unique_ptr<sf::Text> m_txtChuckRpm;
    std::unique_ptr<sf::Text> m_txtLeaderNotifier;
    std::unique_ptr<sf::Text> m_txtWarning;
    std::unique_ptr<sf::Text> m_txtTaperOrRadius;
    std::unique_ptr<sf::Text> m_txtAxis1LinearScalePos;

    // Text items which are displayed sometimes:
    std::unique_ptr<sf::Text> m_txtMode;
    std::unique_ptr<sf::Text> m_txtMisc1;
    std::unique_ptr<sf::Text> m_txtMisc2;
    std::unique_ptr<sf::Text> m_txtMisc3;
    std::unique_ptr<sf::Text> m_txtMisc4;
    std::unique_ptr<sf::Text> m_txtMisc5;

    std::unique_ptr<sf::Text> m_txtNotification;
    std::unique_ptr<sf::Text> m_txtXRetractDirection;
    std::unique_ptr<sf::Text> m_txtXRetracted;

    std::vector<std::unique_ptr<sf::Text>> m_txtMemoryLabel;
    std::unique_ptr<sf::Text> m_txtAxis1MemoryLabel;
    std::unique_ptr<sf::Text> m_txtAxis2MemoryLabel;
    std::vector<std::unique_ptr<sf::Text>> m_txtAxis1MemoryValue;
    std::vector<std::unique_ptr<sf::Text>> m_txtAxis2MemoryValue;

    std::size_t m_iteration { 0 };
};

} // namespace mgo
