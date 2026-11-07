#include "qt_dialog.h"

#include "log.h"
#include "tapers.h"

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
