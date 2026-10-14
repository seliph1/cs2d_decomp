#include "cs2d_gen.h"
#include "cs2d_map.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* BlitzMax Rnd: float in [lo, hi) */
static float rnd(cs2d_gen_t *g, float lo, float hi)
{
    g->seed = g->seed * 1664525u + 1013904223u;
    return lo + (float)(g->seed >> 8) / 16777216.0f * (hi - lo);
}

/* BlitzMax Rand: int in [lo, hi], either order */
static int rand_i(cs2d_gen_t *g, int lo, int hi)
{
    if (hi < lo) { int t = lo; lo = hi; hi = t; }
    int v = lo + (int)rnd(g, 0.0f, (float)(hi - lo + 1));
    return v > hi ? hi : v;
}

/* Tp.Create (0x14004ac20): the defaults of every type, then the per-type ones used here */
static cs2d_gen_particle_t *create(cs2d_gen_t *g, int typ, float x, float y)
{
    if (g->count >= CS2D_GEN_MAX) {
        memmove(&g->p[0], &g->p[1], sizeof(g->p[0]) * (CS2D_GEN_MAX - 1)); /* Drop the oldest */
        g->count--;
    }
    cs2d_gen_particle_t *p = &g->p[g->count++];
    memset(p, 0, sizeof(*p));
    p->typ = (uint8_t)typ;
    p->x = x;
    p->y = y;
    p->sx = p->sy = 1.0f;
    p->a = 1.0f;
    p->r = p->g = p->b = 255.0f;
    p->layer = CS2D_GEN_LAYER_TOP;
    switch (typ) {
        case 1:
            p->layer = CS2D_GEN_LAYER_FLOOR;
            break;
        case 3:
            p->r = p->g = p->b = (float)rand_i(g, 0, 100);
            p->fx    = rnd(g, -0.3f, 0.3f);
            p->fy    = rnd(g, -0.3f, 0.3f);
            p->frame = rand_i(g, 0, 3);
            p->rot   = (float)rand_i(g, 360, 1);
            p->rots  = rnd(g, -2.0f, 2.0f);
            p->sy    = 0.005f;
            break;
        case 12:
            p->rot  = (float)rand_i(g, 360, 1);
            p->a    = rnd(g, 0.7f, 1.2f);
            p->sx   = rnd(g, 0.4f, 0.7f);
            p->fy   = rnd(g, -2.0f, 2.0f);
            p->fx   = rnd(g, -2.0f, 2.0f);
            p->rots = 0.01f;
            break;
        case 14:
            p->r   = (float)rand_i(g, 200, 255);
            p->g   = (float)rand_i(g, 0, (int)p->r);
            p->b   = 0.0f;
            p->a   = 0.6f;
            p->fy  = rnd(g, -0.4f, 0.4f);
            p->fx  = rnd(g, -0.4f, 0.4f);
            p->rot = (float)rand_i(g, 360, 1);
            break;
        case 19:
        case 20: {
            /* Around the screen centre, but not right on it: x in +-[30, 425], y in +-[20, 240] */
            p->layer = CS2D_GEN_LAYER_WEATHER;
            p->x  = (float)(rand_i(g, 0, 1) == 1 ? rand_i(g, -425, -30) : rand_i(g, 30, 425));
            p->y  = (float)(rand_i(g, 0, 1) == 1 ? rand_i(g, -240, -20) : rand_i(g, 20, 240));
            p->sx = rnd(g, 0.5f, 2.5f);
            p->sy = rnd(g, 100.0f, 150.0f);
            p->fx = rnd(g, 0.0f, 360.0f);
            p->fy = rnd(g, -2.0f, 2.0f);
            if (typ == 20) {
                p->a   = 0.9f;
                p->rot = (float)(atan2(-(double)p->x, (double)p->y) * 57.29577951308232 + 180.0);
            }
            break;
        }
        case 21:
            p->layer = CS2D_GEN_LAYER_FOG;
            p->r     = (float)rand_i(g, 170, 200);
            p->g     = p->r + 15.0f;
            p->b     = p->r + 25.0f;
            p->fx    = rnd(g, -0.2f, 0.2f);
            p->fy    = rnd(g, -0.2f, 0.2f);
            p->frame = rand_i(g, 0, 3);
            p->rot   = (float)rand_i(g, 360, 1);
            p->rots  = rnd(g, -0.2f, 0.2f);
            p->a     = rnd(g, 0.0f, 0.1f);
            p->sx    = rnd(g, 2.0f, 5.0f);
            break;
        default:
            break;
    }
    return p;
}

