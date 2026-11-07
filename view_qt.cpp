#include "view_qt.h"

#include "commands.h"
#include "joystick.h"
#include "keycodes.h"
#include "log.h"
#include "menu.h"
#include "model.h"
#include "qt_dialog.h"
#include "threadpitches.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDialog>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QPainter>
#include <QPointer>
#include <QScreen>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <cassert>
#include <cctype>
#include <deque>
#include <variant>

#include <fmt/format.h>

namespace mgo {

namespace {

// The display is laid out in these "logical" coordinates (the original SFML
// window size), and scaled to fit the actual window
constexpr int LOGICAL_WIDTH = 1280;
constexpr int LOGICAL_HEIGHT = 720;

// How long after a leader key (e.g. Z) is pressed its hints are shown
constexpr qint64 LEADER_HINT_DELAY_MS = 500;

// The colours SFML provided as constants
const QColor GREEN(0, 255, 0);
const QColor RED(255, 0, 0);
const QColor YELLOW(255, 255, 0);
const QColor WHITE(255, 255, 255);

// One piece of text on the display. Equivalent to an sf::Text
struct TextItem {
    QPointF position;
    int pixelSize { 20 };
    QColor colour { WHITE };
    std::string text;
    bool bold { false };
    // If set, the text is right-aligned within this width from position
    float rightAlignWidth { 0.f };
    // If set, this character (by index) is highlighted (in yellow, and
    // underlined), e.g. a menu hotkey. Only for left-aligned text
    int highlightIndex { -1 };
};

// A filled rectangle, with an optional border
struct RectItem {
    QRectF rect;
    QColor fill;
    QColor border { Qt::transparent };
};

using DrawItem = std::variant<TextItem, RectItem>;

// A position as displayed (already converted to the display units), avoiding
// "-0.00"
std::string formatMotorPosition(double value, int decimals)
{
    if (std::abs(value) < 0.5 * std::pow(10.0, -decimals)) {
        value = 0.0;
    }
    return fmt::format("{: >8.{}f}", value, decimals);
}

// Parses a pair of joystick button numbers, e.g. "4,6". "none" (or anything
// unreadable, which is logged) means no chord
std::optional<std::pair<unsigned, unsigned>> parseButtonChord(const std::string& setting)
{
    if (setting.empty() || setting == "none") {
        return std::nullopt;
    }
    const auto comma = setting.find(',');
    try {
        if (comma != std::string::npos) {
            const unsigned first = std::stoul(setting.substr(0, comma));
            const unsigned second = std::stoul(setting.substr(comma + 1));
            if (first != second) {
                return std::make_pair(first, second);
            }
        }
    } catch (const std::exception&) {
        // (logged below)
    }
    MGOLOG("Ignoring JoystickMenuChord setting '" << setting << "': should be e.g. 4,6");
    return std::nullopt;
}

bool isModifierKey(int qtKey)
{
    return qtKey == Qt::Key_Shift || qtKey == Qt::Key_Control || qtKey == Qt::Key_Alt
        || qtKey == Qt::Key_AltGr || qtKey == Qt::Key_Meta || qtKey == Qt::Key_CapsLock;
}

bool isArrowKey(int qtKey)
{
    return qtKey == Qt::Key_Left || qtKey == Qt::Key_Right || qtKey == Qt::Key_Up
        || qtKey == Qt::Key_Down;
}

// The main window. It draws whatever items the view gives it, and queues up
// key presses (converted to our key:: codes) for the view to hand out from
// getEvents()
class DisplayWidget final : public QWidget {
public:
    explicit DisplayWidget(const QString& fontFamily)
        : m_fontFamily(fontFamily)
    {
        setWindowTitle("Lathe Control");
        setFocusPolicy(Qt::StrongFocus);
        m_lastKeyTime.start();
    }

    // What to draw, in order. Set by the view on each updateDisplay()
    std::vector<DrawItem> frame;
    // Dims the display (while a dialog is shown on top of it)
    bool dimmed { false };

    // Queue a key code from another source (the joystick)
    void pushKey(int k)
    {
        m_keys.push_back(k);
    }

    int takeKey()
    {
        if (m_keys.empty()) {
            return key::None;
        }
        const int k = m_keys.front();
        m_keys.pop_front();
        return k;
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), Qt::black);
        painter.setRenderHint(QPainter::TextAntialiasing);
        // Scale the logical layout to fit, keeping its aspect ratio
        const double scale = std::min(
            static_cast<double>(width()) / LOGICAL_WIDTH,
            static_cast<double>(height()) / LOGICAL_HEIGHT);
        painter.translate(
            (width() - LOGICAL_WIDTH * scale) / 2.0, (height() - LOGICAL_HEIGHT * scale) / 2.0);
        painter.scale(scale, scale);
        QFont font(m_fontFamily);
        for (const auto& drawItem : frame) {
            if (const auto* rect = std::get_if<RectItem>(&drawItem)) {
                painter.setPen(rect->border == Qt::transparent ? QPen(Qt::NoPen)
                                                                : QPen(rect->border, 2));
                painter.setBrush(rect->fill);
                painter.drawRect(rect->rect);
                continue;
            }
            const auto& item = std::get<TextItem>(drawItem);
            font.setPixelSize(item.pixelSize);
            font.setBold(item.bold);
            painter.setFont(font);
            painter.setPen(item.colour);
            const bool rightAlign = item.rightAlignWidth > 0.f;
            const QString text = QString::fromStdString(item.text);
            painter.drawText(
                QRectF(
                    item.position,
                    QSizeF(rightAlign ? item.rightAlignWidth : LOGICAL_WIDTH, item.pixelSize * 2.0)),
                (rightAlign ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignTop | Qt::TextDontClip,
                text);
            if (!rightAlign && item.highlightIndex >= 0 && item.highlightIndex < text.size()) {
                // Draw the highlighted character again, over itself
                const qreal offset
                    = QFontMetricsF(font).horizontalAdvance(text.left(item.highlightIndex));
                QFont highlightFont = font;
                highlightFont.setUnderline(true);
                painter.setFont(highlightFont);
                painter.setPen(YELLOW);
                painter.drawText(
                    QRectF(
                        item.position + QPointF(offset, 0.0),
                        QSizeF(LOGICAL_WIDTH, item.pixelSize * 2.0)),
                    Qt::AlignLeft | Qt::AlignTop | Qt::TextDontClip,
                    text.mid(item.highlightIndex, 1));
            }
        }
        if (dimmed) {
            painter.resetTransform();
            painter.fillRect(rect(), QColor(0, 0, 0, 135));
        }
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        const int qtKey = event->key();
        if (qtKey == Qt::Key_Alt) {
            m_altHeld = true;
        }
        if (qtKey == Qt::Key_AltGr) {
            m_altGrHeld = true;
        }
        if (event->isAutoRepeat() || isModifierKey(qtKey)) {
            return;
        }
        // Ignore the same key pressed again within 100ms (as the SFML version did)
        if (qtKey == m_lastQtKey && m_lastKeyTime.elapsed() < 100) {
            return;
        }
        m_lastQtKey = qtKey;
        m_lastKeyTime.restart();
        const int k = convertKey(event);
        if (k != key::None) {
            m_keys.push_back(k);
        }
    }

