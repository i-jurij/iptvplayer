@echo off
call "%~dp0_common.bat" || exit /b 1

echo.
echo Открываю MSYS2 %MSYS2_ENV% в %PROJECT_ROOT%
echo.

"%MSYS2_SHELL%" -here -shell bash -l -c ^
    "cd \"$(cygpath -u '%PROJECT_ROOT%')\" && exec bash -l"