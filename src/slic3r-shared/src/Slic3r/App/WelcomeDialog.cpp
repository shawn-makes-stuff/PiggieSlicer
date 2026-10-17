#include "Slic3r/App/WelcomeDialog.hpp"
#include <ranges>
#include "Slic3r/App/AppConfig.hpp"
#include "Slic3r/App/AppConfigInteractor.hpp"
#include "Slic3r/App/Yoga/Circle.hpp"
#include "Slic3r/App/Yoga/Icon.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/Namespace.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/ScrollArea.hpp"
#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/AddPrinterPanel.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/Preset/HwConfigEvaluator.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"

#include "Slic3r/Version.hpp"

#include <fmt/format.h>

namespace Slic3r::App {

using namespace Yoga;

static ImColor red{249, 115, 22};
static ImColor green{34, 197, 94, 178};

class Paragraph : public Yoga::Item
{
public:
    Paragraph(const std::string& text,
              std::optional<Unit> row_height = std::nullopt,
              std::optional<ImColor> color   = std::nullopt) :
        m_row_height{row_height},
        m_color{color}
    {
        set_flex_shrink(0);
        set_flex_wrap(YGWrapWrap);
        set_text(text);
    }

    void set_text(const std::string& text)
    {
        while (!items().empty()) {
            remove(items().back());
        }

        for (auto&& word_range : text | std::views::split(' ')) {
            std::string word(&*word_range.begin(), std::ranges::distance(word_range));
            word += " ";

            auto item{emplace_back<Item>()};
            item->set_flex_shrink(0);
            item->set_align_items(YGAlignCenter);
            if (m_row_height) {
                item->set_height(*m_row_height);
            }

            auto text_item{item->emplace_back<Text>(word)};
            if (m_color) {
                text_item->set_text_color(*m_color);
            }
        }
    }

private:
    std::optional<Unit> m_row_height;
    std::optional<ImColor> m_color;
};

struct ColoredText
{
    std::string text;
    std::optional<ImColor> color;
};

class UnorderedList : public Yoga::Item
{
public:
    UnorderedList(Render::Icon icon,
                  ImColor icon_color,
                  std::initializer_list<std::initializer_list<ColoredText>> list_items)
    {
        set_orientation(Orientation::Vertical);
        set_flex_shrink(0);
        for (std::initializer_list<ColoredText> list_item : list_items) {
            auto line{emplace_back<Item>()};
            line->set_flex_shrink(0);
            if (icon != Render::Icon::None) {
                auto icon_item{line->emplace_back<Icon>(icon)};
                icon_item->set_width(16_fpx);
                icon_item->set_height(16_fpx);
                icon_item->set_flex_shrink(0);
                icon_item->set_tint(icon_color);
            }
            line->set_gap(10_fpx);
            auto text_item{line->emplace_back<Item>()};
            text_item->set_flex_shrink(0);
            text_item->set_flex_wrap(YGWrap::YGWrapWrap);
            for (const ColoredText& colored_text : list_item) {
                for (auto&& word_range : colored_text.text | std::views::split(' ')) {
                    if (word_range.begin() == word_range.end()) {
                        continue;
                    }
                    std::string word(&*word_range.begin(), std::ranges::distance(word_range));
                    word += " ";
                    auto text{text_item->emplace_back<Text>(word)};
                    if (colored_text.color) {
                        text->set_text_color(*colored_text.color);
                    }
                }
            }
        }
    }
};

static Yoga::Item* append_item(Yoga::Item* parent, Yoga::ItemPtr item)
{
    auto item_ptr{item.get()};
    parent->append(std::move(item));
    return item_ptr;
}

struct NavigationSetup
{
    std::string left_button_text;
    std::function<void()> left_button_action{[]() {}};
    std::string center_button_text;
    std::function<void()> center_button_action{[]() {}};
    std::string rigth_button_text;
    std::function<void()> rigth_button_action{[]() {}};
};

static ItemPtr create_navigation(const NavigationSetup& setup, const Theme& theme)
{
    auto result{std::make_unique<Item>()};
    result->set_orientation(Orientation::Vertical);
    result->set_width_percent(100);
    result->set_flex_shrink(0);

    auto border{result->emplace_back<Rectangle>()};
    border->set_width_percent(100);
    border->set_height(1_px);
    border->set_fill(theme.color_imgui(Platform::Color::Text, Platform::ColorGroup::Disabled));
    border->set_flex_shrink(0);

    auto content{result->emplace_back<Item>()};
    content->set_flex_shrink(0);
    content->set_height(85_fpx);
    content->set_width_percent(100);

    content->set_align_items(YGAlignCenter);
    content->set_padding({45_fpx, 0, 45_fpx, 0});

    auto left_item{content->emplace_back<Item>()};
    left_item->set_flex_grow(1);
    left_item->set_width_percent(25);

    if (!setup.left_button_text.empty()) {
        auto left_button{left_item->emplace_back<LayoutButton>(Biz::_u8L("Back"))};
        left_button->set_background_color(Platform::Color::ButtonTransparent);
        left_button->set_content_padding({23_fpx, 14_fpx, 23_fpx, 14_fpx});
        left_button->callbacks().action = setup.left_button_action;
    }

    auto center_item{content->emplace_back<Item>()};
    center_item->set_flex_grow(1);
    center_item->set_justify_content(YGJustifyCenter);
    center_item->set_width_percent(25);

    if (!setup.center_button_text.empty()) {
        auto center_button{center_item->emplace_back<LayoutButton>(setup.center_button_text)};
        center_button->set_background_color(Platform::Color::AccentPrimary);
        center_button->set_width(200_fpx);
        center_button->set_height(45_fpx);
        center_button->set_label_font_type(Render::ImguiFontType::Bold);
        center_button->callbacks().action = setup.center_button_action;
    }

    auto right_item{content->emplace_back<Item>()};
    right_item->set_flex_grow(1);
    right_item->set_width_percent(25);
    right_item->set_justify_content(YGJustifyFlexEnd);

    if (!setup.rigth_button_text.empty()) {
        auto right_button{right_item->emplace_back<LayoutButton>(setup.rigth_button_text)};
        right_button->set_background_color(Platform::Color::ButtonTransparent);
        right_button->set_content_padding({23_fpx, 14_fpx, 23_fpx, 14_fpx});
        right_button->callbacks().action = setup.rigth_button_action;
    }

    return result;
}

class Screen : public Item
{
public:
    Screen(const NavigationSetup& navigation_setup = {})
    {
        set_orientation(Orientation::Vertical);
        set_width_percent(100);
        m_content = emplace_back<ScrollArea>();
        m_content->set_width_percent(100);
        m_content->set_orientation(Orientation::Vertical);
        m_content->set_align_items(YGAlignCenter);
        m_content->set_gap(30_fpx);
        m_content->set_flex_grow(1);
        m_content->set_padding(20_fpx);
        update_navigation(navigation_setup);
    }

