# Concrete native OPL worker plan — design only

2026-10-15. No source edits, no device access. Follows [source audit](audio_streaming_audit.md). Parent reports matched core1 music-on/off diagnostic: incoming28.355→32.817FPS, rockets15.4→19.348FPS. Removing music entirely does not reach60; worker gain cannot be inferred directly from that test because synthesis work remains. Music enabled remains release acceptance.

## Preferred ownership boundary

**Use OPL-only prepared PCM jobs, main-only OpenAL, with MIDI/event preparation on main.** Worker executes ordered emulator commands and original DC correction. Do not move whole legacy OPL callback to worker as first choice.

This is narrower than generic asynchronous OpenAL streaming: no new concurrency in MIDIStreamer, game CVars, decoder/seek, stream registry, OpenAL errors/context, or unsupported music formats. It adds explicit command preparation, so implementation requires more than wrapping existing callback in thread. Existing `OPLEmul` interface makes boundary credible: only Reset/WriteReg/Update/SetPanning, no state reads (`src/oplsynth/opl.h:8–20`). All note allocation/controller decisions use main-owned musicBlock data, not emulator state. `OPLio::OPLwriteReg` resolves chip/register and calls WriteReg (`mlopl_io.cpp:64–77`); OPLwritePan computes exact float pan and calls SetPanning (`252–277`). SetClockRate/WriteDelay are no-ops for emulation (`56–62`).

| Design | Main thread retains | Worker touches | Legacy risk | Rank |
| --- | --- | --- | --- | --- |
| Prepared OPL command→PCM jobs | MIDIStreamer callbacks/events/control/CVars/stats, all AL, all other streams | Actual eligible chips, per-stream DC state, immutable commands and owned PCM | New bounded deferred-refill seam; small write-only emulator split; no song-state sharing | Preferred |
| Whole-callback PCM producer, AL main | AL/context/registry | OPL callback plus MIDIStreamer::ServiceEvent/FillBuffer/event state | Same song/control races as AL worker, plus queue protocol; main-only AL does not solve them | Second only if explicit whole-song ownership chosen |
| Adapt donor OpenAL worker for eligible stream | Main effects/control | AL stream refill plus whole synth/MIDI callbacks | Donor provides lifetime pattern, but local MIDI state, seek/error handling, AL sharing and status need more review | Third for current narrow target |
| Generic donor worker for every local stream | Main effects/control | All legacy synth/decoder callbacks and AL | Widest unsupported threading surface; old GME/module/FluidSynth/decoder controls need audit | Reject for first change |

Alternative whole-callback implementation remains valid if all song controls gain quiesce-before-mutation ownership and status snapshots. It is not smaller in proof burden. A mutex around MIDIStreamer::Callback alone is insufficient; worker reads/mutates Events and emulator state before callback. Song-lock→Stop/StreamLock versus worker StreamLock→song-callback creates ABBA deadlock.

## Exact operations and PCM proof

Keep49716Hz,3551 stereo frames/28408bytes per OPL MIDI callback, chip count/core/fullpan, event delays and original segment boundaries. No extra speculative block beyond existing AL demand. No parameter changes, no added silence, no sample dropping, no replacing waveform/math.

Prepared block records operations in exact encounter order:

1. BeginBlock zeroes owned PCM exactly as old ServiceStream memset.
2. WriteReg: already-resolved chip/register/value. Preserve repeated writes and ordering; do not deduplicate.
3. SetPanning: chip/channel and exact float left/right computed by existing main-side formula. No recomputing trig on worker or precision changes.
4. UpdateChip: chip, frame offset and sample count for each existing Update call, in original chip order.
5. CorrectOffset: same interleaved sample offset/count after all chip updates for that segment. Move existing OffsetSamples arithmetic unchanged into state routine operating on worker-owned LastOffset; synchronous reference stays separately testable.
6. ResetOffset/Reset when existing lifecycle requires them, ordered among register/render commands. Do not reset LastOffset per callback.
7. Block return value/EOF handling recorded from original control loop; existing final unqueued-false callback behavior retained. For OPL MIDI, existing EndQueued handling stays on main.

