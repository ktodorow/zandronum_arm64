# Native R36S single-player performance

Issue [#7](https://github.com/ktodorow/zandronum_arm64/issues/7). Compares qualified
[GL4ES and software renderers](r36s-rendering.md), native **640×480 internal rendering and
presentation**, owner-supplied Doom/Doom II, DBOPL music and active effects.
60 FPS is an aim; sustained 30 FPS is specification gate. Multiplayer and
arbitrary mods are outside these measurements.

**Qualified performance path: software.** All three guided software scenarios
pass declared timing policy with owner-confirmed normal gameplay/audio.
Owner explicitly keeps GL4ES as default; software remains optional. GL4ES
does not qualify sustained-30 heavy scenes, despite higher light-scene peaks.

## Measurement contract

Record individual SDL presentation completions with
[`frame-recorder.c`](../tools/r36s/frame-recorder.c). GL uses
`SDL_GL_SwapBuffers`; software uses `SDL_Flip`. Monotonic wall intervals
include simulation, rendering, frame pacing and swap. Main-thread CPU time
and time inside swap are separate columns. These are client presentation
intervals, not direct panel scanout measurements or whole-process CPU usage.

Recorder keeps frames in memory; writes CSV only after SIGUSR2, outside timed
interval. First incomplete interval is omitted. Capacity overflow explicitly
invalidates capture. Diagnostic library is never part of personal package.

Policy fixed before acceptance runs:

- At least 60 measured seconds per scenario; guided runs target 90 seconds.
- Rolling windows sampled every 100 ms; every complete 5-second window must
  sustain at least 30 FPS.
- Below-30 one-second windows may occupy at most 5% of samples, with no
  consecutive run longer than one second. This makes “brief dip” explicit.
- Report minimum one/five-second FPS, frame-time p95/p99/max, consecutive
  slow-frame episode duration and depth. A high average cannot hide a slow
  combat interval.
- Timing pass also requires correct native rendering, audible music/effects,
  active gameplay/combat and responsive controls. A static or paused snapshot
  cannot qualify combat. Loading/menu intervals require explicit annotation;
  they must not be silently removed to improve results.

Frame counts use completions in `(start, end]`, not mean instantaneous FPS.
Slow-frame episodes consist of adjacent intervals longer than 33.333 ms;
one-second low runs describe sampled rolling windows, not exact start/end
of individual slow frames. JSON retains both kinds of evidence.

## Repeat on inspected device

Preserve owner configuration, named saves and autosaves before diagnostics.
Use [required detached PortMaster lifecycle](portmaster-testing.md#required-emulationstation-lifecycle)
for every launch. Never start binary directly or leave frontend running.

1. Copy recorder source to `/tmp/zandronum-frame-recorder.c` and build using
   installed native diagnostic compiler (client itself remains pinned Docker
   build):

   ```sh
   cc -O2 -Wall -Wextra -Werror -shared -fPIC /tmp/zandronum-frame-recorder.c -o /tmp/zandronum-frame-recorder.so -ldl
   ```

2. Back up installed `runner.sh`. For diagnostic launch only, prefix its
   `./zandronum -iwad` invocation with
   `LD_PRELOAD=/tmp/zandronum-frame-recorder.so`. Do not preload helper, shell,
   frontend or runtime checker. Restore original runner after checks.
3. Launch through `/roms2/ports/Zandronum.sh`, wait for map initialization,
   close console/menu and start gameplay. Fresh-map launches avoid diagnostic
   state inherited from saved snapshots. Capture native scene before/after
   timed interval, never inject screenshots or debugger pauses into timing.
4. Copy `record-performance.py` to device; run in separate SSH session while
   owner plays. Script rejects missing recorder before sending signals,
   requires detached unit, stopped frontend and native framebuffer, records
   loaded libraries and five-second CPU/GPU/governor/temperature samples:

   ```sh
   python3 /tmp/zandronum-issue7-record.py map07-repeat --duration 90
   ```

5. Retrieve `logs/issue7/map07-repeat.csv` and matching `-metadata.json`.
   Analyse on Mac, retaining raw CSV and hash:

   ```sh
   python3 tools/r36s/frame-stats.py ../zandronum-arm64-artifacts/issue-7/map07-repeat.csv --output ../zandronum-arm64-artifacts/issue-7/map07-repeat.json
   python3 tools/r36s/test-frame-stats.py
   ```

6. Exit SELECT+START, verify exit 0, frontend active and helper gone. Restore
   diagnostic changes, preserve owner data, verify managed-file hashes and
   recheck affected local gameplay after any retained tuning.

Host credentials remain in untracked local notes. Artifacts live outside Git
at `../zandronum-arm64-artifacts/issue-7/`; no IWADs are redistributed.

## Qualification scenarios

Use original maps with monsters, unmodified gameplay/protocol, saved GL/audio
settings, normal music/effects volumes and handheld controls. Record any
diagnostic god/all-weapons setup explicitly. Fresh process and map start,
same IWAD/skill and described route provide repeatable setup; guided input
is human gameplay, not a deterministic demo replay.

| Scenario | Exercise |
| --- | --- |
| Doom E1M1 | Walk main route, doors, shotgun pickup, enemies and gunfire; normal E1M2 transition if reached |
| Doom II MAP01 | Walk through rooms/doors and outdoor area, fight enemies; normal MAP02 transition if reached |
| Doom II MAP07 | Open starting switch, fight mancubi then arachnotrons using rockets/plasma; god/all weapons allowed for repeatable observation |

## Initial result, 2026-10-15

Actual deployed client SHA-256:
`58c721104abdb1e08f39e62da62e9301b6bf96d95bb0f4557805865cb023abdf`.
No engine rebuild, firmware/library replacement or reduced resolution used.
Firmware/runtime identities match [renderer qualification](r36s-rendering.md).
Graphics: GL4ES, `vid_renderer=1`, `screenblocks=11`, `cl_capfps=false`,
`vid_maxfps=200`, `vid_vsync=true`, `gl_usevbo=0`, `gl_texture_filter=0`,
dynamic lights enabled. Audio: DBOPL (`snd_mididevice=-3`, `opl_core=1`,
`opl_numchips=2`), OpenAL Soft/ALSA, music/effects volume 1/1.

| Owner content | SHA-256 |
| --- | --- |
| `doom.wad` | `03103e82064a960b548a98eb9656f1f30545458eb437d99475a962053b1f8fcd` |
| `doom2.wad` | `31740ef23994b3959800134b41aaf86b04a2847336d328af8c4ae890450630ab` |
| `zandronum.pk3` | `b7065624305ee960d4a7683ab916b7de826abfe3e7b871001179057b9123a280` |
| `brightmaps.pk3` | `22a2d4bb6e6023f240a9ec709354724d9d9e10ff1d80c722d907a543d38b1e7d` |
| `skulltag_actors.pk3` | `3f270becdb0e2df670e93dd4acd6fbc2a57e82a1244915672bc386b034b44e99` |

| Guided scenario | Seconds | Average FPS | Lowest 1 s / 5 s FPS | Frame p99 / max ms | Longest low-1 s sample run | Timing |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| Doom E1M1 | 89.997 | 60.824 | 28 / 32.8 | 35.096 / 226.817 | 0.9 s | Pass |
| Doom II MAP01 → MAP02 | 89.973 | 66.265 | 29 / 33.6 | 41.221 / 193.256 | 0.4 s | Pass |
| Doom II MAP07 combat, god/all weapons | 89.987 | 43.162 | 19 / 24.2 | 44.748 / 69.557 | 7.7 s | Fail |

Owner confirms Doom movement/combat/music/effects/geometry worked normally;
Doom II gameplay/audio and normal MAP01 → MAP02 transition also accepted.
Full traces retain map transition/loading intervals. Below-30 one-second
windows: Doom 1.011%, Doom II MAP01 0.674%. Longest adjacent slow-frame
episodes: Doom 263.035 ms, Doom II MAP01 235.535 ms.

Raw hashes:

- `doom-e1m1-owner.csv`:
  `0597f899b009e8f6e8b3cb939160a6db71e789d628cea987e8379d294688f3e9`.
- `doom2-map01-owner.csv`:
  `97d32ead2d02547a055ec814fbce7b41a53ad27398acb6e9250798a40e511f11`.

Initial guided MAP07 run: 89.987 seconds, average 43.162 FPS; minimum rolling
one-second 19 FPS, minimum five-second 24.2 FPS. Below-30 one-second windows
occupy 10.899%; longest consecutive low run 7.7 seconds. Frame p99 44.748 ms,
maximum 69.557 ms; longest adjacent slow-frame episode 1.952 seconds.
**Numeric gate fails**, despite high whole-run average.

Owner reports approximately 70 → 45 → 25 FPS, “very smooth and playable,”
and accepts subjective speed without requiring 60 FPS. This confirms perceived
playability; sustained-30 qualification remains separate pending bounded tuning.
CPU governor `ondemand`; samples 1.296–1.512 GHz, GPU 400 MHz, temperature
66.9–70.0°C. Mean main-thread CPU 16.849 ms/frame, swap 0.376 ms/frame.

CSV SHA-256:
`d3bb9ab4cf7355002d3a0a66440413677c393aa347770e1bb140c45bcf904a2f`.
Earlier static saved-view probes are diagnostic only, excluded from combat
acceptance.

## Floor-view repro and bounded tuning

Owner later reports about 25 FPS looking down at floor. Captured MAP07 view
shows 28 FPS on engine overlay at native 640×480. Subsequent 19.980-second
trace averages 27.327 FPS, minimum one/five-second 24/26.4 FPS; every rolling
one-second sample is below 30. Mean main-thread CPU is 32.264 ms/frame,
swap 0.377 ms/frame. This is another sustained slow view, not a brief dip.
Raw `floor-view-owner.csv` SHA-256:
`4ab07ef7723a3b71473ee9e4ff80acf6d9f962226abf82f47825e7fe555db5d2`.

Owner then leaves controls untouched for fixed-view diagnostic probes.
Console query confirms initial `gl_lights=false`; earlier lighting attempts
are not controlled evidence: physical input garbled intended second `exec`
command, so neither intended reset nor lights-on change occurred.
Discard `snapshot-lights-*` and `replay-lights-*` as causal comparisons.

| Fixed floor view, about 15 s each | Average FPS | Lowest 1 s / 5 s | Frame p99 / max ms |
| --- | ---: | ---: | ---: |
| Lights off, VBO 0, ondemand | 27.149 | 26 / 26.8 | 44.063 / 44.952 |
| Lights on, VBO 0, ondemand | 26.164 | 25 / 26.0 | 45.819 / 47.973 |
| Lights on, CPU performance at 1.512 GHz | 26.362 | 25 / 26.2 | 44.436 / 54.852 |
| Lights on, requested static VBO 1 then saved-view reload | 26.053 | 25 / 25.8 | 45.555 / 46.514 |
| Lights on, restored VBO 0 then same saved-view reload | 26.621 | 26 / 26.4 | 45.503 / 46.488 |

All one-second windows remain below 30 in every probe. Governor restored to
`ondemand` in `finally`; maximum-frequency run samples confirm 1.512 GHz
throughout. GPU remains 400 MHz. VBO change requires map reload; earlier
`floor-fixed-vbo1` live-toggle trace is excluded from VBO comparison because
engine explicitly defers activation until next level.

Floor snapshot `logs/issue7/floor-repro.zds`, SHA-256
`601a79861852e2cccd6b4daa2d1fb3ad6163413ec9a8718d1c02951b01457ae3`,
preserves camera for later GL diagnostics. Post-recording debugger check
confirms all 12 monsters killed, VBO 0 restored, one prepared sky key and no
duplicate portals. Debugger is detached outside timed intervals. These short
post-combat views diagnose cost; they cannot qualify active combat.
Main-thread CPU versus swap suggests CPU-side work, but does not identify
specific engine, audio or driver function as root cause.

Raw diagnostic CSV hashes:

| CSV | SHA-256 |
| --- | --- |
| `floor-fixed-baseline.csv` | `b4d2dd0ca81317560d224d7e8165373ba5b3ce70248145faa263b31537aff269` |
| `floor-fixed-lights-on.csv` | `5989e0ff99ed1b9919eb8105e86f3e596aabf564077feea125f3954868c43586` |
| `floor-fixed-performance.csv` | `c008e9301788d904096f8b0da44b68c6ff5e7a32433d0da8ef93813c52f6bc45` |
| `floor-vbo1-active.csv` | `c916d04dfa013e43984865fa9c16a2b9e62c9350fa33e631701a2c952c292b3c` |
| `floor-vbo0-restored.csv` | `f320948cf5d58cdc512a4faa20612985331cad6d6fe309e85131228a2b2567fb` |

## Initial GL result and first device restoration

Initial GL baseline fails sustained native-resolution performance gate.
Doom and Doom II MAP01 guided runs pass declared timing policy; MAP07 combat
fails. Owner accepts subjective smoothness at about 25 FPS, but has not
replaced sustained-30 specification gate. Short software observations from
renderer qualification do not establish sustained software performance;
no claim that both renderers fail is made.

First pass retains existing GL4ES/DBOPL defaults. No lighting reduction, VBO change,
clock change, resolution reduction, engine gameplay/protocol change or
diagnostic library ships. Follow-up below adds sampled CPU attribution for
saved floor view and representative software timing, followed by physical
combat/audio recheck for any promising change.

Final SELECT+START replay exits 0, removes helper and restores active
EmulationStation. Original runner and INI restored byte-for-byte; all original
GL/software named saves and autosaves match pre-diagnostic backup. Diagnostic
autosaves and final INI/log are retained separately under `logs/issue7/after`.
Temporary recorder/debugger removed, governor `ondemand`; all **275 managed
files** pass package hash verification. Inspected managed manifest SHA-256:
`3fa3891dbfdd0aedbaa159b79191d6da2ca0d24ccc0ec291fac06c535e15101d`.

## Follow-up: practical hardware limit or runtime overhead

Owner asks whether hardware simply cannot meet target. One further bounded
diagnostic pass repeats saved floor view after normal packaged launch.
Native `perf` uses main-thread `cpu-clock:u`, 199 Hz over 10 seconds:
1,704 samples, no lost samples. Flat leaf-IP distribution assigns 36.09%
to Mali driver, 29.46% to GL4ES, 24.88% to engine and 8.22% to libc.
These are CPU sample shares, not GPU utilization or whole-system shares;
they identify expensive graphics submission/translation without proving
specific fix or hardware ceiling. Ignore unsupported DWARF unwind warnings;
flat DSO result uses sampled instruction pointers. Profiler/dependencies
are extracted privately under `/tmp`, not installed over firmware.

Separate clean 14.957-second GL recording with music averages 26.744 FPS,
minimum one/five-second 26/26.6. Stopping music for diagnosis only gives
31.088 FPS over 14.990 seconds, minimum one/five-second 30/31. Music
restored with original `D_shawn` immediately afterwards. Music-free result
cannot qualify issue #7; it only bounds remaining synchronous audio cost.
Discard `floor-profile-baseline.csv` from timing evidence because debugger
attachment may overlap it. CPU profiling and clean captures run separately.

Software test uses fresh MAP07, diagnostic god/all weapons and `kill monsters`;
warps to same floor position `(-621.68036, 614.14821, 8)` and sets same BAM
angle `2328690688`, pitch `297402304` outside timed interval. Debugger confirms
coordinates match saved GL view exactly and 12/12 monsters killed. Corpse/
pickup history differs, so this is comparable post-combat view, not identical
world-state replay. Music/effects remain enabled, DBOPL unchanged, native
640×480 rendered and presented through qualified software path.

`software-floor-native.csv`: 19.967 seconds, average 35.007 FPS, minimum
one/five-second 34/34.8, p99/max frame 40.264/41.867 ms. No rolling one-second
sample below 30. This short diagnostic is promising; it does not establish
sustained active-combat acceptance or justify changing renderer default.

| Follow-up CSV | SHA-256 |
| --- | --- |
| `floor-profile-clean.csv` | `4b191c238605c4d38f3e8289589f55f150e0143605a239fcea2742b451cee5f3` |
| `floor-no-music-diagnostic.csv` | `6ad2f73e01d06f661de26f1056cf2a7a41699f0f2eefe5c520e64b2770574cc9` |
| `software-floor-native.csv` | `88aaa4f660bd3ea66dd1ec093c608d6a271e4c6e857fab43fb3204a3f6b749a9` |

## Guided software qualification

Fresh packaged launches use `ZANDRONUM_RENDERER=software`, `vid_renderer=0`,
native 640×480 and separate `saves/software/`; engine/content identities and
DBOPL/effects settings remain as above. Existing renderer preference is
untouched during comparison. No debugger, profiler or screenshot runs inside
timed intervals.

Software MAP07 setup enables diagnostic god mode/all weapons after map load;
all 12 monsters are alive before recording. Owner opens starting switch and
fights using handheld controls. Capture spans 89.977 seconds, 3,655 frames:
average 40.621 FPS, minimum one/five-second 32/37.8, zero below-30 one-second
windows. Frame p50/p95/p99/max 21.815/32.862/41.711/57.022 ms; longest adjacent
slow-frame episode 324.091 ms. Main-thread CPU averages 23.119 ms, presentation
12.886 ms. **Timing passes**; owner confirms active combat and normal geometry,
controls, music and effects. Post-recording check confirms all 12 monsters
killed, console/menu closed and game unpaused.

Raw `software-map07-owner.csv` SHA-256:
`ee8b6c33124be41f559b4aaf200e326e921fb8f23acc2c72c296f6a1919b54d0`.
This establishes native-30 feasibility for tested combat scene.

Software Doom E1M1: 89.990 seconds, 4,145 frames, average 46.061 FPS,
minimum one/five-second 31/41.4, zero below-30 one-second windows. Frame
p95/p99/max 28.792/39.965/104.252 ms; longest adjacent slow-frame episode
348.966 ms. **Timing passes**; owner confirms normal gameplay, geometry,
music/effects and controls. No E1M2 transition recorded in this run.
Raw `software-doom-e1m1-owner.csv` SHA-256:
`37c7a74e032e039d57f3b4a539f1de25e2cf448de0c707d837f0eb955a2cf0d4`.

Software Doom II MAP01: 89.994 seconds, 3,781 frames, average 42.014 FPS,
minimum one/five-second 25/36.6. Below-30 one-second windows occupy 1.685%,
longest consecutive low run 0.7 seconds. Frame p95/p99/max
33.084/44.294/88.733 ms; longest adjacent slow-frame episode 492.255 ms.
These brief dips pass policy fixed before testing; full trace is retained.
Owner confirms normal gameplay, geometry, controls, music and effects.
No MAP02 transition recorded in this run.
Raw `software-doom2-map01-owner.csv` SHA-256:
`1087d83c00a20237cabc2e80cc14bc00d54a7e7c3adf4a7de715b5460e6dbcc5`.

| Software scenario | Seconds | Average FPS | Lowest 1 s / 5 s FPS | Below-30 1 s samples | Longest low-1 s sample run | Timing |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| Doom E1M1 | 89.990 | 46.061 | 31 / 41.4 | 0% | 0 s | Pass |
| Doom II MAP01 | 89.994 | 42.014 | 25 / 36.6 | 1.685% | 0.7 s | Pass |
| Doom II MAP07 combat, god/all weapons | 89.977 | 40.621 | 32 / 37.8 | 0% | 0 s | Pass |

Software uses `screenblocks=11`, `cl_capfps=false`, `vid_maxfps=200`,
`vid_vsync=true`; DBOPL and both audio volumes remain unchanged. Samples show
normal `ondemand` governor, CPU 1.2–1.512 GHz, GPU 400 MHz, temperatures
67.7–73.3°C. No resolution, engine,
gameplay/protocol, music-quality or clock change required.

## Qualification outcome and final restoration

Issue #7 performance evidence is complete for **optional software renderer**
at native 640×480. All representative scenes pass sustained-30 policy with
brief dips recorded; 60 FPS aim remains unmet. Hardware is capable of tested
native-30 baseline through software. GL profiling shows considerable CPU
driver/translation cost, without proving its specific fix or hardware ceiling.
Results do not generalize to arbitrary mods or later multiplayer workload.

Owner explicitly chooses **GL4ES default, software optional** after reviewing
comparison. Keep `renderer.txt=gl4es` and existing launcher fallback. Select
software using [renderer instructions](r36s-rendering.md#selection-and-saves)
for qualified performance; existing GL saves remain in original directory.
No package default, owner setting or named save is changed by diagnostics.

Final SELECT+START exits 0, removes helper and restores active frontend.
Original runner and INI restored byte-for-byte from follow-up backup; all
original GL/software saves and autosaves match. Diagnostic state is retained
separately under `logs/issue7/profile-after`. Temporary recorder, debugger and
privately extracted profiler removed; governor remains `ondemand`.
All **275 managed files** pass unchanged package manifest. No diagnostic
library ships. Raw CSV, metadata and profiler evidence remain outside Git
at `../zandronum-arm64-artifacts/issue-7/`; host credentials stay untracked.
