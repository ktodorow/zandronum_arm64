#!/usr/bin/env python3
"""Exercise actual FRenderState and flat-light submission with Mesa readback.

Compiles complete production gl_renderstate.cpp/header and GLES adapter. Also
compiles unmodified GLFlat::DrawSubsectorLights extracted from production.
Only unrelated configuration/shader-manager/world dependencies are fixtures;
pipeline setters, Apply, cache reset, light-loop ordering and rendering are real.

Linux dependencies match test-gles-renderer.py. Run with --all-versions to check
Mesa ES2 and ES3.2. Desktop compilation uses GLES only as fixture transport;
it checks unchanged desktop preprocessor/default behavior, not desktop GL.
This suite makes no R36S performance or scene-qualification claim.
"""

import argparse
import hashlib
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile


STUBS = {
    "c_cvars.h": """#pragma once
#define Bool bool
#define CVAR(type,name,value,flags) type name=value;
#define EXTERN_CVAR(type,name) extern type name;
""",
    "i_system.h": "void I_FatalError(const char *, ...);\n",
    "gl/system/gl_system.h": """#pragma once
#include "gles_compat.h"
#include <cstdint>
union PalEntry {
 uint32_t d;
 struct { unsigned char b,g,r,a; };
 PalEntry(uint32_t x=0):d(x) {}
 operator uint32_t() const {return d;}
 bool operator!=(const PalEntry &p) const {return d!=p.d;}
};
""",
    "gl/system/gl_interface.h": """#pragma once
struct FakeGL {int shadermodel;}; extern FakeGL gl;
enum {TM_MODULATE=0,TM_MASK=1,CM_DEFAULT=0,CM_FIRSTSPECIALCOLORMAP=32,CM_MAXCOLORMAP=64,VATTR_FOGPARAMS=5};
""",
    "gl/data/gl_data.h": """#pragma once
struct FakeSettings {int lightmode;}; extern FakeSettings glset;
""",
    "gl/system/gl_cvars.h": """#pragma once
extern int gl_fogmode;
extern bool gl_colormap_shader,gl_brightmap_shader,gl_warp_shader,gl_fog_shader;
""",
    "gl/shaders/gl_shader.h": """#pragma once
#include "gl/renderer/gl_renderstate.h"
struct FShader {
 GLuint handle;
 int currentfogenabled,currenttexturemode,fogenabled_index,texturemode_index,camerapos_index;
 int fogcolor_index,glowtopcolor_index,glowbottomcolor_index,lightrange_index,lights_index,dlightcolor_index;
 PalEntry currentfogcolor;
 FStateVec3 currentcamerapos;
};
struct FShaderContainer {FShader *Bind(int,bool,float,bool);};
struct FakeShaderManager {
 FShader *BindEffect(int) {return 0;}
 FShaderContainer *Get(int);
 void SetActiveShader(FShader *shader) {gles::UseProgram(shader?shader->handle:0);}
};
""",
    "gl/renderer/gl_renderer.h": """#pragma once
#include "gl/shaders/gl_shader.h"
struct FakeRenderer {FakeShaderManager *mShaderManager;}; extern FakeRenderer *GLRenderer;
""",
    "gl/renderer/gl_colormap.h": "#pragma once\n",
    "gl/gl_functions.h": "#define OVERRIDE_FOGMODE_IF_NECESSARY\n",
}


