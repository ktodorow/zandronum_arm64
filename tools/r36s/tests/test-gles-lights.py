#!/usr/bin/env python3
"""Run actual engine light collection/packing code without a game or GL context.

Extracts definitions, not reimplementations, from production sources. Real
TArray storage is used; actor visibility/plane queries are controlled fixtures.
Both native and unchanged desktop definitions compile. Native additionally runs
under address/undefined sanitizers. Renderer pixels remain a separate suite.
"""

import argparse
from pathlib import Path
import re
import subprocess
import tempfile


def definition(source, marker, semicolon=False):
    start = source.index(marker)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    if semicolon:
        assert source[end] == ";"
        end += 1
    return source[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="c++")
    parser.add_argument("--no-sanitize", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    header = (root / "src/gl/dynlights/gl_dynlight.h").read_text()
    source = (root / "src/gl/dynlights/gl_dynlight1.cpp").read_text()
    context = (root / "src/gl/system/gl_interface.h").read_text()
    override = re.search(r"#define OVERRIDE_LIGHTS_SIZE_IF_NECESSARY(?:[^\n]*\\\n)*[^\n]*", header).group()
    pieces = [definition(context, "struct RenderContext", True),
              definition(header, "struct FDynLightData", True),
              "#ifdef USE_GLES2\n" + definition(header, "inline bool gl_ForceMultipassLights") + "\n#endif",
              override, definition(source, "bool gl_GetLight(")]
    for filename in ("gl_walls.cpp", "gl_flats.cpp"):
        scene = (root / "src/gl/scene" / filename).read_text()
        assert re.search(r"bool light = gl_forcemultipass;\s*#ifdef USE_GLES2\s*"
                         r"light = gl_ForceMultipassLights\(light, gl_dynlight_shader\);\s*#endif", scene), filename
        assert "lightdata.Combine(numlights, gl.MaxLights());" in (
            (root / "src/gl/scene" / ("gl_walls_draw.cpp" if filename == "gl_walls.cpp" else filename)).read_text())
    print("Engine wiring: wall/flat native policy and queried-capacity collection verified", flush=True)
    with tempfile.TemporaryDirectory(prefix="zandronum-light-test-") as directory:
        temporary = Path(directory)
        (temporary / "engine-light-code.h").write_text("\n\n".join(pieces) + "\n")
        for native in (False, True):
            executable = temporary / ("gles" if native else "desktop")
            command = [args.compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror",
                       "-Wno-unused-parameter", "-Wno-unused-variable",  # Existing gl_GetLight locals.
                       "-I" + str(temporary), "-isystem", str(root / "src"),
                       str(Path(__file__).with_suffix(".cpp")), "-o", str(executable)]
            if native:
                command += ["-DUSE_GLES2"]
                if not args.no_sanitize:
                    command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
            subprocess.run(command, check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
