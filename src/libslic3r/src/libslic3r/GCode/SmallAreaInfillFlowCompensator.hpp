// Ported from OrcaSlicer (based on Alexander Thor's Small-Area-Flow-Comp, GPLv3): the flow of short solid infill
// lines follows a model of (extrusion length, flow factor) points, against over-extruded small areas.

#ifndef slic3r_GCode_SmallAreaInfillFlowCompensator_hpp_
#define slic3r_GCode_SmallAreaInfillFlowCompensator_hpp_

#include <optional>
#include <string>
#include <vector>

#include "libslic3r/GCode/PchipInterpolator.hpp"

namespace Slic3r {

class SmallAreaInfillFlowCompensator
{
public:
    // model: one "extrusion length, flow factor" pair per line (or separated by ';')
    explicit SmallAreaInfillFlowCompensator(const std::string &model);

    // Empty if the model is valid, else the first problem.
    static std::string validate_model(const std::string &model);

    // dE of a line_length (mm) long line, compensated
    double modify_flow(double line_length, double dE) const;

private:
    std::vector<double>              m_lengths;
    std::vector<double>              m_flow_factors;
    std::optional<PchipInterpolator> m_model;

    // the points of a model, or the first problem
    static std::string parse(const std::string &model, std::vector<double> &lengths, std::vector<double> &flow_factors);
};

} // namespace Slic3r

#endif // slic3r_GCode_SmallAreaInfillFlowCompensator_hpp_
