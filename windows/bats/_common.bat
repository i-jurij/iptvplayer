@echo off
REM _common.bat - shared variables for windows/bats/*.bat
REM Поддерживается только MSYS2 UCRT64 (см. windows/scripts/common.sh).

set "WIN_BATS_DIR=%~dp0"
pushd "%WIN_BATS_DIR%..\.." >nul
set "PROJECT_ROOT=%CD%"
popd >nul

if not defined MSYS2_ROOT (
    if exist "C:\msys64\usr\bin\bash.exe" (
        set "MSYS2_ROOT=C:\msys64"
    ) else if exist "C:\tools\msys64\usr\bin\bash.exe" (
        set "MSYS2_ROOT=C:\tools\msys64"
    )
)

if not defined MSYS2_ROOT (
    echo.
    echo [ERROR] MSYS2 not found.
    echo.
    echo Install MSYS2: https://www.msys2.org/
    echo Or set environment variable: set MSYS2_ROOT=D:\path\to\msys64
    echo.
    exit /b 1
)

if not defined MSYS2_ENV set "MSYS2_ENV=UCRT64"

if /I not "%MSYS2_ENV%"=="UCRT64" (
    echo [ERROR] Only UCRT64 is supported. MINGW64 has been deprecated by MSYS2.
    echo Set: set MSYS2_ENV=UCRT64
    echo Current: MSYS2_ENV=%MSYS2_ENV%
    exit /b 1
)

set "MSYS2_BASH=%MSYS2_ROOT%\usr\bin\bash.exe"
if not exist "%MSYS2_BASH%" (
    echo [ERROR] bash.exe not found: %MSYS2_BASH%
    exit /b 1
)

set "MSYS2_SHELL=%MSYS2_ROOT%\ucrt64.exe"
if not exist "%MSYS2_SHELL%" (
    echo [ERROR] MSYS2 UCRT64 shell not found: %MSYS2_SHELL%
    echo Install the UCRT64 environment in MSYS2.
    exit /b 1
)