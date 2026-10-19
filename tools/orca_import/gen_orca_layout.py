"""Generate src/slic3r-domain/src/Slic3r/Domain/OrcaLayout.cpp: Orca's Process tab layout for the sidebar.

For each 3.0 print setting shown in the sidebar: Orca's page, group and row order (Tab.cpp TabPrint::build)
and Orca's option mode (PrintConfig.cpp def->mode; Orca's default is comSimple). 3.0 keys come from Orca's
through the converter's mapping (mapping.MAP, box "print") or the same name. 3.0 settings Orca does not show
on its Process tab are placed by ANCHORS next to an Orca setting, or else in a group of their 3.0 option
group at the end of the page their 3.0 category is on.

  python gen_orca_layout.py --orca-src <Orca src dir with slic3r/GUI/Tab.cpp and libslic3r/PrintConfig.cpp>
"""
import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import audit_tabs  # noqa: E402
import mapping  # noqa: E402

ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
# the FDM definitions and the ones shared with SLA (layer height, ...)
DEFS = [os.path.join(ROOT, "src", "slic3r-domain", "src", "Slic3r", "Domain", name)
        for name in ("ConfigDefsFDM.cpp", "ConfigCommon.cpp")]
OUT = os.path.join(ROOT, "src", "slic3r-domain", "src", "Slic3r", "Domain", "OrcaLayout.cpp")

PAGES = ["Quality", "Strength", "Speed", "Support", "Multimaterial", "Others"]
PAGE_ALIASES = {"Dependencies": "Others"}
# 3.0 category -> page, as the sidebar showed them before (PrintTabListView print_tabs)
CATEGORY_PAGE = {
    "Print_LayersSurfaces": "Quality", "Print_ExtrusionRetraction": "Quality", "Print_PrecisionSlicing": "Quality",
    "Print_WallsPerimeters": "Strength", "Print_Infill": "Strength",
    "Print_Speed": "Speed", "Print_MotionDynamics": "Speed",
    "Print_Supports": "Support", "Print_MultiMaterial": "Multimaterial",
    "Print_BedAdhesion": "Others", "Print_CustomGCode": "Others", "Print_OutputOptions": "Others",
    "Print_Notes": "Others",
}
MODES = {"comSimple": "Simple", "comAdvanced": "Advanced", "comExpert": "Expert", "comDevelop": "Expert"}

# 3.0 settings Orca does not show on its Process tab: (how, Orca key, mode). "as" = converted from that
# Orca key (its place and mode), "after" / "before" = next to it, mode None = Advanced.
ANCHORS = {
    "support_material": ("as", "enable_support", None),
    "support_material_style": ("as", "support_type", None),
    "support_material_pattern": ("as", "support_base_pattern", None),
    "ironing": ("before", "ironing_type", None),
    "min_layer_height": ("after", "initial_layer_print_height", None),
    "max_layer_height": ("after", "initial_layer_print_height", None),
    "automatic_extrusion_widths": ("after", "bridge_line_width", None),
    "scarf_seam_max_segment_length": ("after", "seam_slope_steps", None),
    "gcode_resolution": ("after", "resolution", None),
    "avoid_crossing_curled_overhangs": ("after", "max_travel_detour_distance", None),
    "extra_perimeters": ("after", "extra_perimeters_on_overhangs", None),
    "infill_every_layers": ("after", "infill_combination_max_layer_height", None),
    "solid_infill_every_layers": ("after", "infill_combination_max_layer_height", None),
    "first_layer_speed_over_raft": ("after", "initial_layer_travel_speed", None),
    "over_bridge_speed": ("after", "internal_bridge_speed", None),
    "travel_speed_z": ("after", "travel_speed", None),
    "first_layer_acceleration_over_raft": ("after", "initial_layer_travel_acceleration", None),
    "travel_short_distance_acceleration": ("after", "travel_acceleration", None),
    "wipe_tower_acceleration": ("after", "travel_acceleration", None),
    "wipe_tower_auto_size": ("after", "prime_tower_brim_width", "Simple"),
    "max_print_speed": ("after", "extrusion_rate_smoothing_external_perimeter_only", None),
    "max_volumetric_speed": ("after", "extrusion_rate_smoothing_external_perimeter_only", None),
    "support_material_first_layer_density": ("after", "raft_first_layer_density", None),
    "support_material_first_layer_expansion": ("after", "raft_first_layer_expansion", None),
    "support_material_with_sheath": ("after", "tree_support_wall_count", None),
    "support_material_closing_radius": ("after", "support_base_pattern_spacing", None),
    "support_material_synchronize_layers": ("after", "independent_support_layer_height", None),
    "support_tree_branch_diameter_double_wall": ("after", "tree_support_branch_diameter_angle", None),
    "bed_temperature_extruder": ("after", "wipe_tower_filament", None),
    "gcode_substitutions": ("after", "post_process", None),
    "custom_parameters_print": ("after", "post_process", None),
}


def orca_modes(print_config_cpp):
    src = open(print_config_cpp, encoding="utf-8").read()
    modes = {}
    for m in re.finditer(r'this->add(?:_nullable)?\(\s*"([a-z0-9_]+)"(.*?)(?=this->add(?:_nullable)?\(|\Z)', src, re.S):
        mm = re.search(r"->mode\s*=\s*(com\w+)", m.group(2))
        modes[m.group(1)] = MODES[mm.group(1) if mm else "comSimple"]
    return modes


