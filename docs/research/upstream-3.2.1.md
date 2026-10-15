# Identifying the Zandronum 3.2.1 release base

Checked: **2026-10-14**. This is source and release research, not a successful build or gameplay report. No repository branches, refs, engine files, or dependencies were changed during this investigation.

## Findings

**Pin the official Mercurial tag `ZA_3.2.1`, changeset `a701547f4162dd965c25f082a55be2787310e6ea`.** Its Git mirror equivalent is `28f736fb3f77bf0b767ec5468191bc05d0fccbb9`. None of the current branch heads in the requested GitHub repository identifies the released Zandronum 3.2.1 source tree. The release is an older commit in `master` history. The official [tag listing](https://foss.heptapod.net/zandronum/zandronum-stable/-/tags), [release changeset](https://foss.heptapod.net/zandronum/zandronum-stable/-/commit/a701547f4162dd965c25f082a55be2787310e6ea), [Git release commit](https://github.com/TorrSamaho/zandronum/commit/28f736fb3f77bf0b767ec5468191bc05d0fccbb9), and [following Git tag-recording commit](https://github.com/TorrSamaho/zandronum/commit/0dc8d30cc8dcd9dd92609a6f01c6c57125f35794) establish this mapping.

**3.2.1 remains the latest official stable release** on the date checked. The [official download page](https://zandronum.com/download) lists its release date as 4 August 2025 and separately offers development build `3.3 260925-2352`, released 10 October 2026. The [official announcement](https://zandronum.com/forum/viewtopic.php?t=11430) describes 3.2.1 as a maintenance release. Official downloads listed there are Windows, macOS, and Linux x86_64 packages; this does not establish a native ARM client release.

The [requested GitHub repository](https://github.com/TorrSamaho/zandronum) explicitly identifies itself as a Git mirror of the Heptapod Mercurial repository. Its current `master` reports `3.3-alpha` and `BUILD_INTERNAL`, and the [commit that begins 3.3 development](https://github.com/TorrSamaho/zandronum/commit/7279c8bc1d6200d356b39a7de264e1a6db714bf9) dates to 9 August 2025. The mirror also trails the current official development branch: the [Heptapod branch API](https://foss.heptapod.net/api/v4/projects/1309/repository/branches) returns default-branch changeset `0bf8926534b39096bc4e60573f2b933c021267db`, dated 25 September 2026, while GitHub `master` stops at a June 2026 commit. Do not treat a repository or branch name containing “stable” or “master” as release proof.

## Branch and tag matrix

| Repository / ref | Identity checked | Is this the released 3.2.1 base? |
| --- | --- | --- |
| Heptapod `ZA_3.2.1` | Hg `a701547f4162dd965c25f082a55be2787310e6ea`; `3.2.1`, `BUILD_RELEASE` | **Yes. Preferred authoritative pin.** |
| GitHub pinned release commit | Git `28f736fb3f77bf0b767ec5468191bc05d0fccbb9`; same release version change | **Yes. Suitable Git source pin, subject to the revision metadata issue below.** |
| GitHub tag-recording commit | Git `0dc8d30cc8dcd9dd92609a6f01c6c57125f35794`; only adds the Hg tag to `.hgtags` | Same engine content, but a different commit timestamp; prefer the tagged release commit. |
| GitHub `master` | Git `bdd0f7beb43d9786cc13502395f60aa84d34e28d`; `3.3-alpha`, `BUILD_INTERNAL` | No. Development, beyond the release. |
| Heptapod `branch/default` | Hg `0bf8926534b39096bc4e60573f2b933c021267db`; `3.3-alpha`, `BUILD_INTERNAL` | No. Current development. |
| GitHub `hittarget-2445` | Git `5ada4df1f64386ee9d9826e21986f8b4cfd2701b`; `3.0-alpha`, `BUILD_INTERNAL` | No. Older Zandronum experiment. |
| Other GitHub branch heads and Git tags | Source version headers identify older ZDoom/GZDoom branches; no live Git tag `ZA_3.2.1` or `3.2.1` | No. Numeric Git tags such as `2.8.1` belong to the inherited engine history. |

Evidence: live `git ls-remote --heads --tags https://github.com/TorrSamaho/zandronum.git`, inspection of every branch-head `src/version.h`, [pinned release version header](https://github.com/TorrSamaho/zandronum/blob/28f736fb3f77bf0b767ec5468191bc05d0fccbb9/src/version.h#L47), [pinned mirror master version header](https://github.com/TorrSamaho/zandronum/blob/bdd0f7beb43d9786cc13502395f60aa84d34e28d/src/version.h#L47), and [official default branch header](https://foss.heptapod.net/zandronum/zandronum-stable/-/raw/branch/default/src/version.h). Branch heads can move after this report.

## Revision metadata affects stock-server compatibility

The release [revision generator](https://github.com/TorrSamaho/zandronum/blob/28f736fb3f77bf0b767ec5468191bc05d0fccbb9/tools/updaterevision/updaterevision.c#L71) runs Mercurial commands, despite Git-related names. It writes `HG_REVISION_NUMBER` from the commit's Unix timestamp. If Mercurial information is unavailable and no generated header already exists, its fallback writes zero and unknown revision information. It does **not** parse `.hg_archival.txt`. Existing generated metadata is retained when Mercurial lookup fails.

This is more than a diagnostic string. [Release `version.h:83`](https://github.com/TorrSamaho/zandronum/blob/28f736fb3f77bf0b767ec5468191bc05d0fccbb9/src/version.h#L83) defines `NETGAMEVERSION` as `GetRevisionNumber() % 256`; [gitinfo.cpp:82](https://github.com/TorrSamaho/zandronum/blob/28f736fb3f77bf0b767ec5468191bc05d0fccbb9/src/gitinfo.cpp#L82) returns `HG_REVISION_NUMBER`. The [client sends this byte](https://github.com/TorrSamaho/zandronum/blob/28f736fb3f77bf0b767ec5468191bc05d0fccbb9/src/cl_main.cpp#L908), and the [server rejects a mismatch](https://github.com/TorrSamaho/zandronum/blob/28f736fb3f77bf0b767ec5468191bc05d0fccbb9/src/sv_main.cpp#L2044), including release builds: there is no `BUILD_INTERNAL` condition around that comparison.

The official tagged changeset timestamp is `2025-08-04T21:40:18Z`, so building the pinned tag with the upstream generator produces `HG_REVISION_NUMBER=1754343618`, expected protocol byte **194**, and revision time `250804-2140`. This is a calculation from the [official changeset metadata](https://foss.heptapod.net/zandronum/zandronum-stable/-/commit/a701547f4162dd965c25f082a55be2787310e6ea), not an observed packet from an official executable. Before declaring the port compatible, verify it joins an unmodified official 3.2.1 server with matching game data, then verify gameplay remains synchronized.

The [official tagged source archive](https://foss.heptapod.net/zandronum/zandronum-stable/-/archive/ZA_3.2.1/zandronum-stable-ZA_3.2.1.tar.bz2?ref_type=tags) was downloaded and inspected. Its `.hg_archival.txt` records the release node and tag, but it contains no `gitinfo.h`, and the generator ignores that archival file. A Git-based port therefore needs an explicit, reproducible way to preserve the release compatibility identity while recording the port's own source revision separately. An unmodified Mercurial checkout at `ZA_3.2.1` supplies the upstream metadata naturally. Changing the displayed version string alone does not make a different engine tree compatible.

## Build feasibility and next gate

The [release CMake configuration](https://github.com/TorrSamaho/zandronum/blob/28f736fb3f77bf0b767ec5468191bc05d0fccbb9/src/CMakeLists.txt) includes the graphical client, software and OpenGL rendering, SDL, sound, and `SERVERONLY` options. It still searches for FMOD Ex and uses architecture assumptions such as identifying a 64-bit target by pointer size. These facts identify porting work to investigate; they do not prove ARM build or runtime success. A server-only build cannot satisfy the user's playable-client requirement.

No configure, compile, launch, or multiplayer test was run here. The interview host reports `arm64` and currently has neither `cmake` nor `hg` on its command path. The next implementation gate is an isolated build of the pinned release for a chosen ARM operating system and ABI, with explicit revision metadata, graphics, sound, controls, and stock-server join checks.

Recommendation for the project: once branch-history handling is agreed, base the project's `master` ARM client work on the pinned 3.2.1 release. Keep an upstream-tracking development ref separate. Put R36S-specific packaging, input defaults, renderer integration, and measured performance changes on a branch derived from the working ARM client base. No branch reset or source transplant is implied by this research.

## Verification record

- Read-only Git mirror clone: `/tmp/zandronum-upstream-research-20261014`.
- Official tagged archive inspected: SHA-256 `19d453b50a7c7abd2b2ce699aae52a696594b1b3ecffa3b024ce49107f6c4865` for the archive downloaded on this date; this is a locally recorded download digest, not an upstream published checksum.
- Primary pages returned bot-protection errors through the web browsing tool; ordinary `curl` reads of the official download page, announcement, Heptapod tag listing/API, raw source, and archive succeeded.
