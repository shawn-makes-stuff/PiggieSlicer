#pragma once
#include "Slic3r/Domain/Preset/Bundle.hpp"
#include "Slic3r/Biz/Preset/IO/BundlePaths.hpp"

#include <set>
#include <string>

namespace Slic3r::Biz::Preset::IO {

void populate_local_bundle(const BundlePaths& bundle_paths);
/// With preset_vendors, only those vendors' presets are read; the others get their vendor.yaml
/// (printers, families: the printer picker) and no presets until load_vendor_presets.
Domain::Preset::Bundle load_bundle(
    const BundlePaths& bundle_paths, const std::set<std::string>* preset_vendors = nullptr);
/// Reads the presets (system and user) of a vendor load_bundle left without them; false when
/// they cannot be read.
bool load_vendor_presets(const BundlePaths& bundle_paths, Domain::Preset::VendorBundle& vendor_bundle);

void save_bundle_configs(const Domain::Preset::Bundle& bundle, const std::string& config_path);

void serialize_bundle(
    const std::string& filename,
    const Domain::Preset::Bundle& bundle,
    const BundlePaths& bundle_paths,
    const std::string& slicer_version);
std::optional<Domain::Preset::Bundle> deserialize_bundle(
    const std::string& filename,
    const BundlePaths& bundle_paths,
    const std::string& slicer_version);

}