def our_defs():
    src = "".join(open(path, encoding="utf-8").read() for path in DEFS)
    defs = {}
    for m in re.finditer(r'defs\.add\("(\w+)",[^;]*;(.*?)(?=defs\.add\(|\Z)', src, re.S):
        body = m.group(2)
        get = lambda p: (re.search(p, body) or [None, ""])[1]  # noqa: E731
        # every location assignment (ConfigCommon.cpp sets one per technology)
        locs = re.findall(r"->location\s*=\s*(?:\w+::)?(\w+)", body)
        defs[m.group(1)] = {"loc": "print" if any(l.lower() == "print" for l in locs) else "", "cat": get(r"Category::(\w+)"),
                            "grp": get(r"OptionGroup::(\w+)"), "order": int(get(r"->order\s*=\s*(-?\d+)") or 0)}
    return defs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--orca-src", required=True)
    args = ap.parse_args()
    rows = [r for r in audit_tabs.orca_settings(os.path.join(args.orca_src, "slic3r", "GUI", "Tab.cpp"))
            if r[0] == "Process"]
    modes = orca_modes(os.path.join(args.orca_src, "libslic3r", "PrintConfig.cpp"))
    defs = our_defs()
    shown = {k for k, d in defs.items() if d["loc"].lower() == "print" and d["cat"] in CATEGORY_PAGE}

    # Orca's groups in order, per page; a row's place = (group, 10 * row index)
    groups = []  # (page, label or None, 3.0 option group or None)
    orca_place = {}  # Orca key -> (group index, order, mode)
    for i, (_tab, page, group, okey) in enumerate(rows):
        page = PAGE_ALIASES.get(page, page)
        g = (page, group, None)
        if g not in groups:
            groups.append(g)
        orca_place.setdefault(okey, (groups.index(g), 10 * i, modes.get(okey, "Simple")))

    place = {}  # 3.0 key -> (group index, order, mode)
    for okey, (g, order, mode) in orca_place.items():
        targets = [t for box, t, _c in mapping.MAP.get(okey, []) if box == "print"] or [okey]
        for n, key in enumerate(targets):
            if key in shown and key not in place:
                place[key] = (g, order + n, mode)
    for key, (how, okey, mode) in ANCHORS.items():
        assert key in shown and key not in place, key
        g, order, omode = orca_place[okey]
        offset = {"as": 0, "after": 1 + defs[key]["order"] / 10000, "before": -1}[how]
        place[key] = (g, order + offset, omode if how == "as" else (mode or "Advanced"))
    for key in sorted(shown - set(place)):
        d = defs[key]
        g = (CATEGORY_PAGE[d["cat"]], None, d["grp"])
        if g not in groups:
            groups.append(g)
        place[key] = (groups.index(g), d["order"], "Advanced")

    # group ids in display order: by page, Orca's groups first, then the 3.0 groups (enum order of appearance)
    used = sorted({g for g, _o, _m in place.values()},
                  key=lambda g: (PAGES.index(groups[g][0]), groups[g][1] is None, g))
    gid = {g: n + 1 for n, g in enumerate(used)}
    entries = sorted(place.items(), key=lambda kv: (gid[kv[1][0]], kv[1][1], kv[0]))

    out = ["// Generated by tools/orca_import/gen_orca_layout.py from OrcaSlicer v2.5.0-alpha",
           "// (src/slic3r/GUI/Tab.cpp TabPrint::build, src/libslic3r/PrintConfig.cpp). Do not edit.",
           '#include "Slic3r/Domain/OrcaLayout.hpp"', "", "#include <array>", "",
           "namespace Slic3r::Domain {", "", "namespace {", "",
           "// Domain cannot reach the translation functions; the strings are marked for extraction only.",
           "constexpr std::string_view L(std::string_view s) { return s; }", "",
           "using G = ConfigItemDef::OptionGroup;", "using M = ConfigItemDef::Mode;", "",
           f"constexpr std::array<std::string_view, {len(PAGES)}> pages{{",
           *[f'    L("{p}"),' for p in PAGES], "};", "",
           f"constexpr std::array<OrcaLayoutGroup, {len(used) + 1}> groups{{",
           "    OrcaLayoutGroup{-1, {}, G::Unknown},"]
    for g in used:
        page, label, grp = groups[g]
        lab = f'L("{label}")' if label else "{}"
        out.append(f"    OrcaLayoutGroup{{{PAGES.index(page)}, {lab}, G::{grp or 'Unknown'}}},")
    out += ["};", "", "struct Placement", "{", "    std::string_view key;", "    uint16_t group;",
            "    int order;", "    M mode;", "};", "",
            f"constexpr std::array<Placement, {len(entries)}> placements{{{{"]
    for n, (key, (g, _o, mode)) in enumerate(entries):
        out.append(f'    {{"{key}", {gid[g]}, {n}, M::{mode}}},')
    out += ["}};", "", "} // namespace", "",
            "std::span<const std::string_view> orca_layout_pages() { return pages; }", "",
            "std::span<const OrcaLayoutGroup> orca_layout_groups() { return groups; }", "",
            "void apply_orca_layout(ConfigDefinitions& defs)", "{",
            "    for (const Placement& placement : placements) {",
            "        ConfigItemDef* def = defs.find(placement.key);",
            "        ASSERT(def != nullptr, \"OrcaLayout.cpp is out of date: regenerate it\");",
            "        def->orca_group = placement.group;", "        def->orca_order = placement.order;",
            "        def->mode       = placement.mode;", "    }", "}", "",
            "} // namespace Slic3r::Domain", ""]
    open(OUT, "w", encoding="utf-8", newline="\n").write("\n".join(out))
    counts = {}
    for _k, (_g, _o, m) in place.items():
        counts[m] = counts.get(m, 0) + 1
    print(f"{len(entries)} settings in {len(used)} groups; modes {counts} -> {OUT}")


if __name__ == "__main__":
    main()
