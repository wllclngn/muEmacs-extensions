#!/usr/bin/env python3
"""
Concurrent DFS Extension - Go Build Script

Builds the go_dfs extension using CGO to create a shared library.
"""

import subprocess
import sys
import os
from pathlib import Path

TARGET = "go_dfs.so"
SCRIPT_DIR = Path(__file__).parent.resolve()


def run(cmd: list[str], desc: str) -> int:
    print(f"[go_dfs] {desc}")
    print(f"  $ {' '.join(cmd)}")
    result = subprocess.run(cmd, cwd=SCRIPT_DIR, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"FAILED:\n{result.stderr or result.stdout}", file=sys.stderr)
    return result.returncode


def build() -> int:
    # Set CGO flags
    env = os.environ.copy()
    env["CGO_ENABLED"] = "1"

    # Build shared library
    cmd = [
        "go", "build",
        "-buildmode=c-shared",
        "-o", TARGET,
        ".",
    ]

    print(f"[go_dfs] Building {TARGET}...")
    result = subprocess.run(cmd, cwd=SCRIPT_DIR, env=env, capture_output=True, text=True)

    if result.returncode != 0:
        print(f"FAILED:\n{result.stderr or result.stdout}", file=sys.stderr)
        return 1

    print(f"[go_dfs] Built {TARGET}")

    # Verify output
    so_path = SCRIPT_DIR / TARGET
    if so_path.exists():
        size = so_path.stat().st_size
        print(f"[go_dfs] Output: {TARGET} ({size:,} bytes)")
    else:
        print(f"[go_dfs] ERROR: {TARGET} not created", file=sys.stderr)
        return 1

    return 0


def clean():
    for pattern in [TARGET, "*.h", "*.o"]:
        for f in SCRIPT_DIR.glob(pattern):
            if f.name != "bridge.c":  # Keep bridge.c
                f.unlink()
                print(f"Removed {f.name}")


if __name__ == "__main__":
    os.chdir(SCRIPT_DIR)

    if len(sys.argv) > 1 and sys.argv[1] == "clean":
        clean()
    else:
        sys.exit(build())
