# R36S single-player content launcher

Small C++17/SDL2 menu, launched by PortMaster before Zandronum. Select a base
WAD or PK3, combine local WAD/PK3 add-ons in explicit order, choose native GLES/GL4ES/software,
and save named combinations. Menu exits completely before gameplay.
Native GLES is default when installed; GL4ES/software remain selectable.
This menu does not establish compatibility or performance of arbitrary mods.

Installation needs no `apt`, package downloads or on-device compilation.
`apt-get install` in launcher recipe runs only in disposable Docker builder
on Mac. No packages were installed on handheld for launcher. Actual device
`ldd -r` resolves launcher through existing firmware SDL2, libstdc++, libgcc,
libc, libm and SDL2's libsamplerate dependency. Baseline snapshot already
records same SDL2 and C++ runtime before port development. Additional runtime
libraries absent from supported firmware must ship inside port, with their
transitive dependencies; build dependencies and SDKs are not runtime payload.
See distributed `DEPENDENCIES.md` for firmware/bundle boundary.

## Files and controls

Supply your own base games in `/roms2/doom/` or
`/roms2/ports/zandronum/iwads/`. Put add-ons in
`/roms2/ports/zandronum/mods/`; subfolders work. Menu also scans WAD/PK3 files
under `/roms2/doom/` and `iwads/`. Every discovered WAD and PK3 appears in both
Base game and Add-ons: no filename whitelist, IWAD/PWAD-header filter or
game-specific recipe. Choose file roles yourself; engine checks whether base
contains required game data and whether combination is compatible. A PK3 can
be selected as base, but an add-on-only PK3 still needs its intended base game.
Selected base shows `[BASE]` in Add-ons and cannot load twice. Choosing an
existing add-on as base removes it from add-on order. Hidden files/folders, including macOS
`._` files, are ignored. Canonical paths deduplicate overlapping roots.
Scan stops at depth six or 20,000 entries and reports unreadable folders/limit.
Optional `config/launcher/paths.txt` adds one absolute local folder per line;
lines beginning `#` are comments. No downloads or IWAD distribution.

| Screen | Controls |
| --- | --- |
| All menus | D-pad/left stick navigate; A select; B back; Y rescan |
| Add-ons | A toggles selected file; bracket number shows load order |
| Load order | L1 earlier; R1 later; X removes file; B done |
| Name profile | D-pad/left stick chooses character; A adds; X deletes; Y adds space; START saves; B cancels |
| Home | Play launches selection; B/Exit returns to Ports |
| Any screen | SELECT+START exits to Ports |

Later add-ons override earlier files where engine supports that behavior.
Maximum 128 selected add-ons. On **Profiles**, **Save current** names or
updates selected profile; **Save as new** creates another without overwriting
previous profile; **New combination** starts fresh. Saved entries load their
base file, add-on order and renderer. Names allow up to 32 characters. Changes
persist as last selection on Play/Exit; use Save current to update named entry.

Missing or moved content stays visible in loaded profile. Play reports an
error without starting engine; choose replacement base file or remove missing
add-on through Load order. Y refreshes files copied while menu was open.
Profile corruption reports an error without executing its text.

## Persistence and launch boundary

Profiles are owner data under `config/launcher/` and survive package upgrades:

- `last.profile`: most recent combination, restored on next normal launch.
- `profiles/profile-<timestamp>.profile`: named combinations.
- `paths.txt`: optional additional scan folders.

Native upgrade changes current selection's renderer once, preserving base,
add-ons/order/name and named profiles. `last.before-gles-v1.profile` backs up
previous selection; `gles-default-v1` prevents repeated overrides. Choosing
GL4ES/software later persists normally. Without native executable, legacy
packages retain GL4ES default.

Profiles use versioned quoted text; filenames may include spaces, quotes,
backslashes and shell characters. Menu validates selected files and writes
an atomic NUL-delimited handoff. Runner reads Bash array elements and passes
individual `-iwad` and ordered `-file` arguments; it never evaluates profile text.

Plain `doom.wad`/`doom2.wad` without add-ons retain existing
`config/zandronum.ini`, GL `saves/` and software `saves/software/`.
Native uses `config/gles/zandronum.ini` seeded from existing config and
`saves/gles/`; controls/audio/settings preserved on subsequent launches.
Other combinations use a deterministic `set-<16 hex digits>` key derived
from base path and ordered add-on paths:

- `config/profiles/<key>/zandronum.ini`, copied from current base settings once.
- `saves/profiles/<key>/gl4es/` or `saves/profiles/<key>/software/`.
- Native: `config/profiles/<key>/gles/zandronum.ini` and `saves/profiles/<key>/gles/`.

Renaming a profile keeps its saves. Changing paths or order selects another
save/config set. Replacing a file in place keeps same set; engine save/mod
compatibility still applies. Renderer saves remain separate. Profiles with
identical content share settings/saves regardless of display name.

Normal no-argument PortMaster launch opens menu. `ZANDRONUM_LAUNCHER=0`,
explicit `ZANDRONUM_IWAD`, or extra engine arguments bypass menu for scripted
diagnostics. Renderer environment/preference initializes fresh combinations;
last/named selections retain their chosen renderer. Engine still receives
native 640×480 fullscreen settings. Native SDL2 menu closes before PortMaster
GPTOKEYB and engine start; both cancel and failure restore frontend.

## Build and validation

Build launcher with `python3 tools/r36s/build-launcher.py --output <outside-checkout>`.
Pinned Debian snapshot, GCC14, ARMv8-A/Cortex-A35 flags and firmware SDL2 ABI;
no SDL_ttf, font assets, Python or browser runtime in menu. Export contains
binary, behavioral-test result, headless screenshot, ELF/dependency records,
exact recipe/source, provenance and checksums. Package builder requires
`--launcher <export>` and verifies all three export manifests.

Content tests cover recursive discovery, deduplication, load-order changes,
quoted profile round-trip, exact NUL argument boundaries, atomic failed launch,
missing/duplicate files and malformed profiles. Headless SDL screenshot checks
complete UI initialization, rendering and shutdown. Controller-event replay
drives actual menu to select Doom, toggle WAD and PK3, reverse order, select
software, name/save profile using grid, restart SDL, reload named profile and
produce identical launch handoff. Additional controller replays select PWAD
and PK3 bases, combine IWAD/WAD/PK3 add-ons and prevent duplicate base loading.
Content tests cover every discovered file in either role without filename
preferences. Missing add-on leaves menu usable and cancel
produces no handoff. These checks pass in ARM64 builder.

## Device status, 2026-10-15

Installed on existing dArkOS R36S without replacing firmware libraries.
All 291 managed files verify after generic base/add-on role update and final
clean rebuild, including
controller-replay evidence and offline dependency instructions. All 22 current
configuration/save/preference files retain their hashes across this update;
original 17 configuration/save files were also checked at initial installation.
Device `ldd -r` resolves launcher dependencies.
Detached normal PortMaster launch stops frontend, opens native 640×480 menu
using firmware SDL2 GLES2, and recognizes GO-Super Gamepad through installed
mapping. Menu and helper/client are separate processes; game helper has not
started while menu is open. Generic WAD/PK3 role update is deployment-only at
owner request: no launcher or game started on handheld for this update.
Owner display/controller/profile checks and real menu-to-engine handoff remain
pending; automated replay is supporting evidence.
Final source/client identities and owner-requested closure limits appear in
[single-player delivery record](r36s-single-player-release.md).