The existing ServiceStream control loop's NextTickIn, SamplesPerTick, PlayTick, MLtime and MIDI continuation remain on main, choosing identical command sequences. Worker cannot access OPLMIDIDevice/MIDIStreamer/CallbackData, Wads, CVars, console, SDL or AL. Job stores offsets into its own PCM, never pointers into temporary OpenAL Data or MIDI event buffers.

Equivalence argument: original chip state transition is ordered sequence of WriteReg/SetPanning/Update; main event decisions do not read chip state. Replaying same operations, same arguments and unchanged sample segmentation against same initialized chips yields same float PCM and LastOffset. Deferred execution time only changes delivery deadline; queue tests must prove no underrun under accepted load. Compiler and floating-point mode unchanged; verify bitwise PCM on host and real ARM64 rather than relying on argument alone.

Use one capture/sink seam for immediate and deferred modes, while retaining frozen original implementation as independent PCM test oracle. Proxy OPLEmul can capture chip calls, but OffsetSamples needs explicit sink hook; treating Update as asynchronous while letting old OffsetSamples read unrendered buffer is wrong. Likewise, returning old bool callback success before ready PCM and immediately calling alBufferData is wrong; OpenAL needs explicit prepared/ready protocol.

## Scope and RNG invariant

Initial asynchronous eligibility: **native R36S OpenAL + actual OPL MIDI emulator + core1 DBOPL melodic mode**. Desktop, FMOD, OPL dumper, raw OPL, cores0/2 and all other synth/file decoders keep existing synchronous path. They remain fully supported; asynchronous optimization is narrow, not content/quality downgrade. More cores can be admitted after their independent safety/PCM tests.

Important correction to broad worker warning: DBOPL has file-static unnamed FRandom `pr_opl` (`dosbox/opl.cpp:33,157`) used for active rhythm percussion. Unnamed RNG still belongs to FRandom global list and is reseeded by StaticClearRandom (`m_random.cpp:152–160,263–270`) at new level/load/demo. Broad off-thread emulation can race that reset.

But OPL MIDI initializes BD0/melodic mode (`mlopl_io.cpp:360`). Its MIDI handlers emit only channel register families; no BD rhythm-enable writes, and HandleLongEvent empty (`music_opl_mididevice.cpp:171–255`). DBOPL only calls pr_opl through active rhythm branch (`dosbox/opl.cpp:955,1035–1067`). Thus narrow core1 melodic stream can prove no worker access to global RNG. Test recorded register traces never set BD bit0x20; include negative-control rhythm trace to prove guard catches it. If unexpected rhythm/register capability arrives, drain/quiesce worker and use same synchronous emulator before applying unsupported command; never execute it off-thread under unsupported invariant. No gameplay RNG edits needed for narrow eligible path. Broadening later requires audio drain barrier around central StaticClearRandom, and debug seed inspection if workers touch RNG; never silently detach/reseed audio RNG because that changes PCM.

Initialize core tables/chips on main before handing exclusive ownership to worker. No concurrent creation/reset while worker executes. DBOPL static table init guard is plain value (`dosbox/opl.cpp:556`), so simultaneous first initialization must not be introduced. Only one live worker owns particular chips; main gets exclusive access only after successful drain/join.

## Bounded queue and main-only AL protocol

Use four fixed logical slots, one per existing OpenAL buffer, with preallocated28408byte PCM each plus owned command vector. Each submission has monotonic stream generation/serial and associated main-only AL buffer ID. No parallel execution of jobs for one synth. Worker consumes FIFO and publishes immutable completion; main uploads/completes FIFO. No whole-song PCM cache or extra audio lookahead.

Slot states: QueuedAL → Requested → Rendering → Ready → QueuedAL; false/EOF result goes Ready→Free with original no-upload behavior. Stop ends current generation and discards remaining slots only after worker quiescence. A stale completion cannot attach to restarted stream/new generation. Four slots bound outstanding jobs without arbitrary audio-command truncation.