    void update_navigation(const NavigationSetup& navigation_setup)
    {
        if (m_navigation) {
            remove(m_navigation);
        }
        m_navigation = append_item(this, create_navigation(navigation_setup, *m_theme));
    }

    Item* content()
    {
        return m_content;
    }

private:
    Item* m_content{nullptr};
    Item* m_navigation{nullptr};
};

class Title : public Item
{
public:
    Title(const std::string& title, const std::string& sub_title = "")
    {
        set_align_items(YGAlignCenter);
        set_orientation(Orientation::Vertical);
        set_gap(10_fpx);
        set_flex_shrink(0);
        m_title = emplace_back<Text>(title);
        m_title->set_font_size(32_fpx);
        m_title->set_padding({0, 3_fpx, 0, 3_fpx});
        m_title->set_font_type(Render::ImguiFontType::Bold);

        if (!sub_title.empty()) {
            set_sub_title(sub_title);
        }
    }

    void set_title(const std::string& title)
    {
        m_title->set_text(title);
    }

    void set_sub_title(const std::string& sub_title)
    {
        if (sub_title.empty()) {
            if (m_sub_title) {
                remove(m_sub_title);
                m_sub_title = nullptr;
            }
            return;
        }
        if (!m_sub_title) {
            const ImColor secondary_color{
                m_theme->color_imgui(Platform::Color::Text, Platform::ColorGroup::Disabled)};

            m_sub_title = emplace_back<Paragraph>(sub_title, 18_fpx, secondary_color);
            m_sub_title->set_width(380_fpx);
            m_sub_title->set_justify_content(YGJustifyCenter);
        } else {
            m_sub_title->set_text(sub_title);
        }
    }

private:
    Text* m_title{nullptr};
    Paragraph* m_sub_title{nullptr};
};

static ItemPtr create_changelog_screen(const Theme& theme, std::function<void()> go_to_next)
{
    const NavigationSetup navigation_setup{
        .center_button_text   = Biz::_u8L("Get started"),
        .center_button_action = go_to_next,
    };
    auto result{std::make_unique<Screen>(navigation_setup)};

    result->content()->emplace_back<Title>(SLIC3R_VERSION, Biz::_u8L("EARLY PREVIEW"));

    auto introduction{result->content()->emplace_back<Paragraph>(
        Biz::_u8L(
            "PiggieSlicer is based on PrusaSlicer 3.0.0, which brings multi-project support,"
            " different printers within one project, a new UI, a new profile system for multi-tool machines,"
            " plus early plugin support and built-in calibration prints."
            " It's still rough in places."
        ),
        18_fpx
    )};

    introduction->set_justify_content(YGJustifyCenter);
    introduction->set_width(460_fpx);

    auto notes_gap{10_fpx};
    auto notes{result->content()->emplace_back<Item>()};
    notes->set_flex_shrink(0);
    notes->set_gap(notes_gap);
    notes->set_orientation(Orientation::Vertical);
    notes->set_width(500_fpx);

    const ImColor secondary_color{
        theme.color_imgui(Platform::Color::Text, Platform::ColorGroup::Disabled)};

    auto positive_notes{notes->emplace_back<UnorderedList>(
        Render::Icon::CheckMark,
        green,
        std::initializer_list<std::initializer_list<ColoredText>>{
            {
                {Biz::_u8L("Your existing PrusaSlicer 2.x 3MF projects load and work.")},
                {
                    Biz::_u8L("Configuration is automatically converted to the new format."),
                    secondary_color,
                },
            },
            {
                {Biz::_u8L("Profiles are stored separately.")},
                {
                    Biz::_u8L("PiggieSlicer runs safely alongside PrusaSlicer with no conflicts."),
                    secondary_color,
                },
            },
        })};
    positive_notes->set_gap(notes_gap);

    auto negative_notes{notes->emplace_back<UnorderedList>(
        Render::Icon::TopBarCross,
        red,
        std::initializer_list<std::initializer_list<ColoredText>>{
            {
                {Biz::_u8L("This is a genuine alpha.")},
                {
                    Biz::_u8L("Expect instability and unfinished features."),
                    secondary_color,
                },
            },
            {
                {Biz::_u8L("Not all PrusaSlicer 2.x features have been ported yet.")},
                {
                    Biz::_u8L("Some are already under development."),
                    secondary_color,
                },
            },
            {{Biz::_u8L("Standalone G-code viewer is not available in this release.")}},
            {
                {Biz::_u8L("Third-party printer profiles are not included in this alpha.")},
                {
                    Biz::_u8L("They will be added later."),
                    secondary_color,
                },
            },
        })};
    negative_notes->set_gap(notes_gap);

    return result;
}

struct PrinterToAdd
{
    Domain::Preset::HwPrinterConfig config;
    std::string preset_item_id;
};

class PrinterScreen : public Item
{
public:
    PrinterScreen(Biz::ProjectInteractor& project_interactor,
                  const Theme& theme,
                  std::function<void()> go_to_prev,
                  std::function<void(const std::vector<PrinterToAdd>&)> finish,
                  std::function<void(bool)> hide_top_bar) :
        m_project_interactor{project_interactor},
        m_go_to_prev{go_to_prev},
        m_finish{finish},
        m_hide_top_bar{hide_top_bar}
    {
        m_screen = emplace_back<Screen>();
        update_navigation();

        m_title = m_screen->content()->emplace_back<Title>(m_default_title, m_default_subtitle);

        m_add_button =
            m_screen->content()->emplace_back<LayoutButton>(Biz::_u8L("Choose a printer"));
        m_add_button->set_background_color(Platform::Color::AccentPrimary);
        m_add_button->set_content_padding({30_fpx, 10_fpx, 30_fpx, 10_fpx});
        m_add_button->callbacks().action = [this]() { show_dialog(true); };

        m_printers = m_screen->content()->emplace_back<Rectangle>();
        m_printers->set_flex_shrink(0);
        m_printers->set_border_color(m_theme->color_imgui(Platform::Color::WindowBgAlternate));
        m_printers->set_border_width(1);
        m_printers->set_fill(m_theme->color_imgui(Platform::Color::WindowBg));
        m_printers->set_rounding(10);
        m_printers->set_padding(10_fpx);
        m_printers->set_orientation(Orientation::Vertical);
        m_printers->set_gap(10_fpx);
        m_printers->set_visible(false);

        m_dialog = emplace_back<AddPrinterPanel>(
            project_interactor,
            [this]() { show_dialog(false); },
            [this](const AddPrinterPanel::Printer& printer)
            {
                const Domain::Preset::VendorData& vendor_data{
                    m_project_interactor.workbench()
                        .preset_bundle()
                        .vendor_bundles.at(printer.default_config.vendor_id)
                        .vendor_data};

                Domain::Preset::HwPrinterConfig config{printer.default_config};
                config.sheet = Biz::Preset::from_def(
                    vendor_data,
                    printer.sheets.sheet_defs.at(printer.sheets.selected_sheet));
                config.tools.clear();
                for (std::size_t selected_tool : printer.tools.selected_tools) {
                    config.tools.push_back(
                        Biz::Preset::from_def(vendor_data,
                                              printer.tools.tool_defs.at(selected_tool)));
                }

                add_printer(PrinterToAdd{config, printer.preset_item_id});
                show_dialog(false);
            });
        m_dialog->set_visible(false);
    }

