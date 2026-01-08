#!/usr/bin/env python3
"""
Project Management Extension - Haskell + C Bridge

Builds with GHC's runtime system properly linked.
Uses -flink-rts for static RTS linking and rpath for dynamic libs.
"""

import subprocess
import sys
from pathlib import Path

TARGET = "haskell_project.so"
CFLAGS = ["-O2", "-fPIC", "-Wall"]


def run(cmd: list[str], desc: str) -> int:
    print(f"[haskell_project] {desc}")
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"FAILED: {result.stderr or result.stdout}", file=sys.stderr)
    return result.returncode


def get_ghc_libdir() -> str:
    """Get GHC's library directory for rpath."""
    result = subprocess.run(["ghc", "--print-libdir"],
                           capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError("Failed to get GHC libdir")
    return result.stdout.strip()


def build() -> int:
    # Get GHC paths
    try:
        ghc_libdir = get_ghc_libdir()
    except RuntimeError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 1

    # Find the GHC version-specific directory for rpath
    # Pattern: x86_64-linux-ghc-X.Y.Z
    ghc_arch_dirs = list(Path(ghc_libdir).glob("x86_64-linux-ghc-*"))
    if not ghc_arch_dirs:
        print("ERROR: Could not find GHC arch directory", file=sys.stderr)
        return 1
    ghc_rpath = str(ghc_arch_dirs[0])

    # 1. Compile bridge.c
    if run(["gcc"] + CFLAGS + ["-c", "-o", "bridge.o", "bridge.c"],
           "Compiling bridge.c") != 0:
        return 1

    # 2. Build with GHC
    # -shared: build shared library
    # -dynamic: use dynamic linking
    # -fPIC: position independent code
    # -flink-rts: link RTS into the shared library
    # -optl-Wl,-rpath: set runtime library path
    if run(["ghc", "-O2", "-shared", "-dynamic", "-fPIC", "-flink-rts",
            f"-optl-Wl,-rpath,{ghc_rpath}",
            "-o", TARGET, "Project.hs", "bridge.o"],
           "Building Haskell shared library") != 0:
        return 1

    print(f"[haskell_project] Built {TARGET}")
    return 0


def clean():
    here = Path(__file__).parent
    patterns = [TARGET, "*.o", "*.hi", "*_stub.h"]
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
