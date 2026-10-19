#!/usr/bin/env python3
"""Load every generated fixture through a real client, including its parsers.

Run on a host/container with a licensed IWAD mounted read-only. Native GLES mode
requires a usable context (Mesa plus SDL_VIDEODRIVER=offscreen works); software
mode supports SDL_VIDEODRIVER=dummy but does not initialize GLDEFS or shaders.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import time
import zipfile


ERRORS = re.compile(
    r'Script error|Invalid state parameter|is not a valid voxel file|'
    r'model not found|Skin .+ not found|Brightmap .+ not found|'
    r'Cannot combine warping|Unable to create texture|'
    r'Shader compilation failed|Shader link failed|Framebuffer incomplete', re.I)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run_map(args, item):
    name = item["map"]
    directory = args.output_dir / name
    directory.mkdir(parents=True, exist_ok=True)
    log = directory / "stdout.log"
    command = [str(args.engine), "-iwad", str(args.iwad), "-file", str(args.fixture_pk3),
               "-config", str(directory / "client.ini"), "-nosound", "-nojoy",
               "+vid_renderer", "1" if args.renderer == "gles" else "0", "+map", name]
    ready, problem = False, ""
    with log.open("wb") as output:
        process = subprocess.Popen(command, cwd=directory, stdout=output, stderr=subprocess.STDOUT,
                                   start_new_session=True)
        deadline = time.monotonic() + args.timeout
        try:
            while time.monotonic() < deadline:
                text = log.read_text(errors="replace")
                error = ERRORS.search(text)
                if error:
                    problem = text[error.start():].splitlines()[0]
                    break
                if "GLES-FIXTURE READY" in text:
                    ready = True
                    break
                if process.poll() is not None:
                    problem = f"client exited {process.returncode} before fixture-ready marker"
                    break
                time.sleep(0.1)
            if not ready and not problem:
                problem = f"fixture-ready marker not reached within {args.timeout:g}s"
        finally:
            # Stop only this launched process group after map initialization.
            # This gate does not claim clean quit/lifecycle qualification.
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGTERM)
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait()
    text = log.read_text(errors="replace")
    missing = [marker for marker in item.get("required_markers", []) if marker not in text]
    if missing:
        ready, problem = False, "ACS runtime marker missing: " + ", ".join(missing)
    native = "Native GLES framebuffer:" in text
    if args.renderer == "gles" and not native:
        ready, problem = False, problem or "native GLES framebuffer marker missing"
    return dict(map=name, passed=ready, native_context=native, error=problem,
                required_markers=item.get("required_markers", []),
                log=str(log), exit_after_stop=process.returncode)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, required=True)
    parser.add_argument("--iwad", type=Path, required=True)
    parser.add_argument("--fixture-pk3", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--renderer", choices=["gles", "software"], default="gles")
    parser.add_argument("--timeout", type=float, default=90)
    args = parser.parse_args()
    for field in ("engine", "iwad", "fixture_pk3", "output_dir"):
        setattr(args, field, getattr(args, field).expanduser().resolve())
    for path in (args.engine, args.iwad, args.fixture_pk3):
        if not path.is_file():
            parser.error(f"input missing: {path}")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.fixture_pk3) as archive:
        manifest = json.loads(archive.read("fixtures.json"))
        for name, expected in manifest["assets"].items():
            if hashlib.sha256(archive.read(name)).hexdigest() != expected:
                parser.error(f"fixture manifest mismatch: {name}")
    results = []
    for item in manifest["maps"]:
        result = run_map(args, item)
        results.append(result)
        print(f'{result["map"]}: {"PASS" if result["passed"] else "FAIL"} {result["error"]}', flush=True)
    report = dict(renderer=args.renderer, engine_sha256=digest(args.engine),
                  iwad_sha256=digest(args.iwad), fixtures_sha256=digest(args.fixture_pk3),
                  results=results, passed=all(item["passed"] for item in results))
    (args.output_dir / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    raise SystemExit(0 if report["passed"] else 1)


if __name__ == "__main__":
    main()
