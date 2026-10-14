#include "cs2d_sound.h"
#include "cs2d_types.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.c"
#pragma GCC diagnostic pop
/* stb_vorbis leaves these short macros defined */
#undef L
#undef C
#undef R

#define SND_RATE        44100
#define SND_MAX_SAMPLES 512
#define SND_MAX_VOICES  64

typedef struct {
    char   path[96];
    float *data;   /* Interleaved stereo float at SND_RATE; NULL when the file is missing or unreadable */
    int    frames;
} snd_sample_t;

typedef struct {
    snd_sample_t *s;
    int           pos;      /* Frame */
    float         gl, gr;   /* Channel gains */
    bool          active, loop;
    bool          tracked;  /* TPosSound: re-attenuated every frame */
    float         x, y, vol;
    unsigned      gen;
} snd_voice_t;

static struct {
    SDL_AudioStream *stream;
    snd_sample_t     samples[SND_MAX_SAMPLES];
    int              sample_count;
    snd_voice_t      voices[SND_MAX_VOICES];
    unsigned         gen;
    int              volume;    /* DAT_140ddf17d */
    bool             stereo;    /* DAT_140ddf17a */
    float            camx, camy;
    int              frame_count; /* DAT_140ddf3a8 */
    float           *mix;
    int              mix_frames;
} g_snd = { .volume = 10, .stereo = true };

/* ---------------------------------------------------------------- mixing */

static void SDLCALL snd_callback(void *ud, SDL_AudioStream *stream, int additional, int total)
{
    (void)ud; (void)total;
    int frames = additional / (int)(2 * sizeof(float));
    if (frames <= 0) return;
    if (frames > g_snd.mix_frames) {
        float *m = (float *)realloc(g_snd.mix, (size_t)frames * 2 * sizeof(float));
        if (!m) return;
        g_snd.mix = m;
        g_snd.mix_frames = frames;
    }
    float *out = g_snd.mix;
    memset(out, 0, (size_t)frames * 2 * sizeof(float));
    for (int v = 0; v < SND_MAX_VOICES; v++) {
        snd_voice_t *vo = &g_snd.voices[v];
        if (!vo->active) continue;
        const float *d = vo->s->data;
        for (int i = 0; i < frames; i++) {
            if (vo->pos >= vo->s->frames) {
                if (!vo->loop) { vo->active = false; break; }
                vo->pos = 0;
            }
            out[i * 2]     += d[vo->pos * 2] * vo->gl;
            out[i * 2 + 1] += d[vo->pos * 2 + 1] * vo->gr;
            vo->pos++;
        }
    }
    for (int i = 0; i < frames * 2; i++) {
        if (out[i] > 1.0f) out[i] = 1.0f;
        else if (out[i] < -1.0f) out[i] = -1.0f;
    }
    SDL_PutAudioStreamData(stream, out, frames * 2 * (int)sizeof(float));
}

bool cs2d_sound_init(void)
{
    if (g_snd.stream) return true;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        fprintf(stderr, "[CS2D Sound] SDL audio init failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_AudioSpec spec = { SDL_AUDIO_F32, 2, SND_RATE };
    g_snd.stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, snd_callback, NULL);
    if (!g_snd.stream) {
        fprintf(stderr, "[CS2D Sound] No audio device: %s\n", SDL_GetError());
        return false;
    }
    SDL_ResumeAudioStreamDevice(g_snd.stream);
    return true;
}

void cs2d_sound_shutdown(void)
{
    if (g_snd.stream) {
        SDL_DestroyAudioStream(g_snd.stream);
        g_snd.stream = NULL;
    }
    for (int i = 0; i < g_snd.sample_count; i++) SDL_free(g_snd.samples[i].data);
    g_snd.sample_count = 0;
    memset(g_snd.voices, 0, sizeof(g_snd.voices));
    free(g_snd.mix);
    g_snd.mix = NULL;
    g_snd.mix_frames = 0;
}

void cs2d_sound_set_volume(int volume)  { g_snd.volume = volume < 0 ? 0 : (volume > 100 ? 100 : volume); }
void cs2d_sound_set_stereo(bool stereo) { g_snd.stereo = stereo; }
void cs2d_sound_set_camera(float camx, float camy) { g_snd.camx = camx; g_snd.camy = camy; }

