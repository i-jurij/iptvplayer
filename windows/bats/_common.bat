@echo off
REM _common.bat — общие переменные для windows/bats/*.bat

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
    echo [ERROR] MSYS2 не найден.
    echo.
    echo Установите MSYS2: https://www.msys2.org/
    echo Либо задайте переменную: set MSYS2_ROOT=D:\path\to\msys64
    echo.
    exit /b 1
)

if not defined MSYS2_ENV set "MSYS2_ENV=UCRT64"

set "MSYS2_BASH=%MSYS2_ROOT%\usr\bin\bash.exe"
if not exist "%MSYS2_BASH%" (
    echo [ERROR] bash.exe не найден: %MSYS2_BASH%
    exit /b 1
)

if /I "%MSYS2_ENV%"=="UCRT64"  set "MSYS2_SHELL=%MSYS2_ROOT%\ucrt64.exe"
if /I "%MSYS2_ENV%"=="MINGW64" set "MSYS2_SHELL=%MSYS2_ROOT%\mingw64.exe"
if /I "%MSYS2_ENV%"=="CLANG64" set "MSYS2_SHELL=%MSYS2_ROOT%\clang64.exe"
if not defined MSYS2_SHELL (
    echo [ERROR] Неизвестная MSYS2_ENV: %MSYS2_ENV%
    echo Допустимо: UCRT64, MINGW64, CLANG64
    exit /b 1
)
if not exist "%MSYS2_SHELL%" (
    echo [ERROR] Оболочка MSYS2 не найдена: %MSYS2_SHELL%
    exit /b 1
)