@echo off
REM Incremental build of the GUI lib (compiles AcLan.cpp + AnycubicDevicePanel.cpp).
call "D:\VSBuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
set "PATH=D:\tools\cmake-3.31.7-windows-x86_64\bin;D:\PiggieSlicer\tools;%PATH%;D:\tools\strawberry\perl\bin"
set CMAKE_POLICY_VERSION_MINIMUM=3.5
cd /d "D:\PiggieSlicer\build"
echo === building libslic3r_gui ===
cmake --build . --config Release --target libslic3r_gui -- -m
if errorlevel 1 exit /b 2
echo === GUI_LIB_OK ===
