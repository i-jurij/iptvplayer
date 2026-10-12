@echo off
call "%~dp0_common.bat" || exit /b 1

echo.
echo ============================================================
echo   iptvplayer: clean Windows build directories
echo ============================================================
echo.

"%MSYS2_SHELL%" -here -no-start -shell bash -l -c ^
    "cd \"$(cygpath -u '%PROJECT_ROOT%')\" && ./windows/scripts/clean.sh %*"
set RC=%ERRORLEVEL%

echo.
echo [OK] Cleanup done.
pause
exit /b %RC%