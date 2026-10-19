"""Orca (v2.5.0-alpha) -> PiggieSlicer 3.0 setting mapping table.

Every Orca setting key a converted preset carries is either
  * mapped by MAP (target box + 3.0 key + value conversion),
  * consumed by the converter's structure (STRUCTURAL: compatibility, nozzle, bed type...),
  * or dropped and listed in the report (DROP gives the reason; anything else is
    reported as "no 3.0 equivalent").

Target boxes are the 3.0 preset kinds: printer, tool (kind tool_print, Tool overrides of
Print keys), print, filament. Enum translations are written out value by value; an Orca
enum value missing from a translation is dropped and reported, never guessed.

Sources: Orca src/libslic3r/PrintConfig.cpp (enum key maps, ratio_over) at v2.5.0-alpha;
3.0 key list/types/enums from `piggie-slicer --export-config-schema` (config_schema.json),
ratio_over from src/slic3r-domain/src/Slic3r/Domain/ConfigDefsFDM.cpp.
"""


class Skip(Exception):
    """Value cannot be converted; message goes to the report."""


def first(v):
    """Orca stores per-extruder/per-variant settings as lists; 3.0 presets take slot 0."""
    if isinstance(v, list):
        if not v:
            raise Skip("empty list")
        return v[0]
    return v


def is_nil(v):
    if isinstance(v, list):
        return bool(v) and v[0] in ("nil", None)
    return v in ("nil", None)


def num(v):
    s = str(first(v)).strip()
    if s.endswith("%"):
        raise Skip(f"percentage {s!r} where an absolute number is required")
    try:
        x = float(s)
    except ValueError:
        raise Skip(f"not a number: {s!r}")
    return int(x) if x.is_integer() else x


def integer(v):
    x = num(v)
    if isinstance(x, float):
        if not x.is_integer():
            raise Skip(f"not an integer: {x}")
        x = int(x)
    return x


def boolean(v):
    s = str(first(v)).strip().lower()
    if s in ("1", "true"):
        return True
    if s in ("0", "false"):
        return False
    raise Skip(f"not a bool: {s!r}")


def filament_id(v):
    """Orca's 'filament for feature' (1-based; 0 = default) to a 3.0 extruder (1-based, no 0)."""
    x = integer(v)
    if x == 0:
        raise Skip("0 = default filament (3.0 keeps its default)")
    return x


def not_bool(v):
    return not boolean(v)


def text(v):
    v = first(v)
    return "" if v is None else str(v)


def percent(v):
    """3.0 Percentage: Orca writes '15%' (or a bare number meaning percent)."""
    s = str(first(v)).strip()
    x = float(s[:-1] if s.endswith("%") else s)
    x = int(x) if x.is_integer() else x
    return f"{x}%"


def float_or_percent(v):
    s = str(first(v)).strip()
    if s.endswith("%"):
        return percent(s)
    return num(s)


def non_negative(v):
    """Orca: a negative value means "auto" (e.g. prime_tower_brim_width); 3.0 has no auto."""
    x = num(v)
    if x < 0:
        raise Skip(f"{x} = Orca 'auto', no 3.0 equivalent")
    return x


def percent_only(v):
    """3.0 Percentage key whose Orca counterpart may also be in mm: only '%' values carry over."""
    s = str(first(v)).strip()
    if not s.endswith("%"):
        if s and float(s) == 0:
            return "0%"  # 0 mm == 0 %
        raise Skip(f"absolute value {s!r}; 3.0 takes only a percentage")
    return percent(s)


def float_list(v):
    if not isinstance(v, list):
        v = [v]
    return [float(num(x)) for x in v]


def vec2_list(v):
    """'0x0' strings, kept as Orca writes them (3.0 YAML Vec2d syntax is the same)."""
    out = []
    for p in (v if isinstance(v, list) else [v]):
        parts = str(p).split("x")
        if len(parts) != 2:
            raise Skip(f"not a point: {p!r}")
        out.append(f"{num(parts[0])}x{num(parts[1])}")
    return out


def enum(table):
    def conv(v):
        s = str(first(v))
        if s not in table:
            raise Skip(f"Orca value {s!r} has no 3.0 equivalent")
        return table[s]
    conv.table = table
    return conv


def plus_of(other_key):
    """3.0 value = this Orca value + another Orca key's value (0 when the preset does not set it)."""
    def conv(v, cfg):
        x = num(v) + (num(cfg[other_key]) if other_key in cfg else 0)
        return round(x, 6) if not float(x).is_integer() else int(x)
    conv.needs_cfg = True
    conv.how = f"+ Orca {other_key}"
    return conv


def filament_change_time(v, cfg):
    """3.0's filament_change_time is the time of one filament change (unload + load); Orca splits it
    into machine_unload_filament_time + machine_load_filament_time (+ machine_tool_change_time when the
    printer has several extruders, GCodeProcessor::process_filament_change)."""
    t = num(cfg.get("machine_load_filament_time", 0)) + num(cfg.get("machine_unload_filament_time", 0))
    if len(cfg.get("nozzle_diameter", [0])) > 1:
        t += num(cfg.get("machine_tool_change_time", 0))
    return round(t, 6) if not float(t).is_integer() else int(t)


filament_change_time.needs_cfg = True
filament_change_time.how = "Orca load + unload (+ tool change) time"


def point(v):
    """Orca coPoint "x,y" -> 3.0 point "XxY"."""
    s = str(first(v)).strip()
    x, y = (float(t) for t in s.replace("x", ",").split(","))
    return f"{x:g}x{y:g}"


def bridge_fan_speed(v, cfg):
    """Orca's overhang fan speed is also its external bridge fan speed, when the overhang / bridge fan is on."""
    on = str(first(cfg.get("enable_overhang_bridge_fan", "1"))).strip().lower() in ("1", "true")
    return integer(v) if on else 0


bridge_fan_speed.needs_cfg = True
bridge_fan_speed.how = "Orca overhang_fan_speed when enable_overhang_bridge_fan, else 0"


def percent_of(base_key):
    """Orca value may be '%' of another Orca key; 3.0 key is absolute -> resolve against the
    base value of the same resolved preset."""
    def conv(v, cfg):
        s = str(first(v)).strip()
        if not s.endswith("%"):
            return num(s)
        if base_key not in cfg:
            raise Skip(f"{s} of {base_key}, which the preset does not set")
        base = num(cfg[base_key])
        x = float(s[:-1]) * base / 100.0
        return round(x, 6) if not float(x).is_integer() else int(x)
    conv.needs_cfg = True
    conv.base = base_key
    return conv


# Orca InfillPattern keys (s_keys_map_InfillPattern) -> 3.0 fill_pattern enum.
INFILL = enum({
    "rectilinear": "rectilinear", "alignedrectilinear": "alignedrectilinear", "zigzag": "zigzag",
    "line": "line", "grid": "grid", "triangles": "triangles", "tri-hexagon": "stars",
    "crosszag": "crosszag", "lockedzag": "lockedzag",
    "cubic": "cubic", "adaptivecubic": "adaptivecubic", "supportcubic": "supportcubic",
    "lightning": "lightning", "honeycomb": "honeycomb", "3dhoneycomb": "3dhoneycomb",
    "quartercubic": "quartercubic", "lateral-honeycomb": "lateral-honeycomb", "lateral-lattice": "lateral-lattice",
    "crosshatch": "crosshatch", "tpmsd": "tpmsd", "tpmsfk": "tpmsfk",
    "gyroid": "gyroid", "hilbertcurve": "hilbertcurve", "archimedeanchords": "archimedeanchords",
    "octagramspiral": "octagramspiral", "concentric": "concentric",
})
# top/bottom fill patterns in 3.0 accept a smaller set.
SURFACE = enum({
    "rectilinear": "rectilinear", "monotonic": "monotonic", "monotonicline": "monotoniclines",
    "alignedrectilinear": "alignedrectilinear", "concentric": "concentric", "spiralinset": "spiralinset",
    "hilbertcurve": "hilbertcurve", "archimedeanchords": "archimedeanchords",
    "octagramspiral": "octagramspiral",
})

