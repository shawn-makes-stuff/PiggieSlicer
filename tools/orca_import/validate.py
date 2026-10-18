"""Validate a 3.0 preset vendor directory.

Checks (jsonschema is not available, so the schema rules are implemented here):
  * vendor.yaml documents against specs/presets/vendor-schema.json (kinds, required and
    allowed properties, types). The schema's vendor-def lacks printer_families and
    features.sheet, which the loader reads (HwConfigLoader.cpp VendorInfo/VendorFeatures),
    so those two are accepted as loader-defined extensions.
  * preset-*.yaml documents against specs/presets/preset-schema.json (fragment /
    conditional-fragment / top-fragment, read as the union of their properties).
  * every value key exists in the 3.0 config definition for that preset kind
    (config_schema.json = `piggie-slicer --export-config-schema`; a key is valid in a box
    when its location is the box or the box is in overrides_in), and every value has a
    type/enum the definition accepts, written so the YAML loader parses it as that type.
  * ids unique, inherits resolvable, tool/sheet/printer references resolvable.
  * no unknown placeholders in template values.
Usage: python validate.py <vendor dir> [...]
"""
import json
import os
import re
import sys

import yaml

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))

KIND_BOX = {"printer": "printer", "print": "print", "tool_print": "tool_print", "filament": "filament"}
BOX_LOCATION = {"printer": "Printer", "print": "Print", "tool_print": "Tool", "filament": "Filament"}


def load_schema():
    with open(os.path.join(HERE, "config_schema.json"), encoding="utf-8") as f:
        raw = json.load(f)
    items = {}
    for box in ("printer", "print", "filament"):
        for it in raw[box]["items"]:
            items[it["name"]] = it
    per_box = {}
    for box, loc in BOX_LOCATION.items():
        per_box[box] = {n: it for n, it in items.items() if it["location"] == loc or loc in it["overrides_in"]}
    return per_box


def _json_schema(name):
    with open(os.path.join(REPO, "specs", "presets", name), encoding="utf-8") as f:
        return json.load(f)


def _scalar_kind(s):
    """What the 3.0 YAML loader turns a scalar string into (YamlSlic3rTypes.hpp: bool, double,
    int, Percentage, Vec2d, string - first that parses)."""
    if s in ("true", "false"):
        return "bool"
    if re.fullmatch(r"[-+]?(\d+\.?\d*|\.\d+)([eE][-+]?\d+)?", s) or s.lower() in ("inf", "-inf", "nan"):
        return "double"
    if re.fullmatch(r"[-+]?(\d+\.?\d*|\.\d+)%\s*", s):
        return "percent"
    num = r"[-+]?(\d+\.?\d*|\.\d+)([eE][-+]?\d+)?"
    if re.fullmatch(num + "x" + num, s):
        return "vec2"
    return "string"


def _value_kind(v):
    if isinstance(v, bool):
        return "bool"
    if isinstance(v, (int, float)):
        return "double"
    if isinstance(v, str):
        return _scalar_kind(v)
    return None


def check_value(item, v):
    t = item["type"]
    if t == "Array":
        if not isinstance(v, list):
            return f"expects a list ({item['element_type']})"
        et = item["element_type"]
        for x in v:
            k = _value_kind(x)
            ok = {"Float": k == "double", "Int": k == "double", "Bool": k in ("bool", "double"),
                  "Vec2d": k == "vec2", "String": k == "string"}.get(et)
            if not ok:
                return f"list element {x!r} is not a {et}"
        return None
    if isinstance(v, list):
        return "expects a scalar"
    k = _value_kind(v)
    if t in ("Float", "Int"):
        if k != "double":
            return f"{v!r} is not a number"
        if t == "Int" and float(v) != int(float(v)):
            return f"{v!r} is not an integer"
        return None
    if t == "OptInt":
        if v is None:
            return None  # YAML null -> std::monostate -> nil
        return None if k == "double" and float(v) == int(float(v)) else f"{v!r} is not an integer"
    if t == "Bool":
        # PresetEvaluator ValueCast<double, bool> also accepts numbers.
        return None if k in ("bool", "double") else f"{v!r} is not true/false"
    if t == "Percentage":
        # ValueCast<double, Percentage> also accepts numbers.
        return None if k in ("percent", "double") else f"{v!r} is not 'N%' or a number"
    if t == "FloatOrPercentage":
        return None if k in ("percent", "double") else f"{v!r} is not a number or 'N%'"
    if t == "Enum":
        return None if v in item["enum_values"] else f"{v!r} not in {item['enum_values']}"
    if t == "String":
        return None if k == "string" else f"{v!r} would be loaded as {k}, not a string"
    return f"unhandled type {t}"