    void keyReleaseEvent(QKeyEvent* event) override
    {
        // Auto-repeat generates release events too; they must be ignored, or
        // holding an arrow key would trigger the stop below repeatedly
        if (event->isAutoRepeat()) {
            return;
        }
        const int qtKey = event->key();
        // "Dead man" behaviour: rapids (Alt + arrow) stop as soon as either
        // the Alt key or the arrow key is released
        if (qtKey == Qt::Key_Alt || qtKey == Qt::Key_AltGr) {
            m_altHeld = m_altHeld && qtKey != Qt::Key_Alt;
            m_altGrHeld = m_altGrHeld && qtKey != Qt::Key_AltGr;
            m_keys.push_back(key::SPACE);
            return;
        }
        if (isArrowKey(qtKey) && altPressed(event)) {
            m_keys.push_back(key::SPACE);
        }
    }

    // Qt normally uses Tab to move between widgets, so would never deliver it
    // to keyPressEvent(); we want it as a key (to open the menu)
    bool focusNextPrevChild(bool) override
    {
        return false;
    }

    void closeEvent(QCloseEvent* event) override
    {
        // Closing the window is treated as a request to quit, so the
        // controller can stop the motors and exit cleanly
        event->ignore();
        m_keys.push_back(key::CtrlQ);
    }

private:
    bool altPressed(const QKeyEvent* event) const
    {
        // Right Alt is often AltGr on Linux, which Qt doesn't report as an Alt
        // modifier, so we also track the Alt keys' state ourselves
        return (event->modifiers() & Qt::AltModifier) || m_altHeld || m_altGrHeld;
    }

    // Converts a key press to one of our key:: codes (as convertKeyCode() did
    // for SFML events)
    int convertKey(const QKeyEvent* event) const
    {
        const int qtKey = event->key();
        const auto modifiers = event->modifiers();
        // Letters
        if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z) {
            const int offset = qtKey - Qt::Key_A;
            if (modifiers & Qt::ControlModifier) {
                return (key::a + offset) | key::CTRL;
            }
            if (modifiers & Qt::AltModifier) {
                return (key::a + offset) | key::ALT;
            }
            if (modifiers & Qt::ShiftModifier) {
                return key::A + offset;
            }
            return key::a + offset;
        }
        // Number keys
        if (qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9) {
            if (qtKey == Qt::Key_8 && (modifiers & Qt::ShiftModifier)) {
                return key::ASTERISK;
            }
            return key::ZERO + (qtKey - Qt::Key_0);
        }
        // Function keys
        if (qtKey >= Qt::Key_F1 && qtKey <= Qt::Key_F15) {
            return key::F1 + (qtKey - Qt::Key_F1);
        }
        // Misc. Note Qt reports the shifted symbol for shifted keys (e.g. '{'
        // rather than '['), so where the SFML version ignored shift we map both
        switch (qtKey) {
            case Qt::Key_Return:
            case Qt::Key_Enter:
                return key::ENTER;
            case Qt::Key_BracketLeft:
            case Qt::Key_BraceLeft:
                return key::LBRACKET;
            case Qt::Key_BracketRight:
            case Qt::Key_BraceRight:
                return key::RBRACKET;
            case Qt::Key_Comma:
            case Qt::Key_Less:
                return key::COMMA;
            case Qt::Key_Period:
            case Qt::Key_Greater:
                return key::FULLSTOP;
            case Qt::Key_Colon:
                return key::COLON;
            case Qt::Key_Asterisk:
                return key::ASTERISK;
            case Qt::Key_Right:
                return altPressed(event) ? key::AltRight : key::RIGHT;
            case Qt::Key_Left:
                return altPressed(event) ? key::AltLeft : key::LEFT;
            case Qt::Key_Up:
                return altPressed(event) ? key::AltUp : key::UP;
            case Qt::Key_Down:
                return altPressed(event) ? key::AltDown : key::DOWN;
            case Qt::Key_Backslash:
                return key::BACKSLASH;
            case Qt::Key_Bar:
                return key::PIPE;
            case Qt::Key_Minus:
            case Qt::Key_Underscore:
                return key::MINUS;
            case Qt::Key_Equal:
            case Qt::Key_Plus:
                return key::EQUALS;
            case Qt::Key_Space:
                return key::SPACE;
            case Qt::Key_Escape:
                return key::ESC;
            case Qt::Key_Backspace:
                return key::BACKSPACE;
            case Qt::Key_Tab:
                return key::TAB;
            case Qt::Key_Delete:
                return key::DELETE;
            default:
                return key::None;
        }
    }

    QString m_fontFamily;
    std::deque<int> m_keys;
    bool m_altHeld { false };
    bool m_altGrHeld { false };
    int m_lastQtKey { 0 };
    QElapsedTimer m_lastKeyTime;
};

} // end anonymous namespace

