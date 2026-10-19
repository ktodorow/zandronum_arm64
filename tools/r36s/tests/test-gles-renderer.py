#!/usr/bin/env python3
"""Compile the real adapter and test it using a surfaceless Mesa GLES context.

Debian dependencies: g++ python3 libegl1-mesa-dev libgles2-mesa-dev libgl-dev
libgl1-mesa-dri. Optional glslang-tools adds an independent shader check.
Run from a Linux checkout/container: python3 tools/r36s/tests/test-gles-renderer.py
This validates rendering semantics; it does not qualify R36S speed or scenes.
"""

import argparse
import ast
import csv
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import zipfile


def cpp_strings(text):
    return "".join(ast.literal_eval(s) for s in re.findall(r'"(?:\\.|[^"\\])*"', text))


def generate_programs(root, output, max_lights, fixture_shader=None):
    source = (root / "src/gl/shaders/gl_shader.cpp").read_text()
    prefixes = {}
    for name in ("GLESVertexPrefix", "GLESFragmentPrefix", "GLESFragmentSuffix"):
        declaration = re.search(r"static const char \*" + name + r"\s*=\s*(.*?);\n", source, re.S)
        if declaration is None:
            raise RuntimeError("Missing shader declaration: " + name)
        prefixes[name] = cpp_strings(declaration.group(1))
    variants = re.search(r"const char \* shaderdefines\[\] = \{(.*?)\n\s*\};", source, re.S)
    defines = [cpp_strings(line) for line in variants.group(1).splitlines() if '"' in line]
    if len(defines) != 16:
        raise RuntimeError("Expected 16 material variants")
    defaults = re.search(r"static const FDefaultShader defaultshaders\[\]\s*=\s*\{(.*?)\n\};", source, re.S)
    functions = re.findall(r'"(shaders/glsl/[^"]+\.fp)"', defaults.group(1))
    assets = root / "wadsrc/static"
    programs = []

    def write(name, definitions, fragment, function=None, function_source=None):
        vertex_text = (assets / "shaders/glsl/main.vp").read_text()
        fragment_text = (assets / fragment).read_text()
        if function:
            fragment_text += "\n" + (assets / function).read_text()
        if function_source:
            fragment_text += "\n" + function_source
        (output / (name + ".vert")).write_text(prefixes["GLESVertexPrefix"] + definitions + vertex_text)
        (output / (name + ".frag")).write_text(prefixes["GLESFragmentPrefix"] + definitions + fragment_text + prefixes["GLESFragmentSuffix"])
        programs.append(name)

    for function in functions:
        name = Path(function).stem.removeprefix("func_")
        for variant, definition in enumerate(defines):
            if variant & 4:
                definition = "#define GLES_LIGHT_ARRAY_SIZE %d\n" % (max_lights * 2) + definition
            write(f"{name}-{variant}", definition, "shaders/glsl/main.fp", function)
        write(f"{name}-16", "#define NO_FOG\n#define NO_GLOW\n", "shaders/glsl/main_colormap.fp", function)
        write(f"{name}-17", "#define NO_GLOW\n", "shaders/glsl/main_foglayer.fp", function)
    write("fogboundary", "#define NO_GLOW\n", "shaders/glsl/fogboundary.fp")
    write("spheremap", "#define NO_GLOW\n#define NO_DESATURATE\n#define SPHEREMAP\n#define SPHEREMAP_0\n", "shaders/glsl/main.fp", "shaders/glsl/func_normal.fp")
    if len(programs) != 218:
        raise RuntimeError("Expected 218 built-in material/effect programs")
    for variant in (0, 4, 6):
        definition = defines[variant]
        if variant & 4:
            definition = "#define GLES_LIGHT_ARRAY_SIZE %d\n" % (max_lights * 2) + definition
        name = "normal-opaque" if variant == 0 else f"normal-{variant}-opaque"
        write(name, definition + ("#undef GLES_LIGHT_ARRAY_SIZE\n#define GLES_LIGHT_ARRAY_SIZE %d\n" % min(16,max_lights*2) if variant == 4 else "") + "#define GLES_OMIT_CLIP_ALPHA\n", "shaders/glsl/main.fp", "shaders/glsl/func_normal.fp")
    if fixture_shader:
        for variant, definition in enumerate(defines):
            if variant & 4:
                definition = "#define GLES_LIGHT_ARRAY_SIZE %d\n" % (max_lights * 2) + definition
            write(f"fixture-{variant}", definition, "shaders/glsl/main.fp", function_source=fixture_shader)
        write("fixture-16", "#define NO_FOG\n#define NO_GLOW\n", "shaders/glsl/main_colormap.fp", function_source=fixture_shader)
        write("fixture-17", "#define NO_GLOW\n", "shaders/glsl/main_foglayer.fp", function_source=fixture_shader)
    (output / "programs.txt").write_text("\n".join(programs) + "\n")
    return programs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--max-lights", type=int, default=128)
    parser.add_argument("--generate-only", type=Path, metavar="DIRECTORY")
    parser.add_argument("--compiler", default="c++")
    parser.add_argument("--adapter-source", type=Path, help="Alternate real adapter source for regression negative controls")
    parser.add_argument("--sanitizers", action="store_true", help="Enable ASan/UBSan in adapter and harness")
    parser.add_argument("--gles-version", choices=("2.0", "3.0", "3.1", "3.2"), default="2.0", help="Mesa context version; checks both OES and core binary cache APIs")
    parser.add_argument("--glslang", action="store_true", help="Also validate using glslangValidator")
    parser.add_argument("--fixture-pk3", type=Path, help="Also check all 18 variants of shaders/fixture.fp in generated fixture PK3")
    parser.add_argument("--profiler-smoke", action="store_true", help="Check optional state histogram with real adapter/Mesa and chained frame recorder")
    args = parser.parse_args()
    if not 1 <= args.max_lights <= 128:
        parser.error("--max-lights must be 1..128")
    root = Path(__file__).resolve().parents[3]
    with tempfile.TemporaryDirectory(prefix="zandronum-gles-test-") as temp:
        temporary = Path(temp)
        output = args.generate_only or temporary / "programs"
        output.mkdir(parents=True, exist_ok=True)
        fixture_shader = None
        if args.fixture_pk3:
            with zipfile.ZipFile(args.fixture_pk3) as fixture:
                fixture_shader = fixture.read("shaders/fixture.fp").decode("utf-8")
        programs = generate_programs(root, output, args.max_lights, fixture_shader)
        if args.glslang:
            validator = shutil.which("glslangValidator")
            if not validator:
                parser.error("glslangValidator not installed")
            for name in programs:
                subprocess.run([validator, "--quiet", "-l", str(output / (name + ".vert")), str(output / (name + ".frag"))], check=True)
            print("Independent validator programs linked:", len(programs), flush=True)
        if args.generate_only:
            print("Generated", len(programs), "programs in", output)
            return
        stub = temporary / "i_system.h"
        stub.write_text("void I_FatalError(const char *, ...);\n")
        executable = temporary / "test-gles-renderer"
        subprocess.run([args.compiler, "-std=c++11", "-DUSE_GLES2", "-I" + str(temporary),
                        "-I" + str(root / "src/gl/system"), "-I" + str(root / "src"), str(Path(__file__).with_suffix(".cpp")),
                        str(args.adapter_source or root / "src/gl/system/gles_compat.cpp"),
                        "-Wl,--wrap=glUniform4fv", "-Wl,--wrap=glBufferData", "-Wl,--wrap=glBufferSubData", "-Wl,--wrap=glDrawArrays", "-Wl,--wrap=glGetString", "-Wl,--wrap=chmod",
                        "-Wl,--wrap=glEnableVertexAttribArray", "-Wl,--wrap=glDisableVertexAttribArray", "-Wl,--wrap=glVertexAttribPointer",
                        "-lEGL", "-lGLESv2", "-ldl", "-o", str(executable)]
                       + (["-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if args.sanitizers else []), check=True)
        environment = dict(os.environ, MESA_GLES_VERSION_OVERRIDE=args.gles_version, LIBGL_ALWAYS_SOFTWARE="1")
        if args.profiler_smoke:
            libraries = []
            stub_swap = temporary / "stub-swap.c"
            stub_swap.write_text("void SDL_GL_SwapBuffers(void) {}\n")
            for source in (root / "tools/r36s/gles-call-profiler.c", root / "tools/r36s/frame-recorder.c", stub_swap):
                library = temporary / (source.stem + ".so")
                subprocess.run(["cc", "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra", "-Werror", "-O2", "-shared", "-fPIC", str(source), "-o", str(library), "-ldl"], check=True)
                libraries.append(str(library))
            environment.update(LD_PRELOAD=":".join(libraries), ZANDRONUM_DIAG_GL_STATE="1", ZANDRONUM_TEST_HISTOGRAM="1")
            for name in ("ZANDRONUM_DIAG_GL_FINISH", "ZANDRONUM_DIAG_EGL_INTERVAL"):
                environment.pop(name, None)
            for path in ("/tmp/zandronum-gles-calls.csv", "/tmp/zandronum-gles-states.csv", "/tmp/zandronum-frames.csv"):
                Path(path).unlink(missing_ok=True)
        subprocess.run([str(executable), str(output)], env=environment, check=True)
        if args.profiler_smoke:
            with open("/tmp/zandronum-gles-calls.csv") as file:
                calls = list(csv.DictReader(file))
            with open("/tmp/zandronum-gles-states.csv") as file:
                states = list(csv.DictReader(file))
            with open("/tmp/zandronum-frames.csv") as file:
                frames = list(csv.DictReader(file))
            assert len(calls) == len(frames) == 3, (calls, frames)
            assert len(states) == 3, states
            assert sum(int(s["draws"]) for s in states) == sum(int(c["draw_arrays_calls"]) + int(c["draw_elements_calls"]) for c in calls) == 9
            assert sum(int(s["submitted_vertices_or_indices"]) for s in states) == sum(int(c["draw_arrays_vertices"]) + int(c["draw_elements_indices"]) for c in calls) == 45
            actual = {(s["material"], int(s["alpha_test"]), int(s["alpha_func"]), float(s["alpha_threshold"]), int(s["clip_mask"]), int(s["fog_mode"]), int(s["texture_enabled"]), int(s["texture_mode"]), int(s["burn"])): (int(s["draws"]), int(s["submitted_vertices_or_indices"])) for s in states}
            assert actual == {
                ("basic2D-solid", 0, 0, 0, 0, 0, 0, 0, 0): (3, 18),
                ('normal, "smoke"', 1, 516, 0.5, 32, -1, 1, 2, 0): (3, 18),
                ("basic2D", 1, 519, 0.25, 0, 0, 1, 4, 1): (3, 9),
            }, actual
            assert all(s["primitive"] == "4" and s["overflow"] == "0" for s in states)
            assert next(s for s in states if s["material"] == 'normal, "smoke"')["defines"] == "#define FIRST #define SECOND "
            print("State histogram: 3 exact frame boundaries, 9 draws / 45 submitted vertices; alpha/clip/fog/texture/burn/labels/CSV escaping pass", flush=True)


if __name__ == "__main__":
    main()
