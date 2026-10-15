# Personal R36S PortMaster package

Issues [#3](https://github.com/ktodorow/zandronum_arm64/issues/3),
[#4](https://github.com/ktodorow/zandronum_arm64/issues/4),
[#5](https://github.com/ktodorow/zandronum_arm64/issues/5) and
[#6](https://github.com/ktodorow/zandronum_arm64/issues/6), branch `r36s`,
derived from ARM master, including timer fix `a21bcb037`. Scope: native-resolution
launch, gameplay/menu controls, persistence, audio, rendering and frontend recovery. Acceptance follows
[main specification](specs/zandronum-3.2.1-arm64-r36s.md) and
[device test workflow](portmaster-testing.md). Owner confirmed visible
menu/game, Ports-menu launch and working SELECT+START exit on updated build.
Hardware replay also confirms normal exit and frontend restoration.
Gameplay profile and persistence scenario: [single-player guide](r36s-single-player.md).
Audio path, upgrade and listening checks: [audio guide](r36s-audio.md).
Renderer selection, separate saves and visual checks: [rendering guide](r36s-rendering.md).

## Build and stage

Start with verified client export from [ARM64 build guide](arm64-build.md),
including common SDL signal-timer shutdown fix and issue #6 signed-angle,
node-cache invalidation and sky-key copy fixes. Original issue #2 client
initializes correctly but crashes during normal SDL teardown; earlier clients
also retain missing-surface and duplicate-sky defects.
Run from checkout with Docker Desktop and Python 3 available:

```sh
python3 tools/r36s/build-runtime.py --output ../zandronum-arm64-artifacts/issue-3/runtime-scoped
python3 tools/r36s/package.py --client ../zandronum-arm64-artifacts/issue-6/client-sky-fix --runtime ../zandronum-arm64-artifacts/issue-3/runtime-scoped --output ../zandronum-arm64-artifacts/issue-6/package-single-player
python3 tools/arm64/build.py cleanup
```

Outputs must be absent or empty, outside checkout. Runtime build uses same
private, pinned ARM64 BuildKit builder as client build; cleanup removes only
that builder/cache. Source and exports survive cleanup.

Graphics recipe pins Debian image, signed snapshot dependencies and gl4es
commit `a025615bc79ac43ec911dced8fe5b8eb45a0b72d`, with archive SHA-256 check.
Target flags: `-march=armv8-a -mtune=cortex-a35`. Build disables X11/GLX stubs
and enables GBM, GLES2 and gl4es EGL wrapper. Local patch separates core
`eglGetPlatformDisplay` pointer-sized attributes from EXT 32-bit attributes;
passing core attributes through EXT pointer type failed GCC14 compilation
and would be incorrect on ARM64. Source patch, recipe, build log, dependency
versions, ELF dynamic sections and checksums accompany export.

Package stages private sdl12-compat from client export, gl4es `libGL.so.1`
and EGL wrapper, plus Debian GLU/OpenGL/GLdispatch libraries. Firmware
SDL2, EGL, GLES, GBM and Mali libraries stay installed in firmware paths.
Package verifies both export manifests before copying files. It includes
client/resource identities and `managed-files.sha256`; it includes no IWAD,
live configuration, saves, renderer preference or runtime logs.

## Install and use

Inspected installation uses `/roms2/ports`. Set SSH destination outside
repository; use existing SSH authentication:

```sh
rsync -a --no-owner --no-group ../zandronum-arm64-artifacts/issue-6/package-single-player/ "$R36S_SSH_HOST:/roms2/ports/"
ssh "$R36S_SSH_HOST" 'cd /roms2/ports && sha256sum -c zandronum/managed-files.sha256'
```

Do not add `--delete`: package upgrades preserve owner data. Refresh Ports
list if necessary, then launch **Zandronum** on handheld. SSH diagnostics
can invoke same launcher:

```sh
ssh "$R36S_SSH_HOST" 'bash /roms2/ports/Zandronum.sh'
```

Launcher returns after creating detached `zandronum-r36s.service`; return
code reports unit submission, not game outcome. A second concurrent launch
is rejected. Inspect `zandronum/log.txt` for game status; older runs move to
`zandronum/logs/`. Root unit journal provides recovery diagnostics if game
directory becomes unavailable.

| Data | Location under `/roms2/ports/zandronum` |
| --- | --- |
| Generated configuration | `config/zandronum.ini`, explicit engine `-config` |
| Saves | `saves/software/` (software); original `saves/` (GL), explicit engine `-savedir` |
| Optional private IWADs | `iwads/doom2.wad` or `iwads/doom.wad`, case insensitive |
| Preferred renderer | `renderer.txt`: `gl4es` (default) or `software` |
| Current output and exit status | `log.txt` |
| Previous run output | `logs/*-previous.log` |
| Build/source identity | `package.json`, `evidence/client/`, `evidence/runtime/` |

IWAD discovery prefers Doom II, checking private `iwads/` then installed
`/roms2/doom/`; next checks Doom in same order. `ZANDRONUM_IWAD` overrides
discovery; invalid override exercises missing-data error without moving
owner files. Supply your own IWAD. Missing data opens installed PortMaster
error dialog, logs required locations, then restores frontend.

Controls use installed PortMaster controller database and GPTOKEYB:
D-pad navigates, A selects, B backs out, START opens menu. Gameplay profile
adds left-stick movement, right-stick aim, R2 fire, A use and L1/R1 weapons;
[full mapping and save-name grid](r36s-single-player.md#input-profile).
Owner requested
SELECT+START for exit. Installed GPTOKEYB runs with explicit kill mode and
`-hotkey back`; target regex `^\./zandronum( |$)` matches only client argv,
preserving runner and SSH commands containing package name. Original
SELECT/X sequence failed during Ports retest.
Native engine joystick is disabled to prevent duplicate input.
First launch migrates #3 menu-only bindings once, backing up live INI;
subsequent launches preserve changed settings and bindings. See
[upgrade and paths](r36s-single-player.md#configuration-upgrade-and-paths)
and [current #4 evidence](r36s-single-player.md#evidence-2026-10-15).

## Display and lifecycle

Every launch, including SSH, creates detached systemd unit under current
user with supplementary groups. Runner verifies cgroup isolation, sources
installed PortMaster control/dArkOS hooks, registers cleanup, stops
`emulationstation.service`, then checks service and processes are gone.
Only then may client start. Logs retain memory/service state around stop
and recovery. Never launch binary directly for device tests.

Default `gl4es` path uses installed PortMaster graphics hook; SDL's GL driver
and EGL wrapper are package-private, while `LIBGL_EGL`/`LIBGL_GLES` name
firmware Mali-backed libraries explicitly. Native engine arguments force
640×480 fullscreen. Common node-builder fix restores missing walls/floors;
GL4ES and vendor libraries remain unchanged. Selectable software fallback uses
SDL1.2 compatibility over firmware SDL2 KMSDRM/GLES2 presentation with native
640×480 CPU rendering. See [qualification results](r36s-rendering.md).
Engine refuses cross-renderer saves, so software has separate save directory;
existing GL saves remain available through default GL selection.

Before engine initialization, checker runs `ldd -r` and resolves all 260
required SDL2 symbols against actual selected libraries. Runtime maps,
IWAD/client/resource hashes and renderer identity appear in log.

Normal return and handled signals stop owned game/helper, call installed
finish hook with helper cleanup scoped to owned PID, then restore ES.
Systemd `ExecStopPost` independently restores ES after runner failure or
forced death and logs unit result/exit reason. Restoration resets failed service state and waits for active
service/process. Physical usability must still be checked by owner.

Diagnostic stop, through systemd, preserves recovery path:

```sh
ssh "$R36S_SSH_HOST" 'sudo -n systemctl stop zandronum-r36s.service'
```

## Evidence recorded 2026-10-15

Device: dArkOS `09022026`, Debian 13 ARM64/glibc 2.41, PortMaster
`2026.06.23-0015`, SDL2 `2.32.10`, Mali-G31. Framebuffer virtual size
`640,480`; DRM connector mode `640x480p77`.

Client SHA-256:
`c7cecc51304e883d4c23e546785b39dab4161ac1cc18f8a478fff76c0942371e`.
Owner Doom II SHA-256:
`31740ef23994b3959800134b41aaf86b04a2847336d328af8c4ae890450630ab`.
Initial package used issue #2 `run-2` client. Installed replacement comes from
`issue-3/client-timer-fix`, built from master `8edeef98f` plus then-uncommitted
common timer cleanup, now committed as `a21bcb037`. Export retains original
dirty-build provenance, exact source archive and matching debug symbols.

Initial SSH launch used packaged launcher. Runner survived ES shutdown in
`/system.slice/zandronum-r36s.service`; ES became inactive before client.
`ldd -r` and all 260 SDL2 symbol checks passed against actual device stack.
Engine initialized Doom II and reported:

```text
GL_VENDOR: GL4ES wrapping ARM
GL_RENDERER: GL4ES using Mali-G31
GL_VERSION: 2.1 gl4es wrapper 1.1.7
GL_SHADING_LANGUAGE_VERSION: 1.20 via gl4es
```

Engine also logged `Resolution: 640 x 480`. Owner reported visible menu/game
and successful launch from Ports. Controller quit failed with original client;
fixed client passes real gamepad SELECT+START replay with exit 0, active ES
and no helper. Owner subsequently confirmed updated build works in response
to physical launch/chord/frontend check.

| Check | Recorded result |
| --- | --- |
| SELECT+START on fixed client | Real gamepad replay: exit 0; successful unit; ES active; no game/helper remains |
| Physical launch/exit acceptance | Owner confirmed updated build works after requested Ports launch and SELECT+START frontend-return check |
| Invalid renderer after ES stop | Exit 2; ES active again; no game/helper remains |
| Handled systemd stop | Exit 143; ES active again; no game/helper remains |
| Game killed with SIGKILL | Exit 137; ES active again; no game/helper remains |
| Runner killed with SIGKILL | Independent `ExecStopPost` restored ES; journal records signal 9; no game/helper remains |
| Missing IWAD via invalid override | Owner saw PortMaster Doom-data requirement; dismissal ended run with exit 2 and restored active ES |
| Package update | All 271 managed hashes verified on device; existing config hash preserved |
| Corrupt runtime input | Package staging rejected checksum mismatch before creating output |
| Build/checks | Pinned runtime and fixed client exports built; four source-identity tests, Python/shell syntax and whitespace checks passed; both private builders/caches removed |

Failures retain before/after service state and memory in logs. Forced
runner-death result requires journal from that earlier run; latest recovery
script additionally records systemd result directly in package log.
Owner confirmation completes issue #3 launch/exit gate.
Music initialization failed with inherited FMOD MIDI default during #3.
Issue #5 subsequently selected OPL/ALSA and passed physical Doom/Doom II
listening, volume, normal map transitions and relaunch; see [audio evidence](r36s-audio.md).

### Normal-exit regression

Repeat Ports launch exposed failed controller quit. Enabling installed
GPTOKEYB chord exposed two independent problems: broad `pkill -f` target
also matched runner/SSH command lines, and client crashed during SDL teardown.
Anchored regex fixes target scope. Native GDB backtrace with matching
issue #2 symbols found PC=0, called by `I_HandleAlarm` at
`src/sdl/i_system.cpp:273`: armed `ITIMER_REAL` continued calling SDL ticks
after sdl12-compat cleared its SDL2 function pointers.

Common fix registers timer cleanup with `atterm`, disarms `ITIMER_REAL`
and ignores pending `SIGALRM` before `SDL_Quit`. Common fix lives on master
in commit `a21bcb037`; R36S branch inherits it and adds device packaging.
New client export retains exact fixed
source and provenance for rebuilding independently of worktree state.
Keyboard and signal-adapter prototypes were discarded; no preload workaround
is part of final package.

Hardware regression script replays real gamepad SELECT+START events and
requires game exit 0, active ES and no GPTOKEYB process. Invoke only with
packaged client already visible, using inspected `js0`/`event2` controller:

```sh
scp tools/r36s/check-controller-exit.py "$R36S_SSH_HOST:/tmp/zandronum-controller-exit-check.py"
ssh "$R36S_SSH_HOST" 'python3 /tmp/zandronum-controller-exit-check.py'
```

Owner confirmed physical menu/game, chord and frontend return separately.
Script does not establish display correctness.

Host evidence resides outside Git at
`../zandronum-arm64-artifacts/issue-3/`: inspected installed helpers,
pinned graphics source, runtime exports/build logs, staged package and
retrieved device logs. No SSH credentials are recorded.

Single-player package defaults to GL4ES at native 640×480 and DBOPL music.
Runner applies audio profile 2 once on fresh installs/upgrades, backs up
existing INI, then keeps saved audio choices and volumes. Common client fixes
and final package identity are recorded in [rendering evidence](r36s-rendering.md).
