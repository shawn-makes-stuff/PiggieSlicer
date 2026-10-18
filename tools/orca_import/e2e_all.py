"""Run e2e_test.py vendor by vendor.

The app loads every bundled vendor at start-up and evaluates presets for every printer config,
so with all vendors installed each CLI call takes minutes. For testing, the output directory
(--resources-dir, normally resources/presets/orca-import) is set to hold only the vendor under
test; at the end it holds every vendor of --stage again.

Usage:
  python e2e_all.py --stage <converted vendors dir> --resources-dir resources/presets/orca-import
      --exe ... --datadir <scratch> --orca-profiles ... --model ... --out <dir> [--jobs 4] [vendor ...]
"""
import argparse
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def install(stage, res, vendors):
    if os.path.exists(res):
        shutil.rmtree(res)
    os.makedirs(res)
    for v in vendors:
        shutil.copytree(os.path.join(stage, v), os.path.join(res, v))
        shutil.copyfile(os.path.join(stage, v + ".idx"), os.path.join(res, v + ".idx"))


def main():
    ap = argparse.ArgumentParser()
    for k in ("--stage", "--resources-dir", "--exe", "--datadir", "--orca-profiles", "--model", "--out"):
        ap.add_argument(k, required=True)
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("vendors", nargs="*")
    a = ap.parse_args()
    all_vendors = sorted(d for d in os.listdir(a.stage) if os.path.isdir(os.path.join(a.stage, d)))
    todo = a.vendors or all_vendors
    os.makedirs(a.out, exist_ok=True)
    try:
        for v in todo:
            install(a.stage, a.resources_dir, [v])
            dd = os.path.join(a.datadir, "dd")
            for d in [x for x in os.listdir(a.datadir)] if os.path.isdir(a.datadir) else []:
                shutil.rmtree(os.path.join(a.datadir, d), ignore_errors=True)
            os.makedirs(a.datadir, exist_ok=True)
            res = subprocess.run([sys.executable, os.path.join(HERE, "e2e_test.py"), "--exe", a.exe,
                                  "--datadir", dd, "--orca-profiles", a.orca_profiles, "--model", a.model,
                                  "--out", os.path.join(a.out, "gcode", v), "--jobs", str(a.jobs), v],
                                 capture_output=True, text=True, encoding="utf-8", errors="replace")
            with open(os.path.join(a.out, v + ".txt"), "w", encoding="utf-8", newline="\n") as f:
                f.write(res.stdout + res.stderr)
            last = (res.stdout.strip().splitlines() or ["?"])[-1]
            print(f"{v}: {last}", flush=True)
    finally:
        install(a.stage, a.resources_dir, all_vendors)


if __name__ == "__main__":
    main()
