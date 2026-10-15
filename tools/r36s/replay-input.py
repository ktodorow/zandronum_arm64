#!/usr/bin/env python3
"""Inspect/replay installed SDL controls through real R36S evdev device.

No controls: read-only inspection. Replay requires packaged game running.
Examples: a dpdown a; --hold .5 leftx=1; --chord back start.
Synthetic replay supplements physical controls/display acceptance.
"""
import argparse
import array
import fcntl
import json
from pathlib import Path
import re
import struct
import subprocess
import time


def inspect(joystick, mapping_file):
    device = Path('/sys/class/input') / joystick.name / 'device'
    name = (device / 'name').read_text().strip()
    events = list(device.glob('event*'))
    if len(events) != 1:
        raise ValueError(f'expected one evdev sibling for {joystick}')
    event = Path('/dev/input') / events[0].name
    lines = [line for line in mapping_file.read_text().splitlines()
             if not line.startswith('#') and len(line.split(',')) > 2
             and line.split(',')[1] == name]
    if len(lines) != 1:
        raise ValueError(f'expected one installed SDL mapping for {name}')
    mapping = dict(field.split(':', 1) for field in lines[0].split(',')[2:] if ':' in field)
    buttons = array.array('H', [0] * 512)
    axes = array.array('B', [0] * 64)
    with joystick.open('rb', buffering=0) as source:
        fcntl.ioctl(source, 0x84006A34, buttons)  # JSIOCGBTNMAP
        fcntl.ioctl(source, 0x80406A32, axes)  # JSIOCGAXMAP
    controls = {}
    with event.open('rb', buffering=0) as source:
        for control, binding in mapping.items():
            match = re.fullmatch(r'([ab])(\d+)', binding)
            if not match:
                continue
            kind, index = match[1], int(match[2])
            if kind == 'b':
                controls[control] = {'type': 1, 'code': buttons[index]}
            else:
                code = axes[index]
                value, low, high, fuzz, flat, resolution = struct.unpack(
                    '6i', fcntl.ioctl(source, 0x80184540 + code, bytes(24)))
                controls[control] = {'type': 3, 'code': code, 'value': value,
                                     'min': low, 'max': high}
    return {'name': name, 'joystick': str(joystick), 'event': str(event),
            'sdl_mapping': lines[0], 'controls': controls}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('controls', nargs='*')
    parser.add_argument('--joystick', type=Path, default=Path('/dev/input/js0'))
    parser.add_argument('--mapping', type=Path, default=Path('/tmp/gamecontrollerdb.txt'))
    parser.add_argument('--hold', type=float, default=0.1)
    parser.add_argument('--gap', type=float, default=0.2)
    parser.add_argument('--chord', action='store_true')
    args = parser.parse_args()
    if not 0 < args.hold <= 10 or not 0 <= args.gap <= 10:
        parser.error('hold must be (0,10] seconds; gap [0,10] seconds')
    info = inspect(args.joystick, args.mapping)
    print(json.dumps(info, indent=2), flush=True)
    if not args.controls:
        return
    game = subprocess.run(['pgrep', '-x', 'zandronum'], text=True,
                          stdout=subprocess.PIPE).stdout.strip()
    if not game.isdigit() or 'zandronum-r36s.service' not in Path(f'/proc/{game}/cgroup').read_text():
        parser.error('replay requires one running client in packaged systemd unit')
    if subprocess.run(['systemctl', 'is-active', '--quiet', 'emulationstation.service']).returncode == 0:
        parser.error('replay requires EmulationStation stopped by packaged runner')
    steps = []
    for request in args.controls:
        name, separator, amount = request.partition('=')
        if name not in info['controls']:
            parser.error(f'unsupported control: {name}')
        control = info['controls'][name]
        if control['type'] == 3:
            value = float(amount) if separator else 0
            if not separator or not -1 <= value <= 1:
                parser.error(f'axis requires normalized [-1,1] value: {name}=value')
            center = (control['min'] + control['max']) // 2
            target = control['max'] if value >= 0 else control['min']
            steps.append((name, 3, control['code'], round(center + abs(value) * (target - center)), center))
        else:
            if separator:
                parser.error(f'button takes no value: {name}')
            steps.append((name, 1, control['code'], 1, 0))

    with Path(info['event']).open('wb', buffering=0) as device:
        def emit(kind, code, value):
            device.write(struct.pack('llHHi', 0, 0, kind, code, value))
            device.write(struct.pack('llHHi', 0, 0, 0, 0, 0))

        groups = [steps] if args.chord else [[step] for step in steps]
        for group in groups:
            try:
                for name, kind, code, value, neutral in group:
                    print(f'Replay {name}: EV={kind} code={code} value={value}', flush=True)
                    emit(kind, code, value)
                time.sleep(args.hold)
            finally:
                for name, kind, code, value, neutral in reversed(group):
                    emit(kind, code, neutral)
            time.sleep(args.gap)


if __name__ == '__main__':
    main()
