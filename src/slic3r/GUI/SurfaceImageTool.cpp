#include "SurfaceImageTool.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <unordered_map>
#include <vector>

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/filedlg.h>
#include <wx/image.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/window.h>

#include "libslic3r/FilamentColorMix.hpp"
#include "libslic3r/CustomGCode.hpp"
#include "libslic3r/MixedFilament.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/TriangleSelector.hpp"
#include "GUI_App.hpp"
#include "GUI_ObjectList.hpp"
#include "GLCanvas3D.hpp"
#include "I18N.hpp"
#include "PartPlate.hpp"
#include "Plater.hpp"
#include "Selection.hpp"

namespace Slic3r { namespace GUI {

void apply_photo_to_top_surface(wxWindow *parent)
{
    Plater *plater = wxGetApp().plater();
    if (plater == nullptr)
        return;
    GLCanvas3D *canvas = plater->get_current_canvas3D();
    const int obj_idx = canvas != nullptr ? canvas->get_selection().get_object_idx() : -1;
    if (obj_idx < 0 || obj_idx >= int(wxGetApp().model().objects.size())) {
        wxMessageBox(_L("Select a single object first."), _L("Photo on top surface"), wxICON_INFORMATION, parent);
        return;
    }
    ModelObject *mo = wxGetApp().model().objects[size_t(obj_idx)];
    if (mo->instances.empty())
        return;

    // Palette = the same paintable colors the color-painting gizmo offers:
    // physical filaments followed by enabled virtual mixed filaments.
    const std::vector<ColorRGBA> palette_rgba = plater->get_extruders_colors_with_mixed();
    const size_t palette_size = std::min<size_t>(palette_rgba.size(), 16); // painting state limit
    if (palette_size == 0) {
        wxMessageBox(_L("No filament colors available."), _L("Photo on top surface"), wxICON_INFORMATION, parent);
        return;
    }
    std::vector<std::string> palette_hex;
    palette_hex.reserve(palette_size);
    for (size_t i = 0; i < palette_size; ++i) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "#%02X%02X%02X",
                      int(palette_rgba[i].r() * 255.f + 0.5f),
                      int(palette_rgba[i].g() * 255.f + 0.5f),
                      int(palette_rgba[i].b() * 255.f + 0.5f));
        palette_hex.emplace_back(buf);
    }

    wxFileDialog dlg(parent, _L("Choose an image to paint onto the top surface"), "", "",
                     "Images (*.png;*.jpg;*.jpeg;*.bmp)|*.png;*.jpg;*.jpeg;*.bmp",
                     wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal() != wxID_OK)
        return;
    wxImage image(dlg.GetPath());
    if (!image.IsOk() || image.GetWidth() <= 0 || image.GetHeight() <= 0) {
        wxMessageBox(_L("Could not load the image."), _L("Photo on top surface"), wxICON_ERROR, parent);
        return;
    }

    const Transform3d inst_tr = mo->instances.front()->get_matrix();

    // Pass 1: world-space XY extents of upward-facing facet centroids, so the
    // image is fitted exactly to the visible top area.
    double min_x = std::numeric_limits<double>::max(), min_y = min_x;
    double max_x = std::numeric_limits<double>::lowest(), max_y = max_x;
    size_t up_facets = 0;
    for (const ModelVolume *mv : mo->volumes) {
        if (!mv->is_model_part())
            continue;
        const Transform3d tr = inst_tr * mv->get_matrix();
        const indexed_triangle_set &its = mv->mesh().its;
        for (const stl_triangle_vertex_indices &f : its.indices) {
            const Vec3d a = tr * its.vertices[f(0)].cast<double>();
            const Vec3d b = tr * its.vertices[f(1)].cast<double>();
            const Vec3d c = tr * its.vertices[f(2)].cast<double>();
            const Vec3d n = (b - a).cross(c - a);
            if (n.norm() <= 0.0 || n.z() / n.norm() < 0.5)
                continue;
            const Vec3d centroid = (a + b + c) / 3.0;
            min_x = std::min(min_x, centroid.x()); max_x = std::max(max_x, centroid.x());
            min_y = std::min(min_y, centroid.y()); max_y = std::max(max_y, centroid.y());
            ++up_facets;
        }
    }
    if (up_facets == 0 || max_x <= min_x || max_y <= min_y) {
        wxMessageBox(_L("The selected object has no upward-facing surface to paint."),
                     _L("Photo on top surface"), wxICON_INFORMATION, parent);
        return;
    }

