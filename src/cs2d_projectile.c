#include "cs2d_projectile.h"
#include "cs2d_client.h"
#include "cs2d_weapons.h"
#include "cs2d_fx.h"
#include "cs2d_gen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define PI 3.14159265358979323846f
#define DEG2RAD (PI / 180.0f)
#define RAD2DEG (180.0f / PI)

void cs2d_projectile_init(cs2d_projectile_world_t *pw)
{
    if (!pw) return;
    memset(pw, 0, sizeof(*pw));
    pw->next_id = 1;
    pw->next_field_id = 1;
}

void cs2d_projectile_clear(cs2d_projectile_world_t *pw)
{
    if (!pw) return;
    for (int i = 0; i < CS2D_MAX_PROJECTILES; i++) {
        pw->projectiles[i].active = false;
    }
    for (int i = 0; i < CS2D_MAX_FIELDS; i++) {
        pw->fields[i].active = false;
    }
    pw->next_id = 1;
    pw->next_field_id = 1;
}

bool cs2d_projectile_is_throwable(uint8_t item_id)
{
    switch (item_id) {
        case CS2D_WEP_HE:
        case CS2D_WEP_FLASHBANG:
        case CS2D_WEP_SMOKE:
        case CS2D_WEP_FLARE:
        case CS2D_WEP_GAS:
        case CS2D_WEP_MOLOTOV:
        case CS2D_WEP_SNOWBALL:
        case CS2D_WEP_AIRSTRIKE:
        case CS2D_WEP_GUTBOMB:
            return true;
        default:
            return false;
    }
}

bool cs2d_projectile_is_rocket(uint8_t item_id)
{
    return (item_id == CS2D_WEP_ROCKETLAUNCHER || item_id == CS2D_WEP_RPG7);
}

bool cs2d_projectile_is_impact(uint8_t item_id)
{
    switch (item_id) {
        case CS2D_WEP_ROCKETLAUNCHER:
        case CS2D_WEP_RPG7:
        case CS2D_WEP_GRENADELAUNCHER:
        case CS2D_WEP_MOLOTOV:
        case CS2D_WEP_SNOWBALL:
        case CS2D_WEP_GUTBOMB:
            return true;
        default:
            return false;
    }
}

void cs2d_projectile_get_default_properties(uint8_t type, float *out_speed, float *out_lifetime, float *out_flydist)
{
    float speed = 350.0f;
    float lifetime = 1.8f;
    float flydist = 385.0f;

    switch (type) {
        case CS2D_WEP_RPG7:
            speed = 750.0f;
            lifetime = 10.0f;
            flydist = 10000.0f;
            break;
        case CS2D_WEP_ROCKETLAUNCHER:
            speed = 700.0f;
            lifetime = 10.0f;
            flydist = 10000.0f;
            break;
        case CS2D_WEP_GRENADELAUNCHER:
            speed = 550.0f;
            lifetime = 5.0f;
            flydist = 600.0f;
            break;
        case CS2D_WEP_HE:
        case CS2D_WEP_FLASHBANG:
        case CS2D_WEP_SMOKE:
        case CS2D_WEP_GAS:
            speed = 350.0f;
            lifetime = 1.8f; /* 1800 ms in CS2D */
            flydist = 385.0f;
            break;
        case CS2D_WEP_FLARE:
            speed = 350.0f;
            lifetime = 60.0f; /* 60s ground burning */
            flydist = 385.0f;
            break;
        case CS2D_WEP_MOLOTOV:
            speed = 320.0f;
            lifetime = 10.0f;
            flydist = 385.0f;
            break;
        case CS2D_WEP_SNOWBALL:
            speed = 450.0f;
            lifetime = 5.0f;
            flydist = 385.0f;
            break;
        case CS2D_WEP_AIRSTRIKE:
            speed = 350.0f;
            lifetime = 7.0f; /* 7s countdown on the ground */
            flydist = 385.0f;
            break;
        case CS2D_WEP_GUTBOMB:
            speed = 320.0f;
            lifetime = 10.0f;
            flydist = 385.0f;
            break;
        default:
            speed = 300.0f;
            lifetime = 10.0f;
            flydist = 385.0f;
            break;
    }

    if (out_speed) *out_speed = speed;
    if (out_lifetime) *out_lifetime = lifetime;
    if (out_flydist) *out_flydist = flydist;
}

