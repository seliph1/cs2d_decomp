#ifndef CS2D_HOSTAGE_H
#define CS2D_HOSTAGE_H

#include <stdbool.h>
#include <stdint.h>
#include "cs2d_types.h"

/*
 * Hostages (class Thostage in CS2D.exe, 0x14001c280 New .. 0x140147090 draw).
 * The client keeps its own list: built from the map's Info_Hostage entities at round start (FUN_1402520b0),
 * or from the join snapshot [252,7,2]; the server then moves them with [29] and the use echo [26].
 * Movement on the client is Thostage.draw: 4 px per drawn frame on each axis towards the target tile (tx, ty),
 * facing the followed player. Path finding (findtarget) and rescue run on the server only.
 */
#define CS2D_MAX_HOSTAGES 64

typedef struct {
    bool  active;
    int   id;
    int   health;      /* 0 = rescued or dead (not drawn) */
    int   follow;      /* Player id the hostage follows, 0 = none */
    bool  used;        /* First use pays the CT $150 (Thostage.use) */
    int   look;        /* Frame of gfx/player/hostages.bmp (0..7) */
    float x, y, rot;
    int   tx, ty;      /* Target tile from [29, id, 0, tx, ty]; -1 = none */
} cs2d_hostage_t;

typedef struct {
    cs2d_hostage_t h[CS2D_MAX_HOSTAGES];
} cs2d_hostage_world_t;

void cs2d_hostages_clear(cs2d_hostage_world_t *w);

/* Round start (FUN_1402520b0): Thostage.freeall, then one hostage per Info_Hostage in entity order, ids 1..n,
 * at the tile centre, rot = ints[0], look = min(ints[1], 7) */
void cs2d_hostages_spawn_from_map(cs2d_hostage_world_t *w, const cs2d_map_t *map);

/* Join snapshot [252,7,2]: count u8, then {id u8, health u8, look u8, follow u8, x u16, y u16} (8 bytes).
 * Reads from buf[*pos], advances *pos; returns false on a short message. */
bool cs2d_hostages_read_snapshot(cs2d_hostage_world_t *w, const uint8_t *buf, int len, int *pos);

cs2d_hostage_t *cs2d_hostages_get(cs2d_hostage_world_t *w, int id);

/* Living hostage whose tile ((int)x >> 5, (int)y >> 5) is (tx, ty), or NULL (FUN_14014f820 use ray) */
cs2d_hostage_t *cs2d_hostages_at_tile(cs2d_hostage_world_t *w, int tx, int ty);

/* Thostage.use (0x1401193f0): CT player `pid` starts (release = false) or stops being followed.
 * Returns true when this was the first use of the hostage (the user gets $150, capped at 16000). */
bool cs2d_hostages_use(cs2d_hostage_world_t *w, int id, cs2d_player_t *user, int pid, bool release);

/* [29, id, kind, a u16, b u16] (FUN_1402d93f0): kind 0 = new target tile (a, b), 2 = rescued by player b,
 * 3 = hurt to health a by player b, 4 = killed by player b. Returns the kind handled, -1 if ignored. */
int cs2d_hostages_net_message(cs2d_hostage_world_t *w, int id, int kind, int a, int b);

/* Thostage.draw movement part; frames = elapsed time in 60 Hz frames */
void cs2d_hostages_update(cs2d_hostage_world_t *w, const cs2d_player_t *players, float frames);

#endif