/* g_prop_class of a tile mode (classes as TmapPreview draws them): 0 wall, 1 floor, 2 obstacle, -1 deadly */
static int prop_class(const cs2d_map_t *map, float x, float y)
{
    int tx = (int)(x * 0.03125f), ty = (int)(y * 0.03125f);
    if (!map || x < 0.0f || y < 0.0f || tx >= (int)map->width || ty >= (int)map->height) return 1;
    uint8_t m = cs2d_map_get_tile_mode(map, (uint32_t)tx, (uint32_t)ty);
    if (m == CS2D_TILE_WALL || m == CS2D_TILE_WALL_NOSHADOW || m == CS2D_TILE_WALL_FLOORLEVEL) return 0;
    if (m == CS2D_TILE_OBSTACLE || m == CS2D_TILE_OBSTACLE_NOSHADOW) return 2;
    if (m >= CS2D_TILE_DEADLY_NORMAL && m <= CS2D_TILE_DEADLY_ABYSS) return -1;
    return 1;
}

void cs2d_gen_init(cs2d_gen_t *g, const cs2d_map_t *map)
{
    if (!g) return;
    uint32_t seed = g->seed ? g->seed : 0x5eed6e17u;
    memset(g, 0, sizeof(*g));
    g->seed = seed;
    if (!map) return;
    for (uint32_t i = 0; i < map->entity_count; i++) {
        cs2d_entity_type_t t = map->entities[i].type;
        if (t >= CS2D_ENT_GEN_PARTICLES && t <= CS2D_ENT_GEN_FX && g->ent_count < CS2D_GEN_MAX_ENTS) {
            g->ents[g->ent_count++] = (int)i;
        } else if (t == CS2D_ENT_INFO_NOWEATHER && g->noweather_count < CS2D_GEN_MAX_ENTS) {
            g->noweather[g->noweather_count++] = (int)i;
        }
    }
}

bool cs2d_gen_no_weather(const cs2d_gen_t *g, const cs2d_map_t *map, float cam_x, float cam_y,
                         float half_w, float half_h)
{
    /* Te.draw case 0x34: the screen centre (camx + 425, camy + 240) inside [x, x + ints[0]] x [y, y + ints[1]] tiles */
    int cx = (int)(cam_x + half_w), cy = (int)(cam_y + half_h);
    for (int k = 0; k < g->noweather_count; k++) {
        const cs2d_entity_t *e = &map->entities[g->noweather[k]];
        if (e->x * 32 <= cx && e->y * 32 <= cy &&
            cx <= (e->x + e->int_settings[0]) * 32 && cy <= (e->y + e->int_settings[1]) * 32) return true;
    }
    return false;
}

