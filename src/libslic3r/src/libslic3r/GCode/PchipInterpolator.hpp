#ifndef slic3r_GCode_PchipInterpolator_hpp_
#define slic3r_GCode_PchipInterpolator_hpp_

#include <vector>

namespace Slic3r {

// Piecewise Cubic Hermite Interpolating Polynomial over (x, y) points (Orca PchipInterpolatorHelper).
class PchipInterpolator
{
public:
    PchipInterpolator() = default;
    // Throws std::invalid_argument for less than two points.
    PchipInterpolator(const std::vector<double> &x, const std::vector<double> &y);
    double interpolate(double xi) const;

private:
    std::vector<double> m_x, m_y, m_h, m_delta, m_d;
};

} // namespace Slic3r

#endif // slic3r_GCode_PchipInterpolator_hpp_