struct ViewQt::Impl {
    QString fontFamily;
    std::unique_ptr<DisplayWidget> widget;
    std::unique_ptr<Joystick> joystick;
    // The loop runEventLoop() is in, if any
    QEventLoop* eventLoop { nullptr };
    // The dialog open, if any. (It deletes itself once closed)
    QPointer<QDialog> dialog;
    // The main display is dimmed while a dialog is open
    void setDimmed(bool dimmed);
    void dialogClosed();

    // Main text items which are always displayed:
    TextItem axis1Label;
    TextItem axis1Pos;
    TextItem axis1Units;
    TextItem axis1Speed;
    TextItem axis2Label;
    TextItem axis2Pos;
    TextItem axis2Units;
    TextItem axis2Speed;
    TextItem rpmLabel;
    TextItem rpm;
    TextItem rpmUnits;
    TextItem generalStatus;
    TextItem axis1Status;
    TextItem axis2Status;
    TextItem chuckRpm;
    TextItem leaderNotifier;
    TextItem warning;
    TextItem taperOrRadius;
    TextItem axis1LinearScalePos;
    TextItem surfaceSpeed; // Vc

    // Text items which are displayed sometimes:
    TextItem mode;
    TextItem misc1;
    TextItem misc2;
    TextItem misc3;
    TextItem misc4;
    TextItem misc5;

    TextItem notification;
    TextItem xRetractDirection;
    TextItem xRetracted;
    // Motor fault (driver alarm) message, shown whatever the mode
    TextItem fault;
    std::size_t faultIteration { 0 };

    // The menu (owned by the controller), drawn when open
    const Menu* menu { nullptr };
    void drawMenu(std::vector<DrawItem>& frame, const Model& model) const;

    // Hints shown a moment after a leader key (e.g. Z) is pressed
    KeyMode hintKeyMode { KeyMode::None };
    QElapsedTimer hintTimer;
    void drawLeaderHints(std::vector<DrawItem>& frame, const Model& model) const;

    std::vector<TextItem> memoryLabel;
    TextItem axis1MemoryLabel;
    TextItem axis2MemoryLabel;
    std::vector<TextItem> axis1MemoryValue;
    std::vector<TextItem> axis2MemoryValue;

    std::size_t iteration { 0 };

    void updateTextFromModel(const Model& model);
    void updateThreadData(const Model& model);
    void updateMultiPassData(const Model& model);
};

ViewQt::ViewQt()
    : m_impl(std::make_unique<Impl>())
{
}

ViewQt::~ViewQt() = default;

void ViewQt::initialise(const Model& model)
{
    auto& d = *m_impl;
    // We read the font file ourselves rather than use addApplicationFont(path):
    // on macOS, CoreText refuses to register a font by path if the file has
    // certain extended attributes (e.g. com.apple.provenance)
    QFile fontFile("./lc_font.ttf");
    const int fontId = fontFile.open(QIODevice::ReadOnly)
        ? QFontDatabase::addApplicationFontFromData(fontFile.readAll())
        : -1;
    if (fontId < 0 || QFontDatabase::applicationFontFamilies(fontId).isEmpty()) {
        throw std::runtime_error("Could not load TTF font lc_font.ttf");
    }
    d.fontFamily = QFontDatabase::applicationFontFamilies(fontId).at(0);
    d.widget = std::make_unique<DisplayWidget>(d.fontFamily);

#ifdef FAKE
    // run in a window in "fake" mode for manual testing
    d.widget->resize(LOGICAL_WIDTH, LOGICAL_HEIGHT);
    d.widget->move(50, 50);
    d.widget->show();
#else
    d.widget->showFullScreen();
#endif
    // (The "offscreen" platform, used by the UI tests, has no windows to raise)
    if (QGuiApplication::platformName() != "offscreen") {
        d.widget->raise();
        d.widget->activateWindow();
    }
    if (const QScreen* screen = d.widget->screen()) {
        MGOLOG(fmt::format(
            "Screen size is {} x {}", screen->size().width(), screen->size().height()));
    }

    const auto make = [](float x, float y, int pixelSize, QColor colour = WHITE) {
        return TextItem { QPointF(x, y), pixelSize, colour, "" };
    };

    d.axis1Label = make(20, 10, 60);
    d.axis1Label.text = model.config().read("Axis1Label", "Z") + ":";
    d.axis1Pos = make(110, 10, 60, GREEN);
    d.axis1Units = make(430, 40, 30, QColor(0, 127, 0));
    d.axis1Units.text = model.config().read("Axis1DisplayUnits", "mm");
    d.axis1Speed = make(550, 40, 30, QColor(209, 209, 50));

    d.axis2Label = make(20, 70, 60);
    d.axis2Label.text = model.config().read("Axis2Label", "X") + ":";
    d.axis2Pos = make(110, 70, 60, GREEN);
    d.axis2Units = make(430, 100, 30, QColor(0, 127, 0));
    d.axis2Units.text = model.config().read("Axis2DisplayUnits", "mm");
    d.axis2Speed = make(550, 100, 30, QColor(209, 209, 50));

    d.rpmLabel = make(20, 130, 60);
    d.rpmLabel.text = "n:";
    d.rpm = make(150, 130, 60, GREEN);
    d.rpmUnits = make(430, 160, 30, QColor(0, 127, 0));
    d.rpmUnits.text = "rpm";

    // Memory banks
    constexpr float MEMORY_Y = 250;
    const QColor grey(128, 128, 128);
    for (unsigned n = 0; n < model.getMemorySize(); ++n) {
        auto label = make(60.f + n * 180.f, MEMORY_Y, 30, grey);
        label.text = fmt::format("    Mem {}", n + 1);
        d.memoryLabel.push_back(label);
        d.axis1MemoryValue.push_back(make(60.f + n * 180.f, MEMORY_Y + 25, 30, grey));
        d.axis2MemoryValue.push_back(make(60.f + n * 180.f, MEMORY_Y + 55, 30, grey));
    }
    d.axis1MemoryLabel = make(24, MEMORY_Y + 25, 30, grey);
    d.axis1MemoryLabel.text = model.config().read("Axis1Label", "Z") + ":";
    d.axis2MemoryLabel = make(24, MEMORY_Y + 55, 30, grey);
    d.axis2MemoryLabel.text = model.config().read("Axis2Label", "X") + ":";

    // STATUS BAR (at bottom of screen)
    constexpr float STATUS_BAR_Y = 650;
    d.generalStatus = make(20, STATUS_BAR_Y, 20, GREEN);
    d.axis1Status = make(350, STATUS_BAR_Y, 20, GREEN);
    d.axis2Status = make(550, STATUS_BAR_Y, 20, GREEN);
    d.chuckRpm = make(750, STATUS_BAR_Y, 20, GREEN);
    d.leaderNotifier = make(1000, STATUS_BAR_Y, 20, GREEN);

    // Miscellaneous - this is the bank of text which contains, for example,
    // the help, or text for, say, threading
    constexpr float MISC_Y = 380;
    d.mode = make(20, MISC_Y, 25, YELLOW);
    d.misc1 = make(20, MISC_Y + 30, 25);
    d.misc2 = make(20, MISC_Y + 60, 25);
    d.misc3 = make(20, MISC_Y + 90, 25);
    d.misc4 = make(20, MISC_Y + 120, 25);
    d.misc5 = make(20, MISC_Y + 150, 25);

    d.warning = make(20, MISC_Y + 200, 20, RED);
    d.taperOrRadius = make(860, 75, 20, RED);
    d.notification = make(860, 45, 25, RED);
    d.xRetracted = make(860, 105, 25, RED);
    d.xRetracted.text = "RETRACTED";
    d.xRetractDirection = make(860, 165, 25, RED);
    d.xRetractDirection.text = "-X RTRCT";
    // (Surface speed, on the rpm line, in line with the axes' feed rates; the
    // linear scale's reading is below it)
    d.surfaceSpeed = make(550, 160, 30, QColor(209, 209, 50));
    d.axis1LinearScalePos = make(550, 205, 20, QColor(209, 209, 50));
    d.fault = make(20, STATUS_BAR_Y - 40, 22, RED);

    // (Joystick support is only available on Linux)
    d.joystick = std::make_unique<Joystick>(
        model.config().read("JoystickDevice", "/dev/input/js0"),
        parseButtonChord(model.config().read("JoystickMenuChord", "4,6")));

    QCoreApplication::processEvents();
}

