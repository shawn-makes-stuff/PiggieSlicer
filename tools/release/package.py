"""Packages the Release build as the portable Windows zip that goes on a GitHub release.

    python tools/release/package.py [--out DIR]

Takes the app and its DLLs from build/src/slic3r-app-launcher/Release, the resources from resources/ (without
test_data, which only the tests use) and the licence, under PiggieSlicer-<version>-win64/, the version read from
version.inc. Then tries the packaged app from a clean folder: it must print the version.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
BINARIES = ['piggie-slicer.exe', 'OCCTWrapper.dll', 'WebView2Loader.dll', 'libgmp-10.dll', 'libmpfr-4.dll']


def version():
    text = open(os.path.join(ROOT, 'version.inc'), encoding='utf-8').read()
    return re.search(r'set\(PIGGIESLICER_VERSION "([^"]+)"\)', text).group(1)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--out', default=os.path.join(ROOT, 'build', 'release'))
    args = parser.parse_args()

    name = 'PiggieSlicer-%s-win64' % version()
    build = os.path.join(ROOT, 'build', 'src', 'slic3r-app-launcher', 'Release')
    stage = os.path.join(args.out, name)
    if os.path.exists(stage):
        shutil.rmtree(stage)
    os.makedirs(stage)
    for binary in BINARIES:
        shutil.copy2(os.path.join(build, binary), stage)
    shutil.copy2(os.path.join(ROOT, 'LICENSE'), stage)
    shutil.copytree(os.path.join(ROOT, 'resources'), os.path.join(stage, 'resources'), ignore=shutil.ignore_patterns('test_data'))

    archive = os.path.join(args.out, name + '.zip')
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for folder, _, files in os.walk(stage):
            for f in files:
                path = os.path.join(folder, f)
                z.write(path, os.path.relpath(path, args.out))

    # the packaged app runs on its own
    with tempfile.TemporaryDirectory() as tmp:
        zipfile.ZipFile(archive).extractall(tmp)
        exe = os.path.join(tmp, name, 'piggie-slicer.exe')
        run = subprocess.run([exe, '--version'], capture_output=True, text=True, timeout=120)
        out = (run.stdout + run.stderr).strip()  # the app prints it on stderr
        if version() not in out:
            sys.exit('the packaged app printed %r, not the version %s' % (out, version()))

    print('%s (%.0f MB)' % (archive, os.path.getsize(archive) / 1e6))


if __name__ == '__main__':
    main()