Main stream update:

1. Collect Ready slots in order; call alBufferData/alSourceQueueBuffers on main only.
2. Query processed AL buffers, unqueue once. For each, prepare exact next block on main at point old callback would have run, then enqueue immutable job. Main performs any MIDI MOM_DONE/ServiceEvent now, in old order.
3. Collect immediately ready completions again without waiting.
4. Preserve paused state. If source stopped and queued buffers exist, restart as old underrun recovery. If queued0 but outstanding/ready jobs remain, retain logical Playing; it is not EOF. End only when original EOF/control condition and no queued/pending work.

Ordinary update never waits for worker or silently synthesizes on main. Initial four-buffer Play may remain synchronous, preserving current initialization/event order and avoiding new startup READY race; start worker after prefill and before next refill demand. It will not process any block until main submits job after playback returns. Thread/allocation setup failure before ownership handoff uses original synchronous path, preserving sound/features; log once. Do not claim steady-state FPS protection during initial preload.

Demand timing: prepare only for actually processed AL buffers, not one extra block ahead. Extra speculative preparation would advance MIDI parser/EndQueued and observe controls too early; discarding prepared PCM cannot roll parser back. Existing queue already supplies~286ms full margin. Worker latency must fit remaining queue (~214ms after one processed block), with actual-frame/scheduler measurements. If all buffers become pending, source can underrun; retain lossless FIFO/restart and expose counter. Do not insert zeros/drop samples to conceal miss. Acceptance requires no underruns under target route. Error recovery can quiesce/drain and revert to synchronous rendering at safe boundary; no concurrent emergency rendering of same chip state.

## Ownership and lock order

- Main owns SoundStream, AL buffer/source handles, MIDIStreamer, OPLMIDIDevice frontend, OPL note/controller data, event headers and all public controls.
- Worker owns emulator state and DC correction state while running. It receives only immutable commands and owns PCM being rendered.
- One queue mutex protects job/slot publication and worker mode/stop flag; condition variable for jobs, completion and drain. Hold mutex only to change queue/state, never during rendering, main preparation, AL calls, callbacks, join or external locks.
- Main rare drain requests stop admission and wakes worker, then waits on condition variable releasing queue mutex. Worker finishes already-issued jobs in order, publishes ready state, marks idle, then waits. Main can now touch/reset/move emulator state. Never cancel a running Update by interrupting its thread.
- Join performed without queue mutex and without ChipAccess/CritSec. Worker never takes ChipAccess/CritSec/song/AL locks, so no reverse lock ordering. Queue destructor sets quit, wakes, joins; then frees slots/chips.
- Main per-frame stats/status use main-owned logical state or completion snapshot; no drain or chip read. Worker diagnostics use atomic counters or queue snapshot; no per-sample console output.

## Main-control/lifetime inventory

