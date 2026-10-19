# Native GLES on R36S

Native GLES is default for R36S ARM64 builds, packages and content launcher.
Desktop GL4ES and software remain selectable fallbacks. Engine version,
gameplay and network protocol stay Zandronum3.2.1.

Owner accepted smoother native gameplay and remaining heavy-scene limits on
2026-10-19, ending performance tuning. Latest live MAP01 averages70.718FPS,
minimum5s55.2FPS; matched stationary MAP07 stress averages28.6–28.7FPS,
minimum5s23.4–23.6FPS. These are measured results, not sustained60/30 guarantees
or proof of hardware ceiling. Broader mod, visual and multiplayer gaps below
remain recorded; default selection does not close them.

## Build and package

From clean source worktree, use distinct empty export directories outside
checkout. Build tools, dependency installs and compilation stay inside pinned
Docker; no firmware installation or download required:

```sh
python3 tools/arm64/build.py build --output ../zandronum-arm64-artifacts/gles/client-native
python3 tools/arm64/build.py build --legacy-gl --output ../zandronum-arm64-artifacts/gles/client-legacy
python3 tools/r36s/build-launcher.py --output ../zandronum-arm64-artifacts/gles/launcher
python3 tools/r36s/package.py --client ../zandronum-arm64-artifacts/gles/client-legacy --gles-client ../zandronum-arm64-artifacts/gles/client-native --runtime ../zandronum-arm64-artifacts/issue-3/runtime-scoped --launcher ../zandronum-arm64-artifacts/gles/launcher --output ../zandronum-arm64-artifacts/gles/package
```

Default ARM64 build (also explicit `--gles`) selects `ZANDRONUM_USE_GLES=ON`, `USE_GLES2=1`, native GLESv2 linkage
and `zandronum-gles` executable/debug symbols. Legacy executable stays
`zandronum`. BuildKit keeps independent GLES/desktop compiler caches. Both
exports must contain identical engine PK3s and SDL1.2 compatibility library;
package builder verifies their checksums. Scratch access notes and research
clones are excluded from source snapshots.

Full source, debug symbols, compiler/ABI reports and logs stay in host exports.
SD-card package contains runtime files, SDL2 required-symbol manifests,
identities, licenses and managed-file hashes. Neither WADs nor owner settings
and saves form part of distribution. Install managed files over existing
package, preserve owner data, then verify manifest and preserved-data hashes.

## Select and run

Content menu offers **Native GLES** when `zandronum-gles` is installed. Existing
named profiles retain renderer choice; `renderer.txt` may contain `gles`, `gl4es` or
`software`. Packages write `gles`; first native launch migrates current menu
selection once, backing up `last.profile` and retaining content/order/name.
Later explicit fallback choices persist. Legacy-only packages require explicit
`--default-renderer gl4es` or `software`. For diagnostic one-run
launch through same detached service:

```sh
ZANDRONUM_LAUNCHER=0 ZANDRONUM_RENDERER=gles bash /roms2/ports/Zandronum.sh
```

Native mode uses firmware GLES/EGL, bypasses desktop-GL hooks and runs
`./zandronum-gles`. Native SDL1.2 compatibility library keeps existing engine
input/audio/window lifecycle; bridge calls already-loaded SDL2 for ES2 profile,
EGL and swap control. Startup rejects missing/mismatched module, unsupported
profile, missing stencil or failed swap control. No silent GL4ES fallback.
Internal/display resolution remains640×480; use original PortMaster controls,
music/effects and frontend recovery. SELECT+START matches exact selected client.

Qualification recorder/replay accept `--client zandronum-gles`. Keep raw timing
separate with `record-performance.py RUN --client zandronum-gles --output-dir
/roms2/ports/zandronum-gles-test/logs/performance`. Both tools require native
client inside detached `zandronum-r36s.service` and frontend stopped. Recorder
also requires diagnostic preload before sending signals; never ship preload.

Native base saves use `saves/gles/`; original GL saves stay in `saves/` and
software saves in `saves/software/`. Native renderer retains GL save format,
but original slots are isolated during qualification. Content profiles keep
`saves/profiles/CONTENT_KEY/RENDERER/`. Configuration/audio migrations and
owner settings retain existing behavior. Native configs use
`config/gles/zandronum.ini` or `config/profiles/CONTENT_KEY/gles/zandronum.ini`,
seeded once from corresponding existing config. `gles.cfg` applies tested
shader lighting, client arrays and uncapped/no-requested-vsync settings once;
`r36s_gles_profile=1` preserves later owner changes. No benchmark preload ships.

