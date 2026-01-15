#!/usr/bin/env python3
"""
Fortran Git Extension - Hybrid Build Script

Builds the git extension with:
- bridge.c: C bridge for μEmacs API access
- git_ext.f90: Fortran core with all 12 git commands

The Fortran code is the main implementation; C just provides API glue.
"""

import subprocess
import sys
import re
from pathlib import Path

TARGET = "c_git.so"
CFLAGS = ["-O2", "-fPIC", "-Wall"]
FFLAGS = ["-O2", "-fPIC", "-ffree-form"]
SCRIPT_DIR = Path(__file__).parent.resolve()


def get_api_version() -> int:
    """Read API version from system header."""
    for inc in ["/usr/local/include", "/usr/include"]:
        header = Path(inc) / "uep" / "extension_api.h"
        if header.exists():
            content = header.read_text()
            match = re.search(r'#define\s+UEMACS_API_VERSION\s+(\d+)', content)
            if match:
                return int(match.group(1))
    return 4  # Default fallback


def run(cmd: list[str], desc: str) -> int:
    print(f"[fortran_git] {desc}")
    print(f"  $ {' '.join(cmd)}")
    result = subprocess.run(cmd, cwd=SCRIPT_DIR, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"FAILED:\n{result.stderr or result.stdout}", file=sys.stderr)
    return result.returncode


def build() -> int:
    api_version = get_api_version()
    cflags = CFLAGS + [f"-DUEMACS_API_VERSION_BUILD={api_version}"]

    # 1. Compile bridge.c
    if run(["gcc"] + cflags + ["-c", "-o", "bridge.o", "bridge.c"],
           "Compiling bridge.c (C bridge)") != 0:
        return 1

    # 2. Compile git_ext.f90 (Fortran core)
    if run(["gfortran"] + FFLAGS + ["-c", "-o", "git_ext.o", "git_ext.f90"],
           "Compiling git_ext.f90 (Fortran core)") != 0:
        return 1

    # 3. Link everything into shared library
    # Use gfortran as linker to automatically include libgfortran
    if run(["gfortran", "-shared", "-o", TARGET,
            "bridge.o", "git_ext.o"],
           f"Linking {TARGET}") != 0:
        return 1

    # Verify output
    so_path = SCRIPT_DIR / TARGET
    if so_path.exists():
        size = so_path.stat().st_size
        print(f"[fortran_git] Built {TARGET} ({size:,} bytes)")
        print(f"[fortran_git] Fortran core: 12 commands, buffer navigation")
    else:
        print(f"[fortran_git] ERROR: {TARGET} not created", file=sys.stderr)
        return 1

    return 0


def clean():
    patterns = [TARGET, "*.o", "*.mod"]
    for pattern in patterns:
        for f in SCRIPT_DIR.glob(pattern):
            # Keep source files
            if f.suffix not in ['.c', '.f90', '.py', '.md']:
                f.unlink()
                print(f"Removed {f.name}")


if __name__ == "__main__":
    import os
    os.chdir(SCRIPT_DIR)

    if len(sys.argv) > 1 and sys.argv[1] == "clean":
        clean()
    else:
        sys.exit(build())
