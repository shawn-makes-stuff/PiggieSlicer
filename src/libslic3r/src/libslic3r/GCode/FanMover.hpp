// Ported from OrcaSlicer (GCode/FanMover, from SuperSlicer): moves part cooling fan speed-ups
// fan_speedup_time seconds earlier (optionally only for overhangs), and kick-starts the fan at full speed for
// fan_kickstart seconds (scaled by the speed step) before the target speed. Runs on each layer's G-code after
// the cooling buffer; custom tool change G-code between ;_FAN_MOVER_SKIP_START / _END is a barrier.

#ifndef slic3r_GCode_FanMover_hpp_
#define slic3r_GCode_FanMover_hpp_

#include <cstdint>
#include <list>
#include <string>
#include <string_view>

#include "Slic3r/Biz/GCodeReader/GCodeReader.hpp"

namespace Slic3r {

class GCodeWriter;

class FanMover
{
public:
    FanMover(const GCodeWriter &writer, float nb_seconds_delay, bool only_overhangs, float kickstart, int min_pwm);

    // Processes the G-code, returns what can be output (all of it with flush).
    const std::string &process_gcode(const std::string &gcode, bool flush);

private:
    using GCodeReader = Biz::GCodeReader::GCodeReader;

    struct BufferData
    {
        std::string raw;
        float       time;
        int16_t     fan_speed;
        bool        is_kickstart;
        float x = 0, y = 0, z = 0, e = 0;
        float dx = 0, dy = 0, dz = 0, de = 0;
        BufferData(std::string line, float time = 0, int16_t fan_speed = 0, bool is_kickstart = false)
            : raw(std::move(line)), time(time), fan_speed(fan_speed), is_kickstart(is_kickstart)
        {
            // avoid double \n
            if (!raw.empty() && raw.back() == '\n')
                raw.pop_back();
        }
    };

    const float nb_seconds_delay;
    const bool  relative_e;
    const bool  only_overhangs;
    const float kickstart;
    const int   min_pwm;

    GCodeReader        m_parser{};
    const GCodeWriter &m_writer;

    // current values (at the back of the buffer), when parsing a new line
    bool   m_overhang = false;
    // in unit/second
    double m_current_speed   = 1000 / 60.0;
    bool   m_is_custom_gcode = false;

    // fan speed at the front of the buffer (output) and at its back (parsed)
    int        m_front_buffer_fan_speed = 0;
    int        m_back_buffer_fan_speed  = 0;
    BufferData m_current_kickstart{"", -1, 0};

    std::list<BufferData> m_buffer;
    double                m_buffer_time_size = 0;

    std::string m_process_output;

    BufferData &put_in_buffer(BufferData &&data)
    {
        m_buffer_time_size += data.time;
        m_buffer.emplace_back(data);
        return m_buffer.back();
    }
    std::list<BufferData>::iterator remove_from_buffer(std::list<BufferData>::iterator data)
    {
        m_buffer_time_size -= data->time;
        return m_buffer.erase(data);
    }
    void        process_gcode_line(GCodeReader &reader, const GCodeReader::GCodeLine &line);
    void        put_in_middle_G1(std::list<BufferData>::iterator item_to_split, float nb_sec, BufferData &&line_to_write);
    void        print_in_middle_G1(BufferData &line_to_split, float nb_sec, const std::string &line_to_write);
    void        remove_slow_fan(int16_t min_speed, float past_sec);
    std::string set_fan(int16_t speed) const;
};

} // namespace Slic3r

#endif // slic3r_GCode_FanMover_hpp_
