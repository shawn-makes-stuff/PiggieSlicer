#ifndef slic3r_Brim_hpp_
#define slic3r_Brim_hpp_

#include <map>
#include <vector>

#include "Slic3r/Domain/ObjectID.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/PrintBase.hpp"

namespace Slic3r {

class Print;

// Orca ObjectInstanceID: an instance of a print object.
struct ObjectInstanceID {
    Domain::ObjectID object_id;
    size_t           instance_id;

    bool operator==(const ObjectInstanceID &rhs) const { return object_id == rhs.object_id && instance_id == rhs.instance_id; }
    bool operator!=(const ObjectInstanceID &rhs) const { return ! (*this == rhs); }
    bool operator<(const ObjectInstanceID &rhs) const { return object_id < rhs.object_id || (object_id == rhs.object_id && instance_id < rhs.instance_id); }
};

// Orca (BBS): the brims per object instance, around those objects that have the brim enabled, in the print
// coordinates; their areas per instance and in all (islands_area, for the first layer convex hull). Returns all the
// brims.
ExtrusionEntityCollection make_brim(const Print &print, PrintTryCancel try_cancel, Domain::Polygons &islands_area,
                                    std::map<ObjectInstanceID, ExtrusionEntityCollection> &brim_by_instance,
                                    std::map<ObjectInstanceID, Domain::ExPolygons> &brim_areas_by_instance);

// Orca makeBrimInfill: the brim loops filling a brim area (in the print coordinates).
ExtrusionEntityCollection make_brim_infill(const Domain::ExPolygons &brim_area, const Print &print);

} // Slic3r

#endif // slic3r_Brim_hpp_
