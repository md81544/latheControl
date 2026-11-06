#include "qt_dialog.h"

#include "log.h"

#include <QDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPixmap>
#include <QRegularExpressionValidator>
#include <QVBoxLayout>

#include <cassert>
#include <cctype>
#include <map>
#include <set>

namespace mgo {
namespace dialog {

namespace {

// Colours and sizes as used by the SFML dialogs
constexpr int PROMPT_FONT_SIZE = 20;
constexpr int BODY_FONT_SIZE = 16;
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
            m_list->setFont(bodyFont);
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

} // end anonymous namespace

Input::Return getInput(
    QWidget* parent,
    const QString& fontFamily,
    Input::Type type,
    std::string_view prompt,
    std::vector<std::string> additionalText,
    std::string_view defaultEntry,
    std::optional<std::vector<std::string>> listItems)
{
    // As before, these are designed to be called from more specialised
    // wrappers in the controller, which never supply both
    assert(additionalText.empty() || !listItems.has_value());
    InputDialog dlg(
        parent, fontFamily, type, prompt, std::move(additionalText), defaultEntry, listItems);
    dlg.exec();
    return dlg.inputResult();
}

} // end namespace dialog
} // end namespace mgo
