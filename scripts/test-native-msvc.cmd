@echo off
setlocal enabledelayedexpansion
rem Run the native Unity tests (test\test_*) with MSVC, for Windows machines
rem without a host gcc (where `pio test -e native` cannot compile).
rem Needs: Visual Studio Build Tools with the "Desktop development with C++"
rem workload, and the Unity sources, which PlatformIO puts in
rem .pio\libdeps\native\Unity (run `pio pkg install -e native` once).
rem Usage: scripts\test-native-msvc.cmd     (exit code 0 = all tests passed)

set "ROOT=%~dp0.."
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (echo vswhere.exe not found: install Visual Studio Build Tools. & exit /b 1)
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%I"
if not defined VSINSTALL (echo MSVC C++ tools not found: add the C++ workload. & exit /b 1)
call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul

set "UNITY=%ROOT%\.pio\libdeps\native\Unity\src"
if not exist "%UNITY%\unity.c" (echo Unity not found: run "pio pkg install -e native" first. & exit /b 1)

rem Every lib\<module> is an include dir and contributes its .cpp files.
set INC=/I "%UNITY%"
set SRC="%UNITY%\unity.c"
for /d %%L in ("%ROOT%\lib\*") do (
    set INC=!INC! /I "%%~fL"
    for %%F in ("%%~fL\*.cpp") do set SRC=!SRC! "%%~fF"
)

set "OUT=%ROOT%\.pio\build\native-msvc"
if not exist "%OUT%" mkdir "%OUT%"
set FAILED=0
for /d %%T in ("%ROOT%\test\test_*") do (
    cl /nologo /EHsc /W3 /std:c++17 !INC! /Fo"%OUT%\\" /Fe"%OUT%\%%~nT.exe" "%%~fT\%%~nT.cpp" !SRC! >"%OUT%\%%~nT.log" 2>&1
    if errorlevel 1 (
        echo BUILD FAILED: %%~nT
        type "%OUT%\%%~nT.log"
        set FAILED=1
    ) else (
        echo === %%~nT
        "%OUT%\%%~nT.exe"
        if errorlevel 1 set FAILED=1
    )
)
if !FAILED!==0 (echo ALL NATIVE TESTS PASSED) else (echo SOME NATIVE TESTS FAILED)
exit /b !FAILED!
