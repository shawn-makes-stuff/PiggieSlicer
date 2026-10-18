#include "Wipe.hpp"

#include <string_view>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <cinttypes>

#include "../GCode.hpp"
#include "Slic3r/Biz/libpgcode/Utils.hpp"
#include "libslic3r/Extruder.hpp"
#include "libslic3r/GCode/GCodeWriter.hpp"
#include "libslic3r/GCode/SmoothPath.hpp"
#include "libslic3r/Geometry/ArcWelder.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/libslic3r.h"

using namespace std::string_view_literals;

namespace Slic3r::GCode {

void Wipe::init(const PrintConfigView &config, const std::vector<unsigned int> &extruders)
{
    this->reset_path();
    m_wipe_speed            = config.get<Domain::FloatOrPercentage>("wipe_speed");
    m_role_based_wipe_speed = config.get<bool>("role_based_wipe_speed");

    // Maximum wipe length to accumulate by the wipe cache: the longest wipe_distance (Orca).
    m_wipe_distance = config.get<std::vector<double>>("wipe_distance");
    double wipe_xy = 0;
    for (auto id : extruders)
        if (config.get<std::vector<bool>>("wipe").at(id))
            wipe_xy = std::max(wipe_xy, m_wipe_distance.at(id));

    if (wipe_xy == 0)
        this->disable();
    else
        this->enable(wipe_xy);
}

void Wipe::set_path(SmoothPath &&path) {
    this->reset_path();

    if (this->enabled() && !path.empty()) {
        const coord_t wipe_len_max_scaled = scaled(m_wipe_len_max);
        m_path = std::move(path.front().path);
        int64_t len = Geometry::ArcWelder::estimate_path_length(m_path);
        for (auto it = std::next(path.begin()); len < wipe_len_max_scaled && it != path.end(); ++it) {
            if (it->path_attributes.role.is_bridge())
                break; // Do not perform a wipe on bridges.
            assert(it->path.size() >= 2);
            assert(m_path.back().point == it->path.front().point);
            if (m_path.back().point != it->path.front().point)
                // ExtrusionMultiPath is interrupted in some place. This should not really happen.
                break;
            len += Geometry::ArcWelder::estimate_path_length(it->path);
            m_path.insert(m_path.end(), it->path.begin() + 1, it->path.end());
        }
    }

    assert(m_path.empty() || m_path.size() > 1);
}

std::string Wipe::wipe(
    GCodeGenerator& gcodegen,
    const std::vector<double>& retract_speed,
    double travel_speed,
    bool toolchange
)
{
    std::string gcode;
    const Extruder &extruder = *gcodegen.writer().extruder();
    static constexpr const std::string_view wipe_retract_comment = "wipe and retract"sv;

    // Remaining quantized retraction length.
    if (double retract_length = extruder.retract_to_go(toolchange ? extruder.retract_length_toolchange() : extruder.retract_length());
        retract_length > 0 && this->has_path()) {
        // Delayed emitting of a wipe start tag.
        bool wiped = false;
        const double wipe_speed = this->wipe_speed(travel_speed, gcodegen.writer().current_speed());
        auto start_wipe = [&wiped, &gcode, &gcodegen, wipe_speed](){
            if (! wiped) {
                wiped = true;
                gcode += ";" + std::string{Biz::libpgcode::reserved_tag(Biz::libpgcode::Tags::Wipe_Start)} + "\n";
                gcode += gcodegen.writer().set_speed(wipe_speed * 60, {}, gcodegen.enable_cooling_markers() ? ";_WIPE"sv : ""sv);
            }
        };
        // Orca: the wipe is at most wipe_distance long (from the current position along the path), retracting
        // what it can over it at the retraction speed; the rest is retracted before the wipe
        double path_length = unscaled((this->path().front().point + m_offset - *gcodegen.last_position).cast<double>().norm()) +
            unscaled(Geometry::ArcWelder::path_length<double>(this->path()));
        const double wipe_length = std::min(path_length, m_wipe_distance.at(extruder.id()));
        // 0.95: the effective retraction speed stays below the configured one after rounding (3.0's Wipe)
        const double during_wipe = std::min(retract_length, 0.95 * retract_speed.at(extruder.id()) * wipe_length / wipe_speed);
        if (during_wipe <= EPSILON || wipe_length <= EPSILON) {
            this->reset_path();
            return gcode;
        }
        if (retract_length - during_wipe > EPSILON) {
            const double full_length = toolchange ? extruder.retract_length_toolchange() : extruder.retract_length();
            gcode += gcodegen.writer().retract_to(full_length - during_wipe, toolchange);
            retract_length = during_wipe;
        }
        const double xy_to_e    = during_wipe / wipe_length;
        auto         wipe_linear = [&gcode, &gcodegen, &retract_length, xy_to_e](const Vec2d &prev_quantized, Vec2d &p) {
            Vec2d  p_quantized = GCodeFormatter::quantize(p);
            if (p_quantized == prev_quantized) {
                p = p_quantized;
                return false;
            }
            double segment_length = (p_quantized - prev_quantized).norm();
            // Quantize E axis as it is to be extruded as a whole segment.
            double dE = GCodeFormatter::quantize_e(xy_to_e * segment_length);
            bool   done = false;
            if (dE > retract_length - EPSILON) {
                if (dE > retract_length + EPSILON)
                    // Shorten the segment.
                    p = GCodeFormatter::quantize(Vec2d(prev_quantized + (p - prev_quantized) * (retract_length / dE)));
                else
                    p = p_quantized;
                dE   = retract_length;
                done = true;
            } else
                p = p_quantized;
            gcode += gcodegen.writer().extrude_to_xy(p, -dE, wipe_retract_comment);
            retract_length -= dE;
            return done;
        };
        auto         wipe_arc = [&gcode, &gcodegen, &retract_length, xy_to_e, &wipe_linear](
            const Vec2d &prev_quantized, Vec2d &p, double radius_in, const bool ccw) {
            Vec2d  p_quantized = GCodeFormatter::quantize(p);
            if (p_quantized == prev_quantized) {
                p = p_quantized;
                return false;
            }
            // Use the exact radius for calculating the IJ values, no quantization.
            double radius = radius_in;
            if (radius == 0)
                // Degenerated arc after quantization. Process it as if it was a line segment.
                return wipe_linear(prev_quantized, p);
            Vec2d  center = Geometry::ArcWelder::arc_center(prev_quantized.cast<double>(), p_quantized.cast<double>(), double(radius), ccw);
            float  angle  = Geometry::ArcWelder::arc_angle(prev_quantized.cast<double>(), p_quantized.cast<double>(), double(radius));
            assert(angle > 0);
            double segment_length = angle * std::abs(radius);
            double dE = GCodeFormatter::quantize_e(xy_to_e * segment_length);
            bool   done = false;
            if (dE > retract_length - EPSILON) {
                if (dE > retract_length + EPSILON) {
                    // Shorten the segment. Recalculate the arc from the unquantized end coordinate.
                    center = Geometry::ArcWelder::arc_center(prev_quantized.cast<double>(), p.cast<double>(), double(radius), ccw);
                    angle = Geometry::ArcWelder::arc_angle(prev_quantized.cast<double>(), p.cast<double>(), double(radius));
                    segment_length = angle * std::abs(radius);
                    dE = xy_to_e * segment_length;
                    p = GCodeFormatter::quantize(
                            Vec2d(center + Eigen::Rotation2D((ccw ? angle : -angle) * (retract_length / dE)) * (prev_quantized - center)));
                } else
                    p = p_quantized;
                dE   = retract_length;
                done = true;
            } else
                p = p_quantized;
            assert(dE > 0);
            {
                // Calculate quantized IJ circle center offset.
                Vec2d ij = GCodeFormatter::quantize(Vec2d(center - prev_quantized));
                if (ij == Vec2d::Zero())
                    // Degenerated arc after quantization. Process it as if it was a line segment.
                    return wipe_linear(prev_quantized, p);
                // The arc is valid.
                gcode += gcodegen.writer().extrude_to_xy_G2G3IJ(
                    p, ij, ccw, -dE, wipe_retract_comment);
            }
            retract_length -= dE;
            return done;
        };
        // Start with the current position, which may be different from the wipe path start in case of loop clipping.
        Vec2d prev = gcodegen.point_to_gcode_quantized(*gcodegen.last_position);
        auto  it   = this->path().begin();
        Vec2d p    = gcodegen.point_to_gcode(it->point + m_offset);
        ++ it;
        bool done = false;
        if (p != prev) {
            start_wipe();
            done = wipe_linear(prev, p);
        }
        if (! done) {
            prev = p;
            auto end = this->path().end();
            for (; it != end && ! done; ++ it) {
                p = gcodegen.point_to_gcode(it->point + m_offset);
                if (p != prev) {
                    start_wipe();
                    if (it->linear() ?
                        wipe_linear(prev, p) :
                        wipe_arc(prev, p, unscaled<double>(it->radius), it->ccw()))
                        break;
                    prev = p;
                }
            }
        }
        if (wiped) {
            // add tag for processor
            assert(p == GCodeFormatter::quantize(p));
            gcode += ";" + std::string{Biz::libpgcode::reserved_tag(Biz::libpgcode::Tags::Wipe_End)} + "\n";
            gcodegen.last_position = gcodegen.gcode_to_point(p);
        }
    }

    // Prevent wiping again on the same path.
    this->reset_path();
    return gcode;
}

std::string Wipe::wipe_without_retraction(GCodeGenerator &gcodegen, double travel_speed)
{
    std::string gcode;
    if (this->has_path()) {
        gcode += ";" + std::string{Biz::libpgcode::reserved_tag(Biz::libpgcode::Tags::Wipe_Start)} + "\n";
        gcode += gcodegen.writer().set_speed(this->wipe_speed(travel_speed, gcodegen.writer().current_speed()) * 60, {},
                                             gcodegen.enable_cooling_markers() ? ";_WIPE"sv : ""sv);
        Vec2d prev = gcodegen.point_to_gcode_quantized(*gcodegen.last_position);
        for (const Geometry::ArcWelder::Segment &segment : this->path()) {
            const Vec2d p = GCodeFormatter::quantize(Vec2d(gcodegen.point_to_gcode(segment.point + m_offset)));
            if (p != prev) {
                gcode += gcodegen.writer().extrude_to_xy(p, 0., "wipe inward"sv);
                prev = p;
            }
        }
        gcode += ";" + std::string{Biz::libpgcode::reserved_tag(Biz::libpgcode::Tags::Wipe_End)} + "\n";
        gcodegen.last_position = gcodegen.gcode_to_point(prev);
    }
    // Prevent wiping again on the same path.
    this->reset_path();
    return gcode;
}

} // namespace Slic3r::GCode
