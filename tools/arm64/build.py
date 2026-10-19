#!/usr/bin/env python3
"""Build/export ARM64 client; remove only this checkout's private BuildKit cache."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[2]
BUILDKIT = "moby/buildkit@sha256:6b59b7df63a8cb9902736f9ddf7fcff8261613d3e7449b8ea8b7537fc399c03a"
BUILDER = "zandronum-arm64-" + hashlib.sha256(str(ROOT).encode()).hexdigest()[:12]


def run(*args, **kwargs):
    return subprocess.run(args, check=True, **kwargs)


def git(*args):
    return subprocess.check_output(["git", "-C", str(ROOT), *args]).decode().strip()


def snapshot(destination):
    paths = subprocess.check_output([
        "git", "-C", str(ROOT), "ls-files", "--cached", "--others",
        "--exclude-standard", "-z",
    ]).decode().split("\0")
    for name in sorted(set(paths) - {""}):
        # Scratch may contain private access notes or upstream research clones.
        if name.startswith(".scratch/"):
            continue
        source = ROOT / name
        if not source.is_file():
            continue
        target = destination / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
    commit = git("rev-parse", "HEAD")
    dirty = bool(git("status", "--porcelain"))
    (destination / "source-provenance.txt").write_text(
        f"git_commit={commit}\ngit_time={git('show', '-s', '--format=%cI', 'HEAD')}\n"
        f"git_dirty={int(dirty)}\n"
    )
    manifest = {}
    for path in sorted(destination.rglob("*")):
        if path.is_file():
            manifest[str(path.relative_to(destination))] = hashlib.sha256(path.read_bytes()).hexdigest()
    (destination / "build-source.json").write_text(json.dumps({
        "git_commit": commit, "dirty": dirty, "files_sha256": manifest,
        "builder_image": BUILDKIT,
    }, indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["build", "cleanup", "name"])
    parser.add_argument("--output", type=Path)
    parser.add_argument("--jobs", type=int, default=4)
    graphics = parser.add_mutually_exclusive_group()
    graphics.add_argument("--gles", dest="gles", action="store_true", default=True,
                          help="build native GLES client as zandronum-gles (default)")
    graphics.add_argument("--legacy-gl", dest="gles", action="store_false",
                          help="build desktop GL client for GL4ES/software fallback")
    args = parser.parse_args()
    if args.action == "name":
        print(BUILDER)
        return
    if args.action == "cleanup":
        # buildx rm removes only this builder's container and state volume.
        found = subprocess.run(["docker", "buildx", "inspect", BUILDER], capture_output=True)
        if found.returncode == 0:
            run("docker", "buildx", "rm", BUILDER)
        return
    if args.output is None or args.jobs < 1:
        parser.error("build requires --output and positive --jobs")
    output = args.output.resolve()
    if output == ROOT or ROOT in output.parents:
        parser.error("export outside source checkout (for example ../zandronum-artifacts/run-1)")
    if output.exists() and any(output.iterdir()):
        parser.error("output must be absent or empty")
    found = subprocess.run(["docker", "buildx", "inspect", BUILDER], capture_output=True)
    if found.returncode:
        run("docker", "buildx", "create", "--name", BUILDER, "--driver", "docker-container",
            "--driver-opt", "image=" + BUILDKIT)
    with tempfile.TemporaryDirectory(prefix="zandronum-arm64-") as temporary:
        source = Path(temporary) / "source"
        source.mkdir()
        snapshot(source)
        run("docker", "buildx", "build", "--builder", BUILDER,
            "--platform", "linux/arm64", "--progress", "plain",
            "--build-arg", f"JOBS={args.jobs}",
            "--build-arg", "GLES=" + ("ON" if args.gles else "OFF"),
            "--file", str(source / "tools/arm64/Dockerfile"),
            "--output", "type=local,dest=" + str(output), str(source))
        # Export exact build input beside binary, including uncommitted edits.
        with tarfile.open(output / "source.tar.gz", "w:gz") as archive:
            archive.add(source, arcname="zandronum-arm64")
        digest = hashlib.sha256((output / "source.tar.gz").read_bytes()).hexdigest()
        with (output / "SHA256SUMS").open("a") as checksums:
            checksums.write(f"{digest}  source.tar.gz\n")
    print(f"Exported ARM64 client to {output}; cleanup: python3 tools/arm64/build.py cleanup")


if __name__ == "__main__":
    main()
