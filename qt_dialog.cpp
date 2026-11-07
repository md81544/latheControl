#include "qt_dialog.h"

#include "log.h"
#include "tapers.h"
#include "threadpitches.h"

#include <fmt/format.h>

#include <QDialog>
#include <QPainter>
#include <QSignalBlocker>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPixmap>
#include <QRegularExpressionValidator>
#include <QVBoxLayout>

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cmath>
#include <map>
#include <set>

namespace mgo {
namespace dialog {

namespace {

// Colours and sizes as used by the SFML dialogs
constexpr int PROMPT_FONT_SIZE = 20;
constexpr int BODY_FONT_SIZE = 16;
// Lists to pick from (e.g. thread pitches) are larger, to be easy to read at
// the machine
constexpr int LIST_FONT_SIZE = 20;
constexpr int MIN_WIDTH = 550;
constexpr int MAX_LIST_ITEMS_IN_VIEW = 8;
const char* const BOX_STYLE
    = "#box { background-color: rgb(40, 40, 40); border: 2px solid rgb(128, 128, 128); }";
const char* const PROMPT_STYLE = "color: rgb(0, 255, 0);";
const char* const BODY_STYLE = "color: rgb(192, 192, 192);";
const char* const EDIT_STYLE = "background-color: white; color: black;"
                               "border: 2px solid rgb(192, 0, 0); padding: 4px;";
const char* const LIST_STYLE = "QListWidget { background: transparent; border: none;"
                               "  color: rgb(192, 192, 192); outline: none; }"
                               "QListWidget::item { padding: 4px; }"
                               "QListWidget::item:selected { background: transparent;"
                               "  color: white; }"
                               "QScrollBar:vertical { background: rgb(40, 40, 40); width: 8px;"
                               "  margin: 0; }"
                               "QScrollBar::handle:vertical { background: rgb(128, 128, 128);"
                               "  min-height: 20px; }"
                               "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical"
                               "  { height: 0; }"
                               "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical"
                               "  { background: none; }";

bool isModifierKey(int qtKey)
{
    return qtKey == Qt::Key_Shift || qtKey == Qt::Key_Control || qtKey == Qt::Key_Alt
        || qtKey == Qt::Key_AltGr || qtKey == Qt::Key_Meta || qtKey == Qt::Key_CapsLock;
}

class InputDialog final : public QDialog {
public:
    InputDialog(
        QWidget* parent,
        const QString& fontFamily,
        Input::Type type,
        std::string_view prompt,
        std::vector<std::string> additionalText,
        std::string_view defaultEntry,
        const std::optional<std::vector<std::string>>& listItems)
        : QDialog(parent, Qt::Dialog | Qt::FramelessWindowHint)
        , m_type(type)
    {
        setModal(true);
        setFocusPolicy(Qt::StrongFocus);

        QFont promptFont(fontFamily);
        promptFont.setPixelSize(PROMPT_FONT_SIZE);
        QFont bodyFont(fontFamily);
        bodyFont.setPixelSize(BODY_FONT_SIZE);

        auto* outer = new QVBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);
        auto* box = new QFrame(this);
        box->setObjectName("box");
        box->setStyleSheet(BOX_STYLE);
        box->setMinimumWidth(MIN_WIDTH);
        outer->addWidget(box);
        auto* layout = new QVBoxLayout(box);
        layout->setContentsMargins(25, 20, 25, 20);
        layout->setSpacing(10);

        auto* promptLabel = new QLabel(QString::fromStdString(std::string(prompt)), box);
        promptLabel->setTextFormat(Qt::PlainText);
        promptLabel->setFont(promptFont);
        promptLabel->setStyleSheet(PROMPT_STYLE);
        layout->addWidget(promptLabel);

        if (type == Input::Type::Text || type == Input::Type::Numeric) {
            m_edit = new QLineEdit(QString::fromStdString(std::string(defaultEntry)), box);
            m_edit->setFont(promptFont);
            m_edit->setStyleSheet(EDIT_STYLE);
            if (type == Input::Type::Numeric) {
                m_edit->setValidator(new QRegularExpressionValidator(
                    QRegularExpression(R"(^-?[0-9]*\.?[0-9]*$)"), m_edit));
            }
            // With the default entry selected, typing replaces it (as before,
            // where the first key press cleared the entry)
            m_edit->selectAll();
            m_edit->installEventFilter(this);
            layout->addWidget(m_edit);
            layout->addSpacing(10);
        }

