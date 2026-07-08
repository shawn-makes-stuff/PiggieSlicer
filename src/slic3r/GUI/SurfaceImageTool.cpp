#include "SurfaceImageTool.hpp"

#include <algorithm>
#include <cstdio>
#include <limits>
#include <unordered_map>
#include <vector>

#include <wx/filedlg.h>
#include <wx/image.h>
#include <wx/msgdlg.h>
#include <wx/window.h>

#include "libslic3r/FilamentColorMix.hpp"
#include "libslic3r/CustomGCode.hpp"
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

} // anonymous namespace

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
    wxMessageBox(wxString::Format(_L("Lithophane created with a %zu-filament stack (dark to light).

"
                                     "Start the print with filament %d (the darkest). Filament changes were "
                                     "placed automatically; fine-tune them on the layer slider in Preview."),
                                  n, int(order[0]) + 1),
                 _L("Color lithophane"), wxICON_INFORMATION, parent);
}

} } // namespace Slic3r::GUI
