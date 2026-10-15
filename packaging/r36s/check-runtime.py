#!/usr/bin/env python3
"""Check imports against actual device libraries without creating a display."""
import ctypes
import json
from pathlib import Path
import re
import subprocess
import sys

game = Path(__file__).resolve().parent
result = subprocess.run(["ldd", "-r", str(game / "zandronum")], text=True,
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
print(result.stdout, flush=True)
if result.returncode or re.search(r"not found|undefined symbol", result.stdout):
    sys.exit("ERROR: client imports do not resolve against selected device libraries")
sdl = ctypes.CDLL("libSDL2-2.0.so.0")
requirements = json.loads((game / "evidence/client/sdl2-required-symbols.json").read_text())
missing = [name for name in requirements["sdl2_required_symbols"] if not hasattr(sdl, name)]
if missing:
    sys.exit("ERROR: custom SDL2 lacks: " + ", ".join(missing))
print(f"SDL2: {len(requirements['sdl2_required_symbols'])} required symbols resolved")
for line in Path("/proc/self/maps").read_text().splitlines():
    if "libSDL2" in line:
        print("Actual SDL2:", line.split()[-1])
        break
