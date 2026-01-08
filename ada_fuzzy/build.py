#!/usr/bin/env python3
"""
Fuzzy File Finder Extension - Ada + C Bridge

Uses gnatbind to generate proper elaboration code (adainit/adafinal).
Without this, Ada's secondary stack is uninitialized and causes crashes.
"""

import subprocess
import sys
from pathlib import Path

TARGET = "ada_fuzzy.so"
CFLAGS = ["-O2", "-fPIC", "-Wall"]
ADAFLAGS = ["-O2", "-fPIC"]


def run(cmd: list[str], desc: str) -> int:
    print(f"[ada_fuzzy] {desc}")
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"FAILED: {result.stderr or result.stdout}", file=sys.stderr)
    return result.returncode


def build() -> int:
    here = Path(__file__).parent

    # 1. Compile bridge.c to bridge.o
    if run(["gcc"] + CFLAGS + ["-c", "-o", "bridge.o", "bridge.c"],
           "Compiling bridge.c") != 0:
        return 1

    # 2. Compile Ada code (fuzzy.adb)
    if run(["gnatmake", "-c"] + ADAFLAGS + ["fuzzy.adb"],
           "Compiling fuzzy.adb") != 0:
        return 1

    # 3. Generate binding/elaboration code (creates b~fuzzy.adb)
    if run(["gnatbind", "-n", "-shared", "fuzzy"],
           "Generating Ada elaboration code") != 0:
        return 1

    # 4. Compile the binding file
    if run(["gcc"] + CFLAGS + ["-c", "b~fuzzy.adb"],
           "Compiling binding code") != 0:
        return 1

    # 5. Link everything together
    if run(["gcc", "-shared", "-o", TARGET,
            "bridge.o", "fuzzy.o", "b~fuzzy.o", "-lgnat"],
           "Linking ada_fuzzy.so") != 0:
        return 1

    print(f"[ada_fuzzy] Built {TARGET}")
    return 0


def clean():
    import glob
    here = Path(__file__).parent
    patterns = ["*.o", "*.ali", "b~fuzzy.adb", "b~fuzzy.ads", TARGET]
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
