"""Custom G-code placeholder translation (Orca -> PiggieSlicer 3.0).

The 3.0 macro language (src/slic3r-biz-parser/.../PlaceholderParser.cpp) has two forms:
  [name] / [name[index_identifier]]   legacy expansion; every '[' outside {} must start one
  {code}                              expressions, if/elsif/else/endif, local/global vars
Variables available while exporting G-code are
  * every printer/print/filament config key (config_schema.json, squashed per slot),
  * nozzle_diameter, nozzle_high_flow (added from the tool features, Domain/Config.cpp),
  * the runtime variables set in libslic3r GCode.cpp / Print.cpp / PrintApply.cpp (RUNTIME),
  * timestamp variables (PlaceholderParser::update_timestamp).
Note: 3.0 removes first_layer_speed from the squashed config (Domain/Config.cpp), so it is
not a valid placeholder.
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

KEYWORDS = {"and", "ceil", "digits", "zdigits", "empty", "if", "int", "is_nil", "local", "else",
            "elsif", "endif", "false", "global", "interpolate_table", "min", "max", "random",
            "repeat", "round", "not", "one_of", "or", "size", "true", "then", "inf"}

# grep of set("...") in src/libslic3r/src/libslic3r/{GCode,Print,PrintApply,PrintBase}.cpp
RUNTIME = {
    "bed_name", "color_change_extruder", "current_extruder", "current_object_idx", "e_position",
    "e_restart_extra", "e_retracted", "extruded_volume", "extruded_volume_total", "extruded_weight",
    "extruded_weight_total", "filament_extruder_id", "filament_settings_id", "first_layer_bed_temperature",
    "first_layer_print_convex_hull", "first_layer_print_max", "first_layer_print_min",
    "first_layer_print_size", "fuzzy_skin", "has_single_extruder_multi_material_priming",
    "has_wipe_tower", "infill_extruder", "initial_extruder", "initial_tool", "input_filename",
    "input_filename_base", "is_extruder_used", "layer_num", "layer_z", "max_layer_z", "next_extruder",
    "num_extruders", "num_instances", "num_objects", "perimeter_extruder", "position",
    "previous_extruder", "print_bed_max", "print_bed_min", "print_bed_size", "print_settings_id",
    "printer_settings_id", "scale", "sheet_type", "single_extruder_multi_material_priming",
    "solid_infill_extruder", "toolchange_count", "toolchange_z", "total_layer_count",
    "total_toolchanges", "version", "wipe_tower_rotation_angle", "wipe_tower_x", "wipe_tower_y",
    "z_offset", "zhop",
    "nozzle_diameter", "nozzle_high_flow",
    "timestamp", "year", "month", "day", "hour", "minute", "second",
    # Orca's runtime variables, set by 3.0's GCode.cpp too (parity batches 10 and 11)
    "filament_map", "first_tools", "first_filaments", "first_non_support_tools", "first_non_support_filaments",
    "first_non_support_hotend", "initial_no_support_tool", "initial_no_support_extruder",
    "initial_no_support_filament_id", "initial_no_support_hotend", "initial_filament_id", "current_filament_id",
    "current_extruder_id", "current_hotend", "current_nozzle_id", "initial_nozzle_id", "nozzle_diameter_at_nozzle_id",
    "max_additional_fan", "overall_chamber_temperature", "min_vitrification_temperature", "is_all_bbl_filament",
    "during_print_exhaust_fan_speed_num", "has_tpu_in_first_layer", "flush_temperatures", "flush_volumetric_speeds",
    "outer_wall_volumetric_speed", "adaptive_bed_mesh_min", "adaptive_bed_mesh_max", "bed_mesh_probe_count",
    "bed_mesh_algo", "max_print_z", "first_layer_center_no_wipe_tower", "hold_chamber_temp_for_flat_print",
    "wipe_tower_center_pos_x", "wipe_tower_center_pos_y", "wipe_tower_center_pos_valid",
    "print_time_total_sec", "print_time_day", "print_time_hour", "print_time_minute", "print_time_sec",
    "used_filament_length", "curr_y_acceleration_limit", "curr_accumulated_mass", "curr_layer_mass",
    "next_filament_id", "next_nozzle_id", "next_hotend", "old_retract_length", "new_retract_length",
    "old_retract_length_toolchange", "new_retract_length_toolchange", "x_after_toolchange", "y_after_toolchange",
    "z_after_toolchange", "first_flush_volume", "second_flush_volume", "flush_length", "flush_length_1",
    "flush_length_2", "flush_length_3", "flush_length_4", "travel_point_1_x", "travel_point_1_y",
    "travel_point_2_x", "travel_point_2_y", "travel_point_3_x", "travel_point_3_y", "wipe_avoid_perimeter",
    "wipe_avoid_pos_x", "is_prime_tower_interface", "relative_e_axis", "fan_speed", "old_filament_temp",
    "new_filament_temp", "old_filament_e_feedrate", "new_filament_e_feedrate",
}

# Extra variables only for output_filename_format
# (src/slic3r-shared/src/Slic3r/Biz/ResultExport/ExportNameParser.cpp).
FILENAME_EXTRA = {
    "extruded_volume", "filament_preset", "initial_filament_type", "normal_print_time",
    "num_printing_extruders", "objects_used_material", "print_preset", "print_time",
    "printer_preset", "printing_filament_types", "silent_print_time", "support_used_material",
    "total_cost", "total_weight", "total_wipe_tower_cost", "total_wipe_tower_filament",
    "used_filament",
}

# Orca name -> 3.0 replacement text, for names whose meaning is the same in both apps.
# Orca runtime variables are checked against Orca src/libslic3r/GCode.cpp (v2.5.0-alpha);
# Orca setting keys are added from mapping.MAP below (same value, different name).
RENAMES = {
    # Orca GCode.cpp: first-layer bed temperature of the initial filament (scalar).
    "bed_temperature_initial_layer_single": "first_layer_bed_temperature[initial_extruder]",
    # Orca GCode.cpp: vector of first-layer bed temperatures for the current plate type.
    "bed_temperature_initial_layer": "first_layer_bed_temperature",
    # Orca printable_height == 3.0 max_print_height (both are the printer's Z height).
    "printable_height": "max_print_height",
    # Orca curr_bed_type is the plate's enum key ("Textured PEI Plate" ...); the import uses the
    # same strings as 3.0 sheet types, and 3.0 exposes the sheet type as sheet_type.
    "curr_bed_type": "sheet_type",
    # Orca: print.get_plate_name() - the user's name of the plate; 3.0: bed.name() of the bed.
    "plate_name": "bed_name",
}

# Orca preset-name variables in custom G-code; 3.0 sets the same names as *_settings_id
# (PrintApply.cpp init_placeholder_parser). Not applied to file names, where 3.0 has the Orca names.
GCODE_ONLY_RENAMES = {
    "print_preset": "print_settings_id",
    "printer_preset": "printer_settings_id",
    "filament_preset": "filament_settings_id",
}

# Approximate translations: equal for a single-tool printer without a dedicated support
# filament (all converted printers are single-tool, no feeder). Listed in the report.
APPROXIMATE = {
    # Orca: first extruder used for something other than support only; 3.0 has only the first used.
    "initial_no_support_extruder": "initial_extruder",
}

# Whole-expression rewrites where the value representation differs (regex on code text).
# Orca enable_pressure_advance is a bool vector; 3.0 pressure_advance is an enum
# (disabled / enabled / automatic_calibration) exposed to the parser as its key string.
REWRITES = [
    (re.compile(r'enable_pressure_advance\[(\w+)\]\s*==\s*"true"'), r'(__PA_MODE__[\1] != "disabled")',
     'enable_pressure_advance[i] == "true"', 'pressure_advance[i] != "disabled"'),
    (re.compile(r'enable_pressure_advance\[(\w+)\]'), r'(__PA_MODE__[\1] != "disabled")',
     'enable_pressure_advance[i]', 'pressure_advance[i] != "disabled"'),
]
_PA_SENTINEL = "__PA_MODE__"  # becomes 3.0 pressure_advance after Orca renames ran



def _add_setting_renames():
    import mapping as M
    simple = {M.num, M.integer, M.boolean, M.text, M.float_or_percent, M.float_list, M.vec2_list}
    for okey, targets in M.MAP.items():
        if len(targets) == 1 and targets[0][2] in simple and targets[0][1] != okey:
            RENAMES.setdefault(okey, targets[0][1])
    # Orca keys whose 3.0 value is converted (enum, %, inverted bool...): using them in
    # G-code needs a human, so they are reported as unresolved.
    for okey, targets in M.MAP.items():
        if okey in RENAMES:
            continue
        identity = all(t[1] == okey and getattr(t[2], "table", None) is not None
                       and all(k == v for k, v in t[2].table.items()) for t in targets)
        if not identity and any(t[2] not in simple for t in targets):
            CONVERTED.add(okey)


CONVERTED = set()
# Orca writes "disabled" as 0, 3.0 as nil (idle_temperature): G-code reads Orca's own value from a
# custom parameter instead, so comparisons like `idle_temperature[i] == 0` keep working.
CONVERTED.add("idle_temperature")

# Config keys whose values are macro templates.
TEMPLATE_KEYS = {"start_gcode", "end_gcode", "before_layer_gcode", "layer_gcode", "toolchange_gcode",
                 "pause_print_gcode", "template_custom_gcode", "between_objects_gcode",
                 "color_change_gcode", "start_filament_gcode", "end_filament_gcode",
                 "output_filename_format"}


def _known():
    with open(os.path.join(HERE, "config_schema.json"), encoding="utf-8") as f:
        schema = json.load(f)
    keys = set(RUNTIME)
    for box in ("printer", "print", "filament"):
        keys |= {i["name"] for i in schema[box]["items"]}
    keys.discard("first_layer_speed")
    return keys


KNOWN = _known()


def _vectors():
    """3.0 keys that are per material slot (Filament/Tool location or overrides): in {...}
    expressions they need an index (PlaceholderParser errors on a bare vector)."""
    with open(os.path.join(HERE, "config_schema.json"), encoding="utf-8") as f:
        schema = json.load(f)
    # Keys with a compatibility rule are collapsed to one value (CustomGCodeMigration.cpp is_vector);
    # the rule is not in the exported schema, so read it from the config definitions.
    defs_src = os.path.join(HERE, "..", "..", "src", "slic3r-domain", "src", "Slic3r", "Domain", "ConfigDefsFDM.cpp")
    with open(defs_src, encoding="utf-8") as f:
        src = f.read()
    scalar, current = set(), None
    for line in src.splitlines():
        m = re.search(r'defs\.add\("([a-z_0-9]+)"', line)
        if m:
            current = m.group(1)
        elif "compatibility_rule" in line and current:
            scalar.add(current)
    out = set()
    for box in ("print", "filament"):
        for i in schema[box]["items"]:
            if i["name"] in scalar:
                continue
            if i["location"] in ("Filament", "Tool") or {"Filament", "Tool"} & set(i["overrides_in"]):
                out.add(i["name"])
    return out


VECTORS = _vectors()
sys.path.insert(0, HERE)
_add_setting_renames()
_IDENT = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")


_SUFFIX_INDEX = re.compile(r"^(.*[A-Za-z_])_([0-9]+)$")


def suffix_index(name, known, extra_renames):
    """Legacy `name_N` (= name[N]) of a vector variable: (base, N) or None."""
    m = _SUFFIX_INDEX.match(name)
    if not m or name in known or name in RENAMES or (extra_renames and name in extra_renames):
        return None
    base = m.group(1)
    target = (extra_renames or {}).get(base) or RENAMES.get(base) or base
    if target.split("[")[0] in known and "[" not in target:
        return base, m.group(2)
    return None


def _rename(name, stats, extra_renames=None):
    if extra_renames and name in extra_renames:
        stats[(name, extra_renames[name])] = stats.get((name, extra_renames[name]), 0) + 1
        return extra_renames[name]
    if name in APPROXIMATE:
        stats[(name, APPROXIMATE[name] + " (approximate)")] = stats.get((name, APPROXIMATE[name] + " (approximate)"), 0) + 1
        return APPROXIMATE[name]
    if name in RENAMES:
        stats[(name, RENAMES[name])] = stats.get((name, RENAMES[name]), 0) + 1
        return RENAMES[name]
    return name


def global_names(text):
    """Names declared with `global name =` (shared between all custom G-code blocks)."""
    return set(re.findall(r"\bglobal\s+([A-Za-z_][A-Za-z0-9_]*)", text))


def _translate_code(code, stats, unresolved, local_vars, known, extra_renames=None):
    """Rewrite identifiers inside one {...} block."""
    out = []
    i, n = 0, len(code)
    prev_tok = prev2 = ""
    while i < n:
        c = code[i]
        if c == '"':
            j = i + 1
            while j < n and code[j] != '"':
                j += 2 if code[j] == "\\" else 1
            out.append(code[i:j + 1])
            i = j + 1
            continue
        if c == "/" and prev_tok in ("=~", "!~"):
            j = i + 1
            while j < n and code[j] != "/":
                j += 2 if code[j] == "\\" else 1
            out.append(code[i:j + 1])
            i = j + 1
            prev_tok = "regex"
            continue
        if c.isdigit() or (c == "." and i + 1 < n and code[i + 1].isdigit()):
            m = re.compile(r"[0-9]*\.?[0-9]+([eE][-+]?[0-9]+)?").match(code, i)
            out.append(m.group(0))
            i = m.end()
            prev_tok = "num"
            continue
        m = _IDENT.match(code, i)
        if m:
            name = m.group(0)
            if prev_tok in ("local", "global"):
                local_vars.add(name)
                out.append(name)
            elif name in KEYWORDS or name in local_vars:
                out.append(name)
            elif suffix_index(name, known, extra_renames):
                base, n_idx = suffix_index(name, known, extra_renames)
                new = _rename(base, stats, extra_renames) + f"[{n_idx}]"
                stats[(name, new)] = stats.get((name, new), 0) + 1
                out.append(new)
            else:
                custom = bool(extra_renames) and name in extra_renames
                new = _rename(name, stats, extra_renames)
                base = new.split("[")[0]
                if base not in known or (name in CONVERTED and not custom):
                    unresolved.add(name)
                indexed = code[m.end():].lstrip().startswith("[")
                vector_ref = prev_tok == "(" and prev2 in ("is_nil", "size", "empty")
                is_vector = base in VECTORS or base.startswith("custom_parameter_filament_")
                if is_vector and "[" not in new and not indexed and not vector_ref:
                    # Orca scalar (or single-variant) value; 3.0 keeps it per material slot.
                    key = (name, new + "[current_extruder]")
                    stats[key] = stats.get(key, 0) + 1
                    new += "[current_extruder]"
                out.append(new)
            prev2, prev_tok = prev_tok, name
            i = m.end()
            continue
        if code.startswith("=~", i) or code.startswith("!~", i):
            out.append(code[i:i + 2])
            prev_tok = code[i:i + 2]
            i += 2
            continue
        if not c.isspace():
            prev2, prev_tok = prev_tok, c
        out.append(c)
        i += 1
    return "".join(out)


def translate(text, extra_known=(), extra_renames=None):
    """Returns (translated text, {(orca, new): count}, set(unresolved names or syntax problems)).
    extra_known: additional valid names (globals declared in other blocks, filename variables).
    extra_renames: vendor-specific renames (Orca-only settings carried as custom parameters)."""
    stats, unresolved, local_vars = {}, set(), set()
    extra_renames = extra_renames or {}
    known = KNOWN | set(extra_known) | set(extra_renames.values())
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == "{":
            j = text.find("}", i + 1)
            # strings inside code may contain '}' - skip over them
            k = i + 1
            while True:
                q = text.find('"', k, j if j != -1 else n)
                if q == -1:
                    break
                e = text.find('"', q + 1)
                if e == -1:
                    break
                k = e + 1
                if j != -1 and j < k:
                    j = text.find("}", k)
            if j == -1:
                unresolved.add("<unterminated {>")
                out.append(text[i:])
                break
            code = text[i + 1:j]
            for rx, repl, label_from, label_to in REWRITES:
                code, nsub = rx.subn(repl, code)
                if nsub:
                    stats[(label_from, label_to)] = stats.get((label_from, label_to), 0) + nsub
            code = _translate_code(code, stats, unresolved, local_vars, known | {_PA_SENTINEL}, extra_renames)
            out.append("{" + code.replace(_PA_SENTINEL, "pressure_advance") + "}")
            i = j + 1
            continue
        if c == "[":
            m = re.compile(r"\[([A-Za-z_][A-Za-z0-9_]*)(\[([A-Za-z_][A-Za-z0-9_]*|[0-9]+)\])?\]").match(text, i)
            if not m:
                unresolved.add(f"<'[' not starting a placeholder: {text[i:i + 20]!r}>")
                out.append(c)
                i += 1
                continue
            name, idx = m.group(1), m.group(3)
            si = suffix_index(name, known, extra_renames) if idx is None else None
            if si:
                new = _rename(si[0], stats, extra_renames) + f"[{si[1]}]"
                stats[(name, new)] = stats.get((name, new), 0) + 1
                out.append("{" + new + "}")
                i = m.end()
                continue
            custom = name in extra_renames
            new = _rename(name, stats, extra_renames)
            base = new.split("[")[0]
            if base not in KNOWN and (base in known or name in local_vars):
                # 3.0 legacy [name] expansion only reads config/runtime variables, not macro
                # variables or custom parameters: use the equivalent {name} expression.
                ix = idx if idx is not None else (
                    "current_extruder" if base.startswith("custom_parameter_filament_") else None)
                out.append("{" + new + (f"[{_rename(ix, {}, extra_renames)}]" if ix and "[" not in new else "") + "}")
                i = m.end()
                continue
            if new.split("[")[0] not in known or (name in CONVERTED and not custom):
                unresolved.add(name)
            if idx is not None:
                if idx.isdigit():
                    unresolved.add(f"<numeric index not supported by 3.0 legacy syntax: {m.group(0)}>")
                elif _rename(idx, stats, extra_renames).split("[")[0] not in known:
                    unresolved.add(idx)
                if "[" in new:
                    unresolved.add(f"<double index after rename: {m.group(0)}>")
                out.append(f"[{new}[{_rename(idx, {}, extra_renames)}]]")
            else:
                out.append(f"[{new}]")
            i = m.end()
            continue
        out.append(c)
        i += 1
    return "".join(out), stats, unresolved


# ---------------------------------------------------------------- Bambu-only runtime variables
# 3.0 sets Orca's runtime variables itself (RUNTIME); the import re-creates only the Bambu cutter /
# extruder-change retraction values (the settings behind them are out of scope), with Orca's rule:
# the machine value, overridden by the filament's filament_* value unless nil.
BBL_NAMES = (
    "retraction_distance_when_cut", "long_retraction_when_cut",
    "retraction_distance_when_ec", "long_retraction_when_ec",
)


def _lit(x):
    return format(x, ".10g")


def _vec(items, pad=-1):
    """Initializer list; one-element lists are padded (a 1-tuple does not parse as a vector)."""
    items = list(items)
    if len(items) == 1:
        items.append(str(pad))
    return "(" + ", ".join(items) + ")"


def bbl_preamble(text, mcfg, slots):
    names = [n for n in BBL_NAMES if re.search(r"\b" + n + r"\b", text)]
    if not names or mcfg is None:
        return ""
    n = max(1, slots)
    want = set(names)
    code = ""
    if want & {"retraction_distance_when_ec", "long_retraction_when_ec"}:
        # Orca: filament options (filament_options_with_variant), value of the initial filament
        code += ("{local retraction_distance_when_ec = retraction_distances_when_ec[initial_extruder]}"
                 "{local long_retraction_when_ec = long_retractions_when_ec[initial_extruder]}")
    for short, key in (("cut", "retraction_distances_when_cut"),):
        lkey = key.replace("retraction_distances", "long_retractions")
        if want & {f"retraction_distance_when_{short}", f"long_retraction_when_{short}"}:
            # Orca: the machine value, overridden by the filament's filament_* value unless nil
            md = _floats(mcfg.get(key), n, 0.0)
            ml = _bools(mcfg.get(lkey), n)
            code += (f"{{local _rd_{short} = {_vec(_lit(x) for x in md)}}}{{local _lr_{short} = {_vec(('true' if x else 'false') for x in ml)}}}"
                     f"{{local retraction_distance_when_{short} = _rd_{short}[initial_extruder]}}"
                     f"{{if not is_nil(filament_{key}[initial_extruder])}}"
                     f"{{retraction_distance_when_{short} = filament_{key}[initial_extruder]}}{{endif}}"
                     f"{{local long_retraction_when_{short} = _lr_{short}[initial_extruder]}}"
                     f"{{if not is_nil(filament_{lkey}[initial_extruder])}}"
                     f"{{long_retraction_when_{short} = filament_{lkey}[initial_extruder]}}{{endif}}")
    return code


def _floats(v, n, default):
    v = v if isinstance(v, list) else ([v] if v is not None else [])
    out = []
    for i in range(n):
        try:
            out.append(float(str(v[min(i, len(v) - 1)])))
        except (ValueError, IndexError):
            out.append(default)
    return out


def _bools(v, n):
    v = v if isinstance(v, list) else ([v] if v is not None else [])
    out = []
    for i in range(n):
        try:
            out.append(str(v[min(i, len(v) - 1)]).strip().lower() in ("1", "true"))
        except IndexError:
            out.append(False)
    return out
