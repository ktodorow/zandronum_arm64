# R36S PortMaster test workflow

Status: personal launcher/package implemented on `r36s`, 2026-10-15.
[Package guide and launch evidence](r36s-package.md) record accelerated launch,
owner display observations, passing SELECT+START hardware replay and owner
confirmation of working updated build, completing launch/exit gate for
[issue #3](https://github.com/ktodorow/zandronum_arm64/issues/3).
The [main specification](specs/zandronum-3.2.1-arm64-r36s.md) defines acceptance;
the [build guide](arm64-build.md) records completed build-only checks.

## Package and launch contract

Create the dedicated R36S branch from ARM master for handheld packaging and
defaults. Keep common engine portability fixes on master.

Discover the installed ports directory on the device. The
[2026-10-14 snapshot](research/r36s-device-snapshot.md) found launchers under
`/roms2/ports`; implemented entries are `/roms2/ports/Zandronum.sh` and
`/roms2/ports/zandronum/`, confirmed against installed helpers in #3.

The game directory will hold the ARM64 executable, matching `zandronum.pk3`,
`brightmaps.pk3` and `skulltag_actors.pk3`, verified package-private libraries,
owner-supplied IWADs, configuration, saves and accessible logs. Preserve user
data when replacing build files. Verify the engine's actual config/save/IWAD
paths; an XDG environment variable alone does not establish redirection.

Inspect the installed PortMaster `control.txt`, dArkOS module and graphics
helpers before implementing the launcher. Use their discovered paths,
architecture selection, controller setup and platform/finish lifecycle.
Use native input where sufficient and GPTOKEYB where needed for remapping or
exit; verify that combined paths do not duplicate input. Cleanup must work
after normal exit and launch failure and leave the frontend usable.
The [local PortMaster research](research/local-porting-resources.md) records
candidate helper contracts; their installed behavior still needs testing.

## Required EmulationStation lifecycle

For every Zandronum device launch and runtime test, stop EmulationStation to
release RAM and bring it back afterward. Apply this to Ports-menu launches
and SSH-driven tests alike.

The installed `/roms2/ports/Grand Theft Auto Vice City.sh` and
`/roms2/ports/Minecraft Bedrock.sh`, inspected read-only on 2026-10-15, use
`systemd-run` detachment, `systemctl stop emulationstation.service` and cleanup
that resets failed state and starts the service again. The device reports
`KillMode=control-group`: stopping that service also terminates its remaining
child processes, so the Zandronum runner must first leave its control group.
See the [systemd kill-mode contract](https://raw.githubusercontent.com/systemd/systemd/v257/man/systemd.kill.xml).

Required launcher sequence for #3:

1. Establish a detached runner with the required user, groups and launch
   environment. Verify it is outside `emulationstation.service`; abort safely
   if detachment fails.
2. Register idempotent cleanup before stopping EmulationStation.
3. Stop `emulationstation.service` using installed privilege handling. Verify
   the service is inactive and the ES process is gone before starting the game.
4. Run the client with ES stopped. Log service state and memory observations
   before stop, after stop and after frontend restoration.
5. On normal exit, launch failure, game crash or handled interruption, clean up
   game/helpers, reset failed ES state if needed, start EmulationStation and
   verify the service is active and the frontend is usable again. Preserve
   the game's exit status in the log.

Device checks confirm detached runner survives ES shutdown and client starts
with ES stopped. Normal SELECT+START replay, launch failure, handled stop and
forced game/runner death restore active ES. Owner confirmed working updated
build after physical launch/chord/frontend check; see
[current evidence](r36s-package.md#evidence-recorded-2026-10-15).

## Deploy and test loop

1. Build/export on the Mac using the build guide and verify artifact checksums.
   Retain source identity, resource hashes and matching debug symbols.
2. With the device online over SSH, compare actual loaded-library paths,
   required ELF symbols/versions and dynamic SDL/graphics requirements with
   the export's evidence. Check the custom SDL2 and Mali stack. Select
   compatible package-private dependencies without replacing firmware libraries.
3. Copy the launcher and managed package files over SSH. Keep configuration,
   saves and IWADs intact. Record which build and content were installed.
4. Launch Zandronum from the handheld's Ports menu. SSH is used for deployment
   and diagnostics; physical display, sound and controller observations form
   acceptance evidence. Apply the required EmulationStation lifecycle above.
5. Capture stdout/stderr and exit status in accessible package logs. Record
   build identity, launch settings, actual runtime-library paths, renderer and
   display backend. Retain each failed run's evidence before updating files.
6. Exit using the handheld controls, verify return to the frontend, retrieve
   logs over SSH, fix the observed failure and repeat the same launch path.

Device access is required from #3 onward. The owner supplies Doom/Doom II data
and verifies screen, sound and physical controls. The agent handles build,
packaging, deployment, library checks and diagnostics. SSH credentials stay
outside repository docs and logs.

## First device gate: issue #3

- [x] Launcher uses the installed PortMaster/dArkOS contract and exposes logs.
- [x] Detached runner survives ES shutdown; ES is stopped while the client runs.
- [x] Normal exit, launch failure and interrupted/crashed game restore an active,
  usable EmulationStation frontend; logs record service state and memory.
- [x] Investigate accelerated EGL/GLES/gl4es presentation first and record the
  result. A working software path is an acceptable initial fallback.
- [x] With an owner-supplied IWAD, visibly present the menu or game at native
  **640×480 internal resolution and display output** on the physical R36S.
- [x] Missing IWAD produces an understandable requirement and clean return.
- [x] Basic controller exit returns to the frontend without interfering helper
  or display state; relaunch works.
- [x] Record observed launch, presentation, logs and return for the tested build.

Installed libraries, successful context creation and Docker compilation do not
complete this gate. Full controller mapping and renderer qualification follow.

## Single-player test sequence

| Issue | Acceptance through the PortMaster package |
| --- | --- |
| [#4](https://github.com/ktodorow/zandronum_arm64/issues/4) | Movement, aiming, actions, menus, essential text entry, saves and settings across relaunch, normal map changes. |
| [#5](https://github.com/ktodorow/zandronum_arm64/issues/5) | Audible effects and base-game music during actual gameplay. |
| [#6](https://github.com/ktodorow/zandronum_arm64/issues/6) | Qualify accelerated rendering and the software fallback for visible correctness and stability. |
| [#7](https://github.com/ktodorow/zandronum_arm64/issues/7) | Native 640×480, sustained at least 30 FPS in representative original maps and a heavier combat scene with effects/music active; aim for 60 FPS and record dips. |
| [#8](https://github.com/ktodorow/zandronum_arm64/issues/8) | Rebuild, install and accept the independently usable personal single-player package, including repeated launch/exit, persistence and controller-operated local content profiles. |

Issue #4 [controller/persistence scenario](r36s-single-player.md) covers
save-name grid, actual data paths, relaunch and normal map transitions.
Issue #5 [audio scenario](r36s-audio.md) covers effects/music, independent
volume controls, normal map continuity and retained settings after relaunch.
Issue #7 [performance evidence](r36s-performance.md) records native frame
intervals, explicit dip policy and passing guided software Doom/Doom II/MAP07
runs. Owner retains GL4ES default; its heavy scenes fail sustained-30 gate.
Short diagnostic probes cannot replace sustained combat acceptance.
Owner subsequently requested [local content launcher](r36s-launcher.md):
verify WAD/PK3 base selection, ordered WAD/PK3 combinations, profile naming and
reload, renderer/save isolation, missing content and menu cancellation through
same lifecycle. Launcher must exit before gameplay begins.

Final clean build/deployment and owner-requested closure, including explicit
limits on new physical checks, are recorded in [single-player delivery](r36s-single-player-release.md).

For each result, record date, build/resource/content identities, firmware and
PortMaster versions, renderer/settings, map/scene and repeatable duration,
observed behavior, relevant logs and pass/fail. Performance records include
sustained FPS and dip depth/duration. A smoke-test substitute must be labeled
and cannot replace original Doom/Doom II acceptance.

After #8 acceptance, extend the same packaged launch path to stock-server
compatibility and multiplayer/content workflows in #9–#17. Public PortMaster
catalog submission remains outside the personal-package scope.