void ViewQt::openTaperDialog(double currentAngle, std::function<void(std::optional<double>)> onDone)
{
    assert(!isDialogOpen()); // (one at a time)
    m_impl->setDimmed(true);
    m_impl->dialog = dialog::openTaperDialog(
        m_impl->widget.get(),
        m_impl->fontFamily,
        currentAngle,
        [this, onDone = std::move(onDone)](std::optional<double> angle) {
            m_impl->dialogClosed();
            onDone(angle);
        });
}

void ViewQt::openThreadDialog(
    const ThreadTable& threads,
    ThreadChoice current,
    std::function<void(std::optional<ThreadChoice>)> onDone)
{
    assert(!isDialogOpen()); // (one at a time)
    m_impl->setDimmed(true);
    m_impl->dialog = dialog::openThreadDialog(
        m_impl->widget.get(),
        m_impl->fontFamily,
        threads,
        current,
        [this, onDone = std::move(onDone)](std::optional<ThreadChoice> choice) {
            m_impl->dialogClosed();
            onDone(choice);
        });
}

void ViewQt::openMultiPassDialog(
    const MultiPassDialogInfo& info, std::function<void(std::optional<MultiPassSetup>)> onDone)
{
    assert(!isDialogOpen()); // (one at a time)
    m_impl->setDimmed(true);
    m_impl->dialog = dialog::openMultiPassDialog(
        m_impl->widget.get(),
        m_impl->fontFamily,
        info,
        [this, onDone = std::move(onDone)](std::optional<MultiPassSetup> setup) {
            m_impl->dialogClosed();
            onDone(setup);
        });
}

bool ViewQt::isDialogOpen() const
{
    return !m_impl->dialog.isNull();
}

void ViewQt::cancelDialog()
{
    if (m_impl->dialog) {
        m_impl->dialog->reject();
    }
}

void ViewQt::Impl::setDimmed(bool dimmed)
{
    widget->dimmed = dimmed;
    widget->update();
}

void ViewQt::Impl::dialogClosed()
{
    dialog = nullptr;
    setDimmed(false);
}

void ViewQt::setMenu(const Menu* menu)
{
    m_impl->menu = menu;
}

