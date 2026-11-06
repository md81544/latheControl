#pragma once

#include "iview.h"

#include <memory>

namespace mgo {

// Qt implementation of the view. Qt types are kept out of this header (they
// live in the implementation, view_qt.cpp), so the rest of the program doesn't
// depend on Qt.
//
// Note a QApplication must exist before this is initialised. The controller
// still drives its own loop (calling getEvents() and updateDisplay()), so rather
// than running QApplication::exec(), getEvents() processes Qt's pending events.
class ViewQt final : public IView {
public:
    ViewQt();
    ~ViewQt() override;
    void initialise(const Model&) override;
    void close() override;
    int getEvents() override;
    Input::Return getInput(
        Input::Type type,
        std::string_view prompt,
        std::vector<std::string> additionalText,
        std::string_view defaultEntry = "",
        std::optional<std::vector<std::string>> listItems = std::nullopt) override;
    void updateDisplay(const Model&) override;
    void setMenu(const Menu* menu) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace mgo
