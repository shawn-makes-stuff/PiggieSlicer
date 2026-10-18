"""Extract Orca's built-in preset defaults from its source (v2.5.0-alpha).

Orca starts a root vendor preset (no "inherits") from the default preset of its type
(PresetBundle::load_vendor_configs_from_json -> presets_collection->default_preset()), whose
config holds PrintConfigDef's default for every key of that preset type. Those defaults live in
src/libslic3r/PrintConfig.cpp (def->set_default_value(...)); the key lists per type in
src/libslic3r/Preset.cpp (s_Preset_print_options, s_Preset_filament_options,
s_Preset_printer_options + s_Preset_machine_limits_options + extruder option keys).

Only literal defaults are extracted (numbers, bools, strings, literal vectors, enum values that
resolve through the s_keys_map_* tables); anything computed is left out and listed in
"unparsed". Values are written in Orca's JSON form (strings, or lists of strings for vectors),
so the converter treats a default exactly like a value read from a profile.
"""
import re


def _string_list(src, start_pattern):
    m = re.search(start_pattern + r"\s*\{(.*?)\};", src, re.S)
    if not m:
        raise ValueError(f"list {start_pattern} not found")
    body = re.sub(r"/\*.*?\*/", "", m.group(1), flags=re.S)
    body = re.sub(r"//[^\n]*", "", body)
    return re.findall(r'"([^"]+)"', body)


def option_lists(preset_cpp, printconfig_cpp):
    print_opts = _string_list(preset_cpp, r"static std::vector<std::string> s_Preset_print_options")
    fil_opts = _string_list(preset_cpp, r"static std::vector<std::string> s_Preset_filament_options")
    printer_opts = _string_list(preset_cpp, r"static std::vector<std::string> s_Preset_printer_options")
    limits = _string_list(preset_cpp, r"static std::vector<std::string> s_Preset_machine_limits_options")
    extruder = _string_list(printconfig_cpp, r"m_extruder_option_keys =")
    return {"process": print_opts, "filament": fil_opts, "machine": printer_opts + limits + extruder}


def _enum_maps(src):
    maps = {}
    for name, body in re.findall(r"s_keys_map_(\w+)\s*(?:=\s*)?\{(.*?)\};", src, re.S):
        for key, enumerator in re.findall(r'\{\s*"([^"]+)"\s*,\s*([^}]+?)\s*\}', body):
            e = enumerator.strip()
            e = re.sub(r"^int\((.*)\)$", r"\1", e).strip()
            maps.setdefault(name, {})[e.split("::")[-1]] = key
    return maps


_NUM = r"[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?f?"


def _num(s):
    s = s.strip().rstrip("f")
    x = float(s)
    return str(int(x)) if x.is_integer() else repr(x)


