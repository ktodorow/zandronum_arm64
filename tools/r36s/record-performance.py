#!/usr/bin/env python3
"""On inspected R36S, time already-running diagnostic client. Never launches it."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import time


def diagnostic_environment(raw):
    # Keep process environment private; record only diagnostic profiler knobs.
    selected = dict.fromkeys(("ZANDRONUM_DIAG_GL_FINISH", "ZANDRONUM_DIAG_EGL_INTERVAL",
                              "ZANDRONUM_DIAG_GL_STATE"))
    for field in raw.split(b"\0"):
        key, separator, value = field.partition(b"=")
        name = key.decode("ascii", errors="replace")
        if separator and name in selected and selected[name] is None:
            selected[name] = value.decode("utf-8", errors="replace")
    return selected


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("name", help="unique scene/run name")
    parser.add_argument("--duration", type=int, default=90)
    parser.add_argument("--client", choices=("zandronum", "zandronum-gles"), default="zandronum")
    parser.add_argument("--output-dir", type=Path, default=Path("/roms2/ports/zandronum/logs/issue7"))
    args = parser.parse_args()
    if not args.name.replace("-", "").isalnum() or not args.name.isascii():
        parser.error("name must contain ASCII letters/digits/hyphens")
    if not 10 <= args.duration <= 600:
        parser.error("duration must be 10..600 seconds")
    output = args.output_dir
    output.mkdir(parents=True, exist_ok=True)
    csv_path = output / (args.name + ".csv")
    metadata_path = output / (args.name + "-metadata.json")
    if csv_path.exists() or metadata_path.exists():
        parser.error("run already exists; choose another name")
    processes = subprocess.check_output(["pgrep", "-x", args.client], text=True).split()
    if len(processes) != 1:
        parser.error("expected exactly one running client")
    pid = int(processes[0])
    proc = Path("/proc") / str(pid)
    maps = (proc / "maps").read_text()
    recorder = Path("/tmp/zandronum-frame-recorder.so")
    # Without this library, SIGUSR1 would terminate client instead of recording.
    if not any(line.endswith(" " + str(recorder)) for line in maps.splitlines()):
        parser.error("diagnostic recorder is not loaded; no signal sent")
    if not any("/zandronum-r36s.service" in line for line in (proc / "cgroup").read_text().splitlines()):
        parser.error("client must run through detached PortMaster unit")
    if subprocess.run(["systemctl", "is-active", "--quiet", "emulationstation.service"]).returncode == 0:
        parser.error("EmulationStation must be stopped by packaged runner")
    if Path("/sys/class/graphics/fb0/virtual_size").read_text().strip() != "640,480":
        parser.error("expected native 640x480 framebuffer")
    source = Path("/tmp/zandronum-frames.csv")
    source.unlink(missing_ok=True)
    sensors = {
        "cpu_khz": "/sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq",
        "cpu_max_khz": "/sys/devices/system/cpu/cpufreq/policy0/scaling_max_freq",
        "governor": "/sys/devices/system/cpu/cpufreq/policy0/scaling_governor",
        "gpu_hz": "/sys/class/devfreq/ff400000.gpu/cur_freq",
        "gpu_load": "/sys/class/devfreq/ff400000.gpu/load",
        "temperature_mC": "/sys/class/thermal/thermal_zone0/temp",
        "gpu_temperature_mC": "/sys/class/thermal/thermal_zone1/temp",
        "cpu_cooling_state": "/sys/class/thermal/cooling_device0/cur_state",
    }
    metadata = {
        "name": args.name, "requested_duration_s": args.duration, "pid": pid,
        "client": args.client,
        "client_sha256": hashlib.sha256((proc / "exe").read_bytes()).hexdigest(),
        "utc_start": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "argv": (proc / "cmdline").read_bytes().decode().rstrip("\0").split("\0"),
        "diagnostic_environment": diagnostic_environment((proc / "environ").read_bytes()),
        "maps": maps,
        "recorder_sha256": hashlib.sha256(recorder.read_bytes()).hexdigest(),
        "samples": [],
    }
    start = time.monotonic()
    os.kill(pid, signal.SIGUSR1)
    try:
        while True:
            now = time.monotonic()
            metadata["samples"].append({
                "elapsed_s": round(now - start, 3),
                **{key: Path(path).read_text().strip() for key, path in sensors.items()},
            })
            remaining = args.duration - (now - start)
            if remaining <= 0:
                break
            time.sleep(min(5, remaining))
    finally:
        os.kill(pid, signal.SIGUSR2)
    for attempt in range(100):
        time.sleep(0.1)
        if source.exists() and source.stat().st_size > 100:
            break
    else:
        raise RuntimeError("no frame flush; run rejected")
    data = source.read_bytes()
    metadata["utc_end"] = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
    metadata["csv_sha256"] = hashlib.sha256(data).hexdigest()
    csv_path.write_bytes(data)
    metadata_path.write_text(json.dumps(metadata, indent=2) + "\n")
    print(csv_path, metadata["csv_sha256"], flush=True)


if __name__ == "__main__":
    main()