cs2d_projectile_t *cs2d_projectile_spawn_with_id(
    cs2d_projectile_world_t *pw,
    uint8_t id,
    uint8_t player,
    uint8_t type,
    float x, float y,
    float dir,
    float flydist,
    float speed,
    float lifetime
)
{
    if (!pw) return NULL;

    /* Check if already exists for this player & id */
    cs2d_projectile_t *p = NULL;
    for (int i = 0; i < CS2D_MAX_PROJECTILES; i++) {
        if (pw->projectiles[i].active && pw->projectiles[i].id == id && pw->projectiles[i].player == player) {
            p = &pw->projectiles[i];
            break;
        }
    }

    /* Otherwise find an inactive slot */
    if (!p) {
        for (int i = 0; i < CS2D_MAX_PROJECTILES; i++) {
            if (!pw->projectiles[i].active) {
                p = &pw->projectiles[i];
                break;
            }
        }
    }
    if (!p) return NULL;

    p->active = true;
    p->id = id;
    p->player = player;
    p->type = type;
    p->x = x;
    p->y = y;
    p->flydist = flydist;
    p->start_flydist = flydist > 0.0f ? flydist : 1.0f;
    p->dir = dir;
    p->speed = speed > 0.0f ? speed : 350.0f;
    p->lifetime = lifetime > 0.0f ? lifetime : 1.8f;
    p->max_lifetime = p->lifetime;
    p->rot = dir;
    p->rot_speed = cs2d_projectile_is_rocket(type) ? 0.0f : 360.0f;
    p->state = (flydist > 0.0f) ? CS2D_PROJ_STATE_FLYING : CS2D_PROJ_STATE_GROUND;
    p->effect_timer = 0.0f;

    return p;
}

cs2d_projectile_t *cs2d_projectile_spawn(
    cs2d_projectile_world_t *pw,
    uint8_t player,
    uint8_t type,
    float x, float y,
    float dir,
    float flydist,
    float speed,
    float lifetime
)
{
    if (!pw) return NULL;
    uint8_t id = pw->next_id++;
    if (pw->next_id == 0) pw->next_id = 1;

    return cs2d_projectile_spawn_with_id(pw, id, player, type, x, y, dir, flydist, speed, lifetime);
}

cs2d_projectile_t *cs2d_projectile_get(cs2d_projectile_world_t *pw, uint8_t id, uint8_t player)
{
    if (!pw) return NULL;
    for (int i = 0; i < CS2D_MAX_PROJECTILES; i++) {
        if (pw->projectiles[i].active && pw->projectiles[i].id == id && pw->projectiles[i].player == player) {
            return &pw->projectiles[i];
        }
    }
    return NULL;
}

void cs2d_projectile_kill(cs2d_projectile_world_t *pw, uint8_t id, uint8_t player)
{
    if (!pw) return;
    cs2d_projectile_t *p = cs2d_projectile_get(pw, id, player);
    if (p) p->active = false;
}

cs2d_field_t *cs2d_field_spawn(
    cs2d_projectile_world_t *pw,
    cs2d_field_type_t type,
    uint8_t player,
    float x, float y,
    float radius,
    float lifetime
)
{
    if (!pw || type == CS2D_FIELD_NONE) return NULL;

    cs2d_field_t *f = NULL;
    for (int i = 0; i < CS2D_MAX_FIELDS; i++) {
        if (!pw->fields[i].active) {
            f = &pw->fields[i];
            break;
        }
    }
    if (!f) return NULL;

    f->active = true;
    f->id = pw->next_field_id++;
    if (pw->next_field_id == 0) pw->next_field_id = 1;
    f->type = type;
    f->player = player;
    f->x = x;
    f->y = y;
    f->radius = radius;
    f->lifetime = lifetime;
    f->max_lifetime = lifetime;
    f->tick_timer = 0.0f;
    f->puff_timer = 0.0f;

    return f;
}

static void apply_radial_damage(cs2d_client_t *client, float x, float y, float radius, int power, uint8_t attacker_id)
{
    if (!client) return;

    /* Damage players in radius */
    for (int i = 1; i <= CS2D_MAX_PLAYERS; i++) {
        cs2d_player_t *pl = &client->game.players[i];
        if (!pl->alive) continue;
        float dx = pl->pos.x - x;
        float dy = pl->pos.y - y;
        float dist = sqrtf(dx * dx + dy * dy);
        if (dist <= radius) {
            float frac = 1.0f - (dist / radius);
            int dmg = (int)((float)power * frac);
            if (dmg < 1) dmg = 1;
            cs2d_player_apply_damage(pl, dmg, 0);
        }
    }

    /* Damage breakable entities in radius */
    for (size_t i = 0; i < client->game.map.entity_count; i++) {
        const cs2d_entity_t *e = &client->game.map.entities[i];
        if (e->type == CS2D_ENT_ENV_BREAKABLE) {
            float ex = (float)e->x * CS2D_TILE_SIZE + 16.0f;
            float ey = (float)e->y * CS2D_TILE_SIZE + 16.0f;
            float dx = ex - x;
            float dy = ey - y;
            float dist = sqrtf(dx * dx + dy * dy);
            if (dist <= radius) {
                cs2d_ents_damage_breakable(&client->ents, (int)i, power);
            }
        }
    }

    (void)attacker_id;
}