        const QPixmap tick("img/tick.png");
        if (tick.isNull()) {
            MGOLOG("Could not load img/tick.png");
        }
        for (auto& line : additionalText) {
            // "Hotkeys" are defined as & + a non-whitespace character
            std::optional<char> hotkey;
            const auto pos = line.find('&');
            if (pos != std::string::npos) {
                if (pos < line.size() - 1
                    && !std::isspace(static_cast<unsigned char>(line.at(pos + 1)))) {
                    hotkey = static_cast<char>(
                        std::tolower(static_cast<unsigned char>(line.at(pos + 1))));
                }
                line.erase(pos, 1);
            }
            auto* row = new QHBoxLayout();
            row->setSpacing(0);
            auto* tickLabel = new QLabel(box);
            tickLabel->setFixedWidth(18);
            if (hotkey) {
                if (tick.isNull()) {
                    // Fall back to a text tick if the image is missing
                    tickLabel->setStyleSheet(PROMPT_STYLE);
                    tickLabel->setFont(bodyFont);
                    tickLabel->setProperty("tickText", true);
                } else {
                    tickLabel->setProperty("tickPixmap", tick);
                }
                m_options[*hotkey] = tickLabel;
            }
            auto* textLabel = new QLabel(QString::fromStdString(line), box);
            textLabel->setTextFormat(Qt::PlainText);
            textLabel->setFont(bodyFont);
            textLabel->setStyleSheet(BODY_STYLE);
            row->addWidget(tickLabel);
            row->addWidget(textLabel, 1);
            layout->addLayout(row);
        }

        if (listItems) {
            m_list = new QListWidget(box);
            QFont listFont(fontFamily);
            listFont.setPixelSize(LIST_FONT_SIZE);
            m_list->setFont(listFont);
            m_list->setStyleSheet(LIST_STYLE);
            m_list->setFocusPolicy(Qt::StrongFocus);
            m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            for (const auto& item : *listItems) {
                m_list->addItem(QString::fromStdString(item));
            }
            // The current item is shown in bold white, as before
            QObject::connect(m_list, &QListWidget::currentRowChanged, m_list, [this](int row) {
                for (int i = 0; i < m_list->count(); ++i) {
                    QFont f = m_list->item(i)->font();
                    f.setBold(i == row);
                    m_list->item(i)->setFont(f);
                }
            });
            if (m_list->count() > 0) {
                m_list->setCurrentRow(0);
                const int rows = std::min(m_list->count(), MAX_LIST_ITEMS_IN_VIEW);
                m_list->setFixedHeight(m_list->sizeHintForRow(0) * rows + 2 * m_list->frameWidth());
            }
            m_list->installEventFilter(this);
            layout->addWidget(m_list);
        }

        adjustSize();
        if (parent) {
            // Centre over the main window
            const QPoint centre = parent->mapToGlobal(parent->rect().center());
            move(centre - rect().center());
        }
        if (m_edit) {
            m_edit->setFocus();
        } else if (m_list) {
            m_list->setFocus();
        } else {
            setFocus();
        }
    }

    Input::Return inputResult() const
    {
        if (result() != QDialog::Accepted) {
            return Input::Return(); // cancelled
        }
        Input::Return rc;
        rc.cancelled = false;
        rc.number = 0.0;
        rc.optionsSelected = m_selected;
        rc.pickedItem = 0;
        if (m_edit) {
            rc.text = m_edit->text().toStdString();
            if (m_type == Input::Type::Numeric) {
                try {
                    rc.number = std::stod(rc.text);
                } catch (...) {
                    // Leave as zero (e.g. nothing entered)
                }
            }
        }
        if (m_list && m_list->currentRow() >= 0) {
            rc.pickedItem = static_cast<std::size_t>(m_list->currentRow());
        }
        return rc;
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::KeyPress && handleKey(static_cast<QKeyEvent*>(event))) {
            return true;
        }
        return QDialog::eventFilter(watched, event);
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        if (!handleKey(event)) {
            QDialog::keyPressEvent(event);
        }
    }

private:
    // Returns true if the key press has been dealt with
    bool handleKey(QKeyEvent* event)
    {
        const int qtKey = event->key();
        if (qtKey == Qt::Key_Escape) {
            reject();
            return true;
        }
        if (qtKey == Qt::Key_Return || qtKey == Qt::Key_Enter) {
            accept();
            return true;
        }
        if (m_type == Input::Type::PressAnyKey) {
            if (!isModifierKey(qtKey)) {
                accept();
            }
            return true;
        }
        if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z) {
            const char c = static_cast<char>('a' + (qtKey - Qt::Key_A));
            const bool alt = event->modifiers() & Qt::AltModifier;
            // In a numeric entry, letters can't be typed, so a plain letter
            // can toggle an option; in text entry, Alt is needed
            if ((alt || m_type == Input::Type::Numeric) && m_options.contains(c)) {
                toggleOption(c);
                return true;
            }
            if (alt) {
                return true; // don't type Alt-letter characters
            }
        }
        return false;
    }

    void toggleOption(char c)
    {
        QLabel* tickLabel = m_options.at(c);
        if (m_selected.contains(c)) {
            m_selected.erase(c);
            tickLabel->clear();
        } else {
            m_selected.insert(c);
            if (tickLabel->property("tickText").toBool()) {
                tickLabel->setText("✓");
            } else {
                tickLabel->setPixmap(tickLabel->property("tickPixmap").value<QPixmap>());
            }
        }
    }

    Input::Type m_type;
    QLineEdit* m_edit { nullptr };
    QListWidget* m_list { nullptr };
    std::map<char, QLabel*> m_options; // hotkey -> its tick
    std::set<char> m_selected;
};

