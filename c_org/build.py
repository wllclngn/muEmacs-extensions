#!/usr/bin/env python3
"""
Org-mode Extension - C23 Build Script

Compiles org.c to c_org.so with C23 support for nullptr, etc.
"""

import subprocess
import sys
from pathlib import Path

TARGET = "c_org.so"
CFLAGS = ["-std=c23", "-O2", "-fPIC", "-Wall", "-Wextra"]

# Find μEmacs include path
SCRIPT_DIR = Path(__file__).parent.resolve()
UEMACS_DIR = SCRIPT_DIR.parent.parent.parent / "μEmacs"
INCLUDE_PATH = UEMACS_DIR / "include"

# Fallback include paths
INCLUDE_PATHS = [
    INCLUDE_PATH,
    Path("/usr/local/include"),
    Path("/usr/include"),
]


def find_include() -> Path | None:
    """Find valid include path with uep/extension.h."""
    for inc in INCLUDE_PATHS:
        if (inc / "uep" / "extension.h").exists():
            return inc
    return None


def run(cmd: list[str], desc: str) -> int:
    print(f"[c_org] {desc}")
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"FAILED: {result.stderr or result.stdout}", file=sys.stderr)
    return result.returncode


def build() -> int:
    here = Path(__file__).parent.resolve()
    sources = list(here.glob("*.c"))

    if not sources:
        print("[c_org] No .c files found", file=sys.stderr)
        return 1

    inc = find_include()
    if not inc:
        print("[c_org] WARNING: Could not find μEmacs include path", file=sys.stderr)

    cmd = ["gcc"] + CFLAGS + ["-shared", "-o", TARGET]
    if inc:
        cmd.extend(["-I", str(inc)])
    cmd.extend([str(f) for f in sources])

    if run(cmd, f"Compiling {len(sources)} file(s)") != 0:
        return 1

    print(f"[c_org] Built {TARGET}")
    return 0


def clean():
    here = Path(__file__).parent
    for pattern in [TARGET, "*.o"]:
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