/* ---------------------------------------------------------------- loading */

static bool ends_with(const char *s, const char *ext)
{
    size_t n = strlen(s), m = strlen(ext);
    return n >= m && SDL_strcasecmp(s + n - m, ext) == 0;
}

static snd_sample_t *snd_load(const char *path)
{
    for (int i = 0; i < g_snd.sample_count; i++) {
        if (strcmp(g_snd.samples[i].path, path) == 0) return &g_snd.samples[i];
    }
    if (g_snd.sample_count >= SND_MAX_SAMPLES) return NULL;
    snd_sample_t *s = &g_snd.samples[g_snd.sample_count++];
    memset(s, 0, sizeof(*s));
    snprintf(s->path, sizeof(s->path), "%s", path);

    SDL_AudioSpec src;
    Uint8 *buf = NULL;
    Uint32 len = 0;
    bool owned_by_sdl = true;
    if (ends_with(path, ".ogg")) {
        int ch = 0, rate = 0;
        short *pcm = NULL;
        int n = stb_vorbis_decode_filename(path, &ch, &rate, &pcm);
        if (n <= 0 || !pcm) return s;
        src.format = SDL_AUDIO_S16;
        src.channels = ch;
        src.freq = rate;
        buf = (Uint8 *)pcm;
        len = (Uint32)n * (Uint32)ch * 2;
        owned_by_sdl = false;
    } else if (!SDL_LoadWAV(path, &src, &buf, &len)) {
        return s;
    }
    SDL_AudioSpec dst = { SDL_AUDIO_F32, 2, SND_RATE };
    Uint8 *out = NULL;
    int out_len = 0;
    if (SDL_ConvertAudioSamples(&src, buf, (int)len, &dst, &out, &out_len)) {
        s->data = (float *)out;
        s->frames = out_len / (int)(2 * sizeof(float));
    }
    if (owned_by_sdl) SDL_free(buf);
    else free(buf);
    return s;
}

/* ---------------------------------------------------------------- playing */

bool cs2d_sound_attenuate(float vol, float x, float y, int flags, float camx, float camy, float *gain, float *pan)
{
    /* cs2d_sound_play_at (0x1401191c0): g_camx / g_camy are ints */
    float lx = (float)((int)camx + 0x1a9), ly = (float)((int)camy + 0xf0);
    float g = vol - (fabsf(ly - y) + (float)flags + fabsf(lx - x)) * 0.001f;
    if (gain) *gain = g;
    if (pan) *pan = (x - lx) * 0.002f;
    return vol > 0.0f && g >= 0.0001f;
}

/* Channel gains from volume and pan. Pan law of the BlitzMax driver not verified: estimated as
 * left = min(1, 1 - pan), right = min(1, 1 + pan) */
static void voice_set(snd_voice_t *v, float vol, float pan, bool use_pan)
{
    float a = (float)g_snd.volume * 0.01f * vol;
    if (pan > 1.0f) pan = 1.0f;
    if (pan < -1.0f) pan = -1.0f;
    v->gl = a * (use_pan ? fminf(1.0f, 1.0f - pan) : 1.0f);
    v->gr = a * (use_pan ? fminf(1.0f, 1.0f + pan) : 1.0f);
}

static int voice_start(snd_sample_t *s, float vol, float pan, bool use_pan, bool loop)
{
    if (!s || !s->data || !g_snd.stream) return -1;
    SDL_LockAudioStream(g_snd.stream);
    int slot = -1;
    for (int i = 0; i < SND_MAX_VOICES; i++) {
        if (!g_snd.voices[i].active) { slot = i; break; }
    }
    if (slot < 0) { /* All busy: replace the voice closest to its end */
        int best = 0, left = 0x7fffffff;
        for (int i = 0; i < SND_MAX_VOICES; i++) {
            snd_voice_t *v = &g_snd.voices[i];
            if (v->loop) continue;
            int r = v->s->frames - v->pos;
            if (r < left) { left = r; best = i; }
        }
        slot = best;
    }
    snd_voice_t *v = &g_snd.voices[slot];
    memset(v, 0, sizeof(*v));
    v->s = s;
    v->loop = loop;
    v->active = true;
    v->gen = ++g_snd.gen & 0xffffff;
    voice_set(v, vol, pan, use_pan);
    SDL_UnlockAudioStream(g_snd.stream);
    return (int)((v->gen << 7) | (unsigned)slot);
}

