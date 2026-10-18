#include "libslic3r/GCode/SmallAreaInfillFlowCompensator.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

namespace Slic3r {

static bool nearly_equal(double a, double b)
{
    return std::nextafter(a, std::numeric_limits<double>::lowest()) <= b && std::nextafter(a, std::numeric_limits<double>::max()) >= b;
}

static std::string trimmed(const std::string &s)
{
    const size_t first = s.find_first_not_of(" \t\r\n");
    return first == std::string::npos ? std::string{} : s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
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
        value = trimmed(value);
        if (value.empty())
            continue;
        try {
            const double length = std::stod(value);
            if (std::getline(iss, value, ',')) {
                lengths.push_back(length);
                flow_factors.push_back(std::stod(trimmed(value)));
            }
        } catch (...) {
            return "Error parsing data point in small area infill compensation model: " + trimmed(line);
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
