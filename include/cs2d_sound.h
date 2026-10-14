#ifndef CS2D_SOUND_H
#define CS2D_SOUND_H

#include <stdbool.h>

/*
 * Sound effects on an SDL3 audio stream (the original plays BlitzMax TSound / TChannel objects).
 *
 * Positional sounds follow cs2d_sound_play_at (CS2D.exe 0x1401191c0):
 *   listener = (camx + 425, camy + 240)            (0x1a9 / 0xf0: the centre of an 850x480 screen)
 *   gain     = vol - (|lx - x| + |ly - y| + flags) * 0.001, not played below 0.0001
 *   pan      = (x - lx) * 0.002                    (only with stereo sound, DAT_140ddf17a)
 *   channel volume = volume * 0.01 * gain          (config "volume", DAT_140ddf17d; 0 mutes everything)
 * 2D sounds (FUN_140109d30) play when vol > 0.01 at volume * 0.01 * vol.
 * Both count against a per-frame limit: the 21st sound of a frame is dropped (DAT_140ddf3a8, reset each
 * frame at 0x140230000 + ...; 2D sounds with force ignore it).
 *
 * Files load once by path (relative to the game folder: "sfx/weapons/ak47.wav") and stay cached;
 * .wav goes through SDL_LoadWAV, .ogg through the vendored stb_vorbis. Without an audio device
 * (tests, headless) every play call is a no-op that returns -1.
 */

#define CS2D_SOUND_MAX_PER_FRAME 20 /* DAT_140ddf3a8 < 0x15 */

bool cs2d_sound_init(void);
void cs2d_sound_shutdown(void);

void cs2d_sound_set_volume(int volume);  /* "volume" 0..100 */
void cs2d_sound_set_stereo(bool stereo); /* "soundmode" */
void cs2d_sound_set_camera(float camx, float camy);

/* Start of a frame: resets the sound counter and re-pans the tracked sounds (TPosSound.update, 0x140278730) */
void cs2d_sound_frame(void);

/* The attenuation of cs2d_sound_play_at before the volume setting: false when the sound is not played */
bool cs2d_sound_attenuate(float vol, float x, float y, int flags, float camx, float camy, float *gain, float *pan);

/* 2D sound (FUN_140109d30); force skips the per-frame limit. Returns a voice handle or -1 */
int  cs2d_sound_play(const char *path, float vol, bool force);
/* Positional sound (cs2d_sound_play_at) */
int  cs2d_sound_play_at(const char *path, float x, float y, float vol, int flags);
/* One of the numbered files "<prefix>1<ext>" .. "<prefix><count><ext>" at random (the sound arrays) */
int  cs2d_sound_play_rand_at(const char *prefix, int count, const char *ext, float x, float y);
/* Positional sound that follows the camera while it plays (TPosSound); loop repeats it until stopped */
int  cs2d_sound_play_tracked(const char *path, float x, float y, float vol, bool loop);
void cs2d_sound_stop(int voice);
bool cs2d_sound_playing(int voice);
void cs2d_sound_stop_all(void);

/* File a weapon fires with (FUN_14018c6b0: the per-type array 0x1409f4a20; silenced USP / M4A1 use
 * usp_silenced / m4a1_silenced at flags 100). NULL for weapons without a fire sound */
const char *cs2d_sound_weapon_file(int weapon, bool silenced);

#endif /* CS2D_SOUND_H */
