# Direct GLES for R36S: build, device and release validation

## Current delivery decision —2026-10-19

Owner ended performance tuning, accepted smoother GLES gameplay with heavy
load dips, and requested merge into master with GLES default in R36S launcher.
Earlier strict60/30 gates below remain historical test verdicts, not current
delivery blockers. Hardware ceiling remains unproven; unsupported mod/feature,
thermal-soak and stock-server coverage must not be claimed complete.

Matched immutable stationary MAP07 demo, native16/native14/native16:28.601,
28.961,28.674FPS; minimum5s23.4/23.6/23.6FPS. No large regression reproduced,
no overall native16 gain established. Original requested strafing produced
zero movement tics; stress is not moving combat. Native16 live MAP01 averages
70.718FPS/minimum5s55.2; latest owner reports smooth play with slight stutter.

Diagnostic preload removed; clean native16 runner and managed hashes verified,
57 stock/IWAD files unchanged, config/saves preserved. Device `ldd -r` and261
SDL2-symbol checks pass without desktop GL/GL4ES mapping. Native16 six V2
fixed fixture views manually match corresponding desktop references, allowing
animation phase differences; other viewpoints remain outside coverage.

Default migration uses separate native config, once-only tested graphics
profile and current launcher selection backup. GL4ES/software remain selectable;
named profiles and later explicit renderer choices persist.

## Historical research and qualification record

## Which build and runtime changes establish a native GLES package?

### Takeaway
Native GLES uses existing pinned ARM64 Docker export and PortMaster lifecycle. Explicit GLES build mode selects native SDL2 context through same SDL2 module loaded by sdl12-compat, without GL4ES injection or firmware replacement. Platform/build path works on device; renderer release qualification remains blocked by performance and incomplete feature coverage.

