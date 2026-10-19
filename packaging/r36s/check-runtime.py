#!/usr/bin/env python3
"""Check imports against actual device libraries without creating a display."""
import ctypes
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys

game = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--client", choices=("zandronum", "zandronum-gles"), default="zandronum")
args = parser.parse_args()
result = subprocess.run(["ldd", "-r", str(game / args.client)], text=True,
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
print(result.stdout, flush=True)
if result.returncode or re.search(r"not found|undefined symbol", result.stdout):
    sys.exit("ERROR: client imports do not resolve against selected device libraries")
if args.client == "zandronum-gles":
    if re.search(r"lib(?:GL\.so|GLU\.|GLEW\.|OpenGL\.)|gl4es", result.stdout, re.IGNORECASE):
        sys.exit("ERROR: native GLES client resolves a desktop GL or GL4ES library")
    # These are firmware SONAMEs. Loading validates ABI without opening display.
    for library, symbols in {
        "libEGL.so.1": ("eglGetDisplay", "eglInitialize", "eglCreateContext", "eglSwapBuffers"),
        "libGLESv2.so.2": ("glGetString", "glCreateShader", "glCreateProgram", "glDrawArrays"),
    }.items():
        loaded = ctypes.CDLL(library)
        for symbol in symbols:
            if not hasattr(loaded, symbol):
                sys.exit(f"ERROR: firmware {library} lacks {symbol}")
sdl = ctypes.CDLL("libSDL2-2.0.so.0")
manifest = "sdl2-required-symbols-gles.json" if args.client == "zandronum-gles" else "sdl2-required-symbols.json"
requirements = json.loads((game / manifest).read_text())
missing = [name for name in requirements["sdl2_required_symbols"] if not hasattr(sdl, name)]
if missing:
    sys.exit("ERROR: custom SDL2 lacks: " + ", ".join(missing))
print(f"SDL2: {len(requirements['sdl2_required_symbols'])} required symbols resolved")
for line in Path("/proc/self/maps").read_text().splitlines():
    if "libSDL2" in line:
        print("Actual SDL2:", line.split()[-1])
        break
