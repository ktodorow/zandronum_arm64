# Native GLES shader cost audit

2026-10-15. Read-only source audit; no engine, shader, harness or device changes. Objective: sustained60FPS at native640×480 with existing graphics. Source facts below support candidate changes; they do not establish a speedup.

## Evidence and limit

Integration owner reports native05 fixed MAP01 about31.7FPS, main CPU about11ms/frame, GPU completion about20ms/frame, GPU utilization53% at400MHz; direct EGL interval0 brings no useful improvement. Earlier fresh-store upload fix reduced measured upload time from401ms/frame to0.719ms/frame. These are owner-supplied measurements, not new measurements from this audit. Native06 light-loop comparison remains separate. A20ms GPU completion already exceeds16.667ms budget; aggregate utilization does not identify whether arithmetic, texture, varying, overdraw or scheduling dominates.

## Actual source comparison

| Backend/source pin | Precision and work | Relevant distinction |
| --- | --- | --- |
| Local native Zandronum | Vertex default highp float/int. Fragment defaults highp float when supported, otherwise mediump; highp int. Ordinary variants specialize glow, desaturation, dynamic lights and software lighting, but every material retains runtime six-plane clip tests and general alpha comparator. Fog type and texture mode remain uniforms. | `vGLESEyePosition` remains a highp vec4 because clip tests can be enabled at runtime. Runtime radial/plane fog paths also keep their different inputs live. Native selects SM4, which makes every3D draw use material shader. |
| [GZDoom `c26ce2e6ca2a0c770f140cb25dde0d30073ca8f7`, precision](https://github.com/ZDoom/gzdoom/blob/c26ce2e6ca2a0c770f140cb25dde0d30073ca8f7/src/common/rendering/gles/gles_shader.cpp#L53) | `gles_glsl_precision=2` defaults highp float/int. Low/medium precision are explicit alternatives. | GZDoom does **not** establish mediump as equivalent/default for Zandronum. |
| [Same GZDoom, variant construction](https://github.com/ZDoom/gzdoom/blob/c26ce2e6ca2a0c770f140cb25dde0d30073ca8f7/src/common/rendering/gles/gles_shader.cpp#L682), [state selection](https://github.com/ZDoom/gzdoom/blob/c26ce2e6ca2a0c770f140cb25dde0d30073ca8f7/src/common/rendering/gles/gles_renderstate.cpp#L99) | Compile-time fog enabled/radial/colored, texture mode/flags, alpha-test omission, light-category presence, glow and other state. | Useful reusable design: remove inactive paths from program rather than merely branch around them at runtime. Retain local shader math and licensing lineage. |
| [Same GZDoom fragment main](https://github.com/ZDoom/gzdoom/blob/c26ce2e6ca2a0c770f140cb25dde0d30073ca8f7/wadsrc/static/shaders_gles/glsl/main.fp#L470) | Alpha comparator is `<=`; clipping discard at474 is commented out. | Neither comparator nor omitted clipping can be copied blindly: local GL_GEQUAL must retain equality, and mirrors/portals need actual clip behavior. |
| [Deployed GL4ES `a025615bc79ac43ec911dced8fe5b8eb45a0b72d`, precision conversion](https://github.com/ptitSeb/gl4es/blob/a025615bc79ac43ec911dced8fe5b8eb45a0b72d/src/gl/shaderconv.c#L450) | Generated fixed-function shaders use mediump default; translated application shaders use highp if available. FPE gives UVs/matrices and selected fog quantities explicit highp. | Generic claim that GL4ES uses all-mediump is wrong. Common FPE path can nevertheless have lower varying/arithmetic cost than current mandatory material program. |
| [Same GL4ES generator](https://github.com/ptitSeb/gl4es/blob/a025615bc79ac43ec911dced8fe5b8eb45a0b72d/src/gl/fpe_shader.c#L815) | Generates only active clip planes, fog type and alpha comparator. Fog is per fragment in this pin; vertex-fog implementation is `#if0`. | Specialization is concrete; claiming GL4ES is faster because it uses vertex fog would be false for this pin. |
| [PortMaster-linked Render96ex fork `7195b929964056449aeb0fb97b7b13a3cee79d87`](https://github.com/cdeletre/Render96ex/blob/7195b929964056449aeb0fb97b7b13a3cee79d87/src/pc/gfx/gfx_opengl.c#L198) | Fragment default mediump. Generates only used texture samplers, combiner inputs, fog and texture-edge discard. Vertex shader receives already transformed clip coordinates. | [CPU lighting and fog](https://github.com/cdeletre/Render96ex/blob/7195b929964056449aeb0fb97b7b13a3cee79d87/src/pc/gfx/gfx_pc.c#L792) substantially change workload: N64 lighting/fog factors computed per vertex, then interpolated. Replacing Zandronum per-fragment world lighting/fog this way would alter graphics. GLES path also suppresses noise in this source; that cut is not proposed. |

Render96 provenance boundary: downloaded package README names `cdeletre/Render96ex`, branch `tester_rt64alpha`, `USE_GLES=1`; packaged binary contains GLES dependency, `precision mediump float` and build suffix `dc6776c3`. Fetched fork branch currently resolves to7195b929; exact full commit for packaged dc6776c3 not recovered. Current fork and Render96 upstream master48da5c491c8abfb2f5192b59278c1c269411e533 agree on relevant combiner/precision design; do not claim bit-exact package source provenance. Package defaults include640×480 internal/window size,60FPS enabled, vsync off and precache on; README also describes reduced assets and optional disabled model pack. Configuration does not prove all scenes sustain60FPS.

Local source anchors: [preambles/suffix](../../src/gl/shaders/gl_shader.cpp#L74), [current16 variant definitions](../../src/gl/shaders/gl_shader.cpp#L334), [material fog/light/texture math](../../wadsrc/static/shaders/glsl/main.fp), [SM3/SM4 path selection](../../src/gl/renderer/gl_renderstate.cpp#L153), [solid-pass alpha disabled](../../src/gl/scene/gl_scene.cpp#L396).

## Why GL4ES and native can select different work

Pinned [GL4ES getter](https://github.com/ptitSeb/gl4es/blob/a025615bc79ac43ec911dced8fe5b8eb45a0b72d/src/gl/getter.c#L247) advertises GLSL1.20 for GL2.1, wraps vendor string and does not advertise NV/EXT GPU_shader4. Existing desktop detection therefore normally selects SM3 on Mali; native explicitly selects SM4. Actual deployed startup/config remains authoritative.

With default `gl_fogmode=1`, `gl_fog_shader=false`, ordinary SM3 surfaces can use GL4ES FPE; SM4 uses local material shader for all3D. SM3 also gates brightmaps and some lighting/effects differently. Performance comparison requires matched actual visual settings/path, rather than assuming enum difference alone is overhead. Lowering native shadermodel is invalid: Basic shader does not implement world fixed-function fog/lighting; `Fog*`/`TexEnvi` logical wrappers depend on material path.

Plain Doom MAP01 does **not** imply no fog: `gl_SetFog` uses black fog for light attenuation according to sector light and configured light mode. Preserve exact fogset0/+1/−1/+2/−2 and original distance equation/exp2. Do not force fogenabled0 as an optimization.

## Ranked no-effect-cut implementation plan

1. **Compile a truly unclipped, alpha-disabled common program.** Eligibility: actual adapter clip mask0 and alpha test disabled or comparator GL_ALWAYS. Existing solid draw pass explicitly disables alpha, so this requires no assumption about texture opacity. Remove six clip conditions, eye-position varying and alpha-discard source at compile time. Preserve all color/material/fog/light math and highp defaults. Keep existing generic program for any active clip plane or other alpha comparator. Hypothesis: less varying traffic and discard-free program may improve Mali early depth rejection. Both benefit and magnitude require Mali comparison; compiler may already optimize some uniform branches.
2. **Specialize exact fog and texture state for observed hot tuples.** Compile existing expressions with constant fogset0/+1/−1/+2/−2; no formula/order/precision rewrite. Plane-fog/no-dynamic-light variant can let compiler remove unused pixelpos.xyz/camera-position distance path. Fog-off variant retains pixelpos if dynamic lights/custom material use it. Constant texture mode0/1/2/4/6 plus texture enabled flag removes irrelevant branches; disabled texture must skip fetch only where original texel result is provably overwritten and custom shader semantics retained. Keep every original mode and fallback.
3. **Specialize common masked comparator and active clip mask only if measured worthwhile.** Preserve exact <,==,<=,>,!=,>= behavior and threshold uniform. Never replace local GL_GEQUAL with donor `<=` rejection. Moving alpha test before lighting is allowed only for builtin Process functions with proven alpha independence; custom Process can change alpha arbitrarily, and fog-layer output computes its own alpha. Retain generic post-material test for those paths. Active-plane vertex-distance interpolation could reduce fragment dots, but boundary rounding and custom contracts make it a later change, with mirror-edge evidence required.
4. **Treat selective mediump as separate visual-precision qualification.** It is not bit-equivalent and not a demonstrated GZDoom default optimization. Potential bounded color intermediates/vertex colors can be examined later; retain highp map/eye positions, fog distances, clip equations, dynamic-light subtraction and wrapping UVs. Actual Mali comparison required: desktop/Mesa may execute mediump with full precision. No global precision downgrade, per-vertex fog substitution or texture/color bit-depth reduction.

Start with item1 alone, then identical fixed-camera A/B. Do not create Cartesian product of16 existing flags ×64clip masks ×8alpha funcs ×5fog modes ×5texture modes. Histogram chooses small hot tuple set. Precompile supported hot variants during startup, include full defines/source/attribute ABI in existing binary-cache key, use precompiled generic fallback for all other states. No runtime compile pause on first unusual mod/portal draw. New variants must fit queried limits; existing full feature/light capacity remains.

Integration boundaries:

- Keep specialization ownership in material shader manager. Each actual program needs independent uniform locations and current-value caches. `FShaderManager::SetActiveShader` currently compares FShader pointers only; use distinct variant FShader objects or handle-aware binding. A GLuint switch hidden inside adapter Flush would leave material uniforms targeting wrong program.
- Key uses **actual** clip/alpha/texture state, not stale cached FRenderState fields. `Apply()` sets alpha before ApplyShader today, but raw portal clip changes and direct-state mode need audit. Any relevant state change flushes prior geometry; next draw selects correct variant even if base material identity stays unchanged.
- Program switch must retain matrix/clip/depth-clamp/basic state revision behavior. Depth-clamp CPU fallback inspects model/precomputed-sphere locations; specialization must not silently change that contract. Builtin and custom fragment materials need generic fallback.
- Shader optimization can follow GZDoom architectural pattern without importing mixed-license modern GLSL. No new donor code copied by this audit.

## Proof required for each specialization

- Compile/link all existing236 builtin+fixture programs and added variants on glslang, Mesa GLES2/3.2 and actual Mali; retain full264-check depth-clamp suite. Extend source-driven harness rather than duplicate shader implementation in tests.
- Differential reference renders: generic vs specialized common opaque draw, all five fogset values, all five texture modes, texture enabled/disabled, black/colored fog, radial/plane distances, fog-parm boundaries, desaturation/glow/software lighting, zero/one/multiple/mod/sub/add lights, original custom fixture material and custom alpha-changing material. Exact byte equality where same arithmetic remains; document unavoidable driver rounding tolerance rather than silently relaxing it.
- Alpha tests at threshold−epsilon/equality/+epsilon for all eight comparators; alpha-disabled material with alpha0 must still write color/depth; GL_ALWAYS same; active one/six clip planes still discard. Alternating generic/specialized draws, state changes with queued batches, same base material across alpha/clip toggles, new programs/cache reload and corrupt cache fallback catch state leaks.
- Mirrored portal, plane mirror, skybox, 3D floors, camera FBO, animated models/sphere UV and AA Basic draws remain qualified. Run existing fixtures and original MAP01/MAP07 with identical audio/lights/resolution/clocks/config.
- Measure fixed-camera CPU, GPU completion and total frame time before/after, plus per-draw histogram and visible image. Then sustained combat p50/p95/p99/worst and cold/hot startup behavior.60FPS requires complete frame budget≤16.667ms; no speedup claim from shader source size alone.

Arm primary guidance supports mechanism, not predicted gain: discard/fragment-depth can restrict early depth testing; varying bandwidth and precision can matter; pipeline counters or offline compiler distinguish arithmetic, texture and varying limits. [Mali analysis guidance](https://developer.arm.com/community/arm-community-blogs/b/mobile-graphics-and-gaming-blog/posts/accelerating-mali-gpu-analysis-using-arm-mobile-studio), [Mali Offline Compiler example on G31](https://developer.arm.com/community/arm-community-blogs/b/mobile-graphics-and-gaming-blog/posts/accelerating-shader-programs-with-mali-offline-compiler-7). Preserve source/result pairing when using offline estimates; driver/compiler and workload still decide actual frame time.

## Separate depth-clamp source review

Read-only review of `DepthTransform` through `SplitDepthBatch`, local material main.vp and `TestDepthClamp`: no blocker found for current engine inputs. Object-space split parameter da/(da−db) preserves clip XYZ/W and affine vertex outputs; whole-piece Z=±W yields correct constant clamped NDC depth, while inside piece retains original depth interpolation. Current view/model transforms are affine with eye W=1, so pixelpos.w is affine; sphere-map nonlinear UV precompute handles exception. Convex-piece fan triangulation preserves winding; exact-boundary assignment prevents duplicate blended fragments. Engine supplies three-component positions and fragment-only custom materials, matching implementation assumptions. Negative-W, reversed-depth, model matrix, culling, line, point, in-range VBO and reference-image coverage in264-check suite addresses meaningful failure modes. Actual R36S fallback performance/correctness remains integration owner measurement.

## Actual R36S comparisons and invalid SM3 flag probe

Later integration-owner captures,2026-10-15:

| Scene/build | Owner-reported result | Qualification |
| --- | --- | --- |
| GLTEST05, native06 |25.793FPS | Native material path; screenshot shows brightmap wall dots white. |
| GLTEST05, matching legacy02 GL4ES |52.707FPS | Screenshot shows corresponding brightmap dots dark; warp appearance also differs. Same fixture/camera comparison measures implemented paths, not exact pixels or equal GPU work. |
| Fixed plain MAP01, legacy02 GL4ES |77.369FPS uncapped15s; minimum full5s window77.2FPS; p99 frame21.933ms;210frames exceed16.667ms | Strong average does not establish every-frame60FPS acceptance. |
| Fixed plain MAP01, native05 |31.732FPS | Earlier native fixed-scene result; no attribution to API alone or assertion of exact shader/workload parity. |

Source audit found stronger restriction than ordinary optional-SM3 flags. `FShaderContainer` construction skips every non-SOFTLIGHT ordinary material on SM3: `if ((i&8)==0)` then `if (gl.shadermodel!=4)` stores NULL and continues. This gate exists unchanged in base178fbce175adc2cc3fca645d66d4c9bebaf5d8b1 ([base source](https://github.com/ktodorow/zandronum_arm64/blob/178fbce175adc2cc3fca645d66d4c9bebaf5d8b1/src/gl/shaders/gl_shader.cpp#L331)). Normal selection uses `glowing +2*desat +4*lights +(glset.lightmode&8)`, so default lightmode3 selects those unavailable entries. `gl_SetupMenu` and lightmode cvar clamp ordinary SM3 lightmode8 to3; shader dynamic lights also require SM4. Colormap/fog-layer/special-effect shader objects are separate and may still exist. Map-specific lightmode overrides are another path; do not generalize this finding to every possible map.

Consequently, launching legacy02 with `+gl_fog_shader 1 +gl_brightmap_shader 1 +gl_warp_shader 1 +gl_colormap_shader 1 +gl_glow_shader 1` **cannot establish ordinary material-shader equivalence**. Unavailable shader entries remain NULL and fall back. Enabling warp shader can suppress CPU warp while material remains unavailable, changing image without intended shader workload. Probe was rejected before running; no numbers should be attributed to it.

`NO_SM4` is another non-equivalence boundary: legacy SM3 sources omit texture-matrix7 world transform, use ftransform, choose plane fog without SM4 runtime radial branch, and omit Doom near-light factor. SM3 also rejects radial fog/lightmode2/8 and shader dynamic lights. Setting five booleans cannot remove these compile-time/capability differences. Forcing native enum down toSM3 would break backend contract and is not a fix.

Next clean experiment remains same native binary/common material with generic versus compile-time clipmask0+alpha-disabled variant, identical state/camera/content/precision/math and both prewarmed. Capture actual selected program/state histogram and screenshot, then GPU completion/CPU/frame tails. This isolates native wrapper fragment paths more directly than comparing different FPE/material pipelines. Any diagnostic GL4ES material comparison must first demonstrate actual non-FPE material binding and compatible shader compilation; config intent or shader compilation at startup alone does not establish draw-time use. No engine edits or device access performed by this audit.

## Provably redundant alpha test in old multipass renderer

2026-10-15 follow-up; read-only source/asset analysis. No implementation or speedup claim.

Stock `wadsrc/static/glstuff/gllight.png` SHA256
`751572fdf1af3c87b0c767bca482025076a6176c2e95ff4ea3931db5c2090925`
matches pinned GZDoom asset byte-for-byte. PNG is 256×256, grayscale8, with only
IHDR/IDAT/IEND chunks: no alpha, tRNS or alPh chunk. Decoding confirms gray0–254,
including15160 black pixels; those black pixels are **opaque black**, not holes.
Engine PNG truecolor copy initializes all gray palette entries with alpha255
([pngtexture.cpp](../../src/textures/pngtexture.cpp#L606)).

`FGLRenderer::Initialize` creates this texture as TEX_MiscPatch
([gl_renderer.cpp](../../src/gl/renderer/gl_renderer.cpp#L106)).
`gl_SetupLightTexture` validates it and calls `BindPatch(CM_DEFAULT,0)`
([gl_dynlight1.cpp](../../src/gl/dynlights/gl_dynlight1.cpp#L259)); normal
MiscPatch is not sprite/decal expanded, so no transparent frame is introduced.
Patch upload disallows hi-res replacement and uses CLAMP_TO_EDGE
([gl_material.cpp](../../src/gl/textures/gl_material.cpp#L497)). With alpha1
throughout base image, filtering/mips retain alpha1. Normal material Process
returns texture×vertex color; `gl_SetupLight` uses glColor3f, hence vertex alpha1
([gl_dynlight1.cpp](../../src/gl/dynlights/gl_dynlight1.cpp#L248)). Main fog,
desaturation, glow and dynamic-light operations modify RGB only; colormap main
also preserves input alpha. Fog-layer main is a separate exception.

| Draw scope | Exact safe predicate | Benefit/boundary |
| --- | --- | --- |
| `GLPASS_LIGHT` and `GLPASS_LIGHT_ADDITIVE` inside `!gl_dynlight_shader && gl_lights` block | Bound light patch has actual uploaded alpha255 everywhere; unexpanded; actual shader is known alpha-preserving builtin normal material, not custom/effect/fog-layer; every emitted vertex alpha1; alpha comparator is inherited GL_GEQUAL with clamped threshold≤1. | Alpha test cannot reject any fragment. Disabling it around each light-pass draw block can select existing Default0 discard-free sibling when clip mask0 and actual program is Default ordinary0. Fog, additive/subtractive equation, object desaturation and masked geometry do not invalidate this proof. |
| `GLDL_LIGHT.Draw(GLPASS_BASE)` | Texture disabled; actual shader is builtin NoTexture ordinary or colormap, not fog-layer/effect; all wall/flat vertices set by `gl_SetColor(...,1)`; same comparator. | Alpha1 independently of texture. Currently inherits enabled alpha from preceding masked pass. Disabling is exact under predicate, but existing fast sibling only covers Default0, so this scope needs separate NoTexture sibling to remove compiled discard. |
| Builtin ordinary material with TM_OPAQUE2 or TM_INVERTOPAQUE6 | Known alpha-preserving Process; vertex alpha1; no fog-layer/effect; clamped GEQUAL threshold≤1. | getTexel forces alpha1. Color/fog behavior stays unchanged. Do not generalize by texturemode alone to custom Process. |
| Ordinary untextured builtins, including special colormap | Known Process alpha-preserving or NoTexture; vertex alpha1; no fog-layer/effect. | NoTexture Process returns color; colormap multiplies preserved texel alpha by vertex alpha. An untextured draw can still have translucent vertex alpha, so texture disabled alone is insufficient. |

Relevant scene boundaries: alpha enabled before masked geometry at429; inherited
through untextured multipass base at459 and normal light block476–486. Masked
base draws466–467 require alpha test. Third texture pass explicitly disables
alpha for GLDL_LIGHT only, then restores before GLDL_LIGHTBRIGHT/MASKED
([gl_scene.cpp](../../src/gl/scene/gl_scene.cpp#L429)). Additive light block513–519
inherits enabled test again. Preserve these masked texture/base boundaries;
restore prior alpha state after each eligible light block. Keep GL_EQUAL depth,
depth writes, blending, every color/fog/light expression and draw order exact.
Light-pass removal is safe even over masked walls/3D floors because light texel
alpha is identically1; base texture's mask is never being sampled in that pass.
Subtractive setup changes blend equation/RGB; its alpha stays1. Softlight mode8
uses a different shader index, so disabling alpha does not automatically route
to already-qualified Default0 fast sibling. Active clip planes also retain
generic program until separately qualified specialization exists.

Do **not** infer this proof from light texture filename, `!isMasked()` alone,
`!GetTransparent()` alone, render-list name, enabled fog or alpha threshold0.
Mods can replace `glstuff/gllight.png`, attach a custom Process or override
builtin shader lumps. `GetTransparent` only detects partial alpha and does not
exclude alpha0; bMasked can be conservative and is not an immutable upload
contract. Robust production gate: cache `all uploaded alpha==255` on actual
patch texture revision after conversion/expansion/scaling, invalidate on change,
and pair with verified alpha-preserving compiled-source contract/known builtin
program. Replacement alpha texture or alpha-changing Process keeps old path.
Narrow alternative for current shipped light asset: verify resolved asset and
shader source hashes once at startup, enable only exact known contract, fallback
for replacements. No per-frame scan/readback. Changing TM_MODULATE to TM_OPAQUE
without this gate would alter replacement asset behavior and is not proposed.

GZDoom architecture corroborates specializing discard eligibility rather than
globally dropping alpha: pinned GLES state asks shader manager for discard
variant when `mAlphaThreshold>=0`
([gles_renderstate.cpp](https://github.com/ZDoom/gzdoom/blob/c26ce2e6ca2a0c770f140cb25dde0d30073ca8f7/src/common/rendering/gles/gles_renderstate.cpp#L215));
compiler defines NO_ALPHATEST when discard unnecessary
([gles_shader.cpp](https://github.com/ZDoom/gzdoom/blob/c26ce2e6ca2a0c770f140cb25dde0d30073ca8f7/src/common/rendering/gles/gles_shader.cpp#L745)).
Its GEqual threshold encoding subtracts0.001 before `<=` rejection; local exact
GEQUAL comparator must remain unchanged. This is design evidence, no donor code
copy. Modern engine no longer has old multipass light architecture, so its
opaque draw-list predicates cannot be transplanted wholesale.

Before production change: compare alpha enabled/disabled images using actual
stock gllight pixels on wall/flat, black/colored and radial/plane fog,
modulated/additive/subtractive lights, masked holes, coplanar occluders and solid/
translucent3D floors; include threshold0/0.5/1 and active clip fallback. Repeat
with transparent/partial-alpha replacement light PNG and alpha-changing custom
Process to prove fallback. Exercise empty/nonempty light lists and next masked
texture/decals/translucency draws to catch state leakage. Only then same-native
fixed-scene timing; alpha1 proof predicts equality, not measured performance.

## Zero-light DYNLIGHT specialization audit —2026-10-15

Parent reports native09 fastDefault4/6 GLTEST05 shader1 average51.359FPS versus
native08 51.589FPS: no demonstrated gain. This does not measure zero-light
eligibility or isolate shader GPU cost. No new device test or engine edit here.

**Recommendation: measure actual draw-time light ranges before implementing
zero-light variants.** Current production collectors already avoid DYNLIGHT
when no lights collected. Only `EnableLight(true)`/`SetLights` producers found
are wall `SetupLights` (`src/gl/scene/gl_walls_draw.cpp:208–216`) and flat
`SetupSubsectorLights` (`src/gl/scene/gl_flats.cpp:214–230`); both require
`numlights[2]>0`. Walls reset light enable after draw; flats disable/reapply when
moving from lit to empty subsector and reset after sequence.
`FShaderContainer::Bind` (`src/gl/shaders/gl_shader.cpp:548`) adds4 to shader
index only for light-enabled state. This source invariant suggests little/no
zero-range opportunity, but no actual eligible-count claim is made.

**Matching non-DYNLIGHT sibling is not generally equivalent.** With range
`(0,0,0)`, GLES loop breaks before sampling; dynlight/addlight remain zero.
Nevertheless `main.fp:276` clamps pre-Process RGB to `[0,1.4]`. Non-DYNLIGHT
omits this clamp entirely. Equivalence requires extra bound proof for every
fragment after fog/light/glow calculations; same fog/desat/glow flags alone do
not provide it. Numerical example at material boundary: inputRGB1.6 and
texture0.5 gives0.7 under DYNLIGHT clamp versus0.8 without it. This illustrates
shader-state contract, not a measured production pixel. Glow min1 and linked
ordinary lighting inputs may establish bounds for narrower cases, but that
would require separate proof. Custom Process can also inspect DYNLIGHT defines.

Exact zero-light child should retain original DYNLIGHT define, original
highp math/precision, fog/texture/glow/desat/softlight/Process code, clipping and
alpha semantics, and pre-Process clamp. New GLES-only macro can suppress only
known-empty local loops, retain zero accumulator semantics and additive zero.
Select only when canonical logical `lightrange` is exactly `(0,0,0)`; any
nonzero/invalid/unknown range uses full original128-light path. Subtractive or
additive categories alone remain real lights. Zero RGB/light contribution or
off-screen light source is not equivalent to no light records.

State ownership traps:

- Decide at each real draw from canonical uniform shadow, not FShader::Bind or
  late FRenderState::Apply. Uniforms and alpha/clip can change after bind;
  `mLightData` is caller-owned only until Apply (`gl_renderstate.h:211`). No
  per-draw `glGetUniform` needed. Uniform3i/3iv updates must preserve previous
  queued geometry before updating selector state.
- Do not share existing non-DYNLIGHT physical sibling with another logical
  shader. Per-FShader value caches and per-pair resident generations would not
  see other owner overwrites. Prefer exclusive physical child per logical
  DYNLIGHT program; preserve full canonical light-array state even when zero
  child optimizes range/lights locations away.
- Current adapter supports one fast child per logical base; calling
  `RegisterNoClipAlphaVariant` again unregisters old pair. Adding zero children
  needs explicit multi-child routing/per-child residency and lifecycle, not
  chained pair registrations. Alpha/clip eligibility remains independent;
  zero-light variants must preserve generic masked/clip fallback.
- Test0→1→128→0 ranges without rebinding, full/partial array writes while zero
  child active, all three categories and boundaries, independent programs,
  deletion/relink/replacement, immediate/native-VBO/EBO paths and depth split.
  Compare complete images against original DYNLIGHT at fog0/±1/±2, desat/glow,
  texture modes, alpha/clip and out-of-range materialRGB to catch missing clamp.

Pinned GZDoom GLES offers a more relevant architectural pattern if actual
nonempty ranges dominate: category-presence specialization.
[`gles_renderstate.cpp:191–194`](https://github.com/ZDoom/gzdoom/blob/c26ce2e6ca2a0c770f140cb25dde0d30073ca8f7/src/common/rendering/gles/gles_renderstate.cpp#L191)
derives mod/sub/add booleans from category counts;
[`gles_shader.cpp:704–706`](https://github.com/ZDoom/gzdoom/blob/c26ce2e6ca2a0c770f140cb25dde0d30073ca8f7/src/common/rendering/gles/gles_shader.cpp#L704)
generates compile-time flags;
[`material_normal.fp:42–114`](https://github.com/ZDoom/gzdoom/blob/c26ce2e6ca2a0c770f140cb25dde0d30073ca8f7/wadsrc/static/shaders_gles/glsl/material_normal.fp#L42)
omits empty-category loops while retaining base clamp even with all flags0.
Local ranges correspond to mod `x>0`, sub `y>x`, add `z>y`, in units of vec4
pairs; preserve original accumulation order and128-light capacity. Upstream
GLES100 category caps8/4/4 and modern normal/attenuation/additive-clamp math are
not equivalent and must not be copied. Design reference only; existing shader
license/import boundaries remain. Profiling category bits first avoids adding
untouched variants or assuming benefit from source-level branch removal.
