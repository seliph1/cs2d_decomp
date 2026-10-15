#include "cs2d_fx.h"
#include "cs2d_sound.h"
#include "cs2d_light.h"
#include <math.h>
#include <string.h>

/* Fragment frames (first of 4 variants) and tint per Env_Breakable material */
static const struct { int first; float r, g, b; } k_material[6] = {
    {  0, 0.55f, 0.38f, 0.20f }, /* 0 wood: splinters */
    {  4, 0.60f, 0.60f, 0.60f }, /* 1 stone: chips */
    {  4, 0.80f, 0.82f, 0.86f }, /* 2 metal: chips */
    { 12, 0.60f, 0.02f, 0.02f }, /* 3 flesh: drops */
    {  4, 0.78f, 0.90f, 1.00f }, /* 4 glass: shards */
    { -1, 0.0f,  0.0f,  0.0f  }  /* 5 nothing */
};

static float frand(cs2d_fx_t *fx)
{
    fx->seed = fx->seed * 1664525u + 1013904223u;
    return (float)(fx->seed >> 8) / 16777216.0f;
}

static cs2d_fx_particle_t *alloc_particle(cs2d_fx_t *fx)
{
    if (fx->count >= CS2D_FX_MAX) {
        /* Drop the oldest */
        memmove(&fx->p[0], &fx->p[1], sizeof(fx->p[0]) * (CS2D_FX_MAX - 1));
        fx->count--;
    }
    cs2d_fx_particle_t *p = &fx->p[fx->count++];
    memset(p, 0, sizeof(*p));
    p->r = p->g = p->b = 1.0f;
    return p;
}

void cs2d_fx_clear(cs2d_fx_t *fx)
{
    if (!fx) return;
    uint32_t seed = fx->seed ? fx->seed : 0x2d2d2d2du;
    memset(fx, 0, sizeof(*fx));
    fx->seed = seed;
}

static void spawn_fragments(cs2d_fx_t *fx, float x, float y, int material, int count,
                            float dir_deg, float cone_deg, float speed_min, float speed_max)
{
    if (material < 0 || material > 5 || k_material[material].first < 0) return;
    for (int i = 0; i < count; i++) {
        cs2d_fx_particle_t *p = alloc_particle(fx);
        float a = (dir_deg + (frand(fx) - 0.5f) * cone_deg) * 0.017453292519943295f;
        float v = speed_min + frand(fx) * (speed_max - speed_min);
        p->kind     = CS2D_FX_FRAGMENT;
        p->x        = x;
        p->y        = y;
        p->vx       = sinf(a) * v;
        p->vy       = -cosf(a) * v;
        p->rot      = frand(fx) * 360.0f;
        p->vrot     = (frand(fx) - 0.5f) * 720.0f;
        p->max_life = p->life = 0.35f + frand(fx) * 0.45f;
        p->size     = 32.0f;
        p->frame    = k_material[material].first + (int)(frand(fx) * 4.0f) % 4;
        p->r        = k_material[material].r;
        p->g        = k_material[material].g;
        p->b        = k_material[material].b;
    }
}

void cs2d_fx_breakable_hit(cs2d_fx_t *fx, float x, float y, int material, float angle_deg)
{
    if (!fx) return;
    spawn_fragments(fx, x, y, material, 3, angle_deg + 180.0f, 120.0f, 40.0f, 110.0f);
}

/*
 * Tp.breakable (0x1401197c0) with material 1: two bursts of 2 x particles (sys/config.cfg "particles 4",
 * DAT_140ddf182) Tp.matp (0x14004c710) fragments, one back along angle + 180, one along the shot.
 * Each fragment (Tp type 12, Tp.Create 0x14004ac20, Tp.draw 0x140263cc0):
 *   position: impact + Rnd(-5, 5) (breakable) + Rnd(-5, 5) (matp) on each axis;
 *   frame Rand(4, 7), gray Rand(50, 150), rotation Rand(1, 360), scale Rnd(0.4, 0.7), alpha blend;
 *   speed Rnd(0.2, 3) px per frame exactly along the direction, minus speed / 15 per frame;
 *   alpha Rnd(0.7, 1.2) minus 0.01 per frame.
 * Not reproduced: a fragment stops when it enters a wall, and a stopped one (layer 5) moves to another Tp list.
 */
#define BUILDING_HIT_PARTICLES 4

