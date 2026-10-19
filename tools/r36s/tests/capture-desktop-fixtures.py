#!/usr/bin/env python3
"""Capture fixed fixture cameras with desktop Mesa GL, never GL4ES or GLES.

Run inside disposable Debian ARM64 container with matching desktop client/SDL,
licensed IWAD and fixtures mounted read-only. Requires Xvfb, xdotool and Mesa
desktop GL/GLU. Output and copied config must stay outside version control.
Screenshots are visual references, not performance or audio acceptance tests.
Animated materials/models use wall-clock phase; captures are not pixel oracles.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import time
import zipfile


CVARS = {
    "gamma", "vid_brightness", "vid_contrast", "screenblocks", "crosshair",
    "gl_lightmode", "gl_fogmode", "gl_lights", "gl_dynlight_shader",
    "gl_attachedlights", "gl_light_sprites", "gl_light_models", "gl_glow_shader",
    "gl_brightmap_shader", "gl_warp_shader", "gl_colormap_shader", "gl_fog_shader",
    "gl_texture_filter", "gl_texture_filter_anisotropic", "gl_texture_hqresize",
    "gl_usevbo", "gl_use_models", "gl_interpolate_model_frames", "gl_fuzztype",
    "gl_plane_reflection", "r_mirror_recursions", "gl_render_precise",
    "gl_spriteclip", "gl_sclipfactor", "gl_sclipthreshold", "gl_trimsprites",
    "gl_texture_useshaders", "gl_light_ambient", "gl_distfog", "gl_brightfog",
    "gl_lights_size", "gl_lights_intensity", "gl_lights_checkside",
    "vid_aspect", "vid_nowidescreen", "vid_renderer", "vid_vsync", "cl_capfps",
    "vid_fps", "fullscreen", "con_notifytime", "fov", "r_drawtrans", "r_drawfuzz",
}
ERRORS = re.compile(r"Script error|Shader compilation failed|Shader link failed|"
                    r"Framebuffer incomplete|is not a valid voxel file", re.I)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def config_cvars(path):
    values = {}
    for line in path.read_text(errors="replace").splitlines():
        name, separator, value = line.partition("=")
        if separator and name.lower() in CVARS:
            values[name.lower()] = value
    return values


def stop(process):
    if process.poll() is None:
        os.killpg(process.pid, signal.SIGTERM)
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()


def capture(args, item, environment):
    name = item["map"]
    directory = args.output_dir / name
    directory.mkdir()
    config = directory / "client.ini"
    if args.seed_config:
        shutil.copyfile(args.seed_config, config)
        config.chmod(0o600)
    screenshot = directory / (name + ".png")
    log = directory / "stdout.log"
    command = [str(args.engine), "-iwad", str(args.iwad), "-file", str(args.fixture_pk3),
               "-config", str(config), "-savedir", str(directory), "-nosound", "-nojoy",
               "-width", "640", "-height", "480", "+vid_renderer", "1",
               "+fullscreen", "0", "+gl_usevbo", "0", "+cl_capfps", "0",
               "+vid_vsync", "0", "+vid_fps", "0", "+con_notifytime", "0",
               "+bind", "f12", f"screenshot {screenshot}; wait 5; quit", "+map", name]
    with log.open("wb") as output:
        process = subprocess.Popen(command, cwd=directory, env=environment,
                                   stdout=output, stderr=subprocess.STDOUT,
                                   start_new_session=True)
        try:
            deadline = time.monotonic() + args.timeout
            while "GLES-FIXTURE READY" not in log.read_text(errors="replace"):
                if process.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError(f"{name}: client did not reach fixed-camera READY")
                time.sleep(.1)
            text = log.read_text(errors="replace")
            if ERRORS.search(text) or "Native GLES framebuffer:" in text:
                raise RuntimeError(f"{name}: engine error or native GLES context")
            gl = dict(re.findall(r"^(GL_[A-Z_]+): (.+)$", text, flags=re.M))
            if gl.get("GL_VENDOR") != "Mesa" or "OpenGL ES" in gl.get("GL_VERSION", ""):
                raise RuntimeError(f"{name}: expected Mesa desktop GL")
            if gl.get("GL_SHADING_LANGUAGE_VERSION", "") < "1.3":
                raise RuntimeError(f"{name}: desktop shader model4 requires GLSL>=1.3")
            maps = Path(f"/proc/{process.pid}/maps").read_text()
            libraries = sorted({line.split()[-1] for line in maps.splitlines()
                                if "/" in line and ".so" in line})
            if any("gl4es" in path.lower() for path in libraries):
                raise RuntimeError(f"{name}: GL4ES unexpectedly loaded")
            if not any("libGLX_mesa.so" in path for path in libraries):
                raise RuntimeError(f"{name}: actual Mesa GLX library missing")
            # READY is emitted immediately after ACS ChangeCamera, before first
            # subsequent presentation. Allow several frames and notification fade.
            time.sleep(args.settle_seconds)
            windows = subprocess.check_output(["xdotool", "search", "--pid", str(process.pid),
                                               "--name", "Zandronum"],
                                              env=environment, text=True).splitlines()
            if len(windows) != 1:
                raise RuntimeError(f"{name}: expected one client window")
            subprocess.run(["xdotool", "windowfocus", "--sync", windows[0]],
                           env=environment, check=True)
            subprocess.run(["xdotool", "key", "--clearmodifiers", "F12"],
                           env=environment, check=True)
            process.wait(timeout=15)
            if process.returncode or not screenshot.is_file():
                raise RuntimeError(f"{name}: screenshot or clean quit failed")
        finally:
            stop(process)
    result = dict(map=name, camera=item["camera"], gl=gl, libraries=libraries,
                  argv=command, cvars=config_cvars(config), exit_status=process.returncode,
                  screenshot_sha256=digest(screenshot), capture_transport_passed=True)
    (directory / "metadata.json").write_text(json.dumps(result, indent=2) + "\n")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("engine", "iwad", "fixture-pk3", "output-dir"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--seed-config", type=Path)
    parser.add_argument("--map", action="append", choices=[f"GLTEST{i:02}" for i in range(1, 7)],
                        help="capture selected map; repeat as needed, default all six")
    parser.add_argument("--timeout", type=float, default=90)
    parser.add_argument("--settle-seconds", type=float, default=4)
    args = parser.parse_args()
    for field in ("engine", "iwad", "fixture_pk3", "output_dir", "seed_config"):
        if getattr(args, field) is not None:
            setattr(args, field, getattr(args, field).resolve())
    if "gles" in args.engine.name:
        parser.error("requires desktop client executable")
    args.output_dir.mkdir(parents=True, exist_ok=False)
    args.output_dir.chmod(0o700)
    with zipfile.ZipFile(args.fixture_pk3) as archive:
        manifest = json.loads(archive.read("fixtures.json"))
        for name, expected in manifest["assets"].items():
            if hashlib.sha256(archive.read(name)).hexdigest() != expected:
                parser.error(f"fixture asset hash mismatch: {name}")
    read_fd, write_fd = os.pipe()
    with (args.output_dir / "xvfb.log").open("wb") as output:
        xvfb = subprocess.Popen(["Xvfb", "-displayfd", str(write_fd), "-screen", "0",
                                 "640x480x24", "-nolisten", "tcp"],
                                pass_fds=(write_fd,), stdout=output, stderr=subprocess.STDOUT,
                                start_new_session=True)
    os.close(write_fd)
    with os.fdopen(read_fd) as pipe:
        display = pipe.readline().strip()
    if not display:
        stop(xvfb)
        parser.error("Xvfb failed to allocate display")
    environment = os.environ.copy()
    environment.pop("LD_PRELOAD", None)
    environment.update(DISPLAY=":" + display, SDL_VIDEODRIVER="x11", SDL_AUDIODRIVER="dummy",
                       LIBGL_ALWAYS_SOFTWARE="1", LD_LIBRARY_PATH=str(args.engine.parent / "lib"))
    try:
        results = []
        for item in manifest["maps"]:
            if args.map and item["map"] not in args.map:
                continue
            results.append(capture(args, item, environment))
            print(item["map"] + ": desktop Mesa screenshot, clean quit0", flush=True)
    finally:
        stop(xvfb)
    report = dict(format=1, renderer="desktop Mesa GL, SM4 selected from GLSL>=1.3",
                  engine_sha256=digest(args.engine), pk3_sha256=digest(args.engine.parent / "zandronum.pk3"),
                  iwad_sha256=digest(args.iwad), fixture_sha256=digest(args.fixture_pk3),
                  seed_config_sha256=digest(args.seed_config) if args.seed_config else None,
                  environment={key: environment[key] for key in ("SDL_VIDEODRIVER",
                               "LIBGL_ALWAYS_SOFTWARE", "LD_LIBRARY_PATH")},
                  limitation="Animation phase differs; capture transport pass is not visual acceptance.",
                  results=results)
    (args.output_dir / "results.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
