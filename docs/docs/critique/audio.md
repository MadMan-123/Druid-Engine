# audio — critique

Scope: `src/systems/audio/audio.c`, `include/druid.h` ~1818–1953.

## Verified pre-flagged issues

1. **[moderate] [READ-VERIFIED] Master/SFX volume changes never reach live streams.** `setMasterVolume` (audio.c:254) and `setSfxVolume` (audio.c:260) only store the value. SFX gain is baked once at `playSoundID` (audio.c:186–187); playing voices are never re-gained. Worse, `setMasterVolume` does not re-apply music gain either, so master volume appears dead for the current track until `setMusicVolume`/`playMusic` happens to run. Fix: loop voices + music stream re-applying `SDL_SetAudioStreamGain` (requires storing per-voice base volume, currently discarded).

2. **[moderate] [READ-VERIFIED] Non-looping music end leaves inconsistent state.** `audioPump` (audio.c:133) sets `musicClipIdx = (u32)-1` but, unlike `stopMusic` (audio.c:238–242), never pauses the stream device nor resets `musicCursor`. Also no `SDL_FlushAudioStream` on the final chunk — [NEEDS-RUNTIME] whether the tail is held back (likely not, since spec matches target and no conversion occurs).

3. **[moderate] [READ-VERIFIED] Voice handles have no generation counter.** `playSoundID` returns a raw slot index (audio.c:194); `audioPump` reaps and `playSoundID` reuses slots, so a stale handle passed to `stopSound` (audio.c:197) silently kills an unrelated newer sound. Classic fix: pack a generation into the `i32` handle, bump on reap.

## New findings

### Missing error handling / silent failures
4. **[moderate] [READ-VERIFIED] Unchecked `SDL_BindAudioStream` in `playSoundID` (audio.c:188).** On failure the stream is never consumed, `SDL_GetAudioStreamQueued` never reaches 0, and the voice slot leaks permanently (never reaped by audioPump). Return values of `SDL_PutAudioStreamData` (audio.c:189, 140, 231) are also ignored.
5. **[trivial] [READ-VERIFIED] Reap check `SDL_GetAudioStreamQueued(s) == 0` (audio.c:105)** treats the error return (-1) as "still playing" — an errored stream is never reaped. `< = 0` would be safer.
6. **[trivial] [READ-VERIFIED] `audioInit` failure paths leak the SDL audio subsystem.** Both the `dalloc` failure (audio.c:31–35) and device-open failure (audio.c:43–49) return without `SDL_QuitSubSystem(SDL_INIT_AUDIO)`, leaving init refcount imbalanced.
7. **[trivial] [READ-VERIFIED] `playMusic` returns `void`** (audio.c:207) — caller cannot distinguish "playing" from "name not found" / "musicStream NULL" except via log spam. `playSound` returns -1 for the same cases; inconsistent.

### Allocation / perf
8. **[moderate] [READ-VERIFIED] Per-play stream churn + full-clip copy in `playSoundID` (audio.c:179–190).** Every SFX play heap-creates an `SDL_AudioStream` and copies the entire clip PCM into it (`SDL_PutAudioStreamData` copies). For frequent/large SFX this is hot-path churn; 32 pre-created pooled streams reused via `SDL_ClearAudioStream` would eliminate it. [NEEDS-RUNTIME] to quantify.

### Redundant / defensive code
9. **[trivial] [READ-VERIFIED] Double-clamping of volumes.** Setters clamp on store (audio.c:257, 263, 269), yet `playSoundID`:186, `playMusic`:227 and `setMusicVolume`:271–272 clamp the stored values again. Only justified because `AudioSystem` is fully exposed in druid.h for direct pokes.
10. **[trivial] [READ-VERIFIED] Inconsistent invariant checks:** `audioShutdown` tests `active && stream` (audio.c:73) while `audioPump` (audio.c:102–103) and `stopSound` (audio.c:200–201) trust `active` alone.
11. **[trivial] [READ-VERIFIED] Magic-number duplication:** refill sizing is `44100 / 4 * 2 * sizeof(f32)` in `audioPump` (audio.c:123) but a bare `88200u * 4u` (1 s) in `playMusic` (audio.c:230); no shared constant, easy to drift.

## Categories with no findings
Dead/unreachable code: none. Cache-hostile layout / SoA: none (32 tiny voices, irrelevant).