void cs2d_fx_building_hit(cs2d_fx_t *fx, float x, float y, float angle_deg)
{
    if (!fx) return;
    for (int burst = 0; burst < 2; burst++) {
        float a = (angle_deg + (burst == 0 ? 180.0f : 0.0f)) * 0.017453292519943295f;
        for (int i = 0; i < 2 * BUILDING_HIT_PARTICLES; i++) {
            cs2d_fx_particle_t *p = alloc_particle(fx);
            float v = (0.2f + frand(fx) * 2.8f) * 60.0f;
            float gray = (float)(50 + (int)(frand(fx) * 101.0f)) / 255.0f;
            p->kind     = CS2D_FX_FRAGMENT;
            p->x        = x + (frand(fx) - 0.5f) * 10.0f + (frand(fx) - 0.5f) * 10.0f;
            p->y        = y + (frand(fx) - 0.5f) * 10.0f + (frand(fx) - 0.5f) * 10.0f;
            p->vx       = sinf(a) * v;
            p->vy       = -cosf(a) * v;
            p->drag     = 4.0f;                 /* 1 / 15 per 60 Hz frame */
            p->rot      = (float)(1 + (int)(frand(fx) * 360.0f));
            p->max_life = 1.0f / 0.6f;          /* life / max_life = alpha, 0.6 per second */
            p->life     = (0.7f + frand(fx) * 0.5f) * p->max_life;
            p->size     = 32.0f * (0.4f + frand(fx) * 0.3f);
            p->frame    = 4 + (int)(frand(fx) * 4.0f) % 4;
            p->r = p->g = p->b = gray;
        }
    }
}

void cs2d_fx_explosion(cs2d_fx_t *fx, float x, float y, float range)
{
    if (!fx) return;
    cs2d_sound_play_rand_at("sfx/weapons/explode", 6, ".wav", x, y); /* Tp.explosion (0x14011a500): array 0x140a0d650 */
    cs2d_fx_particle_t *p = alloc_particle(fx);
    p->kind     = CS2D_FX_EXPLOSION;
    p->x        = x;
    p->y        = y;
    p->max_life = p->life = 0.6f;
    p->size     = range * 2.0f > 64.0f ? range * 2.0f : 64.0f;
    p->rot      = frand(fx) * 360.0f;
    /* Tp.explosion (particles > 3, light on): Tp.CreateLight(x, y, 255, Rand(150, 255), 0 -> 35, 180, 5), an orange light
     * of radius 180 shrinking 5 per frame. (The original skips it once explosion_count reaches 11: not tracked) */
    cs2d_light_flash((int)x, (int)y, 255, 150 + (int)(frand(fx) * 106.0f), 35, 180.0f, 5.0f, 1.0f);
}

void cs2d_fx_breakable_break(cs2d_fx_t *fx, const cs2d_entity_t *e)
{
    if (!fx || !e) return;
    float cx = (float)e->x * CS2D_TILE_SIZE + 16.0f;
    float cy = (float)e->y * CS2D_TILE_SIZE + 16.0f;

    /* The four quarters of the tile image drift diagonally away from the center, growing and fading
     * (they keep their orientation) */
    for (int q = 0; q < 4; q++) {
        float ox = (q & 1) ? 8.0f : -8.0f;
        float oy = (q & 2) ? 8.0f : -8.0f;
        cs2d_fx_particle_t *p = alloc_particle(fx);
        float v = 18.0f + frand(fx) * 10.0f;
        p->kind     = CS2D_FX_TILE_PART;
        p->x        = cx + ox;
        p->y        = cy + oy;
        p->vx       = ox / 8.0f * v;
        p->vy       = oy / 8.0f * v;
        p->max_life = p->life = 0.45f + frand(fx) * 0.15f;
        p->size     = 16.0f;
        p->frame    = e->int_settings[0];
        p->quarter  = q;
    }
    spawn_fragments(fx, cx, cy, e->int_settings[2], 8, 0.0f, 360.0f, 40.0f, 140.0f);
    if (e->int_settings[3]) cs2d_fx_explosion(fx, cx, cy, (float)e->int_settings[4]);
}