void ViewQt::Impl::drawMenu(std::vector<DrawItem>& frame, const Model& model) const
{
    // A panel down the right-hand side, so the position read-outs stay
    // visible. It stops short of the motor fault line, so that's never hidden
    constexpr float panelX = 790.f;
    constexpr float panelY = 10.f;
    constexpr float panelWidth = 480.f;
    constexpr float panelHeight = 590.f;
    constexpr float margin = 15.f;
    constexpr float listY = panelY + 55.f;
    constexpr float lineHeight = 30.f;
    constexpr int fontSize = 20;
    constexpr std::size_t visibleRows = 14;
    const QColor normal(200, 200, 200);
    const QColor unavailable(90, 90, 90);
    const QColor shortcutColour(150, 150, 150);
    const QColor hint(140, 140, 140);

    frame.push_back(RectItem { QRectF(panelX, panelY, panelWidth, panelHeight),
                               QColor(25, 25, 25),
                               QColor(128, 128, 128) });

    const auto category = menu->openCategory();
    frame.push_back(TextItem { QPointF(panelX + margin, panelY + 12.f),
                               26,
                               GREEN,
                               category ? menu->categories().at(*category) : "Menu" });

    // Each row: its text, shortcut, and whether it can be chosen now
    struct Row {
        std::string text;
        std::string shortcut;
        bool available;
        int hotkeyIndex; // the character to highlight as the hotkey, or -1
    };
    // Highlights the hotkey's first appearance in the text; if it isn't in
    // the text, it's added in brackets at the end
    const auto makeRow = [](std::string text, std::string shortcut, bool available, char hotkey) {
        int index = -1;
        if (hotkey) {
            const auto it = std::find_if(text.begin(), text.end(), [hotkey](char ch) {
                return std::tolower(static_cast<unsigned char>(ch)) == hotkey;
            });
            if (it != text.end()) {
                index = static_cast<int>(it - text.begin());
            } else {
                text += fmt::format(" ({})", static_cast<char>(std::toupper(hotkey)));
                index = static_cast<int>(text.size()) - 2;
            }
        }
        return Row { std::move(text), std::move(shortcut), available, index };
    };
    std::vector<Row> rows;
    if (category) {
        for (const Command* c : menu->currentCommands()) {
            rows.push_back(makeRow(
                c->description,
                shortcutText(c->key, model.config()),
                isAvailable(*c, model),
                c->hotkey));
        }
    } else {
        for (const auto& name : menu->categories()) {
            rows.push_back(makeRow(name, ">", true, categoryHotkey(name)));
        }
    }

    // Scroll so the selection is visible
    const std::size_t selection = menu->selection();
    std::size_t first = 0;
    if (selection >= visibleRows) {
        first = selection - visibleRows + 1;
    }
    float y = listY;
    for (std::size_t n = first; n < rows.size() && n < first + visibleRows; ++n) {
        const Row& row = rows.at(n);
        const bool selected = n == selection;
        if (selected) {
            frame.push_back(RectItem { QRectF(panelX + 5.f, y - 3.f, panelWidth - 10.f, lineHeight),
                                       QColor(60, 60, 60) });
        }
        const QColor colour = !row.available ? unavailable : selected ? WHITE : normal;
        TextItem text { QPointF(panelX + margin, y), fontSize, colour, row.text, selected };
        if (row.available) {
            text.highlightIndex = row.hotkeyIndex;
        }
        frame.push_back(text);
        frame.push_back(TextItem { QPointF(panelX + margin, y),
                                   fontSize,
                                   row.available ? shortcutColour : unavailable,
                                   row.shortcut,
                                   false,
                                   panelWidth - 2 * margin });
        y += lineHeight;
    }
    if (first > 0) {
        frame.push_back(TextItem { QPointF(panelX + margin, listY - 22.f),
                                   16,
                                   hint,
                                   "(more above)",
                                   false,
                                   panelWidth - 2 * margin });
    }
    if (first + visibleRows < rows.size()) {
        frame.push_back(TextItem { QPointF(panelX + margin, y), 16, hint, "(more below)", false,
                                   panelWidth - 2 * margin });
    }

    // How to use it
    const float footerY = panelY + panelHeight - 90.f;
    frame.push_back(TextItem { QPointF(panelX + margin, footerY),
                               16,
                               hint,
                               "Up/Down: select   Enter/Right: choose" });
    frame.push_back(TextItem { QPointF(panelX + margin, footerY + 20.f),
                               16,
                               hint,
                               "   (or press the highlighted letter)" });
    frame.push_back(TextItem { QPointF(panelX + margin, footerY + 40.f),
                               16,
                               hint,
                               "Left/Esc: back   Tab: close menu" });
    frame.push_back(TextItem { QPointF(panelX + margin, footerY + 60.f),
                               16,
                               YELLOW,
                               "Space stops all motors. Rapids: Alt+arrow" });
}

void ViewQt::Impl::drawLeaderHints(std::vector<DrawItem>& frame, const Model& model) const
{
    // In the same place as the menu
    constexpr float panelX = 790.f;
    constexpr float panelY = 10.f;
    constexpr float panelWidth = 480.f;
    constexpr float margin = 15.f;
    constexpr float keyColumnWidth = 90.f;
    constexpr float lineHeight = 30.f;
    constexpr int fontSize = 20;
    const QColor normal(200, 200, 200);
    const QColor unavailable(90, 90, 90);
    const QColor hint(140, 140, 140);

    const auto commands = commandsForLeader(menu->commands(), model.getKeyMode());
    std::string title;
    switch (model.getKeyMode()) {
        case KeyMode::Axis1:
            title = model.config().read("Axis1Label", "Z") + " then...";
            break;
        case KeyMode::Axis2:
            title = model.config().read("Axis2Label", "X") + " then...";
            break;
        case KeyMode::AxisAll:
            title = "\\ (both axes) then...";
            break;
        case KeyMode::Function:
            title = "F2 then...";
            break;
        case KeyMode::None:
            return;
    }

    const float panelHeight = 55.f + lineHeight * commands.size() + 60.f;
    frame.push_back(RectItem { QRectF(panelX, panelY, panelWidth, panelHeight),
                               QColor(25, 25, 25),
                               QColor(128, 128, 128) });
    frame.push_back(TextItem { QPointF(panelX + margin, panelY + 12.f), 26, GREEN, title });
    float y = panelY + 55.f;
    for (const Command* c : commands) {
        const bool available = isAvailable(*c, model);
        frame.push_back(TextItem { QPointF(panelX + margin, y),
                                   fontSize,
                                   available ? YELLOW : unavailable,
                                   followUpKeyName(c->key) });
        frame.push_back(TextItem { QPointF(panelX + margin + keyColumnWidth, y),
                                   fontSize,
                                   available ? normal : unavailable,
                                   c->description });
        y += lineHeight;
    }
    frame.push_back(TextItem { QPointF(panelX + margin, y + 10.f),
                               16,
                               hint,
                               "Other keys cancel.  Tab: the full menu" });
    frame.push_back(TextItem { QPointF(panelX + margin, y + 30.f),
                               16,
                               YELLOW,
                               "Space stops all motors" });
}

void ViewQt::close()
{
    if (m_impl->widget) {
        m_impl->widget->hide();
    }
}

void ViewQt::runEventLoop(std::chrono::milliseconds interval, std::function<void()> tick)
{
    assert(!m_impl->eventLoop); // (not re-entrant)
    QEventLoop loop;
    QTimer timer;
    // (Precise, as the threading speed is updated on each tick)
    timer.setTimerType(Qt::PreciseTimer);
    QObject::connect(&timer, &QTimer::timeout, std::move(tick));
    m_impl->eventLoop = &loop;
    timer.start(interval);
    loop.exec();
    timer.stop();
    m_impl->eventLoop = nullptr;
}