// ---------------------------------------------------------------------------
// Taper dialog
// ---------------------------------------------------------------------------

// The smallest and largest taper angles the dialog accepts
constexpr double MIN_TAPER_ANGLE = 0.0; // (exclusive)
constexpr double MAX_TAPER_ANGLE = 60.0;

// A diagram of the tool's path when cutting a taper, with the chuck on the
// left. Small angles are exaggerated, as they'd otherwise look flat
class TaperDiagram final : public QWidget {
public:
    TaperDiagram(const QString& fontFamily, QWidget* parent)
        : QWidget(parent)
        , m_fontFamily(fontFamily)
    {
        setFixedSize(340, 200);
    }

    // angle: positive, in degrees (0 if none entered yet)
    void setTaper(double angle, bool toolMovesIn)
    {
        m_angle = angle;
        m_toolMovesIn = toolMovesIn;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), QColor(20, 20, 20));
        const QColor green(0, 192, 0);
        const QColor partColour(0, 70, 0);
        QFont font(m_fontFamily);
        font.setPixelSize(14);
        p.setFont(font);

        // Drawn as seen from where the operator stands: the work's centre line
        // is at the top, and "out" (away from the centre, towards the
        // operator) is down, as with the arrow keys. Only the half of the work
        // nearer the operator is shown, with the chuck on the left
        const double centreY = 25.0;
        const double chuckRight = 46.0;
        const double partRight = width() - 10.0;
        const QRectF chuck(10.0, 15.0, chuckRight - 10.0, height() - 25.0);
        p.setPen(QPen(green, 2));
        p.setBrush(Qt::NoBrush);
        p.drawRect(chuck);
        p.save();
        p.translate(chuck.center());
        p.rotate(-90);
        p.drawText(QRectF(-chuck.height() / 2, -10, chuck.height(), 20), Qt::AlignCenter, "chuck");
        p.restore();
        p.setPen(QPen(QColor(128, 128, 128), 1, Qt::DashDotLine));
        p.drawLine(QPointF(chuckRight, centreY), QPointF(width() - 4.0, centreY));

        if (m_angle <= MIN_TAPER_ANGLE) {
            p.setPen(QColor(192, 192, 192));
            p.drawText(rect().adjusted(60, 0, 0, -30), Qt::AlignCenter, "Choose or enter an angle");
            return;
        }

        // Exaggerate small angles: up to 30 degrees, they're drawn between 10
        // and 30 degrees
        const double drawnAngle = m_angle >= 30.0 ? m_angle : 10.0 + m_angle * 2.0 / 3.0;
        // The tool's path: from the far end of the taper towards the chuck,
        // fitted into the space available
        const double nearX = chuckRight + 14.0;
        const double innerY = centreY + 20.0; // (the smallest radius drawn)
        const double outerY = height() - 28.0; // (the largest)
        const double slope = std::tan(drawnAngle * M_PI / 180.0);
        const double length = std::min(partRight - 20.0 - nearX, (outerY - innerY) / slope);
        const double rise = length * slope;
        const double midY = (innerY + outerY) / 2.0;
        // (Larger y is further from the centre line, i.e. a larger radius)
        const double nearY = m_toolMovesIn ? midY - rise / 2.0 : midY + rise / 2.0;
        const double farY = m_toolMovesIn ? midY + rise / 2.0 : midY - rise / 2.0;
        const QPointF nearEnd(nearX, nearY);
        const QPointF farEnd(nearX + length, farY);

        // The finished work's outline
        QPolygonF part;
        part << QPointF(chuckRight, centreY) << QPointF(chuckRight, nearY) << nearEnd << farEnd
             << QPointF(partRight, farY) << QPointF(partRight, centreY);
        p.setPen(QPen(green, 1));
        p.setBrush(partColour);
        p.drawPolygon(part);

        // The tool's path, with an arrow towards the chuck
        const QColor pathColour(255, 255, 0);
        p.setPen(QPen(pathColour, 3));
        p.drawLine(farEnd, nearEnd);
        const QPointF path = nearEnd - farEnd;
        const QPointF direction = path / std::hypot(path.x(), path.y());
        const QPointF normal(-direction.y(), direction.x());
        QPolygonF arrowHead;
        arrowHead << nearEnd << nearEnd - direction * 14.0 + normal * 6.0
                  << nearEnd - direction * 14.0 - normal * 6.0;
        p.setPen(Qt::NoPen);
        p.setBrush(pathColour);
        p.drawPolygon(arrowHead);

        p.setPen(QColor(220, 220, 220));
        QString label = QString("%1 deg").arg(m_angle, 0, 'g', 6);
        if (drawnAngle != m_angle) {
            label += " (exaggerated)";
        }
        p.drawText(
            QRectF(chuckRight + 10, height() - 20.0, width() - chuckRight - 16, 18),
            Qt::AlignRight,
            label);
    }

private:
    QString m_fontFamily;
    double m_angle { 0.0 };
    bool m_toolMovesIn { false };
};