static snd_voice_t *voice_get(int handle)
{
    if (handle < 0) return NULL;
    snd_voice_t *v = &g_snd.voices[handle & 0x7f];
    return (v->gen == ((unsigned)handle >> 7) && v->active) ? v : NULL;
}

int cs2d_sound_play(const char *path, float vol, bool force)
{
    /* FUN_140109d30 */
    if (!path || !g_snd.stream || vol <= 0.01f || g_snd.volume == 0) return -1;
    snd_sample_t *s = snd_load(path);
    if (!s || !s->data) return -1;
    g_snd.frame_count++;
    if (g_snd.frame_count > CS2D_SOUND_MAX_PER_FRAME && !force) return -1;
    return voice_start(s, vol, 0.0f, false, false);
}

int cs2d_sound_play_at(const char *path, float x, float y, float vol, int flags)
{
    if (!path || !g_snd.stream || g_snd.volume == 0 || g_snd.frame_count > CS2D_SOUND_MAX_PER_FRAME) return -1;
    float gain, pan;
    if (!cs2d_sound_attenuate(vol, x, y, flags, g_snd.camx, g_snd.camy, &gain, &pan)) return -1;
    snd_sample_t *s = snd_load(path);
    if (!s || !s->data) return -1;
    if (++g_snd.frame_count > CS2D_SOUND_MAX_PER_FRAME) return -1;
    return voice_start(s, gain, pan, g_snd.stereo, false);
}

int cs2d_sound_play_rand_at(const char *prefix, int count, const char *ext, float x, float y)
{
    if (count <= 0) return -1;
    char path[96];
    snprintf(path, sizeof(path), "%s%d%s", prefix, 1 + rand() % count, ext);
    return cs2d_sound_play_at(path, x, y, 1.0f, 0);
}

int cs2d_sound_play_tracked(const char *path, float x, float y, float vol, bool loop)
{
    /* TPosSound.Create (0x1401e3640) starts the channel through cs2d_sound_play_at at full volume, then
     * TPosSound.update re-attenuates it every frame with its own vol. A looping sound starts even out of
     * range so it is heard once the camera comes closer */
    if (!path || !g_snd.stream) return -1;
    snd_sample_t *s = snd_load(path);
    if (!s || !s->data) return -1;
    float gain, pan;
    bool audible = cs2d_sound_attenuate(vol, x, y, 0, g_snd.camx, g_snd.camy, &gain, &pan);
    if (!loop && (!audible || g_snd.volume == 0)) return -1;
    if (!audible) gain = 0.0f;
    int h = voice_start(s, gain, pan, g_snd.stereo, loop);
    snd_voice_t *v = voice_get(h);
    if (v) {
        v->tracked = true;
        v->x = x;
        v->y = y;
        v->vol = vol;
    }
    return h;
}

void cs2d_sound_stop(int voice)
{
    if (!g_snd.stream) return;
    SDL_LockAudioStream(g_snd.stream);
    snd_voice_t *v = voice_get(voice);
    if (v) v->active = false;
    SDL_UnlockAudioStream(g_snd.stream);
}

bool cs2d_sound_playing(int voice)
{
    if (!g_snd.stream) return false;
    SDL_LockAudioStream(g_snd.stream);
    bool r = voice_get(voice) != NULL;
    SDL_UnlockAudioStream(g_snd.stream);
    return r;
}

void cs2d_sound_stop_all(void)
{
    if (!g_snd.stream) return;
    SDL_LockAudioStream(g_snd.stream);
    for (int i = 0; i < SND_MAX_VOICES; i++) g_snd.voices[i].active = false;
    SDL_UnlockAudioStream(g_snd.stream);
}

