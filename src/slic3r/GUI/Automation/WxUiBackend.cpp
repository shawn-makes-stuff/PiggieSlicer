#include "WxUiBackend.hpp"
#include "AutomationRegistry.hpp"
#include "ImGuiItemTable.hpp"
#include "JsonRpcDispatcher.hpp"   // for kErrGuiBusy and friends
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "libslic3r/Model.hpp"

#include <wx/window.h>
#include <wx/toplevel.h>
#include <wx/dialog.h>     // wxDialog::IsModal
#include <wx/glcanvas.h>   // wxGLCanvas -> wxWindow* conversion
#include <wx/textctrl.h>   // wxTextEntry
#include <wx/choice.h>
#include <wx/checkbox.h>

#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <type_traits>

namespace Slic3r { namespace GUI { namespace Automation {

// Run `fn` on the GUI thread, block until it returns or the timeout elapses.
// Throws AutomationError(kErrGuiBusy) on timeout. std::promise is move-only, so we
// hold it via shared_ptr to satisfy CallAfter's copyable-functor requirement.
template <class Fn>
static auto run_on_gui(int timeout_ms, Fn&& fn) -> decltype(fn()) {
    using R = decltype(fn());
    auto prom = std::make_shared<std::promise<R>>();
    auto fut  = prom->get_future();
    wxGetApp().CallAfter([prom, fn = std::forward<Fn>(fn)]() mutable {
        try {
            if constexpr (std::is_void_v<R>) { fn(); prom->set_value(); }
            else { prom->set_value(fn()); }
        } catch (...) { prom->set_exception(std::current_exception()); }
    });
    if (fut.wait_for(std::chrono::milliseconds(timeout_ms)) != std::future_status::ready)
        throw AutomationError(kErrGuiBusy, "GUI thread timed out");
    return fut.get();
}

namespace {
std::string wx_class_name(const wxWindow* w) {
    const wxClassInfo* ci = w->GetClassInfo();
    std::string name = ci ? std::string(wxString(ci->GetClassName()).ToUTF8()) : "wxWindow";
    if (name.rfind("wx", 0) == 0 && name.size() > 2) name = name.substr(2);
    return name;
}

std::string wx_value_of(wxWindow* w, bool& has_value) {
    has_value = false;
    if (auto* tc = dynamic_cast<wxTextEntry*>(w))   { has_value = true; return std::string(tc->GetValue().ToUTF8()); }
    if (auto* ch = dynamic_cast<wxChoice*>(w))      { has_value = true; return std::string(ch->GetStringSelection().ToUTF8()); }
    if (auto* cb = dynamic_cast<wxCheckBox*>(w))    { has_value = true; return cb->GetValue() ? "true" : "false"; }
    return {};
}

void build_node(wxWindow* w, UiNode& node, const std::string& parent_path,
                int sibling_index, const DumpOptions& opts, int depth) {
    node.backend = BackendKind::Wx;
    node.klass   = wx_class_name(w);
    node.id      = automation_id_of(w);
    node.path    = parent_path.empty()
                       ? node.klass
                       : parent_path + "/" + node.klass + "[" + std::to_string(sibling_index) + "]";
    node.label   = std::string(w->GetLabel().ToUTF8());
    node.enabled = w->IsEnabled();
    node.visible = w->IsShownOnScreen();
    node.value   = wx_value_of(w, node.has_value);
    node.handle  = reinterpret_cast<std::uint64_t>(w);
    const wxRect r = w->GetScreenRect();
    node.rect = { r.x, r.y, r.width, r.height };

    if (opts.max_depth >= 0 && depth >= opts.max_depth) return;
    int idx = 0;
    for (wxWindow* child : w->GetChildren()) {
        if (opts.visible_only && !child->IsShownOnScreen()) { ++idx; continue; }
        UiNode cn;
        build_node(child, cn, node.path, idx, opts, depth + 1);
        node.children.push_back(std::move(cn));
        ++idx;
    }
}

// Map recorded ImGui items (display coords) to screen coords using the 3D canvas
// client origin + DPI scale, then append them as flat children under the root.
void append_imgui_nodes(UiNode& root) {
    Plater* plater = wxGetApp().plater();
    if (plater == nullptr) return;
    GLCanvas3D* canvas3d = plater->get_current_canvas3D();
    if (canvas3d == nullptr) return;
    wxWindow* canvas = canvas3d->get_wxglcanvas();
    if (canvas == nullptr) return;
    const wxPoint origin = canvas->ClientToScreen(wxPoint(0, 0));
    double scale = canvas->GetContentScaleFactor();
    if (scale <= 0.0) scale = 1.0;
    const auto frame = ImGuiItemTable::instance().snapshot();
    for (const auto& it : frame.items) {
        UiNode n;
        n.backend   = BackendKind::ImGui;
        n.klass     = it.type;
        n.label     = it.label;
        n.path      = "ImGui/" + it.window_name + "/" + it.label;
        n.id        = n.path; // imgui items use their path as id in v1
        n.enabled   = it.enabled;
        n.visible   = true;
        n.has_value = it.has_value;
        n.value     = it.value;
        n.rect = { origin.x + int(it.x / scale), origin.y + int(it.y / scale),
                   int(it.w / scale), int(it.h / scale) };
        root.children.push_back(std::move(n));
    }
}
} // namespace

void WxUiBackend::refresh_ui() {
    run_on_gui(m_gui_timeout_ms, [] {
        // Force a fresh ImGui frame so transient items are recorded, then flush
        // pending events so the latest frame is the one we read.
        if (Plater* p = wxGetApp().plater()) {
            if (GLCanvas3D* canvas = p->get_current_canvas3D()) {
                canvas->set_as_dirty();
                canvas->render();
            }
        }
        wxGetApp().Yield();
    });
}

UiNode WxUiBackend::dump_tree(const DumpOptions& opts) {
    return run_on_gui(m_gui_timeout_ms, [&opts]() -> UiNode {
        wxWindow* root_win = nullptr;
        if (opts.root) root_win = window_for_automation_id(*opts.root);
        if (root_win == nullptr)
            root_win = static_cast<wxWindow*>(wxGetApp().mainframe);
        UiNode root;
        if (root_win) build_node(root_win, root, {}, 0, opts, 0);
        if (opts.include_imgui) append_imgui_nodes(root);
        return root;
    });
}

AppState WxUiBackend::app_state() {
    return run_on_gui(m_gui_timeout_ms, []() -> AppState {
        AppState s;
        MainFrame* mf = wxGetApp().mainframe;
        Plater*    p  = wxGetApp().plater();
        if (mf) {
            // best-effort: MainFrame has no public getter for the selected top tab
            // (m_tabpanel is private), so report the frame title for now.
            s.active_tab = std::string(mf->GetTitle().ToUTF8());
            s.foreground = mf->IsActive();
        }
        if (p) {
            s.project_loaded = !p->model().objects.empty();
            s.slicing        = p->is_background_process_slicing();
        }
        if (wxWindow* top = wxGetActiveWindow())
            if (auto* dlg = dynamic_cast<wxDialog*>(top))
                if (dlg != static_cast<wxWindow*>(mf) && dlg->IsModal())
                    s.modal_dialog = std::string(dlg->GetTitle().ToUTF8());
        return s;
    });
}

// click/type/keys/screenshots implemented in Task 16.

}}} // namespace Slic3r::GUI::Automation
