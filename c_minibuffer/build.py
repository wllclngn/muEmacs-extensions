#!/usr/bin/env python3
"""Build script for c_minibuffer extension."""

import subprocess
import sys
import os

# μEmacs source directory for headers
UEMACS_DIR = os.path.expanduser(
    "~/personal/PROGRAMMING/SYSTEM PROGRAMS/LINUX/μEmacs"
)
INCLUDE_DIR = os.path.join(UEMACS_DIR, "include")

def build():
    """Build the c_minibuffer shared library."""

    sources = ["minibuffer.c"]
    output = "c_minibuffer.so"

    cmd = [
        "gcc",
        "-std=c23",
        "-shared",
        "-fPIC",
        "-O2",
        "-Wall",
        "-Wextra",
        f"-I{INCLUDE_DIR}",
        "-o", output,
    ] + sources

    print(f"Building {output}...")
    print(" ".join(cmd))

    result = subprocess.run(cmd, capture_output=True, text=True)

    if result.returncode != 0:
        print("Build failed!")
        print(result.stderr)
        return 1

    print(f"Successfully built {output}")
    return 0

if __name__ == "__main__":
    sys.exit(build())
