#!/usr/bin/env python3
"""On inspected R36S, replay SELECT+START and require normal packaged exit."""
import array
import fcntl
from pathlib import Path
import struct
import subprocess
import time

game = subprocess.run(["pgrep", "-x", "zandronum"], text=True,
                      stdout=subprocess.PIPE).stdout.strip()
if not game.isdigit() or "zandronum-r36s.service" not in Path(f"/proc/{game}/cgroup").read_text():
    raise SystemExit("FAIL: expected one client inside packaged systemd unit")
with open("/dev/input/js0", "rb", buffering=0) as joystick:
    buttons = array.array("H", [0] * 512)
    fcntl.ioctl(joystick, 0x84006A34, buttons)  # JSIOCGBTNMAP
print(f"Replay SELECT+START: joystick buttons 12+13, Linux keys {buttons[12]}+{buttons[13]}", flush=True)
with open("/dev/input/event2", "wb", buffering=0) as device:
    for value in (1, 0):
        for index in (12, 13):
            device.write(struct.pack("llHHi", 0, 0, 1, buttons[index], value))
            device.write(struct.pack("llHHi", 0, 0, 0, 0, 0))
            time.sleep(0.15)
for attempt in range(40):
    if not Path(f"/proc/{game}").exists():
        break
    time.sleep(0.25)
else:
    raise SystemExit("FAIL: SELECT+START leaves game running")
for attempt in range(40):
    if subprocess.run(["systemctl", "is-active", "--quiet", "emulationstation.service"]).returncode == 0:
        break
    time.sleep(0.25)
else:
    raise SystemExit("FAIL: game exited but frontend did not return")
log = Path("/roms2/ports/zandronum/log.txt").read_text()
if "Game/runner exit status: 0" not in log:
    raise SystemExit("FAIL: chord did not use normal shutdown with exit 0")
if subprocess.run(["pgrep", "-x", "gptokeyb"], stdout=subprocess.DEVNULL).returncode == 0:
    raise SystemExit("FAIL: helper remains after exit")
print("PASS: SELECT+START exits normally (0), restores frontend and removes helper")
