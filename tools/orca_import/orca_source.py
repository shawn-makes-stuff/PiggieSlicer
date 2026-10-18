"""Load an OrcaSlicer vendor profile tree and resolve inheritance.

Resolution follows Orca's PresetBundle::load_vendor_configs_from_json (v2.5.0-alpha):
a preset's config is its parent's resolved config with the preset's own keys applied
on top; a missing parent is looked up in the base bundle (OrcaFilamentLibrary).
Root presets start from Orca's built-in defaults for their type (orca_defaults.json,
extracted from Orca's PrintConfig.cpp / Preset.cpp at the same tag).
"""
import json
import os
import struct

# Keys that describe the preset itself rather than a setting.
META_KEYS = {
    "type", "name", "inherits", "from", "setting_id", "filament_id", "instantiation",
    "renamed_from", "description", "version", "is_custom_defined",
    "printer_settings_id", "print_settings_id", "filament_settings_id", "include",
}


class OrcaSourceError(Exception):
    pass


# --- Port of Orca PrintConfigDef::handle_legacy (src/libslic3r/PrintConfig.cpp, v2.5.0-alpha).
# Orca runs it on every key/value of a JSON preset as it is loaded (ConfigBase::load_from_json ->
# set_deserialize), i.e. before the preset is merged onto its parent. Only rules that can apply
# to vendor JSON are ported; keys Orca drops are recorded in Vendor.ignored.
_KEY_RENAMES = {
    "enable_wipe_tower": "enable_prime_tower", "wipe_tower_width": "prime_tower_width",
    "wiping_volume": "prime_volume", "wipe_tower_brim_width": "prime_tower_brim_width",
    "tool_change_gcode": "change_filament_gcode", "bridge_fan_speed": "overhang_fan_speed",
    "wipe_tower_extruder": "wipe_tower_filament", "support_material_extruder": "support_filament",
    "support_material_interface_extruder": "support_interface_filament",
    "sparse_infill_anchor": "infill_anchor", "sparse_infill_anchor_max": "infill_anchor_max",
    "chamber_temperatures": "chamber_temperature", "thumbnail_size": "thumbnails",
    "initial_layer_flow_ratio": "bottom_solid_infill_flow_ratio", "ironing_direction": "ironing_angle",
    "counterbole_hole_bridging": "counterbore_hole_bridging",
    "prime_tower_extra_rib_length": "wipe_tower_extra_rib_length",
    "prime_tower_rib_width": "wipe_tower_rib_width", "prime_tower_fillet_wall": "wipe_tower_fillet_wall",
    "extruder_clearance_max_radius": "extruder_clearance_radius",
    "machine_switch_extruder_time": "machine_tool_change_time",
}
_FILAMENT_ID_RENAMES = {  # legacy 1-based selectors; "1" becomes "0" (inherit)
    "infill_extruder": "sparse_infill_filament_id", "sparse_infill_filament": "sparse_infill_filament_id",
    "solid_infill_extruder": "internal_solid_filament_id", "solid_infill_filament": "internal_solid_filament_id",
    "top_solid_infill_filament": "top_surface_filament_id",
    "bottom_solid_infill_filament": "bottom_surface_filament_id",
    "perimeter_extruder": "outer_wall_filament_id", "wall_filament": "outer_wall_filament_id",
    "wall_filament_id": "outer_wall_filament_id", "inner_wall_filament": "inner_wall_filament_id",
    "outer_wall_filament": "outer_wall_filament_id",
}
_PERCENT_ERASED = {"initial_layer_print_height", "initial_layer_speed", "internal_solid_infill_speed",
                   "top_surface_speed", "support_interface_speed", "outer_wall_speed",
                   "support_object_xy_distance"}
