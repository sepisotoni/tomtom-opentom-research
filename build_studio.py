"""
TomTom Face Studio - Standalone Executable Build Script
======================================================
Compiles tomtom_face_studio.py into a standalone executable application.
Uses PyInstaller (installing to a local venv if needed).
"""

import sys
import os
import subprocess
import shutil

ROOT_DIR = os.path.dirname(os.path.abspath(__file__))
DIST_DIR = os.path.join(ROOT_DIR, "dist")
BUILD_DIR = os.path.join(ROOT_DIR, "build")
VENV_DIR = os.path.join(ROOT_DIR, ".build_venv")


def build_executable():
    print("=== TomTom Face Studio Build Harness ===")

    # Check if pyinstaller is available
    pyinstaller_cmd = shutil.which("pyinstaller")

    if not pyinstaller_cmd:
        print("[+] PyInstaller not found in global path. Setting up build venv...")
        if not os.path.exists(VENV_DIR):
            subprocess.check_call([sys.executable, "-m", "venv", VENV_DIR])

        pip_cmd = os.path.join(VENV_DIR, "bin", "pip")
        pyinstaller_cmd = os.path.join(VENV_DIR, "bin", "pyinstaller")

        print("[+] Installing PyInstaller and dependencies into venv...")
        subprocess.check_call([pip_cmd, "install", "--quiet", "pyinstaller", "PyQt5", "Pillow"])

    print(f"[+] Using PyInstaller at: {pyinstaller_cmd}")

    cmd = [
        pyinstaller_cmd,
        "--noconfirm",
        "--clean",
        "--name=TomTomFaceStudio",
        "--windowed",
        "--paths=" + ROOT_DIR,
        os.path.join(ROOT_DIR, "studio", "tomtom_face_studio.py")
    ]

    print(f"[+] Running build command: {' '.join(cmd)}")
    subprocess.check_call(cmd, cwd=ROOT_DIR)

    exe_path = os.path.join(DIST_DIR, "TomTomFaceStudio", "TomTomFaceStudio")
    if os.name == "nt" or not os.path.exists(exe_path):
        exe_win = os.path.join(DIST_DIR, "TomTomFaceStudio.exe")
        if os.path.exists(exe_win):
            exe_path = exe_win

    print(f"\n[SUCCESS] Build complete! Desktop Application Executable Path:")
    print(f" -> {exe_path}")
    return exe_path


if __name__ == "__main__":
    build_executable()
