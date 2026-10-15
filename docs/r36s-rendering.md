# R36S renderer qualification

Issue [#6](https://github.com/ktodorow/zandronum_arm64/issues/6), branch `r36s`.
Uses [PortMaster package](r36s-package.md) and native 640×480 internal/display
resolution. Correct base-game rendering takes priority over acceleration.
Sustained frame-rate acceptance remains issue #7.

## Selection and saves

Default: Zandronum OpenGL renderer (`vid_renderer=1`) through GL4ES and
firmware Mali GLES2/KMSDRM. Package audio profile 2 selects built-in DBOPL
(`snd_mididevice=-3`, `opl_core=1`) once for fresh installs and upgrades, then
retains saved audio choices and volumes. Software remains selectable (`vid_renderer=0`):
CPU draws game; sdl12-compat and firmware SDL2 present through GLES2/KMSDRM.
`SDL12COMPAT_OPENGL_SCALING=0` prevents SDL's compatibility scaling path.
Launcher always supplies `-width 640 -height 480 +fullscreen 1`.

Create `/roms2/ports/zandronum/renderer.txt` containing `software` or `gl4es`
to choose future launches. `ZANDRONUM_RENDERER` overrides file for one launch:

```sh
ssh "$R36S_SSH_HOST" 'ZANDRONUM_RENDERER=software bash /roms2/ports/Zandronum.sh'
ssh "$R36S_SSH_HOST" 'ZANDRONUM_RENDERER=gl4es bash /roms2/ports/Zandronum.sh'
```

Exit current run before selecting another renderer. Both paths use same
detached service, controls/audio settings and frontend recovery. Package
updates preserve renderer preference and live configuration.

Engine identifies software saves as `ZANDRONUM (Software)` and GL saves as
`ZANDRONUM (GL)`; it refuses cross-renderer loads. Software uses
`saves/software/`; GL keeps original `saves/`. Separate directories prevent
software autosaves from replacing existing GL saves. Existing GL progress
requires GL renderer; no save conversion is supplied. Software uses original
software lighting; GL-only effects are not promised.

## Accelerated path: common ARM64 fixes

Tested actual R36S vendor stack, not a desktop substitute:

| Component | Tested identity |
| --- | --- |
| Firmware | dArkOS `09022026`, Debian 13 AArch64, glibc 2.41 |
| Display | Native 640×480, DRM `640x480p77` |
| SDL2 | Firmware `2.32.10`, KMSDRM |
| Desktop GL translation | Private GL4ES `a025615bc79ac43ec911dced8fe5b8eb45a0b72d`, GLES2/GBM/EGL wrapper |
| GLU | Package-private Debian GLU/OpenGL/GLdispatch libraries |
| Device graphics | Firmware EGL/GLES, Mali-G31 |
| Reported API | GL 2.1 GL4ES wrapper 1.1.7, GLSL 1.20 |
| Current client SHA-256 | `58c721104abdb1e08f39e62da62e9301b6bf96d95bb0f4557805865cb023abdf` |
| Angle-only client SHA-256 | `d373ac19b1abd3bcb2adc61f8dbce5ca53df65199c18805826d926b3e7734ace` |
| Original client SHA-256 | `c7cecc51304e883d4c23e546785b39dab4161ac1cc18f8a478fff76c0942371e` |

Owner selected two Doom II MAP01 views showing missing walls/floors and
visibility into another room. Saved positions reproduce fault. First view:
world position approximately `(-16.0203, 1418.21, 56)`, angle
`1441857536`, pitch `-64749568` in engine integer units. Software view at
same position/orientation draws complete wall, ceiling and floor.

Accelerated probes did not restore surfaces: disable immediate-mode batching,
use immediate client arrays instead of VBOs, select shader model 2, disable
texture shaders, or enable precise/seamless rendering. Bypassing CPU angular
occlusion changed black gaps into sky; geometry stayed missing. That probe
is not a fix. Captured GL subsectors form closed polygons; fixed/float vertex
coordinates and cached pseudoangles match their expected calculations.

Following geometry through engine draw calls exposed absent linedefs 296/297
and a subsector polygon skipping their corner. Closed polygons alone had
missed this topology defect. `FNodeBuilder::PointToAngle` converted negative
floating-point angles directly to unsigned `angle_t`. AArch64 `FCVTZU`
clamped southwest, south and southeast to zero instead of wrapping them.
Subsector closure then omitted walls and replaced floor/ceiling corners with
diagonal minisegs. GL4ES never received missing surfaces.

Common client fix converts through signed `int32`, then wraps to unsigned BAM.
Scaled `atan2` result lies within signed range. Node-cache magic changes from
`CACH` to `CAC2`, forcing regeneration of previously damaged cached geometry.
Common fixes are committed on `master` as `5eee33fc1` and inherited by `r36s`;
device-specific renderer/save selection stays on `r36s`.

Actual-engine eight-direction regression fails southwest/south/southeast
before fix, passes all eight afterwards. Process-local signed-conversion
probe restores both owner-selected views. Clean rebuilt client independently
passes original native-resolution image check, restores wall segments, and
replaces deliberately generated old cache. GL4ES, EGL/GLES, GLU and SDL
libraries retain exact prior package hashes. No driver installation or
translator patch was needed for this defect.

Regression command, with native GDB attached and matching client symbols:

```gdb
source tools/arm64/check-nodebuilder-angles.gdb
```

New client reports 1,041 segs/201 subsectors in MAP01 versus damaged
956 segs/202 subsectors. Both previously absent wall segments occur once.
Original missing-surface ROI changes from `1.0000` (fail) to `0.0000` (pass),
with full-image inspection confirming complete geometry in both saved views.

GL4ES translates desktop OpenGL to GLES and documents compatibility limits
in its [upstream README](https://github.com/ptitSeb/gl4es#readme).
This engine defect does not establish compatibility of other games.

## Accelerated combat and sky-cache fix, 2026-10-15

Owner confirms angle-only build renders both original glitch views correctly,
with up to 77 FPS in lighter room. Heavier MAP07 combat drops to 15, then
10 FPS during monster attacks/explosions; captured benchmark records 9 FPS.
These observations reject any inference that room maximum proves combat
performance. Diagnostic scene is saved as `issue6-perf.zds` outside owner slots.

Six-second sample reports main thread approximately 81% of one CPU core,
GPU 16–21% busy at 400 MHz. Disabling dynamic lights only reaches 11–12 FPS;
lighting is restored. Sampled stacks include repeated sky rendering.
Prepared draw lists contain 20–22 identical logical sky keys. Their copied
trailing padding contains old heap data (`0x55`), unlike zeroed lookup key.
`UniqueList::Get` compares full object bytes with `memcmp` but used memberwise
assignment, which need not copy padding. Every mismatch adds another portal
and renders same sky again.

Common fix copies full key representation with `memcpy`. Actual prepared-cache
regression fails duplicate entry 1 of 20 on active old client, passes one key
on rebuilt client. Frozen actors can hide defect by changing allocation
patterns; active combat is required for comparison. Regression waits until
BSP traversal has prepared skies:

```gdb
source tools/arm64/check-sky-cache.gdb
```

Rebuilt client retains signed angle fix and `CAC2` cache invalidation.
Eight-direction regression still passes. All 275 managed device files verify;
manifest SHA-256:
`c7ae75c1eec3993655cdfcec05284f460ac39eb4d687fa21609c176ba7270a65`.
No graphics library, resolution or effects reduction accompanies sky fix.

| Fixed GL check | Result |
| --- | --- |
| MAP01, both saved glitch views | Complete surfaces; owner confirms correct rendering, up to 77 FPS on angle-only build |
| MAP07, saved explosion-heavy scene after sky fix | Native 640×480 capture 35 FPS; subsequent active-combat benchmark 39 FPS, one sky portal, lights enabled |
| Physical combat repeat after sky fix | Owner reports better overall, up to 75 FPS; heavy explosions around 25 FPS and spikes from 50 to 25 or 9 FPS remain under diagnosis |
| Normal MAP07 → MAP08, music/effects, save and SELECT+START | Owner accepts final GL4ES/DBOPL check: "its ready. its okay"; new GL save records MAP08, clean exit 0/frontend active |
| Packaged GL save reload | Owner’s new `saves/save4.zds` (`GL RENDERdasd`, MAP08) loads after restart; native capture complete, 74 FPS sample |
| GL Doom rendering repeat | E1M1 and E1M2 native captures show complete surfaces; 76 and 78 FPS samples. Agent invokes `changemap e1m2`; this is a scripted transition, not another owner-confirmed normal exit |

Renderer timers in `stat rendertimes` return zero on this ARM build; those
values cannot establish CPU/GPU timing. FPS observations above are preliminary,
not sustained-FPS acceptance for issue #7.

### Music and remaining frame-time spikes

Native `perf` sampling attributes approximately 24% of sampled CPU to default
MAME OPL synthesis (`opl_core=0`), versus approximately 14.5% with built-in
DBOPL (`opl_core=1`). Scene positions differ between those profiles; percentages
identify expensive work, not a controlled FPS improvement. OpenAL stream
refills run on main thread in roughly 71 ms audio chunks. Faster music synth
reduces that work without disabling music, effects or lighting.

Owner reports DBOPL trial drops from about 55 FPS to 25–33 FPS during heavy
firing, after earlier 9 FPS spikes. Owner accepts final music/effects check
after normal packaged relaunch.
Short frame recordings use temporary `SDL_GL_SwapBuffers` interposer outside
package, recording wall time and main-thread CPU time in memory. No debugger
is attached during valid recordings. Native saved MAP07 scene, lights enabled:

| 12-second trial | Mean FPS | 99th-percentile frame | Longest frame |
| --- | --- | --- | --- |
| MAME, default governor | 36.52 | 53.19 ms | 74.85 ms |
| DBOPL, default governor | 40.09 | 49.42 ms | 65.04 ms |
| Java OPL, default governor | 32.54 | 44.19 ms | 45.93 ms |
| DBOPL, temporary performance governor | 39.52 | 43.86 ms | 46.38 ms |

These are short active-scene diagnostics with differing elapsed combat time,
not deterministic demos or sustained acceptance. Java backend is slower on
average; CPU performance governor offers no clear average gain. Restore
`ondemand`; retain DBOPL after accepted physical audio/rendering check.
No clock increase or driver change is selected. Discard recordings made during failed console
commands or stalled debugger audio calls; stale CSV files are not new evidence.
Saved owner configuration retains `opl_core=1`, `snd_mididevice=-3`, and
effects/music volumes 1/1. At owner request, package audio profile 2 now
selects DBOPL on fresh installs and upgrades from profile 1. Runner backs up
pre-upgrade INI as `config/zandronum.before-audio-v2.ini`; later launches
retain audio settings. Common engine defaults stay unchanged. No frame
recorder ships with package. Remaining heavy-combat dips belong in
issue #7 performance qualification and are not claimed fixed.

## Software fallback evidence, 2026-10-15

Historical checks below precede common engine fixes. Final client also reloads
Doom E1M2 software save through packaged launcher at native 640×480; complete
surfaces and sprites, 41 FPS capture, clean exit 0/frontend recovery. GL4ES is
restored afterwards.

Before common engine fixes, same-position screenshot comparison passes native
640×480 geometry check in software. Original broken GL missing-surface region `(250,240)-(285,320)` is 100%
near-black; software is 0%. Full screenshot review is also required: drawing
sky into hole can pass a black-pixel check while geometry remains broken.

Owner physically walked/turned through original room and confirmed software
rendering "Looks correct." Existing four named GL saves retain their exact
pre-issue hashes. Updated package verifies all 275 managed files on device.
Tested `managed-files.sha256` SHA-256:
`53203ff46833021698f69f22c6d9f17c06f0cd87d447728d3e4bf3d0f161a9de`.

| Scene/check | Result |
| --- | --- |
| Doom II MAP01, original glitch position | Full wall/floor/ceiling restored; owner physically confirms |
| Doom II MAP07, geometry and weapon effects | No observed missing surfaces; native screenshots |
| Doom II MAP07, active mancubi/projectiles | Sprites, occlusion and effects intact in captured views; owner reports correct rendering |
| Preliminary MAP07 timing | Owner observes approximately 32–45 FPS; active-monster captures show 36, 39, 40 and 41 FPS; initial attack capture 36 FPS |
| Normal Doom II MAP07 → MAP08 | Owner completed normal exit, inspected MAP08 and saved `RENDER` through controller menu |
| Software save and exit | `saves/software/save0.zds`; owner confirms save/frontend return; engine exit 0, ES active, no game/helper residue |
| Default-renderer packaged relaunch | `RENDER` software save loads MAP08; native screenshot confirms scene after restart |
| Doom E1M1, geometry, enemies, gunfire and menus | Owner reports "All worked; rendering correct"; native captures show intact surfaces, sprites and muzzle flash |
| Preliminary Doom timing | E1M1 captures show 50, 46, 46 and 46 FPS; E1M2 capture 40 FPS |
| Normal Doom E1M1 → E1M2 | Owner completes transition; engine log and screenshot confirm E1M2 |
| Doom software persistence | Agent saves `RENDER DOOM` to `saves/software/issue6-doom.zds`, replays SELECT+START, relaunches default package with Doom IWAD and reloads E1M2; both runs exit 0 and restore frontend |
| Pre-fix explicit GL selection after package update | Original MAP01 GL save loads at 640×480 with original GL save directory; missing-surface check still fails; exit restores frontend |
| Initial software default selection | Software restored without preference file; matching MAP01 repro draws complete geometry, black-region check passes; final exit 0, frontend active |

MAP07 uses god mode/all weapons for repeatable observation. An initial
no-monsters run inherited diagnostic flag from saved repro and is excluded
from combat evidence; corrected `sv_nomonsters=false` and fresh MAP07 show
active enemies/projectiles. CPU governor reports `ondemand`, current/max
frequency 1,512,000 kHz, temperature sample 69.2°C. FPS counter samples are
preliminary; they do not establish minimum sustained FPS or depth/duration of
dips required by #7. Software settings: `screenblocks=11`, `cl_capfps=false`,
`vid_maxfps=200`, saved `vid_vsync=true`; same accepted controls/audio profiles.
Software draws game on CPU; GLES presentation does not offload rasterization.

Owner's Doom completion reply confirms rendering; log still showed running
E1M2 and only autosave. Named Doom save, exit and reload above are agent-driven
checks, not an additional controller-only save-entry claim. Issue #4 records
physical controller save/load acceptance separately.

Artifacts stay outside Git at `../zandronum-arm64-artifacts/issue-6/`:
owner-selected screenshots, matching renderer saves, replay captures,
debugger geometry/cache dumps, historical rejected GL logs and staged packages.
Diagnostic memory patches are process-local; qualified binaries are clean
source builds. Device repro saves/logs stay under
`/roms2/ports/zandronum/logs/issue6/`. Private profiler, debugger, frame recorder
and final capture/replay helpers are removed after checks; screenshot directory
is restored. No probes or credentials belong in managed package.

Regression check uses actual-device saved scene, not a host unit test that
cannot exercise this vendor graphics path. Diagnostic `render-check.py`
is retained with artifacts. After loading matching renderer's repro via
packaged launcher, capture and check original missing-surface region:

```sh
python render-check.py --host "$R36S_SSH_HOST" --socket "$R36S_SSH_SOCKET" \
  --load '' --output replay.png
```

Requires Pillow and diagnostic keyboard helper on device. Native-size check
and black-region check produce `1.0000` (fail) for original GL view and
`0.0000` (pass) for software and corrected GL. Full image inspection confirms geometry;
threshold alone is insufficient. Harness is diagnostic, not shipped runner.

## Final single-player package

Owner accepts fixed GL rendering and final music/effects, MAP07 → MAP08, save
and clean exit checks. Final package retains native 640×480, GL4ES default,
DBOPL profile 2 and working software fallback. Staged artifact:
`../zandronum-arm64-artifacts/issue-6/package-single-player/`. All 275 managed
files verify on device; manifest SHA-256:
`3fa3891dbfdd0aedbaa159b79191d6da2ca0d24ccc0ec291fac06c535e15101d`.
Owner renderer preference is `gl4es`; final normal Doom II launch stays running
on owner request. Device upgrade executes profile 2, then clean exit archives
`r36s_audio_profile=2`, `opl_core=1` and unchanged volumes 1/1. Next packaged
launch skips migration, retains settings and reports GL4ES using Mali-G31.
Upgrade exit restores frontend; final requested launch remains running.
Renderer qualification passes; sustained performance remains issue #7.

## Repeatable device check

1. Launch default package with Doom II. Inspect MAP01 room corners and floors
   from both sides, including owner's saved glitch views. Turn and move;
   check geometry, textures, sprite occlusion and HUD at native resolution.
2. Fight enemies, fire multiple weapons, open menus, save/load, finish map
   normally and inspect next map. Include heavier combat, not only empty room.
3. Exit SELECT+START; require game exit 0, helper gone and frontend active.
   Relaunch default package, load matching GL save and check saved settings.
4. Repeat representative geometry, sprites/combat and map transition with
   owner's Doom IWAD. Record `vid_fps` samples as preliminary observations,
   not sustained-FPS acceptance.
5. Explicit software selection must still launch at 640×480 and load separate
   software saves. Restore GL default afterwards. Verify managed-file hashes
   and preserve owner saves/configuration throughout upgrade.
