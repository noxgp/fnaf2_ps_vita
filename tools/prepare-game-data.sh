#!/bin/sh
# macOS / Linux: ./prepare-game-data.sh path/to/fnaf2.apk
# Creates a private Python environment in .venv (no system packages touched), then builds fnaf2_data/game.
set -e
cd "$(dirname "$0")"
if [ -z "$1" ]; then echo "Usage: ./prepare-game-data.sh path/to/fnaf2.apk"; exit 1; fi
APK="$1"; case "$APK" in /*) ;; *) APK="$OLDPWD/$APK";; esac
PY=$(command -v python3 || command -v python || true)
if [ -z "$PY" ]; then echo "Python 3 not found. macOS: brew install python | Linux: sudo apt install python3 python3-venv"; exit 1; fi
if [ ! -x .venv/bin/python ]; then
  "$PY" -m venv .venv || { echo "Could not create a virtual environment. On Debian/Ubuntu: sudo apt install python3-venv"; exit 1; }
fi
.venv/bin/python -m pip install -q --upgrade pip
.venv/bin/python -m pip install -q -r requirements.txt
shift
.venv/bin/python build_data.py "$APK" "$@"
echo
echo "Copy the \"game\" folder from fnaf2_data to ux0:data/fnaf2/ on your Vita."