GCODE_FIELDS_PRINTER = {
    "machine_start_gcode": "start_gcode",
    "machine_end_gcode": "end_gcode",
    "before_layer_change_gcode": "before_layer_gcode",
    "layer_change_gcode": "layer_gcode",
    "change_filament_gcode": "toolchange_gcode",
    "machine_pause_gcode": "pause_print_gcode",
    "template_custom_gcode": "template_custom_gcode",
    "printing_by_object_gcode": "between_objects_gcode",
}
GCODE_FIELDS_FILAMENT = {
    "filament_start_gcode": "start_filament_gcode",
    "filament_end_gcode": "end_filament_gcode",
}

# (orca key) -> list of (target box, 3.0 key, converter)
MAP = {
    # ---- machine -> printer
    "printable_area": [("printer", "bed_shape", vec2_list)],
    "printable_height": [("printer", "max_print_height", num)],
    "gcode_flavor": [("printer", "gcode_flavor", enum({k: k for k in (
        "marlin", "klipper", "reprapfirmware", "repetier", "marlin2", "reprap", "teacup",
        "makerware", "sailfish", "smoothie", "mach3", "machinekit", "no-extrusion")}))],
    "emit_machine_limits_to_gcode": [("printer", "machine_limits_usage",
                                      enum({"1": "emit_to_gcode", "0": "time_estimate_only"}))],
    "machine_max_acceleration_e": [("printer", "machine_max_acceleration_e", float_list)],
    "machine_max_acceleration_extruding": [("printer", "machine_max_acceleration_extruding", float_list)],
    "machine_max_acceleration_retracting": [("printer", "machine_max_acceleration_retracting", float_list)],
    "machine_max_acceleration_travel": [("printer", "machine_max_acceleration_travel", float_list)],
    "machine_max_acceleration_x": [("printer", "machine_max_acceleration_x", float_list)],
    "machine_max_acceleration_y": [("printer", "machine_max_acceleration_y", float_list)],
    "machine_max_acceleration_z": [("printer", "machine_max_acceleration_z", float_list)],
    "machine_max_speed_e": [("printer", "machine_max_feedrate_e", float_list)],
    "machine_max_speed_x": [("printer", "machine_max_feedrate_x", float_list)],
    "machine_max_speed_y": [("printer", "machine_max_feedrate_y", float_list)],
    "machine_max_speed_z": [("printer", "machine_max_feedrate_z", float_list)],
    "machine_max_jerk_e": [("printer", "machine_max_jerk_e", float_list)],
    "machine_max_jerk_x": [("printer", "machine_max_jerk_x", float_list)],
    "machine_max_jerk_y": [("printer", "machine_max_jerk_y", float_list)],
    "machine_max_jerk_z": [("printer", "machine_max_jerk_z", float_list)],
    "machine_max_junction_deviation": [("printer", "machine_max_junction_deviation", float_list)],
    "machine_min_extruding_rate": [("printer", "machine_min_extruding_rate", float_list)],
    "machine_min_travel_rate": [("printer", "machine_min_travel_rate", float_list)],
    "extruder_offset": [("printer", "extruder_offset", vec2_list)],
    "extruder_clearance_radius": [("printer", "extruder_clearance_radius", num)],
    # Orca "height to rod" is PrusaSlicer's carriage-rod clearance height.
    "extruder_clearance_height_to_rod": [("printer", "extruder_clearance_height", num)],
    "extruder_clearance_height_to_lid": [("printer", "extruder_clearance_height_to_lid", num)],
    "printer_notes": [("printer", "printer_notes", text)],
    "single_extruder_multi_material": [("printer", "single_extruder_multi_material", boolean)],
    "use_firmware_retraction": [("printer", "use_firmware_retraction", boolean)],
    "use_relative_e_distances": [("printer", "use_relative_e_distances", boolean)],
    "z_offset": [("printer", "z_offset", num)],
    "cooling_tube_length": [("printer", "cooling_tube_length", num)],
    "cooling_tube_retraction": [("printer", "cooling_tube_retraction", num)],
    "extra_loading_move": [("printer", "extra_loading_move", num)],
    "parking_pos_retraction": [("printer", "parking_pos_retraction", num)],
    "high_current_on_filament_swap": [("printer", "high_current_on_filament_swap", boolean)],
    # Orca emits M73 unless disable_m73; 3.0 emits M73 when remaining_times.
    "disable_m73": [("printer", "remaining_times", not_bool)],
    "default_print_profile": [("printer", "default_print", text)],
    "bed_custom_model": [("printer", "bed_custom_model", text)],
    "bed_custom_texture": [("printer", "bed_custom_texture", text)],
    # ---- machine -> tool_print (nozzle-specific; 3.0 Tool overrides of Print keys)
    "min_layer_height": [("tool", "min_layer_height", num)],
    "max_layer_height": [("tool", "max_layer_height", num)],
    "retraction_length": [("tool", "retract_length", num)],
    "retraction_speed": [("tool", "retract_speed", num)],
    "deretraction_speed": [("tool", "deretract_speed", num)],
    "retraction_minimum_travel": [("tool", "retract_before_travel", num)],
    "retract_before_wipe": [("tool", "retract_before_wipe", percent)],
    "retract_when_changing_layer": [("tool", "retract_layer_change", boolean)],
    "retract_length_toolchange": [("tool", "retract_length_toolchange", num)],
    "retract_restart_extra": [("tool", "retract_restart_extra", num)],
    "retract_restart_extra_toolchange": [("tool", "retract_restart_extra_toolchange", num)],
    "z_hop": [("tool", "retract_lift", num)],
    "retract_lift_above": [("tool", "retract_lift_above", num)],
    "retract_lift_below": [("tool", "retract_lift_below", num)],
    "retract_lift_enforce": [("tool", "retract_lift_enforce", enum({"All Surfaces": "all_surfaces", "Top Only": "top_only", "Bottom Only": "bottom_only", "Top and Bottom": "top_and_bottom"}))],
    "wipe": [("tool", "wipe", boolean)],
    "wipe_distance": [("tool", "wipe_distance", num)],
    "filament_wipe_distance": [("filament", "wipe_distance", num)],
    "make_overhang_printable": [("print", "make_overhang_printable", boolean)],
    "make_overhang_printable_angle": [("print", "make_overhang_printable_angle", num)],
    "make_overhang_printable_hole_size": [("print", "make_overhang_printable_hole_size", num)],
    "precise_outer_wall": [("print", "precise_outer_wall", boolean)],
    "precise_z_height": [("print", "precise_z_height", boolean)],
    "hole_to_polyhole": [("print", "hole_to_polyhole", boolean)],
    "hole_to_polyhole_threshold": [("print", "hole_to_polyhole_threshold", float_or_percent)],
    "hole_to_polyhole_twisted": [("print", "hole_to_polyhole_twisted", boolean)],
    "hole_to_polyhole_max_edges": [("print", "hole_to_polyhole_max_edges", integer)],
    "print_order": [("print", "print_order", enum({k: k for k in ("default", "as_obj_list", "best_of", "snake")}))],
    "wipe_inward": [("print", "wipe_inward", boolean)],
    "wipe_inward_distance": [("print", "wipe_inward_distance", float_or_percent)],
    "wipe_on_loops": [("print", "wipe_on_loops", boolean)],
    "wipe_before_external_loop": [("print", "wipe_before_external_loop", boolean)],
    # ---- process -> print
    "layer_height": [("print", "layer_height", num)],
    "initial_layer_print_height": [("print", "first_layer_height", float_or_percent)],
    "wall_loops": [("print", "perimeters", integer)],
    "top_shell_layers": [("print", "top_solid_layers", integer)],
    "bottom_shell_layers": [("print", "bottom_solid_layers", integer)],
    "top_shell_thickness": [("print", "top_solid_min_thickness", num)],
    "bottom_shell_thickness": [("print", "bottom_solid_min_thickness", num)],
    "sparse_infill_density": [("print", "fill_density", percent)],
    "sparse_infill_pattern": [("print", "fill_pattern", INFILL)],
    "top_surface_pattern": [("print", "top_fill_pattern", SURFACE)],
    "bottom_surface_pattern": [("print", "bottom_fill_pattern", SURFACE)],
    # Line widths: both apps take % of nozzle diameter (ratio_over nozzle_diameter).
    "line_width": [("print", "extrusion_width", float_or_percent)],
    "outer_wall_line_width": [("print", "external_perimeter_extrusion_width", float_or_percent)],
    "inner_wall_line_width": [("print", "perimeter_extrusion_width", float_or_percent)],
    "sparse_infill_line_width": [("print", "infill_extrusion_width", float_or_percent)],
    "internal_solid_infill_line_width": [("print", "solid_infill_extrusion_width", float_or_percent)],
    "top_surface_line_width": [("print", "top_infill_extrusion_width", float_or_percent)],
    "initial_layer_line_width": [("print", "first_layer_extrusion_width", float_or_percent)],
    "support_line_width": [("print", "support_material_extrusion_width", float_or_percent)],
    "outer_wall_speed": [("print", "external_perimeter_speed", num)],
    "inner_wall_speed": [("print", "perimeter_speed", num)],
    "sparse_infill_speed": [("print", "infill_speed", num)],
    "internal_solid_infill_speed": [("print", "solid_infill_speed", num)],
    "top_surface_speed": [("print", "top_solid_infill_speed", num)],
    "gap_infill_speed": [("print", "gap_fill_speed", num)],
    "support_speed": [("print", "support_material_speed", num)],
    "support_interface_speed": [("print", "support_material_interface_speed", num)],
    "bridge_speed": [("print", "bridge_speed", num)],
    "travel_speed": [("print", "travel_speed", num)],
    "travel_speed_z": [("print", "travel_speed_z", num)],
    "initial_layer_speed": [("print", "first_layer_speed", num)],
    "initial_layer_infill_speed": [("print", "first_layer_solid_infill_speed", num),
                                   ("print", "first_layer_infill_speed", num)],
    # Orca: % of outer_wall_speed; 3.0: % of perimeter_speed -> write absolute mm/s.
    "small_perimeter_speed": [("print", "small_perimeter_speed", percent_of("outer_wall_speed"))],
    # Both apps: % of the external/outer wall speed. Orca 1/4 overhang = 75 % overlap = 3.0 index 3.
    "overhang_1_4_speed": [("print", "overhang_speed_3", float_or_percent)],
    "overhang_2_4_speed": [("print", "overhang_speed_2", float_or_percent)],
    "overhang_3_4_speed": [("print", "overhang_speed_1", float_or_percent)],
    "overhang_4_4_speed": [("print", "overhang_speed_0", float_or_percent)],
    "enable_overhang_speed": [("print", "enable_dynamic_overhang_speeds", boolean)],
    "default_acceleration": [("print", "default_acceleration", num)],
    "outer_wall_acceleration": [("print", "external_perimeter_acceleration", num)],
    "inner_wall_acceleration": [("print", "perimeter_acceleration", num)],
    "sparse_infill_acceleration": [("print", "infill_acceleration", percent_of("default_acceleration"))],
    "internal_solid_infill_acceleration": [("print", "solid_infill_acceleration", percent_of("default_acceleration"))],
    "top_surface_acceleration": [("print", "top_solid_infill_acceleration", num)],
    "bridge_acceleration": [("print", "bridge_acceleration", percent_of("outer_wall_acceleration"))],
    "initial_layer_acceleration": [("print", "first_layer_acceleration", num)],
    "travel_acceleration": [("print", "travel_acceleration", num)],
    "seam_position": [("print", "seam_position", enum(
        {"nearest": "nearest", "aligned": "aligned", "back": "rear", "random": "random"}))],
    "seam_gap": [("print", "seam_gap_distance", float_or_percent)],
    "brim_type": [("print", "brim_type", enum({k: k for k in (
        "no_brim", "outer_only", "inner_only", "outer_and_inner", "brim_ears", "auto_brim")}))],
    "brim_ears_max_angle": [("print", "brim_ears_max_angle", num)],
    "brim_ears_detection_length": [("print", "brim_ears_detection_length", num)],
    "brim_ears_outer_only": [("print", "brim_ears_outer_only", boolean)],
    "brim_flow_ratio": [("print", "brim_flow_ratio", num)],
    "brim_use_efc_outline": [("print", "brim_use_efc_outline", boolean)],
    "combine_brims": [("print", "combine_brims", boolean)],
    "skirt_type": [("print", "skirt_type", enum({"combined": "combined", "perobject": "perobject"}))],
    "brim_width": [("print", "brim_width", num)],
    "brim_object_gap": [("print", "brim_separation", num)],
    "skirt_loops": [("print", "skirts", integer)],
    "skirt_distance": [("print", "skirt_distance", num)],
    "skirt_height": [("print", "skirt_height", integer)],
    "min_skirt_length": [("print", "min_skirt_length", num)],
    "support_threshold_angle": [("print", "support_material_threshold", integer)],
    "support_on_build_plate_only": [("print", "support_material_buildplate_only", boolean)],
    "support_top_z_distance": [("print", "support_material_contact_distance", num)],
    "support_bottom_z_distance": [("print", "support_material_bottom_contact_distance", num)],
    "support_interface_top_layers": [("print", "support_material_interface_layers", integer)],
    "support_interface_bottom_layers": [("print", "support_material_bottom_interface_layers", integer)],
    "support_base_pattern_spacing": [("print", "support_material_spacing", num)],
    "support_interface_spacing": [("print", "support_material_interface_spacing", num)],
    "support_object_xy_distance": [("print", "support_material_xy_spacing", float_or_percent)],
    "support_angle": [("print", "support_material_angle", num)],
    "support_interface_pattern": [("print", "support_material_interface_pattern", enum(
        {"auto": "auto", "rectilinear": "rectilinear", "concentric": "concentric", "spiralinset": "spiralinset",
         "rectilinear_interlaced": "rectilinear_interlaced", "grid": "grid"}))],
    "bridge_no_support": [("print", "dont_support_bridges", boolean)],
    # Orca organic tree supports use the *_organic values; the rest are shared.
    "tree_support_branch_angle_organic": [("print", "support_tree_angle", num)],
    "tree_support_branch_diameter_organic": [("print", "support_tree_branch_diameter", num)],
    "tree_support_branch_distance_organic": [("print", "support_tree_branch_distance", num)],
    "tree_support_angle_slow": [("print", "support_tree_angle_slow", num)],
    "tree_support_branch_diameter_angle": [("print", "support_tree_branch_diameter_angle", num)],
    "tree_support_branch_diameter_double_wall": [("print", "support_tree_branch_diameter_double_wall", num)],
    "tree_support_tip_diameter": [("print", "support_tree_tip_diameter", num)],
    "tree_support_top_rate": [("print", "support_tree_top_rate", percent)],
    "raft_layers": [("print", "raft_layers", integer)],
    "raft_contact_distance": [("print", "raft_contact_distance", num)],
    "raft_expansion": [("print", "raft_expansion", num)],
    "raft_first_layer_density": [("print", "raft_first_layer_density", percent)],
    "raft_first_layer_expansion": [("print", "raft_first_layer_expansion", num)],
    "ironing_spacing": [("print", "ironing_spacing", num)],
    "ironing_speed": [("print", "ironing_speed", num)],
    "ironing_flow": [("print", "ironing_flowrate", percent)],
    "ironing_pattern": [("print", "ironing_pattern", enum({"rectilinear": "rectilinear", "concentric": "concentric"}))],
    "ironing_inset": [("print", "ironing_inset", num)],
    "initial_layer_min_bead_width": [("print", "initial_layer_min_bead_width", percent)],
    "min_length_factor": [("print", "min_length_factor", num)],
    "wall_maximum_resolution": [("print", "wall_maximum_resolution", num)],
    "wall_maximum_deviation": [("print", "wall_maximum_deviation", num)],
    # Orca wall_direction = 3.0 prefer_clockwise_movements (contours clockwise, holes against them)
    "wall_direction": [("print", "prefer_clockwise_movements", enum({"ccw": False, "cw": True}))],
    "overhang_reverse": [("print", "overhang_reverse", boolean)],
    "overhang_reverse_internal_only": [("print", "overhang_reverse_internal_only", boolean)],
    "overhang_reverse_threshold": [("print", "overhang_reverse_threshold", float_or_percent)],
    "alternate_extra_wall": [("print", "alternate_extra_wall", boolean)],
    "unsupported_wall_last": [("print", "unsupported_wall_last", boolean)],
    "min_width_top_surface": [("print", "min_width_top_surface", float_or_percent)],
    "top_bottom_infill_wall_overlap": [("print", "top_bottom_infill_wall_overlap", percent)],
    "filter_out_gap_fill": [("print", "filter_out_gap_fill", num)],
    "internal_bridge_speed": [("print", "internal_bridge_speed", float_or_percent)],
    "internal_bridge_fan_speed": [("filament", "internal_bridge_fan_speed", integer)],
    "internal_bridge_flow": [("print", "internal_bridge_flow", num)],
    "bridge_density": [("print", "bridge_density", percent)],
    "internal_bridge_density": [("print", "internal_bridge_density", percent)],
    "thick_internal_bridges": [("print", "thick_internal_bridges", boolean)],
    "top_surface_expansion": [("print", "top_surface_expansion", num)],
    "top_surface_expansion_margin": [("print", "top_surface_expansion_margin", num)],
    "top_surface_expansion_direction": [("print", "top_surface_expansion_direction", enum({k: k for k in ("inward_and_outward", "inward", "outward")}))],
    "top_surface_fill_order": [("print", "top_surface_fill_order", enum({k: k for k in ("default", "outward", "inward")}))],
    "bottom_surface_fill_order": [("print", "bottom_surface_fill_order", enum({k: k for k in ("default", "outward", "inward")}))],
    "center_of_surface_pattern": [("print", "center_of_surface_pattern", enum({k: k for k in ("each_surface", "each_model", "each_assembly")}))],
    "fill_multiline": [("print", "fill_multiline", integer)],
    "infill_complete_top": [("print", "infill_complete_top", boolean)],
    "tpms_adaptive": [("print", "tpms_adaptive", enum({k: k for k in ("disabled", "distance_warp", "smooth_blend", "stepped_shells", "lobes", "normal_z", "normal_y", "normal_x")}))],
    "tpms_interior_density": [("print", "tpms_interior_density", percent)],
    "tpms_adaptive_gradient": [("print", "tpms_adaptive_gradient", enum({k: k for k in ("linear", "quadratic", "exponential")}))],
    "gyroid_optimized": [("print", "gyroid_optimized", boolean)],
    "separated_infills": [("print", "separated_infills", boolean)],
    "support_threshold_overlap": [("print", "support_threshold_overlap", float_or_percent)],
    "support_expansion": [("print", "support_expansion", num)],
    "support_remove_small_overhang": [("print", "support_remove_small_overhang", boolean)],
    "support_object_first_layer_gap": [("print", "support_object_first_layer_gap", num)],
    "support_bottom_interface_spacing": [("print", "support_bottom_interface_spacing", num)],
    "support_ironing": [("print", "support_ironing", boolean)],
    "support_ironing_pattern": [("print", "support_ironing_pattern", enum({"rectilinear": "rectilinear", "concentric": "concentric"}))],
    "support_ironing_flow": [("print", "support_ironing_flow", percent)],
    "support_ironing_spacing": [("print", "support_ironing_spacing", num)],
    "support_interface_not_for_body": [("print", "support_interface_not_for_body", boolean)],
    "extra_solid_infills": [("print", "extra_solid_infills", text)],
    "infill_shift_step": [("print", "infill_shift_step", num)],
    "symmetric_infill_y_axis": [("print", "symmetric_infill_y_axis", boolean)],
    "skin_infill_density": [("print", "skin_infill_density", percent)],
    "skeleton_infill_density": [("print", "skeleton_infill_density", percent)],
    "skin_infill_depth": [("print", "skin_infill_depth", num)],
    "infill_lock_depth": [("print", "infill_lock_depth", num)],
    "skin_infill_line_width": [("print", "skin_infill_line_width", float_or_percent)],
    "skeleton_infill_line_width": [("print", "skeleton_infill_line_width", float_or_percent)],
    "lateral_lattice_angle_1": [("print", "lateral_lattice_angle_1", num)],
    "lateral_lattice_angle_2": [("print", "lateral_lattice_angle_2", num)],
    "infill_overhang_angle": [("print", "infill_overhang_angle", num)],
    "sparse_infill_smooth_factor": [("print", "sparse_infill_smooth_factor", percent)],
    "lightning_overhang_angle": [("print", "lightning_overhang_angle", num)],
    "lightning_prune_angle": [("print", "lightning_prune_angle", num)],
    "lightning_straightening_angle": [("print", "lightning_straightening_angle", num)],
    "sparse_infill_rotate_template": [("print", "sparse_infill_rotate_template", text)],
    "solid_infill_rotate_template": [("print", "solid_infill_rotate_template", text)],
    "align_infill_direction_to_model": [("print", "align_infill_direction_to_model", boolean)],
    "counterbore_hole_bridging": [("print", "counterbore_hole_bridging", enum({k: k for k in ("none", "partiallybridge", "sacrificiallayer")}))],
    "enable_extra_bridge_layer": [("print", "enable_extra_bridge_layer", enum({k: k for k in ("disabled", "external_bridge_only", "internal_bridge_only", "apply_to_all")}))],
    "dont_filter_internal_bridges": [("print", "dont_filter_internal_bridges", enum({k: k for k in ("disabled", "limited", "nofilter")}))],
    "internal_bridge_angle": [("print", "internal_bridge_angle", num)],
    "relative_bridge_angle": [("print", "relative_bridge_angle", boolean)],
    "bridge_line_width": [("print", "bridge_line_width", float_or_percent)],
    "elefant_foot_layers_density": [("print", "elefant_foot_layers_density", percent)],
    "small_area_infill_flow_compensation": [("print", "small_area_infill_flow_compensation", boolean)],
    # Orca: a list of "length,factor" lines (each after the first starting with a newline)
    "small_area_infill_flow_compensation_model": [("print", "small_area_infill_flow_compensation_model",
                                                   lambda v: "".join(str(x) for x in v) if isinstance(v, list) else str(v))],
    "spiral_mode_smooth": [("print", "spiral_mode_smooth", boolean)],
    "skirt_start_angle": [("print", "skirt_start_angle", num)],
    "single_loop_draft_shield": [("print", "single_loop_draft_shield", boolean)],
    "spiral_mode_max_xy_smoothing": [("print", "spiral_mode_max_xy_smoothing", float_or_percent)],
    "spiral_starting_flow_ratio": [("print", "spiral_starting_flow_ratio", num)],
    "spiral_finishing_flow_ratio": [("print", "spiral_finishing_flow_ratio", num)],
    "ironing_angle": [("print", "ironing_angle", num)],
    "ironing_angle_fixed": [("print", "ironing_angle_fixed", boolean)],
    "infill_wall_overlap": [("print", "infill_overlap", float_or_percent)],
    "infill_direction": [("print", "fill_angle", num)],
    "solid_infill_direction": [("print", "solid_infill_direction", num)],
    "top_layer_direction": [("print", "top_layer_direction", num)],
    "bottom_layer_direction": [("print", "bottom_layer_direction", num)],
    "detect_narrow_internal_solid_infill": [("print", "detect_narrow_internal_solid_infill", boolean)],
    "internal_solid_infill_pattern": [("print", "internal_solid_infill_pattern", SURFACE)],
    "top_surface_density": [("print", "top_surface_density", percent)],
    "bottom_surface_density": [("print", "bottom_surface_density", percent)],
    "infill_anchor": [("print", "infill_anchor", float_or_percent)],
    "infill_anchor_max": [("print", "infill_anchor_max", float_or_percent)],
    "infill_combination": [("print", "automatic_infill_combination", boolean)],
    "infill_combination_max_layer_height": [("print", "automatic_infill_combination_max_layer_height", float_or_percent)],
    "minimum_sparse_infill_area": [("print", "solid_infill_below_area", num)],
    "is_infill_first": [("print", "infill_first", boolean)],
    "detect_thin_wall": [("print", "thin_walls", boolean)],
    "extra_perimeters_on_overhangs": [("print", "extra_perimeters_on_overhangs", boolean)],
    "wall_generator": [("print", "perimeter_generator", enum({"classic": "classic", "arachne": "arachne"}))],
    "wall_transition_angle": [("print", "wall_transition_angle", num)],
    "wall_transition_filter_deviation": [("print", "wall_transition_filter_deviation", float_or_percent)],
    "wall_transition_length": [("print", "wall_transition_length", float_or_percent)],
    "wall_distribution_count": [("print", "wall_distribution_count", integer)],
    "min_bead_width": [("print", "min_bead_width", float_or_percent)],
    "min_feature_size": [("print", "min_feature_size", float_or_percent)],
    "only_one_wall_first_layer": [("print", "only_one_perimeter_first_layer", boolean)],
    "only_one_wall_top": [("print", "top_one_perimeter_type", enum({"1": "top", "0": "none"}))],
    "ensure_vertical_shell_thickness": [("print", "ensure_vertical_shell_thickness", enum(
        {"none": "disabled", "ensure_all": "enabled"}))],
    "elefant_foot_compensation": [("print", "elefant_foot_compensation", num)],
    "xy_contour_compensation": [("print", "xy_size_compensation", num)],
    # 3.0's xy_size_compensation (Orca's contour compensation) also moves the holes by -contour:
    # the extra hole offset is Orca's hole + Orca's contour
    "xy_hole_compensation": [("print", "xy_hole_compensation", plus_of("xy_contour_compensation"))],
    "elefant_foot_compensation_layers": [("print", "elefant_foot_compensation_layers", integer)],
    "resolution": [("print", "resolution", num)],
    "slice_closing_radius": [("print", "slice_closing_radius", num)],
    "slicing_mode": [("print", "slicing_mode", enum({k: k for k in ("regular", "even_odd", "close_holes")}))],
    "gcode_comments": [("print", "gcode_comments", boolean)],
    "enable_arc_fitting": [("print", "arc_fitting", enum({"1": "emit_center", "0": "disabled"}))],
    "spiral_mode": [("print", "spiral_vase", boolean)],
    "print_sequence": [("print", "complete_objects", enum({"by layer": False, "by object": True}))],
    "filename_format": [("print", "output_filename_format", text)],
    "enable_prime_tower": [("print", "wipe_tower", boolean)],
    "prime_tower_width": [("print", "wipe_tower_width", num)],
    "prime_tower_brim_width": [("print", "wipe_tower_brim_width", num)],  # -1 = auto
    "wipe_tower_cone_angle": [("print", "wipe_tower_cone_angle", num)],
    "wipe_tower_extra_spacing": [("print", "wipe_tower_extra_spacing", percent)],
    "wipe_tower_extra_flow": [("print", "wipe_tower_extra_flow", percent)],
    "wipe_tower_bridging": [("print", "wipe_tower_bridging", num)],
    "wipe_tower_no_sparse_layers": [("print", "wipe_tower_no_sparse_layers", boolean)],
    "wipe_tower_wall_type": [("print", "wipe_tower_wall_type", enum({"rectangle": "rectangle", "cone": "cone", "rib": "rib"}))],
    "wipe_tower_rib_width": [("print", "wipe_tower_rib_width", num)],
    "wipe_tower_extra_rib_length": [("print", "wipe_tower_extra_rib_length", num)],
    "wipe_tower_fillet_wall": [("print", "wipe_tower_fillet_wall", boolean)],
    "wipe_tower_max_purge_speed": [("print", "wipe_tower_max_purge_speed", num)],
    # Orca's current names (orca_source.py renames the legacy *_extruder keys before mapping)
    "wipe_tower_filament": [("print", "wipe_tower_extruder", integer)],
    "support_filament": [("print", "support_material_extruder", integer)],
    "support_interface_filament": [("print", "support_material_interface_extruder", integer)],
    "inner_wall_filament_id": [("print", "perimeter_extruder", filament_id)],
    "sparse_infill_filament_id": [("print", "infill_extruder", filament_id)],
    "internal_solid_filament_id": [("print", "solid_infill_extruder", filament_id)],
    # Orca's purge_in_prime_tower = 0: the unit purges into its own chute, the tower only primes
    "purge_in_prime_tower": [("printer", "purge_in_prime_tower", boolean)],
    "prime_volume": [("print", "prime_volume", num)],
    "ooze_prevention": [("print", "ooze_prevention", boolean)],
    "standby_temperature_delta": [("print", "standby_temperature_delta", integer)],
    "single_extruder_multi_material_priming": [("print", "single_extruder_multi_material_priming", boolean)],
    "max_volumetric_extrusion_rate_slope": [("print", "max_volumetric_extrusion_rate_slope_positive", num),
                                            ("print", "max_volumetric_extrusion_rate_slope_negative", num)],
    "draft_shield": [("print", "draft_shield", enum({"disabled": "disabled", "enabled": "enabled"}))],
    "interface_shells": [("print", "interface_shells", boolean)],
    "interlocking_beam": [("print", "interlocking_beam", boolean)],
    "interlocking_beam_layer_count": [("print", "interlocking_beam_layer_count", integer)],
    "interlocking_beam_width": [("print", "interlocking_beam_width", num)],
    "interlocking_boundary_avoidance": [("print", "interlocking_boundary_avoidance", integer)],
    "interlocking_depth": [("print", "interlocking_depth", integer)],
    "interlocking_orientation": [("print", "interlocking_orientation", num)],
    "mmu_segmented_region_max_width": [("print", "mmu_segmented_region_max_width", num)],
    "mmu_segmented_region_interlocking_depth": [("print", "mmu_segmented_region_interlocking_depth", num)],
    "fuzzy_skin": [("print", "fuzzy_skin", enum({"none": "none", "disabled_fuzzy": "none", "external": "external",
                                                 "hole": "hole", "all": "all", "allwalls": "allwalls"}))],
    "fuzzy_skin_first_layer": [("print", "fuzzy_skin_first_layer", boolean)],
    "fuzzy_skin_mode": [("print", "fuzzy_skin_mode", enum({"displacement": "displacement", "extrusion": "extrusion", "combined": "combined"}))],
    "fuzzy_skin_noise_type": [("print", "fuzzy_skin_noise_type", enum({k: k for k in ("classic", "perlin", "billow", "ridgedmulti", "voronoi", "ripple")}))],
    "fuzzy_skin_scale": [("print", "fuzzy_skin_scale", num)],
    "fuzzy_skin_octaves": [("print", "fuzzy_skin_octaves", integer)],
    "fuzzy_skin_persistence": [("print", "fuzzy_skin_persistence", num)],
    "fuzzy_skin_ripples_per_layer": [("print", "fuzzy_skin_ripples_per_layer", integer)],
    "fuzzy_skin_ripple_offset": [("print", "fuzzy_skin_ripple_offset", percent)],
    "fuzzy_skin_layers_between_ripple_offset": [("print", "fuzzy_skin_layers_between_ripple_offset", integer)],
    "fuzzy_skin_point_distance": [("print", "fuzzy_skin_point_dist", num)],
    "fuzzy_skin_thickness": [("print", "fuzzy_skin_thickness", num)],
    "bridge_flow": [("print", "bridge_flow_ratio", num)],
    "bridge_angle": [("print", "bridge_angle", num)],
    "thick_bridges": [("print", "thick_bridges", boolean)],
    "staggered_inner_seams": [("print", "staggered_inner_seams", boolean)],
    "notes": [("print", "notes", text)],
    "reduce_crossing_wall": [("print", "avoid_crossing_perimeters", boolean)],
    "post_process": [("print", "post_process", lambda v: [str(x) for x in (v if isinstance(v, list) else [v]) if str(x)])],
    "gap_fill_target": [("print", "gap_fill_enabled", enum({"nowhere": False, "everywhere": True, "topbottom": True}))],
    "seam_slope_entire_loop": [("print", "scarf_seam_entire_loop", boolean)],
    "seam_slope_conditional": [("print", "scarf_seam_only_on_smooth", boolean)],
    "scarf_angle_threshold": [("print", "scarf_angle_threshold", integer)],
    "scarf_overhang_threshold": [("print", "scarf_overhang_threshold", percent)],
    "seam_slope_steps": [("print", "seam_slope_steps", integer)],
    "seam_slope_inner_walls": [("print", "scarf_seam_on_inner_perimeters", boolean)],
    "seam_slope_min_length": [("print", "scarf_seam_length", num)],
    # Orca: mm or % of layer height; 3.0: % of layer height.
    "seam_slope_start_height": [("print", "scarf_seam_start_height", percent_only)],
    # Orca: mm or % of the direct travel; 3.0: mm (0 = no limit).
    "max_travel_detour_distance": [("print", "avoid_crossing_perimeters_max_detour", num)],
    "seam_slope_type": [("print", "scarf_seam_placement", enum(
        {"none": "nowhere", "external": "everywhere", "all": "everywhere"}))],
    # ---- filament
    "nozzle_temperature": [("filament", "temperature", integer)],
    "nozzle_temperature_initial_layer": [("filament", "first_layer_temperature", integer)],
    "filament_type": [("filament", "filament_type", text)],
    "filament_vendor": [("filament", "filament_vendor", text)],
    "filament_density": [("filament", "filament_density", num)],
    "filament_cost": [("filament", "filament_cost", num)],
    "filament_diameter": [("filament", "filament_diameter", num)],
    "filament_soluble": [("filament", "filament_soluble", boolean)],
    "filament_notes": [("filament", "filament_notes", text)],
    "filament_max_volumetric_speed": [("filament", "filament_max_volumetric_speed", num)],
    "filament_minimal_purge_on_wipe_tower": [("filament", "filament_minimal_purge_on_wipe_tower", num)],
    "filament_cooling_initial_speed": [("filament", "filament_cooling_initial_speed", num)],
    "filament_cooling_final_speed": [("filament", "filament_cooling_final_speed", num)],
    "filament_cooling_moves": [("filament", "filament_cooling_moves", integer)],
    "filament_loading_speed": [("filament", "filament_loading_speed", num)],
    "filament_loading_speed_start": [("filament", "filament_loading_speed_start", num)],
    "filament_unloading_speed": [("filament", "filament_unloading_speed", num)],
    "filament_unloading_speed_start": [("filament", "filament_unloading_speed_start", num)],
    "filament_toolchange_delay": [("filament", "filament_toolchange_delay", num)],
    "filament_multitool_ramming": [("filament", "filament_multitool_ramming", boolean)],
    "filament_multitool_ramming_flow": [("filament", "filament_multitool_ramming_flow", num)],
    "filament_multitool_ramming_volume": [("filament", "filament_multitool_ramming_volume", num)],
    "filament_ramming_parameters": [("filament", "filament_ramming_parameters", text)],
    "filament_stamping_distance": [("filament", "filament_stamping_distance", num)],
    "filament_stamping_loading_speed": [("filament", "filament_stamping_loading_speed", num)],
    "filament_flow_ratio": [("filament", "extrusion_multiplier", num)],
    "default_filament_colour": [("filament", "filament_colour", text)],
    "fan_min_speed": [("filament", "min_fan_speed", integer)],
    "fan_max_speed": [("filament", "max_fan_speed", integer)],
    "fan_cooling_layer_time": [("filament", "fan_below_layer_time", integer)],
    "slow_down_layer_time": [("filament", "slowdown_below_layer_time", integer)],
    "slow_down_min_speed": [("filament", "min_print_speed", num)],
    "close_fan_the_first_x_layers": [("filament", "disable_fan_first_layers", integer)],
    "full_fan_speed_layer": [("filament", "full_fan_speed_layer", integer)],
    "reduce_fan_stop_start_freq": [("filament", "fan_always_on", boolean)],
    "slow_down_for_layer_cooling": [("filament", "cooling", boolean)],
    "chamber_temperature": [("filament", "chamber_temperature", integer)],
    "pressure_advance": [("filament", "pressure_advance_value", num)],
    "adaptive_pressure_advance": [("filament", "adaptive_pressure_advance", boolean)],
    "filament_adaptive_volumetric_speed": [("filament", "filament_adaptive_volumetric_speed", boolean)],
    "volumetric_speed_coefficients": [("filament", "volumetric_speed_coefficients", text)],
    "adaptive_pressure_advance_overhangs": [("filament", "adaptive_pressure_advance_overhangs", boolean)],
    "adaptive_pressure_advance_bridges": [("filament", "adaptive_pressure_advance_bridges", num)],
    "adaptive_pressure_advance_model": [("filament", "adaptive_pressure_advance_model", text)],
    "enable_pressure_advance": [("filament", "pressure_advance", enum({"1": "enabled", "0": "disabled"}))],
    # Filament retraction overrides ('nil' = no override, skipped).
    "filament_retraction_length": [("filament", "retract_length", num)],
    "filament_retraction_speed": [("filament", "retract_speed", num)],
    "filament_deretraction_speed": [("filament", "deretract_speed", num)],
    "filament_retraction_minimum_travel": [("filament", "retract_before_travel", num)],
    "filament_retract_before_wipe": [("filament", "retract_before_wipe", percent)],
    "filament_retract_when_changing_layer": [("filament", "retract_layer_change", boolean)],
    "filament_z_hop": [("filament", "retract_lift", num)],
    "filament_retract_lift_above": [("filament", "retract_lift_above", num)],
    "filament_retract_lift_below": [("filament", "retract_lift_below", num)],
    "filament_retract_lift_enforce": [("filament", "retract_lift_enforce", enum({"All Surfaces": "all_surfaces", "Top Only": "top_only", "Bottom Only": "bottom_only", "Top and Bottom": "top_and_bottom"}))],
    "filament_retract_restart_extra": [("filament", "retract_restart_extra", num)],
    "filament_wipe": [("filament", "wipe", boolean)],
    "chamber_minimal_temperature": [("filament", "chamber_minimal_temperature", num)],
    # parity batch 1 (ported settings, docs/design/ORCA_SETTINGS_REVIEW.md B)
    "wipe_speed": [("print", "wipe_speed", float_or_percent)],
    "role_based_wipe_speed": [("print", "role_based_wipe_speed", boolean)],
    "skirt_speed": [("print", "skirt_speed", num)],
    # same mechanism as 3.0's only_retract_when_crossing_perimeters (Orca renamed it and also
    # retracts before a perimeter; 3.0 keeps its own rule there)
    "reduce_infill_retraction": [("print", "only_retract_when_crossing_perimeters", boolean)],
    # parity batch 12
    "enable_overhang_bridge_fan": [("filament", "enable_overhang_bridge_fan", boolean)],
    "overhang_fan_speed": [("filament", "overhang_fan_speed", integer), ("filament", "bridge_fan_speed", bridge_fan_speed)],
    "overhang_fan_threshold": [("filament", "overhang_fan_threshold", enum(
        {"0%": "0%", "10%": "10%", "25%": "25%", "50%": "50%", "75%": "75%", "95%": "95%"}))],
    "scarf_joint_speed": [("print", "scarf_joint_speed", float_or_percent)],
    "scarf_joint_flow_ratio": [("print", "scarf_joint_flow_ratio", num)],
    "slowdown_for_curled_perimeters": [("print", "slowdown_for_curled_perimeters", boolean)],
    "preheat_time": [("print", "preheat_time", num)],
    "preheat_steps": [("print", "preheat_steps", integer)],
    "toolchange_cyclic_order": [("print", "toolchange_cyclic_order", text)],
    "fan_speedup_time": [("printer", "fan_speedup_time", num)],
    "fan_speedup_overhangs": [("printer", "fan_speedup_overhangs", boolean)],
    "fan_kickstart": [("printer", "fan_kickstart", num)],
    "toolchange_cyclic_first_layer": [("print", "toolchange_cyclic_first_layer", boolean)],
    "small_support_perimeter_speed": [("print", "small_support_perimeter_speed", float_or_percent)],
    "small_support_perimeter_threshold": [("print", "small_support_perimeter_threshold", num)],
    "max_volumetric_extrusion_rate_slope_segment_length": [("print", "max_volumetric_extrusion_rate_slope_segment_length", num)],
    "extrusion_rate_smoothing_external_perimeter_only": [("print", "extrusion_rate_smoothing_external_perimeter_only", boolean)],
    "time_cost": [("printer", "time_cost", num)],
    "enable_power_loss_recovery": [("printer", "enable_power_loss_recovery", enum(
        {"printer_configuration": "printer_configuration", "enable": "enable", "disable": "disable"}))],
    # parity batch 11: read by Orca's runtime G-code variables
    "bed_mesh_min": [("printer", "bed_mesh_min", point)],
    "bed_mesh_max": [("printer", "bed_mesh_max", point)],
    "bed_mesh_probe_distance": [("printer", "bed_mesh_probe_distance", point)],
    "adaptive_bed_mesh_margin": [("printer", "adaptive_bed_mesh_margin", num)],
    "support_chamber_temp_control": [("printer", "support_chamber_temp_control", boolean)],
    "activate_chamber_temp_control": [("filament", "activate_chamber_temp_control", enum({"0": "off", "1": "on"}))],
    "temperature_vitrification": [("filament", "temperature_vitrification", integer)],
    "nozzle_temperature_range_low": [("filament", "nozzle_temperature_range_low", integer)],
    "nozzle_temperature_range_high": [("filament", "nozzle_temperature_range_high", integer)],
    "filament_flush_temp": [("filament", "filament_flush_temp", integer)],
    "filament_flush_volumetric_speed": [("filament", "filament_flush_volumetric_speed", num)],
    "filament_is_support": [("filament", "filament_is_support", boolean)],
    # parity batch 10
    "file_start_gcode": [("printer", "file_start_gcode", text)],
    "time_lapse_gcode": [("printer", "time_lapse_gcode", text)],
    "timelapse_type": [("print", "timelapse_type", enum({"0": "0", "1": "1"}))],
    # parity batch 9: G-code output
    "gcode_skip_config_block": [("printer", "gcode_skip_config_block", boolean)],
    "gcode_add_line_number": [("print", "gcode_add_line_number", boolean)],
    "manual_filament_change": [("printer", "manual_filament_change", boolean)],
    "bed_temperature_formula": [("printer", "bed_temperature_formula", enum(
        {"by_first_filament": "by_first_filament", "by_highest_temp": "by_highest_temp"}))],
    "machine_load_filament_time": [("printer", "filament_change_time", filament_change_time)],
    "machine_unload_filament_time": [("printer", "filament_change_time", filament_change_time)],
    "machine_tool_change_time": [("printer", "filament_change_time", filament_change_time)],
    # parity batch 8: motion
    "default_jerk": [("print", "default_jerk", num)],
    "outer_wall_jerk": [("print", "outer_wall_jerk", num)],
    "inner_wall_jerk": [("print", "inner_wall_jerk", num)],
    "infill_jerk": [("print", "infill_jerk", num)],
    "top_surface_jerk": [("print", "top_surface_jerk", num)],
    "initial_layer_jerk": [("print", "initial_layer_jerk", num)],
    "travel_jerk": [("print", "travel_jerk", num)],
    "initial_layer_travel_jerk": [("print", "initial_layer_travel_jerk", float_or_percent)],
    "initial_layer_travel_acceleration": [("print", "initial_layer_travel_acceleration", float_or_percent)],
    "accel_to_decel_enable": [("print", "accel_to_decel_enable", boolean)],
    "accel_to_decel_factor": [("print", "accel_to_decel_factor", percent_only)],
    "default_junction_deviation": [("print", "default_junction_deviation", num)],
    # parity batch 7: fans
    "initial_layer_fan_speed": [("filament", "initial_layer_fan_speed", integer)],
    "support_material_interface_fan_speed": [("filament", "support_material_interface_fan_speed", integer)],
    "ironing_fan_speed": [("filament", "ironing_fan_speed", integer)],
    "additional_cooling_fan_speed": [("filament", "additional_cooling_fan_speed", integer)],
    "dont_slow_down_outer_wall": [("filament", "dont_slow_down_outer_wall", boolean)],
    "auxiliary_fan": [("printer", "auxiliary_fan", boolean)],
    "part_cooling_fan_min_pwm": [("printer", "part_cooling_fan_min_pwm", integer)],
    # parity tier 1: 3.0 has these settings (definitions compared 2026-10-18)
    "enforce_support_layers": [("print", "support_material_enforce_layers", integer)],
    "support_interface_loop_pattern": [("print", "support_material_interface_contact_loops", boolean)],
    "detect_overhang_wall": [("print", "overhangs", boolean)],
    "toolchange_ordering": [("print", "toolchange_ordering", enum({"default": "optimized", "cyclic": "cyclic"}))],
    # parity batch 5
    "small_perimeter_threshold": [("print", "small_perimeter_threshold", num)],
    # parity batch 4
    "slow_down_layers": [("print", "slow_down_layers", integer)],
    # parity batch 3: air filtration
    "support_air_filtration": [("printer", "support_air_filtration", boolean)],
    "activate_air_filtration": [("filament", "activate_air_filtration", boolean)],
    "activate_air_filtration_during_print": [("filament", "activate_air_filtration_during_print", boolean)],
    "during_print_exhaust_fan_speed": [("filament", "during_print_exhaust_fan_speed", integer)],
    "activate_air_filtration_on_completion": [("filament", "activate_air_filtration_on_completion", boolean)],
    "complete_print_exhaust_fan_speed": [("filament", "complete_print_exhaust_fan_speed", integer)],
    # parity batch 2: flow ratios
    "set_other_flow_ratios": [("print", "set_other_flow_ratios", boolean)],
    "print_flow_ratio": [("print", "print_flow_ratio", num)],
    "top_solid_infill_flow_ratio": [("print", "top_solid_infill_flow_ratio", num)],
    "outer_wall_flow_ratio": [("print", "outer_wall_flow_ratio", num)],
    "inner_wall_flow_ratio": [("print", "inner_wall_flow_ratio", num)],
    "overhang_flow_ratio": [("print", "overhang_flow_ratio", num)],
    "sparse_infill_flow_ratio": [("print", "sparse_infill_flow_ratio", num)],
    "internal_solid_infill_flow_ratio": [("print", "internal_solid_infill_flow_ratio", num)],
    "gap_fill_flow_ratio": [("print", "gap_fill_flow_ratio", num)],
    "support_flow_ratio": [("print", "support_flow_ratio", num)],
    "support_interface_flow_ratio": [("print", "support_interface_flow_ratio", num)],
    "first_layer_flow_ratio": [("print", "first_layer_flow_ratio", num)],
    "initial_layer_travel_speed": [("print", "initial_layer_travel_speed", float_or_percent)],
    "change_extrusion_role_gcode": [("printer", "change_extrusion_role_gcode", text)],
    "process_change_extrusion_role_gcode": [("print", "process_change_extrusion_role_gcode", text)],
    "filament_change_extrusion_role_gcode": [("filament", "filament_change_extrusion_role_gcode", text)],
    "enable_filament_ramming": [("printer", "enable_filament_ramming", boolean)],
}
for o, t in GCODE_FIELDS_PRINTER.items():
    MAP[o] = [("printer", t, text)]
