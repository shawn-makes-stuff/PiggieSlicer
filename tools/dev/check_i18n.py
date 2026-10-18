"""Check every msgid mentioning PiggieSlicer in a catalog exists verbatim in built source literals."""
import gettext, re, subprocess, sys

files = [f for f in subprocess.run(["git", "ls-files", "src"], capture_output=True, text=True).stdout.split()
         if f.endswith((".cpp", ".hpp", ".h", ".mm")) and not f.startswith("src/slic3r/")]
lit = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
pool = []
for f in files:
    try:
        s = open(f, encoding="utf-8").read()
    except UnicodeDecodeError:
        continue
    if "PiggieSlicer" not in s and "PrusaSlicer" not in s:
        continue
    for m in re.finditer(r'(?:"(?:[^"\\\n]|\\.)*"\s*)+', s):
        pool.append("".join(lit.findall(m.group(0))))
pool = "\x00".join(pool)

def esc(t):
    return t.replace("\\", "\\\\").replace("\n", "\\n").replace('"', '\\"')

lang = sys.argv[1] if len(sys.argv) > 1 else "de"
cat = gettext.GNUTranslations(open(f"resources/localization/{lang}/PiggieSlicer.mo", "rb"))._catalog
ids = sorted({(k[0] if isinstance(k, tuple) else k).split("\x04")[-1] for k in cat})
pig = [i for i in ids if "PiggieSlicer" in i]
pru = [i for i in ids if "PrusaSlicer" in i]
miss = [i for i in pig if esc(i) not in pool]
print(f"{lang}: msgids with PiggieSlicer: {len(pig)}, not found verbatim in source: {len(miss)}")
for i in miss:
    print("  MISSING:", i[:140].replace("\n", " "))
live_pru = [i for i in pru if esc(i) in pool]
print(f"msgids still with PrusaSlicer that the built source uses: {len(live_pru)}")
for i in live_pru:
    print("  KEPT:", i[:140].replace("\n", " "))
