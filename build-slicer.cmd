@echo off
REM Build PiggieSlicer (OrcaSlicer 2.3.2) app (Release) with pinned toolchain.
call "D:\VSBuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
set "PATH=D:\tools\cmake-3.31.7-windows-x86_64\bin;D:\PiggieSlicer\tools;%PATH%;D:\tools\strawberry\perl\bin"
set CMAKE_POLICY_VERSION_MINIMUM=3.5
set WP=D:\PiggieSlicer
set DEPS=%WP%\deps\build\OrcaSlicer_dep
REM forward-slash form for -D args (CMake 3.31 + policy 3.5 errors on "\" escapes)
set "DEPS_FWD=%DEPS:\=/%"
cd /d "%WP%"
if not exist build mkdir build
cd build

REM /LIBPATH: deps lib dir so Boost auto-link pragmas (vc143 in Boost 1.84 headers)
REM resolve the vc143 twins build-deps.cmd created for our vc144 libs.
cmake .. -G "Visual Studio 17 2022" -A x64 -DORCA_TOOLS=ON -DCMAKE_PREFIX_PATH="%DEPS_FWD%/usr/local" -DCMAKE_INSTALL_PREFIX="./OrcaSlicer" -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXE_LINKER_FLAGS=/LIBPATH:%DEPS_FWD%/usr/local/lib -DCMAKE_SHARED_LINKER_FLAGS=/LIBPATH:%DEPS_FWD%/usr/local/lib
if errorlevel 1 exit /b 1
cmake --build . --config Release --target ALL_BUILD -- -m
if errorlevel 1 exit /b 1
cd %WP%
call scripts/run_gettext.bat
cd %WP%\build
cmake --build . --target install --config Release
