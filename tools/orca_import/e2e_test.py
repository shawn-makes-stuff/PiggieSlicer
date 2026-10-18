"""End-to-end check of converted vendors with the built PiggieSlicer executable.

For every printer config of the given vendors: pick the print preset Orca names as the
machine's default_print_profile (else the first compatible one) and the machine's first
default_filament_profile (else the first compatible filament), slice a 20 mm box with the CLI,
and compare the exported G-code with the Orca source values:
  * export succeeded, no '[name]' / '{...' left in the G-code body,
  * bed shape and max print height (config footer) equal Orca printable_area / printable_height,
  * first-layer nozzle and bed temperature appear in the start section,
  * the retraction length (sum of consecutive retract moves, most common) equals the filament
    override or else the machine retraction_length.

Usage:
  python e2e_test.py --exe <piggie-slicer.exe> --datadir <scratch dir> --orca-profiles <dir>
      --model <stl> --out <gcode dir> Anycubic Elegoo [--only "<config name>" ...]
Never point --datadir at the real settings folder.
"""
import argparse
import collections
import json
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import convert  # noqa: E402
import mapping as M  # noqa: E402
import orca_source  # noqa: E402


def run(exe, datadir, args):
    p = subprocess.run([exe, "--datadir", datadir, "--loglevel", "1"] + args,
                       capture_output=True, text=True, encoding="utf-8", errors="replace")
    return p.returncode, p.stdout + p.stderr


def query_profiles(exe, datadir, printer):
    _, out = run(exe, datadir, ["--printer-profile", printer, "--query-print-tool-filament-profiles"])
    return json.loads(out[out.index("{"):])


