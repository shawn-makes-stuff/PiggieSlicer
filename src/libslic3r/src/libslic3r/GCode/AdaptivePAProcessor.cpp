#include "libslic3r/GCode/AdaptivePAProcessor.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "libslic3r/libslic3r.h"
#include "libslic3r/GCode/GCodeWriter.hpp"
#include "Slic3r/Domain/ConfigDefsFDM.hpp"

namespace Slic3r {

bool AdaptivePAInterpolator::parse(const std::string &data)
{
    m_flow_interpolators.clear();
    m_initialised = false;
    try {
        std::istringstream ss(data);
        std::map<double, std::vector<std::pair<double, double>>> acc_to_flow_pa;
        for (std::string line; std::getline(ss, line);) {
            std::istringstream line_stream(line);
            std::string value;
            double pa = 0., flow_rate = 0., acceleration = 0.;
            if (std::getline(line_stream, value, ','))
                pa = std::stod(value);
            if (std::getline(line_stream, value, ','))
                flow_rate = std::stod(value);
            if (std::getline(line_stream, value, ','))
                acceleration = std::stod(value);
            acc_to_flow_pa[acceleration].emplace_back(flow_rate, pa);
        }
        for (const auto &[acceleration, points] : acc_to_flow_pa) {
            if (points.size() < 2)
                continue;
            std::vector<double> flow_rates, pas;
            for (const auto &[flow_rate, pa] : points) {
                flow_rates.push_back(flow_rate);
                pas.push_back(pa);
            }
            m_flow_interpolators.emplace(acceleration, PchipInterpolator(flow_rates, pas));
        }
    } catch (const std::exception &) {
        return false;
    }
    m_initialised = true;
    return true;
}

double AdaptivePAInterpolator::operator()(double flow_rate, double acceleration) const
{
    // the PA of every acceleration's model at this flow rate, then interpolated over the accelerations
    std::vector<double> pas, accelerations;
    for (const auto &[model_acceleration, interpolator] : m_flow_interpolators) {
        const double pa = interpolator.interpolate(flow_rate);
        if (pa != -1) {
            pas.push_back(pa);
            accelerations.push_back(model_acceleration);
        }
    }
    if (accelerations.size() < 2)
        return accelerations.size() == 1 ? std::round(pas.front() * 1000.0) / 1000.0 : -1;
    return std::round(PchipInterpolator(accelerations, pas).interpolate(acceleration) * 1000.0) / 1000.0;
}

AdaptivePAProcessor::AdaptivePAProcessor(const GCodeWriter &writer, const PrintConfigView &config)
    : m_writer(writer)
    , m_vendor_id(config.hw_config().vendor_id)
    , m_gcode_comments(config.get<bool>("gcode_comments"))
    , m_pa(config.get<std::vector<double>>("pressure_advance_value"))
    , m_adaptive_pa(config.get<std::vector<bool>>("adaptive_pressure_advance"))
    , m_bridge_pa(config.get<std::vector<double>>("adaptive_pressure_advance_bridges"))
    , m_models(config.get<std::vector<std::string>>("adaptive_pressure_advance_model"))
    , m_pa_change_pattern(R"(;PA_CHANGE:T(\d+) MM3MM:([0-9]*\.?[0-9]+(?:[eE][-+]?[0-9]+)?) ACCEL:(\d+) BR:(\d+) RC:(\d+) OV:(\d+))")
{
    for (const Domain::PressureAdvance pa : config.get<std::vector<Domain::PressureAdvance>>("pressure_advance"))
        m_pa_enabled.push_back(pa != Domain::PressureAdvance::Disabled);
    for (size_t i = 0; i < m_pa_enabled.size() && i < m_adaptive_pa.size() && !m_enabled; ++i)
        m_enabled = m_pa_enabled[i] && m_adaptive_pa[i];
}

double AdaptivePAProcessor::default_pa(unsigned int extruder) const
{
    return m_pa_enabled.at(extruder) ? m_pa.at(extruder) : 0.;
}

AdaptivePAInterpolator *AdaptivePAProcessor::interpolator(unsigned int extruder)
{
    // the model is parsed the first time the extruder is requested
    auto [it, inserted] = m_interpolators.try_emplace(extruder);
    if (inserted && m_adaptive_pa.at(extruder) && m_pa_enabled.at(extruder)) {
        it->second = std::make_unique<AdaptivePAInterpolator>();
        it->second->parse(m_models.at(extruder));
    }
    return it->second.get();
}

std::string AdaptivePAProcessor::process_layer(std::string &&gcode)
{
    std::istringstream stream(gcode);
    std::ostringstream output;
    bool wipe_command = false;

    for (std::string line; std::getline(stream, line);) {
        // ignore the speed changes of a wipe
        if (line.find("WIPE_START") != std::string::npos)
            wipe_command = true;
        if (line.rfind("G1 F", 0) == 0 && !wipe_command)
            m_current_feedrate = std::stod(line.substr(line.find('F') + 1)) / 60.0;
        if (line.find("WIPE_END") != std::string::npos)
            wipe_command = false;

        m_next_feedrate = 0;

        // A tool change sets the new tool's pressure advance: the next tag sets the adaptive one again.
        // (Orca resets this from GCode::set_extruder; here the layers pass through concurrently with the
        // generation, so the pass sees the tool change itself.)
        if ((line.size() > 1 && line[0] == 'T' && std::isdigit((unsigned char) line[1])) ||
            line.rfind("; MANUAL_TOOL_CHANGE T", 0) == 0)
            m_last_extruder = -1;

        std::smatch match;
        if (line.rfind(";PA_CHANGE", 0) != 0 || !std::regex_search(line, match, m_pa_change_pattern)) {
            if (line.rfind(";PA_CHANGE", 0) != 0)
                output << line << '\n';
            continue;
        }

        const int extruder          = std::stoi(match[1].str());
        const double mm3_per_mm     = std::stod(match[2].str());
        const unsigned acceleration = unsigned(std::stoul(match[3].str()));
        const bool is_bridge        = std::stoi(match[4].str()) != 0;
        const bool is_overhang      = std::stoi(match[6].str()) != 0;
        const bool extruder_changed = extruder != m_last_extruder;
        m_last_extruder = extruder;

        // Look ahead for the feature's speeds: until the first travel after an extrusion, a wipe or a
        // role change.
        const std::streampos current_pos = stream.tellg();
        double max_feedrate = 0;
        bool extrude_move_found = false;
        int line_counter = 0;
        for (std::string next_line; std::getline(stream, next_line);) {
            ++line_counter;
            const bool is_move = next_line.rfind("G1 ", 0) == 0 && next_line.find('X') != std::string::npos &&
                next_line.find('Y') != std::string::npos;
            if (!extrude_move_found && is_move && next_line.find('E') != std::string::npos) {
                extrude_move_found = true;
                continue;
            }
            if (extrude_move_found && is_move && next_line.find('E') == std::string::npos)
                break;
            if (next_line.find("WIPE") != std::string::npos)
                break;
            if (next_line.rfind(";PA_CHANGE", 0) == 0) {
                if (const size_t rc_pos = next_line.rfind("RC:"); rc_pos != std::string::npos &&
                    std::stoi(next_line.substr(rc_pos + 3)) == 1)
                    break;
            }
            if (next_line.rfind("G1 F", 0) == 0) {
                const double feedrate = std::stod(next_line.substr(next_line.find('F') + 1)) / 60.0;
                // right after the tag, before any extrusion: the speed the feature starts with
                if (line_counter == 1)
                    m_current_feedrate = feedrate;
                max_feedrate = std::max(max_feedrate, feedrate);
                if (m_next_feedrate < EPSILON)
                    m_next_feedrate = feedrate;
            }
        }
        m_max_next_feedrate = max_feedrate > 0 ? max_feedrate : m_current_feedrate;
        stream.clear();
        stream.seekg(current_pos);

        double predicted_pa = 0;
        double speed = 0;
        AdaptivePAInterpolator *model = this->interpolator(unsigned(extruder));
        if (model == nullptr) {
            predicted_pa = default_pa(extruder);
            if (m_gcode_comments)
                output << "; APA: Tool doesnt have APA enabled\n";
        } else if (!model->is_initialised()) {
            predicted_pa = default_pa(extruder);
            if (m_gcode_comments)
                output << "; APA: Interpolator setup failed, using default pressure advance\n";
        } else {
            // overhangs: the lower of the current and the next speed, else the feature's highest speed
            speed = is_overhang ?
                ((m_current_feedrate == 0 || m_next_feedrate == 0) ? std::max(m_current_feedrate, m_next_feedrate) :
                                                                     std::min(m_current_feedrate, m_next_feedrate)) :
                std::max(m_max_next_feedrate, m_current_feedrate);
            predicted_pa = (*model)(mm3_per_mm * speed, acceleration);
            if (is_bridge && m_bridge_pa.at(extruder) > EPSILON)
                predicted_pa = m_bridge_pa.at(extruder);
            if (predicted_pa < 0) {
                predicted_pa = default_pa(extruder);
                if (m_gcode_comments)
                    output << "; APA: Interpolation failed, using fallback pressure advance value\n";
            }
        }
        if (m_gcode_comments) {
            output << line << '\n';
            if (is_bridge && m_bridge_pa.at(extruder) > EPSILON)
                output << "; APA Model Override (bridge)\n";
            output << "; APA Current Speed: " << std::to_string(m_current_feedrate) << "\n";
            output << "; APA Next Speed: " << std::to_string(m_next_feedrate) << "\n";
            output << "; APA Max Next Speed: " << std::to_string(m_max_next_feedrate) << "\n";
            output << "; APA Speed Used: " << std::to_string(speed) << "\n";
            output << "; APA Flow rate: " << std::to_string(mm3_per_mm * m_max_next_feedrate) << "\n";
            output << "; APA Prev PA: " << std::to_string(m_last_predicted_pa) << " New PA: " << std::to_string(predicted_pa) << "\n";
        }
        if (extruder_changed || std::fabs(predicted_pa - m_last_predicted_pa) > EPSILON) {
            output << m_writer.set_pressure_advance(predicted_pa, m_vendor_id);
            m_last_predicted_pa = predicted_pa;
        }
    }
    return output.str();
}

std::string AdaptivePAProcessor::validate_model(const std::string &model)
{
    std::istringstream model_stream(model);
    int line_number = 0;
    for (std::string line; std::getline(model_stream, line);) {
        ++line_number;
        const size_t first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            continue;
        line = line.substr(first, line.find_last_not_of(" \t\r\n") - first + 1);
        const std::string at = "Line " + std::to_string(line_number) + ": ";
        if (std::any_of(line.begin(), line.end(), [](char c) { return !std::isdigit((unsigned char) c) && c != ',' && c != '.'; }))
            return at + "only numbers, commas and dots are allowed";
        if (std::count(line.begin(), line.end(), ',') != 2)
            return at + "must contain exactly 3 comma-separated values (PA, flow, acceleration)";
        try {
            std::istringstream line_stream(line);
            std::string pa_str, flow_str, accel_str;
            std::getline(line_stream, pa_str, ',');
            std::getline(line_stream, flow_str, ',');
            std::getline(line_stream, accel_str, ',');
            const double pa = std::stod(pa_str), flow = std::stod(flow_str), accel = std::stod(accel_str);
            if (pa >= 2.0)
                return at + "PA value must be less than 2";
            if (flow <= pa)
                return at + "flow value must be greater than PA value";
            if (accel <= flow)
                return at + "acceleration value must be greater than flow value";
        } catch (const std::exception &) {
            return at + "invalid numeric value";
        }
    }
    return {};
}

} // namespace Slic3r
