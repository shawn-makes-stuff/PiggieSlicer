"""Static pre-build checks. Run from repo root."""
import os, re, subprocess

def git(*a):
    return subprocess.run(["git", *a], capture_output=True, text=True).stdout

tracked = set(git("ls-files").split())
built = [f for f in tracked if f.startswith(("src/", "tests/")) and f.endswith((".cpp", ".hpp", ".h", ".mm", ".c", ".txt", ".cmake", ".in"))]

def read(f):
    try:
        return open(f, encoding="utf-8", errors="replace").read()
    except FileNotFoundError:
        return ""
texts = {f: read(f) for f in built}

# 1. deleted headers still included
deleted = [l.split("\t")[-1] for l in git("diff", "--name-status", "HEAD").splitlines() if l.startswith("D")]
hdrs = [d for d in deleted if d.endswith((".hpp", ".h"))]
bad = []
for h in hdrs:
    base = os.path.basename(h)
    pat = re.compile(r'#\s*(?:include|import)\s*[<"][^>"]*\b' + re.escape(base) + r'[>"]')
    for f, t in texts.items():
        if pat.search(t):
            # same basename may exist elsewhere and still be tracked
            if not any(x.endswith("/" + base) for x in tracked):
                bad.append((h, f))
print(f"[1] deleted headers: {len(hdrs)}; still included: {len(bad)}")
for h, f in bad[:20]:
    print("    ", h, "<-", f)

# 2. removed symbols
syms = ["UserAccount", "Biz::Connect", "IConnectHandler", "connect_message_handler", "PrintHostPrusaConnect",
        "ConnectUpload", "connect_upload_generic", "is_connect_upload_selected", "send_gcode_to_connect",
        "BrowserLogicPrintables", "BrowserLogicConnectPage", "BrowserLogicLogIn", "FileDownloader",
        "UrlProtocolRegistration", "register_prusaslicer_url", "RegisterPrusaSlicerURL",
        "is_prusa_account_enabled", "is_printables_enabled", "LogInScreen", "m_online_decision",
        "on_printables_secret_token", "MenuItemName::SendGcode", "CommandName::SendGcode",
        "single_instance_on_url", "enable_prusa_account", "enable_printables", "downloads_directory",
        "MacOpenURL", "PRUSA_CONNECT_UUID", "prusaslicer://"]
print("[2] removed symbols still referenced in src/tests (non-comment lines):")
any_hit = False
for s in syms:
    hits = []
    for f, t in texts.items():
        if s in t:
            for i, line in enumerate(t.splitlines(), 1):
                st = line.strip()
                if s in line and not st.startswith(("//", "*", "/*", "#")):
                    hits.append(f"{f}:{i}")
    if hits:
        any_hit = True
        print(f"    {s}: {len(hits)} -> {hits[:4]}")
if not any_hit:
    print("    none")

# 3. icon table -> files
helper = read("src/slic3r-render/src/Slic3r/App/Render/ImguiIconHelper.cpp")
names = re.findall(r'\{\s*Icon::\w+\s*,\s*"([^"]+)"\s*\}', helper)
missing = [n for n in names if not os.path.exists(f"resources/icons/{n}.svg") and not os.path.exists(f"resources/icons/{n}.png")]
print(f"[3] icon table entries: {len(names)}; missing files: {missing}")
enum_src = read("src/slic3r-render/include/Slic3r/App/Render/ImguiTypes.hpp")
m = re.search(r"enum class Icon[^{]*\{(.*?)\};", enum_src, re.S)
enum_vals = re.findall(r"^\s*(\w+)\s*[,=]", m.group(1), re.M) if m else []
mapped = set(re.findall(r"\{\s*Icon::(\w+)\s*,", helper))
unmapped = [v for v in enum_vals if v not in mapped and v not in ("None", "Count", "COUNT")]
print(f"    enum values: {len(enum_vals)}; without filename: {unmapped[:15]}")

# 4. CMake source lists -> files exist
cm_bad = []
for f in [x for x in tracked if x.endswith("CMakeLists.txt") and x.startswith("src/") or x.startswith("tests/") and x.endswith("CMakeLists.txt")]:
    d = os.path.dirname(f); t = read(f)
    for tok in re.findall(r"(?<![\w$/{.-])((?:[\w.+-]+/)*[\w.+-]+\.(?:cpp|hpp|h|c|mm))\b", t):
        if "${" in tok:
            continue
        p = os.path.normpath(os.path.join(d, tok))
        if not os.path.exists(p):
            cm_bad.append(f"{f}: {tok}")
print(f"[4] CMake-listed sources missing: {len(cm_bad)}")
for x in cm_bad[:20]:
    print("    ", x)

# 5. web/resources referenced but missing (resources/web html names in code)
web = re.findall(r'"([\w-]+\.html)"', "\n".join(texts.values()))
wm = sorted({w for w in web if not os.path.exists(f"resources/web/{w}")})
print(f"[5] html pages referenced but missing: {wm}")