    void restart() {
        show_dialog(false);
        m_title->set_title(m_default_title);
        m_title->set_sub_title(m_default_subtitle);
        clear_printers();
        reload_content_state();
    }

    void show_dialog(bool show)
    {
        m_screen->set_visible(!show);
        m_dialog->set_visible(show);
        m_hide_top_bar(show);
    }

    void update_navigation()
    {
        if (!m_printers_to_add.empty()) {
            const NavigationSetup navigation_setup{
                .left_button_text     = Biz::_u8L("Back"),
                .left_button_action   = m_go_to_prev,
                .center_button_text   = Biz::_u8L("Finish"),
                .center_button_action = [this]() { m_finish(m_printers_to_add); },
                .rigth_button_text   = Biz::_u8L("Add another printer"),
                .rigth_button_action = [this]() { show_dialog(true); },
            };
            m_screen->update_navigation(navigation_setup);
        } else {
            const NavigationSetup navigation_setup{.left_button_text   = Biz::_u8L("Back"),
                                                   .left_button_action = m_go_to_prev};
            m_screen->update_navigation(navigation_setup);
        }
    }

    void reload_content_state()
    {
        if (m_printers_to_add.empty()) {
            m_printers->set_visible(false);
            m_add_button->set_visible(true);
        } else {
            m_printers->set_visible(true);
            m_add_button->set_visible(false);
        }
        update_navigation();
    }

