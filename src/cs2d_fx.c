#include "cs2d_fx.h"
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

void cs2d_fx_explosion(cs2d_fx_t *fx, float x, float y, float range)
{
    if (!fx) return;
    cs2d_fx_particle_t *p = alloc_particle(fx);
    p->kind     = CS2D_FX_EXPLOSION;
    p->x        = x;
    p->y        = y;
    p->max_life = p->life = 0.6f;
    p->size     = range * 2.0f > 64.0f ? range * 2.0f : 64.0f;
    p->rot      = frand(fx) * 360.0f;
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

void cs2d_fx_update(cs2d_fx_t *fx, float dt)
{
    if (!fx) return;
    int keep = 0;
    for (int i = 0; i < fx->count; i++) {
        cs2d_fx_particle_t *p = &fx->p[i];
        p->life -= dt;
        if (p->life <= 0.0f) continue;
        p->x   += p->vx * dt;
        p->y   += p->vy * dt;
        p->rot += p->vrot * dt;
        float drag = 1.0f - 2.5f * dt; /* Debris slows down on the floor */
        if (drag < 0.0f) drag = 0.0f;
        p->vx *= drag;
        p->vy *= drag;
        fx->p[keep++] = *p;
    }
    fx->count = keep;
}
