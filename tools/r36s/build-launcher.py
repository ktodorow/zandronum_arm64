#!/usr/bin/env python3
"""Build controller content launcher in pinned, scoped ARM64 Docker builder."""
import argparse
import hashlib
import json
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
    with tempfile.TemporaryDirectory(prefix="zandronum-launcher-") as temporary:
        source = Path(temporary)
        recipe = source / "tools/r36s"
        recipe.mkdir(parents=True)
        shutil.copy2(ROOT / "tools/r36s/Dockerfile.launcher", recipe)
        shutil.copytree(ROOT / "tools/r36s/launcher", recipe / "launcher")
        run("docker", "buildx", "build", "--builder", BUILDER, "--platform", "linux/arm64",
            "--progress", "plain", "--file", str(recipe / "Dockerfile.launcher"),
            "--output", "type=local,dest=" + str(output), str(source))
        shutil.copytree(recipe, output / "evidence/recipe")
    (output / "evidence/source-provenance.json").write_text(json.dumps({
        "commit": subprocess.check_output(["git", "-C", str(ROOT), "rev-parse", "HEAD"], text=True).strip(),
        "dirty": bool(subprocess.check_output(["git", "-C", str(ROOT), "status", "--porcelain"], text=True).strip()),
        "builder_image": BUILDKIT,
    }, indent=2) + "\n")
    (output / "SHA256SUMS").write_text("".join(
        f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.relative_to(output)}\n"
        for p in sorted(output.rglob("*")) if p.is_file()))
    print(f"Exported {output}; cleanup: python3 tools/arm64/build.py cleanup")


if __name__ == "__main__":
    main()
