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
private:
    std::unique_ptr<sf::RenderWindow> m_window;
    std::unique_ptr<sf::Text> m_txtZPosition;
    std::unique_ptr<sf::Font> m_font;
};

} // namespace mgo