_IGNORED = {
    "acceleration", "scale", "rotate", "duplicate", "duplicate_grid", "bed_size", "print_center", "g0",
    "wipe_tower_per_color_wipe", "support_sharp_tails", "support_remove_small_overhangs",
    "support_with_sheath", "tree_support_collision_resolution", "tree_support_with_infill",
    "max_volumetric_speed", "max_print_speed", "support_closing_radius", "remove_freq_sweep",
    "remove_bed_leveling", "remove_extrusion_calibration", "support_transition_line_width",
    "support_transition_speed", "bed_temperature", "bed_temperature_initial_layer",
    "can_switch_nozzle_type", "can_add_auxiliary_fan", "extra_flush_volume", "spaghetti_detector",
    "adaptive_layer_height", "z_hop_type", "z_lift_type", "bed_temperature_difference",
    "long_retraction_when_cut", "retraction_distance_when_cut", "internal_bridge_support_thickness",
    "top_area_threshold", "reduce_wall_solid_infill", "filament_load_time", "filament_unload_time",
    "smooth_coefficient", "overhang_totally_speed", "silent_mode", "overhang_speed_classic",
    "anisotropic_surfaces",
}
_PATTERN_KEYS = {"sparse_infill_pattern", "top_surface_pattern", "bottom_surface_pattern",
                 "internal_solid_infill_pattern", "ironing_pattern", "support_ironing_pattern"}


def _joined(v):
    return ",".join(str(x) for x in v) if isinstance(v, list) else str(v)


_SPLIT_VECTORS = {"coFloats", "coInts", "coBools", "coPercents", "coFloatsOrPercents", "coPoints"}


def _flatten(key, value, types):
    """ConfigBase::load_from_json joins a JSON array with ',' (';' for string vectors) before
    deserializing, so an element like "10,8" is two values. Re-split numeric/point vectors."""
    if types.get(key) not in _SPLIT_VECTORS:
        return value
    if isinstance(value, list) and any("," in str(x) for x in value):
        return [p.strip() for x in value for p in str(x).split(",")]
    if isinstance(value, str) and "," in value:
        return [p.strip() for p in value.split(",")]
    return value


def handle_legacy(key, value):
    """Returns (key, value) as Orca stores it, or (None, reason) when Orca drops the key."""
    s = _joined(value)

    def setv(new):
        return [new] if isinstance(value, list) else new

    if key in _PERCENT_ERASED and "%" in s:
        return None, "old-profile percentage; Orca's handle_legacy erases it (parent value kept)"
    if key in _KEY_RENAMES:
        key = _KEY_RENAMES[key]
    elif key in _FILAMENT_ID_RENAMES:
        key = _FILAMENT_ID_RENAMES[key]
        if s == "1":
            value = setv("0")
    elif key == "timelapse_type" and s == "2":
        value = setv("0")
    elif key == "support_type" and s in ("normal", "tree", "hybrid(auto)"):
        value = setv({"normal": "normal(manual)", "tree": "tree(manual)", "hybrid(auto)": "tree(auto)"}[s])
    elif key == "support_base_pattern" and s == "none":
        value = setv("hollow")
    elif key == "tree_support_wall_count" and s == "-1":
        value = setv("0")
    elif key == "overhang_fan_threshold" and s == "5%":
        value = setv("10%")
    elif key == "wall_infill_order":
        key = "wall_sequence"
        m = {"inner wall/outer wall/infill": "inner wall/outer wall",
             "infill/inner wall/outer wall": "inner wall/outer wall",
             "outer wall/inner wall/infill": "outer wall/inner wall",
             "infill/outer wall/inner wall": "outer wall/inner wall",
             "inner-outer-inner wall/infill": "inner-outer-inner wall"}
        if s in m:
            value = setv(m[s])
    elif key == "ensure_vertical_shell_thickness" and s in ("0", "1"):
        value = setv("ensure_all" if s == "1" else "ensure_moderate")
    elif key == "rotate_solid_infill_direction":
        key = "solid_infill_rotate_template"
        if s == "1":
            value = setv("0,90")
    elif key == "top_one_wall_type" and s != "none":
        key, value = "only_one_wall_top", setv("1")
    elif key == "ironing_angle" and s.startswith("-"):
        value = setv("0")
    elif key == "draft_shield" and s == "limited":
        value = setv("disabled")
    elif key in _PATTERN_KEYS and s == "zig-zag":
        value = setv("rectilinear")
    elif key == "filament_type" and "ASA-Aero" in s:
        value = [x.replace("ASA-Aero", "ASA-AERO") for x in value] if isinstance(value, list) else s.replace("ASA-Aero", "ASA-AERO")
    elif key == "prime_tower_rib_wall":
        if s == "1":
            key, value = "wipe_tower_wall_type", setv("rib")
        else:
            return None, "Orca's handle_legacy drops it unless 1"
    elif key == "wall_direction" and s == "auto":
        value = setv("ccw")
    if key in _IGNORED:
        return None, "obsolete key; Orca's handle_legacy ignores it"
    return key, value


