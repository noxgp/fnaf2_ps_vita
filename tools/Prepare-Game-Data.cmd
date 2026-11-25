@echo off
setlocal
cd /d "%~dp0"
if "%~1"=="" (
  echo Drag your FNaF 2 APK onto this file.
  pause
  exit /b 1
)
set "PY=py -3"
where py >nul 2>nul || set "PY=python"
%PY% --version >nul 2>nul || (
  echo Python 3 not found. Install it from https://www.python.org/downloads/ ^(tick "Add python.exe to PATH"^).
  pause
  exit /b 1
)
%PY% -m pip install -q -r requirements.txt
if errorlevel 1 (pause & exit /b 1)
%PY% build_data.py "%~1"
if errorlevel 1 (pause & exit /b 1)
echo.
echo Copy the "game" folder from fnaf2_data to ux0:data/fnaf2/ on your Vita.
pause
