// Adaptive pressure advance, ported from OrcaSlicer (AdaptivePAProcessor, AdaptivePAInterpolator,
// PchipInterpolatorHelper): GCodeGenerator::_extrude emits ";PA_CHANGE:" tags with the flow (mm3/mm) and
// acceleration of the coming feature, and this per-layer pass replaces them by the pressure advance a
// filament's measured model predicts for the feature's flow rate and acceleration.

#ifndef slic3r_GCode_AdaptivePAProcessor_hpp_
#define slic3r_GCode_AdaptivePAProcessor_hpp_

#include <map>
#include <memory>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

#include "libslic3r/ConfigViews.hpp"
#include "libslic3r/GCode/PchipInterpolator.hpp"

namespace Slic3r {

class GCodeWriter;

// Pressure advance from a model of "PA, flow rate, acceleration" lines.
class AdaptivePAInterpolator
{
public:
    // Returns false if the model cannot be parsed.
    bool parse(const std::string &data);
    bool is_initialised() const { return m_initialised; }
    // The interpolated pressure advance (rounded to 3 decimals), -1 if it cannot be interpolated.
    double operator()(double flow_rate, double acceleration) const;

private:
    // a flow rate to PA interpolator per acceleration
    std::map<double, PchipInterpolator> m_flow_interpolators;
    bool m_initialised{false};
};

class AdaptivePAProcessor
{
public:
    AdaptivePAProcessor(const GCodeWriter &writer, const PrintConfigView &config);

    // Whether any filament has pressure advance and adaptive pressure advance on.
    bool enabled() const { return m_enabled; }
    std::string process_layer(std::string &&gcode);

    // Empty if the model is valid, else the first problem.
    static std::string validate_model(const std::string &model);

private:
    const GCodeWriter &m_writer;
    std::string m_vendor_id;
    bool m_gcode_comments;
    std::vector<bool> m_pa_enabled;
    std::vector<double> m_pa;
    std::vector<bool> m_adaptive_pa;
    std::vector<double> m_bridge_pa;
    std::vector<std::string> m_models;
    std::unordered_map<unsigned int, std::unique_ptr<AdaptivePAInterpolator>> m_interpolators;
    bool m_enabled{false};

    double m_last_predicted_pa{0.};
    double m_max_next_feedrate{0.};
    double m_next_feedrate{0.};
    double m_current_feedrate{0.};
    int m_last_extruder{-1};

    std::regex m_pa_change_pattern;

    // nullptr if adaptive pressure advance is off for the extruder
    AdaptivePAInterpolator *interpolator(unsigned int extruder);
    double default_pa(unsigned int extruder) const;
};

} // namespace Slic3r

#endif // slic3r_GCode_AdaptivePAProcessor_hpp_
