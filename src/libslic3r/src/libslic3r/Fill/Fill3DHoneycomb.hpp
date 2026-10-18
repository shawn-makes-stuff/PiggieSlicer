#ifndef slic3r_Fill3DHoneycomb_hpp_
#define slic3r_Fill3DHoneycomb_hpp_

#include <map>
#include <utility>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Fill/FillBase.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Polyline.hpp"

namespace Slic3r {

// Orca's 3D Honeycomb
class Fill3DHoneycomb : public Fill
{
public:
    Fill* clone() const override { return new Fill3DHoneycomb(*this); };
    ~Fill3DHoneycomb() override {}

    // note: updated 3D Honeycomb doesn't need bridge flow because the
    //       pattern is placed on top of previous layers
    bool use_bridge_flow() const override { return false; }
    bool is_self_crossing() override { return false; }
    bool aligned_to_origin() const override { return true; }

protected:
	void _fill_surface_single(
	    const FillParams                &params,
	    unsigned int                     thickness_layers,
	    const std::pair<float, Point>   &direction,
	    ExPolygon                 		 expolygon,
	    Polylines                       &polylines_out) override;
};

// 3.0's 3D Honeycomb
class Fill3DHoneycombClassic : public Fill
{
public:
    Fill* clone() const override { return new Fill3DHoneycombClassic(*this); };
    ~Fill3DHoneycombClassic() override {}

	// require bridge flow since most of this pattern hangs in air
    bool use_bridge_flow() const override { return true; }
    bool is_self_crossing() override { return false; }
    // its grid is aligned to the origin
    bool aligned_to_origin() const override { return true; }

protected:
	void _fill_surface_single(
	    const FillParams                &params, 
	    unsigned int                     thickness_layers,
	    const std::pair<float, Point>   &direction, 
	    ExPolygon                 		 expolygon,
	    Polylines                       &polylines_out) override;
};

} // namespace Slic3r

#endif // slic3r_Fill3DHoneycomb_hpp_
