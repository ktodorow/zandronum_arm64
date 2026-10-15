# Zandronum 3.2.1 ARM client design interview

Status: shared understanding confirmed by the user on 2026-10-14; interview complete. The user subsequently amended the delivery order to single-player first and multiplayer last. This file records the current scope, researched facts, and implementation sequence. Implementation has not started.

## Confirmed decisions

- Build a playable Zandronum **3.2.1 client**; a server-only build is insufficient.
- **Milestone 1 is single-player.** Establish and test Doom/Doom II gameplay, PortMaster integration, controls, audio, persistence, rendering, and performance before beginning multiplayer work. This is an independently acceptable personal delivery.
- **Milestone 2 is multiplayer, after single-player acceptance.** Stock-server compatibility, server browsing, general controller text entry, and Handheld content setup remain required for overall completion.
- **Linux ARM64** is the first platform. Additional operating systems are not part of the first milestone.
- Preserve compatibility with **unmodified official 3.2.1 servers** and existing PC clients.
- Establish **base Doom/Doom II** gameplay on the R36S before testing selected mods.
- Deliver **personal builds for the owner's machines**. Public distribution, upstream submission, and a public PortMaster release are not required.
- The owner's R36S runs **dArkOS by christianhaitian**.
- General ARM client work belongs on `master`; the user selected a **dedicated R36S branch derived from that ARM master**. Common portability fixes should remain shared through master.
- Start from the **exact official 3.2.1 release**, selectively bringing over reviewed legacy portability fixes. See [ADR 0001](adr/0001-use-official-3.2.1-release-baseline.md).
- R36S gameplay must use **native 640×480**, with **30 FPS as the minimum target and 60 FPS desirable**. Reduced internal resolution is not an accepted substitute.
- Investigate accelerated rendering first. **Either renderer is acceptable** if it meets the current milestone's native-resolution gameplay, performance, sound, and input requirements; multiplayer compatibility is validated in the later milestone.
- Use a **disposable Docker Linux ARM64 build environment** on this Mac, with runtime tests on the actual R36S. Debian 13 Trixie matches the inspected device userland; pin the eventual image and dependencies. Export build artifacts while retaining source on the Mac, and provide project-specific cleanup.
- The later multiplayer deliverable must include **server browsing and content setup entirely on the handheld**; saved server launchers alone are insufficient for that milestone. These features do not gate the initial single-player delivery.
- **PortMaster is required** for launching/running the R36S client. This is a personal package, not a requirement to publish to the PortMaster catalog.
- The R36S is networked and **SSH access has been verified**. Its firmware/runtime were inspected read-only; see the [device snapshot](research/r36s-device-snapshot.md).
- Handheld content setup must **find/download available missing or mismatched server add-ons, verify identity and load order, and join on-device**. Unavailable exact content must produce a clear failure. Base-game performance acceptance does not guarantee performance for arbitrary downloaded mods.
- Browser scope: **public and existing LAN discovery, filters/details, manual connection, and controller text entry**. Favorites/history and full Doomseeker feature parity are deferred.
- Judge single-player frame rate first using **representative original maps and a heavier combat scene**. Require sustained 30 FPS, aim for 60, and record brief dips. Add **stock-server multiplayer** measurements with the same targets in Milestone 2.
- The owner will **add Doom/Doom II IWADs to the handheld's Doom folder later**. Original-game gameplay/performance tests depend on those files becoming available.

## Settled design tree

- Official 3.2.1 source identity — researched; exact release baseline selected.
  - Mercurial `ZA_3.2.1` / `a701547f4162dd965c25f082a55be2787310e6ea`; Git `28f736fb3f77bf0b767ec5468191bc05d0fccbb9`.
  - Selectively port reviewed fixes; do not upgrade the old engine forward.
  - Preserve release revision metadata and validate stock-server compatibility at runtime.
  - Preserve the legacy tip and existing commits; integrate the release baseline into `master` without discarding repository history. Exact Git integration commands are an implementation detail to review before mutation.
- Linux ARM64 client — platform selected.
  - Docker Linux ARM64 is the preferred builder. The actual device uses Debian 13 Trixie ARM64/glibc 2.41, with custom SDL2 2.32.10 and Mali graphics libraries.
  - Pin and validate the target-compatible build environment during implementation.
  - The single-player milestone requires a graphical client with responsive input, sound effects and base-game music, and matching engine resources. Successful stock-server gameplay follows in the multiplayer milestone.
- R36S client — device, firmware family, and initial content scope selected.
  - Firmware `dArkOS (09022026)` and SSH networking have been inspected.
  - Native 640×480, minimum-target 30 FPS, goal 60 FPS; representative original-map tests first, then multiplayer tests in the later milestone, with brief dips recorded.
  - Investigate accelerated rendering; software meeting the same targets is an accepted alternative.
  - Maintain a dedicated R36S branch derived from the ARM master.
  - Provide PortMaster launch, handheld controls, configuration persistence, and clean exit.
  - After accepting single-player, reuse the in-engine browser as the first multiplayer implementation candidate; add general controller text entry and checksum-aware content downloading/restart integration. Investigate a small Wadseeker-based helper before adapting a whole desktop browser.
  - Later multiplayer scope: public/LAN lists, filters/details, manual connection; downloaded matching server add-ons and readable failure for unavailable files.