Current firmware advertises640×480p77 and clamps requested swap interval0
to1. No refresh, governor, clock or firmware change accompanies default switch.

## Check before release

Desktop visual reference uses matching desktop `zandronum` export with real
Mesa desktop GL, shader model4 and copied handheld config. Run
[capture tool](../tools/r36s/tests/capture-desktop-fixtures.py) inside disposable
ARM64 container with licensed IWAD and fixtures mounted read-only. Requires
`libgl1`, `libglx-mesa0`, `libgl1-mesa-dri`, `libglu1-mesa`, SDL2, `xvfb` and
`xdotool`; select export's SDL compatibility library, never GL4ES runtime.
Tool verifies actual desktop GL strings/libraries, captures fixed ACS cameras
at640×480 and exits normally. Store copied config/output privately outside Git;
metadata includes config hash and graphics-CVAR whitelist only.

On2026-10-15 all six legacy03 references capture successfully with Mesa25.0.7
llvmpipe, GL4.5 compatibility/GLSL4.50, using isolated native07 config. Native07
GLTEST05 brightmap dots match desktop reference; GL4ES SM3/FPE differs.
GLTEST04 voxel front appears gray on both backends; current horizontal GLTEST03
view shows brick ceiling band on both and does not establish skybox coverage.
Animated warp/model/fuzz phases differ, so screenshots are visual comparisons
with stated coverage limits. Successful capture alone is not feature acceptance.
Static center regions in GLTEST01–03 agree within2 RGB levels in99.80–99.95%
of sampled pixels between Mali07 and desktop reference; HUD, animation and
camera coverage gaps remain outside this comparison.
Separately named V2 fixtures correct sky-camera pitch/sprite placement and
voxel palette while retaining GLTEST05 bytes. Trusted legacy04 desktop V2
captures show all four skybox sprites above foreground walls and RGB voxel
bands. Matching native08 Mali V2 captures confirm both fixed views, with two
screenshots per map and clean exit/frontend recovery. V1 evidence remains
unchanged; other viewpoints, motion and full feature coverage remain open.

