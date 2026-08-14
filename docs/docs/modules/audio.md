# audio

Sources: `src/systems/audio/audio.c` (234 lines), declarations in `include/druid.h` (~lines 1818–1953).

## Synopsis

Voice-mixing audio playback system built on the SDL3 audio API. It owns a single global `AudioSystem *audio` (heap-allocated in `audioInit()` via `dalloc` with `MEM_TAG_AUDIO`), one SDL audio device, one persistent music stream, and up to `AUDIO_MAX_VOICES` (32) transient SFX voice streams. It does **not** own audio data: decoded PCM clips (`AudioClip`, produced from dr_mp3 files) are owned by the ResourceManager and fetched at play time via `resFindAudio()` / `resGetAudio()`. The engine main loop is expected to call `audioPump()` once per frame (per the header comment) to reap finished voices and refill the music stream; no callers exist inside the given files, so all API consumers are external.

### Mixing model

- All output is normalised to a fixed target format: `f32`, 2 channels, 44100 Hz (`static const SDL_AudioSpec s_targetSpec`). SDL performs per-stream conversion from each clip's native `spec`.
- **SFX path (fire-and-forget):** `playSound`/`playSoundID` picks the **first inactive slot** by linear scan of `audio->voices[0..31]`; if none is free it warns `"No free audio voice slots"` and returns -1 (no voice stealing). A new `SDL_AudioStream` is created per play, gain is set to `volume * sfxVol * masterVol` (each clamped to [0,1]), the stream is bound to the device, the **entire** clip PCM is queued at once (`SDL_PutAudioStreamData` + `SDL_FlushAudioStream`), and the slot index is returned as a voice handle. `audioPump()` reaps a voice when `SDL_GetAudioStreamQueued(...) == 0` (fully consumed), destroying the stream and freeing the slot.
- **Music path (streamed):** a single persistent `musicStream` created and bound in `audioInit()` (starts paused). `playMusic()` resolves the clip by name, records `musicClipIdx`/`musicCursor`/`musicLoop`, sets gain to `musicVol * masterVol`, queues an initial chunk (up to `88200*4` bytes ≈ 2 s of f32 stereo 44.1 kHz), and resumes the stream device. `audioPump()` keeps at least ~0.25 s queued (`refillThreshold = 44100/4 * 2 * sizeof(f32)` bytes), feeding chunks of up to `2 * refillThreshold` bytes from `clip->pcm + musicCursor`. At end of data it wraps `musicCursor` to 0 if `musicLoop`, otherwise sets `musicClipIdx = (u32)-1` (stops feeding; stream drains).
- Volumes are applied as SDL stream gains, not by mixing samples in engine code.

## Public API Reference

All are `DAPI` and declared in `druid.h`. No call sites exist within this module's files (other than `playSound` -> `playSoundID`); all listed callers are external (engine loop / game code).

### `DAPI extern AudioSystem *audio`
Global singleton pointer. NULL until `audioInit()` succeeds; every API function early-outs on `!audio`.

### `b8 audioInit(void)`
Initialises the audio system. Idempotent (`if (audio) return true;`).
- Params: none. Return: `true` on success or if already initialised; `false` on SDL subsystem/allocation/device failure.
- Side effects: `SDL_InitSubSystem(SDL_INIT_AUDIO)`; allocates and zeroes `AudioSystem` (`dalloc`, `MEM_TAG_AUDIO`); sets `masterVol`/`sfxVol`/`musicVol` to 1.0 and `musicClipIdx` to `(u32)-1`; opens the default playback device with `s_targetSpec`; creates and binds `musicStream` (failure to create the music stream only WARNs — init still returns true, and music functions no-op). On device-open failure, frees the struct and resets `audio` to NULL.
- Callers: external (engine startup).

### `void audioShutdown(void)`
Tears everything down. Unbinds/destroys all active voice streams and the music stream, closes the device, `SDL_QuitSubSystem(SDL_INIT_AUDIO)`, frees the `AudioSystem` (`dfree`), sets `audio = NULL`. Callers: external (engine shutdown).

### `void audioPump(void)`
Per-frame maintenance (header comment: "call once per frame to refill music, reap finished voices").
- Reaps SFX voices whose stream queue has hit 0 (unbind + destroy stream, mark inactive).
- Refills the music stream as described in the mixing model above; clip pointer is re-fetched via `resGetAudio(audio->musicClipIdx)` every call.
- Callers: external (engine main loop).

### `i32 playSound(const c8 *name, f32 volume)`
Name-based SFX play. Resolves `name` via `resFindAudio()`; WARNs and returns -1 if not found, otherwise forwards to `playSoundID()`. Callers: `playMusic` does **not** use it; internal caller: none besides forwarding; external game code.

### `i32 playSoundID(u32 audioIdx, f32 volume)`
Index-based SFX play. Fetches clip via `resGetAudio(audioIdx)` (NULL -> -1); picks the first free voice slot; creates a per-play `SDL_AudioStream` converting `clip->spec` -> `s_targetSpec`; sets gain `clampVol(volume) * clampVol(sfxVol) * clampVol(masterVol)`; binds, queues full clip, flushes.
- Return: voice slot index [0,31] usable with `stopSound()`, or -1 (no system / bad clip / no free slot / stream creation failure).
- Side effects: allocates an SDL stream owned by the voice slot until reaped or stopped.
- Callers: `playSound` (this file); external.

