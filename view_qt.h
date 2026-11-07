#pragma once

#include "iview.h"

#include <memory>

namespace mgo {

// Qt implementation of the view. Qt types are kept out of this header (they
// live in the implementation, view_qt.cpp), so the rest of the program doesn't
// depend on Qt.
//
// Note a QApplication must exist before this is initialised. The controller
// runs on Qt's event loop (runEventLoop()), with a timer calling it to update.
// Some of the controller's work still blocks inside an update (e.g. waiting
// for a motor to stop), so getEvents() and updateDisplay() also process Qt's
// pending events, to keep the window responsive meanwhile.
class ViewQt final : public IView {
public:
    ViewQt();
    ~ViewQt() override;
    void initialise(const Model&) override;
    void close() override;
    void runEventLoop(std::chrono::milliseconds interval, std::function<void()> tick) override;
    void quitEventLoop() override;
    int getEvents() override;
    Input::Return getInput(
        Input::Type type,
        std::string_view prompt,
        std::vector<std::string> additionalText,
        std::string_view defaultEntry = "",
        std::optional<std::vector<std::string>> listItems = std::nullopt) override;
    std::optional<double> getTaperAngle(double currentAngle) override;
    void updateDisplay(const Model&) override;
    void setMenu(const Menu* menu) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace mgo
