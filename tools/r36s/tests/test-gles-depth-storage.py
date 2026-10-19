#!/usr/bin/env python3
"""Check production depth splitting against frozen original geometry and allocation cost.

Linux GLES development headers/libraries required; CPU-only, no GL context.
Includes production adapter to inspect private vertices without exposing test API.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="c++")
    parser.add_argument("--sanitizers", action="store_true")
    parser.add_argument("--reference-candidate", action="store_true", help="Negative control: old implementation must fail zero-allocation gate")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    with tempfile.TemporaryDirectory(prefix="zandronum-depth-storage-") as directory:
        temporary = Path(directory)
        (temporary / "i_system.h").write_text("void I_FatalError(const char *, ...);\n")
        executable = temporary / "test-depth-storage"
        command = [args.compiler, "-std=c++11", "-O2", "-g", "-DUSE_GLES2", "-I" + str(temporary),
                   "-I" + str(root / "src/gl/system"), "-I" + str(root / "src"),
                   str(Path(__file__).with_suffix(".cpp")), "-lGLESv2", "-ldl", "-o", str(executable)]
        if args.sanitizers:
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        subprocess.run(command, check=True)
        environment = dict(os.environ)
        if args.sanitizers:
            environment.update(ASAN_OPTIONS="detect_leaks=1", UBSAN_OPTIONS="halt_on_error=1")
        subprocess.run([str(executable)] + (["reference"] if args.reference_candidate else []), env=environment, check=True)


if __name__ == "__main__":
    main()
