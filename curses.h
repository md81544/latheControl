#pragma once

// Simple wrapper for ncurses.

#include <ncurses.h>

#include <sstream>
#include <tuple>
#include <unordered_map>
#include <memory>

namespace mgo
{
namespace Curses
{

enum class Cursor{ off, on };
enum class Input{ blocking, nonBlocking };
enum class Scrolling{ on, off };

enum class Colours : short int
{
    greenOnBlack = 1,
    redOnBlack,
    yellowOnBlack,
    whiteOnBlack
};

class Window
{
public:
    Window(int h, int w, int y, int x, bool box = true);
    // Centered window:
    Window(int h, int w, bool box = true);
    Window();
    ~Window();
    void refresh();
    void move(int y, int x);
    int getKey();
    std::tuple<int, int> getScreenSize();
    std::string getString(const std::string& prompt);
    void clear();
    void clearToEol();
    void setBlocking(mgo::Curses::Input block);
    void cursor(mgo::Curses::Cursor cursor);
    void scrolling(mgo::Curses::Scrolling scrolling);
    void setColour( Colours );
private:
    void printOss();
    std::ostringstream m_oss;
    WINDOW* m_handle;
    WINDOW* m_borderHandle{nullptr};
    static std::size_t m_instanceCount;

    template <typename T>
    friend Window& operator<<(Window& win, T t);
};

template <typename T>
Window& operator<<(Window& win, T t)
{
    // Convert everything to a string.
    // Move() and Refresh() cause the buffer to be written to screen.
    // This feels like a bit of a hack compared to implementing a
    // proper stream but works OK
    win.m_oss << t;
    return win;
}


} // namespace Curses
} // namespace mgo