for o, t in GCODE_FIELDS_FILAMENT.items():
    MAP[o] = [("filament", t, text)]

# Keys the converter uses to build the vendor structure (not copied as settings).
STRUCTURAL = {
    "compatible_printers", "compatible_printers_condition", "compatible_prints", "compatible_prints_condition", "printer_model", "nozzle_diameter",
    "printer_variant", "printer_technology", "default_bed_type", "support_multi_bed_types",
    "thumbnails", "thumbnails_format", "enable_support", "support_type", "support_style",
    "support_base_pattern",
    "ironing_type", "z_hop_types", "filament_z_hop_types", "exclude_object", "gcode_label_objects",
    "wall_sequence", "wall_infill_order", "filament_shrink", "filament_shrinkage_compensation_z",
    "cool_plate_temp", "cool_plate_temp_initial_layer", "eng_plate_temp", "eng_plate_temp_initial_layer",
    "hot_plate_temp", "hot_plate_temp_initial_layer", "textured_plate_temp", "textured_plate_temp_initial_layer",
    "supertack_plate_temp", "supertack_plate_temp_initial_layer",
    "textured_cool_plate_temp", "textured_cool_plate_temp_initial_layer",
    "idle_temperature", "default_filament_profile",
}

# Dropped on purpose (reason shown in the report). Keys not in MAP/STRUCTURAL/DROP are
# reported as "no 3.0 equivalent".
DROP = {
    "printer_structure": "informational in Orca",
    "host_type": "physical printer setting, not a preset",
    "printhost_authorization_type": "physical printer setting, not a preset",
    "printhost_ssl_ignore_revoke": "physical printer setting, not a preset",
    "bbl_use_printhost": "physical printer setting, not a preset",
}


def markdown_table():
    """The mapping table as Markdown (used for docs/profiles/ORCA_IMPORT.md)."""
    rows = ["| Orca key | 3.0 preset | 3.0 key | conversion |", "|---|---|---|---|"]
    box_name = {"printer": "printer", "tool": "print (machine base, per nozzle)", "print": "print",
                "filament": "filament"}
    for okey in sorted(MAP):
        for box, target, conv in MAP[okey]:
            if getattr(conv, "table", None) is not None:
                how = "enum: " + ", ".join(f"{k}->{v}" for k, v in conv.table.items())
            elif getattr(conv, "how", None):
                how = conv.how
            elif getattr(conv, "needs_cfg", False):
                how = f"'%' resolved against Orca {conv.base}"
            else:
                how = getattr(conv, "__name__", "custom")
            rows.append(f"| `{okey}` | {box_name[box]} | `{target}` | {how} |")
    return "\n".join(rows)


if __name__ == "__main__":
    print(markdown_table())