void cs2d_fx_tracer(cs2d_fx_t *fx, float mx, float my, float angle_deg, float ex, float ey, bool hit_wall)
{
    /* Tracer from the muzzle to the impact, then glowing dots left along the path */
    if (!fx) return;
    float dx = ex - mx, dy = ey - my;
    float len = sqrtf(dx * dx + dy * dy);
    if (len > 1.0f) {
        cs2d_fx_particle_t *t = alloc_particle(fx);
        t->kind     = CS2D_FX_TRACER;
        t->x        = mx;
        t->y        = my;
        t->x2       = ex;
        t->y2       = ey;
        t->max_life = t->life = 0.08f;
        int dots = (int)(len / 28.0f);
        for (int i = 1; i <= dots && i < 24; i++) {
            float k = ((float)i - frand(fx)) / (float)dots;
            cs2d_fx_particle_t *d = alloc_particle(fx);
            d->kind     = CS2D_FX_GLOWDOT;
            d->x        = mx + dx * k;
            d->y        = my + dy * k;
            d->max_life = d->life = 0.15f + frand(fx) * 0.25f;
            d->size     = 2.0f;
            d->r = 1.0f; d->g = 0.9f; d->b = 0.3f;
        }
    }

    /* Sparks bouncing off a wall */
    if (hit_wall) {
        for (int i = 0; i < 4; i++) {
            float a = (angle_deg + 180.0f + (frand(fx) - 0.5f) * 140.0f) * 0.017453292519943295f;
            float v = 60.0f + frand(fx) * 120.0f;
            cs2d_fx_particle_t *sp = alloc_particle(fx);
            sp->kind     = CS2D_FX_GLOWDOT;
            sp->x        = ex;
            sp->y        = ey;
            sp->vx       = sinf(a) * v;
            sp->vy       = -cosf(a) * v;
            sp->max_life = sp->life = 0.12f + frand(fx) * 0.15f;
            sp->size     = 2.0f;
            sp->r = 1.0f; sp->g = 0.85f; sp->b = 0.25f;
        }
    }
}

void cs2d_fx_shot(cs2d_fx_t *fx, float px, float py, float angle_deg, float muzzle_dist,
                  float ex, float ey, bool hit_wall, int shell_frame)
{
    if (!fx) return;
    float rad = angle_deg * 0.017453292519943295f;
    float fx_ = sinf(rad), fy = -cosf(rad);   /* Forward */
    float rx = cosf(rad), ry = sinf(rad);     /* Right of the aim (weapons are held on the right) */
    float mx = px + fx_ * muzzle_dist, my = py + fy * muzzle_dist;

    /* Muzzle flash: life 1 -> 0 at 0.4 per frame (24/s) */
    cs2d_fx_particle_t *m = alloc_particle(fx);
    m->kind     = CS2D_FX_MUZZLE;
    m->x        = mx;
    m->y        = my;
    m->rot      = angle_deg;
    m->max_life = m->life = 1.0f / 24.0f * 2.5f;
    m->size     = 16.0f;                      /* 32 px * 0.1 * 5 */
    m->x2       = px;                         /* Light around the shooter */
    m->y2       = py;

    cs2d_fx_tracer(fx, mx, my, angle_deg, ex, ey, hit_wall);

    /* Case ejected to the right (slightly backwards), spinning until it lands */
    if (shell_frame >= 0) {
        cs2d_fx_particle_t *c = &fx->shells[fx->shell_next];
        fx->shell_next = (fx->shell_next + 1) % CS2D_FX_MAX_SHELLS;
        memset(c, 0, sizeof(*c));
        float v = 45.0f + frand(fx) * 25.0f;
        float back = -15.0f + frand(fx) * 10.0f;
        c->kind     = CS2D_FX_SHELL;
        c->x        = px + rx * 6.0f + fx_ * 8.0f;
        c->y        = py + ry * 6.0f + fy * 8.0f;
        c->vx       = rx * v + fx_ * back;
        c->vy       = ry * v + fy * back;
        c->rot      = angle_deg + frand(fx) * 360.0f;
        c->vrot     = 720.0f + frand(fx) * 360.0f;
        c->max_life = c->life = 0.25f + frand(fx) * 0.1f; /* Flight time; then it rests */
        c->size     = 8.0f;
        c->frame    = shell_frame;
        c->r = c->g = c->b = 1.0f;
    }
}