    void delete_printer(const std::string& preset_item_id)
    {
        ASSERT(m_printers_to_add.size() == m_printers->items().size());

        auto it{std::ranges::find_if(m_printers_to_add,
                                     [&](const PrinterToAdd& printer)
                                     { return printer.preset_item_id == preset_item_id; })};

        const std::size_t index{
            static_cast<std::size_t>(std::distance(m_printers_to_add.begin(), it))};
        ASSERT(index < m_printers_to_add.size());

        m_printers->remove_later(m_printers->items().at(index));
        m_printers_to_add.erase(it);
        clear_state();
    }

    void clear_printers()
    {
        while (!m_printers->items().empty()) {
            m_printers->remove(m_printers->items().back());
        }
        m_printers_to_add.clear();
    }

    void clear_state() {
        m_title->set_title(m_default_title);
        m_title->set_sub_title(m_default_subtitle);

        reload_content_state();
    }

    void add_printer(const PrinterToAdd& printer)
    {
        auto it{
            std::ranges::find_if(m_printers_to_add,
                                 [&](const PrinterToAdd& candidate)
                                 { return candidate.preset_item_id == printer.preset_item_id; })};

        const std::size_t index{
            static_cast<std::size_t>(std::distance(m_printers_to_add.begin(), it))};

        Rectangle* printer_item{nullptr};
        if (it == m_printers_to_add.end()) {
            m_printers_to_add.push_back(printer);
            printer_item = m_printers->emplace_back<Rectangle>();
        } else {
            *it          = printer;
            printer_item = dynamic_cast<Rectangle*>(m_printers->items().at(index));
            ASSERT(printer_item);
            while (!printer_item->items().empty()) {
                printer_item->remove(printer_item->items().back());
            }
        }

        ASSERT(m_printers_to_add.size() == m_printers->items().size());

        printer_item->set_flex_shrink(0);
        printer_item->set_width(350_fpx);
        printer_item->set_padding({20_fpx, 10_fpx, 20_fpx, 10_fpx});
        printer_item->set_rounding(10);
        printer_item->set_fill(m_theme->color_imgui(Platform::Color::WindowBgAlternate));
        printer_item->set_justify_content(YGJustifySpaceBetween);
        auto label{printer_item->emplace_back<Item>()};
        label->set_orientation(Orientation::Vertical);
        label->set_justify_content(YGJustifyCenter);
        label->set_flex_grow(1);
        auto title{label->emplace_back<Text>(printer.config.short_name)};
        title->set_font_type(Render::ImguiFontType::Bold);
        title->set_flex_shrink(0);

        const std::string sheet_name{printer.config.sheet.name};

        std::string nozzles;
        for (std::size_t i{}; i < printer.config.tools.size(); ++i) {
            nozzles += (i == 0 ? "" : ", ") + printer.config.tools[i].name;
        }

        auto sub_title{label->emplace_back<Text>(
            fmt::format(fmt::runtime("{} sheet, {}"), sheet_name, nozzles)
        )};
        sub_title->set_text_color(
            m_theme->color_imgui(Platform::Color::Text, Platform::ColorGroup::Disabled));
        sub_title->set_wrap_mode(Yoga::Text::WrapMode::Wrap);
        sub_title->set_flex_shrink(0);

        auto right_section{printer_item->emplace_back<Item>()};
        right_section->set_align_items(YGAlign::YGAlignCenter);
        auto delete_button{
            right_section->emplace_back<LayoutButton>("", Render::Icon::DeleteBtnIcon)};
        delete_button->set_width(19_fpx);
        delete_button->set_aspect_ratio(1);
        delete_button->callbacks().action = [this, id = printer.preset_item_id]()
        { delete_printer(id); };

        clear_state();
    }

private:
    Biz::ProjectInteractor& m_project_interactor;
    std::function<void()> m_go_to_prev;
    std::function<void(const std::vector<PrinterToAdd>&)> m_finish;
    std::function<void(bool)> m_hide_top_bar;
    Screen* m_screen{nullptr};
    AddPrinterPanel* m_dialog{nullptr};
    Title* m_title{nullptr};
    LayoutButton* m_add_button{nullptr};
    Rectangle* m_printers{nullptr};
    std::vector<PrinterToAdd> m_printers_to_add;

