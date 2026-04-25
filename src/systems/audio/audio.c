#include "../../../include/druid.h"
#include <string.h>

AudioSystem *audio = NULL;

// SDL3 audio format we normalise everything to: f32 stereo 44100
static const SDL_AudioSpec s_targetSpec = {
    .format   = SDL_AUDIO_F32,
    .channels = 2,
    .freq     = 44100
};

static f32 clampVol(f32 v)
{
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

b8 audioInit(void)
{
    if (audio) return true;

    if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
    {
        WARN("SDL_InitSubSystem(AUDIO) failed: %s", SDL_GetError());
        return false;
    }

    audio = (AudioSystem *)dalloc(sizeof(AudioSystem), MEM_TAG_AUDIO);
    if (!audio)
    {
        ERROR("Failed to allocate AudioSystem");
        return false;
    }
    memset(audio, 0, sizeof(AudioSystem));
    audio->masterVol    = 1.0f;
    audio->sfxVol       = 1.0f;
    audio->musicVol     = 1.0f;
    audio->musicClipIdx = (u32)-1;

    audio->device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &s_targetSpec);
    if (audio->device == 0)
    {
        WARN("SDL_OpenAudioDevice failed: %s", SDL_GetError());
        dfree(audio, sizeof(AudioSystem), MEM_TAG_AUDIO);
        audio = NULL;
        return false;
    }

    // music stream — bound to device, starts paused
    audio->musicStream = SDL_CreateAudioStream(&s_targetSpec, &s_targetSpec);
    if (!audio->musicStream)
    {
        WARN("Failed to create music stream: %s", SDL_GetError());
    }
    else
    {
        SDL_BindAudioStream(audio->device, audio->musicStream);
    }

    INFO("Audio system initialised (device %u)", audio->device);
    return true;
}

void audioShutdown(void)
{
    if (!audio) return;

    // stop all voices
    for (u32 i = 0; i < AUDIO_MAX_VOICES; i++)
    {
        if (audio->voices[i].active && audio->voices[i].stream)
        {
            SDL_UnbindAudioStream(audio->voices[i].stream);
            SDL_DestroyAudioStream(audio->voices[i].stream);
            audio->voices[i].stream = NULL;
            audio->voices[i].active = false;
        }
    }

    if (audio->musicStream)
    {
        SDL_UnbindAudioStream(audio->musicStream);
        SDL_DestroyAudioStream(audio->musicStream);
        audio->musicStream = NULL;
    }

    SDL_CloseAudioDevice(audio->device);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    dfree(audio, sizeof(AudioSystem), MEM_TAG_AUDIO);
    audio = NULL;
}

void audioPump(void)
{
    if (!audio) return;

    // reap finished SFX voices
    for (u32 i = 0; i < AUDIO_MAX_VOICES; i++)
    {
        if (!audio->voices[i].active) continue;
        SDL_AudioStream *s = audio->voices[i].stream;
        // queued == 0 means all data has been consumed by the device
        if (SDL_GetAudioStreamQueued(s) == 0)
        {
            SDL_UnbindAudioStream(s);
            SDL_DestroyAudioStream(s);
            audio->voices[i].stream = NULL;
            audio->voices[i].active = false;
        }
    }

    // music refill for looping
    if (!audio->musicStream) return;
    if (audio->musicClipIdx == (u32)-1) return;

    AudioClip *clip = resGetAudio(audio->musicClipIdx);
    if (!clip) return;

    // keep at least 1/4 second buffered (target: f32 stereo 44100 = 4 bytes * 2ch * 44100 = 352800 bytes/sec)
    i32 queued = SDL_GetAudioStreamQueued(audio->musicStream);
    i32 refillThreshold = 44100 / 4 * 2 * (i32)sizeof(f32);

    if (queued < refillThreshold)
    {
        if (audio->musicCursor >= clip->byteLen)
        {
            if (audio->musicLoop)
                audio->musicCursor = 0;
            else
            {
                audio->musicClipIdx = (u32)-1;
                return;
            }
        }

        u32 remaining = clip->byteLen - audio->musicCursor;
        u32 chunk     = remaining < (u32)refillThreshold * 2 ? remaining : (u32)refillThreshold * 2;
        SDL_PutAudioStreamData(audio->musicStream, clip->pcm + audio->musicCursor, (i32)chunk);
        audio->musicCursor += chunk;
    }
}

