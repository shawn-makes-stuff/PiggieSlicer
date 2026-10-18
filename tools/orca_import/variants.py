"""Orca per-extruder / per-extruder-variant values (multi-nozzle and BBL-style profiles).

Orca stores some settings as one value per extruder variant ("Direct Drive Standard",
"Direct Drive High Flow", ...) instead of one per extruder (PrintConfig.cpp
*_options_with_variant sets; printer_options_with_variant_2 holds (normal, silent) pairs per
variant). Which slot an extruder uses:
  * machine:  the slot j with printer_extruder_id[j] == extruder number and
              printer_extruder_variant[j] == "<extruder_type> <default_nozzle_volume_type>",
  * process:  the slot with print_extruder_id == extruder number and that variant name,
  * filament: the slot whose filament_extruder_variant is that variant name.
Per-extruder keys (init_extruder_option_keys) hold one value per extruder.
The views below return a config whose vectors are reduced to the chosen extruder's value, so
the rest of the converter can keep reading slot 0.
"""


def _list(v):
    return v if isinstance(v, list) else None


def extruder_count(cfg):
    nd = cfg.get("nozzle_diameter")
    return len(nd) if isinstance(nd, list) and nd else 1


def machine_variant(cfg, i):
    """(slot index, variant name) extruder i (0-based) uses."""
    ids = _list(cfg.get("printer_extruder_id")) or []
    names = _list(cfg.get("printer_extruder_variant")) or []
    et = _list(cfg.get("extruder_type")) or ["Direct Drive"]
    nv = _list(cfg.get("default_nozzle_volume_type")) or ["Standard"]
    want = f"{et[min(i, len(et) - 1)]} {nv[min(i, len(nv) - 1)]}"
    num = str(i + 1)
    for j, (a, b) in enumerate(zip(ids, names)):
        if a == num and b == want:
            return j, want
    for j, a in enumerate(ids):
        if a == num:
            return j, names[j] if j < len(names) else want
    return (0, names[0]) if names else (0, want)


def machine_view(cfg, i, sets):
    n_ext = extruder_count(cfg)
    slots = len(_list(cfg.get("printer_extruder_variant")) or [None])
    j, _ = machine_variant(cfg, i)
    v1, v2 = set(sets["printer_options_with_variant_1"]), set(sets["printer_options_with_variant_2"])
    ext = set(sets["extruder_option_keys"])
    out = dict(cfg)
    for k, v in cfg.items():
        if not isinstance(v, list) or len(v) <= 1:
            continue
        if k in v2 and len(v) == 2 * slots and slots > 1:
            out[k] = [v[2 * j], v[2 * j + 1]]
        elif k in v1 and len(v) == slots and slots > 1:
            out[k] = [v[j]]
        elif k in ext and k != "extruder_offset" and len(v) == n_ext and n_ext > 1:
            out[k] = [v[i]]
    return out


def process_view(cfg, variant, extruder_number, sets):
    ids = _list(cfg.get("print_extruder_id")) or []
    names = _list(cfg.get("print_extruder_variant")) or []
    slots = len(names)
    if slots <= 1:
        return cfg
    j = next((k for k, (a, b) in enumerate(zip(ids, names)) if a == str(extruder_number) and b == variant),
             next((k for k, b in enumerate(names) if b == variant), 0))
    keys = set(sets["print_options_with_variant"])
    out = dict(cfg)
    for k, v in cfg.items():
        if k in keys and isinstance(v, list) and len(v) == slots:
            out[k] = [v[j]]
    return out


def filament_view(cfg, variant, sets):
    names = _list(cfg.get("filament_extruder_variant")) or []
    slots = len(names)
    if slots <= 1:
        return cfg
    j = names.index(variant) if variant in names else 0
    keys = set(sets["filament_options_with_variant"])
    out = dict(cfg)
    for k, v in cfg.items():
        if k in keys and isinstance(v, list) and len(v) == slots:
            out[k] = [v[j]]
    return out