    Plater::TakeSnapshot snapshot(plater, "Photo on top surface");

    // Pass 2: paint. Cache image color -> palette index (ΔE2000 search is the
    // expensive part; photos have far fewer unique colors than facets).
    std::unordered_map<unsigned int, int> nearest_cache;
    auto nearest_palette_idx = [&](unsigned char r, unsigned char g, unsigned char b) -> int {
        const unsigned int key = (unsigned(r) << 16) | (unsigned(g) << 8) | unsigned(b);
        auto it = nearest_cache.find(key);
        if (it != nearest_cache.end())
            return it->second;
        char hex[8];
        std::snprintf(hex, sizeof(hex), "#%02X%02X%02X", int(r), int(g), int(b));
        int    best_idx = 0;
        double best_de  = std::numeric_limits<double>::max();
        for (size_t i = 0; i < palette_hex.size(); ++i) {
            const double de = ColorMix::delta_e(hex, palette_hex[i]);
            if (de < best_de) { best_de = de; best_idx = int(i); }
        }
        nearest_cache.emplace(key, best_idx);
        return best_idx;
    };

    size_t painted = 0;
    for (ModelVolume *mv : mo->volumes) {
        if (!mv->is_model_part())
            continue;
        const Transform3d tr = inst_tr * mv->get_matrix();
        const indexed_triangle_set &its = mv->mesh().its;
        TriangleSelector selector(mv->mesh());
        // Keep any existing painting on non-top facets.
        selector.deserialize(mv->mmu_segmentation_facets.get_data(), false);
        bool any = false;
        for (size_t i = 0; i < its.indices.size(); ++i) {
            const stl_triangle_vertex_indices &f = its.indices[i];
            const Vec3d a = tr * its.vertices[f(0)].cast<double>();
            const Vec3d b = tr * its.vertices[f(1)].cast<double>();
            const Vec3d c = tr * its.vertices[f(2)].cast<double>();
            const Vec3d n = (b - a).cross(c - a);
            if (n.norm() <= 0.0 || n.z() / n.norm() < 0.5)
                continue;
            const Vec3d centroid = (a + b + c) / 3.0;
            const double u = (centroid.x() - min_x) / (max_x - min_x);
            const double v = (centroid.y() - min_y) / (max_y - min_y);
            const int px = std::min(image.GetWidth()  - 1, std::max(0, int(u * image.GetWidth())));
            // Image rows grow downward while model Y grows upward - flip v.
            const int py = std::min(image.GetHeight() - 1, std::max(0, int((1.0 - v) * image.GetHeight())));
            const int idx = nearest_palette_idx(image.GetRed(px, py), image.GetGreen(px, py), image.GetBlue(px, py));
            selector.set_facet(int(i), EnforcerBlockerType(idx + 1));
            any = true;
            ++painted;
        }
        if (any)
            mv->mmu_segmentation_facets.set(selector);
    }

    // Same refresh sequence the color-painting gizmo uses after a stroke.
    wxGetApp().obj_list()->update_info_items(size_t(obj_idx));
    plater->get_partplate_list().notify_instance_update(obj_idx, 0);
    if (canvas != nullptr)
        canvas->post_event(SimpleEvent(EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS));

