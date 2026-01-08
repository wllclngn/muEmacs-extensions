#!/usr/bin/env python3
"""
Multiple Cursors Extension - Pascal Library + C Bridge

Two-stage build:
1. Pascal compiled to shared library (libpascal_mc.so)
2. C bridge links against Pascal library with rpath for runtime
"""

import subprocess
import sys
from pathlib import Path

TARGET = "pascal_multicursor.so"
PASCAL_LIB = "libpascal_mc.so"
CFLAGS = ["-O2", "-fPIC", "-Wall"]


def run(cmd: list[str], desc: str) -> int:
    print(f"[pascal_multicursor] {desc}")
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"FAILED: {result.stderr or result.stdout}", file=sys.stderr)
    return result.returncode


def build() -> int:
    # 1. Build Pascal as shared library
    if run(["fpc", "-O2", "-Cg", f"-o{PASCAL_LIB}", "multicursor.pas"],
           "Compiling Pascal to shared library") != 0:
        return 1

    # 2. Build bridge and link against Pascal library
    # -Wl,-rpath,'$ORIGIN' ensures the Pascal lib is found at runtime
    if run(["gcc"] + CFLAGS + [
            "-shared", "-o", TARGET, "bridge.c",
            "-L.", "-lpascal_mc",
            "-Wl,-rpath,$ORIGIN"
           ],
           "Linking bridge with Pascal library") != 0:
        return 1

    print(f"[pascal_multicursor] Built {TARGET}")
    return 0


def clean():
    here = Path(__file__).parent
    patterns = [TARGET, PASCAL_LIB, "*.o", "*.ppu", "link.res", "ppas.sh"]
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
