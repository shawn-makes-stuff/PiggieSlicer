@echo off
REM Build PiggieSlicer (OrcaSlicer 2.3.2) dependencies (Release) with pinned toolchain.
REM Mirrors build_release_vs2022.bat deps step + portable CMake 3.31 + Strawberry Perl.
call "D:\VSBuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
set "PATH=D:\tools\cmake-3.31.7-windows-x86_64\bin;%PATH%;D:\tools\strawberry\perl\bin"
set CMAKE_POLICY_VERSION_MINIMUM=3.5
set WP=D:\PiggieSlicer
cd /d "%WP%\deps"
if not exist build mkdir build
cd build
set DEPS=%CD%\OrcaSlicer_dep

REM Boost 1.84 auto-link aliases: MSVC 14.44 tags built libs vc144 but Boost 1.84
REM headers auto-link request vc143. Same binaries, second name. Idempotent; run
REM before AND after (harmless no-op if Boost already tags vc143).
call :boostalias
REM Do NOT pass -DDESTDIR with a backslash path: under CMAKE_POLICY_VERSION_MINIMUM
REM 3.5, CMake 3.31 errors on "\P" etc. as invalid escapes. The default DESTDIR is
REM ${CMAKE_BINARY_DIR}/OrcaSlicer_dep (forward slashes) == %DEPS% anyway.
cmake ../ -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b 1
cmake --build . --config Release --target deps -- -m
set BUILD_RC=%ERRORLEVEL%
call :boostalias
exit /b %BUILD_RC%

:boostalias
if not exist "%DEPS%\usr\local\lib" goto :eof
for %%F in ("%DEPS%\usr\local\lib\libboost_*-vc144-*") do (
    setlocal ENABLEDELAYEDEXPANSION
    set "N=%%~nxF"
    set "M=!N:vc144=vc143!"
    if not exist "%DEPS%\usr\local\lib\!M!" copy /y "%%F" "%DEPS%\usr\local\lib\!M!" >nul
    endlocal
)
goto :eof
