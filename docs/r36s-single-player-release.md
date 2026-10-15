# Personal single-player delivery, 2026-10-15

Issue [#8](https://github.com/ktodorow/zandronum_arm64/issues/8) records
owner-requested delivery and acceptance of personal R36S package. This is
single-player scope; stock-server compatibility remains unverified and belongs
to [#9](https://github.com/ktodorow/zandronum_arm64/issues/9).

Owner requested commit, push and closure after deployment, and explicitly
reserved further launcher/game runs for themselves. No launcher or game was
started on handheld during final update. Earlier physical qualification in
#3–#7 remains evidence for unchanged engine implementation; new launcher
workflow and final rebuilt executable have no additional physical playthrough.
Those limits are retained here rather than treated as new hardware passes.

## Source and build identity

- Release source: `a9f3a50af127abf33d62862b44fe438832320936`, branch `r36s`.
- Common ARM master ancestor: `5eee33fc1e15b88a17f2ab0ef3c9d4297183f07b`.
- Official release remains `ZA_3.2.1`, Mercurial node `a701547f4162` and
  timestamp `1754343618`; port commit does not replace compatibility identity.
- Client/resources and launcher rebuilt from clean detached Git worktree,
  excluding private untracked scratch. Disposable builder began without cache;
  pinned Debian image, signed `20261014T000000Z` snapshot and ARMv8-A/Cortex-A35
  flags remain unchanged. Build-time package installs occur only inside Docker.
- Package reuses verified pinned runtime export `issue-3/runtime-scoped`;
  runtime libraries were not rebuilt for this delivery.
- All 1,643 engine/resource/library inputs compared with physically qualified
  `issue-6/client-sky-fix` source manifest match. Changes add launcher, packaging
  and documentation; common engine fixes remain intact.

Final client SHA-256: `afb20febda42cd04d83d62c975151ad2671913be7599030cb00ff47cf709b185`.
Launcher SHA-256: `a31fc0ea3f2cf0fdc4285421b1eeb12146154d94e2784892c8f16d7c9c6fafb5`.

Exact source, generated identity, package versions, library requirements,
test logs, checksums and matching debug symbols reside in exports outside Git.
Installed package retains public build/dependency evidence and identities;
credentials, owner WADs, live profiles/configuration and saves are excluded.

## Acceptance record

| Area | Result and evidence |
| --- | --- |
| Clean client/resources export | Passed, clean source commit above; generated release identity preserved; resources and SDL compatibility library match qualified export byte-for-byte; ABI/dependency requirements unchanged |
| Clean C++ launcher | Passed ARM64 content tests, SDL controller replay and headless capture; binary matches already deployed generic launcher byte-for-byte |
| Managed deployment | Final clean package installed; all 291 managed hashes pass. All 22 current configuration/save/preference files retain hashes across update; no apps launched |
| Controller gameplay, menus, save/load, map changes | Owner physical Doom/Doom II checks and retained artifacts in [single-player guide](r36s-single-player.md) |
| Effects/music and saved audio choices | Owner listening, independent mute/volume, map continuity and relaunch in [audio guide](r36s-audio.md) |
| Rendering | Common node-angle/cache and sky-key fixes; owner-confirmed GL/software scenes and save relaunch in [rendering guide](r36s-rendering.md) |
| Native 640×480 performance | Optional software passes guided Doom E1M1, Doom II MAP01 and active MAP07 combat runs under recorded sustained-30/dip policy; [performance guide](r36s-performance.md) |
| Default renderer | Owner keeps GL4ES for higher peaks; sustained heavy-scene dips remain below 30 FPS. Software is qualified alternative |
| PortMaster lifecycle | Earlier actual launch/exit, SELECT+START, failure recovery and usable frontend in [package guide](r36s-package.md); final update starts no apps |
| Generic local content profiles | ARM64 headless SDL controller replay covers WAD/PK3 bases, IWAD/PWAD/PK3 add-ons, order, naming/reload, duplicate prevention and missing-file cancellation; [launcher guide](r36s-launcher.md). Physical new-menu-to-game check remains owner task |
| Supporting regression checks | Four source-identity tests, six frame-analysis tests and shell/whitespace checks pass |
| Offline runtime | Firmware/bundled-library boundary and no on-device package installation in distributed `DEPENDENCIES.md`; actual library resolution checked |

Successful content selection does not establish arbitrary-mod compatibility,
performance, standalone data completeness or stock-server interoperability.
Single-player delivery is accepted at owner's requested closure with physical
recheck limits above; multiplayer acceptance remains separate.

## Rebuild, install and use

Use fresh output paths outside checkout. Build from committed clean worktree
so private untracked access notes cannot enter source export:

```sh
git worktree add --detach ../zandronum-arm64-artifacts/issue-8/release-source a9f3a50af127abf33d62862b44fe438832320936
python3 ../zandronum-arm64-artifacts/issue-8/release-source/tools/arm64/build.py build --output ../zandronum-arm64-artifacts/issue-8/client-release
python3 ../zandronum-arm64-artifacts/issue-8/release-source/tools/r36s/build-launcher.py --output ../zandronum-arm64-artifacts/issue-8/launcher-release
python3 ../zandronum-arm64-artifacts/issue-8/release-source/tools/r36s/package.py --client ../zandronum-arm64-artifacts/issue-8/client-release --runtime ../zandronum-arm64-artifacts/issue-3/runtime-scoped --launcher ../zandronum-arm64-artifacts/issue-8/launcher-release --output ../zandronum-arm64-artifacts/issue-8/package-release
python3 ../zandronum-arm64-artifacts/issue-8/release-source/tools/arm64/build.py cleanup
git worktree remove ../zandronum-arm64-artifacts/issue-8/release-source
```

Runtime export can be reproduced with `tools/r36s/build-runtime.py` as described
in [package guide](r36s-package.md). Cleanup removes only release worktree's
private builder/cache; exported source/artifacts survive.

Copy package over existing `/roms2/ports/` installation without deleting owner
data, then check `zandronum/managed-files.sha256`. [Package guide](r36s-package.md)
provides SSH environment, install commands, launch contract and log locations.
No handheld `apt`, runtime downloads or compiler required.

Owner supplies WAD/PK3 files in `/roms2/doom/`, package `iwads/` or `mods/`.
Launch **Zandronum** from Ports, select any discovered WAD/PK3 as base, choose
ordered add-ons and renderer, optionally save named profile, then Play.
SELECT+START exits to frontend. Configuration, profiles and renderer-separated
saves survive managed-file updates; `zandronum/log.txt` records current run and
`logs/` retains previous output. Full file/control/save behavior is documented
in [launcher guide](r36s-launcher.md).