/* Tshoot.Shoot laser colours by mode: 0 green, 1 red, 2 yellow, 3 blue (Rand ranges / 255) */
static void laser_colour(cs2d_fx_t *fx, int mode, float *r, float *g, float *b)
{
    switch (mode) {
        case 1:  *r = (100.0f + frand(fx) * 155.0f) / 255.0f; *g = frand(fx) * 50.0f / 255.0f;            *b = 0.0f; break;
        case 2:  *r = (100.0f + frand(fx) * 155.0f) / 255.0f; *g = (100.0f + frand(fx) * 155.0f) / 255.0f; *b = 0.0f; break;
        case 3:  *r = 0.0f; *g = (25.0f + frand(fx) * 50.0f) / 255.0f; *b = (100.0f + frand(fx) * 155.0f) / 255.0f; break;
        default: *r = frand(fx) * 50.0f / 255.0f; *g = (100.0f + frand(fx) * 155.0f) / 255.0f;            *b = 0.0f; break;
    }
}

void cs2d_fx_laser(cs2d_fx_t *fx, float mx, float my, float ex, float ey, int mode)
{
    if (!fx) return;
    cs2d_fx_particle_t *l = alloc_particle(fx);
    l->kind = CS2D_FX_LASER;
    l->x = mx; l->y = my; l->x2 = ex; l->y2 = ey;
    laser_colour(fx, mode, &l->r, &l->g, &l->b);
    l->size     = 0.4f + frand(fx) * 0.2f;           /* Start alpha */
    l->max_life = l->life = l->size / 0.03f / 60.0f; /* -0.03 per 60 Hz frame */

    float dx = ex - mx, dy = ey - my;
    float len = sqrtf(dx * dx + dy * dy);
    int glows = (int)(len / 100.0f) + 1;             /* Rand(len / 3, 1) == 1 per px: about 3 per 100 px */
    for (int i = 0; i < glows * 3 && len > 1.0f; i++) {
        float k = frand(fx);
        cs2d_fx_particle_t *p = alloc_particle(fx);
        p->kind = CS2D_FX_FIRE;
        p->x = mx + dx * k; p->y = my + dy * k;
        p->vx = (frand(fx) - 0.5f) * 0.4f * 60.0f;
        p->vy = (frand(fx) - 0.5f) * 0.4f * 60.0f;
        p->rot = frand(fx) * 360.0f;
        p->max_life = p->life = 0.3f + frand(fx) * 0.3f;
        p->size = 32.0f * (0.05f + frand(fx) * 0.15f);
        laser_colour(fx, mode, &p->r, &p->g, &p->b);
    }
    float r, g, b;
    laser_colour(fx, mode, &r, &g, &b);
    for (int i = 0; i < 3; i++) cs2d_fx_sparkle(fx, ex, ey, r, g, b);
}

void cs2d_fx_flame(cs2d_fx_t *fx, float px, float py, float angle_deg, float dist)
{
    if (!fx) return;
    float rad = angle_deg * 0.017453292519943295f;
    float sx = sinf(rad), sy = -cosf(rad);
    for (int d = 20; d <= (int)dist; d += 5) {
        cs2d_fx_particle_t *p = alloc_particle(fx);
        float a = (angle_deg + (frand(fx) - 0.5f) * 100.0f) * 0.017453292519943295f;
        p->kind = CS2D_FX_FIRE;
        p->x = px + sx * (float)d + (frand(fx) - 0.5f) * 6.0f;
        p->y = py + sy * (float)d + (frand(fx) - 0.5f) * 6.0f;
        p->vx = sinf(a) * 60.0f;
        p->vy = -cosf(a) * 60.0f;
        p->rot = frand(fx) * 360.0f;
        p->vrot = (frand(fx) - 0.5f) * 180.0f;
        p->max_life = p->life = 0.35f + frand(fx) * 0.25f;
        if (d < 30) {        /* Blue */
            p->r = frand(fx) * 100.0f / 255.0f; p->g = frand(fx) * 60.0f / 255.0f; p->b = (200.0f + frand(fx) * 55.0f) / 255.0f;
            p->size = 32.0f * frand(fx) * 0.05f + 4.0f;
        } else if (d < 60) { /* Purple */
            p->r = (150.0f + frand(fx) * 105.0f) / 255.0f; p->g = frand(fx) * 100.0f / 255.0f; p->b = (50.0f + frand(fx) * 100.0f) / 255.0f;
            p->size = 32.0f * frand(fx) * 0.1f + 6.0f;
        } else {             /* Orange */
            p->r = (200.0f + frand(fx) * 55.0f) / 255.0f; p->g = (50.0f + frand(fx) * 100.0f) / 255.0f; p->b = frand(fx) * 70.0f / 255.0f;
            p->size = 32.0f * frand(fx) * 0.2f + 8.0f;
        }
    }
}

