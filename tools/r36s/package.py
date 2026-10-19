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
    parser.add_argument("--gles-client", type=Path, help="native GLES export, alongside legacy --client")
    parser.add_argument("--default-renderer", choices=("gles", "gl4es", "software"), default="gles")
    parser.add_argument("--runtime", required=True, type=Path)
    parser.add_argument("--launcher", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    if output == ROOT or ROOT in output.parents:
        parser.error("output must be outside source checkout")
    if output.exists() and any(output.iterdir()):
        parser.error("output must be absent or empty; install managed files over existing package")
    client = args.client.resolve()
    runtime = args.runtime.resolve()
    launcher = args.launcher.resolve()
    verify_export(client, parser)
    if not (client / "zandronum").is_file():
        parser.error("--client must contain legacy zandronum client")
    verify_export(runtime, parser)
    verify_export(launcher, parser)
    gles_client = args.gles_client.resolve() if args.gles_client else None
    if args.default_renderer == "gles" and not gles_client:
        parser.error("native GLES is default; supply --gles-client or explicitly select --default-renderer gl4es/software")
    if gles_client:
        verify_export(gles_client, parser)
        if not (gles_client / "zandronum-gles").is_file():
            parser.error("--gles-client must contain zandronum-gles")
        abi = json.loads((gles_client / "evidence/client-abi.json").read_text())
        if abi.get("renderer") != "gles" or "libGLESv2.so.2" not in abi.get("needed", []):
            parser.error("--gles-client must be verified native GLES build export")
        for name in ("zandronum.pk3", "brightmaps.pk3", "skulltag_actors.pk3"):
            if digest(client / name) != digest(gles_client / name):
                parser.error(f"legacy and GLES resources differ: {name}; rebuild both from same source")
        if digest(client / "lib/libSDL-1.2.so.0") != digest(gles_client / "lib/libSDL-1.2.so.0"):
            parser.error("legacy and GLES SDL compatibility libraries differ")
    game = output / "zandronum"
    game.mkdir(parents=True)
    packaging = ROOT / "packaging/r36s"
    shutil.copy2(packaging / "Zandronum.sh", output)
    for source in sorted(packaging.iterdir()):
        if source.is_file() and source.name != "Zandronum.sh":
            shutil.copy2(source, game)
    for name in ("zandronum", "zandronum.pk3", "brightmaps.pk3", "skulltag_actors.pk3"):
        shutil.copy2(client / name, game)
    shutil.copy2(launcher / "r36s-launcher", game)
    if gles_client:
        shutil.copy2(gles_client / "zandronum-gles", game)
        shutil.copy2(gles_client / "evidence/sdl2-required-symbols.json", game / "sdl2-required-symbols-gles.json")
        shutil.copy2(ROOT / "docs/r36s-gles-attribution.md", game / "GLES-ATTRIBUTION.md")
    (game / "renderer.txt").write_text(args.default_renderer + "\n")
    libs = game / "libs.aarch64"
    libs.mkdir()
    shutil.copy2(client / "lib/libSDL-1.2.so.0", libs)
    for name in ("libs.aarch64", "gl4es.aarch64"):
        shutil.copytree(runtime / name, game / name, dirs_exist_ok=True)
    # Runtime needs only import manifests. Full evidence stays in host exports.
    shutil.copy2(client / "evidence/sdl2-required-symbols.json", game)
    shutil.copy2(runtime / "evidence/gl4es-LICENSE", game / "gl4es.aarch64/LICENSE")
    for path in (output / "Zandronum.sh", game / "runner.sh", game / "restore-frontend.sh", game / "r36s-launcher"):
        path.chmod(0o755)
    (game / "package.json").write_text(json.dumps({
        "packaging_commit": subprocess.check_output(
            ["git", "-C", str(ROOT), "rev-parse", "HEAD"], text=True).strip(),
        "packaging_dirty": bool(subprocess.check_output(
            ["git", "-C", str(ROOT), "status", "--porcelain"], text=True).strip()),
        "client_sha256": digest(client / "zandronum"),
        "gles_client_sha256": digest(gles_client / "zandronum-gles") if gles_client else None,
        "renderers": ["gl4es", "software"] + (["gles"] if gles_client else []),
        "default_renderer": args.default_renderer,
        "launcher_sha256": digest(launcher / "r36s-launcher"),
        "client_source_provenance": (client / "evidence/source-provenance.txt").read_text(),
        "gles_client_source_provenance": (gles_client / "evidence/source-provenance.txt").read_text() if gles_client else None,
        "gl4es_commit": (runtime / "evidence/gl4es-commit.txt").read_text().strip(),
        "scope": "R36S native GLES default with GL4ES/software fallback; see host exports and repository docs for qualification",
    }, indent=2) + "\n")
    files = sorted(p for p in output.rglob("*") if p.is_file())
    (game / "managed-files.sha256").write_text("".join(
        f"{digest(path)}  {path.relative_to(output)}\n" for path in files))
    print(f"Staged {len(files)} managed files at {output}; no IWADs, settings or saves included")


if __name__ == "__main__":
    main()
