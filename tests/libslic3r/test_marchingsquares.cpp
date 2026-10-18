#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <test_utils.hpp>

#include <algorithm>
#include <fstream>

#include "Slic3r/Biz/Algorithms/Polygon.hpp"
#include "Slic3r/Biz/Algorithms/ExPolygon.hpp"
#include <libslic3r/MarchingSquares.hpp>
#include <libslic3r/SLA/RasterToPolygons.hpp>

#include <libslic3r/SLA/AGGRaster.hpp>
#include <libslic3r/MTUtils.hpp>
#include <libslic3r/ClipperUtils.hpp>

#include <libslic3r/TriangleMeshSlicer.hpp>
#include <libslic3r/TriangulateWall.hpp>
#include <libslic3r/SlicesToTriangleMesh.hpp>
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"

// Orca's tests for its MarchingSquares (OrcaSlicer v2.5.0-alpha tests/libslic3r/test_marchingsquares.cpp),
// on 3.0's geometry API. Orca's debug PNG / SVG output and gyroid benchmarks are left out.

using namespace Slic3r;
using namespace Slic3r::Biz;
using namespace Catch::Matchers;

namespace BB = Biz::Algorithms::BoundingBox;

// Note this tests SLA/RasterToPolygons.hpp, SLA/AGGRaster.hpp, and
// ClipperUtils.hpp at least as much as MarchingSquares.hpp.

static Point new_scale(double x, double y) { return Point(scaled(x), scaled(y)); }

// Get the Point corresponding to a raster column and row.
static Point rstPoint(const sla::RasterGrayscaleAA& rst, const size_t c, const size_t r)
{
    size_t  rows = rst.resolution().height_px, cols = rst.resolution().width_px;
    auto    pxd   = rst.pixel_dimensions();
    auto    tr    = rst.trafo();
    coord_t width = scaled(cols * pxd.h_mm), height = scaled(rows * pxd.w_mm);
    Point   p = new_scale(c * pxd.w_mm, r * pxd.h_mm);
    // reverse the raster transformations
    if (tr.mirror_y)
        p.y() = height - p.y();
    if (tr.mirror_x)
        p.x() = width - p.x();
    p.x() -= tr.center_x;
    p.y() -= tr.center_y;
    if (tr.flipXY)
        std::swap(p.x(), p.y());
    return p;
}

// Get the size of a raster pixel in coord_t.
static Point rstPixel(const sla::RasterGrayscaleAA& rst)
{
    auto pxd = rst.pixel_dimensions();
    return new_scale(pxd.w_mm, pxd.h_mm);
}

// Get the ExPolygons directly corresponding to a raster.
static ExPolygons rstGetPolys(sla::RasterGrayscaleAA& rst)
{
    size_t   rows = rst.resolution().height_px, cols = rst.resolution().width_px;
    Polygons polys;
    for (size_t r = 0; r < rows; r++) {
        // use c0==cols as a sentinel marker for "no start column yet".
        size_t c0 = cols;
        for (size_t c = 0; c <= cols; c++) {
            if (c < cols && rst.read_pixel(c, r) > 128) {
                // We have set pixels, set the c0 start column if it is not yet set.
                if (c0 == cols)
                    c0 = c;
            } else if (c0 < cols) {
                // There is no pixel set, but we do have a c0 start column. Output a
                // "row-rectangle" poly for this row between the start column c0 and
                // the current column.
                polys.push_back({rstPoint(rst, c0, r), rstPoint(rst, c0, r + 1), rstPoint(rst, c, r + 1), rstPoint(rst, c, r)});
                // Make sure the poly is anti-clockwise, which it might not be
                // depending on how rstPoint() reverses the raster transformations
                // from (c,r) to (x,y) coordinates.
                if (Algorithms::Polygon::is_clockwise(polys.back()))
                    polys.back().reverse();
                // Clear the start column c0 for the next row-rectangle.
                c0 = cols;
            }
        }
    }
    // Merge all the row-rectangle polys into contiguous raster ExPolygons.
    return union_ex(polys);
}

// Get the length in mm of a "vector" Point.
static double len(const Point& v) { return unscaled(v.cast<double>().norm()); }
// Get the area in mm^2 of a box with corners at the origin and a Point.
static double area(const Point& v) { return unscaled(v.x()) * unscaled(v.y()); }
// The area in mm^2.
static double area_mm2(const ExPolygons& expolys) { return unscaled(unscaled(Algorithms::ExPolygon::area(expolys))); }
static double area_mm2(const ExPolygon& expoly) { return unscaled(unscaled(Algorithms::ExPolygon::area(expoly))); }

