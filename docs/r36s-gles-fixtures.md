# Native GLES scene fixtures

`tools/r36s/tests/make-gles-fixtures.py` generates six small renderer test maps,
procedural RGBA textures/sprites, animated MD2/MD3 cubes, KVX block, GLSL material,
brightmap and ACS camera scripts. Generated assets and generator carry CC0-1.0.
No game assets are copied. Supply independently licensed Doom II-compatible IWAD.
These fixtures supplement original game/save coverage; passing them alone cannot
qualify gameplay, networking, package lifecycle or sustained performance.

## Build

Python 3 standard library plus official [ZDoom ACC](https://github.com/ZDoom/acc)
required. Reference compiler commit:
`bdb9bc4d2c5aee7ca3ff8da985d73aaf83af0557`.
Compiler source/build remains host-side; no firmware installation required.

```sh
git clone https://github.com/ZDoom/acc.git /tmp/r36s-fixture-acc
git -C /tmp/r36s-fixture-acc checkout bdb9bc4d2c5aee7ca3ff8da985d73aaf83af0557
cmake -S /tmp/r36s-fixture-acc -B /tmp/r36s-fixture-acc/build
cmake --build /tmp/r36s-fixture-acc/build
python3 tools/r36s/tests/make-gles-fixtures.py \
  --acc /tmp/r36s-fixture-acc/build/acc \
  --acc-include /tmp/r36s-fixture-acc \
  --output /tmp/r36s-gles-fixtures-v2.pk3
```

Generator refuses to overwrite an existing archive. It also fails if
compiler/includes missing or ACS compilation fails. Every
build includes compiled BEHAVIOR and original ACS source. Output ZIP uses sorted
paths, fixed timestamps/permissions and stored entries. PNG compression and ACC
version also affect archive bytes: reproducibility comparison requires same
Python/zlib and pinned compiler. `fixtures.json` stores all map views, expected
features, structure counts and SHA256 of each other archive member.
Output PK3 should stay outside version control and regular release package.

V2 improves skybox and voxel visibility. GLTEST03's fixed camera looks up
33.75 degrees; raised skybox sprites stand above SkyViewpoint, with checker
walls behind. KVX uses distinct red/green/blue palette entries and exterior
face masks. V1's three stripe indices accidentally shared one gray RGB value.
Only `fixtures.json`, `maps/GLTEST03.wad` and `voxels/fixture.kvx` change from
qualified V1; all GLTEST05 scene/assets/camera bytes and map metadata stay exact.
Preserve V1 for ongoing performance comparisons.

```sh
python3 tools/r36s/tests/check-gles-fixtures.py \
  --fixture-pk3 /tmp/r36s-gles-fixtures-v2.pk3 \
  --baseline-pk3 /tmp/r36s-gles-fixtures.pk3 \
  --repeat-pk3 /tmp/r36s-gles-fixtures-v2-repeat.pk3 \
  --report /tmp/fixtures-v2-structure.json
```

Generate repeat archive with identical compiler/Python before this check.
Checker validates map/asset structure, KVX exterior slab faces and distinct
saturated palette colors, sky view instructions, deterministic bytes and exact
allowed V1 differences. Baseline must match qualified V1 hash below.

## Actual engine parser gate

Run generated archive through actual client before device deployment. Structural
archive checks cannot catch engine-specific ANIMDEFS or DECORATE grammar. This
gate checks every asset hash, launches all six maps, records engine/IWAD/fixture
hashes and per-map logs, and requires ACS `GLES-FIXTURE READY`. Client process is
stopped after marker; this gate does not qualify clean quit or visual correctness.
Keep licensed IWAD read-only and outside source/release artifacts.

```sh
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy LIBGL_ALWAYS_SOFTWARE=1 \
python3 tools/r36s/tests/test-gles-fixtures.py \
  --engine /path/to/zandronum-gles \
  --iwad /private/readonly/doom2.wad \
  --fixture-pk3 /tmp/r36s-gles-fixtures-v2.pk3 \
  --output-dir /tmp/gles-fixture-parser-evidence --renderer gles
```

Linux host/container needs SDL1.2 compatibility library, SDL2 offscreen driver,
Mesa EGL/GLES and normal client runtime dependencies. Native mode initializes
GLDEFS and all material variants, including custom fixture shader. Software mode
(`--renderer software`, `SDL_VIDEODRIVER=dummy`) also checks ANIMDEFS, DECORATE,
MODELDEF, KVX, MAPINFO, UDMF and ACS, but does not initialize GLDEFS/shaders.
Mesa/X11 under Xvfb may reject swap-interval requests; offscreen driver avoids
that display-specific limitation for this parser gate.

2026-10-15 evidence: ARM64 native06 client on Mesa ES3.2 offscreen loaded all six
maps and linked 236 material/effect programs without parser or missing-asset
errors. Logs/hashes: [fixtures-native06](../reports/gles-local/fixtures-native06/results.json).
Qualified archive SHA256:
`ab485c476d4be67315730e9f7e1d4c3b14c717adf4931d7c901d94019041cae2`.
This replaces earlier archive with invalid `speed` keyword in ANIMDEFS and inline
DECORATE state terminators. Warp speed uses bare float; Stop/Loop begin own lines.

V2 SHA256:
`f6c4d64e49cdfd9f5fc5e2eadfc994c4e6df27bfab7c6c73e8474c861a186901`
(151535 bytes). ARM64 native08 on Mesa ES3.2 offscreen passed all six actual
map/parser/context checks, initializing 237 material/effect programs. GLTEST03
also requires runtime marker `GLES-FIXTURE SKY PITCH -6144`, confirming ACS set
camera pitch before READY. Evidence:
[parser results](../reports/gles-local/fixtures-v2-native08/results.json),
[deterministic/structure checks](../reports/gles-local/fixtures-v2-structure.json).
This gate checks loading and runtime camera state; visual device acceptance
remains separate.

Trusted desktop V2 reference: matching legacy04 with Mesa desktop GL4.5/SM4
shows four sprites, checker sky-room walls and blue/green ceiling above
foreground brick in GLTEST03. GLTEST04 KVX front shows distinct red/green/blue
bands. Both runs quit cleanly with exit0. Screenshots and metadata stay outside
Git in `zandronum-arm64-artifacts/gles/desktop-legacy04-v2-reference/`.
Actual R36S native08 V2 fixed views now show these intended features too:
GLTEST03 checker sky-room/ceiling and four colored sprites; GLTEST04 KVX front
with distinct RGB bands. Parent reviewed two captures per map, six seconds
apart, then confirmed clean exit and restored frontend. Runtime identifies
Mali-G31 GLES3.2, 640x480, depth24/stencil8 and 237 cached programs. This closes
these two fixed-view visibility gaps; it does not qualify every angle, motion,
animation or remaining release gate. Private screenshots, logs and visual
metadata stay outside Git under
`/Users/kristiyantodorov/Repos/zandronum-arm64-artifacts/gles/`:
`native08-v2-gltest03.png`, `native08-v2-gltest03-b.png`,
`native08-v2-gltest03.log`, `native08-v2-gltest03-visual.json`, and corresponding
`native08-v2-gltest04` files. Native binary SHA256:
`11613d4b350a0a93ad85e12c8a5a16f830c1e759d24687d397fa58cb4afd9fc8`.

## Launch and views

Add `-file /path/r36s-gles-fixtures-v2.pk3 +map GLTEST01` to isolated native test
launch, retaining existing private libraries, config, IWAD, 640x480 mode and logs.
Substitute map names below for separate launches. No console typing required.
Map ACS ENTER script waits one tic, selects fixed SecurityCamera TID9002, then
logs `GLES-FIXTURE READY`. Camera starts from exact coordinates below, independent
of view bob. First forward/side movement restores player view for circling and
walking. Optional `puke 2` restores fixed camera; `puke 3` restores player view.
On GLTEST03, `puke 2` also restores upward pitch; `puke 5` selects horizontal
camera as an occlusion/control view. Default ENTER view already looks up.
Automatic camera selection avoids reliance on deferred `+puke` command order.
OPEN script binds monitor texture on GLTEST06 after one tic.

| Map / launch argument | Fixed camera x,y,z / angle | Expected features |
| --- | --- | --- |
| `+map GLTEST01` | `96,256,48 / 0` | Four east-wall mirror segments reflect colored sprite and room; central north floor slopes smoothly to 48-unit platform. Check mirror joins, clipping and slope triangles while moving. |
| `+map GLTEST02` | `256,64,48 / 90` | Solid 3D slab at z72–88; translucent slab at z104–120 behind it. Check tops/undersides/sides while circling, sprite visibility through translucent slab, collision when walking. |
| `+map GLTEST03` | `256,96,64 / 90`, pitch `-33.75` | Upward view through sky ceiling should reveal checker walls/blue ceiling and four raised colored sprites from separate SkyViewpoint room. Foreground brick walls should occlude lower view; turn/move to check portal stencil residue. |
| `+map GLTEST04` | `256,96,48 / 90` | Left MD2, center MD3, right KVX. Models interpolate between 40- and 52-unit height every 16 tics. KVX has horizontal red/green/blue bands on exterior faces; circle to check all faces. |
| `+map GLTEST05` | `256,64,48 / 90` | Floor columns: warp1, warp2, red glow, custom GLSL checker. Sprites: opaque green-light source, alpha50%, additive50%, fuzz. Brightmap wall dots stay bright. Far row adds blue-gray fog, warm light and 40% desaturation. |
| `+map GLTEST06` | `256,256,48 / 0` | East-wall monitors show isolated source room with two animated cubes. Camera image updates, stays upright and retains depth/occlusion. Returning to main scene leaves viewport/depth/stencil intact. |

Coordinates use map units, angles use Doom degrees (0 east,90 north). Camera z
is floor-relative; all fixed cameras stand over floor0. Models' rendered height
also follows existing engine model aspect correction. Original player starts
share camera x/y/angle, so camera revert preserves direction.

## Acceptance evidence

Record native driver/context strings, engine and PK3 hashes, config/cvars,
resolution, map, fixed/moving viewpoint, scene screenshots and logs. Keep dynamic
lights, glow, brightmaps, shaders, fog and mirrors enabled for feature checks;
normal renderer configuration can disable some features independently of backend.
Capture moving/animated frames as well as static camera. Animation/fuzz/timer
phase makes an arbitrary wall-clock screenshot unsuitable for exact pixel hash
comparison; compare matched game tics or stable geometry/occlusion regions.

Require clean map load and `GLES-FIXTURE READY` on every map. Search logs for ACS,
DECORATE, MODELDEF, voxel, texture, shader and framebuffer errors. Confirm MD2,
MD3 and KVX visually; sprite fallback is a failure for model checks. Empty/black
monitor or stale model animation is failure for camera test. Mirror or skybox
leaks, cut planes, missing slopes/3D floors, or wrong alpha/fog are failures.

Compare native GLES against trusted desktop renderer on same fixtures, then
repeat original Doom/Doom II scenes and owner-provided regression saves with
fixture PK3 **absent**. Original-content qualification must preserve normal
actor definitions, maps, lighting and game behavior. Also qualify map transitions,
menu/HUD, saves/reloads, audio/controls, quit/relaunch and stock server protocol.
Performance measurement remains separate: warm/cold startup, cached relaunch,
representative gameplay frame-time percentiles and long-session spikes on R36S.

Host checks cover deterministic archive generation, ACS compilation, asset
hashes, WAD directory bounds, two-sided topology and MD2/MD3/KVX structural
headers. Actual engine parser/context gate above additionally loads every map.
Visual correctness and performance on Mali remain device/reference acceptance
work until evidence is recorded in native validation notes.

## Source contracts

Generated definitions use existing Zandronum 3.2.1 parsers, not modern GZDoom-only
features. Primary contracts in this repository:

- `src/actionspecials.h`: `Plane_Align`181, `Line_Mirror`182, `Sector_Set3DFloor`160.
- `src/p_slopes.cpp`: front/back Plane_Align selection.
- `src/p_3dfloors.cpp`: solid type1, alpha in arg3; tagged controller sectors.
- `src/p_udmf.cpp`: ZDoom namespace, sector light/fade/desaturation keys.
- `wadsrc/static/actors/shared/camera.txt`: SecurityCamera9025.
- `wadsrc/static/actors/shared/skies.txt`: SkyViewpoint9080.
- `src/p_lnspec.cpp` / `src/p_user.cpp`: ChangeCamera revert-on-movement.
- `src/p_acs.cpp`: SetActorPitch fixed-turn angle conversion and GetActorPitch
  runtime marker; negative pitch looks up.
- `src/textures/animations.cpp`: camera texture dimensions/fit and warp syntax.
- `src/gl/models/gl_models.cpp`, `gl_models_md2.cpp`, `gl_models_md3.cpp`:
  MODELDEF mappings and model binary contracts.
- `src/r_data/voxels.cpp`: KVX headers, offsets, palette and VOXELDEF syntax.
- `src/gl/textures/gl_texture.cpp`, `src/gl/dynlights/gl_glow.cpp`,
  `src/gl/dynlights/gl_dynlight.cpp`: brightmap/glow/light definitions.
- `src/gl/shaders/gl_shader.cpp`, `wadsrc/static/shaders/glsl/func_normal.fp`:
  custom `Process(vec4)` material contract and GLSL portability aliases.

Mesh positions, triangles, texture pixels, palette, map layout, definitions and
shader body are independently generated original data. Reading format parsers
does not import parser code or copyrighted game/model artwork into test assets.
