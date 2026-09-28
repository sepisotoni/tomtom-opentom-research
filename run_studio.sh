#!/bin/bash
# TomTom Face Studio Launcher Script
# ==================================
# Launches the desktop watch face authoring studio.

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$SCRIPT_DIR"

if command -v python3 &>/dev/null; then
    python3 -m studio.tomtom_face_studio "$@"
else
    echo "Error: python3 is required to launch TomTom Face Studio."
    exit 1
fi
