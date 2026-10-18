"""Audit Orca's settings tabs against PiggieSlicer (PrusaSlicer 3.0).

Reads OrcaSlicer's Tab.cpp (TabPrint, TabFilament, TabPrinter: page -> option group -> keys),
the converter's mapping (mapping.MAP / STRUCTURAL / DROP) and 3.0's config definitions
(ConfigDefsFDM.cpp / ConfigCommon.cpp: category and option group of each key), and writes a
Markdown report: for every Orca setting, where it is in Orca and what it is in 3.0.

  python audit_tabs.py --tab-cpp <Orca src/slic3r/GUI/Tab.cpp> --out <report.md>
"""
import argparse
import difflib
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import mapping  # noqa: E402

ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
DOMAIN_DEFS = [
    os.path.join(ROOT, "src", "slic3r-domain", "src", "Slic3r", "Domain", "ConfigDefsFDM.cpp"),
    os.path.join(ROOT, "src", "slic3r-domain", "src", "Slic3r", "Domain", "ConfigCommon.cpp"),
]

# Orca Tab.cpp: the functions that build each tab, and the patterns that add a setting
TABS = [("Process", "TabPrint::build()"), ("Filament", "TabFilament::build()"),
        ("Printer", "TabPrinter::build_fff()"), ("Printer", "TabPrinter::build_unregular_pages(")]
PAGE = re.compile(r'add_options_page\(\s*L\("([^"]+)"\)')
GROUP = re.compile(r'new_optgroup\(\s*L\("([^"]+)"\)')
OPTION = re.compile(r'(?:append_single_option_line|get_option|append_option_line\(\s*\w+\s*,)\s*\(?\s*"([a-z0-9_]+)"')


def function_bodies(source, signature):
    """Text of each function whose definition line contains the signature (to the next 'void Tab')."""
    out = []
    for m in re.finditer(r"^void " + re.escape(signature), source, re.M):
        end = source.find("\nvoid Tab", m.end())
        out.append(source[m.start(): end if end > 0 else len(source)])
    return out


def orca_settings(tab_cpp):
    source = open(tab_cpp, encoding="utf-8").read()
    rows, seen = [], set()
    for tab, signature in TABS:
        for body in function_bodies(source, signature):
            page, group = "", ""
            for line in body.splitlines():
                if (m := PAGE.search(line)):
                    page, group = m.group(1), ""
                if (m := GROUP.search(line)):
                    group = m.group(1)
                for key in OPTION.findall(line):
                    if (tab, key) not in seen:
                        seen.add((tab, key))
                        rows.append((tab, page, group, key))
    return rows


def domain_defs():
    """3.0 key -> (category, option group) from the domain config definitions."""
    defs = {}
    for path in DOMAIN_DEFS:
        if not os.path.exists(path):
            continue
        current = None
        for line in open(path, encoding="utf-8"):
            if (m := re.search(r'defs\.add\("([a-z0-9_]+)"', line)):
                current = m.group(1)
                defs.setdefault(current, ["", ""])
            elif current and (m := re.search(r"->category\s*=\s*\w+::Category::(\w+)", line)):
                defs[current][0] = m.group(1)
            elif current and (m := re.search(r"->option_group\s*=\s*\w+::OptionGroup::(\w+)", line)):
                defs[current][1] = m.group(1)
    return defs


def status(key, defs):
    """(status, 3.0 keys with their place, note)"""
    if key in mapping.MAP:
        targets = []
        for box, target, _conv in mapping.MAP[key]:
            category, group = defs.get(target, ["", ""])
            place = f"{category}/{group}" if category else "(not in 3.0 defs)"
            targets.append(f"`{target}` ({box}; {place})")
        return "mapped", "; ".join(targets), ""
    if key in mapping.STRUCTURAL:
        return "structural", "", "used to build the vendor structure"
    if key in mapping.DROP:
        return "dropped", "", mapping.DROP[key]
    if key in defs:
        category, group = defs[key]
        return "same key, unmapped", f"`{key}` ({category}/{group})", "3.0 has the key; the converter does not map it"
    # not carried: either 3.0 has it under another name (map it) or it has to be ported
    near = difflib.get_close_matches(key, list(defs), n=3, cutoff=0.6)
    return "not mapped", "", ("closest 3.0 keys: " + ", ".join(f"`{n}`" for n in near)) if near else ""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tab-cpp", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    rows = orca_settings(args.tab_cpp)
    defs = domain_defs()
    counts = {}
    lines = []
    for tab in ("Process", "Filament", "Printer"):
        lines += [f"## {tab}", "", "| Orca page | Orca group | Orca key | status | PiggieSlicer | note |",
                  "|---|---|---|---|---|---|"]
        for t, page, group, key in rows:
            if t != tab:
                continue
            st, target, note = status(key, defs)
            counts[(tab, st)] = counts.get((tab, st), 0) + 1
            lines.append(f"| {page} | {group} | `{key}` | {st} | {target} | {note} |")
        lines.append("")

    summary = ["| tab | " + " | ".join(["mapped", "structural", "dropped", "same key, unmapped", "not mapped"]) + " |",
               "|---|---|---|---|---|---|"]
    for tab in ("Process", "Filament", "Printer"):
        summary.append(f"| {tab} | " + " | ".join(str(counts.get((tab, s), 0)) for s in
                       ["mapped", "structural", "dropped", "same key, unmapped", "not mapped"]) + " |")

    with open(args.out, "w", encoding="utf-8") as f:
        f.write("# Orca settings audit (generated by tools/orca_import/audit_tabs.py)\n\n")
        f.write("Every setting on Orca's Process / Filament / Printer tabs (Tab.cpp, page and group as Orca "
                "shows them) and what PiggieSlicer has for it: `mapped` = the converter carries it to the 3.0 "
                "key shown (with its 3.0 category/option group, i.e. where our UI shows it); `structural` = "
                "used to build the vendor structure; `dropped` = left out on purpose; `same key, unmapped` = 3.0 "
                "has a setting of that name the converter does not map; `not mapped` = no mapping yet: 3.0 may "
                "have it under another name (closest names shown) or it has to be ported.\n\n")
        f.write("\n".join(summary) + "\n\n" + "\n".join(lines))
    print("\n".join(summary))


if __name__ == "__main__":
    main()
