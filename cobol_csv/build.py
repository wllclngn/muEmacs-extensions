#!/usr/bin/env python3
"""
COBOL CSV Reader Extension - GnuCOBOL + C Bridge

Note: This extension uses C for CSV parsing with libcob for COBOL runtime support.
The COBOL runtime is initialized to enable future COBOL modules.
"""

import subprocess
import sys
from pathlib import Path

TARGET = "cobol_csv.so"
CFLAGS = ["-O2", "-fPIC", "-Wall", "-std=c23"]


def run(cmd: list[str], desc: str) -> int:
    print(f"[cobol_csv] {desc}")
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"FAILED: {result.stderr or result.stdout}", file=sys.stderr)
    return result.returncode


def build() -> int:
    here = Path(__file__).parent

    # Get COBOL flags from cobc
    try:
        cob_cflags = subprocess.run(
            ["cobc", "--cflags"],
            capture_output=True, text=True
        ).stdout.strip().split()
        cob_libs = subprocess.run(
            ["cobc", "--libs"],
            capture_output=True, text=True
        ).stdout.strip().split()
    except FileNotFoundError:
        print("ERROR: cobc not found. Install gnucobol:", file=sys.stderr)
        print("  sudo pacman -S gnucobol  # Arch", file=sys.stderr)
        print("  sudo apt install gnucobol  # Debian/Ubuntu", file=sys.stderr)
        return 1

    # Compile bridge.c
    compile_cmd = (
        ["gcc"] + CFLAGS + cob_cflags +
        ["-c", "-o", "bridge.o", "bridge.c"]
    )
    if run(compile_cmd, "Compiling bridge.c") != 0:
        return 1

    # Link shared library
    link_cmd = (
        ["gcc", "-shared", "-o", TARGET, "bridge.o"] + cob_libs
    )
    if run(link_cmd, f"Linking {TARGET}") != 0:
        return 1

    print(f"[cobol_csv] Built {TARGET}")
    return 0


def clean():
    here = Path(__file__).parent
    patterns = ["*.o", TARGET]
    for pattern in patterns:
        for f in here.glob(pattern):
            f.unlink()
            print(f"Removed {f.name}")


if __name__ == "__main__":
    import os
    os.chdir(Path(__file__).parent)

    if len(sys.argv) > 1 and sys.argv[1] == "clean":
        clean()
    else:
        sys.exit(build())