class TaperDialog final : public QDialog {
public:
    TaperDialog(QWidget* parent, const QString& fontFamily, double currentAngle)
        : QDialog(parent, Qt::Dialog | Qt::FramelessWindowHint)
        , m_toolMovesIn(currentAngle > 0.0)
    {
        setModal(true);
        QFont promptFont(fontFamily);
        promptFont.setPixelSize(PROMPT_FONT_SIZE);
        QFont bodyFont(fontFamily);
        bodyFont.setPixelSize(BODY_FONT_SIZE);

        auto* outer = new QVBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);
        auto* box = new QFrame(this);
        box->setObjectName("box");
        box->setStyleSheet(BOX_STYLE);
        outer->addWidget(box);
        auto* layout = new QVBoxLayout(box);
        layout->setContentsMargins(25, 20, 25, 20);
        layout->setSpacing(12);

        auto* prompt = new QLabel("Taper", box);
        prompt->setFont(promptFont);
        prompt->setStyleSheet(PROMPT_STYLE);
        layout->addWidget(prompt);

        auto* columns = new QHBoxLayout();
        columns->setSpacing(20);
        layout->addLayout(columns);

        // Left: the presets, and a custom angle
        m_list = new QListWidget(box);
        m_list->setFont(bodyFont);
        m_list->setStyleSheet(LIST_STYLE);
        m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        for (const auto& preset : taperPresets) {
            m_list->addItem(QString::fromStdString(preset.name));
        }
        m_list->addItem("Custom angle");
        m_list->setFixedWidth(200);
        m_list->installEventFilter(this);
        columns->addWidget(m_list, 0, Qt::AlignTop);

        // Right: the diagram, the angle, and the direction
        auto* right = new QVBoxLayout();
        right->setSpacing(10);
        columns->addLayout(right);
        m_diagram = new TaperDiagram(fontFamily, box);
        right->addWidget(m_diagram);

        auto* angleRow = new QHBoxLayout();
        auto* angleLabel = new QLabel("Angle:", box);
        angleLabel->setFont(bodyFont);
        angleLabel->setStyleSheet(BODY_STYLE);
        m_edit = new QLineEdit(box);
        m_edit->setFont(promptFont);
        m_edit->setStyleSheet(EDIT_STYLE);
        m_edit->setFixedWidth(130);
        // Positive numbers only: the direction is chosen separately
        m_edit->setValidator(new QRegularExpressionValidator(
            QRegularExpression(R"(^[0-9]{0,2}(\.[0-9]*)?$)"), m_edit));
        m_edit->installEventFilter(this);
        auto* degrees = new QLabel("degrees", box);
        degrees->setFont(bodyFont);
        degrees->setStyleSheet(BODY_STYLE);
        angleRow->addWidget(angleLabel);
        angleRow->addWidget(m_edit);
        angleRow->addWidget(degrees);
        angleRow->addStretch();
        right->addLayout(angleRow);

        // Says which way the tool moves (I toggles it)
        m_direction = new QLabel(box);
        m_direction->setTextFormat(Qt::RichText);
        m_direction->setFont(bodyFont);
        m_direction->setStyleSheet(BODY_STYLE);
        right->addWidget(m_direction);
        right->addStretch();

        auto* hint = new QLabel(
            "Up/Down: choose a taper, or type an angle.   I: tool in / out.\n"
            "Enter: OK   Esc: cancel",
            box);
        hint->setFont(bodyFont);
        hint->setStyleSheet("color: rgb(140, 140, 140);");
        layout->addWidget(hint);

        // The current item is shown in bold, as in the other dialogs
        QObject::connect(m_list, &QListWidget::currentRowChanged, m_list, [this](int row) {
            for (int i = 0; i < m_list->count(); ++i) {
                QFont f = m_list->item(i)->font();
                f.setBold(i == row);
                m_list->item(i)->setFont(f);
            }
            presetChosen(row);
        });
        // Typing an angle makes it a custom one
        QObject::connect(m_edit, &QLineEdit::textEdited, m_edit, [this](const QString&) {
            if (m_list->currentRow() != customRow()) {
                const QSignalBlocker blocker(m_list);
                m_list->setCurrentRow(customRow());
                boldRow(customRow());
            }
            refresh();
        });

        // Start with the taper currently in use, if any
        const double current = std::abs(currentAngle);
        int row = 0;
        if (current > MIN_TAPER_ANGLE) {
            row = customRow();
            for (std::size_t n = 0; n < taperPresets.size(); ++n) {
                if (std::abs(taperPresets.at(n).angle - current) < 1e-4) {
                    row = static_cast<int>(n);
                }
            }
        }
        m_list->setCurrentRow(row);
        if (row == customRow()) {
            m_edit->setText(QString::number(current, 'g', 10));
        }
        const int rows = m_list->count();
        m_list->setFixedHeight(m_list->sizeHintForRow(0) * rows + 2 * m_list->frameWidth());
        refresh();