/* Te.draw (0x140271ee0) pass 2, cases 0x32..0x35: one frame of spawning for entity slot k */
static void spawn(cs2d_gen_t *g, const cs2d_map_t *map, int k, float cam_x, float cam_y, float half_w, float half_h)
{
    const cs2d_entity_t *e = &map->entities[g->ents[k]];
    const int *v = e->int_settings;
    float wx = (float)(e->x * 32), wy = (float)(e->y * 32);

    switch (e->type) {
        case CS2D_ENT_GEN_PARTICLES: {
            if (fabsf(wx - half_w - cam_x) >= 670.0f || fabsf(wy - half_h - cam_y) >= 590.0f) break;
            /* Te.setup case 0x32: strs[0] is the colour as an int (0xRRGGBB) */
            unsigned rgb = (unsigned)strtol(e->str_settings[0], NULL, 10);
            cs2d_gen_particle_t *p = create(g, 17, wx + 16.0f, wy + 16.0f);
            p->fx = rnd(g, (float)v[2], (float)v[3]);
            p->fy = rnd(g, (float)v[4], (float)v[5]);
            p->a  = (float)rand_i(g, (int)((float)v[0] * 0.0005f), (int)((float)v[1] * 0.0005f));
            p->r  = (float)((rgb >> 16) & 0xff);
            p->g  = (float)((rgb >> 8) & 0xff);
            p->b  = (float)(rgb & 0xff);
            break;
        }
        case CS2D_ENT_GEN_SPRITES: {
            /* No camera check; Tp level 5 */
            int oy = rand_i(g, -8, 8), ox = rand_i(g, -8, 8);
            cs2d_gen_particle_t *p = create(g, 12, wx + 16.0f + (float)ox, wy + 16.0f + (float)oy);
            p->fx    = rnd(g, (float)v[4], (float)v[5]);
            p->fy    = rnd(g, (float)v[6], (float)v[7]);
            p->r     = (float)v[1];
            p->g     = (float)v[2];
            p->b     = (float)v[3];
            p->a     = (float)strtod(e->str_settings[0], NULL);
            p->frame = v[0] < 0 ? 0 : (v[0] > 15 ? 15 : v[0]);
            break;
        }
        case CS2D_ENT_GEN_WEATHER: {
            int kind = v[0];
            if (kind == 0 || kind == 1) {
                /* Rain: max(ints[1] / 2, 1) Tp 20 per frame; snow: max(ints[1], 1) Tp 19 */
                if (cs2d_gen_no_weather(g, map, cam_x, cam_y, half_w, half_h)) break;
                int n = kind == 0 ? (v[1] < 2 ? 1 : v[1] >> 1) : (v[1] > 0 ? v[1] : 1);
                for (int i = 0; i < n; i++) create(g, kind == 0 ? 20 : 19, 0.0f, 0.0f);
            } else if (kind >= 2 && kind <= 4) {
                /* Fog / dust / smog: max(ints[1], 1) Tp 21 within 50 px of the tile centre */
                if (fabsf(wx - cam_x) >= 960.0f || fabsf(wy - cam_y) >= 720.0f) break;
                int n = v[1] > 0 ? v[1] : 1;
                for (int i = 0; i < n; i++) {
                    int oy = rand_i(g, -50, 50), ox = rand_i(g, -50, 50);
                    cs2d_gen_particle_t *p = create(g, 21, wx + 16.0f + (float)ox, wy + 16.0f + (float)oy);
                    if (kind == 3) {
                        p->r = p->g = (float)rand_i(g, 120, 200);
                        p->b = (float)rand_i(g, 0, 20);
                    } else if (kind == 4) {
                        p->r = p->g = p->b = (float)rand_i(g, 50, 100);
                    }
                }
            } else if (kind == 5) {
                /* Thunder (0x14027525b): ints[9] counts down from Rand(10, 30) frames; at 2 the screen
                 * flashes Rnd(0.3, 0.4), at 1 sfx/env/thunder.wav plays */
                int *c = &g->counter[k];
                if (*c == 0) {
                    *c = rand_i(g, 10, 30);
                } else {
                    (*c)--;
                    if (*c == 2) g->flash = rnd(g, 0.3f, 0.4f);
                    else if (*c == 1) g->thunder++;
                }
            }
            break;
        }
        case CS2D_ENT_GEN_FX: {
            /* Every max(ints[5], 25) ms (Te.setup case 0x35); ints[9] = last spawn, set before the camera check */
            int rate = v[5] < 25 ? 25 : v[5];
            if (g->time_ms - g->counter[k] < rate) break;
            g->counter[k] = g->time_ms;
            if (fabsf(wx - cam_x - half_w) >= 850.0f || fabsf(wy - cam_y - half_h) >= 480.0f) break;
            int oy = rand_i(g, v[3], v[4]), ox = rand_i(g, v[1], v[2]);
            float px = wx + (float)ox, py = wy + (float)oy;
            cs2d_gen_particle_t *p;
            switch (v[0]) {
                case 0: /* Waves */
                    p = create(g, 1, px, py);
                    p->a  = rnd(g, 0.2f, 0.9f);
                    p->sx = rnd(g, 0.1f, 0.4f);
                    break;
                case 1: /* Smoke */
                    p = create(g, 3, px, py);
                    p->sx = rnd(g, 0.5f, 0.9f);
                    p->a  = rnd(g, 0.5f, 0.9f);
                    p->fx = rnd(g, -0.25f, 0.25f);
                    p->fy = rnd(g, -0.25f, 0.25f);
                    p->r = p->g = p->b = (float)rand_i(g, 30, 120);
                    break;
                case 2: /* Fire: a flame, and every other time some smoke */
                    p = create(g, 14, px, py);
                    p->sx = rnd(g, 0.1f, 0.3f);
                    p->fx = rnd(g, -0.5f, 0.5f);
                    p->fy = rnd(g, -0.5f, 0.5f);
                    p->a  = rnd(g, 0.8f, 1.3f);
                    if (rand_i(g, 2, 1) == 1) {
                        oy = rand_i(g, v[3], v[4]);
                        ox = rand_i(g, v[1], v[2]);
                        p = create(g, 3, wx + (float)ox, wy + (float)oy);
                        p->sx = rnd(g, 0.4f, 1.0f);
                        p->a  = rnd(g, 0.3f, 0.7f);
                        p->fx = rnd(g, -0.5f, 0.5f);
                        p->fy = rnd(g, -0.5f, 0.5f);
                        p->r = p->g = p->b = (float)rand_i(g, 0, 150);
                    }
                    break;
                case 3: /* Steam */
                    p = create(g, 3, px, py);
                    p->sx = rnd(g, 0.4f, 0.7f);
                    p->a  = rnd(g, 0.5f, 1.0f);
                    p->fx = rnd(g, -0.3f, 0.3f);
                    p->fy = rnd(g, -0.3f, 0.3f);
                    p->r = p->g = p->b = (float)rand_i(g, 190, 255);
                    break;
                default:
                    break;
            }
            break;
        }
        default:
            break;
    }
}