- Personal delivery — scope selected.
  - Build in Docker; first validate physical display, controls, audio, local gameplay, persistence, and frame rate on the R36S over the available SSH deployment/logging channel. Validate network gameplay and content setup after single-player acceptance.
  - The owner will supply original-game IWADs in the Doom folder; runtime acceptance waits for them.
  - Final shared understanding confirmed by the user.

## Agreed implementation sequence

1. **Establish the release and build environment.** Preserve the legacy reference/history, integrate the exact 3.2.1 base, retain release protocol metadata independently of port commits, and create a pinned disposable Linux ARM64 Docker builder. Record actual runtime requirements against the device snapshot, including its custom SDL and vendor graphics libraries. Do not use Apple CPU-specific `-march=native` output for the RK3326.
2. **Make the general ARM client work locally on master.** Audit remaining CPU/ABI assumptions and port only necessary legacy fixes. Establish graphics presentation, input, sound effects, base-game music, and generated resources. Preserve release compatibility identity now; defer actual server joins and multiplayer validation. Evaluate SDL 1.2 through sdl12-compat before deciding whether SDL2 platform migration is necessary; bring over useful OpenAL work rather than assuming the old FMOD runtime supports the target.
3. **Adapt the client on the dedicated R36S branch.** Package for the installed PortMaster lifecycle, discover its actual paths/hooks, and establish controller mappings, settings/saves, logging and clean exit. Investigate gl4es/EGL acceleration at native resolution and compare a working software-renderer path. Either may satisfy acceptance; failure of both to meet the target requires revisiting the plan, not silently reducing resolution.
4. **Validate and accept the single-player milestone on the actual R36S.** With the supplied Doom/Doom II data, test representative original maps and a heavier combat scene at native 640×480. Record sustained frame rate and dips, effects/music, controller/menu use, save/config persistence, map changes, and launch/exit. Rebuild the personal package and provide usage/build/log/cleanup instructions. No server join, browser enhancement, or add-on download is needed to accept this milestone.
5. **Complete multiplayer last.** After single-player acceptance, prove joining and playing on an unmodified official 3.2.1 server alongside PC clients. Keep the existing browser where viable; provide readable discovery/details and controller-only text entry. Obtain exact server content metadata, retrieve available missing/mismatched add-ons, verify matching content and order, and restart/join appropriately. Include progress/cancel behavior and clear errors for unavailable content.
6. **Validate the multiplayer milestone and overall package.** Test public/LAN discovery, manual connection, and a controller-only discovery → absent downloadable add-on → content match → join flow. Measure stock-server multiplayer at native 640×480 against the same sustained-FPS target, then rerun affected single-player checks. Additional mod performance is a later validation scope.

## Acceptance gates

### Milestone 1: single-player

- Reproducible ARM64 **client** build with matching engine PK3/resources and preserved release compatibility identity. Runtime stock-server compatibility is still unverified at this point.
- PortMaster launch and return to the frontend, responsive controller gameplay and single-player menus, audible effects/music, persistent settings/saves, and normal map transitions.
- Native 640×480 with sustained **at least 30 FPS** in representative original maps and a heavier single-player combat scene; aim for 60 and record brief dips.
- An independently usable personal single-player package with build/deployment/data-placement/logging/cleanup instructions. Multiplayer browser/content work does not gate acceptance.

### Milestone 2: multiplayer and overall completion

- Join and play with an **unmodified official 3.2.1 server** and existing PC clients using matching content; test normal map transitions.
- Discover public/LAN servers and use filters/details/manual connection on the handheld; download and match available server-required add-ons entirely using its controls before joining.
- Provide general controller text entry, clear content/download failures, and cancellation. Saved server launchers or desktop-prepared add-ons alone do not satisfy the multiplayer workflow.
- Meet the same native-resolution sustained-FPS target in representative stock-server multiplayer and recheck affected single-player behavior/performance on the integrated package.

No achieved frame rate, broad mod compatibility, or working graphical ARM client is claimed by the research. Measurements and integration outcomes remain future work; the architectural scope, acceptance gates, and fallbacks above were confirmed by the user.

## Research

The research notes distinguish source identity and source-level feasibility from successful builds and observed gameplay. No working ARM client or measured R36S performance has yet been established in this interview.

- [Upstream release research](research/upstream-3.2.1.md)
- [Legacy ARM fork audit](research/current-arm-fork.md)
- [R36S and dArkOS feasibility](research/r36s-feasibility.md)
- [Docker build environment research](research/docker-build-environment.md)
- [Local PortMaster resources](research/local-porting-resources.md)
- [Handheld server-browser research](research/handheld-server-browser.md)
- [Actual R36S device snapshot](research/r36s-device-snapshot.md)
