#ifndef slic3r_ConnectedBodies_hpp_
#define slic3r_ConnectedBodies_hpp_

#include <cstddef>
#include <functional>
#include <vector>

#include "libslic3r/ExPolygon.hpp"

namespace Slic3r {

// Orca: the connected body of each island of each layer: islands of adjacent layers whose slices overlap are one body.
// Bodies are numbered from 0 in the order of their first island.
std::vector<std::vector<size_t>> connected_bodies(const std::vector<const ExPolygons *> &layers, size_t &count,
                                                  const std::function<void()> &throw_if_canceled = nullptr);

} // namespace Slic3r

#endif // slic3r_ConnectedBodies_hpp_
