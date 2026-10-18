#pragma once

#include "Slic3r/App/Yoga/Window.hpp"

namespace Slic3r::App::Yoga {

class Text;
class LayoutButton;
class Separator;

class CollapsibleWindow : public Window
{
public:
    struct CollapsibleWindowCallbacks
    {
        std::function<void(bool collapsed)> collapsed_changed{nullptr};
    };

    explicit CollapsibleWindow(const std::string& label, const std::string& window_name);

    template <class T, class... Args>
    T* emplace_into_header(Args&&... args)
    {
        std::unique_ptr<T> item = std::make_unique<T>(std::forward<Args>(args)...);
        T* item_raw             = item.get();
        m_header_row->insert(std::move(item), m_header_row->object_count() - 1);
        return item_raw;
    }

    /**
     * @brief Inserts an item right after the label (e.g. a switch next to the title); a spacer
     * between it and the items of emplace_into_header() takes the free width.
     */
    template <class T, class... Args>
    T* emplace_after_label(Args&&... args)
    {
        std::unique_ptr<T> item = std::make_unique<T>(std::forward<Args>(args)...);
        T* item_raw             = item.get();
        m_header_row->insert(std::move(item), *m_header_row->index_of(header_spacer()));
        return item_raw;
    }

    const std::string& label() const;
    void set_label(const std::string& label);

    bool collapsed() const;
    void set_collapsed(bool collapse);
    /// Without its title row (and separator), e.g. shown as a tab's page inside another panel
    void set_header_visible(bool visible);

    void set_flex_grow(float flex);
    /// Applied while expanded; collapsed, the window is its header's size
    void set_flex_basis(std::optional<float> flex_basis);

    Item* content() const;

    CollapsibleWindowCallbacks& collapsible_window_callbacks();

private:
    Item* header_spacer();

    CollapsibleWindowCallbacks m_callbacks;

    bool m_collapsed{false};
    Item* m_header_row{nullptr};
    Text* m_label{nullptr};
    Item* m_header_spacer{nullptr};
    LayoutButton* m_collapse_button{nullptr};
    Item* m_content{nullptr};
    Separator* m_separator{nullptr};
    float m_set_flex_grow{0};
    std::optional<float> m_flex_basis;
};

} // namespace Slic3r::App::Yoga