static void detonate_projectile(cs2d_client_t *client, cs2d_projectile_t *p)
{
    if (!client || !p || !p->active) return;
    p->active = false;

    switch (p->type) {
        case CS2D_WEP_RPG7:
        case CS2D_WEP_ROCKETLAUNCHER:
            cs2d_fx_explosion(&client->fx, p->x, p->y, 100.0f);
            apply_radial_damage(client, p->x, p->y, 140.0f, (p->type == CS2D_WEP_RPG7 ? 325 : 100), p->player);
            break;

        case CS2D_WEP_GRENADELAUNCHER:
        case CS2D_WEP_HE:
            cs2d_fx_explosion(&client->fx, p->x, p->y, 100.0f);
            apply_radial_damage(client, p->x, p->y, 160.0f, 105, p->player);
            break;

        case CS2D_WEP_FLASHBANG: {
            /* Flashbang sound & flash particles */
            cs2d_fx_sparkle(&client->fx, p->x, p->y, 1.0f, 1.0f, 1.0f);
            for (int i = 0; i < 8; i++) {
                cs2d_fx_smoke_puff(&client->fx, p->x, p->y, 0.9f, 0.9f, 0.9f, 16.0f, 40.0f);
            }
            break;
        }

        case CS2D_WEP_SMOKE:
            /* Spawn smoke field */
            cs2d_field_spawn(&client->projectiles, CS2D_FIELD_SMOKE, p->player, p->x, p->y, 64.0f, 30.0f + CS2D_SMOKE_FADE_SECONDS);
            /* Cover radius 64: Tpro.smokegren_cover 0x140046090 (dist^2 < 4096); 30 s: Tpro time = 30.0 */
            break;

        case CS2D_WEP_GAS:
            /* Spawn poison gas field */
            cs2d_field_spawn(&client->projectiles, CS2D_FIELD_GAS, p->player, p->x, p->y, 90.0f, 30.0f);
            break;

        case CS2D_WEP_MOLOTOV:
            /* Explosion + fire field */
            cs2d_fx_explosion(&client->fx, p->x, p->y, 45.0f);
            apply_radial_damage(client, p->x, p->y, 50.0f, 15, p->player);
            cs2d_field_spawn(&client->projectiles, CS2D_FIELD_FIRE, p->player, p->x, p->y, 40.0f, 20.0f);
            break;

        case CS2D_WEP_SNOWBALL:
            /* Snow particles on impact */
            cs2d_fx_snow_puff(&client->fx, p->x, p->y, 14);
            apply_radial_damage(client, p->x, p->y, 24.0f, 35, p->player);
            break;

        case CS2D_WEP_GUTBOMB:
            /* Blood particles and squish */
            cs2d_fx_blood_splat(&client->fx, p->x, p->y, 16);
            apply_radial_damage(client, p->x, p->y, 96.0f, 50, p->player);
            break;

        case CS2D_WEP_AIRSTRIKE:
            /* Huge cluster air strike explosion */
            cs2d_fx_explosion(&client->fx, p->x, p->y, 300.0f);
            apply_radial_damage(client, p->x, p->y, 300.0f, 1500, p->player);
            for (int k = 0; k < 6; k++) {
                float ox = ((float)(rand() % 160) - 80.0f);
                float oy = ((float)(rand() % 160) - 80.0f);
                cs2d_fx_explosion(&client->fx, p->x + ox, p->y + oy, 120.0f);
            }
            break;

        case CS2D_WEP_FLARE:
            /* Flare expires silently */
            break;

        default:
            break;
    }
}

void cs2d_projectile_remove(struct cs2d_client *client, uint8_t id, uint8_t player)
{
    if (!client) return;
    cs2d_projectile_t *p = cs2d_projectile_get(&client->projectiles, id, player);
    if (p) detonate_projectile(client, p);
}