void cs2d_fx_portal_glow(cs2d_fx_t *fx, float x, float y, float sx, int mode)
{
    if (!fx) return;
    cs2d_fx_particle_t *p = alloc_particle(fx);
    p->kind = CS2D_FX_SPARKLE;
    p->x = x;
    p->y = y;
    p->max_life = p->life = 0.3f;
    p->size = 32.0f * sx; /* flare at scale sx */
    if (mode == 2) { p->r = 50.0f / 255.0f; p->g = 100.0f / 255.0f; p->b = 1.0f; }
    else           { p->r = 200.0f / 255.0f; p->g = 100.0f / 255.0f; p->b = 0.0f; }
}

void cs2d_fx_portal_flash(cs2d_fx_t *fx, float px, float py, float angle_deg, int mode)
{
    float rad = angle_deg * 0.017453292519943295f;
    cs2d_fx_portal_glow(fx, px + sinf(rad) * 24.0f, py - cosf(rad) * 24.0f, 0.5f, mode);
}

void cs2d_fx_knifeslash(cs2d_fx_t *fx, float px, float py, float angle_deg)
{
    if (!fx) return;
    cs2d_fx_particle_t *p = alloc_particle(fx);
    p->kind     = CS2D_FX_KNIFESLASH;
    p->x        = px;
    p->y        = py;
    p->rot      = angle_deg;
    p->max_life = p->life = 1.0f / (0.07f * 60.0f); /* 0.07 per 60 Hz frame (Tp.create 0x14004b518) */
    p->size     = 32.0f;
}

void cs2d_fx_smoke_puff(cs2d_fx_t *fx, float x, float y, float r, float g, float b, float size, float speed)
{
    if (!fx) return;
    cs2d_fx_particle_t *p = alloc_particle(fx);
    float a = frand(fx) * 6.2831853f;
    float v = frand(fx) * speed;
    p->kind     = CS2D_FX_SMOKE;
    p->x        = x + (frand(fx) - 0.5f) * 8.0f;
    p->y        = y + (frand(fx) - 0.5f) * 8.0f;
    p->vx       = cosf(a) * v;
    p->vy       = sinf(a) * v;
    p->rot      = frand(fx) * 360.0f;
    p->vrot     = (frand(fx) - 0.5f) * 60.0f;
    p->max_life = p->life = 1.2f + frand(fx) * 0.8f;
    p->size     = size > 0.0f ? size : (16.0f + frand(fx) * 12.0f);
    p->r        = r;
    p->g        = g;
    p->b        = b;
    p->drag     = 1.5f;
}

void cs2d_fx_fire_puff(cs2d_fx_t *fx, float x, float y, float size)
{
    if (!fx) return;
    cs2d_fx_particle_t *p = alloc_particle(fx);
    float a = frand(fx) * 6.2831853f;
    float v = 15.0f + frand(fx) * 35.0f;
    p->kind     = CS2D_FX_FIRE;
    p->x        = x + (frand(fx) - 0.5f) * 12.0f;
    p->y        = y + (frand(fx) - 0.5f) * 12.0f;
    p->vx       = cosf(a) * v;
    p->vy       = sinf(a) * v - (20.0f + frand(fx) * 20.0f); /* Flames drift slightly upward */
    p->rot      = frand(fx) * 360.0f;
    p->vrot     = (frand(fx) - 0.5f) * 180.0f;
    p->max_life = p->life = 0.5f + frand(fx) * 0.4f;
    p->size     = size > 0.0f ? size : (14.0f + frand(fx) * 10.0f);
    p->r        = 1.0f;
    p->g        = 0.4f + frand(fx) * 0.3f;
    p->b        = 0.05f;
    p->drag     = 2.0f;
}

