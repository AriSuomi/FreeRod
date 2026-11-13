@echo off
rem Builds FreeRod with MSVC, MinGW GCC, or MinGW Clang using CMake Presets + vcpkg.
rem Usage: build.bat [msvc|gcc|clang] [debug|release]   (defaults: msvc debug)
rem Output: build\<msvc|gcc|clang>\<Debug|Release>\freerod.exe
setlocal
set "SCRIPT_DIR=%~dp0"
set "TOOLCHAIN="
set "CONFIG="

for %%A in (%*) do (
    if /i "%%~A"=="msvc" (
        set "TOOLCHAIN=msvc"
    ) else if /i "%%~A"=="gcc" (
        set "TOOLCHAIN=gcc"
    ) else if /i "%%~A"=="clang" (
        set "TOOLCHAIN=clang"
    ) else if /i "%%~A"=="debug" (
        set "CONFIG=debug"
    ) else if /i "%%~A"=="release" (
        set "CONFIG=release"
    ) else (
        echo Unknown argument: %%~A
        echo Usage: build.bat [msvc^|gcc^|clang] [debug^|release]
        exit /b 1
    )
)

if not defined TOOLCHAIN set "TOOLCHAIN=msvc"
if not defined CONFIG set "CONFIG=debug"

if /i "%TOOLCHAIN%"=="msvc" goto :setup_msvc
set "PRESET=mingw-%TOOLCHAIN%"
set "BUILD_PRESET=mingw-%TOOLCHAIN%-%CONFIG%"
set "BUILD_DIR=build\%TOOLCHAIN%"
where "%TOOLCHAIN%" >nul 2>&1
if errorlevel 1 (
    echo %TOOLCHAIN% was not found on PATH.
    exit /b 1
)
goto :find_vcpkg

:setup_msvc
set "PRESET=msvc"
set "BUILD_PRESET=msvc-%CONFIG%"
set "BUILD_DIR=build\msvc"
if defined VCToolsInstallDir goto :find_vcpkg

call :find_vs
if defined VSDIR if exist "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" (
    call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
)
if not defined VCToolsInstallDir (
    echo Visual Studio C++ environment not found. Run from an x64 Native Tools Command Prompt or set VSDIR.
    exit /b 1
)

:find_vcpkg
if not defined VCPKG_ROOT if defined VSINSTALLDIR (
    if exist "%VSINSTALLDIR%VC\vcpkg\scripts\buildsystems\vcpkg.cmake" (
        set "VCPKG_ROOT=%VSINSTALLDIR%VC\vcpkg"
    )
)
if not defined VCPKG_ROOT (
    call :find_vs
)
if not defined VCPKG_ROOT if defined VSDIR (
    if exist "%VSDIR%\VC\vcpkg\scripts\buildsystems\vcpkg.cmake" (
        set "VCPKG_ROOT=%VSDIR%\VC\vcpkg"
    )
)
if not defined VCPKG_ROOT (
    echo VCPKG_ROOT is not set and vcpkg was not found under Visual Studio.
    exit /b 1
)

cd /d "%SCRIPT_DIR%"
if not exist "%BUILD_DIR%\build.ninja" (
    cmake --preset "%PRESET%" || exit /b 1
)
cmake --build --preset "%BUILD_PRESET%" || exit /b 1
exit /b 0

:find_vs
if defined VSDIR exit /b 0
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
    for /f "usebackq delims=" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%I"
)
if defined VSDIR exit /b 0
for %%E in (Community Professional Enterprise BuildTools) do (
    if not defined VSDIR if exist "%ProgramFiles%\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat" (
        set "VSDIR=%ProgramFiles%\Microsoft Visual Studio\2022\%%E"
    )
)
exit /b 0
