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
#include <QFontMetricsF>
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
#include <optional>
#include <variant>

#include <fmt/format.h>

namespace mgo {

namespace {

// The display is laid out in these "logical" coordinates (the original SFML
// window size), and scaled to fit the actual window
constexpr int LOGICAL_WIDTH = 1280;
// Where the status line's items usually go, and the least space between them
constexpr float STATUS_GENERAL_X = 20;
constexpr float STATUS_AXIS1_X = 350;
constexpr float STATUS_AXIS2_X = 550;
constexpr float STATUS_CHUCK_X = 750;
constexpr float STATUS_GAP = 30;
constexpr int LOGICAL_HEIGHT = 720;

// How long after a leader key (e.g. Z) is pressed its hints are shown
constexpr qint64 LEADER_HINT_DELAY_MS = 500;

// The colours SFML provided as constants
const QColor GREEN(0, 255, 0);
const QColor RED(255, 0, 0);
const QColor YELLOW(255, 255, 0);
const QColor WHITE(255, 255, 255);

// The flashing warning line, saying how to clear it if it can be: Esc was
// pressed to, out of habit, which leaves the mode (on the mill, it ended a
// multi-pass part way through)
std::string warningText(const Model& model)
{
    const std::string warning = model.getWarning();
    return warning.empty() || !model.warningClearable() ? warning : warning + "  [F9 to clear]";
}

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
// The key mode an axis's leader key gives
KeyMode axisKeyMode(int axisNumber)
{
    switch (axisNumber) {
        case 1:
            return KeyMode::Axis1;
        case 2:
            return KeyMode::Axis2;
        case 3:
            return KeyMode::Axis3;
        case 4:
            return KeyMode::Axis4;
        default:
            return KeyMode::None;
    }
}

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
        m_rapidReleaseTimer.setSingleShot(true);
        m_rapidReleaseTimer.setInterval(RAPID_RELEASE_MS);
        QObject::connect(&m_rapidReleaseTimer, &QTimer::timeout, this, [this]() {
            // The rapid's arrow key really was let go
            m_rapidQtKey = 0;
            m_keys.push_back(key::RAPID_RELEASED);
        });
    }

    // How long after a rapid's arrow key is released before the motors stop.
    // A key held down repeats, and under X11 each repeat is a release then a
    // press, which Qt marks as a repeat only if the press has already arrived
    // when it sees the release. On a busy Pi it sometimes hasn't, so the
    // release looked like letting go: the rapid stopped dead, and the press
    // started it again (seen on the mill, 14 Nov 2026: X stopping and
    // starting about once a second, at 800 mm/min, during a held Alt+Left).
    // So a release only stops it if the key isn't pressed again within this
    // time. (About 0.8 mm at 800 mm/min. Space stops everything at once)
    static constexpr int RAPID_RELEASE_MS = 60;

