@echo off
setlocal DisableDelayedExpansion
title FNaF 2 Vita - Data Tool
cd /d "%~dp0"
set "APK=%~f1"
if not defined APK goto noapk
if not exist "%APK%" goto noapk

set "PY="
py -3 --version >nul 2>nul && set "PY=py -3"
if not defined PY python --version >nul 2>nul && set "PY=python"
if not defined PY goto nopy

echo APK: "%APK%"
echo.
echo Installing Python packages...
%PY% -m pip install -q -r requirements.txt
if errorlevel 1 goto fail
%PY% build_data.py "%APK%"
if errorlevel 1 goto fail
echo.
echo Copy the "game" folder from fnaf2_data to ux0:data/fnaf2/ on your Vita.
goto end

:noapk
echo Drag your FNaF 2 APK onto Prepare-Game-Data.cmd (not into this window).
goto end

:nopy
echo Python 3 not found. Install it from https://www.python.org/downloads/
echo and tick "Add python.exe to PATH" during setup.
goto end

:fail
echo.
echo Something went wrong (see the message above).

:end
echo.
pause
