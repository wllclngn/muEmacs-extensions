#!/usr/bin/env python3
"""Word count script for μEmacs Layer 2."""
import sys

text = sys.stdin.read()
lines = len(text.splitlines())
words = len(text.split())
chars = len(text)

print(f"Lines: {lines}, Words: {words}, Chars: {chars}")