### Cited Findings
- Client builder pins Debian image `sha256:d07d1b51c39f51188e60be9b64e6bf769fa94e187f092bc32b91305cfa34ba5a`, Debian snapshot `20261014T000000Z`, GCC14/CMake3.31, BuildKit digest, Linux ARM64 and `-march=armv8-a -mtune=cortex-a35 -fsigned-char`. Docker daemon currently reports `aarch64 linux`. Existing build snapshots tracked and nonignored untracked files, exports source/ABI/resources/checksums and uses checkout-specific builder. — [builder](../../tools/arm64/build.py), [Dockerfile](../../tools/arm64/Dockerfile), [build guide](../../docs/arm64-build.md)
- SDL1.2 GL backend sets RGBA8, depth24, stencil8 before SDL_SetVideoMode. Native build resolves context attributes and swap-interval functions from already-loaded SDL2 module, rejects bridge/context errors, queries actual context, and swaps once through SDL1.2 entry point. — [SDL GL backend](../../src/sdl/sdlglvideo.cpp)
- Stock sdl12-compat1.2.68 `SDL_GL_SetAttribute` rejects values beyond its SDL1.2 enum, whose final supported attribute is swap control; SDL2 context version/profile attributes cannot be passed through this API. Its actual context is created using SDL2 GL_CreateContext. With OpenGL scaling disabled, swap delegates directly to SDL2 GL_SwapWindow. — [pinned SDL source](https://github.com/libsdl-org/sdl12-compat/blob/release-1.2.68/src/SDL12_compat.c#L7401), [context creation](https://github.com/libsdl-org/sdl12-compat/blob/release-1.2.68/src/SDL12_compat.c#L5913)
- SDL2 requires GL attrs set before window creation and supports ES profile selection. Requested attrs need queried after context creation; drivers can choose different values. — [SDL_GL_SetAttribute](https://wiki.libsdl.org/SDL2/SDL_GL_SetAttribute), [SDL_GLattr](https://wiki.libsdl.org/SDL2/SDL_GLattr)
- Package retains SDL1.2 compatibility and legacy GL4ES/GLU/OpenGL/GLdispatch files for existing mode. Native executable links firmware GLESv2 without desktop GL/GLEW/GLU runtime dependencies; selected-mode checks inspect direct/transitive/dlopen resolution and SDL2 symbols. — [client Dockerfile](../../tools/arm64/Dockerfile), [package builder](../../tools/r36s/package.py), [runtime contract](../../packaging/r36s/DEPENDENCIES.md)
- Runner defaults GL4ES, sets wrapper paths only inside GL4ES case, selects firmware KMSDRM and disables compatibility scaling. Launcher accepts `gles`, `gl4es` and `software`; native path selects `zandronum-gles`, isolates base saves in `saves/gles/` and exports private shader-cache directory. — [runner](../../packaging/r36s/runner.sh), [launcher](../../tools/r36s/launcher/main.cpp)

### Implementation decisions
- `--gles` selects `ZANDRONUM_USE_GLES=ON`, `USE_GLES2=1` and separate artifact `zandronum-gles`. Native and legacy builds use distinct compiler-cache paths; package requires matching PK3 and SDL compatibility-library hashes. — [build/package commands](../../docs/r36s-gles.md#build-and-package)
- Same-module SDL2 bridge sets ES2 profile before SDL_SetVideoMode, queries actual context and calls SDL_GL_SetSwapInterval after creation. No silent translator fallback. Native mode avoids PortMaster GL4ES hook, wrapper paths and LIBGL_* settings; legacy/software remain available during qualification.
- Recorder retains SDL_GL_SwapBuffers boundary. Legacy `glFinish()` executes before recorded swap; native `gles::Flush()` submits batches before swap. GPU completion can therefore move between recorded work and swap categories; comparing swap alone cannot identify pacing regression. — [framebuffer boundary](../../src/gl/system/gl_framebuffer.cpp), [recorder](../../tools/r36s/frame-recorder.c)

### Gaps
- Actual device context confirms GLES3.2, GLSL ES3.20 and depth24/stencil8. Advanced-feature behavior, extension-dependent paths and sustained frame timing still require full acceptance matrix below.

## Is real R36S validation ready, and what baseline must be preserved?

### Takeaway
Initial read-only readiness probe passed; subsequent native03/native05 testing uses isolated candidate and packaged frontend lifecycle. Firmware and owner distribution remain intact. Current GL4ES geometry defects are fixed; heavy-scene timing remains limiting baseline.

### Cited Findings
- Read-only probe on2026-10-15 confirmed AArch64, frontend active, no client,640×480 framebuffer; firmware libEGL.so.1 and libGLESv2.so.2 resolve to `/usr/lib/aarch64-linux-gnu/libmali-bifrost-g31-rxp0-gbm.so`. Actual native startup later confirms Mali-G31 GLES3.2, GLSL ES3.20, depth24/stencil8 and swap interval1; loaded maps contain no GL4ES/desktop GL. — [device snapshot](../../docs/research/r36s-device-snapshot.md), retained device diagnostics
- Existing package issue8 built clean release source a9f3a50af127abf33d62862b44fe438832320936, carries common ARM fix ancestor5eee33fc1, final client SHA256 afb20febda42cd04d83d62c975151ad2671913be7599030cb00ff47cf709b185 and 291 managed files. Further physical new-launcher playthrough was explicitly not claimed. — [single-player release](../../docs/r36s-single-player-release.md)
- Missing MAP01 walls/floors came from negative atan2 conversion to unsigned angle_t on AArch64; fixed signed conversion plus CAC2 invalidation. Duplicate skies came from memcmp key padding versus memberwise copy; fixed full representation copy. Corrected actual-device geometry and sky-key regression already passed. — [renderer evidence](../../docs/r36s-rendering.md)
- Qualified GL4ES baseline uses640×480, music/effects enabled, DBOPL, standard clocks, lights enabled.90s Doom E1M1 average60.824/min5s32.8, DoomII MAP01→MAP02 average66.265/min5s33.6; MAP07 average43.162/min5s24.2 and long sub30 dips fails sustained30 gate. Static floor repro averages27.327FPS, CPU32.264ms/frame and swap0.377ms/frame; timings are CPU-side clues, not proof of single root cause. Software later passes representative native30 guided runs. — [performance evidence](../../docs/r36s-performance.md)
- Existing saved floor repro path is documented `logs/issue7/floor-repro.zds`; initial read-only check did not find it on cleaned package. Source evidence/raw saves remain archived separately from distribution. — [performance evidence](../../docs/r36s-performance.md), [final release](../../docs/r36s-single-player-release.md)
- Firmware build applies SDL2 KMSDRM patch that assigns `interval = 1` inside swap-interval setter; requests for0 succeed but remain1. This explains observed native interval1 after requested vsync0. — [pinned patch](https://github.com/christianhaitian/rk3326_core_builds/blob/fb15446f87ded6d0bc0f7117d539e92462641dce/patches/sdl2-patch-0001-egl.patch#L39), [pinned build script](https://github.com/christianhaitian/rk3326_core_builds/blob/187a08cdd28ba63d8abaf61e27bfe09e5885ba5f/scripts/sdl2.sh)
- Every launch must use detached package entry, stop ES first, recover ES on normal exit/failure/interruption/crash and preserve game status. Never launch binary directly beside frontend. — [required lifecycle](../../docs/portmaster-testing.md#required-emulationstation-lifecycle)

### Inferences
- Prepare snapshots of current managed hashes, live configs/profiles/preferences/saves, current executable/debug/resources/runtime IDs and frontend/process state before any deployment. Back up whole previous distribution separately, replace managed paths only, restore all owner data and diagnostics after testing.
- [Build/package sequence](../../docs/r36s-gles.md#build-and-package) uses separate native/legacy exports with identical PK3/SDL hashes. Keep private transport helpers, access notes and diagnostic archives outside exported source/package.
- Safe deploy uses archived checked package copied over `/roms2/ports/` without recursive delete. Verify `managed-files.sha256` and preserved-owner hashes before launch. Run installed checker under selected library env first; never apt/install/download dependencies on device.
- Qualification stages isolated `/roms2/ports/zandronum-gles-test` plus `/roms2/ports/Zandronum GLES Test.sh`, retaining packaged detached unit and relative runner behavior. Copy owner config into candidate, keep candidate saves separate, read owner IWADs from existing `/roms2/doom`. Hash original package owner data and IWADs before/after; no original managed file replaced. Recorder/profiler preload is applied only to diagnostic candidate runner, never clean package.

### Gaps
- Real context and runtime linkage now pass; complete extension-sensitive renderer coverage and matched native/legacy performance remain open. Firmware SDL path clamps interval1; direct EGL0 diagnostic succeeds but does not improve FPS. Firmware remains unchanged.
- Remote saved repro may have moved into archived diagnostic tar after cleanup; locate hash601a79861852e2cccd6b4daa2d1fb3ad6163413ec9a8718d1c02951b01457ae3 before fixed-camera comparison.

## Current owner target —2026-10-19

Owner revised workload targets to60FPS ordinary pistol/shotgun combat and roughly
30FPS heavy explosions, native640×480 with music/effects/lights and stock clock
settings. Rare small pickup hitches are acceptable; combat pacing remains
priority. Historical60FPS-all-effects requirements below describe earlier gate,
not current heavy-load target. Retain minima, frame percentiles and hitch duration
alongside averages; do not count idle traces as combat. Current status and raw
capture caveats appear in [native GLES record](../../docs/r36s-gles.md).

Native16 owner-controlled MAP01 averages70.718FPS over90s, minimum5s55.2 and
P9928.477ms; owner reports smooth play with slight shooting/aiming stutters.
Strict demo audit confirms movement/fire. Same-build replay averages71.238FPS,
minimum5s56.6, with different capture start offset. Stationary MAP07 rocket
stress averages25.875FPS/minimum5s22.6 over30s and fails current heavy target;
requested analog strafe produced no movement commands. Native16 six-fixture V2
fixed-view review passes captured desktop-reference views with animation-phase
caveat and clean frontend recovery. These checks leave sustained pacing,
moving heavy combat, remaining viewpoints/lifecycle/network and soak open.

## What counts as complete release acceptance?

### Trusted desktop visual-reference route
- Matching legacy03 ARM64 desktop client runs on real Mesa25.0.7 llvmpipe GL4.5 compatibility, GLSL4.50; source selects shader model4 for GLSL>=1.3. This is desktop GL reference, separate from GLES adapter harness and device GL4ES SM3/FPE baseline. Actual loaded libraries contain Mesa GLX/desktop GL and no GL4ES. — [desktop context selection](../../src/gl/system/gl_interface.cpp), [capture tool](../../tools/r36s/tests/capture-desktop-fixtures.py)
- Existing disposable Debian13 ARM64 fixture-parser container already contained Mesa GL/SDL2. Container-only additions include `libglu1-mesa`9.0.2-1.1+b3, `xdotool`1:3.20160805.1-5.1 and `mesa-utils`9.0.0-2+b2. Runtime uses Mesa25.0.7-2+deb13u1, SDL2.32.4+dfsg-1, export's pinned sdl12-compat1.2.68 and Xvfb2:21.1.16-1.3+deb13u4. No handheld firmware/package installation.
- Pure SDL `offscreen` successfully creates desktop GL context and reaches fixture READY. Reusable capture uses Xvfb640×480 plus SDLx11, waits ACS fixed-camera READY, sends bound F12 for in-engine PNG and clean quit. All six generated fixture cameras captured successfully with exit0; binary/resource/fixture/config/screenshot hashes, actual GL strings/libraries and graphics-CVAR whitelist retained. Licensed IWAD and source native07 config remain private, read-only inputs outside Git; copied config/output stay private.
- Native07 GLTEST05 brightmap white dots agree with SM4 desktop reference. GLTEST04 KVX front is gray on both; current horizontal GLTEST03 view shows brick ceiling band on both and does not cover colored skybox sprites. GLTEST01 slope/egg and GLTEST06 two camera monitor images render in desktop reference. These observations guide visual comparison; they do not establish all feature gates. Animated warp/model/fuzz phases differ between captures, preventing unqualified pixel-equality claims.
- Static center-region comparison for GLTEST01–03 records99.80–99.95% pixels within2 RGB levels between Mali07 and desktop reference, using normalized RGB and rectangle[0,64,640,384]. This excludes HUD/animated scenes and leaves camera coverage gaps; edge outliers and missing viewpoints prevent full visual-acceptance claim.
- Separate fixture V2 corrects GLTEST03 sky-camera pitch/raised sprites and GLTEST04 voxel palette. Trusted matching legacy04 desktop GL captures show four skybox sprites/checker source room above foreground walls and distinct RGB voxel bands, both clean quit0. Matching native08 Mali V2 captures confirm both fixed views, with two screenshots per map and clean exit/frontend recovery. Fixture SHA256 `f6c4d64e49cdfd9f5fc5e2eadfc994c4e6df27bfab7c6c73e8474c861a186901`; V1 remains immutable and GLTEST05 bytes unchanged. Other viewpoints, motion and full feature coverage remain open. Capture tool supports repeated `--map` selection for focused references.

### Takeaway
Native acceptance requires sustained60FPS, including representative combat, at640×480 with music/effects/lights enabled. Compilation, context creation and one map cannot establish finished renderer. Require correctness coverage of every migrated feature, native device comparisons, sustained frame-time traces, lifecycle/persistence/audio checks and stock-server compatibility; retain explicit failures until fixed. Historical sustained30 policy does not qualify native release.

### Cited Findings
- Historical legacy timing policy used30FPS threshold; retain its results only as baseline reference. Latest owner clarification on2026-10-15 requires60FPS for native GLES. New native captures must use `python3 tools/r36s/frame-stats.py RUN.csv --target 60 --min-duration 60 --output RUN.stats.json`, with90s gameplay routes. Report1s/5s minima, p95/p99/max frame time, rolling dips and hitch duration; recorder retains in-memory timing without interval writes. — [legacy reference](../../docs/r36s-performance.md), [recorder](../../tools/r36s/frame-recorder.c), [analysis](../../tools/r36s/frame-stats.py)
- Existing full acceptance includes launch/control/menu/text/save/reload/normal-map transitions, music/effects, content profiles, renderer save isolation, missing-content cancellation, clean restart and frontend recovery. Prior owner acceptance does not establish changed GLES backend. — [PortMaster tests](../../docs/portmaster-testing.md), [single-player checks](../../docs/r36s-single-player.md), [audio checks](../../docs/r36s-audio.md), [launcher checks](../../docs/r36s-launcher.md)
- Renderer port must retain Zandronum3.2.1 identity/gameplay/protocol; stock-server acceptance still belongs to currently unverified multiplayer phase. — [release record](../../docs/r36s-single-player-release.md), [scope](../../docs/specs/zandronum-3.2.1-arm64-r36s.md)

### Inferences
| Gate | Exact coverage / retained evidence |
| --- | --- |
| Build provenance | Clean pinned rebuild; binary/resources/debug/source hashes; exact upstream reuse/license notices; API link evidence; source identity4 tests; frame-analysis6 tests; shell/Python/whitespace checks; package checksum validation |
| Genuine native GLES | Startup logs actual GLES and GLSL strings/config; `/proc/PID/maps` contains firmware Mali and no GL4ES wrapper; no desktop GL entry calls or silent translator fallback; shader compile/link failure fatal with diagnostic |
| Base-world correctness | DoomE1M1/E1M2, DoomII MAP01/MAP07/MAP08 and both owner glitch views; move/turn/pitch; complete walls/flats/sky, near-plane clipping, transparent/masked midtextures, sprite occlusion, weapon/HUD/palette/menus/automap, fog/colormaps/fullbright, texture translations/animations/filtering/mips |
| Advanced world | Reproducible Zandronum-compatible WAD/PK3 fixtures for 3D floors/slopes, stacked sectors, mirrors/skyboxes/line+plane portals with stencil recursion, translucent/reverse subtract/additive sprites and decals, brightmaps/glow/dynamic lights, model transforms/interpolation, camera textures/FBO/readback/screenshots, wipe and fullscreen effects. Compare legacyGL captures same position/settings; do not accept missing effect as optimization |
| Performance | Sustained60FPS acceptance target,16.667ms frame budget, native640×480 with DBOPL audio/effects/lights retained; matched GL4ES/GLES clocks/settings/content hash.90s freshE1M1/MAP01/MAP07 combat each; analyze explicitly with `--target 60`, report minima/percentiles/dips rather than average alone. Fixed floor diagnostic separately; cold-start first-use shader/texture spikes, hot-run and transition/load spikes; at least20min thermal/memory soak |
| Device behavior | Full handheld controls/actions and chord, main/menu/save name entry; independent mute and volume; save/load with existingGL saves and newGLES saves; profile save isolation; map transitions; renderer preference, config/audio preserved across launch and package upgrade; repeated launch/exit; cancellation/missingIWAD/shader failure/client kill/runner stop recover ES and remove helper |
| Network | Same release ID/net protocol; local stock3.2.1 server and second stock client: join, movement/weapons/projectiles/chat, map changes, spectate and disconnect/rejoin; no desync under combat. Renderer fixture content must match server; arbitrary public mod compatibility not inferred |
| Offline delivery | Copy-only package, ownerIWAD separate; selected-mode direct/transitive/dlopen ABI resolution and shader/resource completeness; no handheld manager/download/compiler dependency; manifest and source/build/license evidence retained; no debug interposer/profiler/credentials or owner content shipped |

- Native evidence must identify build/content/runtime IDs, dates, settings, map/camera/route, duration, rawCSV+metadata+hash, visual/audio/control observer and exact failures. Distinguish automated replay from owner physical hearing/feel. Device screenshots/loopback audio alone cannot prove physical speaker or controls feel.
- Native acceptance target60FPS applies to sustained combat and effects, not only fixed scenes or short averages. Historical30FPS threshold is not native gate; preserve640×480, music/effects/lights and renderer correctness throughout optimization.

### Qualification status —2026-10-15
- **Release blocked.** Native03 renders base world on actual Mali-G31 GLES3.2, with no GL4ES/desktop GL mapping. SELECT+START exits status0 and restores frontend. ARM64 compile/export, same-branch desktop export,28-file matched package hashes, device `ldd -r` and261 SDL2 symbol checks pass. These checks do not establish full renderer correctness.
- Native03 MAP01 capture averages1.73FPS; separate call profile averages2.09FPS,478.44ms/frame. Approximately59 `glBufferSubData` calls/frame consume401.04ms. Native05 replaces streaming storage per batch with `glBufferData`; measured upload cost falls to0.89ms/frame, with no SubData in recorded route. — [stream upload](../../src/gl/system/gles_compat.cpp)
- Native05 moving/combat diagnostic captures392 frames over14.96s:26.21FPS average, min1s18FPS, min5s24FPS, p9551.49ms/p9963.22ms/max66.48ms. Main-thread CPU13.26ms/frame; measured swap23.63ms/frame. This fails60FPS target. Owner observes peaks near43FPS and explosions near15FPS; observation is not controlled benchmark. Short instrumented capture cannot replace90s release routes or thermal soak; historical diagnostic JSONs using target30 do not establish native acceptance.
- Warm native05 startup records218 cached shader programs, zero source compilations and0.4–0.5s shader setup. Clean native05 package verifies28 managed-file hashes and has no recorder/profiler preload. Isolated test runner adds diagnostic instrumentation separately; release binary/resources remain unchanged.
- Actual-adapter Mesa harness passes740 checks/zero failures on each GLES2.0 and3.2 driver, links239 requested programs per driver and independently links239 with glslang. Streaming regression queues64 flushes before readback, checks every retained color/geometry/matrix cell, zero SubData, exact upload sizes and restored caller VBO. Later checks cover dynamic-light early exit, depth clamp, clip guard, opaque specializations and350 exact dynamic-material image comparisons. Actual engine light collector passes153 native sanitizer cases and43 desktop cases; old-source negative control fails52 expected cases. This is adapter/shader/collector coverage, not740 engine maps or proof of device performance. Fresh native09/matching legacy05 exports share2920 source-input hashes and all10 frozen renderer pins; clean package07 verifies28 managed files, separate recorder-only candidate30. Native GLES-only ABI and current launcher recipe match. Parent deployed isolated native09 SHA256 `e57751c7a91e2a28819ecba5aeecb8a377d7e3803e57ac28c9e58de0c94802df`, preserving57 stock/IWAD hashes and candidate config/saves. No limiter or renderer-default change included; limiter work deferred. — [harness](../../tools/r36s/tests/test-gles-renderer.py), [checks](../../tools/r36s/tests/test-gles-renderer.cpp)
- Firmware clamps requested swap interval0 to1. Legacy `glFinish()` runs before recorded SDL swap boundary; native flush submits queued work before swap. Native swap can include GPU completion counted outside legacy swap, so23.63ms versus older0.377ms swap is not evidence alone of extra vblank wait. Preserve full-frame and CPU timing alongside swap while diagnosing pacing.
- Native05 fixed-scene probes: baseline31.732FPS/10.950ms main CPU/20.178ms swap; diagnostic `glFinish()`28.808FPS/12.837ms CPU/0.368ms swap; direct EGL0 accepted (`EGL_SUCCESS`, config min0/max1)31.666FPS/12.016ms CPU/20.020ms swap. Finish moves completion wait outside swap without increasing FPS; EGL0 gives no improvement. These probes indicate GPU completion dominates previous swap time; no production finish/EGL override retained. Approximately32FPS fixed scene and15FPS explosion observation both fail60FPS outcome.
- Native06 dynamic-light loop early exit gives no measured gain in matched GLTEST05 view: native05 and06 both25.793FPS. Native06 captures386 frames/14.965s, p9941.242ms/max42.639ms, main CPU7.830ms/swap30.042ms; target60 fails. GPU load samples61–65% at400MHz, temperature61.7–66.5°C. Same camera/argv/resources and image checked; short fixed scene remains diagnostic, not combat or thermal acceptance.
- Native07 GLTEST05 captures579 frames/14.965s,38.690FPS average/min5s38.6FPS, p9926.429ms/max29.355ms, main CPU8.367ms/swap18.015ms. Roughly50% gain over06 includes depth-clamp fix and inactive-clip-loop guard together; no isolated attribution. State profiling off, current diagnostic-profiler/frame-recorder chain, no finish/EGL knobs. All frames remain over16.667ms; target60 fails. Isolated install preserves57 stock/IWAD file hashes and candidate config/saves; full six-fixture device visual checks remain open.
- Native08 GLTEST05 fixed view with explicit `gl_dynlight_shader=1` measures51.589FPS at stock clocks. Short diagnostic still fails60FPS target; defaults remain unchanged. Clock probes do not establish sustained combat/thermal acceptance or justify release governor changes.
- Native09 GLTEST05 with explicit shader lights measures51.359FPS at stock clocks: no gain over08. MAP07 quiet view averages64.469FPS but p9923.082ms;90s combat averages22.599FPS, min5s9.8FPS, p99110.308ms/max220.245ms, main CPU30.109ms/swap14.484ms. Sustained60FPS target fails. God/evdev route is diagnostic and not deterministic; separate13.519FPS PC-sampling run includes profiler overhead and cannot qualify release.
- Matching legacy02 GLTEST05 captures791 frames/15.007s,52.707FPS average/min5s52.6FPS, p9920.382ms/max21.238ms, main CPU13.531ms/swap0.343ms. Same launch flags, fixture and camera; legacy default SM3/FPE versus native SM4 means implemented-backend cost comparison, not isolated API overhead or identical pixels. Legacy is faster in this fixture; both miss60FPS target.
- Legacy02 MAP01 fixed view captures1160 frames/14.993s uncapped:77.369FPS average/min5s77.2FPS, p9921.933ms/max24.120ms, main CPU8.674ms/swap1.310ms;210 frames exceed16.667ms. Above60 average does not establish smooth60FPS, representative combat or release acceptance; retain uncapped/fixed-scene caveat.

### Remaining gaps
- Locally generated fixture tools now exist, but complete base/advanced-feature scenes have not passed on R36S. Require engine-supported maps, scripted cameras and matched legacy captures for full matrix above. — [fixture generator](../../tools/r36s/tests/make-gles-fixtures.py)
- Sustained performance, thermal stability, full audio/controller/save/profile/upgrade acceptance and stock-server multiplayer remain unqualified. Native09 build/package and isolated deployment checks pass, but actual combat fails60FPS. Selected native08 V2 fixed views match trusted desktop reference; full six-fixture visual/device coverage remains open. Candidate is not finished release.
