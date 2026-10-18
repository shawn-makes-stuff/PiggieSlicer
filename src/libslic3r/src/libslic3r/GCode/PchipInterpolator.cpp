#include "libslic3r/GCode/PchipInterpolator.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace Slic3r {

PchipInterpolator::PchipInterpolator(const std::vector<double> &x, const std::vector<double> &y)
{
    if (x.size() != y.size() || x.size() < 2)
        throw std::invalid_argument("Input vectors must have the same size and contain at least two points.");
    std::vector<std::pair<double, double>> data;
    for (size_t i = 0; i < x.size(); ++i)
        data.emplace_back(x[i], y[i]);
    std::sort(data.begin(), data.end());
    for (const auto &[xi, yi] : data) {
        m_x.push_back(xi);
        m_y.push_back(yi);
    }

    const size_t n = m_x.size() - 1;
    m_h.resize(n);
    m_delta.resize(n);
    m_d.resize(n + 1);
    for (size_t i = 0; i < n; ++i) {
        m_h[i]     = m_x[i + 1] - m_x[i];
        m_delta[i] = (m_y[i + 1] - m_y[i]) / m_h[i];
    }
    m_d[0] = m_delta[0];
    m_d[n] = m_delta[n - 1];
    for (size_t i = 1; i < n; ++i) {
        if (m_delta[i - 1] * m_delta[i] > 0) {
            const double w1 = 2 * m_h[i] + m_h[i - 1];
            const double w2 = m_h[i] + 2 * m_h[i - 1];
            m_d[i] = (w1 + w2) / (w1 / m_delta[i - 1] + w2 / m_delta[i]);
        } else
            m_d[i] = 0;
    }
}

double PchipInterpolator::interpolate(double xi) const
{
    if (xi <= m_x.front())
        return m_y.front();
    if (xi >= m_x.back())
        return m_y.back();

    const size_t i  = size_t(std::lower_bound(m_x.begin(), m_x.end(), xi) - m_x.begin()) - 1;
    const double h  = m_h[i];
    const double t  = (xi - m_x[i]) / h;
    const double t2 = t * t;
    const double t3 = t2 * t;
    return (2 * t3 - 3 * t2 + 1) * m_y[i] + (t3 - 2 * t2 + t) * h * m_d[i] + (-2 * t3 + 3 * t2) * m_y[i + 1] +
        (t3 - t2) * h * m_d[i + 1];
}

} // namespace Slic3r