void ViewQt::quitEventLoop()
{
    if (m_impl->eventLoop) {
        m_impl->eventLoop->quit();
    }
}

int ViewQt::getEvents()
{
    // (Key presses arrive through Qt's event loop, which the controller runs
    // on; the gamepad is polled)
    for (const int k : m_impl->joystick->poll()) {
        m_impl->widget->pushKey(k | key::JOYSTICK);
    }
    return m_impl->widget->takeKey();
}

void ViewQt::openInput(
    Input::Type type,
    std::string_view prompt,
    std::vector<std::string> additionalText,
    std::function<void(const Input::Return&)> onDone,
    std::string_view defaultEntry,
    std::optional<std::vector<std::string>> listItems)
{
    assert(!isDialogOpen()); // (one at a time)
    m_impl->setDimmed(true);
    m_impl->dialog = dialog::openInput(
        m_impl->widget.get(),
        m_impl->fontFamily,
        type,
        prompt,
        std::move(additionalText),
        defaultEntry,
        std::move(listItems),
        [this, onDone = std::move(onDone)](const Input::Return& rc) {
            m_impl->dialogClosed();
            onDone(rc);
        });
}

void ViewQt::updateDisplay(const Model& model)
{
    auto& d = *m_impl;
    d.updateTextFromModel(model);

    std::vector<DrawItem> frame;
    const auto draw = [&frame](const TextItem& item) { frame.push_back(item); };

    if (!model.isShuttingDown()) {
        if (!model.config().readBool("DisableAxis1", false)) {
            d.axis1Label.colour
                = model.isAxisLocked(1) ? QColor(90, 90, 90) : QColor(0, 192, 0);
            draw(d.axis1Label);
            draw(d.axis1Pos);
            draw(d.axis1Units);
            draw(d.axis1Speed);
            draw(d.axis1Status);
            draw(d.axis1MemoryLabel);
            draw(d.axis1LinearScalePos);
        }
        if (!model.config().readBool("DisableAxis2", false)) {
            d.axis2Label.colour
                = model.isAxisLocked(2) ? QColor(90, 90, 90) : QColor(0, 192, 0);
            draw(d.axis2Label);
            draw(d.axis2Pos);
            draw(d.axis2Units);
            draw(d.axis2Speed);
            draw(d.axis2Status);
            draw(d.axis2MemoryLabel);
        }
        if (!model.config().readBool("DisableRpm", false)) {
            draw(d.rpmLabel);
            // (As in the SFML version, the colour change shows from the next frame)
            d.rpmLabel.colour = model.getChuckRotationDirection() == RotationDirection::reversed
                ? QColor(255, 0, 0)
                : QColor(0, 192, 0);
            draw(d.rpm);
            draw(d.rpmUnits);
            if (!d.surfaceSpeed.text.empty()) {
                draw(d.surfaceSpeed);
            }
        }
        draw(d.leaderNotifier);
        draw(d.chuckRpm);
        draw(d.generalStatus);
        draw(d.warning);
        draw(d.notification);
        if (model.getMode() == Mode::Taper
            || model.getMode() == Mode::Radius) {
            draw(d.taperOrRadius);
        }
        if (model.getRetractionDirection() == XDirection::Inwards) {
            draw(d.xRetractDirection);
        }
        if (model.getIsAxis2Retracted()) {
            draw(d.xRetracted);
        }
        for (std::size_t n = 0; n < d.memoryLabel.size(); ++n) {
            draw(d.memoryLabel.at(n));
            if (!model.config().readBool("DisableAxis1", false)) {
                draw(d.axis1MemoryValue.at(n));
            }
            if (!model.config().readBool("DisableAxis2", false)) {
                draw(d.axis2MemoryValue.at(n));
            }
        }
        // Z/X labels - make red if keyMode corresponds. (As in the SFML
        // version, this takes effect from the next frame)
        if (model.getKeyMode() == KeyMode::Axis1 || model.getKeyMode() == KeyMode::AxisAll) {
            d.axis1MemoryLabel.colour = RED;
        } else {
            d.axis1MemoryLabel.colour = QColor(128, 128, 128);
        }
        if (model.getKeyMode() == KeyMode::Axis2 || model.getKeyMode() == KeyMode::AxisAll) {
            d.axis2MemoryLabel.colour = RED;
        } else {
            d.axis2MemoryLabel.colour = QColor(128, 128, 128);
        }
        if (model.getMode() != Mode::None) {
            draw(d.mode);
        }
        draw(d.misc1);
        draw(d.misc2);
        draw(d.misc3);
        draw(d.misc4);
        draw(d.misc5);
        d.fault.text = model.getFaultMessage();
        if (!d.fault.text.empty()) {
            d.fault.colour = ++d.faultIteration % 6 < 3 ? RED : YELLOW;
            draw(d.fault);
        }
        if (d.menu && d.menu->isOpen()) {
            d.drawMenu(frame, model);
        } else if (d.menu && model.getKeyMode() != KeyMode::None) {
            // A leader key has been pressed: after a moment (so they don't
            // flash up when typing a shortcut quickly), show what can follow
            if (model.getKeyMode() != d.hintKeyMode) {
                d.hintKeyMode = model.getKeyMode();
                d.hintTimer.restart();
            }
            if (d.hintTimer.elapsed() >= LEADER_HINT_DELAY_MS) {
                d.drawLeaderHints(frame, model);
            }
        }
        if (model.getKeyMode() == KeyMode::None) {
            d.hintKeyMode = KeyMode::None;
        }
    } else {
        // The SFML version drew nothing while shutting down, but set this text
        draw(d.axis1Pos);
    }

    d.widget->frame = std::move(frame);
    d.widget->update(); // (repainted by Qt's event loop)
}

