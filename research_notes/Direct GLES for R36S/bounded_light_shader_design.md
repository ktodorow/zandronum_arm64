# Bounded-light shader sibling: read-only design

No implementation or device action. Frozen native09 inspected. Parent currently tests existing `gl_direct_state_change=0`; pool64 had no material gain and was not promoted. This design is queued, not a performance claim.

## Evidence and limits

Private actual state histogram `artifacts/gles/native09-map07-light-states-{incoming,rockets}-states.csv`, all `range_valid=1`, no overflow. Incoming:21128 DYNLIGHT draws/124481 total (16.97%),383139 submitted vertices/indices.21126 draws≤8lights, two9-light fallbacks. Rockets:14946/170920 draws (8.74%),248163/3250794 submitted vertices/indices (7.63%).14931/14946 (99.90%)≤8;15 draws need9–11. All observed lit materials are `Default::default+dynlight::noclip-noalpha`, all modulated x=y=z, alpha off/clip0. No zero-light DYNLIGHT draws. Subtractive/additive/desaturate remain required correctness, not measured eligibility here.

Draw/vertex share does not bound fragment cost: world surfaces can cover screen. API capture independently shows driver draw CPU dominates combat; these files do not tie CPU samples to material programs, and current GPU load suggests combat often CPU-bound. A smaller shader might lower Mali uniform register/loop code pressure or draw validation costs, but none is measured. Existing full-capacity opaque4/6 siblings had no observed fixed-scene gain; do not promise60FPS or attribute general draw hot loop to lights.

## Recommended smallest correct design

Replace the existing full128 opaque child for measured Default index4 with an opaque8 child; keep logical generic128 base as exact fallback. Keep Default index0 existing child. Optionally index6 only after measured usage or explicit scope; all same-math image cases can still be tested. This retains two physical programs per logical material and avoids general multi-child state/lifecycle migration. Rare>8 opaque draws fall back to generic128 (same pixels, inactive clip/alpha uniforms). Existing128 opaque child showed no measured gain, so keeping a third child offers no supported benefit yet.

`src/gl/shaders/gl_shader.cpp: FShaderContainer` already copies exact `str` including NO_GLOW/NO_DESATURATE/DYNLIGHT defines for its opaque child. For bounded child, override only child `GLES_LIGHT_ARRAY_SIZE=16` after inherited definition, plus existing `GLES_OMIT_CLIP_ALPHA`. Base remains256 vec4/128 lights. `main.fp` already uses this macro for BOTH declaration and constant loop bound; no shader math change needed. Independent macro for limit possible but retaining declared256 may not reliably reduce active array/uniform allocation. Explicit child16 guarantees source bound and declared footprint. Child still executes DYNLIGHT clamp and all Process/fog/category order math. Do not alias non-DYNLIGHT shader; it omits pre-Process clamp[0,1.4]. Cache source/driver/attribute hash naturally distinguishes source.

Extend `RegisterNoClipAlphaVariant(base,fast,maxLightVec4=0)` with bounded metadata;0 means existing unbounded fast behavior. Registration records canonical `lightrange` uniform index (ivec3,size1) while reflecting **base**. Require its type/presence for bounded registration; never derive selection from currently bound physical uniform or query GL at draw.

At `PrepareDrawProgram`, existing alpha/clip predicate must pass; bounded child also requires canonical range0≤x≤y≤z≤16, all endpoints even. Producer already guarantees valid sorted complete light records, but invalid state should always fall back instead of silently dropping lights. Read canonical ints[0..2] at actual draw after any setters. Full128 fallback remains if>16 vec4, masked, clip or malformed. Logical `GetIntegerv(GL_CURRENT_PROGRAM)` still returns base. Immediate Flush, native DrawArrays, both native DrawElements paths already call this selector, retain all seams.

## Uniform correctness and lifetime

`MaterialUniform` reflection/storage uses generic base active array256, preserving all128 canonical light records and element versions. Only child locations0..15 exist; `[16]..[255]` naturally produce-1. Existing ReplayMaterial skips missing locations and batches only consecutive present dirty locations, so base-size upload is not forwarded directly to short shader. Reflection should explicitly test this; GL_ACTIVE_UNIFORMS on short shader must never determine canonical length.

All full/partial updates, including far-tail `[255]`, retain canonical data/version while short child bound. Base resident counters remain independent/stale until full fallback replays entire required latest data. Never mark missing child elements resident in base; never resize/drop canonical arrays when selecting short child. Existing RouteUniform Flush-before-write gives queued geometry old range/data before new state changes, and selection occurs again before actual draw. Replay to current child may temporarily upload range>16, which is legal until next draw selects base; no short draw executes it.

Unregister/delete/relink still match base or one child, restore latest base snapshot even when unrelated material current, preserve unrelated actual binding, invalidate cached locations/revision. Same two-program ownership avoids shared physical program residency errors and recapture bugs. Replacement must preserve canonical base before recapture. Short child remains exclusive to one base; do not share a physical program among logical4/6.

If retaining both opaque128 and opaque8 children becomes necessary later, current `[2]` locations/resident arrays, `fast` handle, actual→physical selection, registration, delete/link/unregister all require3-owner generalization. Do not register another pair against same base: current Register calls Unregister, destroying previous mapping. That larger router is unnecessary for first bounded probe.

## Required falsification tests before build

1. Real generic128 vs short8 full-image equality across0..8 actual lights, all modulated/subtractive/additive/mixed categories and last short slot15, fog0/±1/±2, modes0/1/2/4/6, desaturation if6 in scope. Preserve highp math and blending/stencil/depth paths.
2. Reflection: canonical256 vs physical16 active array; `glGetUniformLocation(short,"lights[16]")==-1`; full256 upload while short bound gives no GL error. Update last base slot255 while short selected, then range256; check128-light output and values in raw base program.
3. Prefix/overlap updates both orders: full256→partial[15]/[255]→full256; scalar/vector ivec3 range equivalent; short8→full9→full128→short8; independent material4/6 and logical identity. No stale full-array replay.
4. Change light range after UseProgram but before immediate/native DrawArrays/native EBO DrawElements; queue old geometry then change range, confirm old batch light image unchanged and new selection exact. Alpha/clip changes force128 generic on same logical object; return opaque selects short.
5. Child deletion/replacement while unrelated program bound, base relink, zero default range, independent residency and full128 restoration; existing depth-split/model/sphere cases plus complete218/239 builtins remain green ES2 andES3.2.
6. Actual device: prove short physical label selected for≤8 and generic128 for9+ via private histogram; fixed-scene cold/warm startup/cache; matched no-input and combat API/sample captures. Compare total frame CPU/draw CPU/GPU timing and screenshots. If no gain, do not promote speculative complexity.

Expected source scope small: gl_shader.{cpp,h} child setup if named storage changes, gles_compat.{cpp,h} bounded metadata/selector/API, renderer harness + generated shader variants. No scene/light collector/quality/default/network changes. Risk concentrated in short-array reflection/residency and draw-time routing; source-only design cannot qualify performance.
