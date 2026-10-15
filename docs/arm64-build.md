# Disposable ARM64 client build

Issue [#2](https://github.com/ktodorow/zandronum_arm64/issues/2) supplies a
Linux AArch64 **graphical client** from the pinned Zandronum 3.2.1 baseline.
Device launch, rendering, sound, controls and multiplayer remain later tests.
Scope follows the [main specification](specs/zandronum-3.2.1-arm64-r36s.md).
The [PortMaster test workflow](portmaster-testing.md) covers package setup,
SSH deployment, physical-device checks and subsequent acceptance gates.

## Build and export from the Mac

Requirements: Docker Desktop running with its Linux engine, Docker Buildx,
Git, and Python 3.9 or newer. No project compiler or dependencies are installed
on macOS. Run from this checkout:

```sh
python3 tools/arm64/build.py build --output ../zandronum-arm64-artifacts/run-1
```

Output must be outside the source checkout and absent or empty. Optional
`--jobs 2` reduces compiler concurrency. Source files are copied to a temporary
build context, then into disposable Linux build storage. Configure/resource
generators may change that copy; they cannot change host source. Tracked and
nonignored untracked files are included, so uncommitted work is buildable.
Ignored build files and `.git` are excluded. Export directories are never
used as source or disposable build storage.

The export contains:

- `zandronum`: AArch64 client, with software and desktop OpenGL renderers compiled.
- `zandronum.debug`: matching separate debug symbols.
- `zandronum.pk3`, `brightmaps.pk3`, `skulltag_actors.pk3`: resources generated
  by tools executed natively inside the ARM64 builder.
- `lib/libSDL-1.2.so.0`: private sdl12-compat library. Select with a
  package-local library path during later launcher integration.
- `source.tar.gz`: exact source snapshot, including the Docker recipe and
  optional dirty-state provenance. No commercial IWADs are supplied.
- `SHA256SUMS` and `evidence/`: checksums, package versions, compiler commands,
  build log, source-file hashes, generated release/port identity, actual linked
  dependencies and their ELF headers, required symbols and symbol versions.
  SDL2 lookup requirements, conditional graphics lookups and optional FluidSynth
  imports are recorded separately because dynamic loading bypasses normal ELF
  imports. `client-abi.json` summarizes direct dependencies and symbol versions.

Verify an export:

```sh
cd ../zandronum-arm64-artifacts/run-1
shasum -a 256 -c SHA256SUMS
```

## Pins and architecture

`tools/arm64/Dockerfile` pins Debian Trixie by image digest and uses signed
Debian and security [snapshot repositories](https://snapshot.debian.org/)
at `20261014T000000Z`. This fixes transitive dependency inputs; the full installed
package set is exported in `evidence/toolchain-packages.tsv`. GCC/G++ 14 and
CMake 3.31 are also selected by explicit package version. Upstream SQLite
3.36.0 download retains its upstream archive identity check.

The private BuildKit daemon is pinned by digest in `tools/arm64/build.py`.
Its builder name derives from the checkout's absolute path; no global/default
builder is changed. The target is always `linux/arm64`, with
`-march=armv8-a -mtune=cortex-a35 -fsigned-char`. Host-native targeting and
legacy ARM32 `-marm`, `-mfpu`, `-mfloat-abi` options are excluded.
Bundled x86 assembly is disabled, and the existing non-x86 CPU and portable
arithmetic paths are retained. Pandora board flags, video defaults and input
policy are not imported.

Release timestamp `1754343618` and release node `a701547f4162` stay independent
of the port commit. Worktree snapshots preserve their dirty marker and list
every input file hash. `SOURCE_DATE_EPOCH` is fixed to the release timestamp.

## SDL, audio and runtime boundary

Official SDL 1.2 calls build against Debian's
[sdl12-compat](https://github.com/libsdl-org/sdl12-compat), which loads SDL2.
Export checks load that compatibility layer and initialize it without video
inside Debian. The symbol/dependency reports include SDL2, even though it is
loaded dynamically and omitted from the client's `ldd` output. This checks
the builder's loader, not the handheld's custom SDL2 2.32.10 display backend.

`USE_OPENAL=ON` selects the preserved legacy OpenAL backend, libsndfile and
mpg123 decoders. FMOD remains the default for existing builds. This port copies
only backend/decoder support and required interfaces; it retains 3.2.1 gameplay,
protocol, music defaults and server sound guards. Legacy OpenAL does not
implement 3.2.1's FMOD-dependent VoIP; the existing no-VoIP stubs are used while
ordinary sound and music remain compiled. Voice support is not established.
OPL, internal TiMidity and existing music streaming remain available; actual
effects/base-game music acceptance belongs to #5.
The existing optional FluidSynth path requests `libfluidsynth.so.1`; that
library is absent from this builder and no newer-SONAME compatibility is claimed.

Compare `evidence/dependencies.json` and the per-library symbol/version reports
with actual device libraries before launch. `DT_NEEDED` alone is insufficient:
SDL2, FluidSynth and graphics drivers can be loaded dynamically. The build
does not bundle Debian SDL2, Mesa, EGL, GLES or GBM over the handheld's vendor
stack. The saved [device snapshot](research/r36s-device-snapshot.md) establishes
Trixie, glibc 2.41 and GCC14-era libstdc++; a live symbol comparison and real
Mali/display/audio checks remain necessary during #3 and later runtime work.
Desktop GL/GLU linkage does not establish a working accelerated handheld path.

## Fresh rebuild and scoped cleanup

```sh
python3 tools/arm64/build.py cleanup
python3 tools/arm64/build.py build --output ../zandronum-arm64-artifacts/run-2
```

Cleanup runs `docker buildx rm` for this checkout's private builder only. It
removes its BuildKit container and cache volume. Host source, exported results,
other builders, Docker containers, images and volumes remain. Shared pulled
base/BuildKit images may remain in Docker's image store. No global prune or
volume deletion is used.

To rebuild an archived input without Git, extract `source.tar.gz`, create a
private Buildx docker-container builder, and build the extracted directory
with its `tools/arm64/Dockerfile` and `--platform linux/arm64 --output
type=local,dest=/absolute/new/output`. The archive contains substituted source
identity and the exact build-source manifest; no Mercurial installation is
needed. Remove that separately named builder after export.

## Validation recorded 2026-10-15

Two builds of the same dirty source snapshot at port commit
`b9bfdbae82a055e5b85c98b097933f00e54eae43` exported successfully. Between builds,
the private builder and cache volume were removed; the second build installed
all dependencies and ran all 612 Ninja steps from empty project storage.
The client reported `3.2.1-r250804-2140` and release node `a701547f4162`.
Graphical client, sound and OpenGL configuration checks passed, and all three
PK3s passed ZIP integrity checks. Four source-identity tests also passed:

```sh
python3 tests/source_identity.py
git diff --check
```

Both exports passed all 248 file checksums and their archived source hashes.
PK3s and the private SDL library matched byte for byte. Source manifests and
compiler commands matched. Client code/data matched; GNU build ID, debug-link
CRC and separate debug metadata differed between the cached and fresh builds.
Whole-ELF byte reproducibility is not established.

The built client's highest imported versions are `GLIBC_2.38`,
`GLIBCXX_3.4.32` and `CXXABI_1.3.9`. The builder used glibc
`2.41-12+deb13u4`, libstdc++ `14.2.0-19`, sdl12-compat `1.2.68-3`, SDL2
`2.32.4+dfsg-1`, OpenAL `1:1.24.2-1`, libsndfile `1.2.2-2+deb13u1` and mpg123
`1.32.10-1+deb13u1`. SDL compatibility initialization and all 260 required SDL2
symbol lookups passed in the builder. These are build/loader observations.

Cleanup after both builds retained all 2,848 host source files and both
exports. Unrelated `kukuband-postgres` and `ps2dev-sdk` containers, all four
existing volumes, images and the selected `desktop-linux` builder survived.
The project builder/cache are now removed. Local results reside outside Git
at `../zandronum-arm64-artifacts/issue-2/run-1` and `run-2`; their parent
`validation.json` records checks and artifact hashes. R36S access is needed
for #3's runtime comparison and launch work, not to reproduce this build.