void ViewQt::Impl::updateTextFromModel(const Model& model)
{
    // Updates all the text objects with data in the model
    if (model.isShuttingDown()) {
        axis1Pos.text = "SHUTTING DOWN";
        return;
    }

    // Lengths are shown in the display units (mm or inches); values for an
    // axis which isn't a length (e.g. degrees) are shown as they are
    const auto speedDecimals = [&model](int axis) {
        return model.isLengthAxis(axis) && model.getDisplayUnits() == DisplayUnits::Imperial ? 3 : 2;
    };
    axis1Units.text = model.axisUnitName(1);
    axis2Units.text = model.axisUnitName(2);

    axis1Pos.text = formatMotorPosition(
        model.toDisplayUnits(1, model.getAxis1MotorPosition()), model.axisDecimals(1));

    // (When threading, Z's speed is set by the spindle's)
    const double axis1Speed = model.getMode() == Mode::Threading
        ? model.getThreadingZSpeed()
        : model.getAxis1MotorSpeed();
    this->axis1Speed.text = fmt::format(
        "{:<.{}f} {}/min",
        model.toDisplayUnits(1, axis1Speed),
        speedDecimals(1),
        model.axisUnitName(1));

    if (!model.getIsAxis2Retracted()) {
        axis2Pos.text = formatMotorPosition(
            model.toDisplayUnits(2, model.getAxis2MotorPosition()), model.axisDecimals(2));
    } else {
        axis2Pos.text = "     ---";
    }
    axis2Speed.text = fmt::format(
        "{:<.{}f} {}/min",
        model.toDisplayUnits(2, model.getAxis2MotorSpeed()),
        speedDecimals(2),
        model.axisUnitName(2));
    float rpmValue = model.getRotaryEncoderRpm();
    if (rpmValue > 0.f) {
        // Round to nearest 10 to keep display more constant
        rpmValue = static_cast<int>(rpmValue / 10.f) * 10.f;
    }
    rpm.text = fmt::format("{: >7}", static_cast<int>(rpmValue));
    // Surface speed, once the diameter is known. (Calculated from the rpm
    // as displayed, so the two agree and Vc doesn't flicker)
    if (const auto diameter = model.getDiameterMm()) {
        const double vc = M_PI * *diameter * rpmValue / 1'000.0; // m/min
        if (model.getDisplayUnits() == DisplayUnits::Imperial) {
            // Surface feet per minute
            surfaceSpeed.text = fmt::format("Vc {:.1f} SFM", vc * FEET_PER_METRE);
        } else {
            surfaceSpeed.text = fmt::format("Vc {:.1f} m/min", vc);
        }
    } else {
        surfaceSpeed.text.clear();
    }

    generalStatus.text = model.getGeneralStatus();
    axis1Status.text
        = fmt::format("{}: {}", model.config().read("Axis1Label", "Z"), model.getAxis1Status());
    axis2Status.text
        = fmt::format("{}: {}", model.config().read("Axis2Label", "X"), model.getAxis2Status());
    if (model.getMode() == Mode::Taper) {
        taperOrRadius.text = fmt::format("Angle: {}", model.getTaperAngle());
    } else if (model.getMode() == Mode::Radius) {
        taperOrRadius.text = fmt::format(
            "Radius: {} {}", model.toDisplayUnits(1, model.getRadius()), model.axisUnitName(1));
    }
    if (model.getKeyMode() == KeyMode::Function) {
        leaderNotifier.text = ": (select function)";
    } else {
        leaderNotifier.text = "";
    }
    chuckRpm.text = fmt::format("C: {: >5.1f} deg", model.getChuckAngle());

    axis1LinearScalePos.text = fmt::format(
        "{} Scale: {:<.{}f} {}",
        model.config().read("Axis1Label", "Z"),
        toDisplayLength(model.getAxis1LinearScalePosMm(), model.getDisplayUnits()),
        model.getDisplayUnits() == DisplayUnits::Imperial ? 4 : 3,
        lengthUnitName(model.getDisplayUnits()));

    for (std::size_t n = 0; n < memoryLabel.size(); ++n) {
        const QColor colour = model.getCurrentMemorySlot() == n ? QColor(255, 255, 255)
                                                                : QColor(100, 100, 100);
        memoryLabel.at(n).colour = colour;
        axis1MemoryValue.at(n).colour = colour;
        axis2MemoryValue.at(n).colour = colour;
        if (model.getAxis1Memory(n) == AXIS1_UNSET) {
            axis1MemoryValue.at(n).text = "        -";
        } else {
            axis1MemoryValue.at(n).text
                = fmt::format("{: >9}", model.formatAxis1Position(model.getAxis1Memory(n)));
        }
        if (model.getAxis2Memory(n) == AXIS2_UNSET) {
            axis2MemoryValue.at(n).text = "        -";
        } else {
            axis2MemoryValue.at(n).text
                = fmt::format("{: >9}", model.formatAxis2Position(model.getAxis2Memory(n)));
        }
    }

    switch (model.getMode()) {
        case Mode::Threading:
            notification.text = "THREADING";
            break;
        case Mode::Taper:
            notification.text = "TAPERING";
            break;
        case Mode::Radius:
            notification.text = "RADIUS";
            break;
        case Mode::MultiPass:
            notification.text = "MULTI-PASS";
            break;
        default:
            notification.text = "";
    }

    switch (model.getMode()) {
        case Mode::MultiPass:
            {
                updateMultiPassData(model);
                warning.text = model.getWarning();
                if (!model.getWarning().empty()) {
                    warning.colour = ++iteration % 6 < 3 ? RED : YELLOW;
                }
                break;
            }
        case Mode::Setup:
            {
                // TODO probably remove this
                mode.text = "Setup";
                misc1.text = "This mode allows you to determine backlash compensation";
                misc2.text = "Use a dial indicator to find number of steps backlash per axis";
                misc3.text = "REMEMBER to unset any previous-set backlash figures in config!";
                misc4.text = "";
                misc5.text = fmt::format(
                    "Z step: {}   X step: {}",
                    model.getAxis1MotorCurrentStep(),
                    model.getAxis2MotorCurrentStep());
                warning.text = "Press Esc to exit setup";
                break;
            }
        case Mode::Threading:
            {
                updateThreadData(model);
                misc4.text = "";
                misc5.text = "";
                // e.g. the spindle is too fast for the pitch
                warning.text = model.getWarning();
                if (!model.getWarning().empty()) {
                    warning.colour = ++iteration % 6 < 3 ? RED : YELLOW;
                }
                break;
            }
        // (The taper angle or radius is shown by taperOrRadius)
        case Mode::Taper:
        case Mode::Radius:
        case Mode::None:
            {
                mode.text = "";
                misc1.text = "";
                misc2.text = "";
                misc3.text = "";
                misc4.text = "";
                misc5.text = "";
                warning.text = model.getWarning();
                if (!model.getWarning().empty()) {
                    warning.colour = ++iteration % 6 < 3 ? RED : YELLOW;
                }
                break;
            }
        default:
            {
                assert(false);
            }
    }
}