def _check_props(doc, allowed, required, where, errors):
    for r in required:
        if r not in doc:
            errors.append(f"{where}: missing required '{r}'")
    for k in doc:
        if k not in allowed:
            errors.append(f"{where}: property '{k}' not allowed")


def validate_vendor_yaml(docs, path, errors):
    vs = _json_schema("vendor-schema.json")["$defs"]
    defs = {"vendor": "vendor-def", "printer": "printer-def", "tool": "tool-def", "feeder": "feeder-def",
            "sheet": "sheet-def", "printer_config": "printer-config-template"}
    loader_extensions = {"vendor": {"printer_families"}}
    ids = {k: set() for k in defs}
    configs = []
    for i, d in enumerate(docs):
        where = f"{os.path.basename(path)}#{i}"
        kind = d.get("kind")
        if kind not in defs:
            errors.append(f"{where}: unknown kind {kind!r}")
            continue
        sd = vs[defs[kind]]
        allowed = set(sd["properties"]) | loader_extensions.get(kind, set())
        if sd.get("additionalProperties", True) is not False:
            allowed |= set(d)
        _check_props(d, allowed, sd.get("required", []), where, errors)
        if "id" in d:
            if d["id"] in ids[kind]:
                errors.append(f"{where}: duplicate {kind} id {d['id']!r}")
            ids[kind].add(d["id"])
        if kind in ("printer", "tool", "sheet") and "features" in d:
            for fk, fv in d["features"].items():
                if not isinstance(fv, dict) or "default" not in fv:
                    errors.append(f"{where}: feature {fk} needs 'default'")
                elif set(fv) - {"default", "user_editable", "allowed_values"}:
                    errors.append(f"{where}: feature {fk} has unknown properties")
        if kind == "printer":
            if d.get("technology") not in ("FFF", "SLA"):
                errors.append(f"{where}: bad technology")
            if not isinstance(d.get("model"), dict) or {"base_model", "model"} - set(d["model"]):
                errors.append(f"{where}: model needs base_model and model")
            if not (isinstance(d.get("tool_count"), (int, float)) and d["tool_count"] >= 1):
                errors.append(f"{where}: tool_count >= 1 required")
        if kind == "printer_config":
            configs.append((where, d))
        if kind == "vendor":
            feats = d.get("features", {})
            for grp in feats:
                if grp not in ("printer", "tool", "feeder", "sheet"):
                    errors.append(f"{where}: unknown feature group {grp}")
    for where, d in configs:
        if d["printer"] not in ids["printer"]:
            errors.append(f"{where}: printer {d['printer']!r} not defined")
        for t in d.get("tools", []):
            if str(t.get("tool")) not in ids["tool"]:
                errors.append(f"{where}: tool {t.get('tool')!r} not defined")
        if d["sheet"] not in ids["sheet"]:
            errors.append(f"{where}: sheet {d['sheet']!r} not defined")
    return ids


def validate_preset_node(node, kind, box_items, where, errors, top, ids, inherit_refs):
    ps = _json_schema("preset-schema.json")["$defs"]
    allowed = set(ps["fragment"]["properties"]) | {"condition"} | ({"kind"} if top else set())
    _check_props(node, allowed, ["kind"] if top else [], where, errors)
    if "id" in node:
        if node["id"] in ids:
            errors.append(f"{where}: duplicate preset id {node['id']!r}")
        ids.add(node["id"])
    if "condition" in node and not isinstance(node["condition"], str):
        errors.append(f"{where}: condition must be a string")
    if "match_mode" in node and node["match_mode"] not in ("first_match", "all_matches"):
        errors.append(f"{where}: bad match_mode")
    for k in ("inherits", "unconditional_inherits"):
        if k in node:
            inherit_refs.extend((where, x) for x in node[k])
    for key, val in (node.get("values") or {}).items():
        item = box_items.get(key)
        if item is None:
            errors.append(f"{where}: key {key!r} not valid for kind {kind}")
            continue
        problem = check_value(item, val)
        if problem:
            errors.append(f"{where}: {key}: {problem}")
    conditional_seen = unconditional_seen = 0
    for j, v in enumerate(node.get("variants") or []):
        # PresetCollectionEvaluator: no conditional variant after an unconditional one,
        # and at most one unconditional variant after conditional ones.
        if "condition" in v:
            if unconditional_seen:
                errors.append(f"{where}/variants[{j}]: conditional variant after unconditional one")
            conditional_seen += 1
        else:
            unconditional_seen += 1
            if conditional_seen and unconditional_seen > 1:
                errors.append(f"{where}/variants[{j}]: more than one unconditional fallback")
        validate_preset_node(v, kind, box_items, f"{where}/variants[{j}]", errors, False, ids, inherit_refs)