    std::string m_default_title{Biz::_u8L("Choose your printer")};
    std::string m_default_subtitle{
        Biz::_u8L("If you have more then one printer you can also add them now.")};
};

WelcomeDialog::WelcomeDialog(Biz::ProjectInteractor& project_interactor) :
    m_project_interactor{project_interactor}
{
    WindowPtr window{std::make_unique<Window>("Welcome dialog")};
    window->set_orientation(Orientation::Vertical);
    window->set_gap(0);
    window->set_padding(0);
    window->set_flags(ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
    window->set_width(100_ww);
    window->set_height(100_wh);
    window->set_max_width(800_fpx);
    window->set_max_height(600_fpx);
    window->set_align_items(YGAlignStretch);
    window->set_modal(true);

    set_content_item(std::move(window));

    m_top_bar = content_item()->emplace_back<Item>();
    m_top_bar->set_flex_shrink(0);
    m_top_bar->set_height(60_fpx);
    m_top_bar->set_padding(15_fpx);
    m_top_bar->set_gap(10_fpx);
    m_top_bar->set_align_items(YGAlignCenter);

    m_content = content_item()->emplace_back<Item>();
    m_content->set_align_items(YGAlignStretch);
    m_content->set_flex_grow(1);
    m_changelog_screen = append_item(
        m_content,
        create_changelog_screen(*m_theme,
                                [this]()
                                {
                                    m_printer_screen->restart();
                                    sync_config();
                                    go_to_screen(ASSERT_VAL(m_printer_screen));
                                }));

    m_printer_screen = m_content->emplace_back<PrinterScreen>(
        project_interactor,
        *m_theme,
        [this]() { go_to_screen(ASSERT_VAL(m_changelog_screen)); },
        [this](const std::vector<PrinterToAdd>& printers) { finalize(printers); },
        [this](bool hide) { m_top_bar->set_visible(!hide); });

    for (Item* child : m_content->items()) {
        child->set_width_percent(100);
        child->set_flex_shrink(0);
    }

    go_to_screen(m_changelog_screen);
}

void WelcomeDialog::finalize(const std::vector<PrinterToAdd>& printers)
{
    std::vector<Domain::Preset::HwPrinterConfig> printer_configs;
    printer_configs.reserve(printers.size());

    std::vector<std::string> printer_preset_item_ids;
    printer_preset_item_ids.reserve(printers.size());

    for (const PrinterToAdd& printer : printers) {
        printer_configs.push_back(printer.config);
        printer_preset_item_ids.push_back(printer.preset_item_id);
    }

    m_project_interactor.preset_interactor().update_changed_printer_configs(printer_configs);

    AppSettingsAdvanced& app_settings_advanced{
        AppServices::instance().app_config().app_settings_advanced()};
    app_settings_advanced.printer_favorite_presets.clear();

    ASSERT(!printer_configs.empty());
    ASSERT(printer_preset_item_ids.size() == printer_configs.size());

    for (std::size_t i{}; i < printer_configs.size(); ++i) {
        Domain::Preset::HwPrinterConfig& hw_config{printer_configs[i]};
        const std::string& printer_preset_item_id{printer_preset_item_ids[i]};
        app_settings_advanced.toggle_printer_favorite_preset(printer_preset_item_id, hw_config.id);
    }

    m_project_interactor.preset_interactor().select_printer_preset(printer_configs.front().id,
                                                                   printer_preset_item_ids.front());

    auto& app_config_interactor{AppServices::instance().app_config_interactor()};
    app_config_interactor.set_item_value("printers_only_favorites", Domain::ConfigValue{false});
    if (!app_settings_advanced.printer_favorite_presets.empty()) {
        app_config_interactor.set_item_value("printers_only_favorites", Domain::ConfigValue{true});
    }
    sync_config();
    app_config_interactor.set_item_value("initialized", Domain::ConfigValue{true});
    AppServices::instance().app_config().save();
    close();
}

void WelcomeDialog::reload_top_bar()
{
    while (!m_top_bar->items().empty()) {
        m_top_bar->remove(m_top_bar->items().back());
    }

    const auto current_screen_it{std::ranges::find(m_content->items(), m_current_screen)};
    const std::size_t current_screen_index{
        static_cast<std::size_t>(std::distance(m_content->items().begin(), current_screen_it))};
    ASSERT(current_screen_index < m_content->items().size());

    for (std::size_t i{}; i < m_content->items().size(); ++i) {
        const Item* screen{m_content->items().at(i)};

        if (screen == m_current_screen) {
            auto slicer_icon{m_top_bar->emplace_back<Icon>(Render::Icon::PrusaSlicerIcon)};
            slicer_icon->set_min_width(30_fpx);
            slicer_icon->set_min_height(30_fpx);
            slicer_icon->set_preserve_colors(true); // keep the white sticker backing in light theme
        } else {
            auto screen_indicator{m_top_bar->emplace_back<Circle>()};
            screen_indicator->set_width(6_fpx);
            screen_indicator->set_height(6_fpx);
            screen_indicator->set_fill(m_theme->color_imgui(Platform::Color::Text));
            if (i > current_screen_index) {
                screen_indicator->set_fill(
                    m_theme->color_imgui(Platform::Color::Text, Platform::ColorGroup::Disabled));
            }
        }
    }
}

void WelcomeDialog::go_to_screen(const Item* screen)
{
    const auto it{std::ranges::find(m_content->items(), screen)};

    ASSERT(it != m_content->items().end());

    for (Item* screen : m_content->items()) {
        screen->set_visible(false);
    }

    (*it)->set_visible(true);
    m_current_screen = screen;
    reload_top_bar();
}

void WelcomeDialog::sync_config() const
{
    auto& app_config{AppServices::instance().app_config()};
    auto& app_config_interactor{AppServices::instance().app_config_interactor()};
    if (app_config.is_sentry_available()) {
        app_config_interactor.set_item_value("sentry", Domain::ConfigValue{false});
    }

    app_config_interactor.set_item_value("enable_preset_update", Domain::ConfigValue{false});
}

} // namespace Slic3r::App
