"""Convert OrcaSlicer vendor profiles into a PiggieSlicer 3.0 preset vendor bundle.

Usage:
  python convert.py --orca-install "C:/Program Files/OrcaSlicer" \
      --orca-profiles <dir from fetch_orca_profiles.py> \
      --out <repo>/resources/presets/orca-import Anycubic Elegoo

Writes <out>/<Vendor>/ (vendor.yaml, preset-*.yaml, assets/), <out>/<Vendor>.idx and
<report-dir>/<Vendor>.md. Output is deterministic: re-running gives identical files.
"""
import argparse
import base64
import hashlib
import json
import os
import re
import shutil
import sys

import yaml

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mapping as M  # noqa: E402
import orca_condition  # noqa: E402
import variants  # noqa: E402
import orca_source  # noqa: E402
import placeholders  # noqa: E402
import validate  # noqa: E402

# Orca BedType (PrintConfig.hpp enum order) -> (3.0 sheet id, label, temp key prefix, sheet type).
# The sheet type is Orca's curr_bed_type enum key, so G-code comparing curr_bed_type keeps working
# with 3.0's sheet_type; labels are Orca's enum labels (PrintConfig.cpp "curr_bed_type").
BED_TYPES = {
    1: ("cool_plate", "Smooth Cool Plate", "cool_plate_temp", "Cool Plate"),
    2: ("engineering_plate", "Engineering Plate", "eng_plate_temp", "Engineering Plate"),
    3: ("high_temp_plate", "Smooth High Temp Plate", "hot_plate_temp", "High Temp Plate"),
    4: ("textured_pei_plate", "Textured PEI Plate", "textured_plate_temp", "Textured PEI Plate"),
    5: ("textured_cool_plate", "Textured Cool Plate", "textured_cool_plate_temp", "Textured Cool Plate"),
    6: ("supertack_plate", "Cool Plate (SuperTack)", "supertack_plate_temp", "Supertack Plate"),
}
ORCA_DEFAULT_BED = 3  # Preset::get_default_bed_type falls back to btPEI (High Temp Plate)


def stable_id(*parts):
    h = hashlib.md5("\x1f".join(parts).encode("utf-8")).digest()
    return base64.b64encode(h).decode("ascii")[:22]


def slug(s):
    return re.sub(r"[^A-Za-z0-9]+", "-", s).strip("-")


def nozzle_key(d):
    """0.4 -> '04', 0.25 -> '025' (Prusa feature naming)."""
    return format(d, "g").replace(".", "")


def default_bed_type(machine_cfg, report):
    """Mirror Orca Preset::get_default_bed_type: numeric string > 0, otherwise btPEI."""
    raw = machine_cfg.get("default_bed_type", "")
    if not raw:
        return ORCA_DEFAULT_BED
    s = str(raw)
    n = int(s) if s.lstrip("-").isdigit() else 0
    if n > 0 and n in BED_TYPES:
        return n
    report.note(f"default_bed_type {s!r} is not numeric; Orca (Preset::get_default_bed_type) then uses "
                f"High Temp Plate, so the import does too")
    return ORCA_DEFAULT_BED


class Report:
    def __init__(self, vendor):
        self.vendor = vendor
        self.dropped = {}       # (kind, orca key) -> {reason: count}
        self.notes = []
        self.errors = []
        self.placeholder_renames = {}
        self.unresolved = {}    # placeholder -> set(fields)
        self.counts = {}
        self.orca_ignored = {}
        self.fallbacks = {}     # description -> preset names
        self.inert = {}
        self.custom = {}

    def drop(self, kind, key, reason):
        self.dropped.setdefault((kind, key), {}).setdefault(reason, 0)
        self.dropped[(kind, key)][reason] += 1

    def note(self, msg):
        if msg not in self.notes:
            self.notes.append(msg)

    def write(self, path):
        lines = [f"# Orca import report: {self.vendor}", ""]
        status = "FAILED" if (self.errors or self.unresolved) else "OK"
        lines.append(f"Status: **{status}**")
        lines.append("")
        for k, v in self.counts.items():
            lines.append(f"- {k}: {v}")
        if self.errors:
            lines += ["", "## Errors", ""] + [f"- {e}" for e in self.errors]
        if self.unresolved:
            lines += ["", "## Unresolved placeholders (G-code export would fail)", ""]
            for p in sorted(self.unresolved):
                lines.append(f"- `{p}` in {', '.join(sorted(self.unresolved[p]))}")
        if self.custom:
            lines += ["", "## Orca-only settings carried as custom parameters", "",
                      "| Orca setting | 3.0 placeholder |", "|---|---|"]
            for k, (bx, name) in sorted(self.custom.items()):
                lines.append(f"| `{k}` | `{name}` (custom_parameters_{bx}) |")
        if self.inert:
            lines += ["", "## Not executable on the imported configs (kept as Orca wrote them)", "",
                      "Tool-change G-code only runs on a tool/filament change; the imported configs have one "
                      "material slot and no feeder, so 3.0 never executes it.", ""]
            for p in sorted(self.inert):
                lines.append(f"- `{p}` in {len(self.inert[p])} preset(s), e.g. {sorted(self.inert[p])[0]}")
        if self.fallbacks:
            lines += ["", "## Fallbacks that change the G-code", ""]
            for d, names in sorted(self.fallbacks.items()):
                lines.append(f"- {d}: {len(names)} config(s): {', '.join(sorted(names))}")
        if self.placeholder_renames:
            lines += ["", "## Placeholder translations applied", "", "| Orca | 3.0 | uses |", "|---|---|---|"]
            for (a, b), n in sorted(self.placeholder_renames.items()):
                lines.append(f"| `{a}` | `{b}` | {n} |")
        if self.notes:
            lines += ["", "## Notes", ""] + [f"- {n}" for n in self.notes]
        if self.orca_ignored:
            lines += ["", "## Keys Orca's own loader drops (PrintConfigDef::handle_legacy)", "",
                      "| Orca key | presets | reason |", "|---|---|---|"]
            for (k, why), names in sorted(self.orca_ignored.items()):
                lines.append(f"| `{k}` | {len(names)} | {why} |")
        lines += ["", "## Dropped / unmapped Orca settings", "",
                  "Count = number of converted presets carrying the key.", "",
                  "| kind | Orca key | presets | reason |", "|---|---|---|---|"]
        for (kind, key) in sorted(self.dropped):
            for reason, n in sorted(self.dropped[(kind, key)].items()):
                lines.append(f"| {kind} | `{key}` | {n} | {reason} |")
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(lines) + "\n")


ORCA_NUMBER_TYPES = {"coFloat", "coFloats", "coInt", "coInts"}
_LEADING_NUMBER = re.compile(r"\s*[-+]?(\d+\.?\d*|\.\d+)([eE][-+]?\d+)?")