/* Tp.draw (0x140263cc0): one frame of movement; returns false when the particle dies */
static bool move(cs2d_gen_t *g, cs2d_gen_particle_t *p, const cs2d_map_t *map, float cam_x, float cam_y,
                 float half_w, float half_h)
{
    switch (p->typ) {
        case 1: /* LAB_140263f49 */
            p->sx += 0.01f;
            p->a  -= 0.01f;
            return p->a >= 0.0f;
        case 3: /* Wind (_DAT_140ddf028 / 024) is not added: Info_Storm is not implemented */
            p->x   += p->fx;
            p->y   += p->fy;
            p->sx  += 0.001f;
            p->a   -= p->sy;
            p->rot += p->rots;
            return p->a >= 0.0f;
        case 12:
            if (p->fx != 0.0f && p->fy != 0.0f) {
                p->x += p->fx;
                p->y += p->fy;
                int c = prop_class(map, p->x, p->y);
                if (c == 0) { p->fx = 0.0f; p->fy = 0.0f; }  /* Stops in a wall */
                else if (c == -1) p->a *= 0.5f;             /* Fades fast over deadly tiles */
            }
            p->fx -= p->fx / 15.0f;
            p->fy -= p->fy / 15.0f;
            p->a  -= p->rots;
            /* Level 5: once it has (almost) stopped it moves to list 2 under the players */
            if (p->layer == CS2D_GEN_LAYER_TOP) {
                if (fabsf(p->fx) < 0.3f && fabsf(p->fy) < 0.3f) p->layer = CS2D_GEN_LAYER_FLOOR;
                return true;
            }
            return p->a >= 0.0f;
        case 14:
            p->a   -= 0.02f;
            p->rot -= 5.0f;
            p->x   += p->fx;
            p->y   += p->fy;
            p->sx  += 0.001f;
            return p->a >= 0.0f;
        case 17: /* Per frame, without g_dt */
            p->x += p->fx;
            p->y += p->fy;
            p->a -= 0.01f;
            return p->a >= 0.0f;
        case 19: /* Snow: falls towards the screen while drifting along its heading */
            p->sy -= 0.01f * p->sy;
            p->x  += sinf(p->fx * 0.017453292519943295f);
            p->y  -= cosf(p->fx * 0.017453292519943295f);
            p->fx  = fmodf(p->fy + p->fx, 360.0f);
            if (p->sy < 10.0f) p->a -= 0.2f;
            return p->a >= 0.0f && p->sy >= 0.0f;
        case 20: /* Rain */
            p->sy -= 0.05f * p->sy;
            if (p->sy < 30.0f) p->a -= 0.1f;
            if (p->a < 0.0f || p->sy < 0.0f) {
                /* 1 in 3 drops splashes when it lands on water (particles setting > 3) */
                if (rand_i(g, 3, 1) == 1 && g->count < CS2D_GEN_MAX) {
                    float lx = cam_x + half_w + p->x * p->sy * (1.0f / 30.0f);
                    float ly = cam_y + half_h + p->y * p->sy * (1.0f / 30.0f);
                    int tx = (int)(lx * 0.03125f), ty = (int)(ly * 0.03125f);
                    if (map && lx >= 0.0f && ly >= 0.0f && tx < (int)map->width && ty < (int)map->height &&
                        cs2d_map_get_tile_mode(map, (uint32_t)tx, (uint32_t)ty) == CS2D_TILE_FLOOR_WADE) {
                        cs2d_gen_particle_t *w = create(g, 1, lx, ly); /* Appended: never drops (count checked) */
                        w->a  = rnd(g, 0.3f, 0.4f);
                        w->sx = rnd(g, 0.0f, 0.1f);
                    }
                }
                return false;
            }
            return true;
        case 21: /* Fog: fades in to 0.7, then out */
            p->x   += p->fx;
            p->y   += p->fy;
            p->sx  += 0.001f;
            p->rot += p->rots;
            if (p->age != 0.0f) {
                p->a -= 0.002f;
                return p->a >= 0.0f;
            }
            p->a += 0.01f;
            if (p->a > 0.7f) p->age = 1.0f;
            return true;
        default:
            return false;
    }
}

