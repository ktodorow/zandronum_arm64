# Disposable Linux ARM64 builder for dArkOS

Checked: 2026-10-14. Documentation research and read-only host probes only. This Mac reports `arm64`; the Docker CLI is present at `/Applications/Docker.app/Contents/Resources/bin/docker`. The daemon was not queried, started or configured; no image was pulled and no container was created.

**Device update:** subsequent read-only SSH inspection confirmed Debian 13 Trixie ARM64/glibc 2.41, firmware `dArkOS (09022026)`, and a custom SDL2 2.32.10 runtime. The provisional Debian Trixie recommendation below now matches the actual userland; exact dependencies still need validation during the build. See the [device snapshot](r36s-device-snapshot.md).

## Assessment

**Recommended — inference:** use a project-specific `linux/arm64` Docker build environment on this Apple Silicon Mac, then copy its Linux executable and matching assets to the R36S for runtime tests. Docker Desktop supports Apple Silicon and runs its daemon in a Linux VM. ARM64 containers can execute on that ARM64 Linux environment without the foreign-architecture QEMU route; explicitly select `linux/arm64` to prevent an accidental x86 build. This provides Linux build tools and executable format, whereas compiling directly on macOS would produce a different OS target. [Docker Desktop for Mac](https://docs.docker.com/desktop/setup/install/mac-install/), [Docker platforms and native versus emulated builds](https://docs.docker.com/build/building/multi-platform/), [Docker Desktop Linux VM](https://docs.docker.com/engine/storage/bind-mounts/)

CPU architecture matching does not mean identical CPU features. Use an explicit RK3326-compatible baseline such as `-march=armv8-a`, with Cortex-A35 tuning if appropriate, rather than `-march=native` in the Mac's VM. GCC documents that `native` selects the detected build CPU's features. [GCC AArch64 options](https://gcc.gnu.org/onlinedocs/gcc/AArch64-Options.html)

## Match the installed handheld

Current upstream dArkOS defaults to a Debian Trixie ARM64 root filesystem, with ARMHF compatibility also available; its dependency setup selects GCC/G++ 12 and installs vendor Mali EGL/GLES/GBM libraries. These are source defaults, not measurements of this user's installation. **Provisional recommendation:** start with Debian Trixie ARM64, then pin the image digest, toolchain and package versions after inspecting the installed dArkOS image/update. Use a matching target sysroot for nonstandard headers/libraries where needed. [dArkOS Makefile](https://github.com/christianhaitian/dArkOS/blob/7b91fe0f37c82ed861482173d3cbec8ed1966f09/Makefile), [ARM64 root bootstrap](https://github.com/christianhaitian/dArkOS/blob/7b91fe0f37c82ed861482173d3cbec8ed1966f09/bootstrap_rootfs.sh), [dependency and Mali setup](https://github.com/christianhaitian/dArkOS/blob/7b91fe0f37c82ed861482173d3cbec8ed1966f09/build_deps.sh)

Before choosing the final container, capture a small device dependency manifest:

- dArkOS image/update identity, `/etc/os-release`, kernel/board identification and `dpkg` architecture.
- Installed libc, `libstdc++` and compiler/package versions where available; required and exported `GLIBC`, `GLIBCXX` and `CXXABI` symbol versions.
- SDL library/version and backend configuration; actual EGL/GLES/GBM/Mali/DRM library paths, symlinks and checksums; audio library versions.
- The executable's ELF architecture, interpreter, `DT_NEEDED` dependencies and version requirements, plus matching engine PK3/assets and any deliberately bundled private libraries.

**Rationale — inference:** matching AArch64 alone cannot establish runtime compatibility. A new builder can produce dependencies on libc/C++ symbols missing from an older image. Compare the binary's requirements with the device libraries before deployment; GNU `readelf` exposes headers, interpreter, dynamic dependencies and version information. Preserve the device's working vendor graphics stack rather than shipping arbitrary replacement EGL/GLES/GBM libraries. [libstdc++ ABI and symbol versioning](https://gcc.gnu.org/onlinedocs/libstdc++/manual/abi.html), [GNU readelf](https://sourceware.org/binutils/docs/binutils/readelf.html), [dArkOS graphics installation](https://github.com/christianhaitian/dArkOS/blob/7b91fe0f37c82ed861482173d3cbec8ed1966f09/build_deps.sh)

## Build isolation and cleanup

Keep compilation files inside the container/build stage; copy source into the build or mount it read-only, and export only intended binaries/assets into a dedicated host output directory. Docker supports local artifact export, and a read-only bind mount prevents writes into the host source tree. Writable bind mounts deliberately persist changes on the host. [Build artifact export](https://docs.docker.com/build/building/export/), [bind mounts and read-only mode](https://docs.docker.com/engine/storage/bind-mounts/)

`docker run --rm` removes the container's filesystem and associated anonymous volumes when it exits. Named volumes persist. Source and outputs stored in host bind mounts also persist; images, build cache and Docker Desktop's VM have separate lifecycles. **Proposed cleanup policy:** remove only this project's container/image/cache resources when finished, retaining source and explicitly exported results. Container deletion is not deletion of the whole Linux VM. [Docker automatic container removal](https://docs.docker.com/reference/cli/docker/container/run/#clean-up---rm), [bind-mount storage](https://docs.docker.com/engine/storage/bind-mounts/)

## What still requires the R36S

The container can validate compilation, Linux ELF dependencies and suitable non-graphics checks. It does not reproduce the R36S's Mali driver/kernel pairing, DRM/KMS display, panel behavior, physical controls, audio or thermal/performance limits. dArkOS's RK3326 SDL build specifically enables KMSDRM/OpenGL ES and includes device patches. Validate the accelerated renderer, input, sound, official 3.2.1 server connection and **native 640×480 at minimum 30 FPS / goal 60 FPS** on the actual handheld. No device performance is established by a successful Docker build. [dArkOS SDL build](https://github.com/christianhaitian/dArkOS/blob/7b91fe0f37c82ed861482173d3cbec8ed1966f09/build_sdl2.sh), [RK3326 SDL configuration](https://github.com/christianhaitian/rk3326_core_builds/blob/85834eb07b4437d87adeaf6d47e05531ae97e8f2/scripts/sdl2.sh)

**Deployment update:** SSH access to the actual R36S has been verified, and its firmware/runtime snapshot is recorded above. Build dependencies and the installed PortMaster launcher behavior still require implementation validation. [Device snapshot](r36s-device-snapshot.md), [dArkOS networking and SSH FAQ](https://github.com/christianhaitian/dArkOS/wiki/Frequently-Asked-Questions)
