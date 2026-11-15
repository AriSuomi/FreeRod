@echo off
rem Runs clang-tidy and cppcheck on src\ using the compile database of the
rem MinGW Clang build (build\clang\compile_commands.json).
rem Usage: lint.bat [tidy|cppcheck] [file.c ...]   (default: both, all files)
rem Needs clang, clang-tidy and cppcheck on PATH (or CPPCHECK_EXE set) and the
rem clang preset configured (build.bat clang). Python for run-clang-tidy is
rem taken from PYTHON_EXE, else from "uv python find".
rem Output: build\lint\clang-tidy.log and build\lint\cppcheck.log; the
rem warnings and a count are printed at the end.
setlocal EnableDelayedExpansion
set "SCRIPT_DIR=%~dp0"
cd /d "%SCRIPT_DIR%"

set "RUN_TIDY=1"
set "RUN_CPPCHECK=1"
if /i "%~1"=="tidy" (
    set "RUN_CPPCHECK="
    shift
) else if /i "%~1"=="cppcheck" (
    set "RUN_TIDY="
    shift
)

set "FILES="
:collect_args
if "%~1"=="" goto :args_done
set "FILES=!FILES! %~1"
shift
goto :collect_args
:args_done
set "USER_FILES="
if defined FILES set "USER_FILES=1"
set "FILE_FILTER="
for %%F in (%FILES%) do set "FILE_FILTER=!FILE_FILTER! --file-filter=%%F"
if not defined FILES (
    for /r src %%F in (*.c) do set "FILES=!FILES! %%F"
)

set "DB_DIR=build\clang"
if not exist "%DB_DIR%\compile_commands.json" (
    echo %DB_DIR%\compile_commands.json not found. Run "build.bat clang" first.
    exit /b 1
)
set "LOG_DIR=build\lint"
if defined LINT_LOG_DIR set "LOG_DIR=%LINT_LOG_DIR%"
if not exist "%LOG_DIR%" mkdir "%LOG_DIR%"
set "RESULT=0"
if not defined PYTHON_EXE (
    for /f "delims=" %%P in ('uv python find 2^>nul') do set "PYTHON_EXE=%%P"
)

if not defined RUN_TIDY goto :cppcheck
rem All files: run-clang-tidy (next to clang-tidy) runs one process per core.
rem Single files, or no Python: one clang-tidy process for all of them.
set "RUN_CLANG_TIDY="
for /f "delims=" %%T in ('where clang-tidy 2^>nul') do (
    if not defined RUN_CLANG_TIDY if exist "%%~dpTrun-clang-tidy" (
        set "RUN_CLANG_TIDY=%%~dpTrun-clang-tidy"
    )
)
echo Running clang-tidy...
if not defined USER_FILES if defined PYTHON_EXE if defined RUN_CLANG_TIDY (
    "!PYTHON_EXE!" "!RUN_CLANG_TIDY!" -p "%DB_DIR%" -quiet ^
        -j %NUMBER_OF_PROCESSORS% > "%LOG_DIR%\clang-tidy.log" 2>&1
    goto :count_tidy
)
clang-tidy -p "%DB_DIR%" --quiet %FILES% > "%LOG_DIR%\clang-tidy.log" 2>&1
:count_tidy
rem Counted with "for /f": "find" may be the Unix one from Git on PATH.
set "TIDY_COUNT=0"
for /f "delims=" %%L in ('findstr /r /c:": warning: " /c:": error: " "%LOG_DIR%\clang-tidy.log"') do (
    echo %%L
    set /a TIDY_COUNT+=1
)
echo clang-tidy: !TIDY_COUNT! findings (%LOG_DIR%\clang-tidy.log)
if not "!TIDY_COUNT!"=="0" set "RESULT=1"

:cppcheck
if not defined RUN_CPPCHECK goto :done
if not defined CPPCHECK_EXE (
    where cppcheck >nul 2>&1 && set "CPPCHECK_EXE=cppcheck"
)
if not defined CPPCHECK_EXE if exist "%ProgramFiles%\Cppcheck\cppcheck.exe" (
    set "CPPCHECK_EXE=%ProgramFiles%\Cppcheck\cppcheck.exe"
)
if not defined CPPCHECK_EXE (
    echo cppcheck was not found. Set CPPCHECK_EXE.
    exit /b 1
)
set "CPPCHECK_BUILD=build\cppcheck"
if not exist "%CPPCHECK_BUILD%" mkdir "%CPPCHECK_BUILD%"

echo Running cppcheck...
"%CPPCHECK_EXE%" --project="%DB_DIR%\compile_commands.json" ^
    %FILE_FILTER% ^
    --std=c11 --platform=win64 ^
    --enable=warning,style,performance,portability ^
    --suppressions-list=cppcheck-suppressions.txt --inline-suppr ^
    --suppress=*:*vcpkg_installed* ^
    --cppcheck-build-dir="%CPPCHECK_BUILD%" -j %NUMBER_OF_PROCESSORS% ^
    --quiet --error-exitcode=0 ^
    "--template={file}:{line}: {severity} [{id}] {message}" ^
    > "%LOG_DIR%\cppcheck.log" 2>&1
set "CPPCHECK_COUNT=0"
for /f "delims=" %%L in ('findstr /r /c:"^.*:[0-9]*: [a-z]* \[" "%LOG_DIR%\cppcheck.log"') do (
    echo %%L
    set /a CPPCHECK_COUNT+=1
)
echo cppcheck: !CPPCHECK_COUNT! findings (%LOG_DIR%\cppcheck.log)
if not "!CPPCHECK_COUNT!"=="0" set "RESULT=1"

:done
exit /b %RESULT%
