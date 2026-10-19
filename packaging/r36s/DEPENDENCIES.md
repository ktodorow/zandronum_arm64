# Offline runtime contract

Target: inspected R36S dArkOS `09022026`, Debian 13 AArch64, glibc 2.41,
firmware SDL2 2.32.10 and installed PortMaster `2026.06.23-0015`.

Copy port files onto SD card, add owner-supplied IWADs, then launch Zandronum
from Ports. No package-manager command, runtime download or compilation is
required. Docker recipes in source checkout describe builds on Mac; their
`apt-get install` commands are not handheld installation instructions.

## Included in port

`r36s-launcher`, graphical client and matching engine PK3 resources.
Native GLES packages add `zandronum-gles`; `zandronum` retains GL4ES/software.
`libs.aarch64/` supplies private SDL1.2 compatibility, GLU, OpenGL and
GLdispatch libraries. `gl4es.aarch64/` supplies private GL4ES GL/EGL wrappers.
Managed-file checksums, SDL2 import manifests and dependency/license notices
accompany these files. Full build/debug/profiling evidence stays in host exports.
Launcher uses an embedded bitmap font and needs no font package or SDL_ttf.

## Existing firmware runtime

Launcher resolves firmware `libSDL2-2.0.so.0`, `libstdc++.so.6`, `libgcc_s.so.1`,
`libc.so.6`, `libm.so.6`, loader and SDL2's `libsamplerate.so.0` dependency.
No packages were installed on handheld to supply launcher dependencies.

Client also resolves existing OpenAL, zlib, JPEG, bzip2, OpenSSL crypto,
libsndfile and mpg123, plus firmware codec dependencies. Graphics use
firmware Mali EGL/GLES/GBM/DRM; do not replace these with generic Debian or
desktop graphics libraries. Runner uses existing Bash, Python 3, systemd,
core utilities and installed PortMaster controller/platform helpers.

Any future required library absent from supported stock firmware must be
distributed inside port with its transitive dependencies and applicable
license notices. Do not turn a development-handheld package install into an
unrecorded release prerequisite. Check launcher and client with actual
selected library paths; package database alone does not identify custom SDL2.
Other firmware variants require separate dependency/runtime qualification.

Native GLES links `libGLESv2.so.2` directly and asks already-loaded firmware
SDL2 for ES context and swap control. It uses firmware `libEGL.so.1`, bypasses
PortMaster desktop-GL hooks and does not load bundled GL4ES/GLU/OpenGL libraries.
`check-runtime.py --client zandronum-gles` rejects desktop-GL dependencies,
checks native driver entry points and native bridge's SDL2 imports without
opening display. Successful import check still needs real-device rendering.

Commercial base-game IWADs remain owner-supplied; add-ons are local files.
