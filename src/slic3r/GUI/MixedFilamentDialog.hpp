#ifndef slic3r_GUI_MixedFilamentDialog_hpp_
#define slic3r_GUI_MixedFilamentDialog_hpp_

// PiggieSlicer / FullSpectrum: a self-contained dialog to define "virtual mixed
// filaments" — new colors created by alternating layers between two physical
// filaments (on a single-nozzle ACE printer, each alternation is a tool change +
// purge). The blended preview color is computed by the FilamentMixer engine. Rows
// are persisted into the project setting "mixed_filament_definitions" and rebuilt
// at slice time by Print::apply.

#include <vector>
#include <string>
#include <wx/dialog.h>

class wxScrolledWindow;
class wxBoxSizer;
class wxChoice;
class wxSlider;
class wxStaticText;
class Button;

namespace Slic3r { namespace GUI {

class MixedFilamentDialog : public wxDialog
{
public:
    explicit MixedFilamentDialog(wxWindow* parent);
    ~MixedFilamentDialog() override = default;

private:
    std::vector<std::string> physical_colors() const;        // current per-extruder colors
    void rebuild_rows();                                     // redraw the mixed list
    void persist();                                          // serialize -> project_config + dirty
    void on_add(wxCommandEvent&);
    void on_remove_custom(size_t mixed_index);
    void on_edit(size_t mixed_index);
    void on_preview_changed();                               // recompute the add-row preview swatch

    wxScrolledWindow* m_list      = nullptr;   // scrollable list of mixed rows
    wxBoxSizer*       m_list_sizer = nullptr;
    // "Add" controls
    wxChoice*         m_choice_a   = nullptr;
    wxChoice*         m_choice_b   = nullptr;
    wxSlider*         m_slider_mix = nullptr;
    wxStaticText*     m_lbl_mix    = nullptr;
    wxWindow*         m_preview_sw = nullptr;  // preview swatch (StaticBox)
    Button*           m_add_btn    = nullptr;
    size_t            m_editing_mixed_index = size_t(-1);
};

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_MixedFilamentDialog_hpp_