def read_opc_version(opc_path):
    """Vendor name and version string from an installed Orca .opc preset cache header.

    Header: b"ZCRO", u32 format, u64, u32, u32, then u64-length-prefixed vendor name and
    u64-length-prefixed version string (little endian)."""
    with open(opc_path, "rb") as f:
        head = f.read(4096)
    if head[:4] != b"ZCRO":
        raise OrcaSourceError(f"{opc_path}: not an Orca preset cache (magic {head[:4]!r})")
    pos = 0x18
    (n,) = struct.unpack_from("<Q", head, pos)
    name = head[pos + 8: pos + 8 + n].decode("utf-8")
    pos += 8 + n
    (m,) = struct.unpack_from("<Q", head, pos)
    version = head[pos + 8: pos + 8 + m].decode("utf-8")
    return name, version


def orca_version_stamp(json_version):
    """The version string Orca stamps into a .opc for a vendor JSON "version": Semver::to_string
    of semver_parse (deps_src/semver/semver.c: the 4th part folds into patch as patch*100+part;
    Semver.hpp to_string prints major.minor.patch/100.patch%100). "02.04.00.09" -> "2.4.0.9",
    "03.00.12" -> "3.0.0.12", "1.0.0" -> "1.0.0.0"."""
    parts = [int(p) for p in json_version.split(".")]
    major, minor = parts[0], parts[1]
    patch = parts[2] if len(parts) > 2 else 0
    if len(parts) > 3:
        patch = patch * 100 + parts[3]
    return f"{major}.{minor}.{patch // 100}.{patch % 100}"


def normalize_version(v):
    """'02.04.00.09' and '2.4.0.9' -> (2, 4, 0, 9)."""
    return tuple(int(p) for p in v.split("."))


_DEFAULTS_CACHE = {}


def orca_defaults(profiles_dir):
    """Orca's built-in defaults per preset kind (orca_defaults.json written by the fetch step)."""
    if profiles_dir not in _DEFAULTS_CACHE:
        path = os.path.join(profiles_dir, "orca_defaults.json")
        with open(path, encoding="utf-8") as f:
            _DEFAULTS_CACHE[profiles_dir] = json.load(f)
    return _DEFAULTS_CACHE[profiles_dir]