Use [release research and test matrix](../research_notes/Direct%20GLES%20for%20R36S/release_validation.md),
[required frontend lifecycle](portmaster-testing.md#required-emulationstation-lifecycle)
and [existing timing policy](r36s-performance.md). Require actual firmware
Mali GLES runtime with no GL4ES mapping, full base/advanced-feature visual
checks, sustained combat/thermal frame traces, working audio/controllers,
save/load/profile/upgrade behavior and stock-server compatibility. Build or
context smoke check does not complete renderer acceptance.

**Owner revised native acceptance on2026-10-19:60FPS during ordinary
pistol/shotgun combat,30FPS during heavy explosions**, at640×480 with
music/effects/lights enabled. Frame budgets are16.667ms and33.333ms respectively;
report1s/5s minima, p95/p99/max frame times and dip/hitch duration separately.
Historical diagnostics below retain their original60FPS comparisons; these
do not redefine current workload-specific targets. Record90s gameplay routes
and thermal soak; select explicit analyzer target for each workload:

```sh
python3 tools/r36s/frame-stats.py RUN.csv --target 60 --min-duration 60 --output RUN.stats.json
python3 tools/r36s/frame-stats.py EXPLOSIONS.csv --target 30 --min-duration 60 --output EXPLOSIONS.stats.json
```

Qualification on2026-10-15: **release blocked**. Native03 runs640×480 on actual
Mali-G31 GLES3.2/GLSL ES3.20, depth24/stencil8, with firmware EGL/GLES and no
GL4ES/desktop GL mapping. Base-world rendering works; SELECT+START exits status0
and restores frontend. Full feature, audio/control, save and network coverage
remains open.

| Device diagnostic | Result | Meaning |
| --- | --- | --- |
| Native03 MAP01 | 1.73FPS; separate call profile2.09FPS | Approximately59 `glBufferSubData` calls consume401ms of478ms/frame |
| Native05 MAP01 moving/combat,14.96s | 26.21FPS; main CPU13.26ms, swap23.63ms; min5s24FPS, p9551.49ms | Per-batch `glBufferData` replacement reduces uploads to0.89ms/frame; fails60FPS target |
| Native05/06 GLTEST05 fixed view,about15s each | Both25.793FPS; native06 CPU7.83ms, swap30.04ms, p9941.24ms | Dynamic-light loop early exit gives no measured FPS gain in matched fixture view; both fail60FPS |
| Native07 GLTEST05 fixed view,14.97s | 38.690FPS; min5s38.6FPS, CPU8.37ms, swap18.02ms, p9926.43ms | Roughly50% gain over06 with depth-clamp and clip-loop guard changes together; still fails60FPS |
| Native08 GLTEST05 fixed view, `gl_dynlight_shader=1` | 51.589FPS at stock clocks | Short diagnostic remains below60FPS; unchanged default requires explicit shader setting for comparison |
| Native09 GLTEST05 fixed view, `gl_dynlight_shader=1` | 51.359FPS at stock clocks | No measured gain over08; fails60FPS |
| Native09 MAP07 quiet fixed view | 64.469FPS; p9923.082ms | Average above60 does not establish smooth60 or combat acceptance |
| Native09 MAP07 combat,90s | 22.599FPS; min5s9.8FPS, p99110.308ms, max220.245ms; main CPU30.109ms, swap14.484ms | Fails sustained60FPS; god/evdev diagnostic route is not deterministic |
| Native09 MAP07 incoming,30s per mode | Direct27.984FPS; deferred29.325FPS | Existing `gl_direct_state_change=0` probe; still fails60FPS |
| Native09 MAP07 rockets,30s per mode | Direct14.050FPS; deferred20.570FPS; main CPU53.727→32.046ms; draws406.0→179.5/frame | Approximately46% gain with full effects; source fix and release qualification remain separate |
| Matching legacy02 GLTEST05 fixed view,15.01s | 52.707FPS; min5s52.6FPS, CPU13.53ms, swap0.34ms, p9920.38ms | Faster than current native implementation; still below60FPS target |
| Legacy02 MAP01 fixed view,14.99s uncapped | 77.369FPS; min5s77.2FPS, p9921.93ms;210 frames exceed16.667ms | Above60 average does not establish smooth60, sustained combat or release acceptance |

Fixture comparison uses same camera, content and launch flags. Legacy default
SM3/FPE versus native SM4 differs internally: this measures implemented backend
cost, not isolated API overhead or identical pixels.

Owner observed peaks near43FPS and explosions near15FPS on native05; these
observations are not fixed-route benchmarks. Warm startup records218 shader
cache hits, zero source compilations and0.4–0.5s shader setup. Clean native05
package passes28 managed-file hashes and contains no diagnostic preload;
isolated device runner carries recorder/profiler instrumentation separately.
Native06 fixture capture records GPU load61–65% at400MHz and temperature
61.7–66.5°C; these samples do not establish GPU timing or thermal-soak pass.

Particle-batching probes use fresh seeded MAP07 launches, DBOPL music, the same
private light/call profiler and frame recorder, and cooldowns before each run.
Combat evolves, so these are representative routes rather than frame-matched
deterministic comparisons. Direct/deferred game samples show66.5–72.9°C,
CPU maximum1512MHz, CPU cooling state0 and GPU400MHz. A separate hot frontend
reading of81.5°C/max1296MHz does not establish gameplay throttling. Recorder
now includes CPU frequency ceiling, cooling state and GPU temperature.
Private64-buffer pool test showed no useful speedup; no pool change retained.
Deferred-mode repeat measures20.783FPS with original upload hint and20.038FPS
with `GL_STATIC_DRAW`; no static-hint change retained. These evolving combat
routes do not establish a causal regression from the hint.

Native10 source defaults native pipeline state to deferred application, commits
flat-light blend state before drawing, and invalidates caches on context reset.
Both runtime cvar modes retain coherent desired/applied state. Actual production
render-state tests pass82 native and27 desktop checks per Mesa GLES2.0/3.2
context;64 matching particles or additive sprites batch into one draw with exact
full-image equality. Desktop fixture uses GLES transport to check preprocessor
behavior; this is not desktop-driver or handheld qualification.

Testing resumed2026-10-19 after reboot. Fresh target-compiled frame recorder
is identical across today's09/10/11 captures; earlier profiler-loaded timings
are separate diagnostics. Native10 compiles after native-only render-state
header inclusion in framebuffer setup; matching native/legacy exports share
2926 source-input hashes. Isolated upgrades preserve57 stock/IWAD hashes and
candidate config/saves. Native11 adds bounded8-light opaque sibling with
unchanged128-light fallback; neither build is release-qualified.

| Recorder-only diagnostic,30s per phase | Native09 direct | Native10 deferred | Native11 bounded8 |
| --- | ---: | ---: | ---: |
| GLTEST05 fixed scene, FPS | Earlier51.359 |51.574|51.584|
| MAP07 incoming monsters, FPS |28.691|30.279|29.498|
| MAP07 rockets, FPS |18.499|22.979|23.402|
| MAP07 rockets, mean main CPU ms |36.838|26.967|26.320|

MAP07 trials use same seed/route, god mode, DBOPL1 and full effects at stock
governors, but wall-clock evdev events and evolving combat prevent frame-matched
causal A/B claims. Today's09/10 evidence supports lower CPU cost with batching;
11 timing does not establish bounded-shader gain. Both explosion results remain
below revised30FPS target. Fixed scene screenshots inspected without obvious
glitches; animated scene and unmatched capture tics prevent exact device-image
parity claim. Native11 integration retains82 native/27 desktop render-state
checks per Mesa context. Bounded-light private harness passes1994 checks,
1008 exact full-image comparisons and239 independent/driver shader links per
GLES2.0/3.2 context; disabled-range-gate negative control fails457 checks.
Ordinary gun/shotgun combat and sustained revised-target qualification remain
open. Raw captures/build receipts remain private outside checkout.

Native11 owner-played MAP01 pistol/shotgun capture lasts89.998s and averages
60.479FPS, but minimum1s/5s windows fall to34/39FPS. P99 frame time is34.205ms,
maximum48.449ms, and29.326% of sampled1s windows miss60FPS. Owner reports
noticeable stutter with clean audio; this does not pass ordinary-combat target.
Gameplay thermal samples span59.09–63.75°C with CPU ceiling1512MHz and cooling
state0 throughout; no active CPU thermal restriction observed in this capture.

Native12 reuses depth-clipping scratch storage without changing clipping math,
attribute interpolation or vertex order. Frozen original/new production adapters
produce16,644,240 identical vertex bytes across1536 comparisons; warm heap
allocations drop from83,926 to0 for that corpus. ASan/UBSan pass; original
allocation-heavy routine fails expected allocation gate. Renderer checks remain
1994 per GLES2.0/3.2 context, with82 native/27 desktop render-state checks.
Matching native/legacy exports share2928 source hashes; isolated install
preserves57 stock/IWAD files and candidate config/saves.

Native12 recorder-only MAP07 incoming averages33.820FPS and rockets24.088FPS.
Rocket minimum1s/5s windows are17/18.6FPS, P9964.708ms, maximum71.456ms.
These evolving routes cannot establish isolated speedup over native11 and still
miss30FPS explosion target. CPU ceiling remains1512MHz/cooling0 during capture.
Native12 still needs owner-played ordinary combat and release qualification.

Native13 caches physical attribute enables/pointer layouts separately from
caller arrays. Buffer name, offset, type, size, stride and normalization all
participate; buffer deletion and backend initialization invalidate associations.
Constant attributes and per-batch storage replacement retain original behavior.
Private native12 cache probe forwards8 of2,361,416 pointer requests and8 of
2,361,416 enable requests in MAP07. Cache-on rockets average25.634FPS versus23.536FPS with
same probe disabled; mean main CPU22.459 versus26.183ms. Combat still evolves,
so this supports integration testing, not isolated causal or30FPS acceptance.
Production adapter passes2066 checks per Mesa GLES2.0/3.2 context, including
four attribute-layout runs across context recreation; ASan/UBSan pass. Frozen
native12 negative control fails16 efficiency checks, and removing buffer-name
comparison fails8 checks including visible geometry. Native13 builds and installs
with matching native/legacy source inputs. Production MAP07 incoming averages
33.894FPS and rockets23.685FPS, minimum1s/5s18/20FPS. Private-probe improvement
does not carry over to this production trial; explosion target remains unmet.

Native13 owner MAP01 recording averages63.065FPS over89.986s, minimum1s/5s
49/57.2FPS, P9926.477ms and maximum50.444ms. Owner reports noticeable stutter
with clean audio. Recorded ZDEM input shows first activity at66.657s and only
25.114s of active commands across95.257s demo, so whole-capture average includes
substantial idle time and cannot qualify sustained combat or establish gain over
native11. CPU ceiling1512MHz/cooling0 throughout, SOC57.727–64.166°C. Immutable
demo retained privately for active-section real-time replay comparisons; no
timedemo-throughput result substitutes for normal frame pacing. Release and
60FPS ordinary/30FPS explosion qualification remain open.

Native13 repeated-input diagnostic uses immutable owner ZDEM active section,
65s warmup plus25s recording. Native12 averages62.592FPS and native13 63.248FPS;
short diagnostic does not qualify60FPS combat or establish material improvement.
OpenAL forwarding probe on native13 measures350 OPL refills during24.994s.
Refill synthesis costs10.436ms mean main CPU;64 of67 frames longer than25ms
contain a refill. Probe does not change PCM, format or buffer sizes. Normal-play
stutter correlates strongly with synchronous DBOPL synthesis.

Native14 candidate moves only melodic DBOPL synthesis/DC correction to one
worker. MIDI parsing, event ordering, CVars, controls, OpenAL and gameplay RNG
stay main. Eligibility: native GLES/OpenAL, actual core1 OPL3 MIDI instance,
49716Hz stereo float, original3551-frame buffers and four-buffer queue. Raw OPL,
other cores/backends and dumpers stay synchronous. `opl_async` defaults true,
latched per device open; false selects reference path. Four initial prefills
stay synchronous; immutable command jobs replace only processed buffers, with
no extra speculative callbacks. FIFO completions upload on main. Stop drains
and joins before chip destruction. Unexpected rhythm drains/joins before global
RNG use, then instance stays synchronous; allocation/thread failure falls back.
`stat music` includes worker/pending/underrun status for prepared streams.

Actual DBOPL/production service-loop harness passes1577 PCM/order/lifetime checks,
including looping and EOF with one/two/four chips; initial prefills and synchronous
resume match frozen pre-worker service/DC routines bit for bit. Engine shell and
scripted event producer are fixtures, not complete MIDI-parser coverage. Queue
harness passes404 checks, and production OpenAL stream passes198 delayed-job,
stopped-queue, pause, EOF/error and fallback checks. Actual pthread-create failure
passes3 checks. ASan/UBSan pass; full-emulator ThreadSanitizer exposed shared
`generator_add` writes during reset. Fixed-rate increment is now immutable, with
same arithmetic; full-emulator ThreadSanitizer then passes. Disabled rhythm guard
and reversed panning fail expected negative controls.

Native14 actual MAP01 MIDI/OpenAL integrity probe matches204 synchronous buffers
byte for byte (5,795,232 PCM bytes); worker status reports zero underruns in
short idle diagnostic. Instrumented capture does not qualify frame pacing.
Frame-recorder-only active-section worker-off repeats average62.462/60.590FPS;
worker-on trials average53.505/73.179/71.641FPS. First slow worker trial remains
unexplained and retained. Later two worker trials have minimum5s67.4/65.2FPS,
P9922.678/23.235ms, versus synchronous minimum5s57.4/55.4FPS and
P9928.157/29.878ms. All use same immutable input, native640x480, full effects
and stock governors. Each active window lasts25s; none qualifies sustained60s
combat. Owner observes smooth later replay with remaining health-bonus pickup
stutter. Native14 representative MAP07 diagnostic now measures38.915FPS incoming
and32.748FPS rockets; rocket minimum1s25/minimum5s30FPS, P9948.539ms/max79.685ms.
Each phase lasts30s; seeded live evdev route is not deterministic frame-matched
input. This supports progress toward rough30FPS heavy-load target, with brief
dips remaining. Audio listening, sustained combat and release qualification stay
open.

Private pickup probe records six health-bonus events in same owner replay.
Message output/flush and sound uploads each remain below0.13ms; first cluster's
400ms event-aligned window averages21.023ms/frame, with most wait inside swap.
Temporary solid-quad bypass verifies65 skipped draws and reduces that window to
15.776ms/frame. First bypass attempt lost environment flag across systemd launch;
no draws skipped, so retained as ordinary-flash repeat (22.086ms/frame), not
valid bypass control. Temporary marker replaces flag, removed in helper finally.
Skipping flash is diagnostic only. Probe adds overhead, and replay loading skips
normal level precaching; these results do not establish all ordinary-play hitches.

Native15 retains visible pickup/damage blends using color-only basic shader
sibling. Eligible only with texture0/burn off, alpha/clip/depth-clamp/line-smooth
off and texture mode0; all other states retain generic basic shader. Logical
program0 remains unchanged, physical programs keep separate uniform residency,
and both programs reset with context lifecycle. Actual adapter harness passes
3862 checks on Mesa GLES2.0 and3.2 with239 material/fixture links, including144
exact full-image solid/generic comparisons in each of four context lifecycles,
plus1008 dynamic-material comparisons. ASan/UBSan and profiler-chain smoke pass.
Frozen native14 adapter fails eight expected sibling checks; disabled clip gate
fails state and pixel checks. Matching native15/legacy11 builds share2941 source
input hashes; legacy binary and shared PK3 remain byte-identical to native14
package. Device probe finds no solid-sibling draws: engine leaves depth clamp
enabled across fullscreen blends, excluding native15 optimization in practice.
Potentially disturbed native15 replay is excluded from controlled comparisons.

Native16 shares generic vertex shader with solid sibling, preserving both native
and emulated depth clamp; only fragment shader drops texture/clip branches.
Depth clamp no longer excludes solid draw; texture/alpha/clip/line-smooth and
texture-mode guards remain. Expanded harness passes5590 checks each on Mesa
GLES2.0 under ASan/UBSan and GLES3.2 with independent glslang links, including
1152 exact full-image solid/generic comparisons across four context lifecycles
and depth-clamp states. Frozen native15 adapter fails580 expected assertions in
expanded depth-aware suite. Profiler-chain smoke passes. Native16/legacy12
exports share2941 source hashes; legacy executable and PK3 stay byte-identical.
Actual Mali probe verifies55 solid-sibling draws and six health bonuses with
visible effects retained. First400ms pickup cluster averages18.524ms/frame,
versus21.023ms native14 baseline; whole25s instrumented replay averages62.477FPS,
minimum5s58.8/P9928.168ms, slower overall than earlier native14 trials. CPU
ceiling remains1512MHz, cooling state0, sampled SOC66.5–68.5°C. Single run does
not establish overall gain; frame-only repeat and sustained gameplay remain open.

First two-phase native15 owner capture reports smooth motion, acceptable minor
stutter and clean audio, but recorded demo contains4124 empty tics and no user
commands; screenshot remains at spawn. Retained as idle/audio observation, not
sustained combat evidence. Repeat requested with explicit readiness announcement.
Owner accepts small rare pickup hitches; combat pacing and roughly30FPS heavy
effects remain priority.

[Adapter harness](../tools/r36s/tests/test-gles-renderer.py) passes740 checks
with zero failures on each Mesa GLES2.0 and3.2 context, links239 programs per
driver, and checks64 queued flushes before readback, dynamic-light early exit,
depth clamp, clip guard, opaque specializations and350 exact dynamic-material
image comparisons. Independent glslang links239 programs. Actual engine light
collector checks pass153 native sanitizer cases and43 desktop cases; old-source
negative control fails52 expected cases. These counts describe tests, not maps.
Fresh native09 and matching legacy05 exports share2920 source-input hashes and
all10 frozen renderer pins. Clean package07 verifies28 files; separate
recorder-only archive verifies30. Native GLES-only ABI and launcher recipe pass.
Parent deployed native09 binary SHA256
`e57751c7a91e2a28819ecba5aeecb8a377d7e3803e57ac28c9e58de0c94802df`
to isolated candidate, preserving57 stock/IWAD hashes and candidate config/saves.
Native09 device diagnostics above fail60FPS. Separate PC-sampling run at13.519FPS
includes profiler overhead and is not qualification. Limiter work is deferred;
no limiter or renderer-default change is included.
Fixed-view timing uses state profiling off, with no finish/EGL override; gain
cannot be assigned to one bundled change without isolated probe. These checks
cover adapter behavior and shaders, not complete engine scenes or handheld
performance.

Firmware SDL2 [pins swap interval to1](https://github.com/christianhaitian/rk3326_core_builds/blob/fb15446f87ded6d0bc0f7117d539e92462641dce/patches/sdl2-patch-0001-egl.patch#L39),
including requests for0; command-line vsync changes cannot establish an
uncapped comparison on this firmware. Recorder measures SDL swap boundary:
legacy renderer calls `glFinish()` before that boundary, while native renderer
flushes queued draws. Native swap time can include GPU completion previously
counted as legacy frame work; swap timings alone do not prove extra vblank wait.

Fixed-scene probes measure31.73FPS baseline,28.81FPS with diagnostic
`glFinish()`, and31.67FPS with direct EGL interval0 accepted. Finish moves wait
from swap into frame work without improving FPS; EGL0 yields no gain. No
production finish/EGL override retained. Native05 roughly32FPS fixed scene and
roughly15FPS explosion observation both fail requested60FPS outcome.


Native15 repeated owner-controlled MAP01 trace records90s/6286 frames at69.853FPS
average, minimum1s46/minimum5s57.6, P9926.442ms/max44.359ms. Strict ZDEM audit
confirms1589 changed user commands,3150 movement tics and934 attack-held tics
across107.086s demo. Owner reports smooth play with noticeable stutter and clean
audio. This demonstrates progress but fails strict sustained60FPS timing gate.
Second capture invocation reached already-exited game and overwrote route JSON;
retained caveat distinguishes failed invocation from independently validated
original CSV/metadata/demo. Duplicate/stale-capture guard added to private helper
before cleanup/receipt ownership. No performance evidence discarded or relabeled
as a pass.


Native16 real owner-controlled MAP01 capture records90s/6365 frames at70.718FPS,
minimum1s43/minimum5s55.2, P9928.477ms/max44.144ms. Strict input audit confirms
1772 changed commands,3446 movement tics and1141 attack-held tics across111.343s
recorded demo. Owner reports smooth play with slight shooting/aiming stutters,
and barely noticeable turning hitch. Existing mouse settings remain unchanged.
Same-build replay of that recording measures71.238FPS, minimum1s43/minimum5s56.6,
P9926.355ms/max44.295ms over90s; replay window and original live window begin at
different demo offsets, so not a frame-matched causal comparison. Earlier replay
of native15 input on native16 averaged73.265FPS/minimum5s59.4 and looked smoother
to owner; cross-build observation does not establish live acceptance. Long live
frames over25ms average7.522ms main CPU and21.436ms swap wait. Swap includes
GPU/driver/presentation waits and is not an independent GPU timer. All traces
remain below strict sustained60FPS gate despite average above70FPS.

Native16 fresh MAP07 stress captures38.150FPS incoming/minimum5s32.6 and
25.875FPS rockets/minimum5s22.6/minimum1s20, P9952.435ms/max57.799ms. Rocket
phase spends20.900ms main CPU and17.142ms swap per frame; roughly30FPS heavy
target fails. Strict recorded-input audit confirms700 attack-held tics but zero
movement tics despite requested analog strafing. Treat as stationary rocket
stress, not moving-combat acceptance or matched comparison with native14.
Both30s phases are short diagnostics; CPU ceiling1512MHz/cooling0, sampled
SOC67.3–72.5°C. Retain failure rather than attributing difference to temperature.

Native16 captures all six V2 fixed-camera fixtures with frame recorder only,
clean exit and frontend recovery after each. Manual desktop SM4 reference review
finds corresponding slopes/3D floors, skybox sprites, model/voxel color bands,
brightmaps/fog/lighting and both camera images. Model, warp and fuzz phases differ;
no exact image-equality, motion or complete feature-coverage claim. Static timing
ranges32.5–77.4FPS and does not qualify combat. Idle frontend sysfs advertises
single panel mode `640x480p77`; frame traces cluster near12.9/25.8ms in selected
views. Refresh-deadline misses could contribute to perceived stutter; advertised
mode and swap timing alone do not measure physical scanout or isolate GPU waits.
