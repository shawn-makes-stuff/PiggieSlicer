#include "SplashScreen.hpp"
#include "MainFrame.hpp"

#include <Slic3r/App/WX/BitmapCache.hpp>
#include <Slic3r/App/WX/WidgetsConfig.hpp>
#include "Slic3r/App/WX/StringConversions.hpp"
#include "Slic3r/App/WX/BitmapGetters.hpp"
#include <Slic3r/App/WX/I18N.hpp>

#include "Slic3r/Directories.hpp"

#include "Slic3r/Version.hpp"

#include <wx/font.h>
#include <wx/dcmemory.h>
#include <wx/dcclient.h>
#include <wx/display.h>
#include <wx/fontutil.h>

#include <algorithm>

namespace Slic3r::App::Desktop {

namespace {
// Helper function to determine the scale factor of the display where the splash screen will appear.
// This needs to be a free function so it can be called before the constructor's initializer list.
float get_display_scale_factor(const wxPoint& pos)
{
    int display_idx = wxDisplay::GetFromPoint(pos == wxDefaultPosition ? wxGetMousePosition() : pos);
    wxDisplay display(display_idx != wxNOT_FOUND ? display_idx : 0);
    double scale = display.GetScaleFactor();

    // Ensure scale is at least 1.0 as a fallback.
    if (scale < 1.0) {
        scale = 1.0;
    }
    return scale;
}

// A plain card in piggie grey (#292929) with a thin border, scaled for the target display; the
// logo and the texts are drawn on it by SplashScreen::Decorate.
static wxBitmap make_bitmap(bool is_editor, double scale)
{
    // half the old splash's width (900 x 540: the picture and the banner beside it)
    const wxSize logical_size(450, 540);
    const int border{1};

    wxSize scaled_size(lround(logical_size.GetWidth() * scale), lround(logical_size.GetHeight() * scale));
    scaled_size.IncBy(2 * border);

    wxImage image(scaled_size);
    if (!image.IsOk()) return wxBitmap();

    wxRect rc(scaled_size);
    image.SetRGB(rc, 100, 100, 100);
    rc.Deflate(border);
    image.SetRGB(rc, 41, 41, 41); // piggie grey

    wxBitmap bitmap(image);

#ifndef __WXMSW__
    // This tells wxWidgets that this large bitmap should occupy a smaller logical area.
    if (bitmap.IsOk()) {
        bitmap.SetScaleFactor(scale);
    }
#endif

    return bitmap;
}

} // namespace

SplashScreen::SplashScreen(bool is_editor, wxPoint pos) :
    // The base class constructor is called here. We generate the scaled, but undecorated,
    // bitmap by calling our static helper functions directly in the initializer list.
    wxSplashScreen(make_bitmap(is_editor, get_display_scale_factor(pos)),
    wxSPLASH_CENTRE_ON_SCREEN | wxSPLASH_NO_TIMEOUT,
    0, nullptr, wxID_ANY, pos, wxDefaultSize,
    #ifdef NDEBUG // splashcreen on top is annoying in debug mode, when the app starts slowly
        wxSTAY_ON_TOP |
    #endif
    wxNO_BORDER | wxFRAME_NO_TASKBAR),
    m_is_editor(is_editor),
    m_text_color(wxColour(255, 255, 255)),
    m_highlighted_text_color(wxColour(208, 118, 130)),
    m_scale(get_display_scale_factor(pos)) // Initialize member variable for scale factor.
{
#ifndef __WXMSW__
    // For non-Windows platforms, we need to decrease m_scale based on the content scale factor
    m_scale /= this->GetContentScaleFactor();
#endif
    // The base class now has the undecorated bitmap.
    // We get a reference to it, decorate it, and then set it back.
    m_main_bitmap = m_window->GetBitmap();
    if (!m_main_bitmap.IsOk()) {
        return;
    }

    // By default, the splash screen is always shown on the main display,
    // so we need to set its position manually.
    this->SetPosition(pos);
    // The splash screen size may change after being moved to another display,
    // so update it based on the bitmap size.
    this->SetClientSize(m_main_bitmap.GetLogicalWidth(), m_main_bitmap.GetLogicalHeight());
    this->CenterOnScreen();

    // Initialize constant texts and scale fonts using the determined scale factor.
    init_constant_text();

    // This font will be used for the action string.
    m_action_font = m_constant_text.credits_font.Bold();

    // Draw the logo and constant info text onto our hi-res bitmap.
    Decorate(m_main_bitmap);

    // Update the splash screen window with the newly decorated bitmap.
    set_bitmap(m_main_bitmap);
}

void SplashScreen::SetText(const wxString& text)
{
    // It crashes here when the splash is about to be hidden but the SetText is called
    // The m_main_bitmap is already in invalid state, so prevent setting text in such case
    if (!m_main_bitmap.IsOk())
        return;
    // This method creates a temporary copy of the main bitmap to draw on.
    wxBitmap bitmap(m_main_bitmap);
    if (!text.empty()) {
        wxMemoryDC memDC;
        memDC.SelectObject(bitmap);

        memDC.SetFont(m_action_font);
        memDC.SetTextForeground(m_highlighted_text_color);

        // Draw text at a center of the remained banner place.
        memDC.DrawLabel(text, m_state_text_rect, wxALIGN_CENTER_HORIZONTAL);

        memDC.SelectObject(wxNullBitmap);
    }
    // Update the splash screen with the (potentially modified) bitmap.
    set_bitmap(bitmap);

#ifdef __WXOSX__
    // This yield helps ensure the splash screen updates promptly on macOS.
    wxYield();
#endif
}

void SplashScreen::Decorate(wxBitmap& bmp)
{
    if (!bmp.IsOk())
        return;

    const int width  = bmp.GetLogicalWidth();
    const int height = bmp.GetLogicalHeight();
    const wxCoord margin = lround(24 * m_scale);

    // load bitmap for logo
    WX::BitmapCache bmp_cache;
    const int logo_size = lround(width * 0.25); // the old banner's logo was 74 px
    wxBitmap* logo_bmp_ptr = bmp_cache.load_svg("PiggieSlicer", logo_size, logo_size);
    if (logo_bmp_ptr == nullptr)
        return;

    // use a memory DC to draw directly onto the bitmap
    wxMemoryDC memDc(bmp);

    // logo
    int y = 2 * margin;
    memDc.DrawBitmap(*logo_bmp_ptr, (width - logo_size) / 2, y, true);
    y += logo_size + margin;

    // title: "Piggie" white, "Slicer" pink, centred together
    const wxString strPiggie = WX::from_u8("Piggie");
    const wxString strSlicer = WX::from_u8("Slicer");
    memDc.SetFont(m_constant_text.title_font);
    const int widthPiggie   = memDc.GetTextExtent(strPiggie).GetWidth();
    const int widthCombined = memDc.GetTextExtent(strPiggie + strSlicer).GetWidth();
    const int title_x       = (width - widthCombined) / 2;
    memDc.SetTextForeground(m_text_color);
    memDc.DrawText(strPiggie, title_x, y);
    memDc.SetTextForeground(m_highlighted_text_color);
    memDc.DrawText(strSlicer, title_x + widthPiggie, y);
    memDc.SetTextForeground(m_text_color);
    y += memDc.GetTextExtent(m_constant_text.title).GetY() + margin / 4;

    // version
    memDc.SetFont(m_constant_text.version_font);
    const wxRect version_rect(0, y, width, height - y);
    memDc.DrawLabel(m_constant_text.version, version_rect, wxALIGN_TOP | wxALIGN_CENTER_HORIZONTAL);
    y += memDc.GetTextExtent(m_constant_text.version).GetY();

    // credits at the bottom
    memDc.SetFont(m_constant_text.credits_font);
    const wxRect credits_rect(margin, y, width - 2 * margin, height - y - 2 * margin);
    memDc.DrawLabel(m_constant_text.credits, credits_rect, wxALIGN_BOTTOM | wxALIGN_CENTER_HORIZONTAL);
    const int credits_top = credits_rect.GetBottom() - memDc.GetMultiLineTextExtent(m_constant_text.credits).GetY();

    // the application state (SetText) in the space between the version and the credits
    m_state_text_rect = wxRect(margin, y + margin / 2, width - 2 * margin, std::max(0, credits_top - y - margin));
}

void SplashScreen::init_constant_text()
{
    // The text banner width in logical pixels.
    int text_banner_width = lround(0.55 * m_main_bitmap.GetLogicalWidth());

    m_constant_text.init(WX::w_config()->normal_font(), m_is_editor, text_banner_width);
}

void SplashScreen::set_bitmap(wxBitmap& bmp)
{
    m_window->SetBitmap(bmp);
    m_window->Refresh();
    m_window->Update();
}

void SplashScreen::ConstantText::init(const wxFont& init_font, bool is_editor, int text_banner_width)
{
    using namespace WX;

    // title
    title = from_u8(SLIC3R_APP_NAME); // is_editor ? SLIC3R_APP_NAME : GCODEVIEWER_APP_NAME;

    // dynamically get the version to display
    // TRN: {} is a placeholder for version number
    version = from_u8(fmt::format(fmt::runtime(Biz::_u8L("Version {}")), SLIC3R_APP_VERSION_DISPLAY));

    // credits information
    std::string credits_str = fmt::format(
        "{}\n\n{}",
        fmt::format(
            fmt::runtime(
                // TRN: {} is a placeholder for the application name (PiggieSlicer).
                // Please preserve the "\n" newline characters to keep the line breaks of this phrase.
                Biz::_u8L(
                    "{} is built on PrusaSlicer,\n"
                    "which is based on Slic3r\n"
                    "by Alessandro Ranellucci\n"
                    "and the RepRap community."
                )
            ),
            SLIC3R_APP_NAME
        ),
        Biz::_u8L("Licensed under GNU AGPLv3.")
    );
    credits = from_u8(credits_str);

    // Use a temporary DC to measure text extents with the correct fonts.
    wxMemoryDC memDC;
    memDC.SetFont(init_font.Bold());
    int title_width        = memDC.GetTextExtent(title).GetX();
    float title_font_scale = static_cast<float>(text_banner_width) / title_width;
    title_font             = init_font.Scaled(title_font_scale);

    credits_font = version_font = title_font.Scaled(0.35f);
    title_font = title_font.Bold();
}

} // namespace Slic3r::App::Desktop
