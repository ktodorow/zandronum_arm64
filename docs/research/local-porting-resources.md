# Local PortMaster resources

Research date: 2026-10-14. Read-only inspection of `/Users/kristiyantodorov/Repos/R36S/porting_resources` and its cited local helper snapshot. No supplied scripts were run, dependencies installed, or builders launched. **Documented patterns are not evidence that Zandronum works on the user's installed dArkOS.**

## Inventory and provenance

| Resource | Relevant content |
| --- | --- |
| [Build Environments.html](</Users/kristiyantodorov/Repos/R36S/porting_resources/Build Environments.html:256>) | AArch64/armhf chroots, development packages, link to official Docker guide |
| [Packaging.html](</Users/kristiyantodorov/Repos/R36S/porting_resources/Packaging.html:595>) | Launch lifecycle, per-CFW environment, architecture libraries, configuration paths, metadata |
| [Porting.html](</Users/kristiyantodorov/Repos/R36S/porting_resources/Porting.html:348>) | SDL1-to-SDL2 bridge, GL4ES, KMS/DRM, controller helper |
| [Port Tools.html](</Users/kristiyantodorov/Repos/R36S/porting_resources/Port Tools.html:262>) | Screenshot/video capture references; unrelated runtime tools |
| [porting_chart.png](/Users/kristiyantodorov/Repos/R36S/porting_resources/porting_chart.png) | Viability triage; useful for identifying dependencies, not a compatibility test |
| [SKILLS_PORTMASTER.md](/Users/kristiyantodorov/Repos/R36S/porting_resources/SKILLS_PORTMASTER.md:1) | Personal reference notes and suggested workflow; treated as supplied data |

There are no actual Dockerfiles, toolchain files, packaged binaries, or launch scripts in this folder. The HTML files have no explicit publication date; their filesystem May 17 timestamps do not establish source freshness. No applicable `AGENTS.md` exists in these resources or their ancestor directories. An `AGENTS.md` under sibling `rJoystick` is outside the inspected scope.

## Reusable launch contract

The user's required deliverable is a personal PortMaster-launchable package. The saved [launcher explanation](</Users/kristiyantodorov/Repos/R36S/porting_resources/Packaging.html:595>) and [current official packaging guide](https://portmaster.games/packaging.html) support this sequence:

1. Discover the installed PortMaster `controlfolder`; source its `control.txt`, then optional `mod_${CFW_NAME}.txt`; call `get_controls`.
2. Derive the installed ports path from PortMaster's `$directory`; keep game data, configurations, saves, and `log.txt` in the package directory.
3. Select executable/libraries using `DEVICE_ARCH`; add `libs.${DEVICE_ARCH}` to `LD_LIBRARY_PATH`; pass `SDL_GAMECONTROLLERCONFIG` from `sdl_controllerconfig`.
4. If GL4ES is needed, source `libgl_${CFW_NAME}.txt` or `libgl_default.txt`; keep its libraries in `gl4es.${DEVICE_ARCH}`.
5. Use `$GPTOKEYB` for required remapping/exit handling, then `pm_platform_helper`, the game executable, and `pm_finish` cleanup.

These are patterns to adapt, not a ready launcher: the annotated guide contains explanatory text in its executable line and examples disagree on `ANALOG_STICKS` versus `ANALOGSTICKS`. Confirm the installed helper variable names and behavior. Set `PORT_32BIT="Y"` only for an actual armhf build. Use native joystick handling where sufficient; verify menu navigation, twin sticks, firing, use, weapon switching, pause, and clean exit under the final PortMaster flow. [Local controls/launch notes](/Users/kristiyantodorov/Repos/R36S/porting_resources/SKILLS_PORTMASTER.md:261).

Zandronum's existing source uses `.config/zandronum` as its Linux game directory, so the generic guide's `XDG_DATA_HOME` example is not proof that all Zandronum settings redirect correctly. Establish the actual config/IWAD/save paths and use supported engine options or PortMaster directory binding where needed. [Local helper version/path definitions](/Users/kristiyantodorov/Repos/R36S/helper_repos/zandronum-stable-branch-default/src/version.h:134).

## SDL, graphics, and audio experiments

