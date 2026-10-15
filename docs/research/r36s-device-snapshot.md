# R36S runtime snapshot

Inspected over SSH on 2026-10-14 with the user's supplied access. Commands read firmware, package, library, display and input metadata; no software was installed and no game or launcher was run. Connection credentials are intentionally not recorded here.

## Observed installation

| Item | Observation |
| --- | --- |
| Firmware banner | `dArkOS (09022026)` in `/usr/share/plymouth/themes/text.plymouth` |
| Userland | Debian GNU/Linux 13 Trixie; `DEBIAN_VERSION_FULL=13.6` |
| Kernel / architecture | Linux `4.4.189`, `aarch64`; dpkg native architecture `arm64` |
| Device tree model | `Game Console R35S/R36S fix by lxc` |
| Device tree compatibility | `rockchip,rk3326-odroidgo3-linux`, `rockchip,rk3326` |
| Firmware device alias | `/home/ark/.config/.DEVICE` contains `RG351MP` |
| Display metadata | DRM mode `640x480p77`; framebuffer virtual size `640,480` |
| libc | glibc `2.41`; libc6 package `2.41-12+deb13u3` |
| C++ runtime | libstdc++6 package `14.2.0-19` |
| Native build tools | gcc package `4:14.2.0-1`, CMake `3.31.6-2`; no compilation attempted |
| SDL package | libsdl2-2.0-0 package `2.32.4+dfsg-1` |
| Actual SDL runtime | `SDL_GetVersion` returns **2.32.10** |
| Graphics | EGL/GLES/GBM entry libraries symlink to `libMali.so`, which resolves to `libmali-bifrost-g31-rxp0-gbm.so` |
| Audio libraries | OpenAL `1:1.24.2-1`; libsndfile `1.2.2-2+deb13u1` |
| PortMaster | Installed at `/opt/system/Tools/PortMaster`; version file `2026.06.23-0015` |
| Port folders | `/roms/ports` and `/roms2/ports` exist; current installed launchers are in `/roms2/ports` |
| Doom folders | `/roms/doom` and `/roms2/doom` exist but their directory listings are empty |
| Input names | `GO-Super Gamepad`, `odroidgo3-keys`, plus power/headphone devices |

Both ARM64 and ARMHF runtime packages are installed. The project target remains ARM64.

## Consequences for the builder

**Inference:** Debian 13 Trixie ARM64 is an appropriate initial Docker userland for this particular installation. Pin the eventual image/toolchain and compare binary dependencies against this device before deployment. Avoid `-march=native` on the Mac's ARM64 VM; its CPU features differ from the RK3326.

The SDL package database does **not** describe the library actually loaded: `libSDL2-2.0.so.0` resolves through `libSDL2.so` to `libSDL2-2.0.so.0.3200.10`, while Debian's 2.32.4 library also remains installed. Capture actual library paths and required symbols, rather than relying on package version matching alone. `SDL_GetVersion` was called without initializing video or opening a graphics context.

Installed desktop `libGL` and Mesa files do not prove the handheld can provide the desktop GL context Zandronum needs. Vendor-backed EGL/GLES and the selected display backend require an actual context/rendering test during implementation.

PortMaster has `mod_dArkOS.txt`, GPTOKEYB helpers, a controller database and graphics setup helpers. The installed dArkOS module differs from current upstream descriptions; use and inspect the installed launcher contract before borrowing a saved-guide example. These scripts were read, not sourced or executed.

The firmware device alias and physical model use different labels. Validate controller mappings and defaults against the actual inputs rather than assuming a desktop joystick order or a name-only R36S mapping.

## Still unverified

No Zandronum binary was built or launched. Display-context creation, renderer correctness, audio output, controller usability, PortMaster launch/exit, content downloads, stock-server connection, and native-resolution frame rate remain implementation tests. Existing Doom/Doom II game files must be located or supplied before those original-game acceptance tests.

See [Docker research](docker-build-environment.md), [dArkOS feasibility](r36s-feasibility.md), and [the design record](../arm-port-design.md).