def orca_read(key, val, orca_types, report, orca_kind):
    """Read a value the way Orca does. Orca's ConfigOptionFloat(s)/Int(s)::deserialize use
    istringstream >> value, so e.g. "35%" in a coFloats option is read as 35."""
    if orca_types.get(key) not in ORCA_NUMBER_TYPES:
        return val

    def fix(x):
        sx = str(x)
        m = _LEADING_NUMBER.match(sx)
        if m and m.end() < len(sx.rstrip()) and sx.strip() != "nil":
            report.note(f"{orca_kind} `{key}` = {sx!r}: Orca's option type is {orca_types[key]}, "
                        f"which Orca reads as {m.group(0).strip()}; imported as that number")
            return m.group(0).strip()
        return x
    return [fix(x) for x in val] if isinstance(val, list) else fix(val)


def convert_settings(cfg, orca_kind, report, allowed_boxes, orca_types):
    """Map one resolved Orca preset. Returns {box: {key: value}}."""
    out = {b: {} for b in allowed_boxes}
    cfg = {k: orca_read(k, v, orca_types, report, orca_kind) for k, v in cfg.items()}
    for key in sorted(cfg):
        val = cfg[key]
        if key in M.STRUCTURAL:
            continue
        if key not in M.MAP:
            if key.endswith(("_BRASS", "_HS")):
                reason = "per-hotend variant key with no reader in Orca 2.5 source; Orca ignores it"
            else:
                reason = M.DROP.get(key, "no 3.0 equivalent")
            report.drop(orca_kind, key, reason)
            continue
        if M.is_nil(val):
            continue
        for box, target, conv in M.MAP[key]:
            if box not in allowed_boxes:
                report.drop(orca_kind, key, f"maps to {box} box, not used for {orca_kind} presets")
                continue
            try:
                if getattr(conv, "needs_cfg", False):
                    out[box][target] = conv(val, cfg)
                else:
                    out[box][target] = conv(val)
            except M.Skip as e:
                report.drop(orca_kind, key, str(e))
            except (ValueError, IndexError) as e:
                report.drop(orca_kind, key, f"unparseable value {val!r}: {e}")
    return out


def thumbnails_value(cfg, report, mname=""):
    """Orca thumbnails ("WxH" with thumbnails_format, or "WxH/FMT" entries) -> 3.0 "WxH/FMT, ...".
    3.0 writes PNG, JPG and QOI only (validation InvalidThumbnailRequest); entries in other
    formats (COLPIC, BTT_TFT) are left out and reported."""
    thumbs = cfg.get("thumbnails")
    if not thumbs:
        return None
    fmt = str(cfg.get("thumbnails_format", "PNG"))
    items = thumbs if isinstance(thumbs, list) else str(thumbs).split(",")
    out, dropped = [], []
    for t in items:
        t = str(t).strip()
        if not t:
            continue
        t = t if "/" in t else f"{t}/{fmt}"
        if t.rsplit("/", 1)[1].upper() in ("PNG", "JPG", "QOI"):
            out.append(t)
        else:
            dropped.append(t)
    if dropped:
        report.fallbacks.setdefault("thumbnails: entries in a format 3.0 cannot write left out "
                                    f"({', '.join(sorted(set(d.rsplit('/', 1)[1] for d in dropped)))})",
                                    []).append(mname)
    return ", ".join(out) if out else None


def lift_type_values(cfg, key, z_hop_key, report, kind):
    """Orca z_hop_types: only 'Slope Lift' equals 3.0 ramping lift (travel_slope + max lift)."""
    v = cfg.get(key)
    if v is None or M.is_nil(v):
        return {}
    t = str(M.first(v))
    if t == "Slope Lift":
        out = {"travel_ramping_lift": True}
        if z_hop_key in cfg and not M.is_nil(cfg[z_hop_key]):
            out["travel_max_lift"] = M.num(cfg[z_hop_key])
        if "travel_slope" in cfg:
            out["travel_slope"] = M.num(cfg["travel_slope"])
        return out
    if t in ("Normal Lift", "Auto Lift", "Spiral Lift"):
        if t != "Normal Lift":
            report.drop(kind, key, f"'{t}' has no 3.0 equivalent; printed as a normal vertical lift")
        return {"travel_ramping_lift": False}
    report.drop(kind, key, f"unknown value {t!r}")
    return {}


G92E0 = re.compile(r"^[ \t]*[gG]92[ \t]*[eE](0(\.0*)?|\.0+)[ \t]*(;.*)?$", re.M)  # Print.cpp regex_g92e0
ORCA_BOOL = {"coBool", "coBools"}
ORCA_INT = {"coInt", "coInts"}
ORCA_FLOAT = {"coFloat", "coFloats", "coPercent", "coPercents", "coFloatOrPercent", "coFloatsOrPercents"}


def alias(name, raw):
    """Orca preset alias (PresetBundle: explicit "alias", else the name before '@', trimmed)."""
    if raw.get("alias"):
        return raw["alias"]
    return name.split("@", 1)[0].rstrip() if "@" in name else name


def short_name(name):
    """Domain::Preset::short_name: the name before "@", trailing spaces removed."""
    idx = name.find("@")
    return name if idx <= 0 else name[:idx].rstrip(" ")


