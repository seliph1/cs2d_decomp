#ifndef CS2D_SPECTATE_H
#define CS2D_SPECTATE_H

#include "cs2d_types.h"

/*
 * Spectator camera of CS2D.exe: what a dead player or a Spectator sees (FUN_14025aa60 runs every frame
 * while the local player is not alive). The server decides what is allowed with sv_specmode, sent in
 * [252,6] and changed with [237,3,mode i32]:
 *   0 Nothing (War Mode): the camera shows nothing
 *   1 Everything:         free look or follow any living player
 *   2 Own Team only:      follow only, and only the own team (a Spectator follows anyone)
 * The client tells the server whom it watches with [67, player] (0 = free look) and where its camera
 * is, once per second, with [32, x + 32768 u16, y + 32768 u16] (camera center).
 */

typedef enum {
    CS2D_SPEC_NOTHING = 0, /* sv_specmode 0 */
    CS2D_SPEC_FREE    = 1, /* "Free Look": the movement keys scroll the camera */
    CS2D_SPEC_FOLLOW  = 2  /* "Follow Player": the camera follows spec.target */
} cs2d_spec_mode_t;

typedef struct {
    int  server_mode; /* sv_specmode of the server (0x140a14ee8[1]) */
    int  mode;        /* cs2d_spec_mode_t (DAT_140ddf2f8) */
    int  target;      /* Followed player, 0 = none (DAT_140ddf2f4) */
    bool idle_cam;    /* Join "showcase": while the team menu is open and nobody plays, the camera jumps
                         between random Info_T / Info_CT every 5 s (DAT_140ddf328) */
    int  idle_ticks;  /* Seconds until the next jump (DAT_140ddf324) */
    int  idle_team;   /* 1: next jump to an Info_T, 2: to an Info_CT (DAT_140ddf320) */
} cs2d_spec_t;

/* Fresh state for a join (mode NOTHING, no target, no showcase); server_mode is sv_specmode */
void cs2d_spec_init(cs2d_spec_t *spec, int server_mode);

/* Whether a living player of team `team` may be followed by a local player of team `local_team` */
bool cs2d_spec_can_follow(const cs2d_spec_t *spec, int local_team, int team);

/*
 * FUN_1401698a0: dir 0 keeps a valid target (or picks the first living player), 1 the next one, -1 the
 * previous one, in player id order. Quirk of the original: "previous" from the second player wraps to
 * the last one. Returns true when the target was (re)chosen: the caller then sends [67, target]
 * (FUN_1401682f0, only while the local player is dead). Without anybody to follow the target becomes 0.
 */
bool cs2d_spec_pick(cs2d_spec_t *spec, const cs2d_game_t *game, int local_id, int dir);

/*
 * Free look (FUN_14025aa60): 10 px per 60 Hz frame along each held key, half with walk; like the
 * original the integer camera is truncated after each axis step. frames = elapsed 60 Hz frames.
 */
void cs2d_spec_free_look(int *cam_x, int *cam_y, bool left, bool right, bool up, bool down, bool walk,
                         float frames);

/*
 * Tmap_scroll.toplayer_smoothly (0x140013780): the camera top-left goes to (px - half_w, py - half_h);
 * within 32 px (maxDelta) on both axes it moves 0.3 of the distance per 60 Hz frame, farther it jumps.
 */
void cs2d_spec_follow(int *cam_x, int *cam_y, float px, float py, float half_w, float half_h, float frames);

#endif /* CS2D_SPECTATE_H */