| Path | Required behavior under preferred design |
| --- | --- |
| Open/StartPlayback/Precache | Remain main, including GENMIDI WAD access, initial tempo/events and four startup renders. Install eligible prepared source only for emulator, never OPLDumperMIDIDevice/DiskWriterIO. |
| Play/replay | Stop/quiesce previous generation first. Reinitialize main/frontend and chips in existing order, prefill, hand off exclusively, then publish new generation. Preserve looping flag and callback return rules. |
| Stop/MIDIStreamer::Stop | Main preparation never runs concurrently, so EndQueued=4 assignment no new race. Stream Stop must cancel admission and join/quiesce worker before source reset or callback-owner destruction; pending commands can be discarded because user stopped playback. No wait under ChipAccess/CritSec. |
| IsPlaying / EndQueued | Parser and EndQueued still main, preserving old status semantics; no atomics needed for MIDIStreamer fields. OpenAL stream must count pending jobs so it does not announce EOF merely because AL queue temporarily empty. Test EndQueued causes Stop while last jobs pending; no callback/AL requeue after Stop. |
| Pause/Resume | Main owns m_Status and existing MIDI->Pause/AL calls. Continue only actual processed-buffer demands that old code would refill while paused; no speculative preparation. Already prepared/rendering blocks remain valid future PCM; do not discard/replan because main event state has advanced. Match original behavior for processed buffers during pause, including MIDI NOP preparation when legacy code would do so. Rendering may finish while paused; source stays paused and FIFO data retained. |
| snd_musicvolume / relative volume | Remain main AL gain path; DBOPL uses FakeVolume=false. No job invalidation or synth rewrite, so volume acts on already queued audio exactly as before. General FakeVolume devices unchanged synchronous. |
| SetTempo/SetTimeDiv / StreamOutSync | Remain main; worker sees only final register/render commands. Existing MIDI tempo and event chains need no new mutex. Runtime event tempo order preserved. |
| SetSubsong | Existing SetMIDISubsong mutates parser before Stop; this remains main-only and race-free. Stop joins old worker before any chip reset/delete. New Play gets new generation. No old completion may leak. |
| Seek / SetPosition | MIDIStreamer does not implement time seek; retain existing unsupported behavior. File-stream Decoder seek/GetPosition/GetStats/IsEnded refill path remains synchronous and unchanged. Raw OPL remains synchronous. Do not invent generic seek support for prepared source. |
| opl_core / opl_fullpan | Existing values latched at OPL device creation; runtime CVar changes do not silently switch current core/panning. Subsequent song creation chooses eligibility from actual instance/core, not newly changed global CVar. |
| opl_numchips | Existing callback resets only raw OPL when OPL_Active true; OPL MIDI doesn't set OPL_Active. Preserve behavior. Raw reset remains synchronous. Any new explicit MIDI chip-reset path must drain/join first, then reset in original order. |
| opl_singlevoice / synth_watch | Stay main in note/event preparation; no worker CVar access. No change to double-voice or debug output behavior. |
| GetStats | OPLMIDIDevice reads main-owned channel flags/OPLchannels, safe without worker wait. Do not redirect stats to emulator internal state. Legacy ADD_STAT(opl)/YM3812GetVoiceString block is commented out (`opl_mus_player.cpp:512–517`); do not reenable it as unreviewed cross-thread chip reader. Raw/misc stats retain old path. |
| New game/map/load/demo reseed | Narrow core1 melodic worker never reads global RNG, so no new synchronization in gameplay code. Tests enforce invariant. Raw/core2 synchronous as before. |
| OPL dump / MIDI export | Explicitly ineligible; synchronous DiskWriterIO and source-file copies unchanged. Never start worker from common OPLMIDIDevice constructor before actual derived dumper known. |
| Song change/save-load/playlist | Main Stop joins worker before deleting old device/song/GENMIDI state. Future job has no MIDI pointer, but still enforce ownership order defensively. |
| OPLMIDIDevice destruction | Add explicit early Close/destructor guard because current reverse base destruction frees OPLmusicBlock/io before SoftSynth destructor's Close. Worker/stream stopped before any base-owned resource release, including exceptional/open-failure cleanup. Close idempotent. |
| Stream/render/audio shutdown | Stop/join all eligible stream workers before deleting source/buffers/context/device and before SDL shutdown. Main stream registry remains unchanged and single-threaded. Join even if Device unavailable/initialization partially failed; avoid early-return thread leak. |

## Proposed source boundaries, not committed API

