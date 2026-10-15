#!/usr/bin/env python3
"""Stage personal PortMaster files from verified ARM64 client/runtime exports."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify_export(directory, parser):
    for line in (directory / "SHA256SUMS").read_text().splitlines():
        expected, name = line.split(maxsplit=1)
        source = (directory / name.strip()).resolve()
        if directory not in source.parents or not source.is_file() or digest(source) != expected:
            parser.error(f"export checksum failed: {directory / name.strip()}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", required=True, type=Path)
    parser.add_argument("--runtime", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    if output == ROOT or ROOT in output.parents:
        parser.error("output must be outside source checkout")
    if output.exists() and any(output.iterdir()):
        parser.error("output must be absent or empty; install managed files over existing package")
    client = args.client.resolve()
    runtime = args.runtime.resolve()
    verify_export(client, parser)
    verify_export(runtime, parser)
    game = output / "zandronum"
    game.mkdir(parents=True)
    packaging = ROOT / "packaging/r36s"
    shutil.copy2(packaging / "Zandronum.sh", output)
    for source in sorted(packaging.iterdir()):
        if source.is_file() and source.name != "Zandronum.sh":
            shutil.copy2(source, game)
    for name in ("zandronum", "zandronum.pk3", "brightmaps.pk3", "skulltag_actors.pk3"):
        shutil.copy2(client / name, game)
    libs = game / "libs.aarch64"
    libs.mkdir()
    shutil.copy2(client / "lib/libSDL-1.2.so.0", libs)
    for name in ("libs.aarch64", "gl4es.aarch64"):
        shutil.copytree(runtime / name, game / name, dirs_exist_ok=True)
    shutil.copytree(runtime / "evidence", game / "evidence/runtime")
    shutil.copytree(client / "evidence", game / "evidence/client")
    for path in (output / "Zandronum.sh", game / "runner.sh", game / "restore-frontend.sh"):
        path.chmod(0o755)
    (game / "package.json").write_text(json.dumps({
        "packaging_commit": subprocess.check_output(
            ["git", "-C", str(ROOT), "rev-parse", "HEAD"], text=True).strip(),
        "packaging_dirty": bool(subprocess.check_output(
            ["git", "-C", str(ROOT), "status", "--porcelain"], text=True).strip()),
        "client_sha256": digest(client / "zandronum"),
        "client_source_provenance": (client / "evidence/source-provenance.txt").read_text(),
        "gl4es_commit": (runtime / "evidence/gl4es-commit.txt").read_text().strip(),
        "scope": "Issue #3 launch; gameplay controls/audio/rendering/performance not qualified",
    }, indent=2) + "\n")
    files = sorted(p for p in output.rglob("*") if p.is_file())
    (game / "managed-files.sha256").write_text("".join(
        f"{digest(path)}  {path.relative_to(output)}\n" for path in files))
    print(f"Staged {len(files)} managed files at {output}; no IWADs, settings or saves included")


if __name__ == "__main__":
    main()