def _parse_value(ctor, args, enum_maps, constants):
    ctor_base = ctor.replace("Nullable", "")
    a = args.strip()
    is_vector = ctor_base.endswith("s") and not ctor_base.startswith("ConfigOptionEnum<")
    if a.startswith("(") and a.endswith(")"):
        a = a[1:-1].strip()
    if not is_vector and a.startswith("{") and a.endswith("}"):
        a = a[1:-1].strip()
    a = constants.get(a, a)
    # L("text") is a translation marker around a literal; adjacent literals concatenate in C++.
    a = re.sub(r'\bL\(\s*("(?:[^"\\]|\\.)*")\s*\)', r"\1", a)
    a = re.sub(r'"\s*\n\s*"', "", a)
    if "nil_value()" in a:
        return ["nil"] if is_vector else "nil"
    if ctor_base == "ConfigOptionString" and a == "":
        return ""
    if ctor_base == "ConfigOptionBool" and a in ("0", "1"):
        return a
    if ctor_base == "ConfigOptionEnumsGeneric":
        out = []
        for it in [x.strip() for x in a.strip("{} ").split(",") if x.strip()]:
            enum_type, _, e = it.replace(" ", "").rpartition("::")
            key = enum_maps.get(enum_type, {}).get(e)
            if key is None:
                raise ValueError(f"enum {it}")
            out.append(key)
        return out
    if ctor_base == "ConfigOptionPoints":
        pts = re.findall(rf"Vec2d\(\s*({_NUM})\s*,\s*({_NUM})\s*\)", a)
        if not pts and a.strip("{} "):
            raise ValueError(a)
        return [f"{_num(x)}x{_num(y)}" for x, y in pts]
    if ctor_base.startswith("ConfigOptionEnum<"):
        enum_type = ctor_base[len("ConfigOptionEnum<"):-1]
        e = a.split("::")[-1].strip()
        key = enum_maps.get(enum_type, {}).get(e)
        if key is None:
            raise ValueError(f"enum {enum_type}::{e}")
        return key
    if ctor_base == "ConfigOptionBool":
        if a in ("true", "false"):
            return "1" if a == "true" else "0"
        raise ValueError(a)
    if ctor_base in ("ConfigOptionFloat", "ConfigOptionInt"):
        if re.fullmatch(_NUM, a):
            return _num(a)
        raise ValueError(a)
    if ctor_base == "ConfigOptionPercent":
        if re.fullmatch(_NUM, a):
            return _num(a) + "%"
        raise ValueError(a)
    if ctor_base == "ConfigOptionFloatOrPercent":
        m = re.fullmatch(rf"({_NUM})\s*,\s*(true|false)", a)
        if m:
            return _num(m.group(1)) + ("%" if m.group(2) == "true" else "")
        raise ValueError(a)
    if ctor_base == "ConfigOptionString":
        m = re.fullmatch(r'"((?:[^"\\]|\\.)*)"', a)
        if m:
            return bytes(m.group(1), "utf-8").decode("unicode_escape")
        raise ValueError(a)
    if ctor_base in ("ConfigOptionFloats", "ConfigOptionInts", "ConfigOptionBools", "ConfigOptionPercents",
                     "ConfigOptionStrings", "ConfigOptionFloatsOrPercents"):
        inner = a
        if inner.startswith("{") and inner.endswith("}"):
            inner = inner[1:-1]
        elif not inner:
            return []
        items = [x.strip() for x in re.split(r",(?![^(]*\))(?=(?:[^\"]*\"[^\"]*\")*[^\"]*$)", inner) if x.strip()]
        out = []
        for it in items:
            if ctor_base == "ConfigOptionStrings":
                m = re.fullmatch(r'"((?:[^"\\]|\\.)*)"', it)
                if not m:
                    raise ValueError(it)
                out.append(bytes(m.group(1), "utf-8").decode("unicode_escape"))
            elif ctor_base == "ConfigOptionBools":
                if it not in ("true", "false"):
                    raise ValueError(it)
                out.append("1" if it == "true" else "0")
            elif ctor_base == "ConfigOptionFloatsOrPercents":
                m = re.fullmatch(rf"FloatOrPercent\s*[{{(]\s*({_NUM})\s*,\s*(true|false)\s*[}})]", it)
                if not m:
                    raise ValueError(it)
                out.append(_num(m.group(1)) + ("%" if m.group(2) == "true" else ""))
            else:
                if not re.fullmatch(_NUM, it):
                    raise ValueError(it)
                out.append(_num(it) + ("%" if ctor_base == "ConfigOptionPercents" else ""))
        return out
    raise ValueError(f"unsupported {ctor}")