void cs2d_fx_sparkle(cs2d_fx_t *fx, float x, float y, float r, float g, float b)
{
    if (!fx) return;
    cs2d_fx_particle_t *p = alloc_particle(fx);
    float a = frand(fx) * 6.2831853f;
    float v = 30.0f + frand(fx) * 70.0f;
    p->kind     = CS2D_FX_SPARKLE;
    p->x        = x + (frand(fx) - 0.5f) * 4.0f;
    p->y        = y + (frand(fx) - 0.5f) * 4.0f;
    p->vx       = cosf(a) * v;
    p->vy       = sinf(a) * v;
    p->rot      = frand(fx) * 360.0f;
    p->vrot     = (frand(fx) - 0.5f) * 360.0f;
    p->max_life = p->life = 0.25f + frand(fx) * 0.35f;
    p->size     = 4.0f + frand(fx) * 4.0f;
    p->r        = r;
    p->g        = g;
    p->b        = b;
    p->drag     = 3.0f;
}

void cs2d_fx_blood_splat(cs2d_fx_t *fx, float x, float y, int count)
{
    if (!fx || count <= 0) return;
    for (int i = 0; i < count; i++) {
        cs2d_fx_particle_t *p = alloc_particle(fx);
        float a = frand(fx) * 6.2831853f;
        float v = 20.0f + frand(fx) * 100.0f;
        p->kind     = CS2D_FX_BLOOD;
        p->x        = x + (frand(fx) - 0.5f) * 6.0f;
        p->y        = y + (frand(fx) - 0.5f) * 6.0f;
        p->vx       = cosf(a) * v;
        p->vy       = sinf(a) * v;
        p->rot      = frand(fx) * 360.0f;
        p->vrot     = (frand(fx) - 0.5f) * 360.0f;
        p->max_life = p->life = 0.6f + frand(fx) * 0.8f;
        p->size     = 6.0f + frand(fx) * 8.0f;
        p->frame    = 12 + (int)(frand(fx) * 4.0f) % 4; /* Flesh fragments / blood drops */
        p->r        = 0.65f + frand(fx) * 0.2f;
        p->g        = 0.02f;
        p->b        = 0.02f;
        p->drag     = 4.0f;
    }
}

void cs2d_fx_snow_puff(cs2d_fx_t *fx, float x, float y, int count)
{
    if (!fx || count <= 0) return;
    for (int i = 0; i < count; i++) {
        cs2d_fx_particle_t *p = alloc_particle(fx);
        float a = frand(fx) * 6.2831853f;
        float v = 15.0f + frand(fx) * 60.0f;
        p->kind     = CS2D_FX_SNOW;
        p->x        = x + (frand(fx) - 0.5f) * 8.0f;
        p->y        = y + (frand(fx) - 0.5f) * 8.0f;
        p->vx       = cosf(a) * v;
        p->vy       = sinf(a) * v;
        p->rot      = frand(fx) * 360.0f;
        p->vrot     = (frand(fx) - 0.5f) * 180.0f;
        p->max_life = p->life = 0.5f + frand(fx) * 0.6f;
        p->size     = 5.0f + frand(fx) * 7.0f;
        p->r        = 0.95f;
        p->g        = 0.95f;
        p->b        = 1.0f;
        p->drag     = 3.0f;
    }
}

void cs2d_fx_update(cs2d_fx_t *fx, float dt)
{
    if (!fx) return;
    for (int i = 0; i < CS2D_FX_MAX_SHELLS; i++) {
        cs2d_fx_particle_t *c = &fx->shells[i];
        if (c->kind != CS2D_FX_SHELL || c->life <= 0.0f) continue;
        c->life -= dt;
        c->x   += c->vx * dt;
        c->y   += c->vy * dt;
        c->rot += c->vrot * dt;
        if (c->life <= 0.0f) c->life = 0.0f; /* Landed: stays where it is */
    }
    if (!fx) return;
    int keep = 0;
    for (int i = 0; i < fx->count; i++) {
        cs2d_fx_particle_t *p = &fx->p[i];
        p->life -= dt;
        if (p->life <= 0.0f) continue;
        p->x   += p->vx * dt;
        p->y   += p->vy * dt;
        p->rot += p->vrot * dt;
        float drag = 1.0f - (p->drag > 0.0f ? p->drag : 2.5f) * dt; /* Debris slows down on the floor */
        if (drag < 0.0f) drag = 0.0f;
        p->vx *= drag;
        p->vy *= drag;
        fx->p[keep++] = *p;
    }
    fx->count = keep;
}