i32 playSound(const c8 *name, f32 volume)
{
    i32 idx = resFindAudio(name);
    if (idx < 0)
    {
        WARN("Audio clip not found: %s", name);
        return -1;
    }
    return playSoundID((u32)idx, volume);
}

i32 playSoundID(u32 audioIdx, f32 volume)
{
    if (!audio) return -1;

    AudioClip *clip = resGetAudio(audioIdx);
    if (!clip) return -1;

    // find free voice
    i32 slot = -1;
    for (u32 i = 0; i < AUDIO_MAX_VOICES; i++)
    {
        if (!audio->voices[i].active)
        {
            slot = (i32)i;
            break;
        }
    }
    if (slot < 0)
    {
        WARN("No free audio voice slots");
        return -1;
    }

    SDL_AudioStream *stream = SDL_CreateAudioStream(&clip->spec, &s_targetSpec);
    if (!stream)
    {
        WARN("Failed to create audio stream: %s", SDL_GetError());
        return -1;
    }

    f32 vol = clampVol(volume) * clampVol(audio->sfxVol) * clampVol(audio->masterVol);
    SDL_SetAudioStreamGain(stream, vol);
    SDL_BindAudioStream(audio->device, stream);
    SDL_PutAudioStreamData(stream, clip->pcm, (i32)clip->byteLen);
    SDL_FlushAudioStream(stream);

    audio->voices[slot].stream = stream;
    audio->voices[slot].active = true;
    return slot;
}

void stopSound(i32 voice)
{
    if (!audio || voice < 0 || voice >= AUDIO_MAX_VOICES) return;
    if (!audio->voices[voice].active) return;
    SDL_UnbindAudioStream(audio->voices[voice].stream);
    SDL_DestroyAudioStream(audio->voices[voice].stream);
    audio->voices[voice].stream = NULL;
    audio->voices[voice].active = false;
}

void playMusic(const c8 *name, b8 loop)
{
    if (!audio || !audio->musicStream) return;

    i32 idx = resFindAudio(name);
    if (idx < 0)
    {
        WARN("Music clip not found: %s", name);
        return;
    }

    AudioClip *clip = resGetAudio((u32)idx);
    if (!clip) return;

    SDL_ClearAudioStream(audio->musicStream);
    audio->musicClipIdx = (u32)idx;
    audio->musicCursor  = 0;
    audio->musicLoop    = loop;

    // initial fill
    f32 vol = clampVol(audio->musicVol) * clampVol(audio->masterVol);
    SDL_SetAudioStreamGain(audio->musicStream, vol);

    u32 chunk = clip->byteLen < 88200u * 4u ? clip->byteLen : 88200u * 4u;
    SDL_PutAudioStreamData(audio->musicStream, clip->pcm, (i32)chunk);
    audio->musicCursor = chunk;
    SDL_ResumeAudioStreamDevice(audio->musicStream);
}

void stopMusic(void)
{
    if (!audio || !audio->musicStream) return;
    SDL_ClearAudioStream(audio->musicStream);
    SDL_PauseAudioStreamDevice(audio->musicStream);
    audio->musicClipIdx = (u32)-1;
    audio->musicCursor  = 0;
}

void pauseMusic(b8 paused)
{
    if (!audio || !audio->musicStream) return;
    if (paused)
        SDL_PauseAudioStreamDevice(audio->musicStream);
    else
        SDL_ResumeAudioStreamDevice(audio->musicStream);
}

void setMasterVolume(f32 v)
{
    if (!audio) return;
    audio->masterVol = clampVol(v);
}

void setSfxVolume(f32 v)
{
    if (!audio) return;
    audio->sfxVol = clampVol(v);
}

void setMusicVolume(f32 v)
{
    if (!audio) return;
    audio->musicVol = clampVol(v);
    if (audio->musicStream)
        SDL_SetAudioStreamGain(audio->musicStream,
            clampVol(audio->musicVol) * clampVol(audio->masterVol));
}