static bool is_map_solid(const cs2d_map_t *map, float px, float py)
{
    if (!map || !map->collision) return false;
    int tx = (int)(px * 0.03125f);
    int ty = (int)(py * 0.03125f);
    if (px < 0.0f || py < 0.0f || (uint32_t)tx >= map->width || (uint32_t)ty >= map->height) return true;
    uint8_t c = map->collision[(size_t)ty * map->width + (size_t)tx];
    return c == 1 || c == 2;
}

void cs2d_projectile_update(struct cs2d_client *client, float dt)
{
    if (!client || dt <= 0.0f) return;
    cs2d_projectile_world_t *pw = &client->projectiles;
    const cs2d_map_t *map = &client->game.map;

    /* 1. Update Projectiles */
    for (int i = 0; i < CS2D_MAX_PROJECTILES; i++) {
        cs2d_projectile_t *p = &pw->projectiles[i];
        if (!p->active) continue;

        if (p->state == CS2D_PROJ_STATE_FLYING) {
            float step = p->speed * dt;
            if (step > p->flydist) step = p->flydist;

            float rad = p->dir * DEG2RAD;
            float vx = sinf(rad) * p->speed;
            float vy = -cosf(rad) * p->speed;

            /* Flying effects (rocket trail, etc.) */
            if (cs2d_projectile_is_rocket(p->type)) {
                cs2d_fx_smoke_puff(&client->fx, p->x - sinf(rad) * 12.0f, p->y + cosf(rad) * 12.0f,
                                   0.8f, 0.8f, 0.8f, 10.0f, 15.0f);
                cs2d_fx_fire_puff(&client->fx, p->x - sinf(rad) * 8.0f, p->y + cosf(rad) * 8.0f, 8.0f);
            }

            float next_x = p->x + vx * dt;
            float next_y = p->y + vy * dt;

            /* Check impact on map / obstacle */
            bool hit_solid_x = is_map_solid(map, next_x, p->y);
            bool hit_solid_y = is_map_solid(map, p->x, next_y);
            bool hit_solid = hit_solid_x || hit_solid_y || is_map_solid(map, next_x, next_y);

            /* Check hit on another alive player (radius 20 px) */
            bool hit_player = false;
            for (int pl_idx = 1; pl_idx <= CS2D_MAX_PLAYERS; pl_idx++) {
                const cs2d_player_t *pl = &client->game.players[pl_idx];
                if (!pl->alive || pl->id == p->player) continue;
                float pdx = pl->pos.x - next_x;
                float pdy = pl->pos.y - next_y;
                if (pdx * pdx + pdy * pdy < 400.0f) { /* 20^2 */
                    hit_player = true;
                    break;
                }
            }

            if (hit_solid || hit_player) {
                if (cs2d_projectile_is_impact(p->type)) {
                    /* Rocket, RPG, Gut bomb, Snowball, Molotov stop and detonate on first obstacle */
                    p->x = next_x;
                    p->y = next_y;
                    detonate_projectile(client, p);
                    continue;
                } else {
                    /* Bouncing grenades: bounce off wall */
                    if (hit_solid_x) {
                        vx = -vx * 0.5f;
                    }
                    if (hit_solid_y) {
                        vy = -vy * 0.5f;
                    }
                    p->dir = atan2f(vx, -vy) * RAD2DEG;
                    p->flydist *= 0.5f;
                    p->speed *= 0.65f;
                    p->rot_speed = -p->rot_speed;
                    cs2d_fx_sparkle(&client->fx, p->x, p->y, 0.9f, 0.9f, 0.5f);
                }
            } else {
                p->x = next_x;
                p->y = next_y;
                p->flydist -= step;
            }

            p->rot += p->rot_speed * dt;

            if (p->flydist <= 0.0f) {
                p->state = CS2D_PROJ_STATE_GROUND;
                p->speed = 0.0f;
                p->rot_speed = 0.0f;

                /* Special handling for impact weapons that reached end of flydist without obstacle */
                if (cs2d_projectile_is_impact(p->type)) {
                    detonate_projectile(client, p);
                    continue;
                }
            }
        }

        /* Lifetime countdown */
        p->lifetime -= dt;

        /* Ground effects & detonation */
        if (p->state == CS2D_PROJ_STATE_GROUND) {
            p->effect_timer += dt;

            if (p->type == CS2D_WEP_FLARE) {
                /* Flare emits bright sparks continuously on ground */
                if (p->effect_timer >= 0.05f) {
                    p->effect_timer = 0.0f;
                    cs2d_fx_sparkle(&client->fx, p->x, p->y, 1.0f, 0.3f, 0.2f);
                    cs2d_fx_smoke_puff(&client->fx, p->x, p->y, 0.8f, 0.3f, 0.2f, 8.0f, 15.0f);
                }
            } else if (p->type == CS2D_WEP_AIRSTRIKE) {
                /* Airstrike emits red marker smoke until detonation */
                if (p->effect_timer >= 0.1f) {
                    p->effect_timer = 0.0f;
                    cs2d_fx_smoke_puff(&client->fx, p->x, p->y, 0.9f, 0.1f, 0.1f, 14.0f, 25.0f);
                    cs2d_fx_sparkle(&client->fx, p->x, p->y, 1.0f, 0.2f, 0.1f);
                }
            }

            /* Grenades (HE, Flashbang, Smoke, Gas) detonate when lifetime expires */
            if (p->lifetime <= 0.0f) {
                detonate_projectile(client, p);
                continue;
            }
        }
    }

    /* 2. Update Persistent Fields (smoke, gas, fire) */
    for (int i = 0; i < CS2D_MAX_FIELDS; i++) {
        cs2d_field_t *f = &pw->fields[i];
        if (!f->active) continue;

        f->lifetime -= dt;
        if (f->lifetime <= 0.0f) {
            f->active = false;
            continue;
        }

        f->tick_timer += dt;

        switch (f->type) {
            case CS2D_FIELD_SMOKE:
                /* A Tp 0x20 puff on every 100 ms tick (DAT_140a1b05c) while the grenade lies there (30 s) */
                f->puff_timer += dt;
                if (f->lifetime > CS2D_SMOKE_FADE_SECONDS && f->puff_timer >= 0.1f) {
                    f->puff_timer -= 0.1f;
                    cs2d_gen_smoke_grenade_puff(&client->gen, f->x, f->y);
                }
                break;

            case CS2D_FIELD_GAS:
                /* A green Tp 3 puff on every 100 ms tick (Tpro.draw_ground case 0x48) */
                f->puff_timer += dt;
                if (f->puff_timer >= 0.1f) {
                    f->puff_timer -= 0.1f;
                    cs2d_gen_gas_grenade_puff(&client->gen, f->x, f->y);
                }
                /* Damage tick every 0.5s to players without gas mask */
                if (f->tick_timer >= 0.5f) {
                    f->tick_timer = 0.0f;
                    float r2 = f->radius * f->radius;
                    for (int pl_idx = 1; pl_idx <= CS2D_MAX_PLAYERS; pl_idx++) {
                        cs2d_player_t *pl = &client->game.players[pl_idx];
                        if (!pl->alive || pl->has_gasmask) continue;
                        float dx = pl->pos.x - f->x;
                        float dy = pl->pos.y - f->y;
                        if (dx * dx + dy * dy <= r2) {
                            cs2d_player_apply_damage(pl, 9, 0);
                        }
                    }
                }
                break;

            case CS2D_FIELD_FIRE:
                /* Flames: every 60 Hz frame, a Tp 14 half of the time (Tpro.draw_ground case 0x49) */
                f->puff_timer += dt;
                while (f->puff_timer >= 1.0f / 60.0f) {
                    f->puff_timer -= 1.0f / 60.0f;
                    cs2d_gen_molotov_flame(&client->gen, f->x, f->y);
                }
                /* Damage tick every 0.25s */
                if (f->tick_timer >= 0.25f) {
                    f->tick_timer = 0.0f;
                    float r2 = f->radius * f->radius;
                    for (int pl_idx = 1; pl_idx <= CS2D_MAX_PLAYERS; pl_idx++) {
                        cs2d_player_t *pl = &client->game.players[pl_idx];
                        if (!pl->alive) continue;
                        float dx = pl->pos.x - f->x;
                        float dy = pl->pos.y - f->y;
                        if (dx * dx + dy * dy <= r2) {
                            cs2d_player_apply_damage(pl, 4, 0);
                        }
                    }
                }
                break;

            default:
                break;
        }
    }
}

bool cs2d_projectile_smoke_cover(const cs2d_projectile_world_t *pw, float x, float y)
{
    if (!pw) return false;
    for (int i = 0; i < CS2D_MAX_FIELDS; i++) {
        const cs2d_field_t *f = &pw->fields[i];
        if (!f->active || f->type != CS2D_FIELD_SMOKE || f->lifetime <= CS2D_SMOKE_FADE_SECONDS) continue;
        float dx = f->x - x;
        float dy = f->y - y;
        if (dx * dx + dy * dy <= f->radius * f->radius) return true;
    }
    return false;
}