    // What to draw, in order. Set by the view on each updateDisplay()
    std::vector<DrawItem> frame;
    // Dims the display (while a dialog is shown on top of it)
    bool dimmed { false };
    // The menu (owned by the controller), if any. While it's open, Up and
    // Down auto-repeat (to move through it quickly)
    const Menu* menu { nullptr };

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
                painter.setPen(
                    rect->border == Qt::transparent ? QPen(Qt::NoPen) : QPen(rect->border, 2));
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
                    QSizeF(
                        rightAlign ? item.rightAlignWidth : LOGICAL_WIDTH, item.pixelSize * 2.0)),
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
        if (isModifierKey(qtKey)) {
            return;
        }
        // While a rapid's arrow key is held, a press of it is a repeat, whether
        // or not Qt says so (see RAPID_RELEASE_MS): it keeps going. (Also if
        // Alt has been let go, which stopped it: the repeat mustn't start a
        // jog, which wouldn't stop by itself)
        if (m_rapidQtKey != 0 && qtKey == m_rapidQtKey) {
            m_rapidReleaseTimer.stop();
            return;
        }
        // Otherwise held keys don't repeat: holding an arrow key must not
        // keep jogging the motors
        const bool menuRepeat = event->isAutoRepeat() && menu && menu->isOpen()
            && (qtKey == Qt::Key_Up || qtKey == Qt::Key_Down) && !altPressed(event);
        if (event->isAutoRepeat() && !menuRepeat) {
            return;
        }
        // Ignore the same key pressed again within 100ms (as the SFML version did)
        if (!menuRepeat && qtKey == m_lastQtKey && m_lastKeyTime.elapsed() < 100) {
            return;
        }
        m_lastQtKey = qtKey;
        m_lastKeyTime.restart();
        const int k = convertKey(event);
        if (k != key::None) {
            m_keys.push_back(k);
        }
        if (isArrowKey(qtKey) && altPressed(event)) {
            // A rapid: it moves while the arrow (and Alt) are held
            m_rapidQtKey = qtKey;
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
            m_keys.push_back(key::RAPID_RELEASED);
            return;
        }
        if (qtKey == m_rapidQtKey) {
            // (Unless it's a repeat: see RAPID_RELEASE_MS)
            m_rapidReleaseTimer.start();
            return;
        }
        if (isArrowKey(qtKey) && altPressed(event)) {
            m_keys.push_back(key::RAPID_RELEASED);
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
    // The arrow key of a rapid, while it's held (0 if none). See
    // RAPID_RELEASE_MS
    int m_rapidQtKey { 0 };
    QTimer m_rapidReleaseTimer;
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

    // Each axis's line: the motorised axes' at the top, then the manual
    // axes' (e.g. the mill's Y and Z), as on a DRO, a little smaller
    struct AxisLine {
        int axisNumber { 0 };
        TextItem label;
        TextItem position;
        TextItem units;
        // A motorised axis's speed
        TextItem speed;
        // A motorised axis's scale, if it has one, after its speed
        TextItem scale;
        // A manual axis's "no reading", if it hasn't one
        TextItem status;
        // While a manual axis goes to a memory slot: the distance to wind it
        // (see Model::manualAxisGoTo())
        TextItem goTo;
    };
    std::vector<AxisLine> axisLines;
    AxisLine& axisLine(int axisNumber);
    // Whether an axis's line and memory row are shown: not a motorised axis
    // turned off in the config (DisableAxis1), nor a manual axis the model
    // refused (e.g. with no scale)
    static bool axisShown(const Model& model, int axisNumber);
    // Each axis's row of memory slots, below their labels
    struct MemoryRow {
        int axisNumber { 0 };
        TextItem label;
        std::vector<TextItem> values;
    };
    std::vector<MemoryRow> memoryRows;

    // Main text items which are always displayed:
    TextItem rpmLabel;
    TextItem rpm;
    TextItem rpmUnits;
    TextItem generalStatus;
    TextItem axis1Status;
    TextItem axis2Status;
    TextItem chuckRpm;
    TextItem warning;
    TextItem taperAngle;
    TextItem surfaceSpeed; // Vc

    // Text items which are displayed sometimes:
    TextItem mode;
    TextItem misc1;
    TextItem misc2;
    TextItem misc3;
    TextItem misc4;
    TextItem misc5;
    TextItem misc6;

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
    // (See settlingOverlayText())
    void drawSettlingOverlay(std::vector<DrawItem>& frame, const Model& model) const;

    // The current feature (diameter and work end), when either is set
    void drawFeature(std::vector<DrawItem>& frame, const Model& model) const;
    // The tool in use (with the tool library)
    void drawTool(std::vector<DrawItem>& frame, const Model& model) const;
    // While measuring a tool: its steps, in the mode's lines
    void updateToolMeasurementData(const Model& model);

    std::vector<TextItem> memoryLabel;

    std::size_t iteration { 0 };

    void updateTextFromModel(const Model& model);
    // Where each status line item goes, by how long the ones before it are
    void layOutStatusLine(const Model& model);
    // How wide an item's text is, in the display's logical pixels
    float textWidth(const TextItem& item) const;
    void updateThreadData(const Model& model);
    void updateMultiPassData(const Model& model);
    // The knurl, on one line (e.g. its starts and lead)
    std::string knurlDescription(const Model& model) const;
    void updateGearData(const Model& model);
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
    d.widget->menu = d.menu;

#ifdef FAKE
    // run in a window in "fake" mode for manual testing
    d.widget->resize(LOGICAL_WIDTH, LOGICAL_HEIGHT);
    d.widget->move(50, 50);
    d.widget->show();
#else
    d.widget->showFullScreen();
    // Everything is driven from the keyboard or gamepad, so hide the mouse
    // pointer. An application-wide override also covers the dialogs
    QGuiApplication::setOverrideCursor(Qt::BlankCursor);
#endif
    // (The "offscreen" platform, used by the UI tests, has no windows to raise)
    if (QGuiApplication::platformName() != "offscreen") {
        d.widget->raise();
        d.widget->activateWindow();
    }
    if (const QScreen* screen = d.widget->screen()) {
        MGOLOG(
            fmt::format("Screen size is {} x {}", screen->size().width(), screen->size().height()));
    }

    const auto make = [](float x, float y, int pixelSize, QColor colour = WHITE) {
        return TextItem { QPointF(x, y), pixelSize, colour, "" };
    };

    for (int n = 1; n <= 2; ++n) {
        const float y = n == 1 ? 10.f : 70.f;
        Impl::AxisLine line;
        line.axisNumber = n;
        line.label = make(20, y, 60);
        line.label.text = model.axisLabel(n) + ":";
        line.position = make(110, y, 60, GREEN);
        line.units = make(430, y + 30, 30, QColor(0, 127, 0));
        line.speed = make(550, y + 30, 30, QColor(209, 209, 50));
        // (Lined up with the bottom of the speed's text)
        line.scale = make(850, y + 33, 25, QColor(209, 209, 50));
        d.axisLines.push_back(line);
    }

    // The manual axes' lines go below the motorised axes', in place of the
    // spindle's speed where it isn't shown (as on the mill). They're a little
    // smaller, to fit two there, with their digits lined up with those
    // above. (With the spindle's speed shown as well, they'd run into the
    // memory slots: no machine has that yet)
    constexpr int MANUAL_AXIS_SIZE = 45;
    // (The font's characters are 0.6 of its size wide, and positions 8 wide)
    constexpr float MANUAL_AXIS_POSITION_X = 110 + 8 * 0.6f * (60 - MANUAL_AXIS_SIZE);
    float manualAxisY = model.config().readBool("DisableRpm", false) ? 130 : 190;
    // (From the config, as the view is set up before the model makes the
    // axes: one the model refuses, e.g. with no scale, isn't drawn)
    for (int n = Model::FIRST_MANUAL_AXIS; n <= Model::LAST_MANUAL_AXIS; ++n) {
        if (model.config().read(fmt::format("Axis{}Kind", n), "none") == "manual") {
            Impl::AxisLine line;
            line.axisNumber = n;
            line.label = make(20, manualAxisY, MANUAL_AXIS_SIZE);
            line.label.text
                = model.config().read(fmt::format("Axis{}Label", n), n == 3 ? "Y" : "Z") + ":";
            line.position = make(MANUAL_AXIS_POSITION_X, manualAxisY, MANUAL_AXIS_SIZE, GREEN);
            line.units = make(430, manualAxisY + 15, 30, QColor(0, 127, 0));
            line.status = make(550, manualAxisY + 20, 20, QColor(209, 209, 50));
            line.goTo = make(550, manualAxisY + 10, 30, YELLOW);
            d.axisLines.push_back(line);
            manualAxisY += 50;
        }
    }

    d.rpmLabel = make(20, 130, 60);
    d.rpmLabel.text = "n:";
    d.rpm = make(150, 130, 60, GREEN);
    d.rpmUnits = make(430, 160, 30, QColor(0, 127, 0));
    d.rpmUnits.text = "rpm";

    // Memory banks. With manual axes, a row for each of them too, so the
    // rows are a little smaller and closer, to fit above the mode's lines
    const QColor grey(128, 128, 128);
    const bool manualRows = d.axisLines.size() > 2;
    const float memoryY = manualRows ? 232.f : 250.f;
    const int memorySize = manualRows ? 25 : 30;
    const float rowGap = manualRows ? 26.f : 30.f;
    // (The values, 9 characters, right-aligned under each slot's label)
    const float columnWidth = 180.f;
    const float valuesX = manualRows ? 60.f + 9 * 0.6f * (30 - 25) : 60.f;
    for (unsigned n = 0; n < model.getMemorySize(); ++n) {
        auto label = make(valuesX + n * columnWidth, memoryY, memorySize, grey);
        label.text = fmt::format("    Mem {}", n + 1);
        d.memoryLabel.push_back(label);
    }
    float rowY = memoryY + 25;
    for (const auto& line : d.axisLines) {
        Impl::MemoryRow row;
        row.axisNumber = line.axisNumber;
        row.label = make(24, rowY, memorySize, grey);
        row.label.text = line.label.text;
        for (unsigned n = 0; n < model.getMemorySize(); ++n) {
            row.values.push_back(make(valuesX + n * columnWidth, rowY, memorySize, grey));
        }
        d.memoryRows.push_back(row);
        rowY += rowGap;
    }

    // STATUS BAR (at bottom of screen). These are where its items usually
    // go: one which is longer than usual moves those after it along (see
    // updateDisplay())
    constexpr float STATUS_BAR_Y = 650;
    d.generalStatus = make(STATUS_GENERAL_X, STATUS_BAR_Y, 20, GREEN);
    d.axis1Status = make(STATUS_AXIS1_X, STATUS_BAR_Y, 20, GREEN);
    d.axis2Status = make(STATUS_AXIS2_X, STATUS_BAR_Y, 20, GREEN);
    d.chuckRpm = make(STATUS_CHUCK_X, STATUS_BAR_Y, 20, GREEN);

    // Miscellaneous - this is the bank of text which contains, for example,
    // the help, or text for, say, threading
    constexpr float MISC_Y = 380;
    d.mode = make(20, MISC_Y, 25, YELLOW);
    d.misc1 = make(20, MISC_Y + 30, 25);
    d.misc2 = make(20, MISC_Y + 60, 25);
    d.misc3 = make(20, MISC_Y + 90, 25);
    d.misc4 = make(20, MISC_Y + 120, 25);
    d.misc5 = make(20, MISC_Y + 150, 25);
    d.misc6 = make(20, MISC_Y + 180, 25);

    d.warning = make(20, MISC_Y + 210, 20, RED);
    // (The mode's name, and those below it, are a line lower than they
    // were, to make room for axis 1's scale on its row. Those below the
    // mode's name go a line lower still if axis 2 has a scale, to make room
    // for it on its row too)
    const float belowMode = model.config().read("Axis2Scale", "none") != "none" ? 30.f : 0.f;
    d.notification = make(860, 75, 25, RED);
    d.taperAngle = make(860, 105 + belowMode, 20, RED);
    d.xRetracted = make(860, 135 + belowMode, 25, RED);
    d.xRetracted.text = "RETRACTED";
    d.xRetractDirection = make(860, 165 + belowMode, 25, RED);
    d.xRetractDirection.text = "-X RTRCT";
    // (Surface speed, on the rpm line, in line with the axes' feed rates)
    d.surfaceSpeed = make(550, 160, 30, QColor(209, 209, 50));
    d.fault = make(20, STATUS_BAR_Y - 30, 22, RED);

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
    const MultiPassDialogInfo& info,
    std::function<void(std::optional<MultiPassSetup>)> onDone)
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