class VendorConverter:
    def __init__(self, vendor, orca_install, out_root, schema, orca_types, per_nozzle_configs=False):
        self.orca_types = orca_types
        # one printer_config per Orca machine preset (old layout, kept for e2e tests: the CLI only
        # finds printers that have a template) instead of one per printer model
        self.per_nozzle_configs = per_nozzle_configs
        self.v = vendor
        self.install_assets = os.path.join(orca_install, "resources", "profiles", vendor.name)
        self.out_root = out_root
        self.out = os.path.join(out_root, vendor.name)
        self.schema = schema
        self.report = Report(vendor.name)
        self.machines = {}      # machine preset name -> (model, nozzle, cfg)
        self.globals = set()
        self.nozzle_label = {}  # nozzle diameter -> Orca's spelling
        # Orca-only settings used in custom G-code: orca key -> (3.0 box, custom parameter name)
        self.custom = {}
        self.inert = {}         # placeholder -> fields where it can never be executed
        self.mtools = {}        # machine name -> {"n", "nozzles", "views", "variants", "raw"}
        self.models = {}        # model name -> {"raw": machine_model json, "machines": [names]}

    # ---------------------------------------------------------------- machines
    def collect_machines(self):
        model_defs = self.v.machine_models()
        for name in self.v.instantiated("machine"):
            cfg, _ = self.v.resolve(name)
            model = cfg.get("printer_model")
            if not model or model not in model_defs:
                self.report.errors.append(f"machine {name!r}: printer_model {model!r} has no machine_model")
                continue
            sets = self.v.variant_sets
            n = variants.extruder_count(cfg)
            views = [variants.machine_view(cfg, i, sets) for i in range(n)]
            vnames = [variants.machine_variant(cfg, i)[1] for i in range(n)]
            nds = cfg.get("nozzle_diameter") if isinstance(cfg.get("nozzle_diameter"), list) else [cfg.get("nozzle_diameter", "")]
            nozzles = []
            for nd in nds:
                nozzle = float(nd)
                # Tool label keeps Orca's spelling ("1.0"), so "<model> <label> nozzle" equals the
                # Orca machine preset name.
                label = self.nozzle_label.setdefault(nozzle, str(nd))
                if label != str(nd):
                    self.report.note(f"nozzle {nd!r} of {name!r} also written as {label!r}; {label!r} used")
                nozzles.append(nozzle)
            self.mtools[name] = {"n": n, "nozzles": nozzles, "views": views, "variants": vnames, "raw": cfg}
            self.machines[name] = (model, nozzles[0], views[0])
            self.models.setdefault(model, {"raw": model_defs[model], "machines": [], "base": model,
                                           "name": model})["machines"].append(name)
        # A 3.0 printer config is identified by printer + tools. Orca machine presets of one model
        # that share the same nozzles (hardened-steel / HF variants, IDEX copy/mirror modes, ...)
        # therefore each get their own printer (model = machine name, family = Orca model,
        # printer_model placeholder = Orca printer_model via legacy_printer_model).
        for model in sorted(list(self.models)):
            info = self.models[model]
            by_tools = {}
            for m in info["machines"]:
                by_tools.setdefault(tuple(self.mtools[m]["nozzles"]), []).append(m)
            split = sorted(m for group in by_tools.values() if len(group) > 1 for m in group)
            for m in split:
                if m in self.models:
                    self.report.errors.append(f"machine {m!r} collides with a model name")
                    continue
                info["machines"].remove(m)
                labels = " ".join(f"{self.nozzle_label[x]} nozzle" for x in self.mtools[m]["nozzles"])
                disp = m[:-len(labels) - 1] if self.mtools[m]["n"] == 1 and m.endswith(" " + labels) else m
                self.models[m] = {"raw": info["raw"], "machines": [m], "base": model, "name": disp}
                self.machines[m] = (m,) + self.machines[m][1:]
                self.report.note(f"{model}: machine preset {m!r} shares its nozzles with another preset of the "
                                 f"model; imported as its own printer in the {model!r} family")
            if not info["machines"]:
                del self.models[model]
        for model, info in self.models.items():
            info["counts"] = sorted({self.mtools[m]["n"] for m in info["machines"]})
        unused = sorted(set(model_defs) - {i["base"] for i in self.models.values()})
        for u in unused:
            self.report.note(f"machine model {u!r} has no instantiated machine preset; skipped")

    def tool_count_cond(self, m):
        model = self.machines[m][0]
        if self.models[model]["counts"] != [1]:
            return f" and printer.tool_count == {self.mtools[m]['n']}"
        return ""

    def machine_condition(self, machine_names):
        parts = []
        for m in sorted(machine_names, key=lambda n: (self.machines[n][0], self.mtools[n]["nozzles"])):
            model, nozzle, _ = self.machines[m]
            parts.append(f'(printer.model == "{model}" and tool.nozzle_diameter == {nozzle:g}{self.tool_count_cond(m)})')
        return " or ".join(parts)

    def fslug(self, key):
        """File-name slug of a printer key, unique within the vendor ("2" and "2+" differ)."""
        if not hasattr(self, "_fslugs"):
            keys = sorted(set(self.models) | {"common", "machine"})
            by = {}
            for k in keys:
                by.setdefault(slug(k).lower(), []).append(k)
            self._fslugs = {}
            for sl, ks in by.items():
                for k in ks:
                    self._fslugs[k] = slug(k) if len(ks) == 1 else f"{slug(k)}-{hashlib.md5(k.encode('utf-8')).hexdigest()[:6]}"
        return self._fslugs[key]

    def config_id(self, m):
        """Stable printer_config id; slugs that collide (e.g. "2" and "2+") get a name hash."""
        if not hasattr(self, "_cfg_ids"):
            slugs = {}
            for n in self.machines:
                slugs.setdefault(slug(n), []).append(n)
            self._cfg_ids = {}
            for sl, names in slugs.items():
                for n in names:
                    self._cfg_ids[n] = sl if len(names) == 1 else f"{sl}-{stable_id(n)[:6].replace('/', '_').replace('+', '-')}"
        return self._cfg_ids[m]

    def family(self, model):
        """Picker section of a printer model: its series, from the Orca model name without the vendor -
        the first word plus a model number after it ("Kobra 3" for Kobra 3 / Kobra 3 Max / Kobra 3 V2,
        "Neptune 2" for Neptune 2 / 2D / 2S). Orca's own machine_model "family" is just the vendor, and
        the user wants series (2026-10-18)."""
        name = self.models[model]["base"]
        vendor = self.v.name
        rest = name[len(vendor):].strip() if name.startswith(vendor + " ") else name
        words = rest.split()
        if not words:
            return name
        number = re.match(r"[A-Z]?\d+", words[1]) if len(words) > 1 else None
        return f"{vendor} {words[0]} {number.group(0)}" if number else f"{vendor} {words[0]}"

    def config_groups(self, model):
        """Printer configs (picker cards) of a model: one per tool count, like Prusa's "XL 1T"/"XL 2T";
        the nozzle is chosen afterwards as a tool. Orca's wizard has no single default nozzle (it
        installs every nozzle of a model, WebGuideDialog.cpp nozzle_selected = nozzle_diameter), so the
        card starts with 0.4 when the model has it, else the first nozzle in the model's list.
        Returns [(config id, config name, machine preset used for the card)]."""
        info = self.models[model]
        if self.per_nozzle_configs:
            return [(self.config_id(m), m, m)
                    for m in sorted(info["machines"], key=lambda x: self.machines[x][1])]
        listed = [float(x) for x in str(info["raw"].get("nozzle_diameter", "")).split(";") if x.strip()]

        def rank(m):
            nozzles = self.mtools[m]["nozzles"]
            if all(abs(x - 0.4) < 1e-9 for x in nozzles):
                return (0, 0, m)
            first = nozzles[0]
            return (1, listed.index(first) if first in listed else len(listed), m)

        groups = []
        for n in info["counts"]:
            m = min((x for x in info["machines"] if self.mtools[x]["n"] == n), key=rank)
            cid = self.fslug(model) if len(info["counts"]) == 1 else f"{self.fslug(model)}-{n}t"
            name = info["name"] if len(info["counts"]) == 1 else f"{info['name']} {n}T"
            groups.append((cid, name, m))
        return groups

    def config_name(self, m):
        """Name 3.0 gives the printer config (Domain HwConfig.cpp suggest_name)."""
        t = self.mtools[m]
        model = self.models[self.machines[m][0]]["name"]
        labels = [f"{self.nozzle_label[x]} nozzle" for x in t["nozzles"]]
        if t["n"] > 1:
            return f"{model} {t['n']}T " + ", ".join(labels)
        return f"{model} {labels[0]}"

    def variant_groups(self, compat):
        """Group compatible machines by the extruder variant their first extruder uses."""
        groups = {}
        for m in compat:
            groups.setdefault(self.mtools[m]["variants"][0], []).append(m)
        if len(groups) <= 1:
            return list(groups) or ["Direct Drive Standard"], {}
        base = sorted(groups, key=lambda k: (-len(groups[k]), k))[0]
        return [base], {k: v for k, v in groups.items() if k != base}

    def compat_machines(self, cfg, pname, kind):
        """Machine presets this process/filament is compatible with (Orca rules)."""
        cond = cfg.get("compatible_printers_condition", "")
        cps = cfg.get("compatible_printers", [])
        if not cps:
            if cond:
                # Preset::is_compatible_with_printer: the condition is evaluated against each printer
                # preset; on a parse error Orca treats the preset as compatible with everything.
                names = []
                for m, (_, _, mcfg) in self.machines.items():
                    try:
                        if orca_condition.evaluate(cond, m, mcfg):
                            names.append(m)
                    except orca_condition.ConditionError as e:
                        self.report.note(f"{kind} {pname!r}: compatible_printers_condition not evaluable ({e}); "
                                         f"compatible with every printer, as in Orca")
                        return list(self.machines), None
                if not names:
                    return None, None
                return names, None
            return list(self.machines), None
        names = [c for c in cps if c in self.machines]
        missing = [c for c in cps if c not in self.machines]
        for c in missing:
            self.report.note(f"compatible printer {c!r} referenced by presets is not an instantiated machine here")
        if not names:
            # Orca never shows such a preset; nothing to import.
            return None, None
        return names, None

    # ---------------------------------------------------------------- gcode
    def custom_renames(self):
        # printer vector settings become local vectors of the same name (see printer_vector_preamble)
        return {k: v[1] for k, v in self.custom.items() if v[0] != "printer_vec"}

    def vec_len(self):
        return max(2, max((t["n"] for t in self.mtools.values()), default=1))

    def printer_vector_preamble(self, text):
        """Orca printer settings that are vectors (one value per extruder) and that custom G-code
        indexes: custom parameters cannot hold arrays, so each element travels as
        custom_parameter_printer_<key>__<i> and the G-code block rebuilds the vector as a local."""
        pre = ""
        for k, (bx, _) in sorted(self.custom.items()):
            if bx == "printer_vec" and re.search(r"(?<![A-Za-z0-9_])" + k + r"(?![A-Za-z0-9_])", text):
                items = ", ".join(f"custom_parameter_printer_{k}__{i}" for i in range(self.vec_len()))
                pre += f"{{local {k} = ({items})}}"
                self.count_rename(k, f"local vector of custom_parameter_printer_{k}__<i>")
        return pre

    def emulate(self, text, machine_cfg, slots):
        """Prepend local variables re-creating the Orca runtime variables 3.0 does not set (the
        Bambu cutter / extruder-change retraction values); 3.0 sets the others (placeholders.RUNTIME)."""
        pre = self.printer_vector_preamble(text)
        bbl = placeholders.bbl_preamble(text, machine_cfg, slots) if machine_cfg is not None else ""
        if bbl:
            pre += bbl
            for name in placeholders.BBL_NAMES:
                if re.search(rf"\b{name}\b", text):
                    self.count_rename(name, "local variable: Orca's machine value or the filament override")
        return pre + text

    def count_rename(self, a, b):
        self.report.placeholder_renames[(a, b)] = self.report.placeholder_renames.get((a, b), 0) + 1

    def translate(self, text, where, filename=False, field=None, machine_cfg=None, slots=1):
        extra = self.globals | (placeholders.FILENAME_EXTRA if filename else set())
        text = self.emulate(text, machine_cfg, slots)
        extra |= {f"custom_parameter_printer_{k}__{i}" for k, v in self.custom.items() if v[0] == "printer_vec"
                  for i in range(self.vec_len())}
        renames_extra = dict(self.custom_renames())
        if not filename:
            renames_extra.update(placeholders.GCODE_ONLY_RENAMES)
        new, renames, unresolved = placeholders.translate(text, extra, renames_extra)
        for k, n in renames.items():
            self.report.placeholder_renames[k] = self.report.placeholder_renames.get(k, 0) + n
        for p in unresolved:
            if field == "toolchange_gcode":
                # Imported configs have one material slot and no feeder, so 3.0 never runs the
                # tool-change G-code (it is only parsed when executed).
                self.inert.setdefault(p, set()).add(where)
            else:
                self.report.unresolved.setdefault(p, set()).add(where)
        return new

    def translate_values(self, values, where, machine_cfg=None, slots=1):
        fmt = values.get("output_filename_format")
        if isinstance(fmt, str):
            _, _, unresolved = placeholders.translate(fmt, self.globals | placeholders.FILENAME_EXTRA,
                                                      self.custom_renames())
            if unresolved:
                # Only the file name depends on it: fall back to the 3.0 default name.
                del values["output_filename_format"]
                self.report.drop("process", "filename_format",
                                 f"uses {', '.join(sorted(unresolved))}, unknown to 3.0; default file name used")
        for key in list(values):
            if key in placeholders.TEMPLATE_KEYS and isinstance(values[key], str):
                values[key] = self.translate(values[key], f"{where}: {key}",
                                             key == "output_filename_format", key, machine_cfg, slots)

    def gcode_texts(self):
        """(owner kind, orca field, text) for every custom G-code the vendor (and the library
        filaments it shows) carries."""
        for n in self.machines:
            cfg = self.machines[n][2]
            for k in M.GCODE_FIELDS_PRINTER:
                if k in cfg:
                    yield "machine", k, M.text(cfg[k]), cfg
        for owner, n in self.filament_sources():
            cfg, _ = owner.resolve(n)
            for k in M.GCODE_FIELDS_FILAMENT:
                if k in cfg:
                    yield "filament", k, M.text(cfg[k]), None
        for n in self.v.instantiated("process"):
            cfg, _ = self.v.resolve(n)
            if "filename_format" in cfg:
                yield "process", "filename_format", M.text(cfg["filename_format"]), None

    def find_custom_parameters(self):
        """Orca settings that custom G-code reads but 3.0 has no (equal) key for are carried as
        3.0 custom parameters (custom_parameters_printer/print/filament JSON, exposed to the macro
        language as custom_parameter_<box>_<key>; libslic3r CustomParametersHandling.cpp) with
        Orca's own value, so the G-code evaluates exactly as in Orca."""
        owners = {}
        for kind in ("machine", "process", "filament"):
            for k in self.v.defaults.get(kind, {}):
                owners.setdefault(k, kind)
        box = {"machine": "printer", "process": "print", "filament": "filament"}
        for kind, field, text, mcfg in self.gcode_texts():
            text = self.emulate(text, mcfg, 8)
            extra = self.globals | (placeholders.FILENAME_EXTRA if field == "filename_format" else set())
            renames_extra = {} if field == "filename_format" else dict(placeholders.GCODE_ONLY_RENAMES)
            _, _, unresolved = placeholders.translate(text, extra, renames_extra)
            for name in unresolved:
                if name in self.custom or name.startswith("<"):
                    continue
                # Orca generates the filament_<retraction key> override options in a loop, so they
                # are not in orca_config_types.json; m_filament_retract_keys lists them.
                is_override = name.startswith("filament_") and name[len("filament_"):] in self.v.variant_sets["extruder_option_keys"]
                if name in placeholders.CONVERTED or name in self.orca_types or is_override:
                    if is_override:
                        owners.setdefault(name, "filament")
                    owner = owners.get(name)
                    if owner is None:
                        # Orca option lists are per preset type; fall back to where profiles set it.
                        owner = "filament" if name.startswith(("filament_", "nozzle_temperature")) else "machine"
                    bx = box[owner]
                    if bx == "printer" and self.orca_types.get(name, "").startswith("co") and \
                            self.orca_types.get(name, "").endswith("s") and self.orca_types.get(name) != "coPoints":
                        bx = "printer_vec"
                    self.custom[name] = (bx, f"custom_parameter_{box[owner]}_{name}")
        self.report.placeholder_renames.clear()

    def custom_values_for(self, cfg, okey):
        saved = self.custom
        try:
            self.custom = {okey: ("__one__", "")}
            return self.custom_values(cfg, "__one__").get(okey)
        finally:
            self.custom = saved

    def custom_values(self, cfg, target_box):
        out = {}
        for okey, (bx, _) in sorted(self.custom.items()):
            if bx != target_box or okey not in cfg or M.is_nil(cfg[okey]):
                continue
            t = self.orca_types.get(okey, "")
            raw = M.first(cfg[okey]) if isinstance(cfg[okey], list) else cfg[okey]
            try:
                if t in ("coEnumsGeneric", "coEnums") and okey in self.v.enum_ints:
                    # Orca hands enum vectors to custom G-code as their integer values
                    val = self.v.enum_ints[okey][str(raw)]
                elif t in ORCA_BOOL:
                    val = M.boolean(raw)
                elif t in ORCA_INT:
                    val = int(float(raw))
                elif t in ORCA_FLOAT:
                    val = float(str(raw).rstrip("%"))
                else:
                    val = str(raw)
            except (ValueError, M.Skip, KeyError):
                val = str(raw)
            out[okey] = val
        return out

    def add_custom(self, values, cfg, target_box, views=None):
        cp = self.custom_values(cfg, target_box)
        if target_box == "printer" and views:
            for okey, (bx, _) in sorted(self.custom.items()):
                if bx != "printer_vec":
                    continue
                vals = []
                for i in range(self.vec_len()):
                    vc = views[min(i, len(views) - 1)]
                    one = self.custom_values_for(vc, okey)
                    if one is None:
                        break
                    vals.append(one)
                for i, v in enumerate(vals):
                    cp[f"{okey}__{i}"] = v
        if cp:
            values[f"custom_parameters_{target_box}"] = json.dumps(cp, sort_keys=True)

    # ---------------------------------------------------------------- filaments shown by Orca
    def filament_sources(self):
        """Vendor filaments plus the OrcaFilamentLibrary filaments Orca shows for this vendor's
        printers: generic ones (empty compatible_printers) and ones listing these printers."""
        if hasattr(self, "_fil_sources"):
            return self._fil_sources
        out = [(self.v, n) for n in self.v.instantiated("filament")]
        lib = self.v.base
        if lib is not None:
            for n in lib.instantiated("filament"):
                cfg, _ = lib.resolve(n)
                cps = cfg.get("compatible_printers", [])
                if not cps or any(c in self.machines for c in cps):
                    out.append((lib, n))
        self._fil_sources = out
        return out

    def library_exclusions(self):
        """Preset::is_compatible_with_printer / update_library_profile_excluded_from: a library
        filament with empty compatible_printers is hidden on every printer that a same-alias
        printer-specific filament lists."""
        excl = {}
        for owner, n in self.filament_sources():
            cfg, _ = owner.resolve(n)
            cps = cfg.get("compatible_printers", [])
            if cps:
                excl.setdefault(alias(n, owner.raw[n]), set()).update(c for c in cps if c in self.machines)
        return excl

    # ---------------------------------------------------------------- build
    def build(self):
        self.collect_machines()
        for _kind, _field, text, _m in self.gcode_texts():
            self.globals |= placeholders.global_names(text)
        self.find_custom_parameters()
        if os.path.exists(self.out):
            shutil.rmtree(self.out)
        os.makedirs(os.path.join(self.out, "assets"))
        docs_vendor = []
        nozzles = sorted({x for t in self.mtools.values() for x in t["nozzles"]})

        # vendor document
        printer_features = {}
        for n in nozzles:
            printer_features[f"supports_{nozzle_key(n)}_nozzle"] = {"default": False, "user_editable": False}
        printer_features["multi_bed_types"] = {"default": False, "user_editable": False}
        printer_features["multi_extruder"] = {"default": False, "user_editable": False}
        printer_features["default_bed"] = {"default": BED_TYPES[ORCA_DEFAULT_BED][0], "user_editable": False}
        families = []
        # picker sections: one per printer series (family()), so related models share a grid
        for fam in sorted({self.family(m) for m in self.models}):
            ids = [cid for pid in sorted(self.models) if self.family(pid) == fam
                   for cid, _, _ in self.config_groups(pid)]
            label = fam[len(self.v.name) + 1:] if fam.startswith(self.v.name + " ") else fam
            families.append({"label": label, "base_model": fam, "printer_configs_order": ids})
        ver = orca_source.normalize_version(self.v.version)
        docs_vendor.append({
            "kind": "vendor", "id": self.v.name, "name": self.v.name,
            "version": f"{ver[0]}.{ver[1]}.{ver[2]}+orca.{self.v.version}",
            "features": {
                "printer": printer_features,
                "tool": {"nozzle_high_flow": {"default": False, "user_editable": False},
                         "nozzle_diameter": {"default": nozzles[0], "user_editable": False},
                         "extruder_index": {"default": 0, "user_editable": False}},
            },
            "printer_families": families,
        })

        # printer defs + assets
        for model in sorted(self.models):
            info = self.models[model]
            raw = info["raw"]
            feats = {}
            for m in info["machines"]:
                for x in self.mtools[m]["nozzles"]:
                    feats[f"supports_{nozzle_key(x)}_nozzle"] = {"default": True}
            if max(info["counts"]) > 1:
                # Tool overrides (per-extruder settings) need a multi-extruder printer
                # (Print.cpp validate_input), like Prusa's XL.
                feats["multi_extruder"] = {"default": True}
            mcfg = self.machines[sorted(info["machines"])[0]][2]
            if M.boolean(mcfg.get("support_multi_bed_types", "0")):
                feats["multi_bed_types"] = {"default": True}
            bed = default_bed_type(mcfg, self.report)
            feats["default_bed"] = {"default": BED_TYPES[bed][0]}
            visual = {}
            for field, src in (("bed_model", raw.get("bed_model", "")),
                               ("bed_texture", raw.get("bed_texture", "")),
                               ("thumbnail", f"{info['base']}_cover.png")):
                if src and self.copy_asset(src, info["base"]):
                    visual[field] = src
            doc = {"kind": "printer", "technology": "FFF", "name": info["name"], "id": model,
                   "model": {"base_model": self.family(model), "model": model}, "tool_count": max(info["counts"]),
                   "features": feats}
            if model != info["base"]:
                doc["legacy_printer_model"] = [info["base"]]
            # Orca's wizard installs these filaments with the model (machine_model default_materials)
            defaults = raw.get("default_materials", "")
            defaults = defaults if isinstance(defaults, list) else str(defaults).split(";")
            known = {n for _, n in self.filament_sources()}
            defaults = [d.strip() for d in defaults if d.strip() in known]
            if defaults:
                doc["default_materials"] = defaults
            if visual:
                doc["visual"] = visual
            docs_vendor.append(doc)

        for n in nozzles:
            docs_vendor.append({"kind": "tool", "technology": "FFF", "id": self.nozzle_label[n],
                                "name": f"{self.nozzle_label[n]} nozzle",
                                "condition": f"printer.supports_{nozzle_key(n)}_nozzle",
                                "features": {"nozzle_diameter": {"default": n}}})
        for bt in sorted(BED_TYPES):
            sid, label, _, stype = BED_TYPES[bt]
            docs_vendor.append({"kind": "sheet", "id": sid, "name": label, "type": stype,
                                "condition": f'printer.multi_bed_types or printer.default_bed == "{sid}"'})
        for model in sorted(self.models):
            for cid, cname, m in self.config_groups(model):
                _, nozzle, cfg = self.machines[m]
                t = self.mtools[m]
                pc = {"kind": "printer_config", "id": cid, "name": cname, "printer": model}
                if t["n"] > 1:
                    pc["tool_count"] = t["n"]
                    pc["tools"] = [{"tool": self.nozzle_label[x], "features": {"extruder_index": i}}
                                   for i, x in enumerate(t["nozzles"])]
                else:
                    if self.models[model]["counts"] != [1]:
                        pc["tool_count"] = 1  # the printer's default is its largest tool count
                    pc["tools"] = [{"tool": self.nozzle_label[nozzle]}]
                pc["sheet"] = BED_TYPES[default_bed_type(cfg, self.report)][0]
                docs_vendor.append(pc)
        self.write_yaml("vendor.yaml", docs_vendor)

        n_printer = 0
        # Orca machine presets carry nozzle-specific print settings (retraction, layer height
        # limits). In 3.0 these are Print keys; Tool overrides are rejected for single-tool printers
        # (Print.cpp validate_input: ToolOverridesInSingleToolPrint), so - like Prusa's own
        # '*common MINI*' - they go into one print base preset that every print preset inherits,
        # with one variant per machine preset.
        machine_print_vars = []
        tool_vars = []
        # printer presets, one file per model
        for model in sorted(self.models):
            p_vars = []
            for m in sorted(self.models[model]["machines"], key=lambda x: self.machines[x][1]):
                _, nozzle, cfg = self.machines[m]
                conv = convert_settings(cfg, "machine", self.report, ("printer", "tool"), self.orca_types)
                pv, tv = conv["printer"], conv["tool"]
                if pv.get("gcode_flavor") == "klipper" and pv.get("machine_limits_usage") == "emit_to_gcode":
                    # 3.0 refuses to slice this combination (Print.cpp: MachineLimitsWithKlipper);
                    # Orca would emit the limits as SET_VELOCITY_LIMIT. Limits stay for time estimates.
                    pv["machine_limits_usage"] = "time_estimate_only"
                    self.report.drop("machine", "emit_machine_limits_to_gcode",
                                     "3.0 cannot emit machine limits for Klipper flavor; used for time estimate only")
                th = thumbnails_value(cfg, self.report, m)
                if th:
                    pv["thumbnails"] = th
                tv.update(lift_type_values(cfg, "z_hop_types", "z_hop", self.report, "machine"))
                if "travel_slope" in cfg and not tv.get("travel_ramping_lift"):
                    self.report.drop("machine", "travel_slope", "used only with Orca 'Slope Lift'")
                t = self.mtools[m]
                if t["n"] > 1:
                    offs = t["raw"].get("extruder_offset") or ["0x0"]
                    offs = offs if isinstance(offs, list) else [offs]
                    try:
                        pv["extruder_offset"] = M.vec2_list([offs[min(i, len(offs) - 1)] for i in range(t["n"])])
                    except M.Skip as e:
                        self.report.errors.append(f"{m}: extruder_offset {offs!r}: {e}")
                    for i in range(t["n"]):
                        ti = convert_settings(t["views"][i], "machine", self.report, ("tool",), self.orca_types)["tool"]
                        ti.update(lift_type_values(t["views"][i], "z_hop_types", "z_hop", self.report, "machine"))
                        if ti:
                            tool_vars.append({"condition": self.machine_condition([m]) + f" and tool.extruder_index == {i}",
                                              "id": stable_id(self.v.name, "tool", m, str(i)), "values": ti})
                self.ensure_g92e0(pv, m)
                self.add_custom(pv, cfg, "printer", t["views"])
                self.translate_values(pv, m, cfg, t["n"])
                # 3.0 picks the default print / material by short name (Domain::Preset::short_name, the
                # text before "@"; PresetInteractor get_named_or_first_from_range); Orca stores full names
                if pv.get("default_print"):
                    pv["default_print"] = short_name(pv["default_print"])
                # Orca's machine default_filament_profile -> 3.0's default_material (a print key)
                default_filament = cfg.get("default_filament_profile", "")
                default_filament = (default_filament[0] if default_filament else "")                     if isinstance(default_filament, list) else str(default_filament)
                if default_filament:
                    tv["default_material"] = short_name(default_filament)
                cond = f"tool.nozzle_diameter == {nozzle:g}{self.tool_count_cond(m)}"
                p_vars.append({"condition": cond, "id": stable_id(self.v.name, "printer", m), "values": pv})
                if tv:
                    machine_print_vars.append({"condition": self.machine_condition([m]),
                                               "id": stable_id(self.v.name, "machine-print", m), "values": tv})
                n_printer += 1
            f = self.fslug(model)
            self.write_yaml(f"preset-printer-{f}.yaml", [{
                "kind": "printer", "id": stable_id(self.v.name, "printer-root", model),
                "variants": [{"condition": f'printer.model == "{model}"', "name": model,
                              "id": stable_id(self.v.name, "printer-model", model), "variants": p_vars}]}])
        base_id = "*orca machine*"
        self.write_yaml("preset-print-machine.yaml", [{"kind": "print", "id": base_id, "variants": machine_print_vars}])
        # One tool print preset (Prusa: "no tool"): no overrides on single-tool printers, the
        # machine's per-extruder settings as Tool overrides on multi-extruder ones.
        tool_root = {"kind": "tool_print", "id": stable_id(self.v.name, "tool-root"), "name": "no tool"}
        if tool_vars:
            tool_root["variants"] = tool_vars
        self.write_yaml("preset-tool-print.yaml", [tool_root])

        # print presets
        groups = {}
        n_print = 0
        for name in self.v.instantiated("process"):
            cfg, _ = self.v.resolve(name)
            compat, err = self.compat_machines(cfg, name, "process")
            if err:
                self.report.errors.append(err)
                continue
            if compat is None:
                self.report.note(f"process {name!r}: its compatible_printers_condition matches no printer; not imported")
                continue
            base, others = self.variant_groups(compat)
            conv = self.process_values(variants.process_view(cfg, base[0], 1, self.v.variant_sets), name)
            doc = {"kind": "print", "id": stable_id(self.v.name, "print", name), "name": name}
            if set(compat) != set(self.machines):
                doc["condition"] = self.machine_condition(compat)
            doc["inherits"] = [base_id]
            doc["values"] = conv
            if others:
                doc["variants"] = []
                for vname, group in sorted(others.items()):
                    gv = self.process_values(variants.process_view(cfg, vname, 1, self.v.variant_sets), name)
                    doc["variants"].append({"condition": self.machine_condition(group),
                                            "id": stable_id(self.v.name, "print-variant", name, vname),
                                            "values": {k: v for k, v in gv.items() if conv.get(k) != v}})
            key = self.machines[sorted(compat)[0]][0] if len(compat) < len(self.machines) else "common"
            groups.setdefault(key, []).append(doc)
            n_print += 1
        for key in sorted(groups):
            self.write_yaml(f"preset-print-{self.fslug(key)}.yaml", groups[key])

        # filament presets
        groups = {}
        n_fil = n_lib = 0
        excl = self.library_exclusions()
        for owner, name in self.filament_sources():
            cfg, _ = owner.resolve(name)
            compat, err = self.compat_machines(cfg, name, "filament")
            if err:
                self.report.errors.append(err)
                continue
            if compat is None:
                if owner is self.v:
                    self.report.note(f"filament {name!r}: its compatible_printers_condition matches no printer; not imported")
                continue
            if owner is not self.v:
                if not cfg.get("compatible_printers"):
                    compat = [m for m in compat if m not in excl.get(alias(name, owner.raw[name]), set())]
                if not compat:
                    continue
                n_lib += 1
            for k in ("compatible_prints", "compatible_prints_condition"):
                if cfg.get(k):
                    self.report.drop("filament", k, "print compatibility not translated")
            base, others = self.variant_groups(compat)
            # Orca's filament_ids placeholder = the preset's filament_id (PresetBundle.cpp)
            cfg = dict(cfg)
            cfg["filament_ids"] = [owner.filament_id(name)]
            bcfg = variants.filament_view(cfg, base[0], self.v.variant_sets)
            conv = self.filament_values(bcfg, name)
            doc = {"kind": "filament", "id": stable_id(self.v.name, "filament", name), "name": name}
            if set(compat) != set(self.machines):
                doc["condition"] = self.machine_condition(compat)
            doc["values"] = conv
            sheet_vars = self.bed_variants(bcfg, name)
            if others:
                # Per extruder-variant values (BBL-style profiles): one conditional variant per group
                # of printers, each with its own bed temperatures, then the base group as fallback.
                doc["variants"] = []
                for vname, group in sorted(others.items()):
                    gcfg = variants.filament_view(cfg, vname, self.v.variant_sets)
                    gv = self.filament_values(gcfg, name)
                    var = {"condition": self.machine_condition(group),
                           "id": stable_id(self.v.name, "filament-variant", name, vname),
                           "values": {k: v for k, v in gv.items() if conv.get(k) != v}}
                    gs = self.bed_variants(gcfg, name + "#" + vname)
                    if gs:
                        var["variants"] = gs
                    doc["variants"].append(var)
                fallback = {"id": stable_id(self.v.name, "filament-variant", name, base[0])}
                if sheet_vars:
                    fallback["variants"] = sheet_vars
                doc["variants"].append(fallback)
            elif sheet_vars:
                doc["variants"] = sheet_vars
            key = self.machines[sorted(compat)[0]][0] if len(compat) < len(self.machines) else "common"
            groups.setdefault(key, []).append(doc)
            n_fil += 1
        for key in sorted(groups):
            self.write_yaml(f"preset-filament-{self.fslug(key)}.yaml", groups[key])

        with open(os.path.join(self.out_root, self.v.name + ".idx"), "w", encoding="utf-8", newline="\n") as f:
            f.write("min_slic3r_version = 3.0.0-alpha0\n")
            f.write(f"{docs_vendor[0]['version']} Imported from OrcaSlicer profiles {self.v.name} {self.v.version}\n")

        self.report.counts = {
            "Orca source version": self.v.version,
            "printer models (families)": len({i["base"] for i in self.models.values()}),
            "Orca machine presets": len(self.machines),
            "printer configs (picker cards)": sum(len(self.config_groups(m)) for m in self.models),
            "printer presets (model x nozzle variants)": n_printer,
            "print presets": n_print,
            "filament presets": n_fil,
            "  of which OrcaFilamentLibrary": n_lib,
        }
        return self.report

    # ---------------------------------------------------------------- helpers
    def copy_asset(self, fname, model):
        src = os.path.join(self.install_assets, fname)
        if not os.path.exists(src):
            self.report.note(f"{model}: asset {fname!r} not in the Orca install; not referenced")
            return False
        shutil.copyfile(src, os.path.join(self.out, "assets", fname))
        return True

    def process_values(self, cfg, name):
        conv = convert_settings(cfg, "process", self.report, ("print",), self.orca_types)["print"]
        conv.update(self.support_values(cfg))
        conv.update(self.label_object_values(cfg))
        conv.update(self.wall_order_values(cfg))
        self.ironing_values(cfg, conv)
        self.add_custom(conv, cfg, "print")
        self.translate_values(conv, name)
        return conv

    def filament_values(self, cfg, name):
        conv = convert_settings(cfg, "filament", self.report, ("filament",), self.orca_types)["filament"]
        conv.update(self.filament_extra(cfg))
        self.add_custom(conv, cfg, "filament")
        self.translate_values(conv, name)
        return conv

    def ensure_g92e0(self, pv, mname):
        """3.0 refuses relative E with a Marlin flavor unless layer G-code resets E (Print.cpp
        MissingG92E0; Orca has no such check). Append the reset 3.0 asks for to layer_gcode.
        With absolute E, 3.0 refuses a `G92 E0` in (before-)layer G-code (FoundG92E0...): the slicer
        tracks E itself there, so that line is removed."""
        if not pv.get("use_relative_e_distances"):
            for key in ("before_layer_gcode", "layer_gcode"):
                if G92E0.search(pv.get(key, "")):
                    pv[key] = G92E0.sub("", pv[key])
                    self.report.fallbacks.setdefault(
                        f"{key}: removed `G92 E0` (absolute E; 3.0 FoundG92E0 rule, the slicer tracks E)",
                        []).append(mname)
            return
        if pv.get("gcode_flavor") not in ("marlin", "marlin2"):
            return
        if G92E0.search(pv.get("before_layer_gcode", "")) or G92E0.search(pv.get("layer_gcode", "")):
            return
        lg = pv.get("layer_gcode", "")
        pv["layer_gcode"] = (lg + ("" if not lg or lg.endswith("\n") else "\n") + "G92 E0")
        self.report.fallbacks.setdefault("layer_gcode: appended `G92 E0` (3.0 MissingG92E0 rule; harmless with "
                                         "relative E)", []).append(mname)

    def support_values(self, cfg):
        """enable_support + support_type + support_style -> support_material (+ style)."""
        out = {}
        if "enable_support" in cfg or "support_type" in cfg:
            enabled = M.boolean(cfg.get("enable_support", "0"))
            stype = str(cfg.get("support_type", "normal(auto)"))
            if not enabled:
                out["support_material"] = "none"
            elif stype in ("normal(auto)", "tree(auto)"):
                out["support_material"] = "everywhere"
            elif stype in ("normal(manual)", "tree(manual)"):
                out["support_material"] = "enforcers_only"
        # Orca tooltips (PrintConfig.cpp): style "default" = grid for normal and organic for tree
        # supports; base pattern "default" = rectilinear for normal and hollow for tree supports.
        tree = str(cfg.get("support_type", "normal(auto)")).startswith("tree")
        style = cfg.get("support_style")
        if style is not None:
            s = str(style)
            if s == "default":
                s = "organic" if tree else "grid"
            if s in ("grid", "snug", "organic"):
                out["support_material_style"] = s
            else:
                self.report.drop("process", "support_style", f"Orca style {s!r} has no 3.0 equivalent")
        pat = cfg.get("support_base_pattern")
        if pat is not None:
            p = str(pat)
            if p == "default" and not tree:
                p = "rectilinear"
            if p in ("rectilinear", "rectilinear-grid", "honeycomb"):
                out["support_material_pattern"] = p
            else:
                why = " (tree support: hollow)" if tree and p == "default" else ""
                self.report.drop("process", "support_base_pattern", f"Orca pattern {p!r}{why} has no 3.0 equivalent")
        return out

    def label_object_values(self, cfg):
        """Orca exclude_object (firmware EXCLUDE_OBJECT/M486) / gcode_label_objects (comments)."""
        if "exclude_object" not in cfg and "gcode_label_objects" not in cfg:
            return {}
        if M.boolean(cfg.get("exclude_object", "0")):
            return {"gcode_label_objects": "firmware"}
        if M.boolean(cfg.get("gcode_label_objects", "0")):
            return {"gcode_label_objects": "octoprint"}
        return {"gcode_label_objects": "disabled"}

    def wall_order_values(self, cfg):
        seq = cfg.get("wall_sequence")
        if seq is None:
            return {}
        s = str(seq)
        if s in ("inner wall/outer wall", "outer wall/inner wall", "inner-outer-inner wall"):
            return {"wall_sequence": s}
        self.report.drop("process", "wall_sequence", f"{s!r} has no 3.0 equivalent")
        return {}

    def ironing_values(self, cfg, out):
        t = cfg.get("ironing_type")
        if t is None:
            return
        t = str(t)
        if t == "no ironing":
            out["ironing"] = False
        elif t in ("top", "topmost", "solid"):
            out["ironing"] = True
            out["ironing_type"] = t
        else:
            self.report.drop("process", "ironing_type", f"{t!r} has no 3.0 equivalent")

    def filament_extra(self, cfg):
        out = {}
        out.update(lift_type_values(cfg, "filament_z_hop_types", "filament_z_hop", self.report, "filament"))
        # Orca: measured size as % of nominal (100% = no shrinkage, "94% if you measure 94mm");
        # 3.0: the shrinkage itself ("measured 99mm instead of 100mm" = 1%) -> 100 - Orca.
        for okey, tkey in (("filament_shrink", "filament_shrinkage_compensation_xy"),
                           ("filament_shrinkage_compensation_z", "filament_shrinkage_compensation_z")):
            if okey in cfg:
                s = str(M.first(cfg[okey])).rstrip("%")
                x = round(100.0 - float(s), 6)
                out[tkey] = f"{int(x) if float(x).is_integer() else x}%"
        if "idle_temperature" in cfg and not M.is_nil(cfg["idle_temperature"]):
            try:
                t = M.integer(cfg["idle_temperature"])
                if t > 0:
                    out["idle_temperature"] = t
            except M.Skip as e:
                self.report.drop("filament", "idle_temperature", str(e))
        return out

    def bed_variants(self, cfg, name):
        out = []
        for bt in sorted(BED_TYPES):
            sid, _, prefix, stype = BED_TYPES[bt]
            vals = {}
            for okey, tkey in ((prefix, "bed_temperature"), (prefix + "_initial_layer", "first_layer_bed_temperature")):
                if okey in cfg:
                    try:
                        vals[tkey] = M.integer(cfg[okey])
                    except M.Skip as e:
                        self.report.drop("filament", okey, f"{e} in the Orca profile; dropped")
            if vals:
                out.append({"condition": f'sheet.type == "{stype}"',
                            "id": stable_id(self.v.name, "filament-sheet", name, sid), "values": vals})
        return out

    def write_yaml(self, fname, docs):
        written = self.__dict__.setdefault("_written", set())
        if fname.lower() in written:
            raise RuntimeError(f"{self.v.name}: output file {fname} written twice")
        written.add(fname.lower())
        text = "---\n".join(yaml.dump(d, Dumper=BlockDumper, sort_keys=False, allow_unicode=True, width=1000)
                            for d in docs)
        with open(os.path.join(self.out, fname), "w", encoding="utf-8", newline="\n") as f:
            f.write(text)