        adjustSize();
        if (parent) {
            const QPoint centre = parent->mapToGlobal(parent->rect().center());
            move(centre - rect().center());
        }
        m_list->setFocus();
    }

    // The angle chosen (negative if the tool moves out as it approaches the
    // chuck), or nullopt if cancelled
    std::optional<double> chosenAngle() const
    {
        if (result() != QDialog::Accepted) {
            return std::nullopt;
        }
        const double angle = enteredAngle();
        return m_toolMovesIn ? angle : -angle;
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::KeyPress
            && handleKey(watched, static_cast<QKeyEvent*>(event))) {
            return true;
        }
        return QDialog::eventFilter(watched, event);
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        if (!handleKey(this, event)) {
            QDialog::keyPressEvent(event);
        }
    }

private:
    int customRow() const
    {
        return static_cast<int>(taperPresets.size());
    }

    void boldRow(int row)
    {
        for (int i = 0; i < m_list->count(); ++i) {
            QFont f = m_list->item(i)->font();
            f.setBold(i == row);
            m_list->item(i)->setFont(f);
        }
    }

    void presetChosen(int row)
    {
        if (row >= 0 && row < customRow()) {
            m_edit->setText(QString::number(taperPresets.at(row).angle, 'g', 10));
        }
        refresh();
    }

    // The angle in the entry box, or 0 if it isn't a valid one
    double enteredAngle() const
    {
        bool ok = false;
        const double angle = m_edit->text().toDouble(&ok);
        return ok && angle > MIN_TAPER_ANGLE && angle <= MAX_TAPER_ANGLE ? angle : 0.0;
    }

    void refresh()
    {
        m_diagram->setTaper(enteredAngle(), m_toolMovesIn);
        m_direction->setText(
            QString("[I] Tool moves <span style='color: rgb(255, 255, 0);'>%1</span> "
                    "as it approaches the chuck")
                .arg(m_toolMovesIn ? "in" : "out"));
    }

    // Returns true if the key press has been dealt with
    bool handleKey(QObject* watched, QKeyEvent* event)
    {
        const int qtKey = event->key();
        if (qtKey == Qt::Key_Escape) {
            reject();
            return true;
        }
        if (qtKey == Qt::Key_Return || qtKey == Qt::Key_Enter) {
            if (enteredAngle() > MIN_TAPER_ANGLE) {
                accept();
            }
            return true; // (not accepted without a valid angle)
        }
        if (qtKey == Qt::Key_I) {
            m_toolMovesIn = !m_toolMovesIn;
            refresh();
            return true;
        }
        const QString text = event->text();
        const bool numeric = text.size() == 1 && (text.at(0).isDigit() || text.at(0) == '.');
        if (watched == m_list && numeric) {
            // Typing a number while choosing from the list: a custom angle
            m_list->setCurrentRow(customRow());
            m_edit->clear();
            m_edit->setFocus();
            m_edit->insert(text);
            refresh();
            return true;
        }
        if (watched == m_edit && (qtKey == Qt::Key_Up || qtKey == Qt::Key_Down)) {
            // Back to the list
            m_list->setFocus();
            const int row = m_list->currentRow() + (qtKey == Qt::Key_Up ? -1 : 1);
            if (row >= 0 && row < m_list->count()) {
                m_list->setCurrentRow(row);
            }
            return true;
        }
        if (watched == m_edit && text.size() == 1 && text.at(0).isLetter()) {
            return true; // (letters other than I do nothing)
        }
        return false;
    }

    QListWidget* m_list { nullptr };
    QLineEdit* m_edit { nullptr };
    QLabel* m_direction { nullptr };
    TaperDiagram* m_diagram { nullptr };
    bool m_toolMovesIn;
};

// ---------------------------------------------------------------------------
// Thread dialog
// ---------------------------------------------------------------------------

// A cross-section of a thread's profile: a few teeth, with the pitch, depth
// and angle marked. The work's centre line is at the bottom: an external
// (male) thread is on a shaft, with the work below the teeth; an internal
// (female) one is in a bore, with the work above them
class ThreadDiagram final : public QWidget {
public:
    ThreadDiagram(const QString& fontFamily, QWidget* parent)
        : QWidget(parent)
        , m_fontFamily(fontFamily)
    {
        setFixedSize(360, 230);
    }

