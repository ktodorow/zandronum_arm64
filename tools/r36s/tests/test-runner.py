#!/usr/bin/env python3
"""Exercise packaged runner defaults/migrations with isolated PortMaster services.

Run on Linux; actual launcher controller events have their own SDL UI test.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]


def executable(path, text):
    path.write_text(text)
    path.chmod(0o755)


with tempfile.TemporaryDirectory(prefix="r36s-runner-") as temporary:
    root = Path(temporary)
    game, commands = root / "game", root / "bin"
    shutil.copytree(ROOT / "packaging/r36s", game)
    commands.mkdir()
    control = root / "xdg/PortMaster"
    control.mkdir(parents=True)
    (control / "control.txt").write_text('''
DEVICE_ARCH=aarch64
CFW_NAME=fixture
directory=roms2
get_controls() { :; }
pm_finish() { pm_gptokeyb_finish; }
pm_platform_helper() { :; }
pm_show_error() { echo "$*"; }
''')
    (control / "libgl_default.txt").write_text('touch "$gamedir/legacy-hook-used"\n')
    executable(control / "gptokeyb", "#!/bin/sh\nexit 0\n")
    executable(commands / "sudo", '#!/bin/sh\n[ "$1" = -n ] && shift\nexec "$@"\n')
    executable(commands / "systemctl", '''#!/bin/sh
case "$1" in
stop) echo stopped > "$FRONTEND_STATE";;
is-active) test "$(cat "$FRONTEND_STATE")" = active;;
*) exit 0;;
esac
''')
    for name in ("pgrep", "sleep"):
        executable(commands / name, "#!/bin/sh\nexit " + ("1" if name == "pgrep" else "0") + "\n")
    executable(game / "restore-frontend.sh", '#!/bin/sh\necho active > "$FRONTEND_STATE"\n')
    (game / "check-runtime.py").write_text("print('fixture runtime check')\n")
    executable(game / "zandronum", f"#!{sys.executable}\n" + '''
import json, os, pathlib, sys
args = sys.argv[1:]
config = pathlib.Path(args[args.index('-config')+1])
text = config.read_text()
for i, arg in enumerate(args):
    if arg == '-exec':
        for line in pathlib.Path(args[i+1]).read_text().splitlines():
            parts = line.split()
            if not parts or parts[0] in ('//', 'echo', 'archivecvar'):
                continue
            if parts[0] == 'set':
                parts.pop(0)
            if len(parts) == 2:
                text += '\\n' + parts[0] + '=' + parts[1]
if '-exec' in args:
    config.write_text(text+'\\n')
pathlib.Path(os.environ['TRACE']).write_text(json.dumps({
    'client': pathlib.Path(sys.argv[0]).name, 'args': args,
    'driver': os.environ.get('SDL_VIDEO_GL_DRIVER'),
    'preload': os.environ.get('LD_PRELOAD'),
}))
''')
    shutil.copy2(game / "zandronum", game / "zandronum-gles")
    (game / "config").mkdir()
    base = game / "config/zandronum.ini"
    base.write_text("[GlobalSettings]\nr36s_controls_profile=1\nr36s_audio_profile=2\nowner_setting=77\n")
    base_before = base.read_bytes()
    iwad = root / "doom2.wad"
    iwad.write_text("IWAD fixture")
    state = root / "frontend"
    env = dict(os.environ, PATH=str(commands)+":"+os.environ['PATH'],
               XDG_DATA_HOME=str(root / "xdg"), HOME=str(root), FRONTEND_STATE=str(state),
               TRACE=str(root / "trace.json"), ZANDRONUM_IWAD=str(iwad), ZANDRONUM_LAUNCHER="0")
    env.pop("ZANDRONUM_RENDERER", None)
    env.pop("LD_PRELOAD", None)

    def launch(**changes):
        state.write_text("active\n")
        run = subprocess.run(["bash", str(game / "runner.sh")], env=dict(env, **changes),
                             text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=15)
        assert run.returncode == 0, run.stdout
        assert state.read_text().strip() == "active", run.stdout
        return json.loads((root / "trace.json").read_text())

    native = launch()
    assert native['client'] == 'zandronum-gles', native
    assert native['driver'] == '/usr/lib/aarch64-linux-gnu/libGLESv2.so.2'
    assert not native['preload'] and not (game / "legacy-hook-used").exists()
    config = Path(native['args'][native['args'].index('-config')+1])
    assert config == game / "config/gles/zandronum.ini", native
    assert 'owner_setting=77' in config.read_text()
    for setting in ('gl_dynlight_shader=1', 'vid_maxfps=0', 'cl_capfps=0', 'vid_vsync=0', 'r36s_gles_profile=1'):
        assert setting in config.read_text(), setting
    assert Path(native['args'][native['args'].index('-savedir')+1]) == game / "saves/gles"
    assert config.with_name("zandronum.before-gles-v1.ini").read_bytes() == base_before
    assert base.read_bytes() == base_before
    config.write_text(config.read_text().replace('gl_dynlight_shader=1', 'gl_dynlight_shader=0'))
    before = config.read_bytes()
    again = launch()
    assert '-exec' not in again['args'] and config.read_bytes() == before
    # Real menu's NUL handoff selects a separate native config/save set for add-ons.
    executable(game / "r36s-launcher", f"#!{sys.executable}\n" + '''
import os, pathlib, sys
args = sys.argv[1:]
pathlib.Path(args[args.index('--output')+1]).write_bytes(
    b'gles\\0' + os.environ['FIXTURE_IWAD'].encode() + b'\\0set-0123456789abcdef\\0')
''')
    profile = launch(ZANDRONUM_LAUNCHER="1", ZANDRONUM_IWAD="", FIXTURE_IWAD=str(iwad))
    assert Path(profile['args'][profile['args'].index('-config')+1]) == game / "config/profiles/set-0123456789abcdef/gles/zandronum.ini"
    assert Path(profile['args'][profile['args'].index('-savedir')+1]) == game / "saves/profiles/set-0123456789abcdef/gles"
    legacy = launch(ZANDRONUM_RENDERER="gl4es")
    assert legacy['client'] == 'zandronum' and (game / "legacy-hook-used").exists()
    assert base.read_bytes() == base_before
    assert Path(legacy['args'][legacy['args'].index('-config')+1]) == base
    (game / "zandronum-gles").unlink()
    assert launch()['client'] == 'zandronum'
    print("PASS: native default, firmware driver, one-time tested settings, owner settings and legacy fallback, clean frontend")
