#ifndef slic3r_FillConcentricInternal_hpp_
#define slic3r_FillConcentricInternal_hpp_

#include <utility>

#include "libslic3r/Fill/FillBase.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Polyline.hpp"

namespace Slic3r {

// Orca: the concentric fill of the narrow internal solid areas, by Arachne with fixed parameters
class FillConcentricInternal : public Fill
{
public:
    ~FillConcentricInternal() override = default;
    bool is_self_crossing() override { return false; }

protected:
    Fill* clone() const override { return new FillConcentricInternal(*this); };
    void _fill_surface_single(const FillParams              &params,
                              unsigned int                   thickness_layers,
                              const std::pair<float, Point> &direction,
                              ExPolygon                      expolygon,
                              ThickPolylines                &thick_polylines_out) override;
    bool no_sort() const override { return true; }
};

} // namespace Slic3r

#endif // slic3r_FillConcentricInternal_hpp_