def retraction_sums(lines):
    sums, cur = collections.Counter(), 0.0
    for ln in lines:
        # retract moves, including wipe moves that retract while travelling (G1 X.. Y.. E-..)
        m = re.match(r"G1 (?:[XYZF][-\d.]+ )*E(-?[\d.]+)", ln)
        if not m:
            continue
        e = float(m.group(1))
        if e < 0:
            cur += -e
        elif cur:
            sums[round(cur, 3)] += 1
            cur = 0.0
    return sums


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True)
    ap.add_argument("--datadir", required=True)
    ap.add_argument("--orca-profiles", required=True)
    ap.add_argument("--model", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--only", action="append", default=[])
    ap.add_argument("--jobs", type=int, default=1, help="parallel app instances (one data dir each)")
    ap.add_argument("vendors", nargs="+")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    import concurrent.futures
    import shutil
    import threading
    jobs = []
    for vname in a.vendors:
        v = orca_source.load(a.orca_profiles, vname)
        conv = convert.VendorConverter(v, "", "", None, {})
        conv.collect_machines()
        for mname in v.instantiated("machine"):
            if a.only and mname not in a.only:
                continue
            jobs.append((vname, v, conv, mname))
    local = threading.local()
    counter = iter(range(10 ** 6))
    lock = threading.Lock()

    def datadir():
        # one scratch data dir per worker thread (the app writes into it at start)
        if not hasattr(local, "dd"):
            with lock:
                local.dd = f"{a.datadir}_{next(counter)}" if a.jobs > 1 else a.datadir
            os.makedirs(local.dd, exist_ok=True)
        return local.dd

    def check(job):
        vname, v, conv, mname = job
        row = {"vendor": vname, "config": mname, "checks": [], "ok": True}

        def fail(msg):
            row["ok"] = False
            row["checks"].append("FAIL " + msg)
        if mname not in conv.machines:
            fail("not converted (see vendor report)")
            return row
        dd = datadir()
        mcfg = conv.machines[mname][2]
        cname = conv.config_name(mname)
        try:
            q = query_profiles(a.exe, dd, cname)
        except ValueError:
            fail("printer config not found by the app")
            return row
        prints = {p["name"]: p for p in q["print_profiles"]}
        want = M.text(mcfg.get("default_print_profile", ""))
        pname = want if want in prints else (sorted(prints)[0] if prints else None)
        if not pname:
            fail("no print preset")
            return row
        fils = prints[pname]["filament_profiles"]
        dfl = mcfg.get("default_filament_profile", [])
        dfl = dfl if isinstance(dfl, list) else [dfl]
        fname = next((f for f in dfl if f in fils), fils[0] if fils else None)
        if not fname:
            fail("no filament preset")
            return row
        row["print"], row["filament"] = pname, fname
        # own directory per config: the app writes an intermediate file named by the export
        # name template next to -o before renaming it, which collides between parallel runs
        gdir = os.path.join(a.out, convert.slug(mname))
        os.makedirs(gdir, exist_ok=True)
        gpath = os.path.join(gdir, convert.slug(mname) + ".gcode")
        if os.path.exists(gpath):
            os.remove(gpath)
        code, out = run(a.exe, dd, ["--printer-profile", cname, "--print-profile", pname,
                                           "--material-profile", fname, "--export-gcode",
                                           "-o", gpath, a.model])
        if not os.path.exists(gpath):
            fail("export failed: " + " | ".join(l for l in out.splitlines()
                                                 if "BedGeometry" not in l and "stl_open" not in l
                                                 and "PhysicalPrinter" not in l)[-300:])
            return row
        with open(gpath, encoding="utf-8", errors="replace") as f:
            lines = f.read().splitlines()
        end = next((i for i, l in enumerate(lines) if "prusaslicer_json_config = begin" in l), len(lines))
        body = lines[:end]
        footer = {l[2:].split(" = ", 1)[0]: l[2:].split(" = ", 1)[1]
                  for l in lines[end:] if l.startswith("; ") and " = " in l}
        left = [l for l in body if re.search(r"\[[A-Za-z_][A-Za-z0-9_]*(\[[^\]]*\])?\]|\{[A-Za-z_]", l)]
        if left:
            fail(f"placeholder left in G-code: {left[0][:80]!r}")
        first_layer = next((i for i, l in enumerate(body) if l.startswith(";LAYER_CHANGE")), len(body))
        start = "\n".join(body[:first_layer])
        # bed shape / height
        exp_shape = ",".join(M.vec2_list(mcfg["printable_area"]))

        def pts(t):
            return [tuple(float(c) for c in p.split("x")) for p in t.split(",") if "x" in p]
        got_pts, exp_pts = pts(footer.get("bed_shape", "")), pts(exp_shape)
        same = len(got_pts) == len(exp_pts) and all(
            abs(a[0] - b[0]) < 2e-3 and abs(a[1] - b[1]) < 2e-3 for a, b in zip(got_pts, exp_pts))
        if not same:
            fail(f"bed_shape {footer.get('bed_shape')} != Orca {exp_shape}")
        else:
            row["checks"].append(f"bed {exp_shape}")
        if float(footer.get("max_print_height", "nan")) != float(M.num(mcfg["printable_height"])):
            fail(f"max_print_height {footer.get('max_print_height')} != Orca {mcfg['printable_height']}")
        # temperatures
        fcfg, _ = v.resolve(fname)
        t = M.integer(fcfg["nozzle_temperature_initial_layer"])
        # M104/M109 S<t>, or a firmware macro argument such as START_PRINT EXTRUDER_TEMP=<t>;
        # 0 = unheated (no command expected)
        if t == 0 or re.search(rf"(M10[49] (?:[TP]\d+ )?[SR]|G10 P\d+ (?:R\d+ )?S|M568 P\d+ (?:R\d+ )?S|[A-Za-z_]*(?:EXTRUDER|HOTEND|NOZZLE|xtruder)[A-Za-z_]*=){t}\b", start):
            row["checks"].append(f"nozzle {t}")
        else:
            fail(f"first-layer nozzle temp {t} not in start section")
        bed = convert.BED_TYPES[convert.default_bed_type(mcfg, convert.Report(vname))]
        key = bed[2] + "_initial_layer"
        if key in fcfg:
            bt = M.integer(fcfg[key])
            if bt == 0 or re.search(rf"(M1[49]0 (?:[TP]\d+ )?[SR]|[A-Za-z_]*(?:BED|[bB]ed)[A-Za-z_]*=){bt}\b", start):
                row["checks"].append(f"bed {bed[3]} {bt}")
            else:
                fail(f"first-layer bed temp {bt} ({bed[3]}) not in start section")
        # retraction
        fl = fcfg.get("filament_retraction_length")
        exp = M.num(fl) if fl is not None and not M.is_nil(fl) else M.num(mcfg["retraction_length"])
        sums = retraction_sums(body[first_layer:])
        got = sums.most_common(1)[0][0] if sums else None
        if got is None and exp == 0:
            row["checks"].append("retract 0 (none)")
        elif got is None and any(re.match(r"G10\s*(;|$)", l) for l in body[first_layer:]):
            row["checks"].append("firmware retraction (G10)")
        elif got is None or abs(got - exp) > 1e-3:
            fail(f"retraction {got} != Orca {exp}")
        else:
            row["checks"].append(f"retract {exp}")
        return row

    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as ex:
        results = list(ex.map(check, jobs))
    ok = sum(r["ok"] for r in results)
    for r in results:
        print(("OK  " if r["ok"] else "BAD ") + r["config"], "|", r.get("print", ""), "|",
              r.get("filament", ""), "|", "; ".join(r["checks"]))
    print(f"{ok}/{len(results)} printer configs passed")
    sys.exit(0 if ok == len(results) else 1)


if __name__ == "__main__":
    main()
