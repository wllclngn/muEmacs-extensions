#!/usr/bin/env python3
"""
Haskell Scientific Calculator Extension - GHC + C Bridge

SpeedCrunch-style calculator with:
- Dedicated *calc* buffer with REPL interface
- Syntax highlighting for expressions
- Scientific functions (sin, cos, log, etc.)
- Variables and expression history

Supports two build modes:
1. Pure C mode: Fast, no Haskell dependency (default)
2. Haskell mode: Requires GHC, uses Parsec for parsing (when Calc.hs exists)
"""

import subprocess
import sys
from pathlib import Path

TARGET = "haskell_calc.so"
CFLAGS = ["-O2", "-fPIC", "-Wall", "-std=c23"]


def run(cmd: list[str], desc: str) -> int:
    print(f"[haskell_calc] {desc}")
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"FAILED: {result.stderr or result.stdout}", file=sys.stderr)
    return result.returncode


def build() -> int:
    here = Path(__file__).parent
    has_haskell = (here / "Calc.hs").exists()

    if has_haskell:
        # Haskell mode: compile with GHC
        try:
            ghc_version = subprocess.run(
                ["ghc", "--version"],
                capture_output=True, text=True
            )
            if ghc_version.returncode != 0:
                raise FileNotFoundError
        except FileNotFoundError:
            print("WARNING: ghc not found, building without Haskell support", file=sys.stderr)
            has_haskell = False

    if has_haskell:
        # Compile Haskell module
        if run(["ghc", "-O2", "-fPIC", "-c", "-dynamic", "Calc.hs"],
               "Compiling Calc.hs") != 0:
            return 1

        # Compile bridge.c with Haskell
        if run(["gcc"] + CFLAGS + ["-DUSE_HASKELL", "-c", "-o", "bridge.o", "bridge.c"],
               "Compiling bridge.c (Haskell mode)") != 0:
            return 1

        # Link with GHC
        link_cmd = [
            "ghc", "-O2", "-shared", "-dynamic",
            "-o", TARGET,
            "bridge.o", "Calc.o",
            "-lm"
        ]

        if run(link_cmd, f"Linking {TARGET}") != 0:
            return 1

    else:
        # Pure C mode: just compile bridge.c
        if run(["gcc"] + CFLAGS + ["-c", "-o", "bridge.o", "bridge.c"],
               "Compiling bridge.c (pure C mode)") != 0:
            return 1

        # Link shared library
        if run(["gcc", "-shared", "-o", TARGET, "bridge.o", "-lm"],
               f"Linking {TARGET}") != 0:
            return 1

    print(f"[haskell_calc] Built {TARGET}")
    return 0


def clean():
    here = Path(__file__).parent
    patterns = ["*.o", "*.hi", "*.dyn_o", "*.dyn_hi", TARGET, "Calc_stub.h"]
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