    void setThread(const Thread* thread, bool female)
    {
        m_thread = thread;
        m_female = female;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), QColor(20, 20, 20));
        QFont font(m_fontFamily);
        font.setPixelSize(14);
        p.setFont(font);
        const QColor green(0, 192, 0);
        const QColor partColour(0, 70, 0);
        const QColor dimension(255, 255, 0);
        const QColor text(220, 220, 220);

        const double centreY = height() - 14.0;
        p.setPen(QPen(QColor(128, 128, 128), 1, Qt::DashDotLine));
        p.drawLine(QPointF(4.0, centreY), QPointF(width() - 4.0, centreY));
        if (!m_thread) {
            p.setPen(text);
            p.drawText(rect().adjusted(0, 0, 0, -30), Qt::AlignCenter, "Choose a thread");
            return;
        }
        const ThreadFormInfo& form = threadFormInfo(m_thread->form);
        const double depthFraction = m_female ? form.femaleDepth : form.maleDepth;

        // Three teeth, the pitch drawn 70 pixels wide, so the depth is in
        // proportion. Teeth are trapezia: flanks at the form's angle, with
        // flat crests and roots sharing what's left of the pitch
        const double pitch = 70.0;
        const double depth = pitch * depthFraction;
        const double run = depth * std::tan(form.angle / 2.0 * M_PI / 180.0);
        const double flat = std::max(0.0, (pitch - 2.0 * run) / 2.0);
        const double left = 20.0;
        const double teeth = 3;
        const double right = left + pitch * teeth;
        // y of the thread's crests and roots. For a male thread the crests
        // are the outside (OD), further from the centre line, i.e. higher;
        // for a female one, they're the inside (the bore), nearer to it
        const double outerY = 70.0; // (furthest from the centre line)
        const double innerY = outerY + depth;
        const double crestY = m_female ? innerY : outerY;
        const double rootY = m_female ? outerY : innerY;
        QPolygonF profile;
        double x = left;
        profile << QPointF(x, rootY);
        for (int n = 0; n < teeth; ++n) {
            profile << QPointF(x + flat / 2.0, rootY) << QPointF(x + flat / 2.0 + run, crestY)
                    << QPointF(x + flat * 1.5 + run, crestY)
                    << QPointF(x + flat * 1.5 + 2 * run, rootY) << QPointF(x + pitch, rootY);
            x += pitch;
        }
        // The work: below the teeth (a shaft) or above them (around a bore)
        QPolygonF part = profile;
        const double partEdgeY = m_female ? 22.0 : centreY - 30.0;
        part << QPointF(right, partEdgeY) << QPointF(left, partEdgeY);
        p.setPen(QPen(green, 2));
        p.setBrush(partColour);
        p.drawPolygon(part);

        // Pitch: from one crest's start to the next, on the open side of the
        // teeth (above a male thread; below a female one, in the bore), with
        // the diameter at the crests (OD, or bore) beyond it
        p.setPen(QPen(dimension, 1));
        p.setBrush(dimension);
        const double crestStart = left + flat / 2.0 + run;
        const double pitchY = m_female ? innerY + 20.0 : outerY - 20.0;
        drawDimension(p, QPointF(crestStart, pitchY), QPointF(crestStart + pitch, pitchY));
        p.setPen(text);
        const double labelWidth = 200.0;
        const double pitchLabelY = m_female ? pitchY + 3.0 : pitchY - 21.0;
        // (Centred on the arrow, but kept within the diagram)
        const double labelX = std::clamp(
            crestStart + pitch / 2.0 - labelWidth / 2.0, 4.0, width() - labelWidth - 4.0);
        p.drawText(
            QRectF(labelX, pitchLabelY, labelWidth, 18),
            Qt::AlignCenter,
            QString::fromStdString("pitch " + describePitch(*m_thread)));
        const double diameter = m_female ? m_thread->femaleBore() : m_thread->maleOd();
        QString diameterText = QString("%1 %2 mm")
                                   .arg(m_female ? "bore" : "OD")
                                   .arg(diameter, 0, 'f', 3);
        if (m_thread->inchDiameter) {
            diameterText += QString(" (%1\")").arg(diameter / 25.4, 0, 'f', 4);
        }
        p.drawText(
            QRectF(left, m_female ? pitchLabelY + 22.0 : pitchLabelY - 22.0, width() - left, 18),
            Qt::AlignLeft,
            diameterText);

        // Depth, beside the teeth on the right
        p.setPen(QPen(dimension, 1));
        const double depthX = right + 12.0;
        drawDimension(p, QPointF(depthX, outerY), QPointF(depthX, innerY));
        p.setPen(text);
        const double depthMm = m_female ? m_thread->femaleDepth() : m_thread->maleDepth();
        p.drawText(
            QRectF(depthX + 6.0, (outerY + innerY) / 2.0 - 9.0, width() - depthX - 6.0, 18),
            Qt::AlignLeft | Qt::AlignVCenter,
            QString("depth %1").arg(depthMm, 0, 'f', 3));
    }

private:
    // A dimension line between two points, with arrowheads (using the
    // painter's pen and brush)
    static void drawDimension(QPainter& p, QPointF a, QPointF b)
    {
        p.drawLine(a, b);
        const QPointF along = (b - a) / std::hypot(b.x() - a.x(), b.y() - a.y());
        const QPointF across(-along.y(), along.x());
        for (const auto& [tip, dir] : { std::pair { a, along }, std::pair { b, -along } }) {
            QPolygonF head;
            head << tip << tip + dir * 8.0 + across * 3.5 << tip + dir * 8.0 - across * 3.5;
            p.drawPolygon(head);
        }
    }

    QString m_fontFamily;
    const Thread* m_thread { nullptr };
    bool m_female { false };
};