class Vendor:
    def __init__(self, profiles_dir, name, base=None):
        with open(os.path.join(profiles_dir, "orca_config_types.json"), encoding="utf-8") as f:
            self.types = json.load(f)["types"]
        d = orca_defaults(profiles_dir)
        self.defaults = d["defaults"]
        self.variant_sets = d["variant_sets"]
        self.enum_ints = d.get("enum_ints", {})
        self.name = name
        self.dir = os.path.join(profiles_dir, name)
        with open(os.path.join(profiles_dir, name + ".json"), encoding="utf-8") as f:
            self.index = json.load(f)
        self.version = self.index["version"]
        self.base = base
        self.raw = {}       # preset name -> raw json
        self.kind = {}      # preset name -> machine_model / machine / process / filament
        lists = [("machine_model_list", "machine_model"), ("machine_list", "machine"),
                 ("process_list", "process"), ("filament_list", "filament")]
        for list_key, kind in lists:
            for entry in self.index.get(list_key, []):
                path = os.path.join(self.dir, entry["sub_path"])
                with open(path, encoding="utf-8") as f:
                    data = json.load(f)
                pname = data.get("name", entry["name"])
                if pname in self.raw:
                    raise OrcaSourceError(f"{name}: duplicate preset name {pname!r}")
                self.raw[pname] = data
                self.kind[pname] = kind
        self._resolved = {}
        self.ignored = {}   # (orca key, reason) -> preset names where Orca's loader dropped it

    def find_raw(self, pname):
        if pname in self.raw:
            return self, self.raw[pname]
        if self.base is not None:
            return self.base.find_raw(pname)
        return None, None

    def resolve(self, pname, _stack=()):
        """Fully resolved settings dict (meta keys removed) and the inheritance chain."""
        if pname in self._resolved:
            return self._resolved[pname]
        owner, data = self.find_raw(pname)
        if data is None:
            raise OrcaSourceError(f"{self.name}: preset {pname!r} not found")
        if owner is not self:
            return owner.resolve(pname)
        if pname in _stack:
            raise OrcaSourceError(f"{self.name}: inheritance cycle at {pname!r}")
        parent = data.get("inherits", "")
        if parent:
            cfg, chain = self.resolve(parent, _stack + (pname,))
            cfg = dict(cfg)
            chain = chain + [pname]
        else:
            # Root preset: Orca starts from the default preset of this type.
            cfg, chain = dict(self.defaults.get(self.kind[pname], {})), ["<Orca defaults>", pname]
        # "include" (PresetBundle load_preset_entry): each included preset's stated keys - its
        # resolved config minus the type's defaults - are layered over the parent in list order,
        # then this preset's own keys go on top.
        incs = data.get("include", [])
        for inc in (incs if isinstance(incs, list) else [incs]):
            owner, _ = self.find_raw(inc)
            if owner is None:
                raise OrcaSourceError(f"{self.name}: {pname!r} includes unknown preset {inc!r}")
            icfg, _ = owner.resolve(inc, _stack + (pname,)) if owner is self else owner.resolve(inc)
            idef = owner.defaults.get(owner.kind[inc], {})
            for k, v in icfg.items():
                if idef.get(k) != v:
                    cfg[k] = v
            chain = chain + [f"include:{inc}"]
        for k, v in data.items():
            if k in META_KEYS:
                continue
            nk, nv = handle_legacy(k, _flatten(k, v, self.types))
            if nk is None:
                self.ignored.setdefault((k, nv), set()).add(pname)
                continue
            cfg[nk] = nv
            # ConfigBase::load_from_json: an infill-first wall_infill_order also sets is_infill_first.
            if k == "wall_infill_order" and _joined(v) in ("infill/outer wall/inner wall",
                                                           "infill/inner wall/outer wall"):
                cfg["is_infill_first"] = "true"
        self._resolved[pname] = (cfg, chain)
        return self._resolved[pname]

    def filament_id(self, pname, _depth=0):
        """Orca filament_id of a filament preset: its own, else its parent's (PresetBundle
        filament_id_maps), across the base library."""
        owner, data = self.find_raw(pname)
        if data is None or _depth > 50:
            return ""
        if data.get("filament_id"):
            return data["filament_id"]
        parent = data.get("inherits", "")
        return owner.filament_id(parent, _depth + 1) if parent else ""

    def instantiated(self, kind):
        return sorted(n for n, k in self.kind.items()
                      if k == kind and self.raw[n].get("instantiation") == "true")

    def machine_models(self):
        return {n: self.raw[n] for n, k in self.kind.items() if k == "machine_model"}


def load(profiles_dir, vendor_name, base_name="OrcaFilamentLibrary"):
    base = None
    if base_name and vendor_name != base_name and os.path.exists(os.path.join(profiles_dir, base_name + ".json")):
        base = Vendor(profiles_dir, base_name)
    return Vendor(profiles_dir, vendor_name, base)


def check_against_install(vendor, orca_install):
    """Refuse to convert unless the JSON source has the same version as the installed .opc."""
    checks = [vendor] + ([vendor.base] if vendor.base else [])
    for v in checks:
        opc = os.path.join(orca_install, "resources", "profiles", v.name + ".opc")
        if not os.path.exists(opc):
            raise OrcaSourceError(f"installed preset cache not found: {opc}")
        opc_name, opc_ver = read_opc_version(opc)
        if opc_name != v.name or opc_ver != orca_version_stamp(v.version):
            raise OrcaSourceError(
                f"source mismatch for {v.name}: JSON version {v.version} but installed {opc} "
                f"is {opc_name} {opc_ver}")
