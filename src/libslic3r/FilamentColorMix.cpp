// See FilamentColorMix.hpp — model constants from prusa-fdm-mixer (MIT, Prusa Research).

#include "FilamentColorMix.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Slic3r {

namespace ColorMix {

namespace {

constexpr double PI = 3.14159265358979323846;

struct RGBd { double r, g, b; };          // 0..255
struct LABd { double L, a, b; };

double srgb_to_linear(double c)
{
    c /= 255.0;
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

double linear_to_srgb(double c)
{
    c = std::clamp(c, 0.0, 1.0);
    return (c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055) * 255.0;
}

RGBd hex_to_rgb(const std::string &hex)
{
    auto digit = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    size_t off = (!hex.empty() && hex[0] == '#') ? 1 : 0;
    if (hex.size() - off < 6)
        return {0.0, 0.0, 0.0};
    double ch[3];
    for (int i = 0; i < 3; ++i) {
        int hi = digit(hex[off + 2 * i]), lo = digit(hex[off + 2 * i + 1]);
        ch[i] = (hi < 0 || lo < 0) ? 0.0 : double(hi * 16 + lo);
    }
    return {ch[0], ch[1], ch[2]};
}

std::string rgb_to_hex(const RGBd &c)
{
    auto b = [](double v) { return int(std::clamp(std::lround(v), 0l, 255l)); };
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", b(c.r), b(c.g), b(c.b));
    return buf;
}

LABd rgb_to_lab(const RGBd &c)
{
    const double rl = srgb_to_linear(c.r), gl = srgb_to_linear(c.g), bl = srgb_to_linear(c.b);
    const double x = (rl * 0.4124564 + gl * 0.3575761 + bl * 0.1804375) * 100.0 / 95.047;
    const double y = (rl * 0.2126729 + gl * 0.7151522 + bl * 0.0721750);
    const double z = (rl * 0.0193339 + gl * 0.1191920 + bl * 0.9503041) * 100.0 / 108.883;
    auto f = [](double t) { return t > 0.008856 ? std::cbrt(t) : 7.787 * t + 16.0 / 116.0; };
    const double fx = f(x), fy = f(y), fz = f(z);
    return {116.0 * fy - 16.0, 500.0 * (fx - fy), 200.0 * (fy - fz)};
}

RGBd lab_to_rgb(const LABd &lab)
{
    const double fy = (lab.L + 16.0) / 116.0;
    const double fx = lab.a / 500.0 + fy;
    const double fz = fy - lab.b / 200.0;
    auto finv = [](double t) { return t > 0.206893 ? t * t * t : (t - 16.0 / 116.0) / 7.787; };
    const double x = finv(fx) * 95.047 / 100.0;
    const double y = finv(fy);
    const double z = finv(fz) * 108.883 / 100.0;
    return {linear_to_srgb(x *  3.2404542 + y * -1.5371385 + z * -0.4985314),
            linear_to_srgb(x * -0.9692660 + y *  1.8760108 + z *  0.0415560),
            linear_to_srgb(x *  0.0556434 + y * -0.2040259 + z *  1.0572252)};
}

// Model constants, fitted against measured prints (prusa-fdm-mixer).
constexpr double YN_N          = 3.0;    // Yule-Nielsen exponent
constexpr double L_SLOPE       = -0.0477;
constexpr double L_INTERCEPT   = -2.112;
constexpr double L_KNEE        = 15.0;
constexpr double L_KNEE_SLOPE  = -0.060;
constexpr double C_SLOPE       = 0.2780;
constexpr double C_INTERCEPT   = -15.580;
constexpr double PEAK_STRENGTH = 1.375;
constexpr double HUE_PEAK_DEG  = 10.38;
constexpr double HUE_CENTER    = 210.0;
constexpr double HUE_HALFWIDTH = 30.0;

LABd predict_lab(const std::vector<Component> &parts)
{
    // Gradient safety: a (nearly) pure component is exactly its own color.
    for (const Component &p : parts)
        if (p.ratio >= 0.9999)
            return rgb_to_lab(hex_to_rgb(p.hex));

    // Yule-Nielsen base: linear-light channels to the 1/n, ratio-averaged, back to n.
    double acc[3] = {0.0, 0.0, 0.0};
    double l_min = 1e9, l_max = -1e9;
    for (const Component &p : parts) {
        const RGBd rgb = hex_to_rgb(p.hex);
        acc[0] += std::pow(srgb_to_linear(rgb.r), 1.0 / YN_N) * p.ratio;
        acc[1] += std::pow(srgb_to_linear(rgb.g), 1.0 / YN_N) * p.ratio;
        acc[2] += std::pow(srgb_to_linear(rgb.b), 1.0 / YN_N) * p.ratio;
        const double L = rgb_to_lab(rgb).L;
        l_min = std::min(l_min, L);
        l_max = std::max(l_max, L);
    }
    const RGBd base_rgb = {linear_to_srgb(std::pow(std::max(0.0, acc[0]), YN_N)),
                           linear_to_srgb(std::pow(std::max(0.0, acc[1]), YN_N)),
                           linear_to_srgb(std::pow(std::max(0.0, acc[2]), YN_N))};
    LABd out = rgb_to_lab(base_rgb);

    // Bell-curve weight: N^N * prod(ratios) — 1 at even mixes, 0 at endpoints.
    double w = 1.0;
    for (const Component &p : parts)
        w *= std::max(0.0, p.ratio);
    w = std::clamp(w * std::pow(double(parts.size()), double(parts.size())), 0.0, 1.0);
    const double cs = w * PEAK_STRENGTH;

    // Lightness: layer-stack shadows darken mixes, more so across large L gaps.
    const double gap = l_max - l_min;
    double dL = L_SLOPE * gap + L_INTERCEPT;
    if (gap > L_KNEE)
        dL += L_KNEE_SLOPE * (gap - L_KNEE);
    out.L += dL * cs;

    // Chroma: saturated hues desaturate faster as prints darken.
    const double C = std::hypot(out.a, out.b);
    if (C >= 0.01) {
        const double scale = std::max(0.0, C + (C_SLOPE * out.L + C_INTERCEPT) * cs) / C;
        out.a *= scale;
        out.b *= scale;
    }

    // Cyan band drifts warm around hue 210° with linear falloff.
    const double C2 = std::hypot(out.a, out.b);
    if (C2 >= 1.0) {
        double h = std::atan2(out.b, out.a) * 180.0 / PI;
        if (h < 0.0) h += 360.0;
        const double dist = std::abs(h - HUE_CENTER);
        if (dist < HUE_HALFWIDTH) {
            const double h2 = (h + HUE_PEAK_DEG * (1.0 - dist / HUE_HALFWIDTH) * w) * PI / 180.0;
            out.a = C2 * std::cos(h2);
            out.b = C2 * std::sin(h2);
        }
    }
    return out;
}

double delta_e_2000(const LABd &x, const LABd &y)
{
    const double C1 = std::hypot(x.a, x.b), C2 = std::hypot(y.a, y.b);
    const double avg_C = (C1 + C2) / 2.0;
    const double pow25_7 = std::pow(25.0, 7.0);
    const double G = 0.5 * (1.0 - std::sqrt(std::pow(avg_C, 7.0) / (std::pow(avg_C, 7.0) + pow25_7)));
    const double a1p = x.a * (1.0 + G), a2p = y.a * (1.0 + G);
    const double C1p = std::hypot(a1p, x.b), C2p = std::hypot(a2p, y.b);
    auto hue = [](double a, double b) {
        double h = std::atan2(b, a) * 180.0 / PI;
        return h < 0.0 ? h + 360.0 : h;
    };
    const double h1 = hue(a1p, x.b), h2 = hue(a2p, y.b);

    double dh = 0.0;
    if (C1p * C2p != 0.0) {
        dh = h2 - h1;
        if (dh > 180.0) dh -= 360.0;
        else if (dh < -180.0) dh += 360.0;
    }
    const double dH = 2.0 * std::sqrt(C1p * C2p) * std::sin(dh * PI / 360.0);

    double avg_h;
    if (C1p * C2p == 0.0) avg_h = h1 + h2;
    else if (std::abs(h1 - h2) <= 180.0) avg_h = (h1 + h2) / 2.0;
    else avg_h = (h1 + h2 + (h1 + h2 < 360.0 ? 360.0 : -360.0)) / 2.0;

    const double T = 1.0 - 0.17 * std::cos((avg_h - 30.0) * PI / 180.0)
                         + 0.24 * std::cos((2.0 * avg_h) * PI / 180.0)
                         + 0.32 * std::cos((3.0 * avg_h + 6.0) * PI / 180.0)
                         - 0.20 * std::cos((4.0 * avg_h - 63.0) * PI / 180.0);
    const double avg_L = (x.L + y.L) / 2.0, avg_Cp = (C1p + C2p) / 2.0;
    const double SL = 1.0 + 0.015 * std::pow(avg_L - 50.0, 2.0) / std::sqrt(20.0 + std::pow(avg_L - 50.0, 2.0));
    const double SC = 1.0 + 0.045 * avg_Cp;
    const double SH = 1.0 + 0.015 * avg_Cp * T;
    const double d_theta = 30.0 * std::exp(-std::pow((avg_h - 275.0) / 25.0, 2.0));
    const double RC = 2.0 * std::sqrt(std::pow(avg_Cp, 7.0) / (std::pow(avg_Cp, 7.0) + pow25_7));
    const double RT = -RC * std::sin(2.0 * d_theta * PI / 180.0);
    const double dL = (y.L - x.L) / SL, dC = (C2p - C1p) / SC, dHs = dH / SH;
    return std::sqrt(dL * dL + dC * dC + dHs * dHs + RT * dC * dHs);
}

} // namespace

std::string predict_hex(const std::vector<Component> &components)
{
    if (components.empty())
        return "#000000";
    if (components.size() == 1)
        return rgb_to_hex(hex_to_rgb(components.front().hex));
    return rgb_to_hex(lab_to_rgb(predict_lab(components)));
}

double delta_e(const std::string &hex_a, const std::string &hex_b)
{
    return delta_e_2000(rgb_to_lab(hex_to_rgb(hex_a)), rgb_to_lab(hex_to_rgb(hex_b)));
}

Recipe best_two_filament_recipe(const std::string &target_hex, const std::vector<std::string> &filament_hexes)
{
    Recipe best;
    const LABd target = rgb_to_lab(hex_to_rgb(target_hex));

    auto consider = [&](size_t a, size_t b, int pct_b) {
        const std::string hex = pct_b == 0
            ? filament_hexes[a]
            : predict_hex({{filament_hexes[a], (100 - pct_b) / 100.0}, {filament_hexes[b], pct_b / 100.0}});
        const double de = delta_e_2000(rgb_to_lab(hex_to_rgb(hex)), target);
        if (de < best.delta_e)
            best = {a, b, pct_b, de, hex};
    };

    for (size_t a = 0; a < filament_hexes.size(); ++a) {
        consider(a, a, 0); // plain single filament
        for (size_t b = a + 1; b < filament_hexes.size(); ++b)
            for (int pct = 5; pct <= 95; pct += 5)
                consider(a, b, pct);
    }
    return best;
}

} // namespace ColorMix

} // namespace Slic3r
