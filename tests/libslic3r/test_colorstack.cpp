#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>

#include <libslic3r/GCode/ColorStack.hpp>

using namespace Slic3r;
using namespace Slic3r::ColorStack;

// The expected plans are what ColorStack's own planner (colorstack/plan.py, commit 2522dfd) gives for the same
// layers (scratch script cs_cases.py, 2026-10-19).

namespace {

Path path(int tool, PathType type, std::vector<Vec2d> pts)
{
    Path p;
    p.tool  = tool;
    p.type  = type;
    p.width = 0.42;
    p.pts   = std::move(pts);
    return p;
}

Path square(int tool, double x, double y, double s, PathType type = PathType::OuterWall)
{
    return path(tool, type, {{x, y}, {x + s, y}, {x + s, y + s}, {x, y + s}, {x, y}});
}

Path circle(int tool, double r, PathType type)
{
    std::vector<Vec2d> pts;
    for (int k = 0; k <= 64; ++k)
        pts.emplace_back(r * std::cos(2 * M_PI * k / 64), r * std::sin(2 * M_PI * k / 64));
    return path(tool, type, pts);
}

std::vector<Path> hatch(int tool, double r)
{
    std::vector<Path> out;
    for (double y = -r + 0.45; y < r - 0.45 / 2; y += 0.45) {
        const double x = std::sqrt(r * r - y * y);
        out.push_back(path(tool, PathType::Other, {{-x, y}, {x, y}}));
    }
    return out;
}

template<class F> std::vector<Layer> make(size_t count, F per_layer)
{
    std::vector<Layer> layers;
    for (size_t i = 0; i < count; ++i) {
        Layer layer{0.2 * double(i + 1), 0.2, false, per_layer(i)};
        for (size_t s = 0; s < layer.paths.size(); ++s)
            layer.paths[s].seq = int(s);
        layers.push_back(std::move(layer));
    }
    return layers;
}

// as cs_cases.py prints it: swaps, then (first layer, layers, unit tools) per band
std::string show(const std::vector<Band> &bands)
{
    std::string out = "swaps " + std::to_string(swaps(bands, 0)) + " bands";
    for (const Band &band : bands) {
        out += " (" + std::to_string(band.first) + "," + std::to_string(band.count) + ",";
        for (const Unit &u : band.order)
            out += std::to_string(u.tool);
        out += ")";
    }
    return out;
}

const Nozzle nozzle{0.5, 45., 2., 0.2};

Options colorstack_options()
{
    Options options;
    options.overhang_tolerance = 1.; // ColorStack's centre test only
    return options;
}

} // namespace

TEST_CASE("Colour stacking: far apart colours stack up to the clear height", "[ColorStack]")
{
    const auto layers = make(10, [](size_t) { return std::vector<Path>{square(0, 0, 0, 10), square(1, 30, 0, 10)}; });
    CHECK(show(plan_bands(layers, 0, nozzle, colorstack_options())) == "swaps 1 bands (0,10,01)");
}

TEST_CASE("Colour stacking: touching colours stack as far as the edge contact allows", "[ColorStack]")
{
    const auto layers = make(6, [](size_t) { return std::vector<Path>{square(0, 0, 0, 10), square(1, 10, 0, 10)}; });
    CHECK(show(plan_bands(layers, 0, nozzle, colorstack_options())) == "swaps 3 bands (0,2,01) (2,2,10) (4,2,01)");
    Nozzle no_contact = nozzle;
    no_contact.contact = 0.;
    CHECK(show(plan_bands(layers, 0, no_contact, colorstack_options())) ==
          "swaps 6 bands (0,1,01) (1,1,10) (2,1,01) (3,1,10) (4,1,01) (5,1,10)");
}

TEST_CASE("Colour stacking: interior material is handed to the colour printed after it", "[ColorStack]")
{
    const auto layers = make(12, [](size_t) {
        std::vector<Path> paths{circle(0, 10, PathType::OuterWall), circle(1, 9.58, PathType::Other)};
        for (Path &p : hatch(2, 9.1))
            paths.push_back(std::move(p));
        return paths;
    });
    CHECK(show(plan_bands(layers, 0, nozzle, colorstack_options())) == "swaps 4 bands (0,2,012) (2,10,210)");
    CHECK(show(plan_staggered(layers, 0, nozzle, colorstack_options())) ==
          "swaps 4 bands (0,2,0) (0,10,1) (0,10,2) (10,2,2) (2,10,0) (10,2,1)");
}

TEST_CASE("Colour stacking: a colour resting on another prints after it", "[ColorStack]")
{
    const auto layers = make(10, [](size_t i) { return std::vector<Path>{square(i < 5 ? 0 : 1, 0, 0, 10)}; });
    CHECK(show(plan_bands(layers, 0, nozzle, colorstack_options())) == "swaps 1 bands (0,10,01)");
}

TEST_CASE("Colour stacking: a band never spans a pause", "[ColorStack]")
{
    auto layers = make(6, [](size_t) { return std::vector<Path>{square(0, 0, 0, 10), square(1, 30, 0, 10)}; });
    layers[3].barrier = true;
    const std::vector<Band> bands = plan_bands(layers, 0, nozzle, colorstack_options());
    REQUIRE(bands.size() == 2);
    CHECK(bands[1].first == 3);
}

TEST_CASE("Colour stacking: island units keep crossing overhangs in their colours", "[ColorStack]")
{
    // island 0: colour 0 below, colour 1 on it; island 1: colour 1 below, colour 0 on it
    auto layers = make(4, [](size_t i) {
        Path a = square(i < 2 ? 0 : 1, 0, 0, 10), b = square(i < 2 ? 1 : 0, 40, 0, 10);
        a.island = 0;
        b.island = 1;
        return std::vector<Path>{a, b};
    });
    Options options = colorstack_options();
    options.islands = true;
    const std::vector<Band> bands = plan(layers, 0, nozzle, options, false);
    REQUIRE(bands.size() == 1);
    CHECK(bands.front().ceded_mm == 0.);
    CHECK(show(bands) == "swaps 2 bands (0,4,0110)");
}

TEST_CASE("Colour stacking: island units are the islands within the band", "[ColorStack]")
{
    // a base (layers 0-1) carrying two arms; arm A colour 0 then 1, arm B colour 1 then 0 (layers 2-7). The arms join
    // at the base, but in a band above it they are separate islands.
    auto layers = make(8, [](size_t i) {
        std::vector<Path> paths;
        if (i < 2) {
            paths.push_back(square(0, 0, 0, 50));
            paths.back().slice = int(i) * 10;
        } else {
            paths.push_back(square(i < 5 ? 0 : 1, 0, 0, 10));
            paths.back().slice = int(i) * 10 + 1;
            paths.push_back(square(i < 5 ? 1 : 0, 40, 0, 10));
            paths.back().slice = int(i) * 10 + 2;
        }
        return paths;
    });
    layers[1].links = {{10, 0}};
    layers[2].links = {{21, 10}, {22, 10}};
    for (size_t i = 3; i < 8; ++i)
        layers[i].links = {{int(i) * 10 + 1, int(i - 1) * 10 + 1}, {int(i) * 10 + 2, int(i - 1) * 10 + 2}};
    layers[2].barrier = true;
    Options options = colorstack_options();
    options.islands = true;
    CHECK(show(plan(layers, 0, nozzle, options, false)) == "swaps 2 bands (0,2,0) (2,6,0110)");
}
