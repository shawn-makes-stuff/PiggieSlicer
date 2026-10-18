#ifndef slic3r_GCode_Wipe_hpp_
#define slic3r_GCode_Wipe_hpp_

#include <math.h>
#include <algorithm>
#include "Slic3r/Domain/Percentage.hpp"
#include <cassert>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <cmath>

#include "SmoothPath.hpp"
#include "libslic3r/ConfigViews.hpp"
#include "libslic3r/Geometry/ArcWelder.hpp"
#include "libslic3r/Point.hpp"

namespace Slic3r {

class GCodeGenerator;

namespace GCode {

class Wipe {
public:
    using Path = Slic3r::Geometry::ArcWelder::Path;

    Wipe() = default;

    void            init(const PrintConfigView &config, const std::vector<unsigned int> &extruders);
    void            enable(double wipe_len_max) { m_enabled = true; m_wipe_len_max = wipe_len_max; }
    void            disable() { m_enabled = false; }
    bool            enabled() const { return m_enabled; }

    const Path&     path() const { return m_path; }
    bool            has_path() const { assert(m_path.empty() || m_path.size() > 1); return ! m_path.empty(); }
    void            reset_path() { m_path.clear(); m_offset = Point::Zero(); }
    void            set_path(const Path &path) {
        assert(path.empty() || path.size() > 1);
        this->reset_path(); 
        if (this->enabled() && path.size() > 1) 
            m_path = path;
    }
    void            set_path(Path &&path) {
        assert(path.empty() || path.size() > 1);
        this->reset_path(); 
        if (this->enabled() && path.size() > 1)
            m_path = std::move(path);
    }
    void            set_path(SmoothPath &&path);
    void            offset_path(const Point &v) { m_offset += v; }
    // Orca wipe_distance of an extruder
    double          wipe_distance(unsigned int extruder_id) const { return m_wipe_distance.at(extruder_id); }
    // Orca wipe_inward: the moves along the path at the wipe speed, without a retraction
    std::string     wipe_without_retraction(GCodeGenerator &gcodegen, double travel_speed);

    std::string wipe(
        GCodeGenerator& gcodegen,
        const std::vector<double>& retract_speed,
        double travel_speed,
        bool toolchange
    );

    // Reduce feedrate a bit; travel speed is often too high to move on existing material.
    // Too fast = ripping of existing material; too slow = short wipe path, thus more blob.
    static double   calc_wipe_speed(double travel_speed) { return travel_speed * 0.8; }
    // Orca's wipe speed: role_based_wipe_speed ? the current extrusion's speed : wipe_speed (over
    // travel_speed); at least 10 mm/s
    double          wipe_speed(double travel_speed, double current_speed_mm_min) const
    {
        const double speed = m_role_based_wipe_speed && current_speed_mm_min > 0. ?
            current_speed_mm_min / 60. : m_wipe_speed.get_abs_value(travel_speed);
        return std::max(speed, 10.);
    }
    // Reduce retraction length a bit to avoid effective retraction speed to be greater than the configured one
    // due to rounding (TODO: test and/or better math for this).
    static double   calc_xy_to_e_ratio(const std::vector<double>& retract_speed, double travel_speed, unsigned int extruder_id)
        { return 0.95 * floor(retract_speed.at(extruder_id) + 0.5) / calc_wipe_speed(travel_speed); }

private:
    Domain::FloatOrPercentage m_wipe_speed{Domain::Percentage{80.}};
    bool    m_role_based_wipe_speed{false};
    bool    m_enabled{ false };
    // Maximum length of a path to accumulate. Only wipes shorter than this threshold will be requested.
    double  m_wipe_len_max{ 0. };
    // Orca wipe_distance, per extruder
    std::vector<double> m_wipe_distance;
    Path    m_path;
    // Offset from m_path to the current PrintObject active.
    Point   m_offset{ Point::Zero() };
};

} // namespace GCode
} // namespace Slic3r

#endif // slic3r_GCode_Wipe_hpp_