    wxMessageBox(wxString::Format(_L("Painted %zu top facets with %zu colors.\n\n"
                                     "Detail is limited by the mesh triangle density - for finer results, "
                                     "subdivide/remesh the model before applying the photo."),
                                  painted, palette_hex.size()),
                 _L("Photo on top surface"), wxICON_INFORMATION, parent);
}


namespace {

// Perceived luminance 0..1 of a "#rrggbb" color.
double hex_luminance(const std::string &hex)
{
    auto d = [&](size_t i) {
        char c = hex[i];
        return double(c <= '9' ? c - '0' : (c | 32) - 'a' + 10);
    };
    if (hex.size() < 7 || hex[0] != '#')
        return 0.5;
    const double r = d(1) * 16 + d(2), g = d(3) * 16 + d(4), b = d(5) * 16 + d(6);
    return (0.299 * r + 0.587 * g + 0.114 * b) / 255.0;
}

struct SurfaceShadingOptions
{
    unsigned int shadow_id = 1;
    unsigned int highlight_id = 2;
    int steps = 6;
    int light_preset = 0;
    int contrast = 1;
};

static wxString filament_choice_label(size_t idx, const std::string &hex)
{
    return wxString::Format(_L("Filament %d  %s"), int(idx + 1), wxString::FromUTF8(hex.c_str()));
}

static bool show_surface_shading_options_dialog(wxWindow *parent,
                                                const std::vector<std::string> &filament_colors,
                                                SurfaceShadingOptions &options)
{
    wxDialog dlg(parent, wxID_ANY, _L("FullSpectrum surface shading"), wxDefaultPosition, wxDefaultSize,
                 wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    auto *root = new wxBoxSizer(wxVERTICAL);

    auto *grid = new wxFlexGridSizer(2, 8, 12);
    grid->AddGrowableCol(1, 1);

    auto *shadow_choice = new wxChoice(&dlg, wxID_ANY);
    auto *highlight_choice = new wxChoice(&dlg, wxID_ANY);
    for (size_t i = 0; i < filament_colors.size(); ++i) {
        const wxString label = filament_choice_label(i, filament_colors[i]);
        shadow_choice->Append(label);
        highlight_choice->Append(label);
    }

    size_t darkest = 0;
    size_t lightest = 0;
    for (size_t i = 1; i < filament_colors.size(); ++i) {
        if (hex_luminance(filament_colors[i]) < hex_luminance(filament_colors[darkest]))
            darkest = i;
        if (hex_luminance(filament_colors[i]) > hex_luminance(filament_colors[lightest]))
            lightest = i;
    }
    if (darkest == lightest && filament_colors.size() > 1)
        lightest = darkest == 0 ? 1 : 0;
    shadow_choice->SetSelection(int(darkest));
    highlight_choice->SetSelection(int(lightest));

    auto *steps_choice = new wxChoice(&dlg, wxID_ANY);
    const int max_steps = std::clamp(18 - int(filament_colors.size()), 2, 8);
    for (int steps = 2; steps <= max_steps; ++steps)
        steps_choice->Append(wxString::Format(_L("%d shade steps"), steps));
    steps_choice->SetSelection(std::min(4, max_steps - 2)); // Prefer 6 shade steps when slots allow it.

    auto *light_choice = new wxChoice(&dlg, wxID_ANY);
    light_choice->Append(_L("Front left"));
    light_choice->Append(_L("Front right"));
    light_choice->Append(_L("Back left"));
    light_choice->Append(_L("Back right"));
    light_choice->Append(_L("Left side"));
    light_choice->Append(_L("Right side"));
    light_choice->Append(_L("Overhead"));
    light_choice->Append(_L("Zenithal 45"));
    light_choice->SetSelection(0);

    auto *contrast_choice = new wxChoice(&dlg, wxID_ANY);
    contrast_choice->Append(_L("Soft"));
    contrast_choice->Append(_L("Normal"));
    contrast_choice->Append(_L("Hard"));
    contrast_choice->SetSelection(1);

    auto add_row = [&](const wxString &label, wxWindow *control) {
        auto *text = new wxStaticText(&dlg, wxID_ANY, label);
        grid->Add(text, 0, wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT);
        grid->Add(control, 1, wxEXPAND);
    };
    add_row(_L("Shadow"), shadow_choice);
    add_row(_L("Highlight"), highlight_choice);
    add_row(_L("Ramp"), steps_choice);
    add_row(_L("Light"), light_choice);
    add_row(_L("Contrast"), contrast_choice);

    root->Add(grid, 1, wxEXPAND | wxALL, 14);

    auto *buttons = new wxStdDialogButtonSizer();
    buttons->AddButton(new wxButton(&dlg, wxID_OK));
    buttons->AddButton(new wxButton(&dlg, wxID_CANCEL));
    buttons->Realize();
    root->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 14);

    dlg.SetSizerAndFit(root);
    dlg.SetMinSize(wxSize(360, dlg.GetSize().GetHeight()));
    if (dlg.ShowModal() != wxID_OK)
        return false;

    const int shadow = shadow_choice->GetSelection();
    const int highlight = highlight_choice->GetSelection();
    if (shadow < 0 || highlight < 0 || shadow == highlight)
        return false;
    options.shadow_id = unsigned(shadow + 1);
    options.highlight_id = unsigned(highlight + 1);
    options.steps = steps_choice->GetSelection() + 2;
    options.light_preset = light_choice->GetSelection();
    options.contrast = contrast_choice->GetSelection();
    return true;
}

static Vec3d light_direction_from_preset(int preset)
{
    Vec3d light_dir(-1.0, -1.0, 1.0);
    switch (preset) {
    case 1: light_dir = Vec3d( 1.0, -1.0, 1.0); break;
    case 2: light_dir = Vec3d(-1.0,  1.0, 1.0); break;
    case 3: light_dir = Vec3d( 1.0,  1.0, 1.0); break;
    case 4: light_dir = Vec3d(-1.0,  0.0, 0.2); break;
    case 5: light_dir = Vec3d( 1.0,  0.0, 0.2); break;
    case 6: light_dir = Vec3d( 0.0,  0.0, 1.0); break;
    case 7: light_dir = Vec3d( 0.0, -std::sqrt(0.5), std::sqrt(0.5)); break;
    default: break;
    }
    light_dir.normalize();
    return light_dir;
}

static double apply_contrast_curve(double value, int contrast)
{
    const double t = std::clamp(value, 0.0, 1.0);
    if (contrast == 0)
        return std::sqrt(t);
    if (contrast == 2)
        return std::pow(t, 1.45);
    return t;
}

static int find_simple_pair_palette_index(const MixedFilamentManager &manager,
                                          unsigned int shadow_id,
                                          unsigned int highlight_id,
                                          int highlight_percent,
                                          size_t physical_count)
{
    size_t enabled_index = 0;
    for (const MixedFilament &mf : manager.mixed_filaments()) {
        if (mf.deleted || !mf.enabled)
            continue;
        const bool simple_pair = mf.gradient_component_ids.empty() &&
                                 mf.manual_pattern.empty() &&
                                 mf.distribution_mode == int(MixedFilament::Simple);
        const bool forward = mf.component_a == shadow_id &&
                             mf.component_b == highlight_id &&
                             mf.mix_b_percent == highlight_percent;
        const bool reverse = mf.component_a == highlight_id &&
                             mf.component_b == shadow_id &&
                             mf.mix_b_percent == 100 - highlight_percent;
        if (simple_pair && (forward || reverse))
            return int(physical_count + enabled_index);
        ++enabled_index;
    }
    return -1;
}

static bool is_simple_pair_mix(const MixedFilament &mf,
                               unsigned int shadow_id,
                               unsigned int highlight_id,
                               int highlight_percent)
{
    const bool simple_pair = mf.gradient_component_ids.empty() &&
                             mf.manual_pattern.empty() &&
                             mf.distribution_mode == int(MixedFilament::Simple);
    const bool forward = mf.component_a == shadow_id &&
                         mf.component_b == highlight_id &&
                         mf.mix_b_percent == highlight_percent;
    const bool reverse = mf.component_a == highlight_id &&
                         mf.component_b == shadow_id &&
                         mf.mix_b_percent == 100 - highlight_percent;
    return simple_pair && (forward || reverse);
}

static void promote_shading_ramp_rows(MixedFilamentManager &manager,
                                      unsigned int shadow_id,
                                      unsigned int highlight_id,
                                      const std::vector<int> &highlight_percentages)
{
    std::vector<MixedFilament> &rows = manager.mixed_filaments();
    std::vector<bool> used(rows.size(), false);
    std::vector<MixedFilament> ordered;
    ordered.reserve(rows.size());

    for (const int pct : highlight_percentages) {
        for (size_t i = 0; i < rows.size(); ++i) {
            if (used[i] || rows[i].deleted || !rows[i].enabled)
                continue;
            if (!is_simple_pair_mix(rows[i], shadow_id, highlight_id, pct))
                continue;
            ordered.emplace_back(rows[i]);
            used[i] = true;
            break;
        }
    }

    for (size_t i = 0; i < rows.size(); ++i) {
        if (!used[i])
            ordered.emplace_back(rows[i]);
    }
    rows = std::move(ordered);
}

} // anonymous namespace

