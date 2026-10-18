#ifndef slic3r_GCode_OrderingStrategies_hpp_
#define slic3r_GCode_OrderingStrategies_hpp_

#include <cstddef>
#include <vector>

#include "Slic3r/Domain/ConfigDefsFDM.hpp"
#include "libslic3r/Point.hpp"

// Orca print_order (GCode/OrderingStrategies): the orders of the object instances and of the islands in a layer.
namespace Slic3r {

class Print;
class PrintInstance;

// 2-opt improvement: reverses segments that reduce the total cycle length. Returns true if any improvement was made.
bool tsp_2opt_improve(std::vector<size_t> &path, const Points &centers, int max_passes = 10);
// Crossing removal: reverses any segment pair whose edges cross. Returns true if any crossing was removed.
bool tsp_remove_crossings(std::vector<size_t> &path, const Points &centers);
// Rotates the cycle so the closing edge (last -> first) is the shortest.
void tsp_rotate_minimize_closing(std::vector<size_t> &path, const Points &centers);

// Nearest neighbor chaining, then 2-opt and crossing removal; without start_near the closing edge is minimized.
std::vector<size_t> chain_points_with_postprocessing(const Points &points, const Point *start_near = nullptr);
// Snake ordering: row grouping, serpentine traversal, then 2-opt and crossing removal.
std::vector<size_t> snake_core(const Points &centers);

// The instances by the snake order / by the shorter of the default and the snake orders.
std::vector<const PrintInstance*> chain_print_object_instances_snake(const Print &print);
std::vector<const PrintInstance*> chain_print_object_instances_best_of(const Print &print);

// The points by the order (the island level counterpart of the above), the cycle starting at the point closest to
// start_near; the orders without a dedicated strategy chain by the nearest neighbor.
std::vector<size_t> order_points_with_strategy(const Points &points, Domain::PrintOrder print_order, const Point *start_near);

} // namespace Slic3r

#endif // slic3r_GCode_OrderingStrategies_hpp_
