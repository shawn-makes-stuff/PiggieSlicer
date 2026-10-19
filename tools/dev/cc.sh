#!/bin/bash
# Compile-check single .cpp files (no linking): tools/dev/cc.sh src/.../File.cpp [more.cpp ...]
# Uses the generated Release projects in build/; the file must already be part of one.
cd "$(dirname "$0")/../.."
export WindowsSdkDir="C:/Program Files (x86)/Windows Kits/10/" WindowsSDKVersion="10.0.26100.0" MSYS_NO_PATHCONV=1
MSB=/d/VSBuildTools/MSBuild/Current/Bin/amd64/MSBuild.exe
for f in "$@"; do
  case "$f" in
    src/libslic3r/*) proj=build/src/libslic3r/libslic3r.vcxproj ;;
    src/slic3r-biz-algorithms/*) proj=build/src/slic3r-biz-algorithms/slic3r-biz-algorithms.vcxproj ;;
    src/libpgcode/*) proj=build/src/libpgcode/libpgcode.vcxproj ;;
    src/libvgcode/*) proj=build/src/libvgcode/libvgcode.vcxproj ;;
    src/slic3r-biz-parser/*) proj=build/src/slic3r-biz-parser/slic3r-biz-parser.vcxproj ;;
    src/slic3r-app-launcher/*) proj=build/src/slic3r-app-launcher/slic3r-app-launcher.vcxproj ;;
    src/slic3r-app-cli/*) proj=build/src/slic3r-app-cli/slic3r-app-cli.vcxproj ;;
    src/slic3r-render/*) proj=build/src/slic3r-render/slic3r-render.vcxproj ;;
    src/slic3r-shared/test/*) proj=build/src/slic3r-shared/slic3r-shared-tests.vcxproj ;;
    src/slic3r-shared/*) proj=build/src/slic3r-shared/slic3r-shared.vcxproj ;;
    src/slic3r-app-desktop/*) proj=build/src/slic3r-app-desktop/slic3r-app-desktop.vcxproj ;;
    src/slic3r-domain/test/*) proj=build/src/slic3r-domain/slic3r-domain-tests.vcxproj ;;
    tests/libslic3r/*) proj=build/tests/libslic3r/libslic3r_tests.vcxproj ;;
    tests/fff_print/*) proj=build/tests/fff_print/fff_print_tests.vcxproj ;;
    src/slic3r-domain/*) proj=build/src/slic3r-domain/slic3r-domain.vcxproj ;;
    *) echo "no project for $f"; continue ;;
  esac
  win=$(cygpath -w "$PWD/$f")
  out=$("$MSB" "$proj" -t:ClCompile -p:Configuration=Release -p:SelectedFiles="$win" -p:CL_MPCount=1 -nologo -v:minimal -nodeReuse:false 2>&1)
  [ -z "$out" ] && { echo "NO OUTPUT $f"; continue; }
  errs=$(echo "$out" | grep -E "error C|fatal error" | sed 's/\[.*//' | sort -u)
  if [ -n "$errs" ]; then echo "FAIL $f"; echo "$errs" | head -25; else echo "ok   $f"; fi
done
