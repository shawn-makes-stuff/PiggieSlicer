"""Download the OrcaSlicer vendor profile JSONs for a release tag.

Since Orca 2.5 the installed app ships presets only as binary .opc caches, so the
JSON sources are fetched from the release tag that built the install. Only *.json
files are downloaded (assets are taken from the local Orca install instead).

Usage:
  python fetch_orca_profiles.py --tag v2.5.0-alpha --out <dir> Anycubic Elegoo OrcaFilamentLibrary

Also writes <out>/orca_defaults.json (Orca's built-in preset defaults, see orca_defaults.py) and
<out>/orca_config_types.json: Orca option key -> option type (coFloats, ...) parsed
from src/libslic3r/PrintConfig.cpp at the same tag; the converter uses it to read values the
way Orca does (e.g. a coFloats value "35%" is read by Orca as 35).

Needs an authenticated `gh` CLI. Files that already exist with the same git blob
sha are skipped, so re-runs are cheap.
"""
import argparse
import base64
import hashlib
import json
import os
import re
import subprocess
import sys

REPO = "OrcaSlicer/OrcaSlicer"


def gh_json(path):
    out = subprocess.run(["gh", "api", path], check=True, capture_output=True)
    return json.loads(out.stdout)


def git_blob_sha(data: bytes) -> str:
    return hashlib.sha1(b"blob %d\0" % len(data) + data).hexdigest()


def fetch_blob(sha: str) -> bytes:
    blob = gh_json(f"repos/{REPO}/git/blobs/{sha}")
    return base64.b64decode(blob["content"])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tag", required=True)
    ap.add_argument("--out", required=True, help="output profiles dir (gets <Vendor>.json + <Vendor>/)")
    ap.add_argument("vendors", nargs="+")
    a = ap.parse_args()

    if a.vendors == ["ALL"]:
        # Whole profile set in one request: stream the release tarball and keep only
        # resources/profiles/**.json (data only; nothing from it is executed).
        import tarfile
        proc = subprocess.Popen(["gh", "api", f"repos/{REPO}/tarball/{a.tag}"], stdout=subprocess.PIPE)
        n = 0
        with tarfile.open(fileobj=proc.stdout, mode="r|gz") as tf:
            for m in tf:
                parts = m.name.split("/", 1)
                if len(parts) < 2 or not m.isfile():
                    continue
                rel = parts[1]
                if not (rel.startswith("resources/profiles/") and rel.endswith(".json")):
                    continue
                rel = rel[len("resources/profiles/"):]
                if ".." in rel.split("/"):
                    continue
                dst = os.path.join(a.out, *rel.split("/"))
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                with open(dst, "wb") as f:
                    f.write(tf.extractfile(m).read())
                n += 1
        if proc.wait() != 0:
            sys.exit("tarball download failed")
        print(f"{n} json files extracted into {a.out}")
        wanted = []
        top = []
    else:
        top = gh_json(f"repos/{REPO}/git/trees/{a.tag}:resources/profiles")["tree"]
    wanted = []
    for e in top:
        for v in a.vendors:
            if e["path"] == v + ".json":
                wanted.append((e["path"], e["sha"]))
            elif e["path"] == v and e["type"] == "tree":
                sub = gh_json(f"repos/{REPO}/git/trees/{e['sha']}?recursive=1")
                if sub.get("truncated"):
                    sys.exit(f"tree listing for {v} truncated")
                for s in sub["tree"]:
                    if s["type"] == "blob" and s["path"].endswith(".json"):
                        wanted.append((f"{v}/{s['path']}", s["sha"]))
    fetched = 0
    for rel, sha in sorted(wanted):
        dst = os.path.join(a.out, rel)
        if os.path.exists(dst):
            with open(dst, "rb") as f:
                if git_blob_sha(f.read()) == sha:
                    continue
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        data = fetch_blob(sha)
        if git_blob_sha(data) != sha:
            sys.exit(f"sha mismatch for {rel}")
        with open(dst, "wb") as f:
            f.write(data)
        fetched += 1
    print(f"{len(wanted)} json files listed, {fetched} downloaded into {a.out}")

    def raw(path):
        r = subprocess.run(["gh", "api", f"repos/{REPO}/contents/{path}?ref={a.tag}",
                            "-H", "Accept: application/vnd.github.raw"], check=True, capture_output=True)
        return r.stdout.decode("utf-8")
    src = raw("src/libslic3r/PrintConfig.cpp")
    types = dict(re.findall(r'this->add(?:_nullable)?\("([A-Za-z0-9_]+)",\s*(co[A-Za-z]+)\)', src))
    with open(os.path.join(a.out, "orca_config_types.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump({"tag": a.tag, "types": dict(sorted(types.items()))}, f, indent=1)
    print(f"{len(types)} Orca option types written")

    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import orca_defaults
    d = orca_defaults.build(raw("src/libslic3r/Preset.cpp"), src, raw("src/libslic3r/PrintConfigConstants.hpp"),
                            raw("src/libslic3r/PrintConfig.hpp"))
    d["tag"] = a.tag
    with open(os.path.join(a.out, "orca_defaults.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(d, f, indent=1, sort_keys=True)
    print("Orca defaults written:", {k: len(v) for k, v in d["defaults"].items()})


if __name__ == "__main__":
    main()