void apply_fullspectrum_surface_shading(wxWindow *parent)
{
    Plater *plater = wxGetApp().plater();
    if (plater == nullptr)
        return;
    GLCanvas3D *canvas = plater->get_current_canvas3D();
    const int obj_idx = canvas != nullptr ? canvas->get_selection().get_object_idx() : -1;
    if (obj_idx < 0 || obj_idx >= int(wxGetApp().model().objects.size())) {
        wxMessageBox(_L("Select a single object first."), _L("FullSpectrum surface shading"), wxICON_INFORMATION, parent);
        return;
    }
    ModelObject *mo = wxGetApp().model().objects[size_t(obj_idx)];
    if (mo->instances.empty())
        return;

    auto *pb = wxGetApp().preset_bundle;
    if (pb == nullptr)
        return;

    std::vector<std::string> filament_colors;
    if (auto *opt = pb->project_config.option<ConfigOptionStrings>("filament_colour"))
        filament_colors = opt->values;
    if (filament_colors.size() < 2) {
        wxMessageBox(_L("At least two physical filaments are needed."), _L("FullSpectrum surface shading"), wxICON_INFORMATION, parent);
        return;
    }

    SurfaceShadingOptions options;
    if (!show_surface_shading_options_dialog(parent, filament_colors, options))
        return;

    std::string serialized;
    if (auto *opt = pb->project_config.option<ConfigOptionString>("mixed_filament_definitions"))
        serialized = opt->value;
    pb->mixed_filaments.auto_generate(filament_colors);
    pb->mixed_filaments.load_custom_entries(serialized, filament_colors);
    const std::vector<MixedFilament> rows_before_ramp = pb->mixed_filaments.mixed_filaments();

    std::vector<int> ramp_palette_indices;
    ramp_palette_indices.reserve(size_t(options.steps));
    std::vector<int> ramp_highlight_percentages;
    bool added_mixes = false;
    for (int step = 0; step < options.steps; ++step) {
        if (step == 0) {
            ramp_palette_indices.emplace_back(int(options.shadow_id - 1));
            continue;
        }
        if (step == options.steps - 1) {
            ramp_palette_indices.emplace_back(int(options.highlight_id - 1));
            continue;
        }
        const int highlight_percent = int(std::lround(100.0 * double(step) / double(options.steps - 1)));
        ramp_highlight_percentages.emplace_back(highlight_percent);
        int palette_idx = find_simple_pair_palette_index(pb->mixed_filaments,
                                                         options.shadow_id,
                                                         options.highlight_id,
                                                         highlight_percent,
                                                         filament_colors.size());
        if (palette_idx < 0) {
            pb->mixed_filaments.add_custom_filament(options.shadow_id, options.highlight_id, highlight_percent, filament_colors);
            added_mixes = true;
            palette_idx = find_simple_pair_palette_index(pb->mixed_filaments,
                                                         options.shadow_id,
                                                         options.highlight_id,
                                                         highlight_percent,
                                                         filament_colors.size());
        }
        if (palette_idx < 0) {
            pb->mixed_filaments.mixed_filaments() = rows_before_ramp;
            return;
        }
        ramp_palette_indices.emplace_back(palette_idx);
    }

    promote_shading_ramp_rows(pb->mixed_filaments, options.shadow_id, options.highlight_id, ramp_highlight_percentages);
    ramp_palette_indices.clear();
    ramp_palette_indices.reserve(size_t(options.steps));
    for (int step = 0; step < options.steps; ++step) {
        if (step == 0) {
            ramp_palette_indices.emplace_back(int(options.shadow_id - 1));
            continue;
        }
        if (step == options.steps - 1) {
            ramp_palette_indices.emplace_back(int(options.highlight_id - 1));
            continue;
        }
        const int highlight_percent = int(std::lround(100.0 * double(step) / double(options.steps - 1)));
        const int palette_idx = find_simple_pair_palette_index(pb->mixed_filaments,
                                                               options.shadow_id,
                                                               options.highlight_id,
                                                               highlight_percent,
                                                               filament_colors.size());
        if (palette_idx < 0) {
            pb->mixed_filaments.mixed_filaments() = rows_before_ramp;
            return;
        }
        ramp_palette_indices.emplace_back(palette_idx);
    }

    for (int palette_idx : ramp_palette_indices) {
        if (palette_idx >= 16) {
            pb->mixed_filaments.mixed_filaments() = rows_before_ramp;
            wxMessageBox(_L("The generated shade ramp uses a paint color slot above 16. Remove or disable some existing mixed colors, or use fewer shade steps."),
                         _L("FullSpectrum surface shading"), wxICON_INFORMATION, parent);
            return;
        }
    }

    if (added_mixes || !ramp_highlight_percentages.empty()) {
        if (auto *opt = pb->project_config.option<ConfigOptionString>("mixed_filament_definitions", true))
            opt->value = pb->mixed_filaments.serialize_custom_entries();
        plater->on_config_change(pb->full_config());
        plater->schedule_background_process();
        if (canvas != nullptr)
            canvas->set_as_dirty();
    }

    const Vec3d light_dir = light_direction_from_preset(options.light_preset);
    auto ramp_index_for_value = [&](double value) {
        const double t = apply_contrast_curve(value, options.contrast);
        const size_t band = std::min(ramp_palette_indices.size() - 1,
                                     size_t(std::floor(t * double(ramp_palette_indices.size()))));
        return ramp_palette_indices[band];
    };

    const Transform3d inst_tr = mo->instances.front()->get_matrix();
    Plater::TakeSnapshot snapshot(plater, "FullSpectrum surface shading");

    size_t painted = 0;
    for (ModelVolume *mv : mo->volumes) {
        if (!mv->is_model_part())
            continue;
        const Transform3d tr = inst_tr * mv->get_matrix();
        const indexed_triangle_set &its = mv->mesh().its;
        TriangleSelector selector(mv->mesh());
        selector.deserialize(mv->mmu_segmentation_facets.get_data(), false);
        bool any = false;
        for (size_t i = 0; i < its.indices.size(); ++i) {
            const stl_triangle_vertex_indices &f = its.indices[i];
            const Vec3d a = tr * its.vertices[f(0)].cast<double>();
            const Vec3d b = tr * its.vertices[f(1)].cast<double>();
            const Vec3d c = tr * its.vertices[f(2)].cast<double>();
            Vec3d n = (b - a).cross(c - a);
            if (n.norm() <= 0.0)
                continue;
            n.normalize();
            const double brightness = std::max(0.0, n.dot(light_dir));
            const int idx = ramp_index_for_value(brightness);
            selector.set_facet(int(i), EnforcerBlockerType(idx + 1));
            any = true;
            ++painted;
        }
        if (any)
            mv->mmu_segmentation_facets.set(selector);
    }

    wxGetApp().obj_list()->update_info_items(size_t(obj_idx));
    plater->get_partplate_list().notify_instance_update(obj_idx, 0);
    if (canvas != nullptr)
            canvas->post_event(SimpleEvent(EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS));

    wxMessageBox(wxString::Format(_L("Painted %zu facets with a %d-step FullSpectrum shade ramp."),
                                  painted, int(ramp_palette_indices.size())),
                 _L("FullSpectrum surface shading"), wxICON_INFORMATION, parent);
}

