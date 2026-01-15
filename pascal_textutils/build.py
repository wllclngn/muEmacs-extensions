#!/usr/bin/env python3
"""
Pascal Text Utilities Extension - Build Script

Compiles:
  1. textutils.pas -> libtextutils.so (Pascal shared library)
  2. bridge.c + libtextutils.so -> pascal_textutils.so (final extension)
"""

import subprocess
import sys
import os
from pathlib import Path

TARGET = "pascal_textutils.so"
PASCAL_LIB = "libtextutils.so"
# API version from env or default to 4
API_VERSION = os.environ.get("UEMACS_API_VERSION", "4")

# Find μEmacs include path
SCRIPT_DIR = Path(__file__).parent.resolve()
UEMACS_DIR = SCRIPT_DIR.parent.parent.parent / "μEmacs"
INCLUDE_PATH = UEMACS_DIR / "include"

INCLUDE_PATHS = [
    INCLUDE_PATH,
    Path("/usr/local/include"),
    Path("/usr/include"),
]


def find_include() -> Path | None:
    for inc in INCLUDE_PATHS:
        if (inc / "uep" / "extension_api.h").exists():
            return inc
    return None


def run(cmd: list[str], desc: str) -> int:
    print(f"[pascal_textutils] {desc}")
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"FAILED: {result.stderr or result.stdout}", file=sys.stderr)
    return result.returncode


def build() -> int:
    here = Path(__file__).parent.resolve()

    # Step 1: Compile Pascal to shared library
    pas_files = list(here.glob("*.pas"))
    if not pas_files:
        print("[pascal_textutils] No .pas files found", file=sys.stderr)
        return 1

    # Use fpc to compile Pascal library
    pas_cmd = [
        "fpc",
        "-Cg",           # Generate PIC code
        "-O2",           # Optimization
        "-XS",           # Static link RTL
        "-o" + PASCAL_LIB,
        str(pas_files[0])
    ]

    if run(pas_cmd, f"Compiling {pas_files[0].name}") != 0:
        return 1

    # Step 2: Compile C bridge and link with Pascal library
    inc = find_include()
    if not inc:
        print("[pascal_textutils] WARNING: Could not find μEmacs include path", file=sys.stderr)

    c_cmd = ["gcc", "-shared", "-fPIC", "-O2", f"-DUEMACS_API_VERSION_BUILD={API_VERSION}", "-o", TARGET]
    if inc:
        c_cmd.extend(["-I", str(inc)])
    c_cmd.extend([
        "bridge.c",
        "-L.", f"-l:libtextutils.so",
        "-Wl,-rpath,$ORIGIN"
    ])

    if run(c_cmd, "Linking bridge with Pascal library") != 0:
        return 1

    print(f"[pascal_textutils] Built {TARGET}")
    return 0


def clean():
    here = Path(__file__).parent
    for pattern in [TARGET, PASCAL_LIB, "*.o", "*.ppu", "link.res", "textutils.o"]:
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
