"""Compare the settings PiggieSlicer shares with Orca (same key) against Orca's PrintConfig.cpp: label, tooltip,
units, min, max, default and enum values / labels. Writes audit.json (key -> differences) to the current directory.
Defaults and units are compared loosely (numbers / unit strings), so review each reported difference.

  python audit_defs.py <Orca src/libslic3r/PrintConfig.cpp>
"""
import json
import os
import re
import sys
from collections import Counter

ORCA = sys.argv[1]
OURS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "src", "slic3r-domain", "src", "Slic3r",
                    "Domain", "ConfigDefsFDM.cpp")
STR = r'"(?:[^"\\]|\\.)*"'


def lits(text):
    """The concatenated C string literals in text."""
    esc = {"n": "\n", "t": "\t", '"': '"', "\\": "\\", "'": "'"}
    return "".join(re.sub(r"\\(.)", lambda m: esc.get(m.group(1), m.group(0)), s[1:-1]) for s in re.findall(STR, text))


def field(body, name):
    m = re.search(r"->" + name + r"\s*=\s*(.*?);\s*\n", body, re.S)
    return m.group(1) if m else None


def nums(text):
    return sorted(set(float(x) for x in re.findall(r"(?<![\w.])-?\d+\.?\d*(?:e-?\d+)?", text or "")))


def blocks(src, pat):
    out = {}
    for m in re.finditer(pat + r'\("([a-z0-9_]+)"(.*?)(?=' + pat + r'\(|\Z)', src, re.S):
        out.setdefault(m.group(1), m.group(2))
    return out


def main():
    orca = blocks(open(ORCA, encoding="utf-8").read(), r"this->add(?:_nullable)?")
    ours = blocks(open(OURS, encoding="utf-8").read(), r"defs\.add")
    rows = []
    for k in sorted(set(orca) & set(ours)):
        o, u = orca[k], ours[k]
        issues = []
        ol, ul = lits(field(o, "label") or ""), lits(field(u, "label") or "")
        if ol != ul:
            issues.append(("label", ol, ul))
        ot = " ".join(lits(field(o, "tooltip") or "").split())
        ut = " ".join(lits(field(u, "tooltip") or "").split())
        if ot != ut:
            issues.append(("tooltip", ot, ut))
        side = lits(field(o, "sidetext") or "")
        units = [s[1:-1] for s in re.findall(STR, field(u, "units") or "")]
        if (side or units) and side.replace(" ", "") not in ("".join(units), "/".join(units), "or".join(units)) \
                and not (units and side and side.split(" or ")[0].strip() == units[0]):
            issues.append(("units", side, units))
        for f in ("min", "max"):
            a, b = field(o, f), field(u, f)
            if nums(a) != nums(b):
                issues.append((f, a, b))
        od = re.search(r"set_default_value\((.*?)\);\s*\n", o, re.S)
        od = od.group(1) if od else ""
        ud = field(u, "init_fn") or ""
        if nums(od) != nums(re.sub(r"\{\{.*", "", ud, flags=re.S)):
            issues.append(("default", " ".join(od.split()), " ".join(ud.split())[:200]))
        ov = re.findall(r"enum_values\.push_back\((" + STR + r")\)", o) + re.findall(r"enum_values\s*=\s*\{(.*?)\}", o, re.S)
        ovals = [s[1:-1] for s in re.findall(STR, " ".join(ov))]
        uvals = re.findall(r'\{\s*int\([^)]*\)\s*,\s*"([^"]*)"', u) or re.findall(r'\{\s*[\w:]+\s*,\s*"([^"]*)"\s*,', u)
        if ovals and ovals != uvals:
            issues.append(("enum values", ovals, uvals))
        olab = [lits(s) for s in re.findall(r"enum_labels\.push_back\((.*?)\);", o)]
        ulab = [lits(s) for s in re.findall(r'"[^"]*"\s*,\s*(L\(' + STR + r"\))\s*\}", u)]
        if olab and ulab and olab != ulab:
            issues.append(("enum labels", olab, ulab))
        if issues:
            rows.append((k, issues))
    json.dump(rows, open("audit.json", "w", encoding="utf-8"), indent=1, ensure_ascii=False)
    print(len(set(orca) & set(ours)), "shared keys,", len(rows), "with differences")
    print(Counter(i[0] for _, iss in rows for i in iss))


if __name__ == "__main__":
    main()