void ViewQt::openGearDialog(
    const GearDialogInfo& info,
    std::function<void(std::optional<GearSetup>)> onDone)
{
    assert(!isDialogOpen()); // (one at a time)
    m_impl->setDimmed(true);
    m_impl->dialog = dialog::openGearDialog(
        m_impl->widget.get(),
        m_impl->fontFamily,
        info,
        [this, onDone = std::move(onDone)](std::optional<GearSetup> setup) {
            m_impl->dialogClosed();
            onDone(setup);
        });
}

void ViewQt::openRadiusDialog(
    const RadiusDialogInfo& info,
    std::function<void(std::optional<RadiusSetup>)> onDone)
{
    assert(!isDialogOpen()); // (one at a time)
    m_impl->setDimmed(true);
    m_impl->dialog = dialog::openRadiusDialog(
        m_impl->widget.get(),
        m_impl->fontFamily,
        info,
        [this, onDone = std::move(onDone)](std::optional<RadiusSetup> setup) {
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
    // Take the keyboard focus back from the dialog. Otherwise the window
    // manager may give it elsewhere (seen on Raspberry Pi OS: the desktop's
    // panel appeared, and keys were lost until the window was clicked).
    // (The "offscreen" platform, used by the UI tests, has no windows to raise)
    if (QGuiApplication::platformName() != "offscreen") {
        widget->raise();
        widget->activateWindow();
    }
}

void ViewQt::setMenu(const Menu* menu)
{
    m_impl->menu = menu;
    if (m_impl->widget) {
        m_impl->widget->menu = menu;
    }
}

namespace {

// The menu panel's width, the margin inside it, and its rows' font size
constexpr float MENU_PANEL_WIDTH = 480.f;
constexpr float MENU_MARGIN = 15.f;
constexpr int MENU_FONT_SIZE = 20;

// A menu row's text, and the index of its hotkey's character (or -1 if it has
// none). The hotkey is its first appearance in the text; if it isn't in the
// text, it's added in brackets at the end
std::pair<std::string, int> menuRowText(std::string text, char hotkey)
{
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
    return { std::move(text), index };
}

} // end anonymous namespace

std::vector<float> lineUpItems(const std::vector<std::pair<float, float>>& items, float gap)
{
    std::vector<float> positions;
    std::optional<float> previousEnd;
    for (const auto& [x, width] : items) {
        const float position = previousEnd ? std::max(x, *previousEnd + gap) : x;
        positions.push_back(position);
        previousEnd = position + width;
    }
    return positions;
}

float ViewQt::Impl::textWidth(const TextItem& item) const
{
    QFont font(fontFamily);
    font.setPixelSize(item.pixelSize);
    font.setBold(item.bold);
    return static_cast<float>(
        QFontMetricsF(font).horizontalAdvance(QString::fromStdString(item.text)));
}

void ViewQt::Impl::layOutStatusLine(const Model& model)
{
    // (Only the items shown, left to right)
    std::vector<TextItem*> items { &generalStatus };
    std::vector<float> usual { STATUS_GENERAL_X };
    if (!model.config().readBool("DisableAxis1", false)) {
        items.push_back(&axis1Status);
        usual.push_back(STATUS_AXIS1_X);
    }
    if (!model.config().readBool("DisableAxis2", false)) {
        items.push_back(&axis2Status);
        usual.push_back(STATUS_AXIS2_X);
    }
    items.push_back(&chuckRpm);
    usual.push_back(STATUS_CHUCK_X);
    std::vector<std::pair<float, float>> sizes;
    for (std::size_t n = 0; n < items.size(); ++n) {
        sizes.emplace_back(usual[n], textWidth(*items[n]));
    }
    const std::vector<float> positions = lineUpItems(sizes, STATUS_GAP);
    for (std::size_t n = 0; n < items.size(); ++n) {
        items[n]->position.setX(positions[n]);
    }
}

bool ViewQt::menuRowFits(const std::string& description, char hotkey, const std::string& shortcut)
    const
{
    QFont font(m_impl->fontFamily);
    font.setPixelSize(MENU_FONT_SIZE);
    const QFontMetricsF metrics(font);
    const QString text = QString::fromStdString(menuRowText(description, hotkey).first);
    // (At least a space between the text and its shortcut)
    const QString row = shortcut.empty() ? text : text + " " + QString::fromStdString(shortcut);
    return metrics.horizontalAdvance(row) <= MENU_PANEL_WIDTH - 2 * MENU_MARGIN;
}

void ViewQt::Impl::drawMenu(std::vector<DrawItem>& frame, const Model& model) const
{
    // A panel down the right-hand side, so the position read-outs stay
    // visible. It stops short of the motor fault line, so that's never hidden
    constexpr float panelX = 790.f;
    constexpr float panelY = 10.f;
    constexpr float panelWidth = MENU_PANEL_WIDTH;
    constexpr float panelHeight = 590.f;
    constexpr float margin = MENU_MARGIN;
    constexpr float listY = panelY + 55.f;
    constexpr float lineHeight = 30.f;
    constexpr int fontSize = MENU_FONT_SIZE;
    constexpr std::size_t visibleRows = 14;
    const QColor normal(200, 200, 200);
    const QColor unavailable(90, 90, 90);
    const QColor shortcutColour(150, 150, 150);
    const QColor hint(140, 140, 140);

    frame.push_back(
        RectItem { QRectF(panelX, panelY, panelWidth, panelHeight),
                   QColor(25, 25, 25),
                   QColor(128, 128, 128) });

    const auto category = menu->openCategory();
    frame.push_back(
        TextItem { QPointF(panelX + margin, panelY + 12.f),
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
    const auto makeRow
        = [](const std::string& text, std::string shortcut, bool available, char hotkey) {
              auto [rowText, index] = menuRowText(text, hotkey);
              return Row { std::move(rowText), std::move(shortcut), available, index };
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
            frame.push_back(
                RectItem { QRectF(panelX + 5.f, y - 3.f, panelWidth - 10.f, lineHeight),
                           QColor(60, 60, 60) });
        }
        const QColor colour = !row.available ? unavailable : selected ? WHITE : normal;
        TextItem text { QPointF(panelX + margin, y), fontSize, colour, row.text, selected };
        if (row.available) {
            text.highlightIndex = row.hotkeyIndex;
        }
        frame.push_back(text);
        frame.push_back(
            TextItem { QPointF(panelX + margin, y),
                       fontSize,
                       row.available ? shortcutColour : unavailable,
                       row.shortcut,
                       false,
                       panelWidth - 2 * margin });
        y += lineHeight;
    }
    if (first > 0) {
        frame.push_back(
            TextItem { QPointF(panelX + margin, listY - 22.f),
                       16,
                       hint,
                       "(more above)",
                       false,
                       panelWidth - 2 * margin });
    }
    if (first + visibleRows < rows.size()) {
        frame.push_back(
            TextItem { QPointF(panelX + margin, y),
                       16,
                       hint,
                       "(more below)",
                       false,
                       panelWidth - 2 * margin });
    }

    // How to use it
    const float footerY = panelY + panelHeight - 90.f;
    frame.push_back(
        TextItem {
            QPointF(panelX + margin, footerY), 16, hint, "Up/Down: select   Enter/Right: choose" });
    frame.push_back(
        TextItem { QPointF(panelX + margin, footerY + 20.f),
                   16,
                   hint,
                   "   (or press the highlighted letter)" });
    frame.push_back(
        TextItem { QPointF(panelX + margin, footerY + 40.f),
                   16,
                   hint,
                   "Left/Esc: back   Tab: close menu" });
    frame.push_back(
        TextItem { QPointF(panelX + margin, footerY + 60.f),
                   16,
                   YELLOW,
                   "Space stops all motors. Rapids: Alt+arrow" });
}

void ViewQt::Impl::drawFeature(std::vector<DrawItem>& frame, const Model& model) const
{
    const auto diameter = model.getFeatureDiameterMm();
    const auto workEnd = model.getWorkEndZ();
    if (!model.currentFeatureEnabled() || (!diameter && !workEnd)) {
        return;
    }
    // Below the memory slots, to the right of the mode's first four lines
    // (which have to stay short to fit, about 65 characters: longer lines go
    // further down, below the box)
    constexpr float boxX = 1010.f;
    constexpr float boxY = 380.f;
    constexpr float boxWidth = 255.f;
    constexpr float boxHeight = 112.f;
    constexpr float margin = 12.f;
    constexpr int fontSize = 26;
    const QColor grey(128, 128, 128);
    const QColor value(0, 192, 0);
    frame.push_back(RectItem { QRectF(boxX, boxY, boxWidth, boxHeight), QColor(20, 20, 20), grey });
    frame.push_back(TextItem { QPointF(boxX + margin, boxY + 8.f), 18, grey, "Current feature" });
    const auto line = [&](float y, const std::string& label, const std::string& text) {
        frame.push_back(TextItem { QPointF(boxX + margin, y), fontSize, grey, label });
        TextItem item { QPointF(boxX + margin, y), fontSize, text == "-" ? grey : value, text };
        item.rightAlignWidth = boxWidth - 2.f * margin;
        frame.push_back(item);
    };
    const auto length = [&model](int axis, double mm) {
        const int decimals = model.axisDecimals(axis);
        double v = model.toDisplayUnits(axis, mm);
        // (Not "-0.00")
        if (std::abs(v) < 0.5 * std::pow(10.0, -decimals)) {
            v = 0.0;
        }
        return fmt::format("{:.{}f} {}", v, decimals, model.axisUnitName(axis));
    };
    line(boxY + 36.f, "Ø", diameter ? length(2, *diameter) : "-");
    // (The work's end, as "Z 0.00 mm": not "Z end", which in the multi-pass
    // dialog is the cut's end)
    line(boxY + 72.f, "End", workEnd ? model.axisLabel(1) + " " + length(1, *workEnd) : "-");
}

void ViewQt::Impl::drawTool(std::vector<DrawItem>& frame, const Model& model) const
{
    if (!model.toolLibraryEnabled()) {
        return;
    }
    const Tool& tool = model.toolLibrary().current();
    // At the top right, above the memory slots, clear of the mode's name and
    // RETRACTED (which are further left)
    constexpr float boxX = 1010.f;
    constexpr float boxY = 112.f;
    constexpr float boxWidth = 255.f;
    constexpr float boxHeight = 112.f;
    constexpr float margin = 12.f;
    const QColor grey(128, 128, 128);
    const QColor value(0, 192, 0);
    frame.push_back(RectItem { QRectF(boxX, boxY, boxWidth, boxHeight), QColor(20, 20, 20), grey });
    frame.push_back(TextItem { QPointF(boxX + margin, boxY + 8.f), 18, grey, "Tool" });
    const bool known = model.toolLibrary().currentIsKnown();
    if (!known) {
        // The positions still read the last tool's tip, if they haven't been
        // set since (see Model::positionsTool())
        if (const auto positions = model.positionsTool()) {
            TextItem as { QPointF(boxX + margin, boxY + 8.f),
                          18,
                          grey,
                          fmt::format("positions: T{}", *positions) };
            as.rightAlignWidth = boxWidth - 2.f * margin;
            frame.push_back(as);
        }
    }
    // Its number, large (in yellow if it isn't in the library), with the
    // nose radius on the right
    frame.push_back(
        TextItem { QPointF(boxX + margin, boxY + 30.f),
                   40,
                   known ? value : YELLOW,
                   known ? fmt::format("T{}", tool.number) : std::string("T?") });
    if (known) {
        TextItem radius { QPointF(boxX + margin, boxY + 44.f),
                          22,
                          grey,
                          fmt::format("r {:.2f} mm", tool.noseRadiusMm) };
        radius.rightAlignWidth = boxWidth - 2.f * margin;
        frame.push_back(radius);
    }
    // (The font is fixed-width: about 21 characters fit)
    constexpr std::size_t maxChars = 21;
    std::string description = tool.description;
    if (description.size() > maxChars) {
        description = description.substr(0, maxChars - 2) + "..";
    }
    frame.push_back(TextItem { QPointF(boxX + margin, boxY + 80.f), 18, WHITE, description });
}

void ViewQt::Impl::updateToolMeasurementData(const Model& model)
{
    const ToolLibrary& library = model.toolLibrary();
    const int from = library.current().number;
    const int number = model.toolMeasurementNumber();
    const std::string z = model.axisLabel(1);
    const std::string x = model.axisLabel(2);
    const auto step = model.toolTouchStep();
    mode.text = fmt::format(
        "Measuring T{}{} from T{}: Enter records, Esc cancels",
        number,
        library.find(number) ? "" : " (new)",
        from);
    // Each step, marked as done or to do next (kept short: the boxes are to
    // their right)
    const auto line = [step](ToolTouchStep s, const std::string& text) {
        if (step > s) {
            return "  done: " + text;
        }
        return (step == s ? "> " : "  ") + text;
    };
    misc1.text = line(
        ToolTouchStep::ReferenceDiameter, fmt::format("T{}: touch the diameter ({} in)", from, x));
    misc2.text = line(
        ToolTouchStep::ReferenceEnd, fmt::format("T{}: touch the end ({} to chuck)", from, z));
    misc3.text = line(
        ToolTouchStep::ToolDiameter,
        fmt::format("change to T{}, touch the diameter ({} in)", number, x));
    misc4.text
        = line(ToolTouchStep::ToolEnd, fmt::format("T{}: touch the end ({} to chuck)", number, z));
    misc5.text = "";
    misc6.text = "Only scratch the surfaces. External tools only, so far";
}

std::vector<std::string> settlingOverlayText(const Model& model)
{
    const auto state = model.multiPass().state();
    if (state != MultiPass::State::Settling && state != MultiPass::State::FeedSettling) {
        return {};
    }
    // (The feed axis's, after the feed, or the cut axis's)
    const int axis = state == MultiPass::State::FeedSettling ? 2 : 1;
    return { fmt::format("Waiting for {}'s scale", model.axisLabel(axis)),
             "to settle before the next cut",
             "(Space stops)" };
}

void ViewQt::Impl::drawSettlingOverlay(std::vector<DrawItem>& frame, const Model& model) const
{
    const auto lines = settlingOverlayText(model);
    if (lines.empty()) {
        return;
    }
    // A box in the middle of the display, over the axes, but not a dialog:
    // keys still work (Space stops), and it goes by itself once settled
    constexpr float width = 800.f;
    constexpr float height = 200.f;
    constexpr float x = (LOGICAL_WIDTH - width) / 2.f;
    constexpr float y = (LOGICAL_HEIGHT - height) / 2.f;
    frame.push_back(RectItem { QRectF(x, y, width, height), QColor(40, 40, 0), YELLOW });
    // Each line centred (the font's characters are 0.6 of its size wide)
    const auto centred = [&](const std::string& text, float top, int size, QColor colour) {
        const float textWidth = 0.6f * static_cast<float>(size * text.size());
        frame.push_back(
            TextItem { QPointF(x + (width - textWidth) / 2.f, top), size, colour, text, true });
    };
    centred(lines.at(0), y + 25.f, 50, YELLOW);
    centred(lines.at(1), y + 90.f, 40, YELLOW);
    centred(lines.at(2), y + 145.f, 26, QColor(200, 200, 200));
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
            title = model.axisLabel(1) + " then...";
            break;
        case KeyMode::Axis2:
            title = model.axisLabel(2) + " then...";
            break;
        case KeyMode::AxisAll:
            title = "\\ (both axes) then...";
            break;
        case KeyMode::Axis3:
        case KeyMode::Axis4:
            if (const ManualAxis* axis
                = model.manualAxis(model.getKeyMode() == KeyMode::Axis3 ? 3 : 4)) {
                title = axis->label() + " then...";
            }
            break;
        case KeyMode::None:
            return;
    }

    const float panelHeight = 55.f + lineHeight * commands.size() + 60.f;
    frame.push_back(
        RectItem { QRectF(panelX, panelY, panelWidth, panelHeight),
                   QColor(25, 25, 25),
                   QColor(128, 128, 128) });
    frame.push_back(TextItem { QPointF(panelX + margin, panelY + 12.f), 26, GREEN, title });
    float y = panelY + 55.f;
    for (const Command* c : commands) {
        const bool available = isAvailable(*c, model);
        frame.push_back(
            TextItem { QPointF(panelX + margin, y),
                       fontSize,
                       available ? YELLOW : unavailable,
                       followUpKeyName(c->key) });
        frame.push_back(
            TextItem { QPointF(panelX + margin + keyColumnWidth, y),
                       fontSize,
                       available ? normal : unavailable,
                       c->description });
        y += lineHeight;
    }
    frame.push_back(
        TextItem { QPointF(panelX + margin, y + 10.f),
                   16,
                   hint,
                   "Other keys cancel.  Tab: the full menu" });
    frame.push_back(
        TextItem { QPointF(panelX + margin, y + 30.f), 16, YELLOW, "Space stops all motors" });
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
    d.layOutStatusLine(model);

    std::vector<DrawItem> frame;
    const auto draw = [&frame](const TextItem& item) { frame.push_back(item); };

    if (!model.isShuttingDown()) {
        // (The status line's items for the motorised axes)
        if (d.axisShown(model, 1)) {
            draw(d.axis1Status);
        }
        if (d.axisShown(model, 2)) {
            draw(d.axis2Status);
        }
        for (auto& line : d.axisLines) {
            if (!d.axisShown(model, line.axisNumber)) {
                continue;
            }
            if (model.manualAxis(line.axisNumber)) {
                line.label.colour
                    = model.getKeyMode() == axisKeyMode(line.axisNumber) ? RED : QColor(0, 192, 0);
            } else {
                line.label.colour
                    = model.isAxisLocked(line.axisNumber) ? QColor(90, 90, 90) : QColor(0, 192, 0);
            }
            draw(line.label);
            draw(line.position);
            draw(line.units);
            if (model.manualAxis(line.axisNumber)) {
                draw(line.status);
                if (!line.goTo.text.empty()) {
                    draw(line.goTo);
                }
            } else {
                draw(line.speed);
                if (model.hasScale(line.axisNumber)) {
                    draw(line.scale);
                }
            }
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
        draw(d.chuckRpm);
        draw(d.generalStatus);
        draw(d.warning);
        draw(d.notification);
        if (model.getMode() == Mode::Taper) {
            draw(d.taperAngle);
        }
        if (model.getRetractionDirection() == XDirection::Inwards) {
            draw(d.xRetractDirection);
        }
        if (model.getIsAxis2Retracted()) {
            draw(d.xRetracted);
        }
        for (const auto& label : d.memoryLabel) {
            draw(label);
        }
        // (A row's label is red after its axis's leader key, or \)
        for (auto& row : d.memoryRows) {
            if (!d.axisShown(model, row.axisNumber)) {
                continue;
            }
            const bool leader = model.getKeyMode() == axisKeyMode(row.axisNumber)
                || model.getKeyMode() == KeyMode::AxisAll;
            row.label.colour = leader ? RED : QColor(128, 128, 128);
            draw(row.label);
            for (const auto& value : row.values) {
                draw(value);
            }
        }
        if (model.getMode() != Mode::None || model.isMeasuringTool()) {
            draw(d.mode);
        }
        d.drawFeature(frame, model);
        d.drawTool(frame, model);
        draw(d.misc1);
        draw(d.misc2);
        draw(d.misc3);
        draw(d.misc4);
        draw(d.misc5);
        draw(d.misc6);
        d.drawSettlingOverlay(frame, model);
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
        draw(d.axisLine(1).position);
    }

    d.widget->frame = std::move(frame);
    d.widget->update(); // (repainted by Qt's event loop)
}

ViewQt::Impl::AxisLine& ViewQt::Impl::axisLine(int axisNumber)
{
    for (auto& line : axisLines) {
        if (line.axisNumber == axisNumber) {
            return line;
        }
    }
    throw std::logic_error(fmt::format("No line for axis {}", axisNumber));
}

bool ViewQt::Impl::axisShown(const Model& model, int axisNumber)
{
    if (axisNumber <= 2) {
        return !model.config().readBool(fmt::format("DisableAxis{}", axisNumber), false);
    }
    return model.manualAxis(axisNumber) != nullptr;
}

void ViewQt::Impl::updateTextFromModel(const Model& model)
{
    // Updates all the text objects with data in the model
    if (model.isShuttingDown()) {
        axisLine(1).position.text = "SHUTTING DOWN";
        return;
    }

    // Lengths are shown in the display units (mm or inches); values for an
    // axis which isn't a length (e.g. degrees) are shown as they are
    const auto speedDecimals = [&model](int axis) {
        return model.isLengthAxis(axis) && model.getDisplayUnits() == DisplayUnits::Imperial ? 3
                                                                                             : 2;
    };
    AxisLine& axis1 = axisLine(1);
    AxisLine& axis2 = axisLine(2);
    axis1.position.text = formatMotorPosition(
        model.toDisplayUnits(1, model.motorPosition(1)), model.axisDecimals(1));

    // (When threading or knurling, Z's speed is set by the spindle's)
    const double axis1Speed
        = model.getMode() == Mode::Threading || model.getMode() == Mode::Knurling
        ? model.getThreadingZSpeed()
        : model.motorSpeed(1);
    axis1.speed.text = fmt::format(
        "{:<.{}f} {}/min",
        model.toDisplayUnits(1, axis1Speed),
        speedDecimals(1),
        model.axisUnitName(1));

    if (!model.getIsAxis2Retracted()) {
        axis2.position.text = formatMotorPosition(
            model.toDisplayUnits(2, model.getAxis2Position()), model.axisDecimals(2));
    } else {
        axis2.position.text = "     ---";
    }
    axis2.speed.text = fmt::format(
        "{:<.{}f} {}/min",
        model.toDisplayUnits(2, model.motorSpeed(2)),
        speedDecimals(2),
        model.axisUnitName(2));
    float rpmValue = model.getRotaryEncoderRpm();
    // Rounded down to a multiple of 10, to keep the display steady, except
    // below 50 rpm (e.g. knurling, at 10-30 rpm), where that would lose too
    // much
    if (rpmValue >= 50.f) {
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
    axis1Status.text = fmt::format("{}: {}", model.axisLabel(1), model.axisStatus(1));
    axis2Status.text = fmt::format("{}: {}", model.axisLabel(2), model.axisStatus(2));
    if (model.getMode() == Mode::Taper) {
        taperAngle.text = fmt::format("Angle: {}", model.getTaperAngle());
    }
    chuckRpm.text = fmt::format("C: {: >5.1f} deg", model.getChuckAngle());

    for (auto& line : axisLines) {
        line.units.text = model.axisUnitName(line.axisNumber);
        // (The motorised axes' positions and speeds are above.) Their
        // scale's position, and (in brackets) how far it is from the motor's,
        // which shows any lost steps. Short, as it's on the axis's line, in
        // its units
        if (line.axisNumber < Model::FIRST_MANUAL_AXIS) {
            const int n = line.axisNumber;
            const auto position = model.scalePositionMm(n);
            const auto difference = model.scaleDifferenceMm(n);
            if (position && difference) {
                // (X's position is shown less any multi-pass correction, so
                // its scale's is too, to read alongside it)
                const double shown = n == 2
                    ? *position - (model.motorPosition(2) - model.getAxis2Position())
                    : *position;
                line.scale.text = fmt::format(
                    "scale {:.{}f} ({:+.{}f})",
                    model.toDisplayUnits(n, shown),
                    model.axisDecimals(n),
                    model.toDisplayUnits(n, *difference),
                    model.axisDecimals(n));
            } else if (model.scaleHasReading(n)) {
                line.scale.text = "scale: settling";
            } else {
                line.scale.text = "scale: no reading";
            }
            continue;
        }
        const ManualAxis* axis = model.manualAxis(line.axisNumber);
        const auto position = axis ? axis->positionMm() : std::nullopt;
        if (position) {
            line.position.text = formatMotorPosition(
                model.toDisplayUnits(line.axisNumber, *position),
                model.axisDecimals(line.axisNumber));
            line.status.text.clear();
        } else {
            line.position.text = "     ---";
            line.status.text = "no reading";
        }
        line.goTo.text.clear();
        if (const auto goTo = model.manualAxisGoTo(line.axisNumber)) {
            const auto value = [&](double mm) {
                return fmt::format(
                    "{:.{}f}",
                    model.toDisplayUnits(line.axisNumber, mm),
                    model.axisDecimals(line.axisNumber));
            };
            if (goTo->there) {
                line.goTo.text = fmt::format("at {}: settling", value(goTo->targetMm));
                line.goTo.colour = GREEN;
            } else if (goTo->past) {
                line.goTo.text = fmt::format(
                    "to {}: {} past", value(goTo->targetMm), value(std::abs(goTo->toGoMm)));
                line.goTo.colour = RED;
            } else {
                line.goTo.text = fmt::format(
                    "to {}: {} to go", value(goTo->targetMm), value(std::abs(goTo->toGoMm)));
                line.goTo.colour = YELLOW;
            }
        }
    }
    const auto slotColour = [&model](std::size_t slot) {
        return model.getCurrentMemorySlot() == slot ? QColor(255, 255, 255) : QColor(100, 100, 100);
    };
    for (std::size_t n = 0; n < memoryLabel.size(); ++n) {
        memoryLabel.at(n).colour = slotColour(n);
    }
    for (auto& row : memoryRows) {
        for (std::size_t n = 0; n < row.values.size(); ++n) {
            row.values.at(n).colour = slotColour(n);
            const auto memory = model.getMemory(row.axisNumber, n);
            row.values.at(n).text = memory
                ? fmt::format("{: >9}", model.formatPosition(row.axisNumber, *memory))
                : "        -";
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
        case Mode::Knurling:
            notification.text = "KNURLING";
            break;
        case Mode::GearCutting:
            notification.text = "GEAR CUTTING";
            break;
        default:
            notification.text = "";
    }

    switch (model.getMode()) {
        case Mode::MultiPass:
            {
                updateMultiPassData(model);
                warning.text = warningText(model);
                if (!model.getWarning().empty()) {
                    warning.colour = ++iteration % 6 < 3 ? RED : YELLOW;
                }
                break;
            }
        case Mode::Threading:
            {
                if (model.threadingPassesSet()) {
                    // The thread on the first line, then the passes
                    updateThreadData(model);
                    const std::string thread = misc1.text;
                    updateMultiPassData(model);
                    misc6.text = misc5.text;
                    misc5.text = misc4.text;
                    misc4.text = misc3.text;
                    misc3.text = misc2.text;
                    misc2.text = misc1.text;
                    misc1.text = thread;
                } else {
                    updateThreadData(model);
                    misc4.text = "";
                    misc5.text = "";
                    misc6.text = "";
                }
                // e.g. the spindle is too fast for the pitch
                warning.text = warningText(model);
                if (!model.getWarning().empty()) {
                    warning.colour = ++iteration % 6 < 3 ? RED : YELLOW;
                }
                break;
            }
        case Mode::Knurling:
            {
                // The knurl on the first line, then the passes
                updateMultiPassData(model);
                misc6.text = misc5.text;
                misc5.text = misc4.text;
                misc4.text = misc3.text;
                misc3.text = misc2.text;
                misc2.text = misc1.text;
                misc1.text = knurlDescription(model);
                // e.g. the spindle is too fast for the lead
                warning.text = warningText(model);
                if (!model.getWarning().empty()) {
                    warning.colour = ++iteration % 6 < 3 ? RED : YELLOW;
                }
                break;
            }
        case Mode::GearCutting:
            {
                updateGearData(model);
                warning.text = warningText(model);
                if (!model.getWarning().empty()) {
                    warning.colour = ++iteration % 6 < 3 ? RED : YELLOW;
                }
                break;
            }
        case Mode::Radius:
            {
                updateMultiPassData(model);
                warning.text = warningText(model);
                if (!model.getWarning().empty()) {
                    warning.colour = ++iteration % 6 < 3 ? RED : YELLOW;
                }
                break;
            }
        // (The taper angle is shown by taperAngle)
        case Mode::Taper:
        case Mode::None:
            {
                mode.text = "";
                misc1.text = "";
                misc2.text = "";
                misc3.text = "";
                misc4.text = "";
                misc5.text = "";
                misc6.text = "";
                // (Measuring a tool needs no mode: see Model::startToolMeasurement())
                if (model.isMeasuringTool()) {
                    updateToolMeasurementData(model);
                }
                warning.text = warningText(model);
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
    const MultiPassSetup& options = model.activeMultiPassSetup();
    // (Threading and knurling passes feed in, rather than step over)
    const bool threading = model.getMode() == Mode::Threading;
    const bool knurling = model.getMode() == Mode::Knurling;
    const bool feedsIn = threading || knurling;
    // (A radius's passes are arcs: see radius.h)
    const bool radius = model.getMode() == Mode::Radius;
    const MultiPassRange& range = model.getMultiPassRange();
    const std::string z = model.axisLabel(1);
    const std::string x = model.axisLabel(2);
    const auto xLength = [&model](double mm) {
        return fmt::format(
            "{:.{}f} {}",
            model.toDisplayUnits(2, mm),
            model.axisDecimals(2),
            model.axisUnitName(2));
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
    const RadiusSetup& radiusSetup = model.getRadiusSetup();
    const bool shoulder = radius && radiusSetup.kind == RadiusKind::Shoulder;
    if (shoulder) {
        // e.g. "Shoulder: Ø 20.00 mm to 12.00 mm, fillet 2.00 mm"
        mode.text = fmt::format(
            "Shoulder: Ø {} to {}, fillet {}",
            xLength(2.0 * std::abs(range.feedStart)),
            xLength(radiusSetup.finishedDiameter),
            xLength(radiusSetup.radius));
    } else if (radius) {
        // e.g. "Radius 5.00 mm on the end, Ø 20.00 mm"
        mode.text = fmt::format(
            "Radius {} on the end, Ø {}",
            xLength(radiusSetup.radius),
            xLength(2.0 * std::abs(range.feedStart)));
    }
    // The lines are, in order: what's happening, what Enter does next, how
    // far X has gone, the options, then "Space stops". The first lines are
    // kept short, as the current feature's box is to their right; the options
    // line can be long, so it goes below the box.
    std::string options1;
    if (options.feedPerPass < MultiPass::TOLERANCE) {
        options1 = "Repeating the same cut";
    } else if (feedsIn && options.constantArea) {
        options1 = fmt::format(
            "{} infeed {} first, then a constant area per pass", x, xLength(options.feedPerPass));
    } else {
        options1 = fmt::format(
            "{} {} {} per pass", x, feedsIn ? "infeed" : "step-over", xLength(options.feedPerPass));
    }
    if (radius) {
        options1 = fmt::format(
            "{} per pass, {}",
            xLength(radiusSetup.depthPerPass),
            radiusSetup.noseRadius > 0.0 ? "tool nose radius " + xLength(radiusSetup.noseRadius)
                                         : std::string("a sharp tool"));
    }
    if (threading && options.compoundInfeed) {
        options1 += ", compound";
    }
    if (options.pause) {
        options1 += ", pausing";
    }
    if (options.retract && !feedsIn) {
        options1 += options.retractLastOnly
            ? fmt::format(", retracting on the last {}", model.retractLastPasses())
            : std::string(", retracting");
    }
    if (model.taperCorrection() != 0.0 && !feedsIn) {
        options1 += ", correcting taper";
    }
    // (Once threading passes have finished: see Model::springPassesAvailable())
    const bool spring = threading && model.springPasses();
    if (spring) {
        options1 = "Spring passes at the same depth, pausing after each";
    }

    const std::string pass = spring ? fmt::format("Spring pass {}", mp.pass())
        : mp.totalPasses() == 0     ? fmt::format("Pass {}", mp.pass())
                                    : fmt::format("Pass {} of {}", mp.pass(), mp.totalPasses());
    std::string status;
    std::string prompt;
    switch (mp.state()) {
        case MultiPass::State::Idle:
            status = mp.message().empty() ? "" : "Stopped";
            if (model.multiPassMovingToStart()) {
                status = "Moving to the start";
            } else if (threading && model.springPassesAvailable()) {
                prompt = model.springPassAtStart()
                    ? "Enter takes a spring pass (" + x + " can be nudged in first)"
                    : "Press Enter to move back to the start of the cut";
            } else if (model.multiPassAtStart() && !model.getIsAxis2Retracted()) {
                status = status.empty() ? "At the start" : "Stopped, at the start";
                prompt = "Press Enter to start cutting";
            } else {
                prompt = "Press Enter to move to the start";
                if (options.retractBeforeMoveToStart) {
                    prompt += " (" + x + " retracts first)";
                }
            }
            break;
        case MultiPass::State::Cutting:
            status = pass
                + (knurling        ? fmt::format(
                                         ": knurling, start {} of {}", mp.startNumber(), mp.plan().starts)
                       : threading ? ": threading"
                                   : ": cutting");
            break;
        case MultiPass::State::CuttingBack:
            status = pass
                + fmt::format(
                         ": {}, start {} of {}, back",
                         knurling ? "knurling" : "threading",
                         mp.startNumber(),
                         mp.plan().starts);
            break;
        case MultiPass::State::Retracting:
            status = pass + ": retracting";
            break;
        case MultiPass::State::Returning:
            status = pass + ": returning";
            break;
        case MultiPass::State::Paused:
            status = mp.plan().feedManual && mp.pass() == 0 ? "" : pass + " done";
            prompt = mp.plan().feedManual
                ? fmt::format("Feed {} by {}, then press Enter", x, xLength(mp.nextFeed()))
                : spring && !model.springPassAtStart() ? "Enter moves back to the last pass's start"
                : spring ? "Enter for another (" + x + " can be nudged in), Space ends"
                : radius ? "Press Enter for the next"
                         : "Press Enter for the next (" + x + " can be nudged)";
            break;
        case MultiPass::State::Unretracting:
            status = pass + ": unretracting";
            break;
        case MultiPass::State::Feeding:
            status = pass
                + (feedsIn      ? ": feeding in"
                       : radius ? ": " + x + " to the pass's start"
                                : ": stepping over");
            break;
        // (See IMultiPassMachine::feedReady() and cutReady())
        case MultiPass::State::FeedSettling:
            status = pass + ": waiting for " + x + "'s scale to settle";
            break;
        case MultiPass::State::Settling:
            status = pass + ": waiting for " + z + "'s scale to settle";
            break;
        case MultiPass::State::Positioning:
            status = pass + ": " + z + " to the pass's start";
            break;
        // (Only when gear cutting: see updateGearData())
        case MultiPass::State::Indexing:
            status = pass + ": indexing";
            break;
        case MultiPass::State::Finishing:
            status = "Finishing";
            break;
        case MultiPass::State::Finished:
            status = fmt::format("Finished ({} passes)", mp.pass());
            prompt = threading && !model.springPassAtStart()
                ? "Enter moves back to the start of the cut"
                : threading ? "Enter takes a spring pass (" + x + " can be nudged in first)"
                : model.multiPassAtStart() ? "Enter starts again"
                                           : "Enter moves to the start, to go again";
            break;
    }
    if (mp.pauseRequested()) {
        prompt = "Pausing after this pass (Enter cancels)";
    }
    misc1.text = status;
    misc2.text = prompt;
    // How far X has stepped over since the first pass
    misc3.text = "";
    if (spring) {
        // (From where the first threading pass started)
        misc3.text = fmt::format(
            "{} fed in: {} in all",
            x,
            xLength(std::abs(model.getAxis2Position() - range.feedStart)));
    } else if (
        radius && mp.pass() > 0 && mp.pass() <= static_cast<int>(model.radiusPassArcs().size())) {
        // Each pass is an arc a depth per pass from the next (or, for a
        // shoulder, first a straight pass, turning the step)
        const double arc = model.radiusPassArcs()[mp.pass() - 1];
        if (arc > 0.0) {
            misc3.text = fmt::format(
                "This pass's radius: {} (finishing at {})",
                xLength(arc),
                xLength(radiusSetup.radius));
        } else {
            misc3.text = fmt::format(
                "Turning the step: Ø {} (finishing at {})",
                xLength(2.0 * std::abs(mp.plan().paths.at(mp.pass() - 1).feedStart)),
                xLength(radiusSetup.finishedDiameter));
        }
    } else if (mp.pass() > 0) {
        misc3.text
            = fmt::format("{} {}: {}", x, feedsIn ? "fed in" : "stepped over", xLength(mp.fed()));
        if (mp.totalPasses() != 0) {
            misc3.text += fmt::format(
                " of {}", xLength(std::abs(mp.plan().feedEnd - mp.plan().feedStart)));
        }
    }
    misc4.text = options1;
    misc5.text = mp.inProgress() || model.multiPassMovingToStart() ? "Space stops" : "";
    misc6.text = "";
}

void ViewQt::Impl::updateGearData(const Model& model)
{
    const MultiPass& mp = model.multiPass();
    const GearSetup& gear = model.getGearSetup();
    const MultiPassRange& range = model.getMultiPassRange();
    const std::string x = model.axisLabel(1);
    const std::string a = model.axisLabel(2);
    // (The depth is fed by hand, but is a length, as axis 1's are)
    const auto length = [&model](double mm, int extraDecimals = 0) {
        return fmt::format(
            "{:.{}f} {}",
            model.toDisplayUnits(1, mm),
            model.axisDecimals(1) + extraDecimals,
            model.axisUnitName(1));
    };
    if (!model.gearCuttingSet()) {
        mode.text = "";
        misc1.text = "Gear cutting isn't set up";
        misc2.text = misc3.text = misc4.text = misc5.text = misc6.text = "";
        return;
    }
    const auto position = [&model](double mm) {
        const int decimals = model.axisDecimals(1);
        double value = model.toDisplayUnits(1, mm);
        // (Not "-0.00")
        if (std::abs(value) < 0.5 * std::pow(10.0, -decimals)) {
            value = 0.0;
        }
        return fmt::format("{:.{}f}", value, decimals);
    };
    // e.g. "X 10.00 to -30.00, depth 2.157 mm by 0.500 mm per pass"
    mode.text = fmt::format(
        "{} {} to {}, depth {} by {} per pass",
        x,
        position(range.cutStart),
        position(range.cutEnd),
        length(gear.depth, 1),
        length(gear.depthPerPass, 1));
    const int cutter = gearCutterNumber(gear.teeth);
    misc1.text = fmt::format(
        "Gear: {} teeth, module {:g}, {:.2f}° per tooth, blank Ø {}{}",
        gear.teeth,
        gear.module,
        360.0 / gear.teeth,
        length(gearBlankDiameterMm(gear.module, gear.teeth)),
        cutter == 0 ? std::string() : fmt::format(", cutter No. {}", cutter));

    const std::string pass = fmt::format("Pass {} of {}", mp.pass(), mp.totalPasses());
    const std::string tooth
        = fmt::format("{}, tooth {} of {}", pass, mp.startNumber(), mp.plan().starts);
    std::string status;
    std::string prompt;
    switch (mp.state()) {
        case MultiPass::State::Idle:
            status = mp.message().empty() ? "" : "Stopped";
            if (model.multiPassMovingToStart()) {
                status = "Moving to the start";
            } else if (model.multiPassAtStart()) {
                status = status.empty() ? "At the start" : "Stopped, at the start";
                prompt = "Press Enter to start: the first tooth is where " + a + " is now";
            } else if (!model.gearAOnTooth()) {
                prompt = "Press Enter to move " + x + " to the start, then " + a
                    + " on to the next tooth";
            } else {
                prompt = "Press Enter to move " + x + " to the start";
            }
            break;
        case MultiPass::State::Indexing:
            status = tooth + ": turning " + a;
            break;
        case MultiPass::State::Settling:
            status = tooth + ": waiting for " + x + "'s scale to settle";
            break;
        case MultiPass::State::Cutting:
            status = tooth + ": cutting";
            break;
        case MultiPass::State::Returning:
            status = tooth + ": returning";
            break;
        case MultiPass::State::Paused:
            if (mp.pausedBetweenStarts()) {
                status = tooth + " done";
                prompt = "Press Enter for the next tooth";
            } else {
                status = mp.pass() == 0 ? "" : pass + " done (once round)";
                prompt = fmt::format(
                    "Feed the depth in by {}, then press Enter", length(mp.nextFeed(), 1));
            }
            break;
        case MultiPass::State::Finishing:
            status = "Finishing: " + a + " on round to the first tooth";
            break;
        case MultiPass::State::Finished:
            status = fmt::format("Finished ({} passes)", mp.pass());
            prompt = model.multiPassAtStart() ? "Enter starts again"
                                              : "Enter moves to the start, to go again";
            break;
        // (Not used for gear cutting)
        case MultiPass::State::CuttingBack:
        case MultiPass::State::Retracting:
        case MultiPass::State::Unretracting:
        case MultiPass::State::Feeding:
        case MultiPass::State::FeedSettling:
        case MultiPass::State::Positioning:
            status = tooth;
            break;
    }
    if (mp.pauseRequested()) {
        prompt = "Pausing after this tooth (Enter cancels)";
    }
    misc2.text = status;
    misc3.text = prompt;
    misc4.text = mp.pass() > 0
        ? fmt::format("Depth fed in: {} of {}", length(mp.fed(), 1), length(gear.depth, 1))
        : "";
    misc5.text = mp.inProgress() || model.multiPassMovingToStart() ? "Space stops" : "";
    misc6.text = "";
}

std::string ViewQt::Impl::knurlDescription(const Model& model) const
{
    const MultiPassSetup& setup = model.getKnurlingSetup();
    const auto lead = model.getKnurlLeadMm();
    if (!lead) {
        return "Knurling isn't set up";
    }
    return fmt::format(
        "Knurl: {} starts at {:g}°, lead {:.{}f} {}: spindle at most {:.0f} rpm",
        setup.knurlStarts,
        setup.knurlHelixAngleDeg,
        model.toDisplayUnits(1, *lead),
        model.axisDecimals(1),
        model.axisUnitName(1),
        model.spindlePassSpeedLimit() / *lead);
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
        misc2.text
            = fmt::format("OD: {:.3f} mm, depth: {:.3f} mm", thread->maleOd(), thread->maleDepth());
    }
    // (Whether it retracts by itself at the end of each pass, too: it's easily
    // forgotten, having been turned off from the menu)
    misc3.text = fmt::format(
        "{} retracts {}; auto-retract {}",
        model.axisLabel(2),
        model.getRetractionDirection() == XDirection::Inwards ? "inwards" : "outwards",
        model.threadingAutoRetract() ? "on" : "off");
}

} // namespace mgo
