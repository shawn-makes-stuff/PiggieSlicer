// Ported from OrcaSlicer v2.5.0-alpha (src/libslic3r/FlushVolCalc.cpp, src/libslic3r/FlushVolPredictor.cpp,
// src/slic3r/Utils/ColorSpaceConvert.cpp, resources/flush/flush_data_standard.txt), which is based on
// BambuStudio. Copyright (c) the OrcaSlicer and BambuStudio authors, AGPL-3.0.
//
// Only the standard nozzle data set (Orca's nozzle_flush_dataset = 0, the default for every
// single-nozzle printer) is ported; the dual-nozzle data sets of Bambu H2D printers are not.

#include "libslic3r/GCode/FlushVolCalc.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <unordered_map>

namespace Slic3r::FlushVolCalc {

const int g_max_flush_volume = 20000; // Orca: increase limit to 20k vs 900 in upstream BBS code.

namespace {

constexpr double PI = 3.14159265358979323846;

// ---- FlushVolPredictor.cpp (namespace FlushPredict) ----

struct RGBColor
{
    unsigned char r{0};
    unsigned char g{0};
    unsigned char b{0};
};

struct LABColor
{
    double l{0};
    double a{0};
    double b{0};
};

double deg_to_rad(double deg) { return deg * PI / 180.0; }

LABColor RGB2LAB(const RGBColor& color)
{
    auto gamma = [](double x) {
        if (x > 0.04045)
            return std::pow((x + 0.055) / 1.055, 2.4);
        else
            return x / 12.92;
    };
    double R = gamma(static_cast<double>(color.r) / 255.0) * 100;
    double G = gamma(static_cast<double>(color.g) / 255.0) * 100;
    double B = gamma(static_cast<double>(color.b) / 255.0) * 100;

    double x = 0.412453 * R + 0.357580 * G + 0.180423 * B;
    double y = 0.212671 * R + 0.715160 * G + 0.072169 * B;
    double z = 0.019334 * R + 0.119193 * G + 0.950227 * B;

    static const double XN = 95.0489;
    static const double YN = 100;
    static const double ZN = 108.8840;

    auto f = [](double t) {
        static const double threshold = 0.008856f;
        if (t > threshold)
            return std::pow(t, 1.0 / 3.0);
        else
            return 7.787 * t + 0.137931;
    };

    double xn = f(x / XN);
    double yn = f(y / YN);
    double zn = f(z / ZN);

    return LABColor{116.0 * yn - 16.0, 500.0 * (xn - yn), 200.0 * (yn - zn)};
}

// DeltaE2000
float calc_color_distance(const LABColor& lab1, const LABColor& lab2)
{
    static const double pow_25_to_7 = std::pow(25, 7);

    const double C1 = std::sqrt(lab1.a * lab1.a + lab1.b * lab1.b);
    const double C2 = std::sqrt(lab2.a * lab2.a + lab2.b * lab2.b);
    const double CMean = (C1 + C2) / 2.0;
    const double pow_CMean_to_7 = std::pow(CMean, 7);
    const double G = 0.5 * (1 - std::sqrt(pow_CMean_to_7 / (pow_CMean_to_7 + pow_25_to_7)));

    const double p_l1 = lab1.l;
    const double p_l2 = lab2.l;
    const double p_a1 = (1. + G) * lab1.a;
    const double p_a2 = (1. + G) * lab2.a;
    const double p_b1 = lab1.b;
    const double p_b2 = lab2.b;
    const double p_c1 = std::sqrt(p_a1 * p_a1 + p_b1 * p_b1);
    const double p_c2 = std::sqrt(p_a2 * p_a2 + p_b2 * p_b2);
    double p_h1;
    if (p_a1 == 0 && p_b1 == 0)
        p_h1 = 0;
    else {
        p_h1 = std::atan2(p_b1, p_a1);
        if (p_h1 < 0)
            p_h1 += PI * 2;
    }
    double p_h2;
    if (p_a2 == 0 && p_b2 == 0)
        p_h2 = 0;
    else {
        p_h2 = std::atan2(p_b2, p_a2);
        if (p_h2 < 0)
            p_h2 += PI * 2;
    }

    const double delta_L = p_l2 - p_l1;
    const double delta_C = p_c2 - p_c1;

    double delta_H;
    const double p_c_multi = p_c1 * p_c2;
    if (p_c_multi == 0)
        delta_H = 0;
    else {
        delta_H = p_h2 - p_h1;
        if (delta_H < -PI)
            delta_H += 2 * PI;
        else if (delta_H > PI)
            delta_H -= 2 * PI;
        delta_H = 2 * std::sqrt(p_c_multi) * std::sin(delta_H / 2.);
    }

    double p_L_mean = (p_l1 + p_l2) / 2.0;
    double p_C_mean = (p_c1 + p_c2) / 2.0;

    double p_H_mean, p_H_sum = p_h1 + p_h2;
    if (p_c1 * p_c2 == 0) {
        p_H_mean = p_H_sum;
    } else {
        if (std::fabs(p_h1 - p_h2) <= PI)
            p_H_mean = p_H_sum / 2;
        else {
            if (p_H_sum < 2 * PI)
                p_H_mean = (p_H_sum + 2 * PI) / 2.0;
            else
                p_H_mean = (p_H_sum - 2 * PI) / 2.0;
        }
    }

    const double T = 1 - 0.17 * std::cos(p_H_mean - deg_to_rad(30)) + 0.24 * std::cos(2 * p_H_mean)
        + 0.32 * std::cos(3 * p_H_mean + deg_to_rad(6)) - 0.2 * std::cos(4 * p_H_mean - deg_to_rad(63));
    const double dtheta = deg_to_rad(30) * std::exp(-std::pow((p_H_mean - deg_to_rad(275)) / deg_to_rad(25), 2));

    const double pow_p_cmean_to_7 = std::pow(p_C_mean, 7);
    const double R_C = 2 * std::sqrt(pow_p_cmean_to_7 / (pow_p_cmean_to_7 + pow_25_to_7));

    const double pow_p_lmean_to_2 = std::pow(p_L_mean - 50, 2);
    const double S_L = 1 + (0.015 * pow_p_lmean_to_2) / std::sqrt(20 + pow_p_lmean_to_2);
    const double S_C = 1 + 0.045 * p_C_mean;
    const double S_H = 1 + 0.015 * p_C_mean * T;
    const double R_T = -std::sin(2 * dtheta) * R_C;

    const double K_L = 1.0, K_C = 1.0, K_H = 1.0;

    double de = std::sqrt(
        std::pow(delta_L / (K_L * S_L), 2) + std::pow(delta_C / (K_C * S_C), 2) + std::pow(delta_H / (K_H * S_H), 2)
        + (R_T * (delta_C / (K_C * S_C)) * (delta_H / (K_H * S_H)))
    );
    return float(de);
}

bool is_similar_color(const RGBColor& from, const RGBColor& to, float distance_threshold = 5.0)
{
    return calc_color_distance(RGB2LAB(from), RGB2LAB(to)) <= distance_threshold;
}

RGBColor to_rgb(uint32_t hex) { return RGBColor{(unsigned char)(hex >> 16), (unsigned char)(hex >> 8), (unsigned char)hex}; }

uint64_t generate_hash_key(const RGBColor& from, const RGBColor& to)
{
    uint64_t key = 0;
    key |= (static_cast<uint64_t>(from.r) << 40);
    key |= (static_cast<uint64_t>(from.g) << 32);
    key |= (static_cast<uint64_t>(from.b) << 24);
    key |= (static_cast<uint64_t>(to.r) << 16);
    key |= (static_cast<uint64_t>(to.g) << 8);
    key |= static_cast<uint64_t>(to.b);
    return key;
}

// Orca resources/flush/flush_data_standard.txt: colour list and measured src -> dst flush volumes.
const uint32_t standard_colors[] = {
    0x000000, 0xC12E1F, 0x00AE42, 0x545454, 0xD1D3D5, 0x5B6579,
    0xF4EE2A, 0x9D432C, 0x5E43B7, 0x0A2989, 0xFF6A13, 0x8E9089
};

struct FlushData
{
    uint32_t from;
    uint32_t to;
    float flush;
};

const FlushData standard_flush_data[] = {
    {0x000000, 0x545454, 236},
    {0x000000, 0xF4EE2A, 386},
    {0x000000, 0x5E43B7, 266},
    {0x000000, 0x0A2989, 236},
    {0xC12E1F, 0x000000, 26},
    {0xC12E1F, 0x00AE42, 326},
    {0xC12E1F, 0xF4EE2A, 356},
    {0xC12E1F, 0xFF6A13, 146},
    {0x00AE42, 0x000000, 26},
    {0x00AE42, 0xC12E1F, 146},
    {0x00AE42, 0x545454, 56},
    {0x00AE42, 0xD1D3D5, 266},
    {0x00AE42, 0xF4EE2A, 176},
    {0x00AE42, 0x9D432C, 86},
    {0x00AE42, 0xFF6A13, 206},
    {0x00AE42, 0x8E9089, 206},
    {0x545454, 0xC12E1F, 146},
    {0x545454, 0x00AE42, 116},
    {0x545454, 0xD1D3D5, 176},
    {0x545454, 0x5B6579, 86},
    {0x545454, 0xF4EE2A, 206},
    {0x545454, 0x9D432C, 86},
    {0x545454, 0x5E43B7, 56},
    {0x545454, 0xFF6A13, 236},
    {0x545454, 0x8E9089, 56},
    {0xD1D3D5, 0xC12E1F, 116},
    {0xD1D3D5, 0x00AE42, 116},
    {0xD1D3D5, 0xF4EE2A, 56},
    {0xD1D3D5, 0x9D432C, 86},
    {0xD1D3D5, 0x5E43B7, 86},
    {0xD1D3D5, 0xFF6A13, 86},
    {0x5B6579, 0x000000, 26},
    {0x5B6579, 0xC12E1F, 56},
    {0x5B6579, 0x00AE42, 26},
    {0x5B6579, 0x545454, 26},
    {0x5B6579, 0xD1D3D5, 56},
    {0x5B6579, 0xF4EE2A, 116},
    {0x5B6579, 0x9D432C, 56},
    {0x5B6579, 0x5E43B7, 26},
    {0x5B6579, 0x0A2989, 26},
    {0x5B6579, 0xFF6A13, 116},
    {0x5B6579, 0x8E9089, 26},
    {0xF4EE2A, 0x000000, 56},
    {0xF4EE2A, 0xC12E1F, 26},
    {0xF4EE2A, 0x00AE42, 86},
    {0xF4EE2A, 0x545454, 146},
    {0xF4EE2A, 0x9D432C, 86},
    {0xF4EE2A, 0x5E43B7, 146},
    {0xF4EE2A, 0xFF6A13, 26},
    {0x9D432C, 0x000000, 56},
    {0x9D432C, 0xC12E1F, 56},
    {0x9D432C, 0x00AE42, 176},
    {0x9D432C, 0x545454, 86},
    {0x9D432C, 0xD1D3D5, 236},
    {0x9D432C, 0xF4EE2A, 206},
    {0x9D432C, 0x5E43B7, 116},
    {0x9D432C, 0x0A2989, 86},
    {0x9D432C, 0xFF6A13, 116},
    {0x9D432C, 0x8E9089, 146},
    {0x5E43B7, 0xC12E1F, 176},
    {0x5E43B7, 0x00AE42, 116},
    {0x5E43B7, 0x545454, 56},
    {0x5E43B7, 0xD1D3D5, 206},
    {0x5E43B7, 0x5B6579, 116},
    {0x5E43B7, 0xF4EE2A, 206},
    {0x5E43B7, 0x9D432C, 86},
    {0x5E43B7, 0x0A2989, 56},
    {0x5E43B7, 0xFF6A13, 206},
    {0x5E43B7, 0x8E9089, 146},
    {0x0A2989, 0x000000, 26},
    {0x0A2989, 0xC12E1F, 266},
    {0x0A2989, 0x00AE42, 146},
    {0x0A2989, 0x545454, 86},
    {0x0A2989, 0xD1D3D5, 386},
    {0x0A2989, 0x5B6579, 176},
    {0x0A2989, 0xF4EE2A, 566},
    {0x0A2989, 0x9D432C, 206},
    {0x0A2989, 0x5E43B7, 116},
    {0x0A2989, 0xFF6A13, 326},
    {0x0A2989, 0x8E9089, 206},
    {0xFF6A13, 0x000000, 56},
    {0xFF6A13, 0xC12E1F, 26},
    {0xFF6A13, 0x545454, 86},
    {0xFF6A13, 0xD1D3D5, 146},
    {0xFF6A13, 0xF4EE2A, 146},
    {0xFF6A13, 0x9D432C, 56},
    {0xFF6A13, 0x5E43B7, 146},
    {0xFF6A13, 0x0A2989, 146},
    {0xFF6A13, 0x8E9089, 116},
    {0x8E9089, 0xC12E1F, 86},
    {0x8E9089, 0x00AE42, 56},
    {0x8E9089, 0xD1D3D5, 86},
    {0x8E9089, 0xF4EE2A, 206},
    {0x8E9089, 0x9D432C, 86},
    {0x8E9089, 0xFF6A13, 86},
};

// FlushVolPredictor::predict() for the standard data set.
bool predict(const RGBColor& from, const RGBColor& to, float& flush)
{
    static const std::unordered_map<uint64_t, float> flush_map = [] {
        std::unordered_map<uint64_t, float> map;
        for (const FlushData& d : standard_flush_data)
            map.emplace(generate_hash_key(to_rgb(d.from), to_rgb(d.to)), d.flush);
        return map;
    }();

    // find similar colors in color list
    std::optional<RGBColor> similar_from, similar_to;
    for (uint32_t c : standard_colors) {
        if (is_similar_color(to_rgb(c), from)) {
            similar_from = to_rgb(c);
            break;
        }
    }
    for (uint32_t c : standard_colors) {
        if (is_similar_color(to_rgb(c), to)) {
            similar_to = to_rgb(c);
            break;
        }
    }

    // `from` and `to` should have similar colors in list
    if (!similar_from || !similar_to)
        return false;

    auto iter = flush_map.find(generate_hash_key(*similar_from, *similar_to));
    if (iter == flush_map.end())
        return false;

    flush = iter->second;
    return true;
}

// ---- ColorSpaceConvert.cpp ----

void RGB2HSV(float r, float g, float b, float* h, float* s, float* v)
{
    float Cmax = std::max(std::max(r, g), b);
    float Cmin = std::min(std::min(r, g), b);
    float delta = Cmax - Cmin;

    if (std::abs(delta) < 0.001) {
        *h = 0.f;
    } else if (Cmax == r) {
        *h = 60.f * std::fmod((g - b) / delta, 6.f);
    } else if (Cmax == g) {
        *h = 60.f * ((b - r) / delta + 2);
    } else {
        *h = 60.f * ((r - g) / delta + 4);
    }

    if (std::abs(Cmax) < 0.001) {
        *s = 0.f;
    } else {
        *s = delta / Cmax;
    }

    *v = Cmax;
}

// ---- FlushVolCalc.cpp ----

float to_radians(float degree) { return degree / 180.f * float(PI); }

float get_luminance(float r, float g, float b) { return r * 0.3 + g * 0.59 + b * 0.11; }

float calc_triangle_3rd_edge(float edge_a, float edge_b, float degree_ab)
{
    return std::sqrt(edge_a * edge_a + edge_b * edge_b - 2 * edge_a * edge_b * std::cos(to_radians(degree_ab)));
}

float DeltaHS_BBS(float h1, float s1, float v1, float h2, float s2, float v2)
{
    float h1_rad = to_radians(h1);
    float h2_rad = to_radians(h2);

    float dx = std::cos(h1_rad) * s1 * v1 - std::cos(h2_rad) * s2 * v2;
    float dy = std::sin(h1_rad) * s1 * v1 - std::sin(h2_rad) * s2 * v2;
    float dxy = std::sqrt(dx * dx + dy * dy);
    return std::min(1.2f, dxy);
}

int calc_flush_vol_rgb(
    unsigned char src_r, unsigned char src_g, unsigned char src_b,
    unsigned char dst_r, unsigned char dst_g, unsigned char dst_b
)
{
    float flush_volume;
    if (predict(RGBColor{src_r, src_g, src_b}, RGBColor{dst_r, dst_g, dst_b}, flush_volume))
        return flush_volume;
    float src_r_f, src_g_f, src_b_f, dst_r_f, dst_g_f, dst_b_f;
    float from_hsv_h, from_hsv_s, from_hsv_v;
    float to_hsv_h, to_hsv_s, to_hsv_v;

    src_r_f = (float) src_r / 255.f;
    src_g_f = (float) src_g / 255.f;
    src_b_f = (float) src_b / 255.f;
    dst_r_f = (float) dst_r / 255.f;
    dst_g_f = (float) dst_g / 255.f;
    dst_b_f = (float) dst_b / 255.f;

    // Calculate color distance in HSV color space
    RGB2HSV(src_r_f, src_g_f, src_b_f, &from_hsv_h, &from_hsv_s, &from_hsv_v);
    RGB2HSV(dst_r_f, dst_g_f, dst_b_f, &to_hsv_h, &to_hsv_s, &to_hsv_v);
    float hs_dist = DeltaHS_BBS(from_hsv_h, from_hsv_s, from_hsv_v, to_hsv_h, to_hsv_s, to_hsv_v);

    // 1. Color difference is more obvious if the dest color has high luminance
    // 2. Color difference is more obvious if the source color has low luminance
    float from_lumi = get_luminance(src_r_f, src_g_f, src_b_f);
    float to_lumi = get_luminance(dst_r_f, dst_g_f, dst_b_f);
    float lumi_flush = 0.f;
    if (to_lumi >= from_lumi) {
        lumi_flush = std::pow(to_lumi - from_lumi, 0.7f) * 560.f;
    } else {
        lumi_flush = (from_lumi - to_lumi) * 80.f;

        float inter_hsv_v = 0.67 * to_hsv_v + 0.33 * from_hsv_v;
        hs_dist = std::min(inter_hsv_v, hs_dist);
    }
    float hs_flush = 230.f * hs_dist;

    flush_volume = calc_triangle_3rd_edge(hs_flush, lumi_flush, 120.f);
    flush_volume = std::max(flush_volume, 60.f);

    return flush_volume;
}

unsigned char to_byte(float v) { return (unsigned char) std::lround(std::clamp(v, 0.f, 1.f) * 255.f); }

// "#RRGGBB" -> colour, white when it cannot be parsed.
Domain::ColorRGBA parse_colour(const std::string& s)
{
    if (s.size() == 7 && s[0] == '#') {
        try {
            const unsigned long v = std::stoul(s.substr(1), nullptr, 16);
            return Domain::ColorRGBA{(unsigned char)(v >> 16), (unsigned char)(v >> 8), (unsigned char)v, (unsigned char)255};
        } catch (...) {}
    }
    return Domain::ColorRGBA{1.f, 1.f, 1.f, 1.f};
}

} // namespace

int calc_flush_volume(const Domain::ColorRGBA& from, const Domain::ColorRGBA& to, int min_flush_volume)
{
    unsigned char src_r = to_byte(from.r()), src_g = to_byte(from.g()), src_b = to_byte(from.b());
    unsigned char dst_r = to_byte(to.r()), dst_g = to_byte(to.g()), dst_b = to_byte(to.b());
    // BBS: Transparent materials are treated as white materials
    if (to_byte(from.a()) == 0) {
        src_r = src_g = src_b = 255;
    }
    if (to_byte(to.a()) == 0) {
        dst_r = dst_g = dst_b = 255;
    }

    float flush_volume = calc_flush_vol_rgb(src_r, src_g, src_b, dst_r, dst_g, dst_b);

    flush_volume += min_flush_volume;
    return std::min((int) flush_volume, g_max_flush_volume);
}

std::vector<float> calc_flush_volumes(const std::vector<std::string>& colours, int min_flush_volume)
{
    const size_t n = colours.size();
    std::vector<float> matrix(n * n, 0.f);
    for (size_t i = 0; i < n; ++i)
        for (size_t j = 0; j < n; ++j)
            if (i != j)
                matrix[i * n + j] = float(calc_flush_volume(parse_colour(colours[i]), parse_colour(colours[j]), min_flush_volume));
    return matrix;
}

} // namespace Slic3r::FlushVolCalc