def defaults(printconfig_cpp, constants_hpp=""):
    src = printconfig_cpp
    enum_maps = _enum_maps(src)
    # PrintConfigConstants.hpp: #define INITIAL_LAYER_HEIGHT 0.2 ...
    constants = dict(re.findall(r"#define\s+(\w+)\s+(\S+)", constants_hpp))
    out, unparsed = {}, {}
    # split into per-definition chunks
    starts = [(m.start(), m.group(1)) for m in re.finditer(r'this->add(?:_nullable)?\("([A-Za-z0-9_]+)"', src)]
    for i, (pos, key) in enumerate(starts):
        end = starts[i + 1][0] if i + 1 < len(starts) else len(src)
        chunk = src[pos:end]
        m = re.search(r"set_default_value\(\s*new\s+(ConfigOption[A-Za-z]+(?:<\w+>)?)\s*(\{.*?\}|\(.*?\))\s*\)\s*;", chunk, re.S)
        if not m:
            continue
        try:
            out[key] = _parse_value(m.group(1), m.group(2), enum_maps, constants)
        except ValueError as e:
            unparsed[key] = f"{m.group(1)}{m.group(2)[:60]}: {e}"
    # Machine limits are defined in a loop over the AxisDefault table
    # { "x", { max_feedrate }, { max_acceleration }, { max_jerk } }.
    pair = rf"\{{\s*({_NUM})\s*,\s*({_NUM})\s*\}}"
    for axis, *v in re.findall(rf'\{{\s*"([xyze])"\s*,\s*{pair}\s*,\s*{pair}\s*,\s*{pair}\s*\}}', src):
        for i, name in enumerate(("machine_max_speed_", "machine_max_acceleration_", "machine_max_jerk_")):
            out[name + axis] = [_num(v[2 * i]), _num(v[2 * i + 1])]
    for k in ("machine_max_speed_", "machine_max_acceleration_", "machine_max_jerk_"):
        unparsed.pop(k, None)
    return out, unparsed


def enum_ints(printconfig_cpp, printconfig_hpp):
    """option key -> {Orca value string: enum integer}, for options with an enum_keys_map
    (Orca exposes ConfigOptionEnumsGeneric vectors to custom G-code as these integers)."""
    decl = {}
    for name, body in re.findall(r"enum\s+(?:class\s+)?(\w+)\s*(?::\s*[\w:]+)?\s*\{(.*?)\}", printconfig_hpp, re.S):
        body = re.sub(r"//[^\n]*", "", body)
        val, out = -1, {}
        for item in [x.strip() for x in body.split(",") if x.strip()]:
            if "=" in item:
                n, v = [x.strip() for x in item.split("=", 1)]
                try:
                    val = int(v, 0)
                except ValueError:
                    val = out.get(v, val + 1)
            else:
                n, val = item, val + 1
            out[n] = val
        decl[name] = out
    maps = _enum_maps(printconfig_cpp)
    result = {}
    starts = [(m.start(), m.group(1)) for m in re.finditer(r'this->add(?:_nullable)?\("([A-Za-z0-9_]+)"', printconfig_cpp)]
    for i, (pos, key) in enumerate(starts):
        end = starts[i + 1][0] if i + 1 < len(starts) else len(printconfig_cpp)
        m = re.search(r"ConfigOptionEnum<(\w+)>::get_enum_values\(\)", printconfig_cpp[pos:end])
        if m and m.group(1) in maps and m.group(1) in decl:
            vals = {key: decl[m.group(1)].get(e) for e, key in maps[m.group(1)].items()}
            result[key] = {k: v for k, v in vals.items() if v is not None}
    return result


def build(preset_cpp, printconfig_cpp, constants_hpp="", printconfig_hpp=""):
    lists = option_lists(preset_cpp, printconfig_cpp)
    vals, unparsed = defaults(printconfig_cpp, constants_hpp)
    result = {}
    for kind, keys in lists.items():
        result[kind] = {k: vals[k] for k in keys if k in vals}
    missing = {kind: sorted(k for k in keys if k not in vals) for kind, keys in lists.items()}
    # Option sets whose vectors hold one value per extruder variant (PrintConfig.cpp
    # *_options_with_variant; printer_options_with_variant_2 = (normal, silent) pairs per variant)
    # and the per-extruder keys (init_extruder_option_keys).
    variant_sets = {name: sorted(_string_list(printconfig_cpp, rf"std::set<std::string> {name} ="))
                    for name in ("print_options_with_variant", "filament_options_with_variant",
                                 "printer_options_with_variant_1", "printer_options_with_variant_2")}
    variant_sets["extruder_option_keys"] = sorted(set(_string_list(printconfig_cpp, r"m_extruder_option_keys =")))
    return {"defaults": result, "no_literal_default": missing, "unparsed": unparsed, "variant_sets": variant_sets,
            "enum_ints": enum_ints(printconfig_cpp, printconfig_hpp) if printconfig_hpp else {}}
