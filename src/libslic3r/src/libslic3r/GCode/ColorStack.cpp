#include "libslic3r/GCode/ColorStack.hpp"

// A port of colorstack/plan.py (ColorStack, commit 2522dfd). Names follow the Python; where it differs, the
// comment says so.

#include <algorithm>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <numeric>
#include <set>
#include <stdexcept>
#include <tuple>
#include <unordered_map>

#include <tbb/parallel_for.h>

namespace Slic3r::ColorStack {

namespace {

constexpr double INF = std::numeric_limits<double>::infinity();

// Samples along a path: points, segment index, t within segment (plan.py densify).
struct Sampled
{
    std::vector<Vec2d>  pts;
    std::vector<int>    seg;
    std::vector<double> ts;
};

Sampled densify(const Path &p, double step)
{
    Sampled out;
    for (size_t i = 0; i + 1 < p.pts.size(); ++i) {
        const Vec2d a = p.pts[i], b = p.pts[i + 1];
        const int   n = std::max(1, int((b - a).norm() / step));
        for (int k = 0; k < n; ++k) {
            const double t = (k + 0.5) / n;
            out.pts.push_back(a + (b - a) * t);
            out.seg.push_back(int(i));
            out.ts.push_back(t);
        }
    }
    return out;
}

// The nearest sample within a bound (plan.py uses scipy's cKDTree with distance_upper_bound): a grid searched in
// rings outward from the query's cell, ending once no nearer sample can be in the next ring.
class Tree
{
public:
    explicit Tree(const std::vector<const Sampled *> &samples)
    {
        for (const Sampled *s : samples)
            for (const Vec2d &pt : s->pts) {
                m_cells[key(cell(pt.x()), cell(pt.y()))].push_back(pt);
                m_min = m_min.cwiseMin(pt);
                m_max = m_max.cwiseMax(pt);
                ++m_size;
            }
    }
    bool   empty() const { return m_size == 0; }
    double nearest(const Vec2d &q, double bound) const
    {
        const Vec2d out = (m_min - q).cwiseMax(q - m_max).cwiseMax(Vec2d::Zero());
        if (m_size == 0 || out.squaredNorm() > bound * bound)
            return INF;
        double     best2 = INF;
        const long cx = cell(q.x()), cy = cell(q.y());
        auto visit = [&](long x, long y) {
            if (auto it = m_cells.find(key(x, y)); it != m_cells.end())
                for (const Vec2d &pt : it->second)
                    best2 = std::min(best2, (pt - q).squaredNorm());
        };
        const long rings = long(std::ceil(bound / CELL));
        for (long r = 0; r <= rings; ++r) {
            if (r == 0)
                visit(cx, cy);
            else {
                for (long x = cx - r; x <= cx + r; ++x) {
                    visit(x, cy - r);
                    visit(x, cy + r);
                }
                for (long y = cy - r + 1; y < cy + r; ++y) {
                    visit(cx - r, y);
                    visit(cx + r, y);
                }
            }
            // the cells of the next ring are at least r cells away
            if (best2 <= double(r) * CELL * double(r) * CELL)
                break;
        }
        const double best = std::sqrt(best2);
        return best <= bound ? best : INF;
    }
    std::vector<Vec2d> points() const
    {
        std::vector<Vec2d> out;
        for (const auto &[k, v] : m_cells)
            out.insert(out.end(), v.begin(), v.end());
        return out;
    }

private:
    static constexpr double CELL = 1.0;
    static long    cell(double v) { return long(std::floor(v / CELL)); }
    static int64_t key(long x, long y) { return (int64_t(x) << 32) ^ int64_t(uint32_t(y)); }
    std::unordered_map<int64_t, std::vector<Vec2d>> m_cells;
    Vec2d  m_min{INF, INF}, m_max{-INF, -INF};
    size_t m_size{0};
};

std::vector<double> nearest(const std::optional<Tree> &tree, const std::vector<Vec2d> &pts, double bound)
{
    std::vector<double> out(pts.size(), INF);
    if (tree && !tree->empty())
        for (size_t i = 0; i < pts.size(); ++i)
            out[i] = tree->nearest(pts[i], bound);
    return out;
}

std::optional<Tree> tree_of(const std::vector<const Sampled *> &samples)
{
    if (samples.empty())
        return std::nullopt;
    return Tree(samples);
}

// Split path p where the per-sample mask changes (plan.py split).
std::vector<std::pair<bool, Path>> split(const Path &p, const Sampled &s, const std::vector<bool> &mask)
{
    std::vector<std::pair<bool, Path>> out;
    std::optional<bool> cur_flag;
    std::vector<Vec2d>  cur_pts;
    auto flush = [&]() {
        if (cur_flag && cur_pts.size() > 1) {
            Path piece  = p;
            piece.pts   = cur_pts;
            piece.cache = -1;
            out.emplace_back(*cur_flag, std::move(piece));
        }
    };
    for (size_t i = 0; i + 1 < p.pts.size(); ++i) {
        std::vector<bool>   flags;
        std::vector<double> tt;
        for (size_t k = 0; k < s.seg.size(); ++k)
            if (s.seg[k] == int(i)) {
                flags.push_back(mask[k]);
                tt.push_back(s.ts[k]);
            }
        if (flags.empty())
            continue;
        // cut midway between samples whose flag differs
        std::vector<double> cuts{0.};
        std::vector<bool>   cf{flags[0]};
        for (size_t k = 1; k < flags.size(); ++k)
            if (flags[k] != flags[k - 1]) {
                cuts.push_back((tt[k] + tt[k - 1]) / 2.);
                cf.push_back(flags[k]);
            }
        cuts.push_back(1.);
        const Vec2d a0 = p.pts[i], b0 = p.pts[i + 1];
        for (size_t k = 0; k < cf.size(); ++k) {
            const Vec2d a = a0 + (b0 - a0) * cuts[k];
            const Vec2d b = a0 + (b0 - a0) * cuts[k + 1];
            if (!cur_flag || cf[k] != *cur_flag) {
                flush();
                cur_flag = bool(cf[k]);
                cur_pts  = {a};
            }
            cur_pts.push_back(b);
        }
    }
    flush();
    return out;
}

// Also hand over unflagged runs shorter than min_samples (plan.py absorb_short_runs).
std::vector<bool> absorb_short_runs(const std::vector<bool> &mask, double min_samples)
{
    std::vector<bool> out = mask;
    const size_t n = mask.size();
    size_t i = 0;
    while (i < n) {
        if (out[i]) {
            ++i;
            continue;
        }
        size_t j = i;
        while (j < n && !mask[j])
            ++j;
        if (double(j - i) < min_samples)
            std::fill(out.begin() + i, out.begin() + j, true);
        i = j;
    }
    return out;
}

// Unit normals of the samples' segments.
std::vector<Vec2d> normals(const Path &p, const Sampled &s)
{
    std::vector<Vec2d> out;
    for (int i : s.seg) {
        const Vec2d d = p.pts[i + 1] - p.pts[i];
        double n = d.norm();
        if (n == 0.)
            n = 1.;
        out.emplace_back(-d.y() / n, d.x() / n);
    }
    return out;
}

// Samples of an outer wall that lie inside the part (where two colours meet) rather than on its surface: plastic
// within ~1 mm on both sides of the line (plan.py interface_mask). Addition: with the parts' outlines, the samples
// more than a line width from them (a wall on the surface is half a line width in).
std::vector<bool> interface_mask(const Path &p, const Sampled &s, const std::optional<Tree> &layer_tree, const std::optional<Tree> &outline)
{
    std::vector<bool> out(s.pts.size(), false);
    if (!layer_tree || !p.visible() || (p.type != PathType::OuterWall && p.type != PathType::OverhangWall))
        return out;
    if (outline) {
        for (size_t k = 0; k < out.size(); ++k)
            out[k] = outline->nearest(s.pts[k], p.width) == INF;
        return out;
    }
    const std::vector<Vec2d> nrm = normals(p, s);
    std::vector<bool> sides[2] = {std::vector<bool>(s.pts.size(), false), std::vector<bool>(s.pts.size(), false)};
    for (int side = 0; side < 2; ++side) {
        const double sgn = side == 0 ? 1. : -1.;
        for (double off : {0.75, 1.25, 1.75, 2.25}) // line widths out, own line excluded
            for (size_t k = 0; k < s.pts.size(); ++k) {
                const Vec2d probe = s.pts[k] + sgn * nrm[k] * off * p.width;
                if (layer_tree->nearest(probe, p.width) < 0.45 * p.width)
                    sides[side][k] = true;
            }
    }
    for (size_t k = 0; k < out.size(); ++k)
        out[k] = sides[0][k] && sides[1][k];
    return out;
}

// Per layer, made once (plan.py Samples caches the same): its paths' samples, the layer's tree and its outer walls'
// interface samples. Thread safe; release_below() frees the layers no plan comes back to.
class Samples
{
public:
    struct LayerData
    {
        std::vector<Sampled>           samples; // per path of the layer
        std::optional<Tree>            tree;    // all the layer's paths
        std::vector<std::vector<bool>> inner;   // per path: interface_mask
        std::vector<Sampled>           outline_samples;
        std::optional<Tree>            outline; // the parts' outlines
    };
    Samples(const std::vector<Layer> &layers, double step) : m_layers(layers), m_step(step), m_data(layers.size()), m_locks(new std::mutex[layers.size()]) {}
    double step() const { return m_step; }
    const LayerData &layer(size_t j)
    {
        std::lock_guard<std::mutex> lock(m_locks[j]);
        if (!m_data[j]) {
            auto data = std::make_unique<LayerData>();
            std::vector<const Sampled *> printed;
            for (const Path &p : m_layers[j].paths) {
                data->samples.push_back(densify(p, m_step));
                if (p.tool >= 0)
                    printed.push_back(&data->samples.back());
            }
            // the pointers after the vector stopped growing
            printed.clear();
            for (size_t i = 0; i < m_layers[j].paths.size(); ++i)
                if (m_layers[j].paths[i].tool >= 0)
                    printed.push_back(&data->samples[i]);
            data->tree = tree_of(printed);
            for (const std::vector<Vec2d> &line : m_layers[j].outline) {
                Path p;
                p.pts = line;
                data->outline_samples.push_back(densify(p, m_step));
            }
            std::vector<const Sampled *> outline;
            for (const Sampled &s : data->outline_samples)
                outline.push_back(&s);
            data->outline = tree_of(outline);
            for (size_t i = 0; i < m_layers[j].paths.size(); ++i)
                data->inner.push_back(interface_mask(m_layers[j].paths[i], data->samples[i], data->tree, data->outline));
            m_data[j] = std::move(data);
        }
        return *m_data[j];
    }
    void release_below(size_t j)
    {
        for (size_t i = 0; i < std::min(j, m_data.size()); ++i)
            m_data[i].reset();
    }

private:
    const std::vector<Layer>                &m_layers;
    double                                   m_step;
    std::vector<std::unique_ptr<LayerData>>  m_data;
    std::unique_ptr<std::mutex[]>            m_locks;
};

Unit unit_of(const Path &p, const Options &opts, bool per_object)
{
    if (opts.islands)
        return {p.tool, p.island};
    return {p.tool, per_object ? p.obj : -1};
}

bool any(const std::vector<bool> &v) { return std::any_of(v.begin(), v.end(), [](bool b) { return b; }); }

struct BandResult
{
    Band                                      band;
    std::vector<std::vector<Unit>>            origin;   // per layer, per piece: the owner it started with
    std::map<Unit, std::pair<double, double>> ceded_by; // first owner -> ceded mm, visible ceded mm
};

using Work = std::vector<std::vector<std::pair<Unit, Path>>>;

// Print layers [first, first + count) as one band with the units in order; none if a visible edge would move too
// far (plan.py plan_band). given: per layer the pieces when some are printed already (owner DONE: never handed
// over, every later nozzle must clear it); starts: per unit, the band layer it starts at.
// samples: the cache, its layer offset + first + j the band's layer j.
std::optional<BandResult> plan_band(const std::vector<Layer> &layers, size_t first, size_t count, const std::vector<Unit> &order,
                                    const Nozzle &nozzle, const Options &opts, bool per_object, Samples &samples, size_t offset,
                                    const Work *given = nullptr, const std::map<Unit, size_t> *starts = nullptr)
{
    const double step = samples.step();
    const Unit   head = order.front();
    Work work;
    if (given)
        work = *given;
    else
        for (size_t j = 0; j < count; ++j) {
            work.emplace_back();
            for (size_t i = 0; i < layers[first + j].paths.size(); ++i) {
                const Path &p = layers[first + j].paths[i];
                work.back().emplace_back(p.tool < 0 ? head : unit_of(p, opts, per_object), p);
                work.back().back().second.cache = int(i);
            }
        }
    // the samples of a path of band layer j: kept while it is whole, else made here
    std::deque<Sampled> made;
    auto sampled = [&](const Path &p, size_t j) -> const Sampled & {
        if (p.cache >= 0)
            return samples.layer(offset + first + j).samples[p.cache];
        made.push_back(densify(p, step));
        return made.back();
    };
    auto tree_of_paths = [&](const std::vector<const Path *> &paths, size_t j) {
        std::vector<const Sampled *> s;
        for (const Path *p : paths)
            s.push_back(&sampled(*p, j));
        return tree_of(s);
    };
    std::vector<std::vector<Unit>> origin;
    for (const auto &w : work) {
        origin.emplace_back();
        for (const auto &op : w)
            origin.back().push_back(op.first);
    }
    double ceded = 0., vis_ceded = 0.;
    std::map<Unit, std::pair<double, double>> ceded_by;
    const double margin_mm = step; // samples lie up to step/2 off the true line
    double band_height = 0.;
    for (size_t j = 0; j < count; ++j)
        band_height += layers[first + j].height;
    const double max_need = nozzle.reach(band_height + 1.0) + margin_mm + 1.0;
    std::vector<const Samples::LayerData *> layer_data;
    for (size_t j = 0; j < count; ++j)
        layer_data.push_back(&samples.layer(offset + first + j));
    bool has_done = false;
    for (const auto &w : work)
        for (const auto &op : w)
            has_done |= op.first == DONE;
    for (size_t k = has_done ? 0 : 1; k < order.size(); ++k) {
        const Unit T = order[k];
        std::set<Unit> earlier(order.begin(), order.begin() + k);
        earlier.insert(DONE);
        const size_t start = starts && starts->count(T) ? starts->at(T) : 0;
        std::vector<std::optional<Tree>> trees_T, trees_E; // per band layer, after ceding
        for (size_t j = 0; j < count; ++j) {
            const Layer &L = layers[first + j];
            if (j < start) { // T printed this layer already
                trees_T.emplace_back();
                trees_E.emplace_back();
                continue;
            }
            std::vector<const Path *> own_paths;
            for (const auto &op : work[j])
                if (op.first == T)
                    own_paths.push_back(&op.second);
            const std::optional<Tree> own_tree = tree_of_paths(own_paths, j);
            std::vector<std::pair<Unit, Path>> new_work;
            std::vector<Unit>                  new_origin;
            for (size_t w = 0; w < work[j].size(); ++w) {
                const Unit  owner       = work[j][w].first;
                const Path &p           = work[j][w].second;
                const Unit  first_owner = origin[j][w];
                if (!earlier.count(owner)) {
                    new_work.emplace_back(owner, p);
                    new_origin.push_back(first_owner);
                    continue;
                }
                const Sampled &s = sampled(p, j);
                std::vector<double> margin(s.pts.size(), INF);
                for (size_t i = 0; i < j; ++i) {
                    if (!trees_T[i])
                        continue;
                    // layer Z, not path Z
                    const double dz = nozzle.over(L.z - layers[first + i].z);
                    if (dz <= 1e-6)
                        continue;
                    const std::vector<double> d = nearest(trees_T[i], s.pts, max_need + p.width);
                    for (size_t q = 0; q < d.size(); ++q)
                        margin[q] = std::min(margin[q], d[q] - nozzle.reach(dz) - margin_mm);
                }
                std::vector<bool> mask(s.pts.size());
                for (size_t q = 0; q < mask.size(); ++q)
                    mask[q] = margin[q] < p.width / 2.;
                if (owner == DONE) {
                    if (any(mask))
                        return std::nullopt;
                    new_work.emplace_back(owner, p);
                    new_origin.push_back(first_owner);
                    continue;
                }
                if (j > 0) {
                    // resting only on T's previous layer (printed later): hand it to T, or it prints in mid-air.
                    // Addition: besides the line centre, the bead edges (overhang_tolerance in from the edge).
                    const std::vector<Vec2d> nrm  = normals(p, s);
                    const double             edge = std::max(0., p.width / 2. - opts.overhang_tolerance);
                    for (double off : {0., edge, -edge}) {
                        std::vector<Vec2d> probe(s.pts.size());
                        for (size_t q = 0; q < probe.size(); ++q)
                            probe[q] = s.pts[q] + nrm[q] * off;
                        const std::vector<double> on_T = nearest(trees_T[j - 1], probe, p.width);
                        const std::vector<double> on_E = nearest(trees_E[j - 1], probe, p.width);
                        for (size_t q = 0; q < mask.size(); ++q)
                            if (on_T[q] < p.width / 2. && !(on_E[q] < p.width / 2.))
                                mask[q] = true;
                        if (edge <= 0.)
                            break;
                    }
                }
                if (any(mask) && opts.min_piece > 0)
                    mask = absorb_short_runs(mask, opts.min_piece / step);
                if (!any(mask)) {
                    new_work.emplace_back(owner, p);
                    new_origin.push_back(first_owner);
                    continue;
                }
                if (p.type == PathType::Support && owner.tool != T.tool)
                    return std::nullopt; // support keeps the slicer's filament (interface material is chosen on purpose)
                std::vector<bool> vis(s.pts.size(), p.visible());
                if (p.visible()) {
                    const std::vector<bool> inner = p.cache >= 0 ? layer_data[j]->inner[p.cache] :
                                                                   interface_mask(p, s, layer_data[j]->tree, layer_data[j]->outline);
                    for (size_t q = 0; q < vis.size(); ++q)
                        vis[q] = vis[q] && !inner[q];
                }
                std::vector<Vec2d> moved;
                for (size_t q = 0; q < vis.size(); ++q)
                    if (vis[q] && mask[q])
                        moved.push_back(s.pts[q]);
                if (!moved.empty()) {
                    const std::vector<double> d_own = nearest(own_tree, moved, opts.surface_tolerance + p.width + 1.0);
                    for (double d : d_own)
                        if (d - p.width / 2. > opts.surface_tolerance)
                            return std::nullopt;
                }
                const double v = double(moved.size()) * step;
                vis_ceded += v;
                auto &by = ceded_by[first_owner];
                by.second += v;
                for (auto &[flag, piece] : split(p, s, mask)) {
                    if (flag) {
                        ceded += piece.length();
                        by.first += piece.length();
                    }
                    new_work.emplace_back(flag ? T : owner, std::move(piece));
                    new_origin.push_back(first_owner);
                }
            }
            work[j]   = std::move(new_work);
            origin[j] = std::move(new_origin);
            std::vector<const Path *> t_paths, e_paths;
            for (const auto &op : work[j]) {
                if (op.first == T)
                    t_paths.push_back(&op.second);
                else if (earlier.count(op.first))
                    e_paths.push_back(&op.second);
            }
            trees_T.push_back(tree_of_paths(t_paths, j));
            trees_E.push_back(tree_of_paths(e_paths, j));
        }
    }
    BandResult result;
    result.band.first            = first;
    result.band.count            = count;
    result.band.order            = order;
    result.band.work             = std::move(work);
    result.band.ceded_mm         = ceded;
    result.band.visible_ceded_mm = vis_ceded;
    result.origin                = std::move(origin);
    result.ceded_by              = std::move(ceded_by);
    return result;
}

// The units in the order the slicer first prints them.
std::vector<Unit> units_first_seen(const std::vector<Layer> &layers, size_t first, size_t count, const Options &opts, bool per_object)
{
    std::map<Unit, int> first_seen;
    for (size_t j = 0; j < count; ++j)
        for (const Path &p : layers[first + j].paths)
            if (p.tool >= 0) {
                const Unit u = unit_of(p, opts, per_object);
                if (!first_seen.count(u))
                    first_seen[u] = p.seq;
            }
    std::vector<Unit> units;
    for (const auto &[u, s] : first_seen)
        units.push_back(u);
    std::stable_sort(units.begin(), units.end(), [&](const Unit &a, const Unit &b) { return first_seen[a] < first_seen[b]; });
    return units;
}

// Each tool's units, in the order the slicer first prints them (plan.py _units_by_tool).
std::map<int, std::vector<Unit>> units_by_tool(const std::vector<Layer> &layers, size_t first, size_t count, const Options &opts, bool per_object)
{
    std::map<int, std::vector<Unit>> out;
    for (const Unit &u : units_first_seen(layers, first, count, opts, per_object))
        out[u.tool].push_back(u);
    return out;
}

// Addition, island units: island by island in the order the slicer first prints them, each its colours in the
// order that cedes least (the loaded colour first when the island has it), chosen with the islands before it.
// The first of the results with the least (visible ceded, ceded), as a serial loop over them would pick.
std::optional<BandResult> least_ceded(std::vector<std::optional<BandResult>> &results)
{
    std::optional<BandResult> *best = nullptr;
    for (std::optional<BandResult> &b : results)
        if (b && (!best || std::make_pair(b->band.visible_ceded_mm, b->band.ceded_mm) <
                               std::make_pair((*best)->band.visible_ceded_mm, (*best)->band.ceded_mm)))
            best = &b;
    return best ? std::move(*best) : std::nullopt;
}

// Plans the band for each order in parallel.
std::vector<std::optional<BandResult>> plan_orders(const std::vector<Layer> &layers, size_t first, size_t count,
                                                   const std::vector<std::vector<Unit>> &orders, const Nozzle &nozzle,
                                                   const Options &opts, bool per_object, Samples &samples, size_t offset)
{
    std::vector<std::optional<BandResult>> results(orders.size());
    tbb::parallel_for(size_t(0), orders.size(), [&](size_t i) {
        results[i] = plan_band(layers, first, count, orders[i], nozzle, opts, per_object, samples, offset);
    });
    return results;
}

std::optional<BandResult> best_island_band(const std::vector<Layer> &layers, size_t first, size_t count, int loaded, const Nozzle &nozzle,
                                           const Options &opts, bool per_object, Samples &samples)
{
    const size_t offset = first;
    // the band's islands: its slices joined to the band's slices below they overlap (joined lower down they are
    // separate islands here)
    std::map<int, int> parent;
    for (size_t j = 0; j < count; ++j)
        for (const Path &p : layers[first + j].paths)
            if (p.slice >= 0)
                parent.emplace(p.slice, p.slice);
    auto find = [&parent](int i) {
        while (parent[i] != i)
            i = parent[i] = parent[parent[i]];
        return i;
    };
    for (size_t j = 1; j < count; ++j)
        for (const auto &[slice, below] : layers[first + j].links)
            if (parent.count(slice) && parent.count(below))
                parent[find(slice)] = find(below);
    std::vector<Layer> band(layers.begin() + first, layers.begin() + first + count);
    for (Layer &layer : band)
        for (Path &p : layer.paths)
            p.island = p.slice >= 0 ? find(p.slice) : -2 - p.island; // support: its own island
    first = 0;

    std::vector<int>                 islands;
    std::map<int, std::vector<Unit>> by_island;
    for (const Unit &u : units_first_seen(band, first, count, opts, per_object)) {
        if (!by_island.count(u.part))
            islands.push_back(u.part);
        by_island[u.part].push_back(u);
    }
    if (islands.empty())
        return plan_band(band, first, count, {Unit{loaded, -1}}, nozzle, opts, per_object, samples, offset);
    std::vector<Unit>         order;
    std::optional<BandResult> result;
    for (int island : islands) {
        std::vector<Unit> units = by_island[island];
        std::sort(units.begin(), units.end());
        const bool has_loaded = std::any_of(units.begin(), units.end(), [loaded](const Unit &u) { return u.tool == loaded; });
        std::vector<std::vector<Unit>> orders;
        do {
            if (has_loaded && units.front().tool != loaded)
                continue;
            orders.push_back(order);
            orders.back().insert(orders.back().end(), units.begin(), units.end());
        } while (std::next_permutation(units.begin(), units.end()));
        std::vector<std::optional<BandResult>> results = plan_orders(band, first, count, orders, nozzle, opts, per_object, samples, offset);
        std::optional<BandResult> best = least_ceded(results);
        if (!best)
            return std::nullopt;
        order  = best->band.order;
        loaded = order.back().tool;
        result = std::move(best);
    }
    return result;
}

// plan.py _best_band: the loaded tool first (else each tool), every order of the others; the least ceded.
std::optional<BandResult> best_band(const std::vector<Layer> &layers, size_t first, size_t count, int loaded, const Nozzle &nozzle,
                                    const Options &opts, bool per_object, Samples &samples)
{
    if (opts.islands) {
        std::optional<BandResult> b = best_island_band(layers, first, count, loaded, nozzle, opts, per_object, samples);
        if (b)
            b->band.first = first;
        return b;
    }
    const std::map<int, std::vector<Unit>> units = units_by_tool(layers, first, count, opts, per_object);
    if (units.empty())
        return plan_band(layers, first, count, {Unit{loaded, -1}}, nozzle, opts, per_object, samples, 0);
    std::vector<int> tools;
    for (const auto &tu : units)
        tools.push_back(tu.first);
    const std::vector<int> starts = units.count(loaded) ? std::vector<int>{loaded} : tools;
    std::vector<std::vector<Unit>> orders;
    for (int s : starts) {
        std::vector<int> rest;
        for (int t : tools)
            if (t != s)
                rest.push_back(t);
        do {
            orders.push_back(units.at(s));
            for (int t : rest)
                orders.back().insert(orders.back().end(), units.at(t).begin(), units.at(t).end());
        } while (std::next_permutation(rest.begin(), rest.end()));
    }
    std::vector<std::optional<BandResult>> results = plan_orders(layers, first, count, orders, nozzle, opts, per_object, samples, 0);
    return least_ceded(results);
}

int max_layers(const std::vector<Layer> &layers, const Nozzle &nozzle, const Options &opts)
{
    double h = 0.;
    for (const Layer &L : layers)
        h = std::max(h, L.height);
    return std::max(1, std::min(opts.max_band_layers, h > 0. ? int(nozzle.clear_height / h + 1e-9) : opts.max_band_layers));
}

// plan.py plan: grow each band one layer at a time while a valid plan exists, never into a barrier layer
std::vector<Band> bands_planned(const std::vector<Layer> &layers, int first_tool, const Nozzle &nozzle, const Options &opts, bool per_object)
{
    std::vector<Band> bands;
    int       loaded = first_tool;
    size_t    i      = 0;
    const int max_k  = max_layers(layers, nozzle, opts);
    Samples   samples(layers, opts.resolution * 2.);
    while (i < layers.size()) {
        samples.release_below(i);
        std::optional<BandResult> best = best_band(layers, i, 1, loaded, nozzle, opts, per_object, samples);
        if (!best)
            throw std::runtime_error("colour stacking: no valid plan for a layer");
        size_t k = 1;
        while (int(k) < max_k && i + k < layers.size() && !layers[i + k].barrier) {
            std::optional<BandResult> b = best_band(layers, i, k + 1, loaded, nozzle, opts, per_object, samples);
            if (!b)
                break;
            best = std::move(b);
            ++k;
        }
        bands.push_back(std::move(best->band));
        loaded = bands.back().order.back().tool;
        i += bands.back().count;
    }
    return bands;
}

// plan.py plan_staggered: each unit on its own schedule. A run prints one unit's next layers as far as the units
// below could still finish the window after it as a band, and no further than the next barrier layer while any
// unit is below it. Next run: the most layers for no swap, else for one swap; ties go to a unit not touching the
// one just printed. One single-unit band per run (its tower is placed by the caller).
std::vector<Band> staggered_planned(const std::vector<Layer> &layers, int first_tool, const Nozzle &nozzle, const Options &opts, bool per_object)
{
    const size_t n     = layers.size();
    const int    max_k = max_layers(layers, nozzle, opts);
    Samples      samples(layers, opts.resolution * 2.);
    Work own(n);
    std::vector<Unit> units;
    for (size_t j = 0; j < n; ++j)
        for (size_t i = 0; i < layers[j].paths.size(); ++i)
            if (const Path &p = layers[j].paths[i]; p.tool >= 0) {
                const Unit u = unit_of(p, opts, per_object);
                own[j].emplace_back(u, p);
                own[j].back().second.cache = int(i);
                if (std::find(units.begin(), units.end(), u) == units.end())
                    units.push_back(u);
            }
    std::map<Unit, size_t> front;
    for (const Unit &u : units)
        front[u] = 0;
    const double reach = nozzle.reach(nozzle.clear_height) + 1.0;
    std::map<std::pair<size_t, Unit>, std::optional<Tree>> trees;

    auto eff = [&](const Unit &u) { // first layer with material u still has to print
        size_t j = front[u];
        while (j < n && std::none_of(own[j].begin(), own[j].end(), [&](const auto &op) { return op.first == u; }))
            ++j;
        front[u] = j;
        return j;
    };
    auto tree = [&](size_t j, const Unit &u) -> const std::optional<Tree> & {
        const auto key = std::make_pair(j, u);
        auto it = trees.find(key);
        if (it == trees.end()) {
            const Samples::LayerData &data = samples.layer(j);
            std::vector<const Sampled *> paths;
            for (size_t i = 0; i < layers[j].paths.size(); ++i)
                if (const Path &p = layers[j].paths[i]; p.tool >= 0 && unit_of(p, opts, per_object) == u)
                    paths.push_back(&data.samples[i]);
            it = trees.emplace(key, tree_of(paths)).first;
        }
        return it->second;
    };
    auto touching = [&](const Unit &u, const Unit &v, size_t lo) {
        for (size_t j = lo; j < std::min(n, lo + size_t(max_k)); ++j) {
            const std::optional<Tree> &a = tree(j, u);
            const std::optional<Tree> &b = tree(j, v);
            if (a && b && !a->empty() && !b->empty())
                for (const Vec2d &pt : a->points())
                    if (b->nearest(pt, reach) < reach)
                        return true;
        }
        return false;
    };
    // u printing its layers up to b, the rest of play finishing the window after it in some order
    auto run = [&](const Unit &u, size_t lo, size_t b, const std::vector<Unit> &play, const std::map<Unit, size_t> &effs) -> std::optional<BandResult> {
        std::vector<Unit> others;
        for (const Unit &v : play)
            if (!(v == u))
                others.push_back(v);
        std::stable_sort(others.begin(), others.end(), [&](const Unit &a, const Unit &c) {
            return std::make_pair(effs.at(a), a.tool) < std::make_pair(effs.at(c), c.tool);
        });
        std::vector<int>                 group_order;
        std::map<int, std::vector<Unit>> groups;
        for (const Unit &v : others) {
            if (!groups.count(v.tool))
                group_order.push_back(v.tool);
            groups[v.tool].push_back(v);
        }
        std::map<Unit, size_t> starts;
        for (const Unit &v : play)
            starts[v] = effs.at(v) - lo;
        // the group orders as itertools.permutations gives them (lexicographic in the groups' first-seen order)
        std::vector<size_t> idx(group_order.size());
        std::iota(idx.begin(), idx.end(), 0);
        const Work window(own.begin() + lo, own.begin() + b);
        do {
            std::vector<Unit> order{u};
            for (size_t g : idx)
                order.insert(order.end(), groups[group_order[g]].begin(), groups[group_order[g]].end());
            if (std::optional<BandResult> band = plan_band(layers, lo, b - lo, order, nozzle, opts, per_object, samples, 0, &window, &starts))
                return band;
        } while (std::next_permutation(idx.begin(), idx.end()));
        return std::nullopt;
    };

    std::vector<size_t> barriers;
    for (size_t j = 0; j < n; ++j)
        if (layers[j].barrier)
            barriers.push_back(j);
    int                 loaded = first_tool;
    std::optional<Unit> last;
    size_t              top = 0; // layers below are printed by somebody
    std::vector<Band>   bands;
    struct Cand
    {
        Unit       u;
        size_t     e, b, lo;
        BandResult band;
    };
    while (true) {
        std::map<Unit, size_t>               effs;
        std::vector<std::pair<Unit, size_t>> active;
        for (const Unit &u : units) {
            effs[u] = eff(u);
            if (effs[u] < n)
                active.emplace_back(u, effs[u]);
        }
        if (active.empty())
            break;
        size_t lo_all = n;
        for (const auto &ue : active)
            lo_all = std::min(lo_all, ue.second);
        // nothing below is planned again
        samples.release_below(lo_all);
        for (auto it = trees.begin(); it != trees.end();)
            it = it->first.first < lo_all ? trees.erase(it) : std::next(it);
        size_t cap = n; // nobody prints past a barrier until all reach it
        for (size_t k : barriers)
            if (k > lo_all) {
                cap = k;
                break;
            }
        std::vector<Cand> cands;
        for (const auto &[u, e] : active) {
            std::optional<Cand> best;
            size_t b = std::max(top, e + 1);
            while (b <= cap) {
                std::vector<Unit> play;
                size_t lo = n;
                for (const auto &[v, ev] : active)
                    if (ev < b) {
                        play.push_back(v);
                        lo = std::min(lo, ev);
                    }
                if (int(b - lo) > max_k)
                    break;
                std::optional<BandResult> band = run(u, lo, b, play, effs);
                if (!band)
                    break;
                best = Cand{u, e, b, lo, std::move(*band)};
                ++b;
            }
            if (best)
                cands.push_back(std::move(*best));
        }
        if (cands.empty())
            throw std::runtime_error("colour stacking: no valid run at a layer");
        auto key = [&](const Cand &c) { return std::make_tuple(c.u.tool == loaded, c.b - c.lo, !last || !touching(c.u, *last, lo_all)); };
        size_t pick = 0; // Python max(): the first of the largest
        for (size_t c = 1; c < cands.size(); ++c)
            if (key(cands[pick]) < key(cands[c]))
                pick = c;
        Cand &c = cands[pick];
        // commit u's pieces: printed now (DONE) or handed to the unit that prints them later
        std::vector<std::vector<Path>> mine(c.b - c.lo);
        for (size_t j = 0; j < c.b - c.lo; ++j) {
            std::vector<std::pair<Unit, Path>> keep;
            for (const auto &op : own[c.lo + j])
                if (!(op.first == c.u))
                    keep.push_back(op);
            for (size_t w = 0; w < c.band.band.work[j].size(); ++w)
                if (c.band.origin[j][w] == c.u) {
                    const auto &op = c.band.band.work[j][w];
                    keep.emplace_back(op.first == c.u ? DONE : op.first, op.second);
                    if (op.first == c.u)
                        mine[j].push_back(op.second);
                }
            own[c.lo + j] = std::move(keep);
        }
        top = std::max(top, c.b);
        Band out;
        out.first = c.e; // u has nothing to print below e
        out.count = c.b - c.e;
        out.order = {c.u};
        out.work.resize(out.count);
        for (size_t j = c.e; j < c.b; ++j)
            for (const Path &p : mine[j - c.lo])
                out.work[j - c.e].emplace_back(c.u, p);
        if (auto it = c.band.ceded_by.find(c.u); it != c.band.ceded_by.end()) {
            out.ceded_mm         = it->second.first;
            out.visible_ceded_mm = it->second.second;
        }
        out.run         = true;
        out.complete_to = n;
        for (const Unit &v : units)
            out.complete_to = std::min(out.complete_to, eff(v));
        bands.push_back(std::move(out));
        loaded = c.u.tool;
        last   = c.u;
    }
    return bands;
}

} // namespace

double Path::length() const
{
    double l = 0.;
    for (size_t i = 0; i + 1 < pts.size(); ++i)
        l += (pts[i + 1] - pts[i]).norm();
    return l;
}

std::vector<int> Band::tools() const
{
    std::vector<int> seq;
    for (const Unit &u : order)
        if (seq.empty() || seq.back() != u.tool)
            seq.push_back(u.tool);
    return seq;
}

size_t swaps(const std::vector<Band> &bands, int first_tool)
{
    size_t n      = 0;
    int    loaded = first_tool;
    for (const Band &band : bands)
        for (int t : band.tools()) {
            if (t != loaded)
                ++n;
            loaded = t;
        }
    return n;
}

double purge(const std::vector<Band> &bands, int first_tool, const std::vector<std::vector<float>> &volumes)
{
    double total  = 0.;
    int    loaded = first_tool;
    for (const Band &band : bands)
        for (int t : band.tools()) {
            if (t != loaded && loaded >= 0 && size_t(loaded) < volumes.size() && size_t(t) < volumes[loaded].size())
                total += volumes[loaded][t];
            loaded = t;
        }
    return total;
}

std::vector<Band> plan_bands(const std::vector<Layer> &layers, int first_tool, const Nozzle &nozzle, const Options &options)
{
    return bands_planned(layers, first_tool, nozzle, options, options.per_object);
}

std::vector<Band> plan_staggered(const std::vector<Layer> &layers, int first_tool, const Nozzle &nozzle, const Options &options)
{
    return staggered_planned(layers, first_tool, nozzle, options, options.per_object);
}

std::vector<Band> plan(const std::vector<Layer> &layers, int first_tool, const Nozzle &nozzle, const Options &options, bool staggered)
{
    // pipeline.py: plan every way asked for that can differ. Per-object units are the tools themselves when no
    // tool spans two objects; staggering gains nothing with at most two colours a layer.
    std::set<std::pair<int, int>> tool_obj;
    std::set<int>                 tools;
    bool many_colours = false;
    for (const Layer &L : layers) {
        std::set<int> here;
        for (const Path &p : L.paths)
            if (p.tool >= 0) {
                tool_obj.insert({p.tool, p.obj});
                tools.insert(p.tool);
                here.insert(p.tool);
            }
        many_colours |= here.size() > 2;
    }
    const bool split_objects = tool_obj.size() > tools.size();
    std::vector<std::pair<bool, bool>> variants;
    for (bool s : (staggered && many_colours) ? std::vector<bool>{false, true} : std::vector<bool>{false})
        for (bool po : (options.per_object && split_objects) ? std::vector<bool>{false, true} : std::vector<bool>{false})
            variants.emplace_back(s, po);
    // ColorStack keeps the fastest by a G-code time estimate; here the least purged (the tower is most of the swap
    // time), then the fewest swaps, then the least ceded
    std::vector<std::vector<Band>> planned(variants.size());
    tbb::parallel_for(size_t(0), variants.size(), [&](size_t v) {
        const auto [s, po] = variants[v];
        planned[v] = s ? staggered_planned(layers, first_tool, nozzle, options, po) : bands_planned(layers, first_tool, nozzle, options, po);
    });
    std::optional<std::vector<Band>>             best;
    std::tuple<double, size_t, double, double> best_key;
    for (std::vector<Band> &bands : planned) {
        double vis = 0., ceded = 0.;
        for (const Band &b : bands) {
            vis += b.visible_ceded_mm;
            ceded += b.ceded_mm;
        }
        const std::tuple<double, size_t, double, double> key{purge(bands, first_tool, options.purge), swaps(bands, first_tool), vis, ceded};
        if (!best || key < best_key) {
            best     = std::move(bands);
            best_key = key;
        }
    }
    return best ? std::move(*best) : std::vector<Band>{};
}

} // namespace Slic3r::ColorStack
