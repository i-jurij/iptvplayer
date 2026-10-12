@echo off
call "%~dp0_common.bat" || exit /b 1

echo.
echo ============================================================
echo   Сборка iptvplayer для Windows
echo   MSYS2: %MSYS2_ROOT% (%MSYS2_ENV%)
echo ============================================================
echo.

"%MSYS2_SHELL%" -here -no-start -shell bash -l -c ^
    "cd \"$(cygpath -u '%PROJECT_ROOT%')\" && ./windows/scripts/build-release.sh %*"
set RC=%ERRORLEVEL%

echo.
if %RC% EQU 0 (
    echo [OK] Готово. Артефакты: windows\install\
) else (
    echo [ERROR] Скрипт завершился с кодом %RC%.
)
pause
exit /b %RC%