void ViewQt::Impl::updateMultiPassData(const Model& model)
{
    const MultiPass& mp = model.multiPass();
    const MultiPassSetup& options = model.getMultiPassSetup();
    const MultiPassRange& range = model.getMultiPassRange();
    const std::string z = model.config().read("Axis1Label", "Z");
    const std::string x = model.config().read("Axis2Label", "X");
    const auto xLength = [&model](double mm) {
        return fmt::format(
            "{:.{}f} {}", model.toDisplayUnits(2, mm), model.axisDecimals(2), model.axisUnitName(2));
    };
    const auto position = [&model](int axis, double mm) {
        const int decimals = model.axisDecimals(axis);
        double value = model.toDisplayUnits(axis, mm);
        // (Not "-0.00")
        if (std::abs(value) < 0.5 * std::pow(10.0, -decimals)) {
            value = 0.0;
        }
        return fmt::format("{:.{}f}", value, decimals);
    };
    // e.g. "Z 12.35 to 5.35, X 10.20 to 9.70"
    mode.text = fmt::format(
        "{} {} to {}, {} {} to {}",
        z,
        position(1, range.cutStart),
        position(1, range.cutEnd),
        x,
        position(2, range.feedStart),
        position(2, range.feedEnd));
    if (options.feedPerPass < MultiPass::TOLERANCE) {
        misc1.text = "Repeating the same cut";
    } else {
        misc1.text = fmt::format("{} step-over {} per pass", x, xLength(options.feedPerPass));
    }
    if (options.pause) {
        misc1.text += ", pausing";
    }
    if (options.retract) {
        misc1.text += ", retracting";
    }

    const std::string pass = mp.totalPasses() == 0
        ? fmt::format("Pass {}", mp.pass())
        : fmt::format("Pass {} of {}", mp.pass(), mp.totalPasses());
    switch (mp.state()) {
        case MultiPass::State::Idle:
            {
                const std::string stopped = mp.message().empty() ? "" : "Stopped. ";
                if (model.multiPassMovingToStart()) {
                    misc2.text = "Moving to the start";
                } else if (model.multiPassAtStart() && !model.getIsAxis2Retracted()) {
                    misc2.text = stopped + "At the start: press Enter to start cutting";
                } else {
                    misc2.text = stopped + "Press Enter to move to the start";
                    if (options.retractBeforeMoveToStart) {
                        misc2.text += " (" + x + " retracts first)";
                    }
                }
            }
            break;
        case MultiPass::State::Cutting:
            misc2.text = pass + ": cutting";
            break;
        case MultiPass::State::Retracting:
            misc2.text = pass + ": retracting";
            break;
        case MultiPass::State::Returning:
            misc2.text = pass + ": returning";
            break;
        case MultiPass::State::Paused:
            misc2.text = mp.plan().feedManual
                ? fmt::format("{} done: feed {} by {}, then press Enter", pass, x, xLength(mp.nextFeed()))
                : pass + " done: press Enter for the next (" + x + " can be nudged)";
            break;
        case MultiPass::State::Unretracting:
            misc2.text = pass + ": unretracting";
            break;
        case MultiPass::State::Feeding:
            misc2.text = pass + ": stepping over";
            break;
        case MultiPass::State::Finishing:
            misc2.text = "Finishing";
            break;
        case MultiPass::State::Finished:
            misc2.text = model.multiPassAtStart()
                ? fmt::format("Finished ({} passes). Enter starts again", mp.pass())
                : fmt::format(
                      "Finished ({} passes). Enter moves to the start, to go again", mp.pass());
            break;
    }
    // How far X has stepped over since the first pass
    misc3.text = "";
    if (mp.pass() > 0) {
        misc3.text = fmt::format("{} stepped over: {}", x, xLength(mp.fed()));
        if (mp.totalPasses() != 0) {
            misc3.text += fmt::format(
                " of {}", xLength(std::abs(mp.plan().feedEnd - mp.plan().feedStart)));
        }
    }
    misc4.text = mp.inProgress() || model.multiPassMovingToStart() ? "Space stops" : "";
    misc5.text = "";
}

void ViewQt::Impl::updateThreadData(const Model& model)
{
    mode.text = "";
    const Thread* thread = model.getThread();
    if (!thread) {
        misc1.text = "No thread chosen";
        misc2.text = "";
        misc3.text = "";
        return;
    }
    const bool female = model.getThreadChoice().female;
    misc1.text = fmt::format(
        "Thread: {} {}, pitch {}",
        thread->name,
        female ? "female (internal)" : "male (external)",
        describePitch(*thread));
    if (female) {
        misc2.text = fmt::format(
            "Bore: {:.3f} mm, depth: {:.3f} mm", thread->femaleBore(), thread->femaleDepth());
    } else {
        misc2.text = fmt::format(
            "OD: {:.3f} mm, depth: {:.3f} mm", thread->maleOd(), thread->maleDepth());
    }
    misc3.text = fmt::format(
        "{} retracts {}",
        model.config().read("Axis2Label", "X"),
        model.getRetractionDirection() == XDirection::Inwards ? "inwards" : "outwards");
}

} // namespace mgo