void cs2d_gen_step(cs2d_gen_t *g, const cs2d_map_t *map, float cam_x, float cam_y, float half_w, float half_h)
{
    if (!g || !map) return;
    g->time_ms += 17; /* 1000 / 60, rounded like a 60 Hz millisecs() */
    for (int k = 0; k < g->ent_count; k++) {
        if (map->entity_state[g->ents[k]] != 0) continue;
        spawn(g, map, k, cam_x, cam_y, half_w, half_h);
    }

    /* Removal compacts in place; a rain splash appends behind the cursor (and is moved next frame) */
    int keep = 0, n = g->count;
    for (int i = 0; i < n; i++) {
        cs2d_gen_particle_t cur = g->p[i];
        if (move(g, &cur, map, cam_x, cam_y, half_w, half_h)) g->p[keep++] = cur;
    }
    if (g->count > n) {
        memmove(&g->p[keep], &g->p[n], sizeof(g->p[0]) * (size_t)(g->count - n));
        keep += g->count - n;
    }
    g->count = keep;

    g->flash -= 0.005f; /* FUN_140131710: flash -= 0.005 * g_dt */
    if (g->flash < 0.0f) g->flash = 0.0f;
}

void cs2d_gen_update(cs2d_gen_t *g, const cs2d_map_t *map, float cam_x, float cam_y,
                     float half_w, float half_h, float dt)
{
    if (!g || !map) return;
    g->step_acc += dt;
    if (g->step_acc > 0.25f) g->step_acc = 0.25f; /* After a stall, catch up at most 15 frames */
    while (g->step_acc >= 1.0f / 60.0f) {
        g->step_acc -= 1.0f / 60.0f;
        cs2d_gen_step(g, map, cam_x, cam_y, half_w, half_h);
    }
}
