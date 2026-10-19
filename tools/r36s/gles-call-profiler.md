# GLES call profiler

Diagnostic preload only. Exclude source/binary from release packages. Build on
Linux/AArch64 with no EGL/GLES development headers or link dependency:

```sh
cc -O2 -shared -fPIC -std=c11 -Wall -Wextra -Werror tools/r36s/gles-call-profiler.c -o /tmp/gles-call-profiler.so -ldl
```

Preload order matters: profiler first, frame recorder second. Both receive
`SIGUSR1` to start at next swap and `SIGUSR2` to stop/write at next swap. Profiler
writes `/tmp/zandronum-gles-calls.csv` after recording stops, with bounded memory
and no recording disk I/O. Timings cover main-thread API wall time, not GPU time
or physical memory traffic. `buffer_data_allocated_bytes` includes null-source
allocations; `buffer_data_source_bytes` counts supplied source bytes only.

Baseline leaves all optional probe variables unset:

```sh
env -u ZANDRONUM_DIAG_GL_FINISH -u ZANDRONUM_DIAG_EGL_INTERVAL -u ZANDRONUM_DIAG_GL_STATE LD_PRELOAD="/tmp/gles-call-profiler.so:/tmp/zandronum-frame-recorder.so" ./zandronum
```

Use separate runs for each probe; neither is enabled by default. Preserve map,
camera, settings, warmup and measurement window when comparing results.

```sh
env -u ZANDRONUM_DIAG_EGL_INTERVAL ZANDRONUM_DIAG_GL_FINISH=1 LD_PRELOAD="/tmp/gles-call-profiler.so:/tmp/zandronum-frame-recorder.so" ./zandronum
```

`ZANDRONUM_DIAG_GL_FINISH=1` calls real `glFinish` on every swap, immediately
before entering inner frame recorder and before profiler's swap timer. GPU
completion therefore moves into frame work time rather than recorder swap time.
This changes CPU/GPU overlap; timing difference is not a direct GPU timer and is
not a proposed release setting. Missing `glFinish` disables probe with one log.

```sh
env -u ZANDRONUM_DIAG_GL_FINISH ZANDRONUM_DIAG_EGL_INTERVAL=0 LD_PRELOAD="/tmp/gles-call-profiler.so:/tmp/zandronum-frame-recorder.so" ./zandronum
```

`ZANDRONUM_DIAG_EGL_INTERVAL=0` directly calls current EGL implementation's
`eglSwapInterval(display, 0)` once, at first swap with current context/display.
It bypasses SDL's interval setter, including firmware patches forcing its
argument to one. Only already loaded EGL libraries are inspected; `RTLD_NOLOAD`
handles SDL's locally loaded EGL. No new library, display or context is created.
One log reports result, EGL error, prior error and config min/max interval
(`-1` if unavailable). Probe consumes EGL error state for diagnostics. Success
does not prove immediate presentation: driver/config/compositor may still
clamp interval or impose pacing. It does not reapply after context recreation.

These variables are read once at preload initialization. Only exact values
`1` for finish and `0` for interval activate them. Setting both explicitly runs
both, but cannot isolate their individual effects. Unset variables for baseline
and all release runs; engine sources and production runner remain unchanged.

`ZANDRONUM_DIAG_GL_STATE=1` enables a separate draw-state histogram. Keep finish
and interval probes unset for its first capture:

```sh
env -u ZANDRONUM_DIAG_GL_FINISH -u ZANDRONUM_DIAG_EGL_INTERVAL ZANDRONUM_DIAG_GL_STATE=1 LD_PRELOAD="/tmp/gles-call-profiler.so:/tmp/zandronum-frame-recorder.so" ./zandronum
```

On the same stop signal, `/tmp/zandronum-gles-states.csv` records program and
material/variant labels, primitive, draw count, submitted vertex/index count,
alpha test enable/function/threshold, six-plane clip mask, fog mode, texture
enable/mode and basic burn state. Program serial distinguishes deleted/reused
GL names. Engine label hooks run only when programs are created; without this
preload they do nothing. Uniform observation and draw aggregation remain in
the diagnostic library. Cached programs receive the same labels as source links.

Weights cover exactly the frames included in call CSV; work before first
accepted frame and after final accepted frame is discarded. Submitted indices
are counted with repetitions; weights do not measure covered pixels, GPU time
or shader instruction cost. Zero values for uniforms absent from a program
mean no observed state for that uniform. Defines identify built-in material
variants; newlines in defines become spaces in valid quoted CSV fields.

Histogram is bounded to 1024 simultaneously tracked programs and 8192 distinct
state keys. Capacity loss sets `overflow=1` and logs after recording. Labels and
defines truncate at 95/255 bytes. Collection adds diagnostic CPU overhead and
is unsuitable as a clean performance baseline. No disk writes occur until stop.
Run one profiler capture at a time because CSV output paths are fixed.

Real-adapter integration smoke (Mesa EGL/GLES plus chained frame recorder):

```sh
python3 tools/r36s/tests/test-gles-renderer.py --profiler-smoke --gles-version 2.0
python3 tools/r36s/tests/test-gles-renderer.py --profiler-smoke --gles-version 3.2
```

Smoke verifies three exact frame windows, nine draws/45 vertices, indexed and
array draws, changing material/alpha/clip/fog/texture/burn state, label quoting,
and excluded stop-window tail. Normal renderer suite still runs without this
option. Integration smoke overwrites the three diagnostic CSV paths in `/tmp`.
