#!/usr/bin/env python3
"""Build R36S private graphics libraries with ARM client's scoped builder."""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "arm64"))
from build import BUILDER, BUILDKIT, ROOT, run


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    if output == ROOT or ROOT in output.parents or (output.exists() and any(output.iterdir())):
        parser.error("output must be absent or empty, outside source checkout")
    found = subprocess.run(["docker", "buildx", "inspect", BUILDER], capture_output=True)
    if found.returncode:
        run("docker", "buildx", "create", "--name", BUILDER, "--driver", "docker-container",
            "--driver-opt", "image=" + BUILDKIT)
    with tempfile.TemporaryDirectory(prefix="zandronum-r36s-") as temporary:
        source = Path(temporary)
        recipe = source / "tools/r36s"
        recipe.mkdir(parents=True)
        for name in ("Dockerfile", "gl4es-egl-platform-display.patch"):
            shutil.copy2(ROOT / "tools/r36s" / name, recipe / name)
        run("docker", "buildx", "build", "--builder", BUILDER, "--platform", "linux/arm64",
            "--progress", "plain", "--file", str(recipe / "Dockerfile"),
            "--output", "type=local,dest=" + str(output), str(source))
        shutil.copytree(recipe, output / "evidence/recipe")
    (output / "SHA256SUMS").write_text("".join(
        f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.relative_to(output)}\n"
        for p in sorted(output.rglob("*")) if p.is_file()))
    print(f"Exported {output}; cleanup: python3 tools/arm64/build.py cleanup")


if __name__ == "__main__":
    main()