class ThreadDialog final : public QDialog {
public:
    ThreadDialog(
        QWidget* parent, const QString& fontFamily, const ThreadTable& threads, ThreadChoice current)
        : QDialog(parent, Qt::Dialog | Qt::FramelessWindowHint)
        , m_threads(threads)
        , m_choice(current)
    {
        assert(!m_threads.empty());
        if (m_choice.category >= m_threads.size()
            || m_choice.thread >= m_threads.at(m_choice.category).threads.size()) {
            m_choice = {};
        }
        setModal(true);
        QFont promptFont(fontFamily);
        promptFont.setPixelSize(PROMPT_FONT_SIZE);
        QFont bodyFont(fontFamily);
        bodyFont.setPixelSize(BODY_FONT_SIZE);
        QFont listFont(fontFamily);
        listFont.setPixelSize(LIST_FONT_SIZE);

        auto* outer = new QVBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);
        auto* box = new QFrame(this);
        box->setObjectName("box");
        box->setStyleSheet(BOX_STYLE);
        outer->addWidget(box);
        auto* layout = new QVBoxLayout(box);
        layout->setContentsMargins(25, 20, 25, 20);
        layout->setSpacing(12);

        auto* prompt = new QLabel("Thread", box);
        prompt->setFont(promptFont);
        prompt->setStyleSheet(PROMPT_STYLE);
        layout->addWidget(prompt);

        auto* columns = new QHBoxLayout();
        columns->setSpacing(20);
        layout->addLayout(columns);

        // Left: the categories, or the threads in one
        auto* left = new QVBoxLayout();
        left->setSpacing(4);
        columns->addLayout(left);
        m_heading = new QLabel(box);
        m_heading->setFont(bodyFont);
        m_heading->setStyleSheet("color: rgb(255, 255, 0);");
        left->addWidget(m_heading);
        m_list = new QListWidget(box);
        m_list->setFont(listFont);
        m_list->setStyleSheet(LIST_STYLE);
        m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_list->setFixedWidth(290);
        m_list->installEventFilter(this);
        left->addWidget(m_list);
        left->addStretch();

        // Right: the diagram, the figures, and male / female
        auto* right = new QVBoxLayout();
        right->setSpacing(10);
        columns->addLayout(right);
        m_diagram = new ThreadDiagram(fontFamily, box);
        right->addWidget(m_diagram);
        m_figures = new QLabel(box);
        m_figures->setFont(bodyFont);
        m_figures->setStyleSheet(BODY_STYLE);
        right->addWidget(m_figures);
        m_gender = new QLabel(box);
        m_gender->setTextFormat(Qt::RichText);
        m_gender->setFont(bodyFont);
        m_gender->setStyleSheet(BODY_STYLE);
        right->addWidget(m_gender);
        right->addStretch();

        m_hint = new QLabel(box);
        m_hint->setFont(bodyFont);
        m_hint->setStyleSheet("color: rgb(140, 140, 140);");
        layout->addWidget(m_hint);

        QObject::connect(m_list, &QListWidget::currentRowChanged, m_list, [this](int row) {
            boldRow(row);
            refresh();
        });

        // Start in the category of the thread in use, on that thread
        showThreads(m_choice.category, static_cast<int>(m_choice.thread));
        // (A fixed height, so the dialog doesn't change size between levels)
        m_list->setFixedHeight(
            m_list->sizeHintForRow(0) * MAX_LIST_ITEMS_IN_VIEW + 2 * m_list->frameWidth());

        adjustSize();
        if (parent) {
            const QPoint centre = parent->mapToGlobal(parent->rect().center());
            move(centre - rect().center());
        }
        m_list->setFocus();
    }

    // The thread chosen, or nullopt if cancelled
    std::optional<ThreadChoice> chosenThread() const
    {
        if (result() != QDialog::Accepted) {
            return std::nullopt;
        }
        return m_choice;
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::KeyPress && handleKey(static_cast<QKeyEvent*>(event))) {
            return true;
        }
        return QDialog::eventFilter(watched, event);
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        if (!handleKey(event)) {
            QDialog::keyPressEvent(event);
        }
    }

