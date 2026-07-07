@echo off
REM Relink OrcaSlicer.dll with the rebuilt GUI lib + install to build\src\Release run dir.
call "D:\VSBuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
set "PATH=D:\tools\cmake-3.31.7-windows-x86_64\bin;D:\PiggieSlicer\tools;%PATH%;D:\tools\strawberry\perl\bin"
set CMAKE_POLICY_VERSION_MINIMUM=3.5
cd /d "D:\PiggieSlicer\build"
echo === building + installing (Release) ===
cmake --build . --config Release --target install -- -m
if errorlevel 1 exit /b 2
echo === INSTALL_OK ===
