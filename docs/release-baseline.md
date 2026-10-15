# Zandronum 3.2.1 source baseline

Issue: [#1](https://github.com/ktodorow/zandronum_arm64/issues/1).
Scope: source/history preservation and exported identity. No ARM client build,
handheld launch, or stock-server gameplay is claimed by these checks.

## Pinned upstream

| Identity | Value |
| --- | --- |
| Official Mercurial tag | `ZA_3.2.1` |
| Official Mercurial node | `a701547f4162dd965c25f082a55be2787310e6ea` |
| Equivalent Git mirror commit | `28f736fb3f77bf0b767ec5468191bc05d0fccbb9` |
| Imported Git tree | `951f47fc68595e7e136026c494e6822e22ac1de4` |
| Release commit timestamp | `1754343618` (`2025-08-04T21:40:18Z`) |
| Release revision display | `250804-2140` |
| Source-derived network byte | `194` (`1754343618 % 256`) |

Primary sources: [official changeset](https://foss.heptapod.net/zandronum/zandronum-stable/-/commit/a701547f4162dd965c25f082a55be2787310e6ea),
[Git mirror commit](https://github.com/TorrSamaho/zandronum/commit/28f736fb3f77bf0b767ec5468191bc05d0fccbb9),
[official tagged source archive](https://foss.heptapod.net/zandronum/zandronum-stable/-/archive/ZA_3.2.1/zandronum-stable-ZA_3.2.1.tar.bz2?ref_type=tags).

The import commit has the legacy tip and exact upstream release as parents; its
tree equals the upstream tree. All 2,779 tracked upstream files and executable
modes match the official Hg archive. Only `.hg_archival.txt`, added by Mercurial
when exporting, is excluded from that comparison. Downloaded archive SHA-256:
`19d453b50a7c7abd2b2ce699aae52a696594b1b3ecffa3b024ce49107f6c4865`.
This is our recorded download digest, not an upstream-published signature.

## Preserved history

- `legacy-arm-tip`: `92dad02ccd1abe1ed49f93e6f7c16641cf0f20d1`, including all five
  original commits. No resets, rebases, or replacements of existing commits.
- `upstream/ZA_3.2.1`: exact Git release commit, with upstream ancestry.
- `baseline/3.2.1-import`: import merge whose tree equals the release tree.
- `master`: common ARM client development, descending from that merge.

Locate old work, compare it with upstream, and inspect later port changes:

```sh
git show legacy-arm-tip
git log --oneline legacy-arm-tip
git diff legacy-arm-tip upstream/ZA_3.2.1 -- src tools CMakeLists.txt
git diff upstream/ZA_3.2.1 baseline/3.2.1-import
git diff baseline/3.2.1-import master
```

The fourth command must be empty. To reproduce the archive comparison, download
the linked official archive and run:

```sh
python3 tools/verify-release-baseline.py /path/to/official-3.2.1.tar.bz2
```

Preserve the named tags when transferring history. A normal source archive has
no history; use a bundle as well when recovery/comparison is needed:

```sh
git bundle create /path/to/zandronum-history.bundle master legacy-arm-tip upstream/ZA_3.2.1 baseline/3.2.1-import
git bundle verify /path/to/zandronum-history.bundle
git clone -b master /path/to/zandronum-history.bundle /path/to/restored-zandronum
```

The bundle includes both ancestries and the named preservation tags. These refs
are local until explicitly pushed with the implementation.

## Release identity and port provenance

`tools/updaterevision/release_identity.h` pins upstream Hg node, timestamp, and
revision display. Generated `HG_*` fields and existing `GetGit*` compatibility
accessors continue to identify that release. `NETGAMEVERSION` retains upstream's
calculation. No port commit timestamp or Mercurial lookup can change it.

Generated `GIT_*` fields instead record the port commit, ISO commit time, and
`arm64-<short-hash>[-dirty]` description. New `GetPort*` accessors expose these;
the engine console's `version_info` shows both identities. Dirty means tracked
working-tree/index changes; untracked files are excluded. A clean committed
export is the reproducible build input.

`source-provenance.txt` contains Git export substitutions. Keep placeholders in
the maintained Git tree. `git archive` fills them with the exported commit and
time. In a source-root Git checkout (including a linked worktree), current Git
provenance takes precedence; otherwise an expanded archive supplies it, even
inside an unrelated repository. Mercurial and Git are unnecessary for revision
generation from an export. Missing/invalid provenance fails the build rather
than leaving an unidentified or stale header. Unchanged output is not rewritten.

Export any committed revision; uncommitted and untracked files are not included:

```sh
python3 tools/export-source.py /path/to/zandronum-source.tar --ref master
tar -xf /path/to/zandronum-source.tar -C /path/to/source-directory
```

Repeated exports of the same commit produce identical tar bytes. The exported
root is `zandronum-arm64/`. Do not substitute the timestamp of a later tag-recording
or port commit for the release timestamp.

## Supporting validation

```sh
python3 tests/source_identity.py
```

Tests compile the actual revision generator and `src/gitinfo.cpp`, then check
the public version/revision accessors. Cases cover later Git commits, dirty
tracked changes, stale-header repair, unchanged-header timestamps, byte-identical
exports, archive generation without Git/Hg, unrelated enclosing repositories,
archive import into Git, and failure without valid provenance despite a stale
header. They require Python 3, Git, and C/C++ compilers (`CC`/`CXX` supported).

Compatibility remains **provisional and source-derived**. Issue #9 must prove
active gameplay alongside a PC client on an unmodified official 3.2.1 server,
after single-player acceptance. Byte `194` alone does not prove compatibility.
