#pragma once

#include "iview.h"

#include <memory>
#include <string>

namespace mgo {

// Qt implementation of the view. Qt types are kept out of this header (they
// live in the implementation, view_qt.cpp), so the rest of the program doesn't
// depend on Qt.
//
// Note a QApplication must exist before this is initialised. The controller
// runs on Qt's event loop (runEventLoop()), with a timer calling it to update.
// Dialogs don't block: they're window modal, and call back when closed.
class ViewQt final : public IView {
public:
    ViewQt();
    ~ViewQt() override;
    void initialise(const Model&) override;
    void close() override;
    void runEventLoop(std::chrono::milliseconds interval, std::function<void()> tick) override;
    void quitEventLoop() override;
    int getEvents() override;
    void openInput(
        Input::Type type,
        std::string_view prompt,
        std::vector<std::string> additionalText,
        std::function<void(const Input::Return&)> onDone,
        std::string_view defaultEntry = "",
        std::optional<std::vector<std::string>> listItems = std::nullopt) override;
    void openTaperDialog(double currentAngle, std::function<void(std::optional<double>)> onDone)
        override;
    void openThreadDialog(
        const ThreadTable& threads,
        ThreadChoice current,
        std::function<void(std::optional<ThreadChoice>)> onDone) override;
    void openMultiPassDialog(
        const MultiPassDialogInfo& info,
        std::function<void(std::optional<MultiPassSetup>)> onDone) override;
    void openGearDialog(
        const GearDialogInfo& info,
        std::function<void(std::optional<GearSetup>)> onDone) override;
    bool isDialogOpen() const override;
    void cancelDialog() override;
    void updateDisplay(const Model&) override;
    void setMenu(const Menu* menu) override;

    // Whether a menu row (a command's description, with its hotkey added in
    // brackets if it isn't in the description, and its shortcut on the right)
    // fits across the menu without the two overlapping. For the tests, as the
    // font is only known once initialised
    bool
    menuRowFits(const std::string& description, char hotkey, const std::string& shortcut) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace mgo