def extract_method(source, signature):
    """Keep actual method text intact; no replacement of setters or Apply."""
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    for index in range(opening + 1, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if not depth:
                return source[start:index + 1] + "\n"
    raise RuntimeError("Unterminated production method: " + signature)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="c++")
    parser.add_argument("--gles-version", choices=("2.0", "3.2"), default="2.0")
    parser.add_argument("--all-versions", action="store_true")
    parser.add_argument("--branch", choices=("native", "desktop", "both"), default="both")
    parser.add_argument("--source-root", type=Path, help="Check private source snapshot with these tests")
    parser.add_argument("--output", type=Path, help="Keep source hashes and individual native/desktop logs")
    args = parser.parse_args()
    root = args.source_root.resolve() if args.source_root else Path(__file__).resolve().parents[3]
    versions = ("2.0", "3.2") if args.all_versions else (args.gles_version,)
    with tempfile.TemporaryDirectory(prefix="zandronum-renderstate-test-") as temporary:
        temp = Path(temporary)
        stub = temp / "stubs"
        for name, source in STUBS.items():
            path = stub / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(source)
        flat = root / "src/gl/scene/gl_flats.cpp"
        (temp / "flat-method.inc").write_text(extract_method(flat.read_text(), "void GLFlat::DrawSubsectorLights("))
        initialization = extract_method((root / "src/gl/system/gl_framebuffer.cpp").read_text(),
                                        "void OpenGLFrameBuffer::InitializeState()")
        reset = initialization.index("gl_RenderState.Reset();")
        commit = initialization.index("gl_RenderState.ApplyPipelineState();", reset)
        if not reset < commit < initialization.index("glClear(GL_COLOR_BUFFER_BIT"):
            raise RuntimeError("Framebuffer must reset and commit native pipeline before clear/Begin2D")

        # Use real material source/prefixes from existing shader test generator.
        loader = importlib.util.spec_from_file_location("gles_programs", Path(__file__).with_name("test-gles-renderer.py"))
        programs = importlib.util.module_from_spec(loader)
        loader.loader.exec_module(programs)
        shader_dir = temp / "programs"
        shader_dir.mkdir()
        programs.generate_programs(root, shader_dir, 128)

        sources = [root / name for name in (
            "src/gl/renderer/gl_renderstate.h", "src/gl/renderer/gl_renderstate.cpp",
            "src/gl/scene/gl_flats.cpp", "src/gl/system/gl_framebuffer.cpp",
            "src/gl/system/gles_compat.h", "src/gl/system/gles_compat.cpp",
            "src/gl/shaders/gl_shader.cpp", "wadsrc/static/shaders/glsl/main.vp",
            "wadsrc/static/shaders/glsl/main.fp", "wadsrc/static/shaders/glsl/func_normal.fp")]
        if args.output:
            args.output.mkdir(parents=True, exist_ok=True)
            (args.output / "production-sources.sha256").write_text("".join(
                hashlib.sha256(p.read_bytes()).hexdigest() + "  " + str(p.relative_to(root)) + "\n" for p in sources))

        branches = (True, False) if args.branch == "both" else (args.branch == "native",)
        for native in branches:
            kind = "native" if native else "desktop"
            executable = temp / ("test-renderstate-" + kind)
            command = [args.compiler, "-std=c++11", "-O1", "-g"]
            if native:
                command.append("-DUSE_GLES2")
            command += ["-I" + str(stub), "-I" + str(temp), "-I" + str(root / "src/gl/system"),
                        "-I" + str(root / "src"), str(Path(__file__).with_suffix(".cpp")),
                        str(root / "src/gl/renderer/gl_renderstate.cpp"), str(root / "src/gl/system/gles_compat.cpp"),
                        "-Wl,--wrap=glDrawArrays", "-Wl,--wrap=glDrawElements", "-lEGL", "-lGLESv2", "-ldl", "-o", str(executable)]
            subprocess.run(command, check=True)
            for version in versions:
                environment = dict(os.environ, LIBGL_ALWAYS_SOFTWARE="1", MESA_GLES_VERSION_OVERRIDE=version)
                result = subprocess.run([str(executable), str(shader_dir)], env=environment, text=True,
                                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                print(result.stdout, end="", flush=True)
                if args.output:
                    (args.output / (kind + "-es" + version.replace(".", "") + ".log")).write_text(result.stdout)
                if result.returncode:
                    raise SystemExit(result.returncode)


if __name__ == "__main__":
    main()