void generate_hueforge_lithophane(wxWindow *parent)
{
    Plater *plater = wxGetApp().plater();
    if (plater == nullptr)
        return;

    // Physical filaments only (the stack is printed with real toolchanges),
    // darkest at the bottom like a classic filament painting stack.
    std::vector<std::string> colors;
    if (auto *pb = wxGetApp().preset_bundle)
        if (auto *opt = pb->project_config.option<ConfigOptionStrings>("filament_colour"))
            colors = opt->values;
    if (colors.size() < 2) {
        wxMessageBox(_L("At least two filaments are needed for a color lithophane."),
                     _L("Color lithophane"), wxICON_INFORMATION, parent);
        return;
    }
    std::vector<size_t> order(colors.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return hex_luminance(colors[a]) < hex_luminance(colors[b]);
    });
    if (order.size() > 4)
        order.resize(4); // classic stacks rarely benefit from more

    wxFileDialog dlg(parent, _L("Choose an image for the lithophane"), "", "",
                     "Images (*.png;*.jpg;*.jpeg;*.bmp)|*.png;*.jpg;*.jpeg;*.bmp",
                     wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal() != wxID_OK)
        return;
    wxImage image(dlg.GetPath());
    if (!image.IsOk() || image.GetWidth() <= 1 || image.GetHeight() <= 1) {
        wxMessageBox(_L("Could not load the image."), _L("Color lithophane"), wxICON_ERROR, parent);
        return;
    }

    // Fixed v1 geometry: 120 mm long edge, ~0.5 mm pixels, 0.6 mm base, 2.4 mm relief.
    constexpr double PLATE_LONG_EDGE = 120.0;
    constexpr double BASE_H          = 0.6;
    constexpr double RELIEF_H        = 2.4;
    const int    long_px  = std::max(image.GetWidth(), image.GetHeight());
    const int    grid_max = 240;
    const double scale_px = std::min(1.0, double(grid_max) / double(long_px));
    const int W = std::max(2, int(image.GetWidth()  * scale_px));
    const int H = std::max(2, int(image.GetHeight() * scale_px));
    const double cell = PLATE_LONG_EDGE / double(std::max(W, H));

    // Luminance -> height: light pixels are tall so they reach the light top filaments.
    auto height_at = [&](int gx, int gy) {
        const int px = std::min(image.GetWidth()  - 1, int(double(gx) / W * image.GetWidth()));
        const int py = std::min(image.GetHeight() - 1, int(double(gy) / H * image.GetHeight()));
        const double lum = (0.299 * image.GetRed(px, py) + 0.587 * image.GetGreen(px, py) +
                            0.114 * image.GetBlue(px, py)) / 255.0;
        return BASE_H + RELIEF_H * lum;
    };

    // Watertight heightmap mesh: top grid + flat bottom + side walls.
    indexed_triangle_set its;
    const int VW = W + 1, VH = H + 1;
    its.vertices.reserve(size_t(VW) * VH * 2);
    for (int gy = 0; gy < VH; ++gy)
        for (int gx = 0; gx < VW; ++gx)
            its.vertices.emplace_back(float(gx * cell), float((VH - 1 - gy) * cell), float(height_at(gx, gy)));
    const int bottom0 = VW * VH;
    for (int gy = 0; gy < VH; ++gy)
        for (int gx = 0; gx < VW; ++gx)
            its.vertices.emplace_back(float(gx * cell), float((VH - 1 - gy) * cell), 0.f);
    auto top = [&](int gx, int gy) { return gy * VW + gx; };
    auto bot = [&](int gx, int gy) { return bottom0 + gy * VW + gx; };
    for (int gy = 0; gy < H; ++gy)
        for (int gx = 0; gx < W; ++gx) {
            // top faces up (note: +y in model = -gy in grid, hence this winding)
            its.indices.emplace_back(top(gx, gy),     top(gx + 1, gy + 1), top(gx + 1, gy));
            its.indices.emplace_back(top(gx, gy),     top(gx, gy + 1),     top(gx + 1, gy + 1));
            // bottom faces down
            its.indices.emplace_back(bot(gx, gy),     bot(gx + 1, gy),     bot(gx + 1, gy + 1));
            its.indices.emplace_back(bot(gx, gy),     bot(gx + 1, gy + 1), bot(gx, gy + 1));
        }
    for (int gx = 0; gx < W; ++gx) {
        its.indices.emplace_back(top(gx, 0), top(gx + 1, 0), bot(gx + 1, 0));
        its.indices.emplace_back(top(gx, 0), bot(gx + 1, 0), bot(gx, 0));
        its.indices.emplace_back(top(gx + 1, H), top(gx, H), bot(gx, H));
        its.indices.emplace_back(top(gx + 1, H), bot(gx, H), bot(gx + 1, H));
    }
    for (int gy = 0; gy < H; ++gy) {
        its.indices.emplace_back(top(0, gy + 1), top(0, gy), bot(0, gy));
        its.indices.emplace_back(top(0, gy + 1), bot(0, gy), bot(0, gy + 1));
        its.indices.emplace_back(top(VW - 1, gy), top(VW - 1, gy + 1), bot(VW - 1, gy + 1));
        its.indices.emplace_back(top(VW - 1, gy), bot(VW - 1, gy + 1), bot(VW - 1, gy));
    }

    Plater::TakeSnapshot snapshot(plater, "Color lithophane");
    Model &model = wxGetApp().model();
    ModelObject *obj = model.add_object();
    obj->name = "Lithophane";
    obj->add_volume(TriangleMesh(std::move(its)));
    obj->add_instance();
    obj->ensure_on_bed();
    wxGetApp().obj_list()->add_object_to_list(model.objects.size() - 1);

    // Filament changes at even fractions of the relief, darkest first.
    auto &pcgc = model.plates_custom_gcodes[plater->get_partplate_list().get_curr_plate_index()];
    pcgc.mode = CustomGCode::MultiAsSingle;
    const size_t n = order.size();
    for (size_t k = 1; k < n; ++k) {
        CustomGCode::Item item;
        item.print_z  = BASE_H + RELIEF_H * double(k) / double(n);
        item.type     = CustomGCode::ToolChange;
        item.extruder = int(order[k]) + 1;
        pcgc.gcodes.push_back(item);
    }
    std::sort(pcgc.gcodes.begin(), pcgc.gcodes.end());

    plater->update();
    wxMessageBox(wxString::Format(_L("Lithophane created with a %zu-filament stack (dark to light).\n\n"
                                     "Start the print with filament %d (the darkest). Filament changes were "
                                     "placed automatically; fine-tune them on the layer slider in Preview."),
                                  n, int(order[0]) + 1),
                 _L("Color lithophane"), wxICON_INFORMATION, parent);
}

} } // namespace Slic3r::GUI