static Point bbox_center(const Polygon& polygon) { return BB::center(Algorithms::Polygon::get_bounding_box(polygon)); }

// Find the index of the nearest extracted ExPolygon for a reference ExPolygon.
static size_t find_closest_ext(const ExPolygons& exts, const ExPolygon& ref)
{
    const Point ref_center = bbox_center(ref.contour);
    auto closest = std::min_element(exts.begin(), exts.end(), [&ref_center](const ExPolygon& a, const ExPolygon& b) {
        return (bbox_center(a.contour) - ref_center).cast<double>().norm() < (bbox_center(b.contour) - ref_center).cast<double>().norm();
    });
    return size_t(std::distance(exts.begin(), closest));
}

static Slic3r::sla::RasterGrayscaleAA create_raster(const sla::Resolution& res, double disp_w = 100., double disp_h = 100.)
{
    sla::PixelDim pixdim{disp_w / res.width_px, disp_h / res.height_px};

    auto                   bb = BoundingBox({0, 0}, {scaled(disp_w), scaled(disp_h)});
    sla::RasterBase::Trafo trafo;
    trafo.center_x = BB::center(bb).x();
    trafo.center_y = BB::center(bb).y();

    return sla::RasterGrayscaleAA{res, pixdim, trafo, agg::gamma_threshold(.5)};
}

static ExPolygon square(double a, Point center = {0, 0})
{
    ExPolygon poly;
    coord_t   V = scaled(a / 2.);

    poly.contour.points = {{-V, -V}, {V, -V}, {V, V}, {-V, V}};
    poly.translate(center.x(), center.y());

    return poly;
}

static ExPolygon square_with_hole(double a, Point center = {0, 0})
{
    ExPolygon poly = square(a);

    poly.holes.emplace_back();
    coord_t V                 = scaled(a / 4.);
    poly.holes.front().points = {{-V, V}, {V, V}, {V, -V}, {-V, -V}};

    poly.translate(center.x(), center.y());

    return poly;
}

static ExPolygons circle_with_hole(double r, Point center = {0, 0})
{
    ExPolygon poly;

    std::vector<double> pis = linspace_vector(0., 2 * PI, 100);

    coord_t rs = scaled(r);
    for (double phi : pis) {
        poly.contour.points.emplace_back(rs * std::cos(phi), rs * std::sin(phi));
    }

    poly.holes.emplace_back(poly.contour);
    poly.holes.front().reverse();
    for (auto& p : poly.holes.front().points)
        p /= 2;

    poly.translate(center.x(), center.y());

    return {poly};
}

static const std::array<int, 2> W2x2 = {2, 2};
static const std::array<int, 2> W1x1 = {1, 1};

template<class Rst>
static void test_expolys(Rst&& rst, const ExPolygons& ref, std::array<int, 2> window, bool strict = true)
{
    Point  pixel_size = rstPixel(rst);
    Point  window_size{coord_t(pixel_size.x() * window[0]), coord_t(pixel_size.y() * window[1])};
    double pixel_area  = area(pixel_size);
    double pixel_len   = len(pixel_size);
    double window_area = area(window_size);

    for (const ExPolygon& expoly : ref)
        rst.draw(expoly);

    const ExPolygons bmp = rstGetPolys(rst);
    const ExPolygons ext = sla::raster_to_polygons(rst, window);

    // Note all these areas are unscaled back to mm^2.
    double raster_area    = area_mm2(bmp);
    double reference_area = area_mm2(ref);
    double extracted_area = area_mm2(ext);

    // Note that errors accumulate with each step going from the reference
    // polys to the extracted polys. The rendering of the reference polys to
    // the raster does introduce pixelization errors too. This checks for
    // acceptable errors going from reference to raster, and raster to
    // reference.
    CHECK_THAT(raster_area, WithinRel(reference_area, pixel_len * 0.05) || WithinAbs(reference_area, pixel_area));
    CHECK_THAT(extracted_area, WithinRel(raster_area, 0.05) || WithinAbs(raster_area, window_area));
    for (size_t i = 0; i < ext.size(); ++i) {
        CHECK(Algorithms::Polygon::is_counter_clockwise(ext[i].contour));
        for (auto& h : ext[i].holes)
            CHECK(Algorithms::Polygon::is_clockwise(h));
    }

    BoundingBox ref_bb;
    for (auto& expoly : ref)
        ref_bb = BB::merge(ref_bb, Algorithms::Polygon::get_bounding_box(expoly.contour));
    BoundingBox ext_bb;
    for (auto& expoly : ext)
        ext_bb = BB::merge(ext_bb, Algorithms::Polygon::get_bounding_box(expoly.contour));
    CHECK(len(BB::center(ext_bb) - BB::center(ref_bb)) < pixel_len);

    // In ambigous cases (when polygons just touch) there are multiple equally
    // valid interpretations of the raster into polygons. Although
    // MarchingSquares currently systematically selects the solution that
    // breaks them into separate polygons, that might not always be true. Also,
    // SLA/RasterToPolygons.hpp, and in particular union_ex() from
    // ClipperUtils.hpp that it uses, can and does sometimes merge them back
    // together. This means we cannot reliably make assertions about the
    // extracted number of polygons and their shapes in these cases. So we skip
    // the individual polygon checks for strict=false.
    if (strict) {
        CHECK(ext.size() == ref.size());
        for (size_t i = 0; i < ext.size(); ++i) {
            size_t j = find_closest_ext(ref, ext[i]);
            INFO("Comparing ext[" << i << "] against closest ref[" << j << "]");
            CHECK(ext[i].holes.size() == ref[j].holes.size());
            double ext_i_area = area_mm2(ext[i]);
            double ref_j_area = area_mm2(ref[j]);
            CHECK_THAT(ext_i_area, WithinRel(ref_j_area, pixel_len * 0.05) || WithinAbs(ref_j_area, window_area));
            CHECK(len(bbox_center(ext[i].contour) - bbox_center(ref[j].contour)) < pixel_len);
        }
    }
}

