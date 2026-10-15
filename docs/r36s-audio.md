# R36S effects and base-game music

Issue [#5](https://github.com/ktodorow/zandronum_arm64/issues/5), branch `r36s`.
Uses existing [PortMaster package](r36s-package.md), native 640×480 display,
controller profile and mandatory frontend stop/recovery lifecycle.
Physical listening, map continuity and relaunch are acceptance gates.

## Selected audio path

Client keeps common `USE_OPENAL=ON` backend port from `375170956`.
Reviewed legacy reuse includes `src/sound/oalsound.cpp` for effect buffers,
streaming and gain, plus libsndfile/mpg123 decoders for supported file music.
Built-in OPL synth streams Doom MUS/MIDI through that same OpenAL output;
base-game music needs neither FMOD, external instrument files nor a separate
synth process. Decoder availability alone does not prove music playback.

Inherited release default `snd_mididevice=-1` selects FMOD. Actual issue #4
device log reports `Starting MIDI playback failed` with that setting.
Package `audio.cfg` selects `snd_mididevice=-3` (Emulated OPL FM Synth)
and `opl_core=1` (DBOPL), accepted during [renderer qualification](r36s-rendering.md).
This is handheld policy; common engine and other builds retain their defaults.

Package-local `alsoft.conf` selects `drivers = alsa`, using installed ALSA
device instead of probing unavailable desktop audio servers. OpenAL Soft's
[1.24.2 configuration contract](https://raw.githubusercontent.com/kcat/openal-soft/1.24.2/alsoftrc.sample)
supports executable-local configuration and an exclusive backend list.
No firmware libraries or system-wide mixer settings are changed.

## Upgrade and volume behavior

Runner copies existing INI once to `config/zandronum.before-audio-v2.ini`,
then executes audio profile 2 after loading configuration. Fresh installs
and upgrades from profile 1 receive DBOPL. Engine archives
`r36s_audio_profile=2` with settings on clean exit. Failed initialization
before configuration write retries profile next launch. Later launches keep
owner's MIDI-device and volume settings. Controls migration remains separate;
fresh configurations receive both profiles.

Migration selects MIDI device and DBOPL core; it preserves `snd_sfxvolume`,
`snd_musicvolume` and physical mixer volume. Use controller Sound Options to
mute/raise effects and music independently. Firmware volume keys control
handheld output; package does not force mixer to maximum at launch.

## Device checks, 2026-10-15

Initial profile 1 evidence follows. Profile 2 keeps same OpenAL/ALSA output
and volumes, selects accepted DBOPL backend, and is packaged with fixed client.
Final profile migration/relaunch evidence appears in [rendering guide](r36s-rendering.md).

Device matches [saved runtime snapshot](research/r36s-device-snapshot.md).
Client remains `c7cecc51304e883d4c23e546785b39dab4161ac1cc18f8a478fff76c0942371e`;
no engine rebuild needed for profile/backend selection.

| Actual firmware library | Package version | Loaded file |
| --- | --- | --- |
| OpenAL Soft | `1:1.24.2-1` | `/usr/lib/aarch64-linux-gnu/libopenal.so.1.24.2` |
| ALSA | `1.2.14-1` | `/usr/lib/aarch64-linux-gnu/libasound.so.2.0.0` |
| libsndfile | `1.2.2-2+deb13u1` | `/usr/lib/aarch64-linux-gnu/libsndfile.so.1.0.37` |
| mpg123 | `1.32.10-1+deb13u1` | `/usr/lib/aarch64-linux-gnu/libmpg123.so.0.48.3` |

Package stages no replacement audio libraries and installs no desktop audio
environment. Existing `ldd -r` import check passes against firmware; live
client maps confirm four audio libraries above, with no PulseAudio, PipeWire
or FluidSynth library loaded. Actual PCM uses RK817 card 0, device 0,
44,100 Hz, S16_LE stereo, period 1024, buffer 4096. Engine `snd_status`
reports OpenAL Soft 1.24.2 and 44,100 Hz output.

Initial package verifies 275 managed hashes. Pre-deployment live INI and
existing save hashes remain intact. New profile executes on Doom II MAP01;
log no longer reports MIDI startup failure. Optional RTKit priority request
still reports missing service; no RTKit/desktop service installed.
These runtime checks establish routing and compatibility, not audibility.

| Listening acceptance | Result |
| --- | --- |
| Doom II effects and music | Passed: owner reports working music/effects, "works great" |
| Effects/music sliders | Passed: owner tested mute/raise; final music volume saved as 1, effects as 1 |
| Handheld volume keys | Passed: owner confirms final listening/volume scenario works |
| Continued audio across normal MAP01 → MAP02 | Passed: owner heard audio on next level; engine log confirms both maps |
| Saved volumes and audio after packaged relaunch | Passed: profile skipped, volumes 1/1 retained, owner confirms audible music/effects |
| Doom effects and music | Passed: owner reports working sound without glitches; log confirms E1M1 → E1M2 |

First physical run exited 0 through SELECT+START; frontend restored active.
Saved INI contains `snd_mididevice=-3`, `r36s_audio_profile=1`,
`snd_sfxvolume=1` and `snd_musicvolume=1` (changed from pre-upgrade 0.5).
Pre-audio backup matches downloaded original INI SHA-256:
`7acde6d6a83a88f08f399ff2c0700ccc4acd493897ff65e06e7ea4be51b6da4e`.
Tested `managed-files.sha256` SHA-256:
`54a6a94f30339c46afb0cb118262574e36cfc392c52b3f3acad94c6c4d338287`.

Owner confirmed Doom II music, mute controls and continued next-level audio,
then confirmed Doom after relaunch: "No weird glitches on sound found."
Both normal map transitions are corroborated by engine logs and autosaves.
Owner's IWAD SHA-256 values match earlier single-player checks:
`doom2.wad`: `31740ef23994b3959800134b41aaf86b04a2847336d328af8c4ae890450630ab`;
`doom.wad`: `03103e82064a960b548a98eb9656f1f30545458eb437d99475a962053b1f8fcd`.
Audio listening gate passed; renderer correctness/performance remain #6/#7.
Final Doom run exited 0 through SELECT+START with no remaining game/helper;
EmulationStation restored active. Final device check again verifies all 275
managed files, unchanged pre-audio backup and retained OPL/volume settings.
Retained `doom2-physical.log`, `doom-physical-relaunch.log`, before/after INI,
loaded-library maps, PCM parameters, package checks and owner's listening replies.

## Repeatable listening scenario

1. Launch **Zandronum** from Ports using owner's Doom II IWAD. Start MAP01;
   listen for level music and R2 gunfire, doors, pickups and enemy effects.
   Note speaker/headphone output and any crackle, dropouts or excess delay.
2. Options → Sound Options: mute music while effects remain audible; raise
   music again. Mute effects while music continues; raise effects again.
   Choose identifiable final slider values. Check physical volume down/up.
3. Finish MAP01 through normal exit, listen through intermission and MAP02,
   then continue firing/playing. Record continuity and whether music changes.
4. Exit SELECT+START. Check exit 0, no helper residue and active frontend;
   retain actual INI and log. Relaunch same Ports entry, verify chosen volume
   values remain and both music/effects play again.
5. Repeat listening with owner's Doom IWAD, including E1M1 → E1M2 if practical.
   SSH may select installed content through same launcher:

   ```sh
   ssh "$R36S_SSH_HOST" 'ZANDRONUM_IWAD=/roms2/doom/doom.wad bash /roms2/ports/Zandronum.sh'
   ```

6. Exit SELECT+START and retain results. Do not accept issue from library
   initialization or a silent recording alone. Renderer correctness and FPS
   remain separate #6/#7 gates.

Host evidence: `../zandronum-arm64-artifacts/issue-5/`; device captures under
`/roms2/ports/zandronum/logs/issue5/`. Credentials remain untracked.