class BlockDumper(yaml.SafeDumper):
    pass


def _str_presenter(dumper, data):
    # Unicode line/paragraph separators (some Orca G-code contains U+2029) are line breaks to a
    # YAML parser: write such text double-quoted so they stay escaped characters.
    if any(c in data for c in (" ", " ", "\x85")):
        return dumper.represent_scalar("tag:yaml.org,2002:str", data, style='"')
    if "\n" in data and dumper.analyze_scalar(data).allow_block:
        return dumper.represent_scalar("tag:yaml.org,2002:str", data, style="|")
    return dumper.represent_scalar("tag:yaml.org,2002:str", data)


BlockDumper.add_representer(str, _str_presenter)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--orca-install", required=True, help="OrcaSlicer install dir (for .opc version check and assets)")
    ap.add_argument("--orca-profiles", required=True, help="profiles dir fetched by fetch_orca_profiles.py")
    ap.add_argument("--out", required=True, help="output repository dir, e.g. resources/presets/orca-import")
    ap.add_argument("--report-dir", default=os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                         "..", "..", "docs", "profiles", "orca-import"))
    ap.add_argument("--per-nozzle-configs", action="store_true",
                    help="one printer config per Orca machine preset (for e2e tests) instead of one per model")
    ap.add_argument("vendors", nargs="+")
    a = ap.parse_args()

    os.makedirs(a.report_dir, exist_ok=True)
    schema = validate.load_schema()
    with open(os.path.join(a.orca_profiles, "orca_config_types.json"), encoding="utf-8") as f:
        orca_types = json.load(f)["types"]
    os.makedirs(a.out, exist_ok=True)
    failed = False
    for name in a.vendors:
        try:
            vendor = orca_source.load(a.orca_profiles, name)
            orca_source.check_against_install(vendor, a.orca_install)
            conv = VendorConverter(vendor, a.orca_install, a.out, schema, orca_types, a.per_nozzle_configs)
            report = conv.build()
        except Exception as e:  # one broken vendor must not stop the others
            import traceback
            failed = True
            print(f"{name}: STOPPED {type(e).__name__}: {e}")
            traceback.print_exc()
            continue
        problems = validate.validate_vendor_dir(conv.out, schema)
        # placeholder problems are already listed per placeholder in report.unresolved
        report.errors.extend(p for p in problems if "placeholder" not in p)
        report.orca_ignored = vendor.ignored
        report.inert = conv.inert
        report.custom = conv.custom
        report.write(os.path.join(a.report_dir, name + ".md"))
        bad = bool(report.errors or report.unresolved)
        failed |= bad
        print(f"{name}: {'FAILED' if bad else 'OK'} "
              + ", ".join(f"{k}={v}" for k, v in report.counts.items()))
        for e in report.errors[:20]:
            print("  error:", e)
        for p in sorted(report.unresolved)[:20]:
            print("  unresolved placeholder:", p, sorted(report.unresolved[p])[:3])
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
