#include "libslic3r/GCode/FanMover.hpp"

#include <cassert>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <sstream>

#include "libslic3r/libslic3r.h"
#include "libslic3r/ExtrusionRole.hpp"
#include "libslic3r/GCode/GCodeWriter.hpp"

namespace Slic3r {

using Domain::GCodeFlavor;

static float get_axis_value(const std::string &line, char axis)
{
    char match[3] = " X";
    match[1] = axis;
    size_t pos = line.find(match);
    if (pos == std::string::npos)
        return NAN;
    pos += 2;
    const char *c    = line.c_str();
    char       *pend = nullptr;
    errno = 0;
    const double v = strtod(c + pos, &pend);
    if (pend != nullptr && errno == 0 && pend != c)
        return float(v);
    return NAN;
}

static void change_axis_value(std::string &line, char axis, const float new_value, const int decimal_digits)
{
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(decimal_digits) << new_value;
    char match[3] = " X";
    match[1] = axis;
    const size_t pos = line.find(match) + 2;
    const size_t end = std::min(line.find(' ', pos + 1), line.find(';', pos + 1));
    line = line.replace(pos, end - pos, ss.str());
}

// The part cooling fan speed (0-255) a line sets, -1 if none.
static int16_t get_fan_speed(const std::string &line, GCodeFlavor flavor)
{
    if (line.compare(0, 4, "M106") == 0) {
        if (flavor == GCodeFlavor::gcfMach3 || flavor == GCodeFlavor::gcfMachinekit)
            return (int16_t) get_axis_value(line, 'P');
        // M106 and M106 P1 control the part cooling fan; P2 is the auxiliary fan, other indices are ignored.
        const float idx = get_axis_value(line, 'P');
        if (!std::isnan(idx) && idx != 1.0f)
            return -1;
        return (int16_t) get_axis_value(line, 'S');
    } else if (line.compare(0, 4, "M127") == 0 || line.compare(0, 4, "M107") == 0) {
        return 0;
    } else if ((flavor == GCodeFlavor::gcfMakerWare || flavor == GCodeFlavor::gcfSailfish) && line.compare(0, 4, "M126") == 0) {
        return (int16_t) get_axis_value(line, 'T');
    }
    return -1;
}

FanMover::FanMover(const GCodeWriter &writer, float nb_seconds_delay, bool only_overhangs, float kickstart, int min_pwm)
    : nb_seconds_delay(nb_seconds_delay > 0 ? std::max(0.01f, nb_seconds_delay) : 0)
    , relative_e(writer.config.use_relative_e_distances)
    , only_overhangs(only_overhangs)
    , kickstart(kickstart)
    , min_pwm(min_pwm)
    , m_writer(writer)
{
    if (const std::string axis = writer.extrusion_axis(); !axis.empty())
        m_parser.set_extrusion_axis(axis.front());
    m_parser.set_use_relative_e_distances(relative_e);
}

const std::string &FanMover::process_gcode(const std::string &gcode, bool flush)
{
    m_process_output = "";

    // recompute buffer time to recover from rounding
    m_buffer_time_size = 0;
    for (const BufferData &data : m_buffer)
        m_buffer_time_size += data.time;

    if (!gcode.empty())
        m_parser.parse_buffer(gcode, [this](GCodeReader &reader, const GCodeReader::GCodeLine &line) {
            this->process_gcode_line(reader, line);
        });

    if (flush) {
        while (!m_buffer.empty()) {
            BufferData &front = m_buffer.front();
            m_process_output += front.raw + "\n";
            // keep the emitted fan state in sync when flushing buffered fan commands
            if (front.fan_speed >= 0)
                m_front_buffer_fan_speed = front.fan_speed;
            remove_from_buffer(m_buffer.begin());
        }
    }
    return m_process_output;
}

void FanMover::put_in_middle_G1(std::list<BufferData>::iterator item_to_split, float nb_sec_since_itemtosplit_start, BufferData &&line_to_write)
{
    assert(item_to_split != m_buffer.end());
    if (nb_sec_since_itemtosplit_start > item_to_split->time * 0.9) {
        // doesn't really need to be split, print it after
        m_buffer.insert(next(item_to_split), line_to_write);
    } else if (nb_sec_since_itemtosplit_start < item_to_split->time * 0.1) {
        // doesn't really need to be split, print it before (also if line_to_split.time == 0)
        m_buffer.insert(item_to_split, line_to_write);
    } else if (item_to_split->raw.size() > 2 && item_to_split->raw[0] == 'G' && item_to_split->raw[1] == '1' && item_to_split->raw[2] == ' ') {
        const float percent = nb_sec_since_itemtosplit_start / item_to_split->time;
        BufferData  before  = *item_to_split;
        before.time *= percent;
        item_to_split->time *= (1 - percent);
        if (item_to_split->dx != 0) {
            before.dx = item_to_split->dx * percent;
            item_to_split->x += before.dx;
            item_to_split->dx = item_to_split->dx * (1 - percent);
            change_axis_value(before.raw, 'X', before.x + before.dx, 3);
        }
        if (item_to_split->dy != 0) {
            before.dy = item_to_split->dy * percent;
            item_to_split->y += before.dy;
            item_to_split->dy = item_to_split->dy * (1 - percent);
            change_axis_value(before.raw, 'Y', before.y + before.dy, 3);
        }
        if (item_to_split->dz != 0) {
            before.dz = item_to_split->dz * percent;
            item_to_split->z += before.dz;
            item_to_split->dz = item_to_split->dz * (1 - percent);
            change_axis_value(before.raw, 'Z', before.z + before.dz, 3);
        }
        if (item_to_split->de != 0) {
            before.de = item_to_split->de * percent;
            if (relative_e) {
                change_axis_value(before.raw, 'E', before.de, 5);
                item_to_split->de = item_to_split->de * (1 - percent);
                change_axis_value(item_to_split->raw, 'E', item_to_split->de, 5);
            } else {
                item_to_split->e += before.de;
                item_to_split->de = item_to_split->de * (1 - percent);
                change_axis_value(before.raw, 'E', before.e + before.de, 5);
            }
        }
        // the first part, then line_to_write, then the rest
        m_buffer.insert(item_to_split, before);
        m_buffer.insert(item_to_split, line_to_write);
    } else {
        // not a G1, print it before
        m_buffer.insert(item_to_split, line_to_write);
    }
}

void FanMover::print_in_middle_G1(BufferData &line_to_split, float nb_sec, const std::string &line_to_write)
{
    const std::string eol = line_to_write.back() == '\n' ? "" : "\n";
    if (nb_sec < line_to_split.time * 0.1) {
        m_process_output += line_to_write + eol;
        m_process_output += line_to_split.raw + "\n";
    } else if (nb_sec > line_to_split.time * 0.9) {
        m_process_output += line_to_split.raw + "\n";
        m_process_output += line_to_write + eol;
    } else if (line_to_split.raw.size() > 2 && line_to_split.raw[0] == 'G' && line_to_split.raw[1] == '1' && line_to_split.raw[2] == ' ') {
        const float percent = nb_sec / line_to_split.time;
        std::string before  = line_to_split.raw;
        std::string &after  = line_to_split.raw;
        if (line_to_split.dx != 0)
            change_axis_value(before, 'X', line_to_split.x + line_to_split.dx * percent, 3);
        if (line_to_split.dy != 0)
            change_axis_value(before, 'Y', line_to_split.y + line_to_split.dy * percent, 3);
        if (line_to_split.dz != 0)
            change_axis_value(before, 'Z', line_to_split.z + line_to_split.dz * percent, 3);
        if (line_to_split.de != 0) {
            if (relative_e) {
                change_axis_value(before, 'E', line_to_split.de * percent, 5);
                change_axis_value(after, 'E', line_to_split.de * (1 - percent), 5);
            } else
                change_axis_value(before, 'E', line_to_split.e + line_to_split.de * percent, 5);
        }
        m_process_output += before + "\n";
        m_process_output += line_to_write + eol;
        m_process_output += line_to_split.raw + "\n";
    } else {
        // not a G1, print it before
        m_process_output += line_to_write + eol;
        m_process_output += line_to_split.raw + "\n";
    }
}

void FanMover::remove_slow_fan(int16_t min_speed, float past_sec)
{
    // Erase the slower fan commands in the buffer (don't slow down while speeding up), from the recent side,
    // as long as past_sec is not used up.
    auto it = m_buffer.begin();
    while (it != m_buffer.end() && past_sec > 0) {
        past_sec -= it->time;
        if (it->fan_speed >= 0 && it->fan_speed < min_speed)
            it = remove_from_buffer(it);
        else
            ++it;
    }
}

std::string FanMover::set_fan(int16_t speed) const
{
    return GCodeWriter::set_fan(m_writer.config.gcode_flavor, false, unsigned(speed), unsigned(std::max(0, min_pwm)));
}

void FanMover::process_gcode_line(GCodeReader &reader, const GCodeReader::GCodeLine &line)
{
    bool              need_flush = false;
    const std::string cmd(line.cmd());
    double            time      = 0;
    int16_t           fan_speed = -1;
    if (cmd.length() > 1) {
        if (line.has_f())
            m_current_speed = line.f() / 60.0f;
        switch (::toupper(cmd[0])) {
        case 'G': {
            if (::atoi(&cmd[1]) == 1 || ::atoi(&cmd[1]) == 0) {
                const double distx = line.dist_X(reader);
                const double disty = line.dist_Y(reader);
                const double distz = line.dist_Z(reader);
                double       dist  = distx * distx + disty * disty + distz * distz;
                if (dist > 0)
                    time = std::sqrt(dist) / m_current_speed;
            }
            break;
        }
        case 'M': {
            fan_speed = get_fan_speed(line.raw(), m_writer.config.gcode_flavor);
            if (fan_speed >= 0) {
                const auto fan_baseline = 255.0;
                fan_speed = int16_t(100 * fan_speed / fan_baseline);
                // speed change: stop kickstart reverting if any
                m_current_kickstart.time = -1;
                if (!m_is_custom_gcode) {
                    // slowing down: put in the queue
                    if (m_back_buffer_fan_speed < fan_speed) {
                        if (nb_seconds_delay > 0 && (!only_overhangs || m_overhang)) {
                            // this M106 needs to go in the past, not in the queue
                            time = -1;
                            if (kickstart > 0 && fan_speed > m_front_buffer_fan_speed) {
                                // stop the current kickstart, it's not relevant anymore
                                if (m_current_kickstart.time > 0)
                                    m_current_kickstart.time = -1;
                                // erase everything lower than this value, then everything lower than the kickstart
                                remove_slow_fan(fan_speed, m_buffer_time_size + 1);
                                remove_slow_fan(int16_t(fan_baseline), kickstart);
                                if (!m_buffer.empty() && (m_buffer_time_size - m_buffer.front().time * 0.1) > nb_seconds_delay) {
                                    print_in_middle_G1(m_buffer.front(), m_buffer_time_size - nb_seconds_delay, set_fan(100));
                                    remove_from_buffer(m_buffer.begin());
                                } else
                                    m_process_output += set_fan(100);
                                // write it in the queue if possible
                                const float kickstart_duration = kickstart * float(fan_speed - m_front_buffer_fan_speed) / 100.f;
                                float       time_count         = kickstart_duration;
                                auto        it                 = m_buffer.begin();
                                while (it != m_buffer.end() && time_count > 0) {
                                    time_count -= it->time;
                                    if (time_count < 0) {
                                        put_in_middle_G1(it, it->time + time_count, BufferData(std::string(line.raw()), 0, fan_speed, true));
                                        break;
                                    }
                                    ++it;
                                }
                                if (time_count > 0) {
                                    // can't place it in the buffer, use m_current_kickstart
                                    m_current_kickstart.fan_speed = fan_speed;
                                    m_current_kickstart.time      = time_count;
                                    m_current_kickstart.raw       = line.raw();
                                }
                                m_front_buffer_fan_speed = fan_speed;
                            } else {
                                // erase everything lower than this value, then write the fan command
                                remove_slow_fan(fan_speed, m_buffer_time_size + 1);
                                if (!m_buffer.empty() && (m_buffer_time_size - m_buffer.front().time * 0.1) > nb_seconds_delay) {
                                    print_in_middle_G1(m_buffer.front(), m_buffer_time_size - nb_seconds_delay, line.raw());
                                    remove_from_buffer(m_buffer.begin());
                                } else
                                    m_process_output += line.raw() + "\n";
                                m_front_buffer_fan_speed = fan_speed;
                            }
                        } else {
                            if (kickstart <= 0) {
                                // nothing to do: printed from the buffer as the other lines
                            } else if (m_current_kickstart.time > 0) {
                                if (m_back_buffer_fan_speed >= fan_speed) {
                                    // stop kickstart, this is printed just after as time >= 0
                                    m_current_kickstart.time = -1;
                                } else {
                                    // add some duration to the kickstart and use it for this line
                                    const float kickstart_duration = kickstart * float(fan_speed - m_back_buffer_fan_speed) / 100.f;
                                    m_current_kickstart.fan_speed = fan_speed;
                                    m_current_kickstart.time += kickstart_duration;
                                    m_current_kickstart.raw = line.raw();
                                    time = -1;
                                }
                            } else if (m_back_buffer_fan_speed < fan_speed - 10) { // only kickstart for more than a 10% change
                                // delay this line, kickstart at full speed first
                                time = -1;
                                const float kickstart_duration = kickstart * float(fan_speed - m_back_buffer_fan_speed) / 100.f;
                                put_in_buffer(BufferData(set_fan(100), 0, fan_speed, true));
                                m_current_kickstart.fan_speed = fan_speed;
                                m_current_kickstart.time      = kickstart_duration;
                                m_current_kickstart.raw       = line.raw();
                            }
                        }
                    }
                    m_back_buffer_fan_speed = fan_speed;
                } else {
                    // flush the buffer to avoid erasing a fan command
                    need_flush = true;
                }
            }
            break;
        }
        }
    } else if (!line.raw().empty() && line.raw().front() == ';') {
        // the role of the next extrusions
        if (line.raw().rfind(";TYPE:", 0) == 0)
            m_overhang = line.raw().substr(6) == gcode_extrusion_role_to_string(GCodeExtrusionRole::OverhangPerimeter);
        if (line.raw().rfind(";_FAN_MOVER_SKIP_", 0) == 0) {
            // internal markers: read here, then dropped from the output
            m_is_custom_gcode = line.raw().rfind(";_FAN_MOVER_SKIP_START", 0) == 0;
            time = -1;
        }
    }

    if (time >= 0) {
        BufferData &new_data = put_in_buffer(BufferData(line.raw(), float(time), fan_speed));
        if (line.has_x()) {
            new_data.x  = reader.x();
            new_data.dx = line.dist_X(reader);
        }
        if (line.has_y()) {
            new_data.y  = reader.y();
            new_data.dy = line.dist_Y(reader);
        }
        if (line.has_z()) {
            new_data.z  = reader.z();
            new_data.dz = line.dist_Z(reader);
        }
        if (line.has_e()) {
            new_data.e  = reader.e();
            new_data.de = relative_e ? line.e() : line.dist_E(reader);
        }
        if (m_current_kickstart.time > 0 && time > 0) {
            m_current_kickstart.time -= float(time);
            if (m_current_kickstart.time < 0)
                put_in_middle_G1(prev(m_buffer.end()), float(time) + m_current_kickstart.time,
                                 BufferData{m_current_kickstart.raw, 0, m_current_kickstart.fan_speed, true});
        }
        // if the buffer is too big, flush it
        while (!m_buffer.empty() && (need_flush || m_buffer_time_size - m_buffer.front().time > nb_seconds_delay - EPSILON)) {
            BufferData &frontdata = m_buffer.front();
            if (frontdata.fan_speed < 0 || frontdata.fan_speed != m_front_buffer_fan_speed || frontdata.is_kickstart) {
                if (frontdata.is_kickstart && frontdata.fan_speed < m_front_buffer_fan_speed) {
                    // slow down, not kickstart: rewrite the fan speed
                    m_process_output += set_fan(frontdata.fan_speed);
                    m_front_buffer_fan_speed = frontdata.fan_speed;
                } else {
                    m_process_output += frontdata.raw + "\n";
                    if (frontdata.fan_speed >= 0)
                        m_front_buffer_fan_speed = frontdata.fan_speed;
                }
            }
            remove_from_buffer(m_buffer.begin());
        }
    }
}

} // namespace Slic3r
