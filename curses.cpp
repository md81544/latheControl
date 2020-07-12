#include "curses.h"

std::size_t mgo::Curses::Window::m_instanceCount = 0;

#include <cassert>
#include <stdexcept>

mgo::Curses::Window::Window(
    int h, int w, int y, int x, bool box
    )
{
    assert( m_instanceCount > 0 );
    ++m_instanceCount;
    if ( box )
    {
        m_borderHandle = ::newwin( h, w, y, x );
        ::box( m_borderHandle, 0, 0 );
        m_handle = ::newwin( h - 2, w - 2, y + 1, x + 1 );
    }
    else
    {
        m_handle = ::newwin( h, w, y, x );
    }
}

mgo::Curses::Window::Window(
    int h, int w, bool box
    )
{
    assert( m_instanceCount > 0 );
    ++m_instanceCount;

    int rows;
    int cols;
    std::tie( rows, cols ) = getScreenSize();
    int y = ( rows / 2 ) - ( h / 2 );
    int x = ( cols / 2 ) - ( w / 2 );

    if ( box )
    {
        m_borderHandle = ::newwin( h, w, y, x );
        ::box( m_borderHandle, 0, 0 );
        m_handle = ::newwin( h - 2, w - 2, y + 1, x + 1 );
    }
    else
    {
        m_handle = ::newwin( h, w, y, x );
    }
}

mgo::Curses::Window::Window()
{
    assert( m_instanceCount == 0 );
    ++m_instanceCount;
    ::initscr();
    if ( stdscr == nullptr )
    {
        throw std::runtime_error( "Error initialising ncurses" );
    }
    ::start_color();
    ::init_pair( static_cast<short>(Colours::greenOnBlack),  COLOR_GREEN,  COLOR_BLACK );
    ::init_pair( static_cast<short>(Colours::redOnBlack),    COLOR_RED,    COLOR_BLACK );
    ::init_pair( static_cast<short>(Colours::yellowOnBlack), COLOR_YELLOW, COLOR_BLACK );
    ::init_pair( static_cast<short>(Colours::whiteOnBlack),  COLOR_WHITE,  COLOR_BLACK );
    ::init_pair( static_cast<short>(Colours::cyanOnBlack),   COLOR_CYAN,   COLOR_BLACK );
    ::attron(COLOR_PAIR( Colours::greenOnBlack ));
    ::raw();
    ::keypad( stdscr, TRUE );
    ::noecho();
    ::ESCDELAY = 25;
    m_handle = stdscr;
}

mgo::Curses::Window::~Window()
{
    if ( m_instanceCount == 1 )
    {
        ::endwin();
    }
    else
    {
        --m_instanceCount;
        ::delwin( m_handle );
        if ( m_borderHandle )
        {
            ::delwin( m_borderHandle );
        }
    }
}

void mgo::Curses::Window::refresh()
{
    printOss();
    if ( m_borderHandle )
    {
        ::wrefresh( m_borderHandle );
    }
    ::wrefresh( m_handle );
}

void mgo::Curses::Window::move(int y, int x)
{
    printOss();
    ::wmove( m_handle, y, x );
}

int mgo::Curses::Window::getKey()
{
    return ::wgetch( m_handle );
}

void mgo::Curses::Window::printOss()
{
    ::wprintw( m_handle, m_oss.str().c_str() );
    m_oss.str( "" );
    m_oss.clear();
}

std::tuple<int, int> mgo::Curses::Window::getScreenSize()
{
    int rows;
    int cols;
    // getmaxyx is a macro hence no scope resolution
    getmaxyx( stdscr, rows, cols ); // note always stdscr in this case
    return std::make_tuple( rows, cols );
}

std::string mgo::Curses::Window::getString(const std::string& prompt)
{
    ::wprintw( m_handle, prompt.c_str() );
    ::echo();
    char buf[ 256 ];
    ::getnstr( buf, 255 );
    ::noecho();
    return std::string( buf );
}

void mgo::Curses::Window::clear()
{
    m_oss.str( "" );
    m_oss.clear();
    ::werase( m_handle );
    ::wrefresh( m_handle );
}

void mgo::Curses::Window::clearToEol()
{
    ::wclrtoeol( m_handle );
}

void mgo::Curses::Window::setBlocking(mgo::Curses::Input block)
{
    if ( block == mgo::Curses::Input::blocking )
    {
        ::nodelay( m_handle, FALSE );
    }
    else
    {
        ::nodelay( m_handle, TRUE );
    }
}

void mgo::Curses::Window::cursor(mgo::Curses::Cursor cursor)
{
    int n = 0;
    if ( cursor == mgo::Curses::Cursor::on )
    {
        n = 1;
    }
    ::curs_set( n );
}

void mgo::Curses::Window::scrolling(mgo::Curses::Scrolling scrolling)
{
    if ( scrolling == mgo::Curses::Scrolling::on )
    {
        ::scrollok( m_handle, TRUE );
    }
    else
    {
        ::scrollok( m_handle, FALSE );
    }
}

void mgo::Curses::Window::setColour( Colours pair )
{
    printOss(); // write out any existing text
    ::attron(COLOR_PAIR( pair ) | A_BOLD );
}