- New small native OPL job/worker module: typed commands, exclusive chip/DC state, four-slot FIFO, condition-variable sleep, drain/join, exact synchronous execution fallback. Internal API exposes Prepare/Submit/TryCollect/Drain/Stop; no AL or MIDI callback thread.
- `src/oplsynth/opl_mus_player.{h,cpp}`: explicit render/correction sink used by same timing/control loop; preserve existing synchronous/default branch. Capture native jobs without changing PlayTick behavior. Optional pure OffsetSamples state helper with independent frozen reference test.
- `src/oplsynth/opl.h` / narrow adapter: write-only capture proxy or explicit sink, preserving exact arguments. `mlopl_io.cpp` may need panning hook if capture proxy unsuitable. Main frontend keeps metadata; actual chip objects exclusively worker-owned after handoff.
- `src/oplsynth/music_opl_mididevice.cpp` / `src/sound/i_musicinterns.h`: opt-in only real core1 emulator, explicit lifetime close. No broad MIDIStreamer threading changes.
- `src/sound/i_sound.{h,cpp}`: optional prepared-source registration with default unsupported implementation, not new pure virtual requirement. Existing bool callback API unchanged for all existing consumers. A prepared job must distinguish Pending/Ready/EOF; bool-only interface cannot encode asynchronous pending safely.
- `src/sound/oalsound.cpp`: prepared stream's pending-slot handling, main-only AL upload and correct pending-aware status/Stop. Generic streams and decoder/seek branch unchanged. No renderer-global background thread.
- `src/CMakeLists.txt`: module selected only native client optimization; desktop/legacy/server builds still compile and behave as before. No settings default change.
- Harness/docs/attribution: actual adapted donor blocks only if used. Prepared-command worker itself is local design; donor's AL Process cannot be claimed copied if only lifecycle idea consulted. Pinned GZDoom BSD block notices remain required for any actual adopted code.

## Tests before device build

1. **Actual-production PCM seam:** original synchronous ServiceStream/reference vs prepared execution, same generated licensed GENMIDI/MUS/MIDI fixtures and unchanged3551-frame callbacks; byte-for-byte float PCM including signed zeros, exact output length, LastOffset and final state. At least one multi-buffer loop, nonintegral tick durations, tempo changes, pan/pitch/control/sustain, double voices, silence/off channels, extrema causing nonzero OffsetSamples/ramp and final release tail. Run x86 host and real ARM64 executable under container. Real owner IWAD privately can supply supplementary comparison, never redistributed.
2. **Command trace oracle:** match original operation order/arguments and segment boundaries; fullpan, repeated register writes, chip order, counts. Negative controls swap register order, alter segment split, omit panning or LastOffset reset; tests must fail for actual meaningful cases. Confirm rhythm BD bit never set by eligible MIDI; injected rhythm job rejects/offloads safely before any worker RNG call.
3. **Ownership:** deterministic barrier tests force worker Rendering while main Stop/Close/replay/destructor executes. No callbacks or chip accesses after lifetime end, no stale generation completion, no deadlock. Thread startup failure, no-Device/partial-Open failure, repeated Close and destructor after failed Resume. Worker loops sleep when idle, no leaked thread after SDL teardown.
4. **Queue:** real fake-AL boundary or host OpenAL loopback tests four slots, processed counts0..4, repeated polls, FIFO upload, exactly-once unqueue, false/EOF, ready+pending source STOPPED, AL errors, underrun recovery, worker delay longer than one frame and saturation. No main wait in ordinary refill, no duplicate/drop/zero PCM. Handle frame delay>queue duration honestly as underrun, not false EOF.
5. **Controls:** pause/resume before demand, during render and with ready blocks; volume changes affect queued audio through gain; SetSubsong/new map/save-load/playlist; settings latched behavior; raw OPL seek/reset/stat/dumper, other decoders and desktop paths unchanged. Compare main event/EndQueued trace with synchronous reference at identical refill/control boundaries.
6. **Sanitizers/builds:** ASan/UBSan; meaningful ThreadSanitizer forced-overlap execution; native ARM64 and matching legacy full builds; renderer tests unchanged; no audio new network/gameplay state.
7. **Actual R36S acceptance:** core1/numchips2/fullpantrue/music-on stock settings/native640×480, repeated90s incoming/rockets route. Record main and worker CPU/callback durations, frame p99/max and budget misses, ready queue high/low marks, underruns and physical listening; transitions/pause/volume/save/load/exit/relaunch/frontend restore. Show incremental measured effect, no60 claim until full target passes. Worker steals CPU from other cores and may worsen driver contention; retain only with actual benefit and clean audio.

This plan is implementation-ready boundary proposal, not implemented or qualified feature. Parent keeps renderer-driver experiments isolated before authorizing audio source changes.
