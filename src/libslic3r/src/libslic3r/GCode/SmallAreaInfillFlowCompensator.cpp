#include "libslic3r/GCode/SmallAreaInfillFlowCompensator.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

#include <boost/algorithm/string/trim.hpp>

namespace Slic3r {

using boost::algorithm::trim_copy;

static bool nearly_equal(double a, double b)
{
    return std::nextafter(a, std::numeric_limits<double>::lowest()) <= b && std::nextafter(a, std::numeric_limits<double>::max()) >= b;
}

std::string SmallAreaInfillFlowCompensator::parse(const std::string &model, std::vector<double> &lengths, std::vector<double> &flow_factors)
{
    std::string normalized = model;
    std::replace(normalized.begin(), normalized.end(), ';', '\n');
    std::istringstream lines(normalized);
    for (std::string line; std::getline(lines, line);) {
        std::istringstream iss(line);
        std::string        value;
        if (!std::getline(iss, value, ','))
            continue;
        value = trim_copy(value);
        if (value.empty())
            continue;
        try {
            const double length = std::stod(value);
            if (std::getline(iss, value, ',')) {
                lengths.push_back(length);
                flow_factors.push_back(std::stod(trim_copy(value)));
            }
        } catch (...) {
            return "Error parsing data point in small area infill compensation model: " + trim_copy(line);
        }
    }
    for (size_t i = 0; i < lengths.size(); ++i) {
        if (i == 0) {
            if (!nearly_equal(lengths[i], 0.0))
                return "First extrusion length for small area infill compensation model must be 0";
        } else {
            if (nearly_equal(lengths[i], 0.0))
                return "Only the first extrusion length for small area infill compensation model can be 0";
            if (lengths[i] <= lengths[i - 1])
                return "Extrusion lengths for subsequent points must be increasing";
        }
    }
    for (size_t i = 1; i < flow_factors.size(); ++i)
        if (flow_factors[i] <= flow_factors[i - 1])
            return "Flow compensation factors must strictly increase with extrusion length";
    if (!flow_factors.empty() && !nearly_equal(flow_factors.back(), 1.0))
        return "Final compensation factor for small area infill flow compensation model must be 1.0";
    if (lengths.size() < 2)
        return "The small area infill compensation model needs at least two points";
    return {};
}

std::string SmallAreaInfillFlowCompensator::validate_model(const std::string &model)
{
    std::vector<double> lengths, flow_factors;
    return parse(model, lengths, flow_factors);
}

SmallAreaInfillFlowCompensator::SmallAreaInfillFlowCompensator(const std::string &model)
{
    if (parse(model, m_lengths, m_flow_factors).empty())
        m_model.emplace(m_lengths, m_flow_factors);
}

double SmallAreaInfillFlowCompensator::modify_flow(const double line_length, const double dE) const
{
    if (!m_model || line_length == 0 || line_length > m_lengths.back())
        return dE;
    return dE * m_model->interpolate(line_length);
}

} // namespace Slic3r
