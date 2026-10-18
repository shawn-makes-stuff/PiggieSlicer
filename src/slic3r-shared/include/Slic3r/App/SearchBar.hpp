#pragma once

#include "Slic3r/App/SearchObservableList.hpp"
#include "Slic3r/App/Yoga/Item.hpp"

#include <functional>
#include <optional>
#include <string>

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App::Yoga {
class InputText;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {

class Navigator;
class SearchPopup;

class SearchBar : public Yoga::Item
{
public:
    explicit SearchBar(Biz::ProjectInteractor& project_interactor, Navigator& navigator);

    void render(const Yoga::Vec2f& pos, const Yoga::Vec2f& size) override;

    void focus_search();

    /// Searches something else than the settings (e.g. the preview's G-code and legend): typing
    /// calls text_changed, Enter calls entered; no settings results popup.
    struct ContextSearch
    {
        std::string hint;
        std::function<void(const std::string& text)> text_changed;
        std::function<void(const std::string& text)> entered;
    };
    void set_context_search(ContextSearch context_search);

private:
    void update_open_popup();

private:
    Biz::ProjectInteractor& m_project_interactor;
    Navigator& m_navigator;

    Biz::UnsharedPointer<SearchObservableList> m_search_observable_list;
    SearchPopup* m_search_popup{nullptr};

    Yoga::InputText* m_input_text{nullptr};
    std::optional<ContextSearch> m_context_search;
};

} // namespace Slic3r::App
