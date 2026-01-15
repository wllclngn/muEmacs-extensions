#!/usr/bin/env python3
"""
NEUROXUS AI Agent Extension - Crystal + C Bridge

Compiles bridge.c first, then builds Crystal linking against it.
Crystal follows require "./agent" to include agent.cr.
"""

import subprocess
import sys
from pathlib import Path

TARGET = "crystal_ai.so"
CFLAGS = ["-O2", "-fPIC", "-Wall"]


def run(cmd: list[str], desc: str) -> int:
    print(f"[crystal_ai] {desc}")
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"FAILED: {result.stderr or result.stdout}", file=sys.stderr)
    return result.returncode


def build() -> int:
    here = Path(__file__).parent.resolve()

    # 1. Compile bridge.c to object file
    if run(["gcc"] + CFLAGS + ["-c", "-o", "bridge.o", "bridge.c"],
           "Compiling bridge.c") != 0:
        return 1

    # 2. Build Crystal with bridge object linked in
    # Quote the path to handle spaces in directory names
    bridge_obj = str(here / "bridge.o")
    if run(["crystal", "build", "--release", "--no-debug",
            f"--link-flags=-shared '{bridge_obj}'",
            "-o", TARGET, "ai_completion.cr"],
           "Building Crystal shared library") != 0:
        return 1

    print(f"[crystal_ai] Built {TARGET}")
    return 0


def clean():
    here = Path(__file__).parent
    patterns = [TARGET, "*.o"]
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