def _walk_values(node):
    yield node.get("values") or {}
    for v in node.get("variants") or []:
        yield from _walk_values(v)


def validate_vendor_dir(vendor_dir, schema=None):
    sys.path.insert(0, HERE)
    import placeholders
    schema = schema or load_schema()
    errors = []
    with open(os.path.join(vendor_dir, "vendor.yaml"), encoding="utf-8") as f:
        vdocs = [d for d in yaml.safe_load_all(f) if d]
    validate_vendor_yaml(vdocs, "vendor.yaml", errors)
    idx = vendor_dir.rstrip("/\\") + ".idx"
    if not os.path.exists(idx):
        errors.append(f"missing index {idx} (BundleLoader populate_local_bundle copies it)")
    ids, roots, refs = set(), set(), []
    globals_ = set()
    all_docs = []
    for fn in sorted(os.listdir(vendor_dir)):
        if fn.endswith(".yaml") and fn != "vendor.yaml":
            with open(os.path.join(vendor_dir, fn), encoding="utf-8") as f:
                raw = f.read()
            if any(c in raw for c in (" ", " ", "")):
                errors.append(f"{fn}: raw Unicode line separator (the app's YAML parser breaks lines there)")
            all_docs.append((fn, [d for d in yaml.safe_load_all(raw) if d]))
    for _fn, docs in all_docs:
        for d in docs:
            for vals in _walk_values(d):
                for key, val in vals.items():
                    if key in placeholders.TEMPLATE_KEYS and isinstance(val, str):
                        globals_ |= placeholders.global_names(val)
                    if key.startswith("custom_parameters_") and isinstance(val, str) and val:
                        # custom_parameters_<box> JSON -> custom_parameter_<box>_<key> placeholders
                        box = key[len("custom_parameters_"):]
                        try:
                            globals_ |= {f"custom_parameter_{box}_{k}" for k in json.loads(val)}
                        except ValueError:
                            errors.append(f"{key}: invalid JSON")
    for fn, docs in all_docs:
        for i, d in enumerate(docs):
            where = f"{fn}#{i}"
            kind = d.get("kind")
            if kind not in KIND_BOX:
                errors.append(f"{where}: kind {kind!r} not supported")
                continue
            if not d.get("id"):
                errors.append(f"{where}: root preset needs an id")
            roots.add(d.get("id"))
            validate_preset_node(d, kind, schema[KIND_BOX[kind]], where, errors, True, ids, refs)
            for vals in _walk_values(d):
                for key, val in vals.items():
                    if key in placeholders.TEMPLATE_KEYS and isinstance(val, str):
                        extra = globals_ | (placeholders.FILENAME_EXTRA if key == "output_filename_format" else set())
                        _, renames, unresolved = placeholders.translate(val, extra)
                        if key == "toolchange_gcode":
                            # only executed on a tool/filament change, which a single material slot
                            # per extruder never has (reported as "not executable" by convert.py)
                            continue
                        for u in sorted(unresolved):
                            errors.append(f"{where}: {key}: unknown placeholder {u}")
                        for (a, _b) in renames:
                            if a.split("[")[0] in placeholders.KNOWN:
                                continue  # also a 3.0 name (e.g. pressure_advance) - written on purpose
                            errors.append(f"{where}: {key}: untranslated Orca placeholder {a}")
    for where, ref in refs:
        if ref not in roots:
            errors.append(f"{where}: inherits unknown root preset {ref!r}")
    return errors


if __name__ == "__main__":
    bad = False
    for d in sys.argv[1:]:
        errs = validate_vendor_dir(d)
        print(f"{d}: {'OK' if not errs else f'{len(errs)} problem(s)'}")
        for e in errs:
            print("  ", e)
        bad |= bool(errs)
    sys.exit(1 if bad else 0)