TEST_CASE("Empty raster should result in empty polygons", "[MarchingSquares]")
{
    sla::RasterGrayscaleAAGammaPower rst{{}, {}, {}};
    ExPolygons                       extracted = sla::raster_to_polygons(rst);
    REQUIRE(extracted.size() == 0);
}

TEST_CASE("Marching squares directions", "[MarchingSquares]")
{
    using namespace marchsq;
    Coord crd{0, 0};

    __impl::step(crd, __impl::Dir::left);
    CHECK(crd == Coord(0, -1));
    __impl::step(crd, __impl::Dir::down);
    CHECK(crd == Coord(1, -1));
    __impl::step(crd, __impl::Dir::right);
    CHECK(crd == Coord(1, 0));
    __impl::step(crd, __impl::Dir::up);
    CHECK(crd == Coord(0, 0));
    __impl::step(crd, __impl::Dir::left, 7);
    CHECK(crd == Coord(0, -7));
    __impl::step(crd, __impl::Dir::down, 7);
    CHECK(crd == Coord(7, -7));
    __impl::step(crd, __impl::Dir::right, 7);
    CHECK(crd == Coord(7, 0));
    __impl::step(crd, __impl::Dir::up, 7);
    CHECK(crd == Coord(0, 0));
    __impl::step(crd, __impl::Dir::left, -3);
    CHECK(crd == Coord(0, 3));
    __impl::step(crd, __impl::Dir::down, -3);
    CHECK(crd == Coord(-3, 3));
    __impl::step(crd, __impl::Dir::right, -3);
    CHECK(crd == Coord(-3, 0));
    __impl::step(crd, __impl::Dir::up, -3);
    CHECK(crd == Coord(0, 0));
}

TEST_CASE("Fully covered raster should result in a rectangle", "[MarchingSquares]")
{
    auto rst = create_raster({4, 4}, 4., 4.);

    ExPolygon rect = square(4);

    SECTION("Full accuracy") { test_expolys(rst, {rect}, W1x1); }

    SECTION("Half accuracy") { test_expolys(rst, {rect}, W2x2); }
}

TEST_CASE("4x4 raster with one ring", "[MarchingSquares]")
{
    sla::PixelDim pixdim{1, 1};

    // We need one additional row and column to detect edges
    sla::RasterGrayscaleAA rst{{4, 4}, pixdim, {}, agg::gamma_threshold(.5)};

    ExPolygons one = {{{1, 1}, {3, 1}, {3, 3}, {2, 3}, {2, 2}, {1, 2}}};
    for (ExPolygon& p : one)
        p.scale(scaled(1.0));
    test_expolys(rst, one, W1x1);
}

