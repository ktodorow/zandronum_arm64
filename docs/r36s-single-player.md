# R36S controller gameplay and persistence

Issue [#4](https://github.com/ktodorow/zandronum_arm64/issues/4), branch `r36s`.
Uses accepted [PortMaster launch/lifecycle](r36s-package.md) and unchanged
3.2.1 client from issue #3. Audio, renderer qualification and performance
remain separate gates. Physical acceptance results must be recorded below.

## Input profile

Installed GPTOKEYB supplies keyboard/mouse input. Native engine joystick
stays disabled, so only one path handles controller buttons in menus.
`gameplay.gptk` uses installed SDL mapping rather than guessed raw indices.
Read-only inspection on tested device found `GO-Super Gamepad` on `js0`
and `event2`, SDL GUID `190000004b4800000011000000010000`.
Four raw stick axes use Linux ABS codes 0, 1, 3, 4, range -1800..1800.
SDL normalizes these before GPTOKEYB applies its 6000-unit deadzone.
Left stick generates digital movement; right stick generates mouse motion.

| Handheld control | Gameplay | Menus |
| --- | --- | --- |
| Left stick | Forward/back, strafe | Use D-pad |
| Right stick | Yaw/pitch aim | Mouse selection disabled |
| D-pad | Forward/back, turn; automap pan | Navigate/change values |
| A | Use/open switches and doors | Select; choose save-name character |
| B | Open menu | Back/cancel |
| X | Jump | Use A for selection |
| Y | No gameplay binding | Yes on confirmation prompts |
| L1 / R1 | Previous / next weapon | — |
| L2 | Run/walk modifier | — |
| R2 | Fire | — |
| Left-stick click | Crouch | — |
| Right-stick click | Center view | — |
| SELECT | Automap | — |
| START | Open/close menu | Back |
| SELECT + START | Exit normally and restore frontend | Same |

Jump/crouch remain subject to map rules. Labels follow tested firmware SDL
mapping; confirm physical labels on owner's handheld rather than inferring
them from Linux button numbers. SELECT uses installed helper's `back`
hotkey; exit target remains anchored to explicit client argv.

## Configuration upgrade and paths

First launch applies `controls.cfg` after engine reads existing configuration.
It updates only profile keys, enables `m_showinputgrid` and disables menu
mouse selection. Existing config is copied once to
`config/zandronum.before-controls-v1.ini` before migration.
Engine archives `r36s_controls_profile=1` together with bindings. Later
launches detect that marker and keep owner binding/settings changes.
Failed initialization before engine writes configuration retries migration.
Deleting live INI creates fresh defaults and applies profile again.

| Owner data under `/roms2/ports/zandronum` | Engine contract |
| --- | --- |
| `config/zandronum.ini` | Explicit `-config`; `FGameConfigFile::GetConfigPath` |
| `saves/*.zds` | Explicit `-savedir`; `G_BuildSaveName` |
| `config/zandronum.before-controls-v1.ini` | One-time pre-profile backup |
| `log.txt`, `logs/*-previous.log` | Current/previous packaged launch evidence |

Package updates contain neither live INI nor saves; install without
`rsync --delete`. No XDG redirection claim substitutes for checking actual
engine-written files. Doom and Doom II share engine's Doom configuration;
save menu filters saves by content. Use matching IWAD when reloading.

## Repeatable physical scenario

Retain logs from each run outside checkout, together with package identity,
IWAD SHA-256, map names and observations. Always use Ports launcher or
same `/roms2/ports/Zandronum.sh`; runner stops/restores EmulationStation.

1. Launch **Zandronum** from Ports. With D-pad/A, select New Game and ordinary
   single-player difficulty. Verify each D-pad tap moves one menu item.
2. On Doom II MAP01, move forward/back and strafe with left stick. Aim both
   horizontally and vertically with right stick, then release sticks and
   check motion stops. Fire R2, change weapons L1/R1, use A at door/switch,
   test run modifier, automap and center view. Check jump/crouch where allowed.
3. START → Save Game → new slot. Built-in character grid appears. Use D-pad
   and A to enter `R36S` (or another unique name). Bottom-row **BS** removes
   character; **ED** completes entry; B cancels. Confirm save returns to play.
   Never require external keyboard for this step.
4. Move to visibly different location. START → Load Game → select named
   save → A; confirm saved location/progress returns. Cancel another save-name
   entry with B and check no unintended slot appears.
5. Change identifiable setting through ordinary menu, for example mouse
   sensitivity. Record chosen value. Exit with SELECT+START; verify frontend
   usable. Check log records exit 0 and no helper remains.
6. Relaunch from Ports. Confirm setting retained, then load named save and
   verify location/progress. Log must say saved controls/settings kept;
   migration must not run again. Actual live INI and `.zds` files must be
   under paths above. Keep pre-profile backup unchanged.
7. Complete MAP01 through normal level exit, continue through intermission
   into MAP02, and keep playing. Save/reload after transition. Do not replace
   normal transition with `map`/`changemap`/`nextmap` command.
8. Repeat with owner-supplied Doom E1M1 → E1M2 and separate named save.
   SSH may select other installed IWAD without moving owner files:

   ```sh
   ssh "$R36S_SSH_HOST" 'ZANDRONUM_IWAD=/roms2/doom/doom.wad bash /roms2/ports/Zandronum.sh'
   ```

9. Exit with SELECT+START. Retain both content hashes, before/after INI,
   save files, logs and physical observations. Sound belongs to #5.

## Supporting replay

Inspector derives evdev sibling, button/axis maps and actual axis ranges
from `js0` and installed `/tmp/gamecontrollerdb.txt`. No controls means
read-only inspection. Replay writes through actual controller evdev node,
only while packaged client runs with ES stopped. Synthetic replay checks
helper/engine path; physical feel/display/normal gameplay still need owner.
Wait for visible menu/map before replay: running process alone does not prove
startup complete. Do not replay while owner is playing.

```sh
scp tools/r36s/replay-input.py "$R36S_SSH_HOST:/tmp/zandronum-replay-input.py"
ssh "$R36S_SSH_HOST" 'python3 /tmp/zandronum-replay-input.py'
ssh "$R36S_SSH_HOST" 'python3 /tmp/zandronum-replay-input.py dpdown a'
ssh "$R36S_SSH_HOST" 'python3 /tmp/zandronum-replay-input.py --hold .5 leftx=1'
ssh "$R36S_SSH_HOST" 'python3 /tmp/zandronum-replay-input.py --chord back start'
```

Original exit regression additionally checks exit status, helper cleanup
and active frontend:

```sh
scp tools/r36s/check-controller-exit.py "$R36S_SSH_HOST:/tmp/zandronum-controller-exit-check.py"
ssh "$R36S_SSH_HOST" 'python3 /tmp/zandronum-controller-exit-check.py'
```

## Evidence, 2026-10-15

Device and client match [issue #3 evidence](r36s-package.md#evidence-recorded-2026-10-15).
Issue #4 package verifies 273 managed hashes on device; existing live INI
preserved during deployment. Engine logs successful controls-profile
execution at native 640×480 with GL4ES/Mali-G31. Actual inspected SDL map,
button codes and axis ranges are available from inspector above.

| Acceptance check | Result |
| --- | --- |
| Profile execution through packaged launcher | Passed: engine reports profile 1 applied |
| Device input identity/ranges | Inspected live; owner confirmed physical movement, aim, fire, use and menus |
| Weapon selection | Corrected literal bracket console bindings; replay visibly changes pistol to fist; owner confirmed physical recheck |
| Save-name entry and save/load | Controller-grid replay created `A` in `save0.zds`; owner loaded it after relaunch, made new named saves and confirmed readable grid/save/load using controls |
| Settings and relaunch | Ordinary-menu replay changed sensitivity 1.0 → 1.1; clean exit archived value and corrected bindings; next launch kept profile and next clean exit retained 1.1 |
| Normal Doom II transition | Owner completed MAP01 → MAP02 and continued play; logs contain both maps and MAP02 autosaves |
| Normal Doom transition | Owner completed E1M1 → E1M2, saved/reloaded using controls; `save3.zds` (`SAF`) records E1M2 and loaded again after packaged relaunch |
| Exit/frontend | Repeated SELECT+START exits report status 0 and active restored frontend |

Owner reports missing floor/wall surfaces and visibility through walls during
gameplay. Renderer correctness remains **failed/unaccepted**, tracked by
[#6](https://github.com/ktodorow/zandronum_arm64/issues/6); input/persistence
results do not accept overall single-player milestone. Retained
`doom-relaunched-e1m2.png` and matching `save3.zds` preserve E1M2 view for
renderer diagnosis; compare surfaces with reference rendering in that issue.

Pre-profile backup SHA-256 matches original downloaded INI:
`ce744bf15391e333012bd471b856ced46bb32985084599c08bf74c2bce3e7313`.
Final package redeployment preserved live INI, backup and `save0.zds` hashes.
Supporting diagnostic replay used console shortcuts to open save/load/options
menus; it does not substitute for physical menu/navigation acceptance.

| Tested artifact | SHA-256 |
| --- | --- |
| Client, unchanged from #3 | `c7cecc51304e883d4c23e546785b39dab4161ac1cc18f8a478fff76c0942371e` |
| Final `managed-files.sha256`, 273 passing entries | `94df0a8e98cc861efa2c938ed7f2f14e23573b09c694df9579ee106de50548bd` |
| Owner's `doom.wad` | `03103e82064a960b548a98eb9656f1f30545458eb437d99475a962053b1f8fcd` |
| Owner's `doom2.wad` | `31740ef23994b3959800134b41aaf86b04a2847336d328af8c4ae890450630ab` |

Retained evidence includes `doom2-transition.log`, `save-settings-replay.log`,
`doom2-physical-save-load.log`, controller inspection, replay traces,
screenshots, before/after INI and actual save files. Owner's controller-named
Doom II saves are `save1.zds` (`d`) and `save2.zds` (`TST`).
`doom-physical-transition-save-load.log` records normal Doom transition;
`doom-relaunch-save-load.log` records loading `SAF` after relaunch and exit 0.
One early automation probe sent exit during initialization and invoked helper's
forced-kill fallback (137); frontend recovered. Retained separately as
`doom-relaunch-premature-exit.log`, excluded from passed gameplay runs.

Issue #4 input/menu/persistence checks passed with owner's physical checks
and supporting artifacts. Audio, renderer correctness, performance and full
single-player milestone acceptance remain later gates.

Local evidence: `../zandronum-arm64-artifacts/issue-4/`. Credentials remain
outside tracked docs and package.