void cs2d_sound_frame(void)
{
    g_snd.frame_count = 0;
    if (!g_snd.stream) return;
    SDL_LockAudioStream(g_snd.stream);
    for (int i = 0; i < SND_MAX_VOICES; i++) {
        snd_voice_t *v = &g_snd.voices[i];
        if (!v->active || !v->tracked) continue;
        float gain, pan;
        if (!cs2d_sound_attenuate(v->vol, v->x, v->y, 0, g_snd.camx, g_snd.camy, &gain, &pan)) gain = 0.0f;
        voice_set(v, gain, pan, g_snd.stereo);
    }
    SDL_UnlockAudioStream(g_snd.stream);
}

/* ---------------------------------------------------------------- weapons */

const char *cs2d_sound_weapon_file(int weapon, bool silenced)
{
    /* Slots of the array 0x1409f4a20 filled in 0x140230330 with the globals loaded in 0x14030cc00 */
    if (silenced && weapon == CS2D_WEP_USP)  return "sfx/weapons/usp_silenced.wav";
    if (silenced && weapon == CS2D_WEP_M4A1) return "sfx/weapons/m4a1_silenced.wav";
    switch (weapon) {
    case CS2D_WEP_USP:             return "sfx/weapons/usp.wav";
    case CS2D_WEP_GLOCK:           return "sfx/weapons/glock18.wav";
    case CS2D_WEP_DEAGLE:          return "sfx/weapons/deagle.wav";
    case CS2D_WEP_P228:            return "sfx/weapons/p228.wav";
    case CS2D_WEP_ELITE:           return "sfx/weapons/elite.wav";
    case CS2D_WEP_FIVESEVEN:       return "sfx/weapons/fiveseven.wav";
    case CS2D_WEP_M3:              return "sfx/weapons/m3.wav";
    case CS2D_WEP_XM1014:          return "sfx/weapons/xm1014.wav";
    case CS2D_WEP_MP5:             return "sfx/weapons/mp5.wav";
    case CS2D_WEP_TMP:             return "sfx/weapons/tmp.wav";
    case CS2D_WEP_P90:             return "sfx/weapons/p90.wav";
    case CS2D_WEP_MAC10:           return "sfx/weapons/mac10.wav";
    case CS2D_WEP_UMP45:           return "sfx/weapons/ump45.wav";
    case CS2D_WEP_AK47:            return "sfx/weapons/ak47.wav";
    case CS2D_WEP_SG552:           return "sfx/weapons/sg552.wav";
    case CS2D_WEP_M4A1:            return "sfx/weapons/m4a1.wav";
    case CS2D_WEP_AUG:             return "sfx/weapons/aug.wav";
    case CS2D_WEP_SCOUT:           return "sfx/weapons/scout.wav";
    case CS2D_WEP_AWP:             return "sfx/weapons/awp.wav";
    case CS2D_WEP_G3SG1:           return "sfx/weapons/g3sg1.wav";
    case CS2D_WEP_SG550:           return "sfx/weapons/sg550.wav";
    case CS2D_WEP_GALIL:           return "sfx/weapons/galil.wav";
    case CS2D_WEP_FAMAS:           return "sfx/weapons/famas.wav";
    case CS2D_WEP_M249:            return "sfx/weapons/m249.wav";
    case CS2D_WEP_LASER:           return "sfx/weapons/laser.ogg";
    case CS2D_WEP_FLAMETHROWER:    return "sfx/weapons/flamethrower.ogg";
    case CS2D_WEP_RPG7:
    case CS2D_WEP_ROCKETLAUNCHER:  return "sfx/weapons/rocket.ogg";
    case CS2D_WEP_GRENADELAUNCHER: return "sfx/weapons/glaunch.ogg";
    case CS2D_WEP_KNIFE:
    case CS2D_WEP_WRENCH:
    case CS2D_WEP_CLAW:            return "sfx/weapons/knife_slash.wav";
    case CS2D_WEP_MACHETE:         return "sfx/weapons/machete_slash.wav";
    case CS2D_WEP_CHAINSAW:        return "sfx/weapons/chainsaw.wav";
    case CS2D_WEP_PORTALGUN:       return "sfx/weapons/portalgun.ogg";
    case CS2D_WEP_M134:            return "sfx/weapons/m134.wav";
    case CS2D_WEP_FNF2000:         return "sfx/weapons/fnf2000.wav";
    case 92:                       return "sfx/weapons/teslacoil.wav"; /* Tesla coil (slot 0x2e0) */
    default:                       return NULL;
    }
}
