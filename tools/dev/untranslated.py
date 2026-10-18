"""Count translatable msgids in built source that a catalog lacks. Usage: untranslated.py LANG [--dump FILE]"""
import gettext, re, subprocess, sys, json

files = [f for f in subprocess.run(["git", "ls-files", "src"], capture_output=True, text=True).stdout.split()
         if f.endswith((".cpp", ".hpp", ".h", ".mm")) and not f.startswith("src/slic3r/") and "/test" not in f]
# _L("..."), _u8L("..."), L("..."), _CTX/L_CONTEXT first arg; adjacent literals joined
call = re.compile(r'\b(?:_L|_u8L|L|_CTX_utf8|L_CONTEXT|_CTX)\s*\(\s*((?:"(?:[^"\\\n]|\\.)*"\s*)+)')
lit = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
def unesc(s):
    return s.encode("utf-8").decode("unicode_escape").encode("latin-1").decode("utf-8")
ids = {}
for f in files:
    try:
        s = open(f, encoding="utf-8").read()
    except UnicodeDecodeError:
        continue
    for m in call.finditer(s):
        raw = "".join(lit.findall(m.group(1)))
        try:
            ids.setdefault(unesc(raw), f)
        except Exception:
            pass
lang = sys.argv[1]
cat = gettext.GNUTranslations(open(f"resources/localization/{lang}/PiggieSlicer.mo", "rb"))._catalog
have = {(k[0] if isinstance(k, tuple) else k).split("\x04")[-1] for k in cat}
missing = {k: v for k, v in ids.items() if k and k not in have}
print(f"{lang}: source msgids {len(ids)}, translated {len(ids) - len(missing)}, missing {len(missing)}")
by_file = {}
for k, v in missing.items():
    by_file[v] = by_file.get(v, 0) + 1
for f, n in sorted(by_file.items(), key=lambda x: -x[1])[:15]:
    print(f"  {n:4d}  {f}")
if "--dump" in sys.argv:
    json.dump(missing, open(sys.argv[sys.argv.index("--dump") + 1], "w", encoding="utf-8"), ensure_ascii=False, indent=0)
