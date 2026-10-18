#include "Slic3r/App/SearchBar.hpp"

#include "Slic3r/App/Yoga/InputText.hpp"
#include "Slic3r/App/Yoga/Icon.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/Validator.hpp"
#include "Slic3r/App/SearchPopup.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"

using namespace Slic3r::App::Yoga;
using namespace Slic3r::Biz;

namespace Slic3r::App {

SearchBar::SearchBar(Biz::ProjectInteractor& project_interactor, Navigator& navigator) :
    m_project_interactor(project_interactor),
    m_navigator(navigator),
    m_search_observable_list(
        std::make_shared<SearchObservableList>(project_interactor.preset_interactor())
    )
{
    m_search_popup = emplace_back<SearchPopup>(
        m_project_interactor,
        m_navigator,
        m_search_observable_list.get()
    );
    // results right below the search box, as wide as it (sidebar)
    // beside the sidebar, so the settings it navigates to stay in view
    m_search_popup->attach_to_item(this, Position::Right, 8, AlignU::Start);

    set_gap(5);
    set_align_items(YGAlignCenter);

    Icon* icon = emplace_back<Icon>(Render::Icon::Search);
    icon->set_width(18);
    icon->set_height(18);

    m_input_text = emplace_back<InputText>();
    m_input_text->set_hint(_u8L("Search..."));
    // fills its row (the sidebar's search box)
    m_input_text->set_min_width(150);
    m_input_text->set_flex_grow(1);
    m_input_text->set_flags(ImGuiInputTextFlags_EscapeClearsAll);

    // the box keeps the focus: Enter again goes to the next result
    m_input_text->callbacks().text_entered = [this]
    {
        if (m_context_search) {
            if (m_context_search->entered)
                m_context_search->entered(m_input_text->text());
            m_input_text->request_focus();
            return;
        }
        m_search_popup->open_selected();
        m_search_popup->navigate_down();
        m_input_text->request_focus();
    };
    m_input_text->callbacks().text_changed = [this]
    {
        const std::string& text = m_input_text->text();
        if (m_context_search) {
            if (m_context_search->text_changed)
                m_context_search->text_changed(text);
            return;
        }
        m_search_observable_list->set_search_text(text);
        if (!text.empty()) {
            m_search_popup->content_item()->set_width(width());
            m_search_popup->select_top();
            m_search_popup->open();
            m_search_popup->content_item()->bring_to_front();
        } else {
            m_search_popup->close();
        }
    };
    m_input_text->callbacks().focus_lost   = [this] { update_open_popup(); };
    m_input_text->callbacks().focus_gained = [this]
    {
        if (m_context_search)
            return;
        const std::string& text = m_input_text->text();
        if (!text.empty()) {
            m_search_popup->content_item()->set_width(width());
            m_search_popup->select_top();
            m_search_popup->open();
            m_search_popup->content_item()->bring_to_front();
        }
    };
    m_search_popup->content_item()->callbacks().hovered_changed = [this](bool)
    { update_open_popup(); };
}

void SearchBar::render(const Yoga::Vec2f& pos, const Yoga::Vec2f& size)
{
    Item::render(pos, size);

    if (m_input_text->active() && !m_context_search) {
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
            m_search_popup->navigate_up();
        } else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
            m_search_popup->navigate_down();
        } else if (ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            m_search_popup->open_selected();
        }
    }
}

void SearchBar::focus_search()
{
    m_input_text->request_focus();
}

void SearchBar::set_context_search(ContextSearch context_search)
{
    if (!context_search.hint.empty())
        m_input_text->set_hint(context_search.hint);
    m_context_search = std::move(context_search);
}

void SearchBar::update_open_popup()
{
    if (!m_search_popup->content_item()->hovered() && !m_input_text->has_focus()) {
        m_search_popup->close();
    }
}

} // namespace Slic3r::App