private:
    void showCategories(std::size_t category)
    {
        m_inCategory = false;
        const QSignalBlocker blocker(m_list);
        m_list->clear();
        for (const auto& c : m_threads) {
            m_list->addItem(QString::fromStdString(c.name));
        }
        m_list->setCurrentRow(static_cast<int>(category));
        boldRow(static_cast<int>(category));
        m_heading->setText("Type of thread");
        m_hint->setText("Up/Down: choose a type   Right/Enter: its sizes   M: male / female\n"
                        "Esc: cancel");
        refresh();
    }

    void showThreads(std::size_t category, int row)
    {
        m_inCategory = true;
        m_category = category;
        const QSignalBlocker blocker(m_list);
        m_list->clear();
        for (const auto& t : m_threads.at(category).threads) {
            m_list->addItem(QString::fromStdString(t.name));
        }
        m_list->setCurrentRow(row);
        boldRow(row);
        m_heading->setText(QString::fromStdString(m_threads.at(category).name));
        m_hint->setText("Up/Down: choose a size   Left: other types   M: male / female\n"
                        "Enter: OK   Esc: cancel");
        refresh();
    }

    void boldRow(int row)
    {
        for (int i = 0; i < m_list->count(); ++i) {
            QFont f = m_list->item(i)->font();
            f.setBold(i == row);
            m_list->item(i)->setFont(f);
        }
    }

    // The thread highlighted (only when choosing a size)
    const Thread* highlighted() const
    {
        const int row = m_list->currentRow();
        if (!m_inCategory || row < 0) {
            return nullptr;
        }
        return &m_threads.at(m_category).threads.at(static_cast<std::size_t>(row));
    }

    void refresh()
    {
        const Thread* thread = highlighted();
        m_diagram->setThread(thread, m_choice.female);
        if (thread) {
            const ThreadFormInfo& form = threadFormInfo(thread->form);
            m_figures->setText(
                QString::fromStdString(fmt::format(
                    "{}: {} form ({} deg), pitch {}\n"
                    "Male: turn to {:.3f} mm, cut {:.3f} deep\n"
                    "Female: bore to {:.3f} mm, cut {:.3f} deep",
                    thread->name,
                    form.name,
                    form.angle,
                    describePitch(*thread),
                    thread->maleOd(),
                    thread->maleDepth(),
                    thread->femaleBore(),
                    thread->femaleDepth())));
        } else {
            m_figures->setText("");
        }
        const QString on = "<span style='color: rgb(255, 255, 0);'>%1</span>";
        m_gender->setText(
            QString("[M] %1 thread. %2 will retract %3")
                .arg(on.arg(m_choice.female ? "Female (internal)" : "Male (external)"))
                .arg("X")
                .arg(on.arg(m_choice.female ? "inwards" : "outwards")));
    }

    // Returns true if the key press has been dealt with
    bool handleKey(QKeyEvent* event)
    {
        const int qtKey = event->key();
        if (qtKey == Qt::Key_Escape) {
            reject();
            return true;
        }
        if (qtKey == Qt::Key_M) {
            m_choice.female = !m_choice.female;
            refresh();
            return true;
        }
        const int row = m_list->currentRow();
        if (!m_inCategory) {
            if ((qtKey == Qt::Key_Right || qtKey == Qt::Key_Return || qtKey == Qt::Key_Enter)
                && row >= 0) {
                const auto category = static_cast<std::size_t>(row);
                // (On the thread in use, if it's in this category)
                showThreads(
                    category,
                    category == m_choice.category ? static_cast<int>(m_choice.thread) : 0);
                return true;
            }
            return false; // (e.g. Up/Down, for the list)
        }
        if (qtKey == Qt::Key_Left || qtKey == Qt::Key_Backspace) {
            showCategories(m_category);
            return true;
        }
        if ((qtKey == Qt::Key_Return || qtKey == Qt::Key_Enter) && row >= 0) {
            m_choice.category = m_category;
            m_choice.thread = static_cast<std::size_t>(row);
            accept();
            return true;
        }
        return false;
    }

    const ThreadTable& m_threads;
    ThreadChoice m_choice;
    bool m_inCategory { false };
    std::size_t m_category { 0 };
    QLabel* m_heading { nullptr };
    QListWidget* m_list { nullptr };
    ThreadDiagram* m_diagram { nullptr };
    QLabel* m_figures { nullptr };
    QLabel* m_gender { nullptr };
    QLabel* m_hint { nullptr };
};

} // end anonymous namespace

QDialog* openInput(
    QWidget* parent,
    const QString& fontFamily,
    Input::Type type,
    std::string_view prompt,
    std::vector<std::string> additionalText,
    std::string_view defaultEntry,
    std::optional<std::vector<std::string>> listItems,
    std::function<void(const Input::Return&)> onDone)
{
    // As before, these are designed to be called from more specialised
    // wrappers in the controller, which never supply both
    assert(additionalText.empty() || !listItems.has_value());
    auto* dlg = new InputDialog(
        parent, fontFamily, type, prompt, std::move(additionalText), defaultEntry, listItems);
    // (Deleted once closed, after finished() has been emitted)
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    QObject::connect(dlg, &QDialog::finished, dlg, [dlg, onDone = std::move(onDone)](int) {
        onDone(dlg->inputResult());
    });
    dlg->open();
    return dlg;
}

QDialog* openThreadDialog(
    QWidget* parent,
    const QString& fontFamily,
    const ThreadTable& threads,
    ThreadChoice current,
    std::function<void(std::optional<ThreadChoice>)> onDone)
{
    auto* dlg = new ThreadDialog(parent, fontFamily, threads, current);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    QObject::connect(dlg, &QDialog::finished, dlg, [dlg, onDone = std::move(onDone)](int) {
        onDone(dlg->chosenThread());
    });
    dlg->open();
    return dlg;
}

QDialog* openTaperDialog(
    QWidget* parent,
    const QString& fontFamily,
    double currentAngle,
    std::function<void(std::optional<double>)> onDone)
{
    auto* dlg = new TaperDialog(parent, fontFamily, currentAngle);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    QObject::connect(dlg, &QDialog::finished, dlg, [dlg, onDone = std::move(onDone)](int) {
        onDone(dlg->chosenAngle());
    });
    dlg->open();
    return dlg;
}

} // end namespace dialog
} // end namespace mgo