### `void stopSound(i32 voice)`
Stops a voice by handle. Bounds-checks `voice` against `[0, AUDIO_MAX_VOICES)`, no-ops on inactive slots; unbinds and destroys the stream, marks the slot inactive. Callers: external. Note: handles are not generation-checked — a stale handle can stop a newer sound reusing the slot.

### `void playMusic(const c8 *name, b8 loop)`
Starts (or replaces) streamed music by clip name. No-ops if system or `musicStream` is missing; WARNs if the name is not found. Clears any queued music data, sets `musicClipIdx`/`musicCursor`/`musicLoop`, applies gain `musicVol * masterVol` (note: no per-call volume parameter, unlike SFX), queues the initial chunk, advances `musicCursor`, resumes the stream device. Callers: external.

### `void stopMusic(void)`
Clears the music stream queue, pauses the stream device, resets `musicClipIdx` to `(u32)-1` and `musicCursor` to 0. Callers: external.

### `void pauseMusic(b8 paused)`
Pauses (`true`) or resumes (`false`) the music stream device via `SDL_PauseAudioStreamDevice` / `SDL_ResumeAudioStreamDevice`. Does not touch cursor/state. Callers: external.

### `void setMasterVolume(f32 v)` / `void setSfxVolume(f32 v)` / `void setMusicVolume(f32 v)`
Store the clamped volume in `AudioSystem`. Only `setMusicVolume` immediately re-applies gain to the live `musicStream` (`musicVol * masterVol`). `setMasterVolume` and `setSfxVolume` do **not** update already-playing streams — new values take effect on the next `playSound*`/`playMusic` call (and master changes never reach the current music gain until `setMusicVolume` or `playMusic` runs). Callers: external.

### `#define AUDIO_MAX_VOICES 32`
Compile-time cap on simultaneous SFX voices; sizes `AudioSystem.voices` and bounds all voice loops/handle checks.

## Data Structures

### `AudioClip` (druid.h ~1818)
```c
typedef struct { c8 name[256]; u8 *pcm; u32 byteLen; SDL_AudioSpec spec; } AudioClip;
```
Decoded PCM clip. Header comment: "decoded PCM owned by the resource manager" — lives in `ResourceManager.audioBuffer`, looked up by `resFindAudio` (name -> index via `audioIDs` HashMap) and `resGetAudio` (index -> pointer). The audio module only reads clips; it never allocates or frees them. Not ECS-managed — plain buffer inside the ResourceManager. (The dispatch notes clips are decoded from mp3 via dr_mp3; the decoding itself lives in the ResourceManager module, not here.)

### `AudioVoice` (druid.h ~1918)
```c
typedef struct { SDL_AudioStream *stream; b8 active; } AudioVoice;
```
One SFX playback slot. `stream` is created in `playSoundID` and destroyed in `audioPump` (natural end), `stopSound`, or `audioShutdown`. Lives by value inside `AudioSystem.voices[32]`; no separate allocation. Not ECS-managed.

### `AudioSystem` (druid.h ~1924)
Fields:
- `SDL_AudioDeviceID device` — default playback device opened in `audioInit`, closed in `audioShutdown`.
- `SDL_AudioStream *musicStream` — persistent stream for music; created/bound at init, destroyed at shutdown; may be NULL if creation failed (music API then no-ops).
- `AudioVoice voices[AUDIO_MAX_VOICES]` — the 32 SFX slots.
- `b8 musicLoop` — wrap `musicCursor` at end of clip when true.
- `u32 musicClipIdx` — index into `resources->audioBuffer`; `(u32)-1` (`U32_MAX`) = no music.
- `u32 musicCursor` — byte offset into the current music clip's `pcm` for streaming/looping.
- `f32 masterVol, sfxVol, musicVol` — [0,1] levels; combined multiplicatively into SDL stream gain.

Lifetime: single global instance behind `audio`; allocated in `audioInit` (`dalloc`, `MEM_TAG_AUDIO`), freed in `audioShutdown` (`dfree`). Plain heap, not ECS-managed.

### `static const SDL_AudioSpec s_targetSpec` (audio.c)
File-scope constant: `SDL_AUDIO_F32`, 2 channels, 44100 Hz. Target format for the device and all stream conversions. Static/immutable, no ownership concerns.

## Internal (static) functions

- `clampVol(f32 v)` — clamp a volume to [0, 1]; used everywhere a gain is computed or stored.

(That is the only static function; `s_targetSpec` is the only static datum.)

## Open questions / unclear intent

1. **Non-looping music end state:** when a non-looping track's data is exhausted, `audioPump` sets `musicClipIdx = (u32)-1` but never pauses `musicStream` (unlike `stopMusic`). Whether the stream is intentionally left resumed-but-empty (e.g. so a subsequent `playMusic` is cheaper) or this is incidental cannot be determined from these files.
2. **`setMasterVolume`/`setSfxVolume` not applying to live streams:** deferred application (in-flight SFX and current music keep their old gain; master changes don't reach music until `setMusicVolume`/`playMusic`) may be intentional simplicity or an oversight — not determinable here.
3. **Voice handle reuse:** `stopSound` takes a raw slot index with no generation counter, so a handle held after `audioPump` reaps the voice can silence an unrelated later sound. Whether callers are expected to only use handles within the same frame is not documented.
4. **dr_mp3 involvement:** the header only says clips are "decoded PCM owned by the resource manager"; the dr_mp3 decode step is asserted by the module description but does not appear in the files read (it lives in ResourceManager code, outside this module's file list).
5. **`audioPump` caller:** the header comment says "call once per frame", but the actual call site (presumably the engine core loop) is outside the given files, so the exact ordering relative to game logic is unverified.
