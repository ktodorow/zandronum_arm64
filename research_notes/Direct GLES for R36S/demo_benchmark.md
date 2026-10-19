# Combat benchmark route: local demo source audit

## Decision

Use existing evdev MAP07 route with explicit fixed RNG seed for immediate
90-second native qualification. Keep gameplay, 640x480, music/effects,
lighting, clocks and owner controls/settings. Describe repeated runs as
representative seeded trials, not deterministic simulation A/B. No demo tool,
gameplay patch or fixture change implemented.

Critical primary evidence: `docs/Skulltag Version History.txt:646` explicitly
warns single-player demos are unsupported and quickly go out of sync; recommends
client demos. Current source still implements ZDEM input playback, but source
presence plus saved seed is insufficient proof of route determinism. A future
ZDEM experiment must compare record against full playback and repeated playback
before trusting combat timing. No such validation performed here.

## Current preferred launch

Append `-rngseed 12345` to existing private isolated native09 MAP07 launch.
Use existing `+map MAP07`, or `-warp 7`; preserve current skill instead of
silently changing it. CLI `-skill 3` means internal skill2 if an explicit
normal-skill new-game benchmark is deliberately selected. No `-host`,
`-connect`, fixture PK3, cheats, god mode or unlimited ammunition required.

`src/d_main.cpp:2434` parses seed into `rngseed`, `staticrngseed` and
`use_staticrng`; startup prints `D_DoomInit: Static RNGseed 12345 set.`
`src/g_level.cpp:568` uses static seed on ordinary new game and clears
gameplay RNG streams. Record launch/config/content/build identities, route
script hash, actual start/end timestamps, player death/health/kills/visible
explosions and raw frame timings. Fixed seed does not make wall-clock evdev
events land on identical tics. Cold loading and earlier missed events can
change combat. Observe live map before starting route; release all inputs,
then record90 seconds and quit normally. Repeat if capture lacks intended
combat/explosions; do not substitute idle/dead view for active-combat gate.

## Exact regular ZDEM commands, if later investigated

These commands are source-supported, not qualified deterministic benchmarks:

```sh
./zandronum-gles -iwad /private/doom2.wad -config /private/bench.ini \
  -skill 3 -warp 7 -rngseed 12345 -record /private/map07-combat.lmp

./zandronum-gles -iwad /private/doom2.wad -config /private/bench.ini \
  -playdemo /private/map07-combat.lmp
```

- `src/d_main.cpp:3141` chooses regular recording only outside server/client
  mode. `G_RecordDemo`, `src/g_game.cpp:5122`, defaults extension `.lmp` and
  records in memory. `G_BeginRecording:5138` writes FORM/ZDEM, map, RNG seed,
  userinfo, server/demo-save CVars and weapon ordering before new-map setup.
  No IWAD/content hash manifest stored; preserve private SHA256 manifest.
- `stop` command (`src/g_game.cpp:5063`) sets flag; next recording tic invokes
  finalization, writes file, then opens full console. Normal exit invokes
  `D_QuitNetGame` (`src/d_net.cpp:677`) and also finalizes recording. Confirm
  `Demo /private/map07-combat.lmp recorded` and file FORM/ZDEM header after
  graceful exit. No file required before finalization. Choose unused name:
  file-writing path does not protect existing recording from overwrite.
  Crash/SIGKILL is unsuitable finalization.
- `-playdemo` (`src/d_main.cpp:3171`) loads header/start map itself. Omit
  `-record`, `-warp`, `+map` and savegame arguments on playback. Absolute `.lmp`
  path avoids lump-name ambiguity; ensure no same-basename `.cld` exists:
  `G_DoPlayDemo:5399` checks `.cld` sibling first and redirects to client demo.
  Vanilla IWAD demos fail `Cannot play non-ZDoom demos.`
- Regular playback ends with `Demo ended.` and full console
  (`src/g_game.cpp:5555`), despite obsolete CLI comment saying quit after one.
  Process requires normal quit afterwards. `stopdemo` only handles `.cld`
  playback (`src/g_game.cpp:5243`); it does not stop regular recording.
- `-timedemo` (`src/g_game.cpp:5487`) enables `singletics`: one simulation tic
  per render iteration as fast as possible, then reports throughput through
  `I_FatalError` at completion. This differs from normal interpolation/pacing;
  unsuitable substitute for normal `-playdemo`60FPS/frame-time qualification.
- Do not replay evdev combat during demo playback: `G_Responder:1355` turns
  most key presses into menu opening. Focus loss/paused states can stop tics.

## Readiness and possible future recorded markers

Launch `+echo READY` runs before initial map setup
(`src/d_main.cpp:3124`). `Playing demo` also prints before `G_InitNew`.
Level-name banner precedes `P_SetupLevel`. None proves playable scene ready.
Startup `wait` command uses ordinary `DWaitingCommand` thinker
(`src/c_dispatch.cpp:248`); map teardown destroys it through
`P_FreeLevelData`/`DThinker::DestroyAllThinkers`. Do not schedule recording
route/READY from startup `wait`.

If ZDEM ever passes sync checks, prebound spare buttons can invoke
`say R36S_BENCH_BEGIN` / `say R36S_BENCH_END` while recording. Existing
`chat_SendMessage` (`src/chat.cpp:1581`) stores `DEM_SAY`; replay
`Net_DoCommand` (`src/d_net.cpp:2065`) emits same message at recorded tic.
SDL `I_PrintStr` (`src/sdl/i_system.cpp:451`) flushes stdout. Private local
watcher could start/stop existing signal-controlled frame recorder from these
markers, avoiding startup race. Watcher scheduling leaves presentation-boundary
uncertainty; it is not an exact in-engine frame/tic latch. Marker chat HUD/sound
also becomes part of identical benchmark workload. No watcher or bindings
created. Mods may suppress chat; original IWAD/no bots avoids chat event-script
or bot reactions. Keep renderer/audio config identical between runs.

## Client demo applicability

`src/cl_main.cpp:515` handles `-connect`, switches NETSTATE_CLIENT, then starts
`CLIENTDEMO_BeginRecording` from `-record` at540. Other caller at9654 is also
connection startup. No offline single-player CLD recording CLI found.
`CLIENTDEMO_BeginRecording` (`src/cl_demo.cpp:167`) writes `.cld`/ZCLD with
version/build, seed, WAD list/authentication and map checksums; recorded network
commands provide authoritative state rather than unsupported single-player
input resimulation. Capture requires actual server/client session once; later
playback can be offline. This changes benchmark mode/setup and is outside
immediate MAP07 performance priority. No server/demo setup performed.

Audio load remains part of release test. ZDEM captures gameplay seed/input,
not real-time mixer scheduling; `M_Random` explicitly serves non-gameplay
randomness (`src/m_random.h:220`). Do not claim bit-identical audio or arbitrary
wall-time screenshot equality from seed alone.