TEST_CASE("10x10 raster with two rings", "[MarchingSquares]")
{
    sla::PixelDim pixdim{1, 1};

    // We need one additional row and column to detect edges
    sla::RasterGrayscaleAA rst{{10, 10}, pixdim, {}, agg::gamma_threshold(.5)};

    SECTION("Ambiguous case with 'bd' square")
    {
        ExPolygons ac = {{{1, 1}, {3, 1}, {3, 2}, {2, 2}, {2, 3}, {1, 3}}, {{4, 4}, {2, 4}, {2, 3}, {3, 3}, {3, 2}, {4, 2}}};
        for (ExPolygon& p : ac)
            p.scale(scaled(2.0));
        test_expolys(rst, ac, W1x1, false);
    }

    SECTION("Ambiguous case with 'ac' square")
    {
        ExPolygons bd = {{{1, 4}, {1, 2}, {2, 2}, {2, 3}, {3, 3}, {3, 4}}, {{4, 1}, {4, 3}, {3, 3}, {3, 2}, {2, 2}, {2, 1}}};
        for (ExPolygon& p : bd)
            p.scale(scaled(2.0));
        test_expolys(rst, bd, W1x1, false);
    }
}

TEST_CASE("Square with hole in the middle", "[MarchingSquares]")
{
    ExPolygons inp = {square_with_hole(50.)};

    SECTION("Proportional raster, 1x1 mm pixel size, full accuracy") { test_expolys(create_raster({100, 100}, 100., 100.), inp, W1x1); }
    SECTION("Proportional raster, 1x1 mm pixel size, half accuracy") { test_expolys(create_raster({100, 100}, 100., 100.), inp, W2x2); }
    SECTION("Landscape raster, 1x1 mm pixel size, full accuracy") { test_expolys(create_raster({150, 100}, 150., 100.), inp, W1x1); }
    SECTION("Landscape raster, 1x1 mm pixel size, half accuracy") { test_expolys(create_raster({150, 100}, 150., 100.), inp, W2x2); }
    SECTION("Portrait raster, 1x1 mm pixel size, full accuracy") { test_expolys(create_raster({100, 150}, 100., 150.), inp, W1x1); }
    SECTION("Portrait raster, 1x1 mm pixel size, half accuracy") { test_expolys(create_raster({100, 150}, 100., 150.), inp, W2x2); }
    SECTION("Proportional raster, 2x2 mm pixel size, full accuracy") { test_expolys(create_raster({50, 50}, 100., 100.), inp, W1x1); }
    SECTION("Proportional raster, 2x2 mm pixel size, half accuracy") { test_expolys(create_raster({50, 50}, 100., 100.), inp, W2x2); }
    SECTION("Proportional raster, 0.5x0.5 mm pixel size, full accuracy") { test_expolys(create_raster({200, 200}, 100., 100.), inp, W1x1); }
    SECTION("Proportional raster, 0.5x0.5 mm pixel size, half accuracy") { test_expolys(create_raster({200, 200}, 100., 100.), inp, W2x2); }
}

TEST_CASE("Circle with hole in the middle", "[MarchingSquares]")
{
    test_expolys(create_raster({1000, 1000}), circle_with_hole(25.), W1x1);
}

static void recreate_object_from_rasters(const std::string &objname, float lh) {
    using Biz::Algorithms::BoundingBox::center;

    Domain::TriangleMesh mesh = load_model(objname);

    auto bb = mesh.bounding_box();
    Vec3f tr = -center(bb).cast<float>();
    mesh.translate(tr);
    bb = mesh.bounding_box();

    std::vector<ExPolygons> layers = slice_mesh_ex(mesh.its, grid(float(bb.min.z()) + lh, float(bb.max.z()), lh));

    sla::Resolution res{2560, 1440};
    double                      disp_w = 120.96;
    double                      disp_h = 68.04;

    for (ExPolygons &layer : layers) {
        auto rst = create_raster(res, disp_w, disp_h);

        for (ExPolygon &island : layer) {
            rst.draw(island);
        }

        ExPolygons layer_ = sla::raster_to_polygons(rst);

        double layera = 0., layera_ = 0.;
        for (auto &p : layer) layera += p.area();
        for (auto &p : layer_) layera_ += p.area();
        double diff = std::abs(layera_ - layera);
        REQUIRE((diff <= 0.1 * layera || diff < scaled<double>(1.) * scaled<double>(1.)));

        layer = std::move(layer_);
    }

    indexed_triangle_set out = slices_to_mesh(layers, bb.min.z(), double(lh), double(lh));

    its_write_obj(out, "out_from_rasters.obj");
}

TEST_CASE("Recreate object from rasters", "[SL1Import]") {
    recreate_object_from_rasters("frog_legs.obj", 0.05f);
}
