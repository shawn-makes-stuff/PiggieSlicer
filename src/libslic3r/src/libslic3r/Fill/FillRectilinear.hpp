#ifndef slic3r_FillRectilinear_hpp_
#define slic3r_FillRectilinear_hpp_

#include <stddef.h>
#include <initializer_list>
#include <cstddef>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Fill/FillBase.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/ShortestPath.hpp"

namespace Slic3r {

class PrintRegionConfigView;
class Surface;

class FillRectilinear : public Fill
{
public:
    Fill* clone() const override { return new FillRectilinear(*this); }
    ~FillRectilinear() override = default;
    Polylines fill_surface(const Surface *surface, const FillParams &params) override;
    bool is_self_crossing() override { return false; }

protected:
    // Fill by single directional lines, interconnect the lines along perimeters.
	bool fill_surface_by_lines(const Surface *surface, const FillParams &params, float angleBase, float pattern_shift, Polylines &polylines_out);


    // Fill by multiple sweeps of differing directions.
    struct SweepParams {
        float angle_base;
        float pattern_shift;
    };
    bool fill_surface_by_multilines(const Surface *surface, FillParams params, const std::initializer_list<SweepParams> &sweep_params, Polylines &polylines_out);
    // Orca: the non-crossing multiline patterns; 0 = grid, 1 = triangles, 2 = tri-hexagon (stars), 3 = cubic
    bool fill_surface_trapezoidal(const Surface *surface, FillParams params, Polylines &polylines_out, int Pattern_type);
};

class FillAlignedRectilinear : public FillRectilinear
{
public:
    Fill* clone() const override { return new FillAlignedRectilinear(*this); }
    ~FillAlignedRectilinear() override = default;

protected:
    // Always generate infill at the same angle.
    virtual float _layer_angle(size_t idx) const override { return 0.f; }
};

class FillMonotonic : public FillRectilinear
{
public:
    Fill* clone() const override { return new FillMonotonic(*this); }
    ~FillMonotonic() override = default;
    Polylines fill_surface(const Surface *surface, const FillParams &params) override;
	bool no_sort() const override { return true; }
};

class FillMonotonicLines : public FillRectilinear
{
public:
    Fill* clone() const override { return new FillMonotonicLines(*this); }
    ~FillMonotonicLines() override = default;
    Polylines fill_surface(const Surface *surface, const FillParams &params) override;
    bool no_sort() const override { return true; }
};

class FillGrid : public FillRectilinear
{
public:
    Fill* clone() const override { return new FillGrid(*this); }
    ~FillGrid() override = default;
    Polylines fill_surface(const Surface *surface, const FillParams &params) override;
    bool is_self_crossing() override { return true; }

protected:
	// The grid fill will keep the angle constant between the layers, see the implementation of Slic3r::Fill.
    float _layer_angle(size_t idx) const override { return 0.f; }
};

// Orca's Lateral Lattice
class FillLateralLattice : public FillRectilinear
{
public:
    Fill* clone() const override { return new FillLateralLattice(*this); }
    ~FillLateralLattice() override = default;
    Polylines fill_surface(const Surface *surface, const FillParams &params) override;

protected:
	// The grid fill will keep the angle constant between the layers, see the implementation of Slic3r::Fill.
    float _layer_angle(size_t idx) const override { return 0.f; }
};

class FillTriangles : public FillRectilinear
{
public:
    Fill* clone() const override { return new FillTriangles(*this); }
    ~FillTriangles() override = default;
    Polylines fill_surface(const Surface *surface, const FillParams &params) override;
    bool is_self_crossing() override { return true; }

protected:
	// The grid fill will keep the angle constant between the layers, see the implementation of Slic3r::Fill.
    float _layer_angle(size_t idx) const override { return 0.f; }
};

class FillStars : public FillRectilinear
{
public:
    Fill* clone() const override { return new FillStars(*this); }
    ~FillStars() override = default;
    Polylines fill_surface(const Surface *surface, const FillParams &params) override;
    bool is_self_crossing() override { return true; }

protected:
    // The grid fill will keep the angle constant between the layers, see the implementation of Slic3r::Fill.
    float _layer_angle(size_t idx) const override { return 0.f; }
};

class FillCubic : public FillRectilinear
{
public:
    Fill* clone() const override { return new FillCubic(*this); }
    ~FillCubic() override = default;
    Polylines fill_surface(const Surface *surface, const FillParams &params) override;
    bool is_self_crossing() override { return true; }

protected:
	// The grid fill will keep the angle constant between the layers, see the implementation of Slic3r::Fill.
    float _layer_angle(size_t idx) const override { return 0.f; }
};

// Orca's Quarter Cubic
class FillQuarterCubic : public FillRectilinear
{
public:
    Fill* clone() const override { return new FillQuarterCubic(*this); }
    ~FillQuarterCubic() override = default;
    Polylines fill_surface(const Surface *surface, const FillParams &params) override;

protected:
	// The grid fill will keep the angle constant between the layers, see the implementation of Slic3r::Fill.
    float _layer_angle(size_t idx) const override { return 0.f; }
};

// Orca's Lateral Honeycomb
class FillLateralHoneycomb : public FillAlignedRectilinear
{
public:
    Fill* clone() const override { return new FillLateralHoneycomb(*this); }
    ~FillLateralHoneycomb() override = default;
    Polylines fill_surface(const Surface *surface, const FillParams &params) override;
};

class FillSupportBase : public FillRectilinear
{
public:
    Fill* clone() const override { return new FillSupportBase(*this); }
    ~FillSupportBase() override = default;
    Polylines fill_surface(const Surface *surface, const FillParams &params) override;

protected:
    // The grid fill will keep the angle constant between the layers, see the implementation of Slic3r::Fill.
    float _layer_angle(size_t idx) const override { return 0.f; }
};

class FillZigZag : public FillRectilinear
{
public:
    Fill* clone() const override { return new FillZigZag(*this); }
    ~FillZigZag() override = default;

    bool has_consistent_pattern() const override { return true; }
};

// Orca's Cross Zag: the zig zag shifted every layer
class FillCrossZag : public FillRectilinear
{
public:
    Fill *clone() const override { return new FillCrossZag(*this); }
    ~FillCrossZag() override = default;

    bool has_consistent_pattern() const override { return true; }
};

// Orca's Locked Zag: a cross zag skin locked onto a zig zag skeleton
class FillLockedZag : public FillRectilinear
{
public:
    Fill *clone() const override { return new FillLockedZag(*this); }
    ~FillLockedZag() override = default;
    LockRegionParam lock_param;

    bool has_consistent_pattern() const override { return true; }
    void set_lock_region_param(const LockRegionParam &lock_param) override { this->lock_param = lock_param;};
    void fill_surface_locked_zag(const Surface *                          surface,
                                 const FillParams &                       params,
                                 std::vector<std::pair<Polylines, Flow>> &multi_width_polyline);
};

Points sample_grid_pattern(const ExPolygon &expolygon, coord_t spacing, const BoundingBox &global_bounding_box);
Points sample_grid_pattern(const ExPolygons &expolygons, coord_t spacing, const BoundingBox &global_bounding_box);
Points sample_grid_pattern(const Polygons &polygons, coord_t spacing, const BoundingBox &global_bounding_box);

} // namespace Slic3r

#endif // slic3r_FillRectilinear_hpp_