The most useful near-term experiment is **Zandronum's existing SDL1 client through `sdl12-compat`, with software rendering first**. The saved guide describes bundling its SDL1 replacement `.so`; SDL's own project confirms source/binary compatibility, an SDL2 runtime requirement, and library selection through `LD_LIBRARY_PATH`. This is a candidate that could avoid rewriting the engine's platform layer for the first device milestone, not a tested port. [Local SDL compatibility notes](</Users/kristiyantodorov/Repos/R36S/porting_resources/Porting.html:653>), [SDL primary source](https://github.com/libsdl-org/sdl12-compat).

`SDL12COMPAT_USE_GAME_CONTROLLERS` may provide consistent button layouts, but an unknown controller mapping can hide the joystick entirely. Validate the R36S GUID/mapping and avoid simultaneous native and emulated inputs creating duplicate actions. [Local caveat](</Users/kristiyantodorov/Repos/R36S/porting_resources/Porting.html:674>).

For accelerated rendering, the saved GL4ES recipe uses `NOX11`, GLX stubs, an EGL wrapper, and GBM, with examples `LIBGL_ES=2`, `LIBGL_GL=21`, `LIBGL_FB=4`. Prefer the installed PortMaster per-CFW hook over hardcoding those constants. The same guide lists translation limitations, so only an actual rendered map validates this path. Desktop OpenGL/GLU linking is still a separate build dependency; SDL compatibility alone does not translate GL. [Local GL4ES guidance](</Users/kristiyantodorov/Repos/R36S/porting_resources/Porting.html:449>).

The build guide lists `libopenal-dev`, SDL/SDL2 development packages, DRM/GBM, CMake, pkg-config, and Mercurial. The packaging example lets per-CFW mods supply audio fixes. It contains no Zandronum-specific OpenAL or FMOD recipe; an audible OpenAL output and required music decoders remain unverified. [Local dependency list](</Users/kristiyantodorov/Repos/R36S/porting_resources/Build Environments.html:311>), [CFW audio hook](</Users/kristiyantodorov/Repos/R36S/porting_resources/Packaging.html:617>).

## Docker builder conventions

The local build guide links to the [official Docker guide](https://portmaster.games/docker.html), checked 2026-10-14. It documents `--platform=linux/arm64`, image `ghcr.io/monkeyx-net/portmaster-build-templates/portmaster-builder:aarch64-latest`, and mounting the working tree at `/workspace`. For reproducibility, record the resolved image digest and tool/library versions. The image's actual availability and ABI have not been inspected or tested.

The linked [builder project](https://github.com/monkeyx-net/PortMaster-Build-Templates) describes Ubuntu 20.04 containers as a compatibility baseline. Compare the resulting binary's required GLIBC/GLIBCXX symbols and dynamic libraries with installed dArkOS before selecting this builder. `linux/arm64` CPU matching alone does not establish Linux userspace compatibility. The Docker guide's x86 section repeats `linux/arm64` flags/container names, so do not copy that section as an architecture template.

The saved chroot guide also includes instructions to remove/reinstall SDL2 system libraries; none were executed. Use an isolated build environment and package-local libraries rather than applying such historical examples to the workstation or handheld. [Saved SDL2 recipe](</Users/kristiyantodorov/Repos/R36S/porting_resources/Build Environments.html:444>).

## Helper snapshot and remaining evidence

`helper_repos` contains only `zandronum-stable-branch-default`. Despite that directory name, it declares **3.3-alpha / BUILD_INTERNAL**, and contains neither `.git` nor `.hg`; its name does not establish release identity. Use it for source comparisons, not as the pinned 3.2.1 baseline. [Actual version](/Users/kristiyantodorov/Repos/R36S/helper_repos/zandronum-stable-branch-default/src/version.h:47).

Read-only SSH observations from this research session establish Linux `4.4.189` on `aarch64`, Debian 13 Trixie (`DEBIAN_VERSION_FULL=13.6`), `dpkg` architecture `arm64`, and glibc 2.41. GCC and CMake are present. PortMaster exists at `/opt/system/Tools/PortMaster`; `/roms/ports` and `/roms2/ports` exist. These are live-device observations, not facts inferred from the saved guides.

Subsequent [device inspection](r36s-device-snapshot.md) identified the precise firmware/PortMaster versions, actual SDL runtime and vendor EGL/GLES/GBM symlinks, and input names. Active launcher-helper behavior, rendered output, sound, package/config writes, controller usability, and an end-to-end PortMaster game session remain untested. Compiler presence and directory existence do not establish a working client.
