#include "cs2d_client.h"
#include "cs2d_weapons.h"
#include "cs2d_player.h"
#include "cs2d_game.h"
#include "cs2d_network.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/*
 * Game scene: a loaded map with the local player, practice bots, HUD and the
 * UDP connection to config.server_ip. Everything acquired here is released in
 * game_leave, so returning to the lobby leaves no map, textures or socket behind.
 */

static void center_camera_on_local_player(cs2d_client_t *client)
{
    const cs2d_player_t *pl = &client->game.players[client->local_player_id];
    client->cam_x = pl->pos.x - (float)client->config.window_width * 0.5f;
    client->cam_y = pl->pos.y - (float)client->config.window_height * 0.5f;
}

/* --- Spectator: camera, [67] / [32] and HUD of a dead player or a Spectator (cs2d_spectate.h) --- */

#define SPEC_BAR_H 40.0f /* Black bands: button height + 15 (FUN_14029ac10) */
#define SPEC_BTN_H 25.0f /* 25 * font height / 13 */

static cs2d_texture_t s_tex_shade; /* gfx/gui_shade.bmp (0x140a0de78) */

/* DAT_140ddf344: HUD windows open (the console does not count) */
static bool spec_menu_open(const cs2d_client_t *client)
{
    return client->server_info_open || client->team_menu_open || client->download_open;
}

static const cs2d_player_t *spec_target(const cs2d_client_t *client)
{
    int t = client->spec.target;
    if (t < 1 || t > CS2D_MAX_PLAYERS || client->game.players[t].id == 0) return NULL;
    return &client->game.players[t];
}

/* FUN_1401682f0: [67, player] (reliable, 0 = free look) tells the server whom we watch; only while dead */
static void spec_send_target(cs2d_client_t *client, int target)
{
    if (!client->connected || client->join_stage != 5) return;
    if (client->game.players[client->local_player_id].alive) return;
    uint8_t pkt[2] = { (uint8_t)CS2D_NET_CLIENTSPEC, (uint8_t)target };
    cs2d_client_send_udp(client, pkt, sizeof(pkt), true);
}

static void spec_pick(cs2d_client_t *client, int dir)
{
    if (cs2d_spec_pick(&client->spec, &client->game, client->local_player_id, dir)) {
        spec_send_target(client, client->spec.target);
    }
}

/* "specplayer" (bound to space, 0x1401b58c6): the next player, or follow mode again. A proxy shows
 * [67, n][67, n]: one from the pick, one from the command */
void cs2d_scene_game_specplayer(cs2d_client_t *client)
{
    if (spec_menu_open(client) || client->spec.server_mode == 0) return;
    if (client->spec.mode == CS2D_SPEC_FOLLOW) spec_pick(client, 1);
    client->spec.mode = CS2D_SPEC_FOLLOW;
    spec_send_target(client, client->spec.target);
}

/* "specmode" (bound to leftctrl, 0x1401b5980): free look <-> follow, only with sv_specmode 1 */
void cs2d_scene_game_specmode(cs2d_client_t *client)
{
    if (client->spec.server_mode != 1) return;
    if (client->spec.mode == CS2D_SPEC_FREE) {
        client->spec.mode = CS2D_SPEC_FOLLOW;
        spec_send_target(client, client->spec.target);
    } else {
        client->spec.mode = CS2D_SPEC_FREE;
        spec_send_target(client, 0);
    }
}

/* FUN_140060b10: nobody in T or CT and a menu open -> a random Info_T, next time an Info_CT, every 5 s */
static void spec_showcase(cs2d_client_t *client, int *cam_x, int *cam_y, float half_w, float half_h)
{
    int playing = 0;
    for (int i = 1; i <= CS2D_MAX_PLAYERS; i++) {
        const cs2d_player_t *p = &client->game.players[i];
        if (p->id != 0 && p->team != CS2D_TEAM_SPEC) playing++;
    }
    if (playing == 0 && spec_menu_open(client)) {
        cs2d_entity_type_t type = (client->spec.idle_team < 2) ? CS2D_ENT_INFO_T : CS2D_ENT_INFO_CT;
        client->spec.idle_team = (type == CS2D_ENT_INFO_T) ? 2 : 1;
        const cs2d_map_t *map = &client->game.map;
        int n = 0;
        for (uint32_t i = 0; i < map->entity_count; i++) n += (map->entities[i].type == type);
        if (n > 0) {
            int k = rand() % n; /* Te.getrandom */
            for (uint32_t i = 0; i < map->entity_count; i++) {
                if (map->entities[i].type != type || k-- > 0) continue;
                *cam_x = (int)((float)(map->entities[i].x * 32 + 16) - half_w);
                *cam_y = (int)((float)(map->entities[i].y * 32 + 16) - half_h);
                break;
            }
        }
    }
    client->spec.idle_ticks = 5;
}

/* FUN_14025aa60: the camera of a local player that is not alive. tick = the 1 s network tick */
static void update_spectator_camera(cs2d_client_t *client, float delta_time, bool tick)
{
    cs2d_spec_t *s = &client->spec;
    float half_w = (float)client->config.window_width * 0.5f;
    float half_h = (float)client->config.window_height * 0.5f;
    int cx = (int)client->cam_x, cy = (int)client->cam_y;
    float frames = delta_time * 60.0f; /* Time factor DAT_140a1b090: 1.0 per 60 Hz frame */

    if (s->server_mode == 0) {
        /* Nothing (War Mode): the camera looks far outside the map */
        s->mode = CS2D_SPEC_NOTHING;
        cx = cy = -100000;
    } else {
        if (s->server_mode == 2 || s->mode == CS2D_SPEC_NOTHING) s->mode = CS2D_SPEC_FOLLOW;
        bool following = false;
        if (s->mode == CS2D_SPEC_FREE) {
            cs2d_spec_free_look(&cx, &cy, client->key_left, client->key_right, client->key_forward, client->key_back,
                                client->key_walk, frames);
        } else {
            spec_pick(client, 0);
            const cs2d_player_t *t = spec_target(client);
            if (t) {
                cs2d_spec_follow(&cx, &cy, t->pos.x, t->pos.y, half_w, half_h, frames);
                following = true;
            }
        }
        if (!following && s->idle_cam && tick && --s->idle_ticks < 1) spec_showcase(client, &cx, &cy, half_w, half_h);
        /* Own Team only: a dead target is replaced by the next player (HUD, 0x14029b574) */
        const cs2d_player_t *t = spec_target(client);
        if (s->server_mode == 2 && t && !t->alive) spec_pick(client, 1);
    }
    client->cam_x = (float)cx;
    client->cam_y = (float)cy;
}

static bool point_in_rect(float px, float py, float x, float y, float w, float h)
{
    return px >= x && py >= y && px <= x + w && py <= y + h;
}

/* Text colors of the spectator HUD: 0 gray/white, 1 Terrorist (255,25,0), 2 Counter-Terrorist (50,150,255) */
static void spec_text_color(int color, bool hover, bool enabled, float *r, float *g, float *b, float *a)
{
    if (color == 1)      { *r = 1.0f;            *g = 25.0f / 255.0f;  *b = 0.0f; }
    else if (color == 2) { *r = 50.0f / 255.0f;  *g = 150.0f / 255.0f; *b = 1.0f; }
    if (color == 0) {
        float c = hover ? 1.0f : (enabled ? 200.0f / 255.0f : 150.0f / 255.0f);
        *r = *g = *b = c;
        *a = 1.0f;
    } else {
        *a = hover ? 1.0f : (enabled ? 0.8f : 0.5f);
    }
}

/*
 * FUN_140298bf0: HUD button. Dark inside (50, alpha 0.6), 1 px frame 200 hovered / 150 / 100 disabled,
 * additive gui_shade 0.3 hovered / 0.1, text centered and squeezed to w - 4. Returns whether it is hovered.
 */
static bool spec_button(const cs2d_client_t *client, float x, float y, float w, float h, const char *text,
                        int color, bool enabled)
{
    bool inside = point_in_rect(client->mouse_screen_x, client->mouse_screen_y, x, y, w, h);
    bool hover  = enabled && inside && !spec_menu_open(client);
    float fc = hover ? 200.0f / 255.0f : (enabled ? 150.0f / 255.0f : 100.0f / 255.0f);
    float d = 50.0f / 255.0f;
    cs2d_render_rect(x + 1.0f, y + 1.0f, w - 2.0f, h - 2.0f, d, d, d, 0.6f, true);
    cs2d_render_rect(x + 1.0f, y, w - 2.0f, 1.0f, fc, fc, fc, 1.0f, true);
    cs2d_render_rect(x, y + 1.0f, 1.0f, h - 2.0f, fc, fc, fc, 1.0f, true);
    cs2d_render_rect(x + 1.0f, y + h - 1.0f, w - 2.0f, 1.0f, fc, fc, fc, 1.0f, true);
    cs2d_render_rect(x + w - 1.0f, y + 1.0f, 1.0f, h - 2.0f, fc, fc, fc, 1.0f, true);
    if (s_tex_shade.id) {
        cs2d_render_sprite_frame(&s_tex_shade, 10, 10, 0, x + 1.0f + (w - 2.0f) * 0.5f, y + 6.0f, w - 2.0f, 10.0f,
                                 0.0f, 1.0f, 1.0f, 1.0f, hover ? 0.3f : 0.1f, 4);
    }

    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
    spec_text_color(color, hover, enabled, &r, &g, &b, &a);
    float tw, th;
    cs2d_render_text_measure(CS2D_FONT_REGULAR, text, &tw, &th);
    float sc = (tw > w - 4.0f) ? (w - 4.0f) / tw : 1.0f;
    float tx = (float)((int)w / 2 + (int)x - (int)(tw * 0.5f * sc));
    float ty = (float)((int)h / 2 + (int)y - (int)th / 2);
    cs2d_render_text_scaled_ex(CS2D_FONT_REGULAR, tx, ty, text, sc, r, g, b, a);
    return hover;
}

/* Special armors 201..206 are shown by name (items 79..84, name table 0x1409fff98 + 0x278) */
static const char *const s_armor_names[6] = {
    "Light Armor", "Armor", "Heavy Armor", "Medic Armor", "Super Armor", "Stealth Suit"
};

/* FUN_140299470: followed player's name, health and armor centered in the name box */
static void spec_player_info(const cs2d_client_t *client, float x, float y, float w, float h, int color,
                             const cs2d_player_t *p, bool enabled)
{
    const cs2d_font_id_t f = CS2D_FONT_REGULAR;
    float w_sp = cs2d_render_text_width_ex(f, " ");
    float w_name = cs2d_render_text_width_ex(f, p->name);
    float w_num = cs2d_render_text_width_ex(f, "0000");
    float total = w_name + w_sp + w_num + w_num;
    bool hover = enabled && !spec_menu_open(client) &&
                 point_in_rect(client->mouse_screen_x, client->mouse_screen_y, x, y, w, h);
    char probe[CS2D_MAX_NAME_LEN + 4];
    snprintf(probe, sizeof(probe), "%s(X)", p->name);
    float tw, th;
    cs2d_render_text_measure(f, probe, &tw, &th);
    int x0 = ((int)w / 2 + (int)x) - (int)(total * 0.5f);

    /* Avatar square left of the name; the original draws "?" when the player has none */
    if (p->id != client->local_player_id) {
        float ax = (float)x0 - h, gray = 128.0f / 255.0f;
        cs2d_render_rect(ax - 1.0f, y + 2.0f, h - 4.0f, h - 4.0f, gray, gray, gray, 1.0f, true);
        cs2d_render_rect(ax, y + 3.0f, h - 6.0f, h - 6.0f, 0.0f, 0.0f, 0.0f, 1.0f, true);
        float qw, qh;
        cs2d_render_text_measure(f, "?", &qw, &qh);
        cs2d_render_text_ex(f, ax + (h - 6.0f - qw) * 0.5f, y + 3.0f + (h - 6.0f - qh) * 0.5f, "?", 1, 1, 1, 1);
    }

    /* Team colors, except in Deathmatch (no teams: table 0x140a015d0) */
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
    spec_text_color(client->net_sv_gamemode == 1 ? 0 : color, hover, enabled, &r, &g, &b, &a);
    float sc = (w - 3.0f <= total) ? (w - 4.0f) / total : 1.0f;
    float ty = (float)(int)((float)((int)h / 2) + y - th * 0.5f);
    cs2d_render_text_scaled_ex(f, (float)x0, ty, p->name, sc, r, g, b, a);

    cs2d_gfx_bank_t *bank = cs2d_render_get_gfx_bank();
    int cell = (bank && bank->tex_hud_symbols.height >= 64) ? 64 : 32;
    float step = (w_sp + th) * sc;
    char num[16];

    /* Health: hud_symbols frame 0, additive (255,255,32); value from red (0) to green (100) */
    float hx = (float)(int)((w_name + w_sp) * sc + (float)x0);
    if (bank && bank->tex_hud_symbols.id) {
        cs2d_render_sprite_frame(&bank->tex_hud_symbols, cell, cell, 0, hx + th * 0.5f, ty + 1.0f + th * 0.5f, th, th,
                                 0.0f, 1.0f, 1.0f, 32.0f / 255.0f, a, 4);
    }
    float vx = (float)(int)(hx + step);
    float hp = (float)p->health;
    snprintf(num, sizeof(num), "%d", p->health);
    cs2d_render_text_scaled_ex(f, vx, ty, num, sc, fminf(fmaxf((100.0f - hp) * 5.1f / 255.0f, 0.0f), 1.0f),
                               fminf(fmaxf(hp * 5.1f / 255.0f, 0.0f), 1.0f), 0.0f, a);

    /* Armor: frame 1, same colors */
    if (p->armor > 0) {
        float ax = (float)(int)(w_num * sc + vx);
        if (bank && bank->tex_hud_symbols.id) {
            cs2d_render_sprite_frame(&bank->tex_hud_symbols, cell, cell, 1, ax + th * 0.5f, ty + 1.0f + th * 0.5f, th, th,
                                     0.0f, 1.0f, 1.0f, 32.0f / 255.0f, a, 4);
        }
        float av = (float)p->armor;
        const char *txt = num;
        if (p->armor >= 201 && p->armor <= 206) txt = s_armor_names[p->armor - 201];
        else snprintf(num, sizeof(num), "%d", p->armor);
        cs2d_render_text_scaled_ex(f, (float)(int)(step + ax), ty, txt, sc,
                                   fminf(fmaxf((100.0f - av) * 5.1f / 255.0f, 0.0f), 1.0f),
                                   fminf(fmaxf(av * 5.1f / 255.0f, 0.0f), 1.0f), 0.0f, a);
    }
}

/* What the name box shows (FUN_14029ac10 at 0x14029b4ed..0x14029b68e) */
typedef enum { SPEC_BOX_DASH, SPEC_BOX_NOSPEC, SPEC_BOX_NAME, SPEC_BOX_INFO } spec_box_kind_t;

static spec_box_kind_t spec_box(const cs2d_client_t *client, const cs2d_player_t **out, int *color)
{
    const cs2d_spec_t *s = &client->spec;
    const cs2d_player_t *t = spec_target(client);
    int lt = (int)client->game.players[client->local_player_id].team;
    *out = t;
    *color = 0;
    if (!t || s->mode != CS2D_SPEC_FOLLOW) return (s->server_mode != 0) ? SPEC_BOX_DASH : SPEC_BOX_NOSPEC;
    if (t->team == CS2D_TEAM_T) {
        *color = 1;
        return (lt > 1 && s->server_mode != 1) ? SPEC_BOX_NAME : SPEC_BOX_INFO;
    }
    *color = 2;
    return (lt == 1 && s->server_mode != 1) ? SPEC_BOX_NAME : SPEC_BOX_INFO;
}

/* Spectator HUD (FUN_14029ac10): bands, "Spectating", Menu / Map / Fog, name box and mode button */
static void render_spectator_hud(cs2d_client_t *client, int screen_w, int screen_h)
{
    const cs2d_spec_t *s = &client->spec;
    const cs2d_player_t *me = &client->game.players[client->local_player_id];
    bool menu = spec_menu_open(client);
    float sw = (float)screen_w, sh = (float)screen_h;
    float by = sh - 8.0f - SPEC_BTN_H;
    const cs2d_font_id_t f = CS2D_FONT_REGULAR;
    float tw, th;

    cs2d_render_rect(0.0f, 0.0f, sw, SPEC_BAR_H, 0.0f, 0.0f, 0.0f, 0.55f, true);
    cs2d_render_rect(0.0f, sh - SPEC_BAR_H, sw, SPEC_BAR_H, 0.0f, 0.0f, 0.0f, 0.55f, true);

    /* A dead team member: "You are dead!" at the top. Respawn game modes (DM, TDM, Construction, Zombies)
     * pulse yellow-red and offer a respawn (not implemented: the click sends nothing yet) */
    if (me->team != CS2D_TEAM_SPEC) {
        double t = (double)GetTickCount();
        const char *msg;
        float r, g;
        if (client->net_sv_gamemode >= 1 && client->net_sv_gamemode <= 4) {
            msg = "You are dead! - Click to respawn!"; /* sys/language 0764 */
            r = 1.0f;
            g = (float)fabs(sin((double)((int)t / 5) * 0.017453292519943295));
        } else {
            msg = "You are dead!";                     /* sys/language 0765 */
            r = (float)(fabs(sin((double)((int)t / 10) * 0.017453292519943295)) * 155.0 + 100.0) / 255.0f;
            g = 0.0f;
        }
        cs2d_render_text_measure(f, msg, &tw, &th);
        cs2d_render_text_ex(f, (float)(screen_w / 2 - (int)tw / 2), 0.0f, msg, r, g, 0.0f, 1.0f);
    }

    const char *spectating = "Spectating"; /* 0768 */
    cs2d_render_text_measure(f, spectating, &tw, &th);
    float gray = 150.0f / 255.0f;
    cs2d_render_text_ex(f, (float)(screen_w / 2 - (int)tw / 2), SPEC_BAR_H - th - 3.0f, spectating, gray, gray, gray, 1.0f);

    /* Menu, Map and Fog (the last only with sv_fow and sv_specmode 1); their windows are not reconstructed */
    spec_button(client, 5.0f, by, 46.0f, SPEC_BTN_H, "Menu", 0, !menu);
    spec_button(client, 56.0f, by, 37.0f, SPEC_BTN_H, "Map", 0, !menu);
    spec_button(client, 98.0f, by, 37.0f, SPEC_BTN_H, "Fog", 0, !menu && client->net_sv_fow > 0 && s->server_mode == 1);

    /* Mode button: 0770 "Spectate Nothing", 0771 "Free Look", 0772 "Follow Player" */
    static const char *const mode_names[3] = { "Spectate Nothing", "Free Look", "Follow Player" };
    bool mode_enabled = !menu && (s->mode == CS2D_SPEC_FREE || (s->mode == CS2D_SPEC_FOLLOW && s->server_mode == 1));
    spec_button(client, sw - 135.0f, by, 130.0f, SPEC_BTN_H, mode_names[s->mode], 0, mode_enabled);

    /* Name box between them */
    const cs2d_player_t *t;
    int color;
    spec_box_kind_t kind = spec_box(client, &t, &color);
    float nx = 140.0f, nw = sw - 280.0f;
    if (kind == SPEC_BOX_DASH) {
        spec_button(client, nx, by, nw, SPEC_BTN_H, "-", 0, false);
    } else if (kind == SPEC_BOX_NOSPEC) {
        spec_button(client, nx, by, nw, SPEC_BTN_H, "Server does not allow Spectating", 0, false); /* 0773 */
    } else if (kind == SPEC_BOX_NAME) {
        spec_button(client, nx, by, nw, SPEC_BTN_H, t->name, color, !menu);
    } else {
        spec_button(client, nx, by, nw, SPEC_BTN_H, "", color, !menu);
        spec_player_info(client, nx, by, nw, SPEC_BTN_H, color, t, !menu);
    }
}

/* Clicks on the spectator HUD: the mode button toggles, the name box picks the next (left) or previous (right) player */
static bool spectator_hud_click(cs2d_client_t *client, float mx, float my, int button)
{
    cs2d_spec_t *s = &client->spec;
    if (spec_menu_open(client) || (button != 1 && button != 2)) return false;
    float sw = (float)client->config.window_width, sh = (float)client->config.window_height;
    float by = sh - 8.0f - SPEC_BTN_H;

    if (point_in_rect(mx, my, sw - 135.0f, by, 130.0f, SPEC_BTN_H)) {
        if (s->mode == CS2D_SPEC_FOLLOW && s->server_mode == 1) {
            s->mode = CS2D_SPEC_FREE;
            spec_send_target(client, 0);
        } else if (s->mode == CS2D_SPEC_FREE) {
            s->mode = CS2D_SPEC_FOLLOW;
            spec_send_target(client, s->target);
        }
        return true;
    }
    const cs2d_player_t *t;
    int color;
    spec_box_kind_t kind = spec_box(client, &t, &color);
    if ((kind == SPEC_BOX_NAME || kind == SPEC_BOX_INFO) && point_in_rect(mx, my, 140.0f, by, sw - 280.0f, SPEC_BTN_H)) {
        if (s->mode == CS2D_SPEC_FOLLOW) spec_pick(client, button == 1 ? 1 : -1);
        s->mode = CS2D_SPEC_FOLLOW;
        return true;
    }
    return point_in_rect(mx, my, 0.0f, sh - SPEC_BAR_H, sw, SPEC_BAR_H);
}

/* Loads maps/<name>.map and its tileset (FUN_140113d50) into the running game */
static bool game_load_map(cs2d_client_t *client, const char *map_name)
{
    char map_path[128];
    snprintf(map_path, sizeof(map_path), "maps/%s.map", map_name);
    if (!cs2d_game_load_map(&client->game, map_path)) return false;
    snprintf(client->game_map_name, sizeof(client->game_map_name), "%s", map_name);

    cs2d_texture_free(&client->tex_tileset);
    cs2d_texture_load_tileset(&client->tex_tileset, client->game.map.tileset);
    cs2d_render_invalidate_shadows();
    cs2d_game_start_round(&client->game);

    /* Entity logic: offline the client runs it (round start fires every Trigger_Start);
     * online the join snapshot replaces these states with the server's */
    cs2d_ents_init(&client->ents, &client->game.map);
    cs2d_ents_round_start(&client->ents);
    cs2d_fx_clear(&client->fx);
    memset(client->ground_items, 0, sizeof(client->ground_items));
    client->ents_round = client->game.round_number;
    client->snapshot_states_reset = false;
    client->walk_active = false;
    client->last_tile_x = client->last_tile_y = -1;

    /* On de_dust2, place the local player at the exact CT spawn tile (31, 44.5) */
    if (_stricmp(map_name, "de_dust2") == 0) {
        cs2d_player_t *pl = &client->game.players[client->local_player_id];
        pl->pos.x = 31.0f * CS2D_TILE_SIZE;
        pl->pos.y = 44.5f * CS2D_TILE_SIZE;
    }
    center_camera_on_local_player(client);

    cs2d_client_console_print(client, 170, 170, 170, "load map '%s' (%ux%u, tileset: %s, sun: %d deg)",
                              map_name, client->game.map.width, client->game.map.height,
                              client->game.map.tileset, client->game.map.daylight_time);
    return true;
}

bool cs2d_scene_game_change_map(cs2d_client_t *client, const char *map_name)
{
    if (!client || client->scene != CS2D_SCENE_GAME) return false;
    if (!game_load_map(client, map_name)) {
        cs2d_client_console_print(client, 255, 45, 45, "Error: Unable to load map 'maps/%s.map'", map_name);
        return false;
    }
    return true;
}

void cs2d_scene_game_restart_round(cs2d_client_t *client)
{
    cs2d_game_start_round(&client->game);
    client->ents_round = client->game.round_number;
    cs2d_ents_round_start(&client->ents);
    cs2d_fx_clear(&client->fx);
    client->walk_active = false;
    client->last_tile_x = client->last_tile_y = -1;
    center_camera_on_local_player(client);
    cs2d_client_console_print(client, 255, 220, 0, "Round Restart");
}

/* Round rules shared with the dedicated server: mp_freezetime (s) and mp_roundtime (min) from sys/server.cfg */
static void load_server_rules(cs2d_game_t *game)
{
    FILE *f = fopen("sys/server.cfg", "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        float v;
        if (sscanf(line, "mp_freezetime %f", &v) == 1 && v >= 0.0f) game->cfg_freezetime = v;
        if (sscanf(line, "mp_roundtime %f", &v) == 1 && v > 0.0f) game->cfg_roundtime = v * 60.0f;
    }
    fclose(f);
}

void cs2d_scene_game_apply_entity_state(cs2d_client_t *client, int tx, int ty, uint8_t state)
{
    const cs2d_map_t *map = &client->game.map;
    for (uint32_t i = 0; i < map->entity_count; i++) {
        const cs2d_entity_t *e = &map->entities[i];
        if (e->x == tx && e->y == ty && map->entity_state[i] != state) {
            cs2d_ents_set_state(&client->ents, (int)i, state);
            return;
        }
    }
}

static bool tile_walkable(const cs2d_map_t *map, int tx, int ty)
{
    if (tx < 0 || ty < 0 || (uint32_t)tx >= map->width || (uint32_t)ty >= map->height) return false;
    uint8_t c = map->collision[(size_t)ty * map->width + (size_t)tx];
    return c == 0;
}

/* Breadth-first search over walkable tiles (8-way, no corner cutting); fills walk_path with tile centers */
bool cs2d_scene_game_walk_to(cs2d_client_t *client, int tx, int ty)
{
    const cs2d_map_t *map = &client->game.map;
    const cs2d_player_t *pl = &client->game.players[client->local_player_id];
    int sx = (int)(pl->pos.x / CS2D_TILE_SIZE);
    int sy = (int)(pl->pos.y / CS2D_TILE_SIZE);
    if (!map->collision || !tile_walkable(map, tx, ty)) return false;

    int w = (int)map->width, h = (int)map->height;
    int *prev = (int *)malloc(sizeof(int) * (size_t)w * (size_t)h);
    int *queue = (int *)malloc(sizeof(int) * (size_t)w * (size_t)h);
    if (!prev || !queue) {
        free(prev);
        free(queue);
        return false;
    }
    for (int i = 0; i < w * h; i++) prev[i] = -2;

    static const int dirs[8][2] = { {1,0}, {-1,0}, {0,1}, {0,-1}, {1,1}, {1,-1}, {-1,1}, {-1,-1} };
    int head = 0, tail = 0, start = sy * w + sx, goal = ty * w + tx;
    prev[start] = -1;
    queue[tail++] = start;
    while (head < tail && prev[goal] == -2) {
        int cur = queue[head++];
        int cx = cur % w, cy = cur / w;
        for (int d = 0; d < 8; d++) {
            int nx = cx + dirs[d][0], ny = cy + dirs[d][1];
            if (!tile_walkable(map, nx, ny) || prev[ny * w + nx] != -2) continue;
            if (dirs[d][0] && dirs[d][1] && (!tile_walkable(map, cx + dirs[d][0], cy) || !tile_walkable(map, cx, cy + dirs[d][1]))) continue;
            prev[ny * w + nx] = cur;
            queue[tail++] = ny * w + nx;
        }
    }

    bool found = (prev[goal] != -2);
    if (found) {
        int n = 0;
        for (int at = goal; at != -1 && n < 1024; at = prev[at]) queue[n++] = at; /* Reuse queue for the reversed path */
        client->walk_len = 0;
        for (int i = n - 1; i >= 0 && client->walk_len < 1024; i--) {
            client->walk_path[client->walk_len].x = (float)(queue[i] % w) * CS2D_TILE_SIZE + 16.0f;
            client->walk_path[client->walk_len].y = (float)(queue[i] / w) * CS2D_TILE_SIZE + 16.0f;
            client->walk_len++;
        }
        client->walk_pos    = 0;
        client->walk_active = client->walk_len > 0;
    }
    free(prev);
    free(queue);
    return found;
}

/*
 * A shot of the local player. Online it is the fire packet [7, seed u8] (recorded from CS2D.exe:
 * melee always sends 0, firearms count 0, 2, 3, 4, ... per shot; the server stores the byte,
 * reading 0 as 1, before running the shot) and the server reports what broke; the impact debris
 * is drawn locally either way. Offline the shot damages the breakable it hits with the weapon damage.
 */
static void fire_shot(cs2d_client_t *client, const cs2d_player_t *pl)
{
    const cs2d_weapon_def_t *wep = cs2d_weapon_get(pl->current_weapon);
    if (!wep) return;
    bool melee = (wep->slot == CS2D_SLOT_MELEE || pl->current_weapon == CS2D_WEP_WRENCH);

    if (client->connected && client->join_stage == 5) {
        uint8_t seed = 0;
        if (!melee) {
            seed = (uint8_t)(client->fire_seed < 0 ? 0 : client->fire_seed);
            client->fire_seed = (seed == 0) ? 2 : (seed == 255 ? 1 : seed + 1);
        }
        uint8_t fire_pkt[2] = { (uint8_t)CS2D_NET_FIRE, seed };
        cs2d_client_send_udp(client, fire_pkt, sizeof(fire_pkt), false);
    }

    float hx, hy;
    float reach = cs2d_weapon_shot_reach(wep);
    int hit = cs2d_ents_trace_shot(&client->ents, pl->pos.x, pl->pos.y, pl->angle, reach, &hx, &hy);

    if (melee) {
        if (pl->current_weapon != CS2D_WEP_CHAINSAW) {
            cs2d_fx_knifeslash(&client->fx, pl->pos.x, pl->pos.y, pl->angle);
        }
    } else {
        /* Tracer, muzzle flash and case: measured on CS2D.exe captures (AK-47 flash ~30 px ahead) */
        float ddx = hx - pl->pos.x, ddy = hy - pl->pos.y;
        bool hit_wall = (hit < 0 && sqrtf(ddx * ddx + ddy * ddy) < reach - 1.0f);
        int shell = (pl->current_weapon == CS2D_WEP_M3 || pl->current_weapon == CS2D_WEP_XM1014) ? 2 :
                    (wep->slot == CS2D_SLOT_SECONDARY ? 0 : 1);
        float muzzle = (wep->slot == CS2D_SLOT_SECONDARY) ? 22.0f : 30.0f;
        cs2d_fx_shot(&client->fx, pl->pos.x, pl->pos.y, pl->angle, muzzle, hx, hy, hit_wall, shell);
    }
    if (hit < 0) return;
    const cs2d_entity_t *e = &client->game.map.entities[hit];
    cs2d_fx_breakable_hit(&client->fx, hx, hy, e->int_settings[2], pl->angle);
    if (!client->connected) cs2d_ents_damage_breakable(&client->ents, hit, wep->damage);
}

/* Another player's shot or melee swing reported by the server: same recoil / swing / tracer / flash / case as a local shot */
void cs2d_scene_game_remote_shot(cs2d_client_t *client, cs2d_player_t *pl)
{
    if (!pl || !pl->alive) return;
    const cs2d_weapon_def_t *wep = cs2d_weapon_get(pl->current_weapon);
    if (!wep) return;
    bool melee = (wep->slot == CS2D_SLOT_MELEE || pl->current_weapon == CS2D_WEP_WRENCH);

    if (wep->visual_recoil > 0) {
        pl->recoil = (float)wep->visual_recoil + ((float)(rand() & 0x7FFF) / 32768.0f);
    }
    if (melee && pl->current_weapon != CS2D_WEP_CHAINSAW) {
        pl->slash_timer = 8.0f;
        pl->draw_angle  = pl->angle + 6.0f * pl->slash_timer;
        cs2d_fx_knifeslash(&client->fx, pl->pos.x, pl->pos.y, pl->angle);
    }

    float hx, hy, reach = cs2d_weapon_shot_reach(wep);
    int hit = cs2d_ents_trace_shot(&client->ents, pl->pos.x, pl->pos.y, pl->angle, reach, &hx, &hy);
    if (!melee) {
        float ddx = hx - pl->pos.x, ddy = hy - pl->pos.y;
        bool hit_wall = (hit < 0 && sqrtf(ddx * ddx + ddy * ddy) < reach - 1.0f);
        int shell = (pl->current_weapon == CS2D_WEP_M3 || pl->current_weapon == CS2D_WEP_XM1014) ? 2 :
                    (wep->slot == CS2D_SLOT_SECONDARY ? 0 : 1);
        cs2d_fx_shot(&client->fx, pl->pos.x, pl->pos.y, pl->angle, wep->slot == CS2D_SLOT_SECONDARY ? 22.0f : 30.0f,
                     hx, hy, hit_wall, shell);
    }
    if (hit >= 0) cs2d_fx_breakable_hit(&client->fx, hx, hy, client->game.map.entities[hit].int_settings[2], pl->angle);
}

/* Explosion of an Env_Breakable (offline): players and breakables in range take power * (1 - d / range) */
static void breakable_explode(cs2d_client_t *client, const cs2d_entity_t *e)
{
    float cx = (float)e->x * CS2D_TILE_SIZE + 16.0f;
    float cy = (float)e->y * CS2D_TILE_SIZE + 16.0f;
    float range = (float)e->int_settings[4];
    int power = e->int_settings[5];
    if (range <= 0.0f || power <= 0) return;
    for (int i = 1; i <= CS2D_MAX_PLAYERS; i++) {
        cs2d_player_t *p = &client->game.players[i];
        if (!p->alive) continue;
        float dx = p->pos.x - cx, dy = p->pos.y - cy;
        float dist = sqrtf(dx * dx + dy * dy);
        if (dist <= range) cs2d_player_apply_damage(p, (int)((float)power * (1.0f - dist / range)), 0);
    }
    cs2d_ents_explode(&client->ents, cx, cy, range, power);
}

/* Effects of entity logic: break debris both ways; the rest only offline (online the server performs them) */
static void drain_entity_events(cs2d_client_t *client)
{
    cs2d_ent_event_t ev;
    while (cs2d_ents_pop_event(&client->ents, &ev)) {
        const cs2d_entity_t *e = &client->game.map.entities[ev.entity];
        if (ev.type == CS2D_ENT_EVENT_STATE && e->type == CS2D_ENT_ENV_BREAKABLE &&
            client->game.map.entity_state[ev.entity] == 1) {
            cs2d_fx_breakable_break(&client->fx, e);
        } else if (ev.type == CS2D_ENT_EVENT_BREAK && !client->connected && e->int_settings[3]) {
            breakable_explode(client, e);
        } else if (ev.type == CS2D_ENT_EVENT_MESSAGE && e->str_settings[0][0]) {
            cs2d_client_console_print(client, (uint8_t)e->int_settings[2], (uint8_t)e->int_settings[3],
                                      (uint8_t)e->int_settings[4], "%s", e->str_settings[0]);
        } else if (ev.type == CS2D_ENT_EVENT_ACTION && !client->connected) {
            /* Func_GameAction (editor list): 0 draw, 1 T win, 2 CT win, 3 restart in X s,
             * 4 kill all Ts, 5 kill all CTs, 6 kill everyone, 7 load map X */
            int action = e->int_settings[0];
            if (action >= 0 && action <= 2) {
                static const cs2d_team_t winner[3] = { CS2D_TEAM_SPEC, CS2D_TEAM_T, CS2D_TEAM_CT };
                cs2d_game_end_round(&client->game, winner[action], "Func_GameAction");
            } else if (action == 3) {
                cs2d_game_end_round(&client->game, CS2D_TEAM_SPEC, "Round Restart");
                float secs = (float)atof(e->str_settings[0]);
                if (secs > 0.0f) client->game.state_timer = secs;
            } else if (action >= 4 && action <= 6) {
                for (int i = 1; i <= CS2D_MAX_PLAYERS; i++) {
                    cs2d_player_t *p = &client->game.players[i];
                    if (!p->alive) continue;
                    if (action == 6 || (action == 4 && p->team == CS2D_TEAM_T) || (action == 5 && p->team == CS2D_TEAM_CT)) {
                        p->alive  = false;
                        p->health = 0;
                    }
                }
            } else if (action == 7 && e->str_settings[0][0]) {
                cs2d_client_start_game(client, e->str_settings[0]);
            }
        }
    }
}

static void game_enter(cs2d_client_t *client)
{
    /* Joined from the menu: the handshake already runs ([252,4,0] loads the server's map through here) */
    bool joining = client->net_sock != INVALID_SOCKET;

    cs2d_game_init(&client->game, CS2D_MODE_DE);
    load_server_rules(&client->game);

    if (joining) {
        /* Only the local player, with the id the server assigned in [252,2], as Spectator */
        cs2d_game_add_player(&client->game, client->local_player_id, client->config.name, CS2D_TEAM_SPEC);
        client->game.players[client->local_player_id].alive = false;
    } else {
        /* Local player (CT, Player 1) and practice bots exist before the map spawns them */
        cs2d_spec_init(&client->spec, 1);
        client->local_player_id = 1;
        cs2d_game_add_player(&client->game, 1, client->config.name, CS2D_TEAM_CT);
        cs2d_game_add_player(&client->game, 2, "Bot_Bravo", CS2D_TEAM_T);
        cs2d_game_add_player(&client->game, 3, "Bot_Charlie", CS2D_TEAM_T);
    }

    const char *map_name = client->pending_map[0] ? client->pending_map : "de_dust2";
    if (!game_load_map(client, map_name)) {
        cs2d_client_console_print(client, 255, 45, 45, "Error: Unable to load map 'maps/%s.map'", map_name);
        game_load_map(client, "cs_assault"); /* Fallback */
    }

    if (!joining) {
        /* Configure authentic player skins (look 0..3) and starting loadout */
        client->game.players[1].look  = 0; /* ct1.bmp */
        client->game.players[1].money = 16000;
        client->game.players[1].armor = 0;

        client->game.players[2].look = 0; /* t1.bmp */
        cs2d_player_give_weapon(&client->game.players[2], CS2D_WEP_KEVLARHELM);
        cs2d_player_give_weapon(&client->game.players[2], CS2D_WEP_BOMB);
        cs2d_player_give_weapon(&client->game.players[2], CS2D_WEP_AK47);

        client->game.players[3].look = 2; /* t3.bmp */
        cs2d_player_give_weapon(&client->game.players[3], CS2D_WEP_DEAGLE);
    }

    /* Load original sprite assets with exact CS2D magenta color-key maskMode = 2 and 32x32 cell padding */
    cs2d_texture_load_grid(&client->tex_ct,   "gfx/player/ct1.bmp",     2, 32, 32);
    cs2d_texture_load_grid(&client->tex_t,    "gfx/player/t1.bmp",      2, 32, 32);
    cs2d_texture_load_ex(&client->tex_ak47,   "gfx/weapons/ak47.bmp",   2);
    cs2d_texture_load_ex(&client->tex_m4a1,   "gfx/weapons/m4a1.bmp",   2);
    cs2d_texture_load_ex(&client->tex_deagle, "gfx/weapons/deagle.bmp", 2);
    cs2d_texture_load_ex(&s_tex_shade,        "gfx/gui_shade.bmp",      3);

    if (!joining) cs2d_client_connect(client, client->config.server_ip, client->config.server_port);
}

static void game_leave(cs2d_client_t *client)
{
    if (client->net_sock != INVALID_SOCKET) {
        if (client->join_stage >= 1) {
            uint8_t leave_pkt[2] = { (uint8_t)CS2D_NET_LEAVE, 0 };
            cs2d_client_send_udp(client, leave_pkt, sizeof(leave_pkt), true);
        }
        closesocket(client->net_sock);
        client->net_sock = INVALID_SOCKET;
    }
    client->connected  = false;
    client->join_stage = 0;

    cs2d_texture_free(&client->tex_tileset);
    cs2d_texture_free(&client->tex_ct);
    cs2d_texture_free(&client->tex_t);
    cs2d_texture_free(&client->tex_ak47);
    cs2d_texture_free(&client->tex_m4a1);
    cs2d_texture_free(&client->tex_deagle);
    cs2d_texture_free(&s_tex_shade);

    cs2d_game_free(&client->game);
    memset(&client->game, 0, sizeof(client->game));
    client->pending_map[0] = '\0';
}

static void game_update(cs2d_client_t *client, float delta_time)
{
    /* Update game simulation; online the server runs the round rules and only players are advanced */
    if (client->connected) {
        for (int i = 1; i <= CS2D_MAX_PLAYERS; i++) {
            if (client->game.players[i].alive) cs2d_player_update(&client->game.players[i], delta_time);
        }
    } else {
        cs2d_game_update(&client->game, delta_time);
    }

    cs2d_player_t *pl = &client->game.players[client->local_player_id];
    if (pl->alive && !cs2d_client_any_ui_open(client)) {
        /*
         * Aiming: calculate world mouse position and rotation angle.
         * In CS2D.exe (Tpl.draw 0x140121674), player & weapon sprites face
         * North (-Y, Up) at angle = 0 deg, and rotate clockwise (+90 deg = East/+X).
         */
        float world_mx = client->mouse_screen_x + client->cam_x;
        float world_my = client->mouse_screen_y + client->cam_y;
        float dx = world_mx - pl->pos.x;
        float dy = world_my - pl->pos.y;
        pl->angle = atan2f(dx, -dy) * (180.0f / 3.14159265f);
        pl->draw_angle = pl->angle + ((pl->slash_timer > 0.0f) ? (6.0f * pl->slash_timer) : 0.0f);
        client->aim_distance = sqrtf(dx * dx + dy * dy);

        /* Movement (FUN_1402563a0): (2 + weapon bonus) * 1.2 px per 60 Hz frame = 144 px/s; walking halves it.
         * Players are frozen during freeze time in round based game modes. */
        float speed = 2.4f * 60.0f;
        if (client->key_walk) speed *= 0.5f;
        const cs2d_weapon_def_t *cur_wep = cs2d_weapon_get(pl->current_weapon);
        if (cur_wep) speed *= cur_wep->speed_modifier;
        bool frozen = (client->game.round_state == CS2D_ROUND_FREEZETIME &&
                       client->game.gamemode != CS2D_MODE_DM && client->game.gamemode != CS2D_MODE_ZOMBIE);

        float move_x = 0.0f, move_y = 0.0f;
        if (!frozen) {
            if (client->key_forward) move_y -= 1.0f;
            if (client->key_back)    move_y += 1.0f;
            if (client->key_left)    move_x -= 1.0f;
            if (client->key_right)   move_x += 1.0f;
        }

        /* "walkto" test command: steer through the path waypoints like a held movement key */
        while (client->walk_active && !frozen) {
            cs2d_vec2_t tgt = client->walk_path[client->walk_pos];
            float wx = tgt.x - pl->pos.x;
            float wy = tgt.y - pl->pos.y;
            float wd = sqrtf(wx * wx + wy * wy);
            if (wd < 3.0f) {
                if (++client->walk_pos >= client->walk_len) client->walk_active = false;
                continue;
            }
            move_x = wx / wd;
            move_y = wy / wd;
            if (wd < speed * delta_time) speed = wd / delta_time;
            break;
        }

        float len = sqrtf(move_x * move_x + move_y * move_y);
        if (len > 0.001f) {
            float dir_x = move_x / len;
            float dir_y = move_y / len;
            move_x = dir_x * speed * delta_time;
            move_y = dir_y * speed * delta_time;
            cs2d_player_move(pl, &client->game.map, move_x, move_y);

            /* Animate legs (Tpl +0x378 feet_frame, +0x380 feet_angle, +0x388 feet_timer) */
            pl->feet_angle = atan2f(dir_x, -dir_y) * (180.0f / 3.14159265f);
            pl->feet_frame = fmodf(pl->feet_frame + delta_time * (client->key_walk ? 9.0f : 16.0f), 8.0f);
            pl->feet_timer = 0.15f;
        } else if (pl->feet_timer > 0.0f) {
            pl->feet_timer -= delta_time;
            if (pl->feet_timer < 0.0f) pl->feet_timer = 0.0f;
        }

        /* Firing (left-click attack, or right-click attack2 for Knife/Claw per Tpl.attack2 0x1401fdd37) */
        bool melee_atk2 = client->mouse_attack2 &&
                          (pl->current_weapon == CS2D_WEP_KNIFE || pl->current_weapon == CS2D_WEP_CLAW);
        if ((client->mouse_fire || melee_atk2) && cs2d_weapon_fire(pl)) {
            if (melee_atk2 && !client->mouse_fire) {
                pl->fire_cooldown = 21.0f * 0.02f; /* 21 ticks at 50 Hz (0x1401fdc28) */
            }
            fire_shot(client, pl);
        }
    }

    /* Offline the client runs the entity logic itself; online the server does and sends the results */
    if (!client->connected) {
        if (client->ents_round != client->game.round_number) {
            client->ents_round = client->game.round_number;
            cs2d_ents_round_start(&client->ents);
            cs2d_fx_clear(&client->fx);
        }
        cs2d_ents_update(&client->ents, delta_time);
        int tx = (int)(pl->pos.x / CS2D_TILE_SIZE);
        int ty = (int)(pl->pos.y / CS2D_TILE_SIZE);
        if (pl->alive && (tx != client->last_tile_x || ty != client->last_tile_y)) {
            client->last_tile_x = tx;
            client->last_tile_y = ty;
            int tele = cs2d_ents_enter_tile(&client->ents, tx, ty, pl->team);
            if (tele >= 0) {
                const cs2d_entity_t *t = &client->game.map.entities[tele];
                pl->pos.x = (float)t->int_settings[0] * CS2D_TILE_SIZE + 16.0f;
                pl->pos.y = (float)t->int_settings[1] * CS2D_TILE_SIZE + 16.0f;
                client->last_tile_x = t->int_settings[0];
                client->last_tile_y = t->int_settings[1];
            }
        }
    }
    drain_entity_events(client);
    cs2d_fx_update(&client->fx, delta_time);

    /* Camera stays centered on the player like CS2D.exe; a lagging camera would also turn the aim
     * every frame while walking (and send a rotation packet each time) with the mouse at rest.
     * Dead or Spectator: the spectator camera. Its 1 s tick (DAT_140ddf540) also paces [32] below. */
    client->spec_tick += delta_time;
    bool spec_tick = client->spec_tick >= 1.0f;
    if (spec_tick) client->spec_tick = 0.0f;
    if (pl->alive) {
        center_camera_on_local_player(client);
    } else {
        update_spectator_camera(client, delta_time, spec_tick);
    }

    /*
     * Network: like CS2D.exe, every update that changes the position sends it (unreliable)
     *   [10 run / 11 walk, x u16, y u16]
     * and every rotation change sends
     *   [70, angle u16 (deg * 65536 / 360), aim distance / 2 u8]
     * (recorded from the original client through a UDP proxy).
     */
    if (client->connected && client->join_stage == 5 && pl->alive) {
        int px = (int)pl->pos.x, py = (int)pl->pos.y;
        if (px != client->net_sent_x || py != client->net_sent_y) {
            uint8_t pkt[5] = { (uint8_t)(client->key_walk ? CS2D_NET_POS_WALK : CS2D_NET_POS_RUN),
                               (uint8_t)(px & 0xFF), (uint8_t)((px >> 8) & 0xFF),
                               (uint8_t)(py & 0xFF), (uint8_t)((py >> 8) & 0xFF) };
            cs2d_client_send_udp(client, pkt, sizeof(pkt), false);
            client->net_sent_x = px;
            client->net_sent_y = py;
        }
        float a = fmodf(pl->angle, 360.0f);
        if (a < 0.0f) a += 360.0f;
        int rot = (int)(a * 65536.0f / 360.0f) & 0xFFFF;
        if (rot != client->net_sent_rot) {
            int dist = (int)(client->aim_distance * 0.5f);
            if (dist > 255) dist = 255;
            uint8_t pkt[4] = { (uint8_t)CS2D_NET_ROT_CLIENT, (uint8_t)(rot & 0xFF), (uint8_t)(rot >> 8), (uint8_t)dist };
            cs2d_client_send_udp(client, pkt, sizeof(pkt), false);
            client->net_sent_rot = rot;
        }
    }

    /* Dead or Spectator, once per second (FUN_14025a270): [32, x u16, y u16] = camera center + 32768
     * (camx + 33193.0, camy + 33008.0 on the 850x480 screen), unreliable */
    if (client->connected && client->join_stage == 5 && !pl->alive && spec_tick) {
        int sx = ((int)(client->cam_x + (float)client->config.window_width * 0.5f) + 32768) & 0xFFFF;
        int sy = ((int)(client->cam_y + (float)client->config.window_height * 0.5f) + 32768) & 0xFFFF;
        uint8_t pkt[5] = { (uint8_t)CS2D_NET_SPECPOS, (uint8_t)(sx & 0xFF), (uint8_t)(sx >> 8),
                           (uint8_t)(sy & 0xFF), (uint8_t)(sy >> 8) };
        cs2d_client_send_udp(client, pkt, sizeof(pkt), false);
    }

    /* Poll network packets */
    cs2d_client_poll_network(client);
}

/* Weapons lying on the ground (gfx/weapons/<name>_d.bmp, like Env_Item) */
static void render_ground_items(const cs2d_client_t *client)
{
    cs2d_gfx_bank_t *bank = cs2d_render_get_gfx_bank();
    if (!bank) return;
    float cx = roundf(client->cam_x), cy = roundf(client->cam_y);
    for (int i = 0; i < CS2D_MAX_GROUND_ITEMS; i++) {
        const cs2d_ground_item_t *it = &client->ground_items[i];
        if (!it->active || it->type == 0 || it->type >= CS2D_MAX_WEAPONS) continue;
        const cs2d_texture_t *dtex = &bank->tex_weapons_d[it->type];
        if (!dtex->id) continue;
        float dw = (dtex->width > 64) ? (float)(dtex->width / 2) : (float)dtex->width;
        float dh = (dtex->height > 32) ? (float)(dtex->height / 2) : (float)dtex->height;
        cs2d_render_sprite(dtex, it->x - cx, it->y - cy, dw, dh, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    }
}

static void game_render(cs2d_client_t *client, int screen_w, int screen_h)
{
    /* 1. Map Pass 0 & 1 (background, tileset, tile modifiers, ground entities, items, hostages) */
    cs2d_render_map_ex(&client->game.map, &client->tex_tileset, client->cam_x, client->cam_y, screen_w, screen_h);
    render_ground_items(client);

    /* 2. Players (Tpl.draw: shadow, legs, torso frame, armor/helmet, held weapon, objective icon) */
    for (int i = 1; i <= CS2D_MAX_PLAYERS; i++) {
        const cs2d_player_t *p = &client->game.players[i];
        if (p->alive) {
            const cs2d_texture_t *stex = (p->team == CS2D_TEAM_CT) ? &client->tex_ct : &client->tex_t;
            const cs2d_texture_t *wtex = NULL;
            cs2d_gfx_bank_t *wbank = cs2d_render_get_gfx_bank();
            if (wbank && p->current_weapon < CS2D_MAX_WEAPONS && wbank->tex_weapons[p->current_weapon].id) {
                wtex = &wbank->tex_weapons[p->current_weapon]; /* gfx/weapons/<name>.bmp held sprite */
            } else if (p->current_weapon == CS2D_WEP_AK47) wtex = &client->tex_ak47;
            else if (p->current_weapon == CS2D_WEP_M4A1) wtex = &client->tex_m4a1;
            else if (p->current_weapon == CS2D_WEP_DEAGLE) wtex = &client->tex_deagle;

            cs2d_render_player(p, stex, wtex, client->cam_x, client->cam_y);
        }
    }

    /* Breakable debris and explosions */
    cs2d_render_fx(&client->fx, &client->game.map, &client->tex_tileset, client->cam_x, client->cam_y);

    /* 3. Map Pass 2 (Overhead entities: Env_Sprite, Env_Image over players, Env_Object trees/lamps, Shadow Map) */
    cs2d_render_map_overhead(&client->game.map, client->cam_x, client->cam_y, screen_w, screen_h);

    /* 4. In-Game HUD (hud_nums.bmp, hud_symbols.bmp, hud_radar_advanced.png) */
    const cs2d_player_t *local_pl = &client->game.players[client->local_player_id];
    if (local_pl->alive) {
        cs2d_render_hud(local_pl, &client->game, screen_w, screen_h);
    } else {
        render_spectator_hud(client, screen_w, screen_h);
    }
}

/* Weapon slot keys; online the new weapon is announced as [9, type, 0] (recorded from CS2D.exe) */
static void switch_slot(cs2d_client_t *client, cs2d_player_t *pl, cs2d_weapon_slot_t slot)
{
    uint8_t before = pl->current_weapon;
    if (!cs2d_player_switch_weapon(pl, slot) || pl->current_weapon == before) return;
    if (client->connected && client->join_stage == 5) {
        uint8_t pkt[3] = { (uint8_t)CS2D_NET_SELECT_WEAPON, pl->current_weapon, 0 };
        cs2d_client_send_udp(client, pkt, sizeof(pkt), true);
    }
}

static void game_handle_key(cs2d_client_t *client, int key, bool pressed)
{
    cs2d_player_t *pl = &client->game.players[client->local_player_id];
    bool online = client->connected && client->join_stage == 5;

    switch (key) {
        case 'W': case 'w': case VK_UP:
            client->key_forward = pressed;
            break;
        case 'S': case 's': case VK_DOWN:
            client->key_back = pressed;
            break;
        case 'A': case 'a': case VK_LEFT:
            client->key_left = pressed;
            break;
        case 'D': case 'd': case VK_RIGHT:
            client->key_right = pressed;
            break;
        case VK_SHIFT:
            client->key_walk = pressed;
            break;
        case 'R': case 'r':
            /* [16, 1]: the server answers [16, player, 1] and refills the clip with [16, player, 2] */
            if (pressed && cs2d_weapon_reload(pl) && online) {
                uint8_t pkt[2] = { (uint8_t)CS2D_NET_RELOAD, 1 };
                cs2d_client_send_udp(client, pkt, sizeof(pkt), true);
            }
            break;
        case 'B': case 'b':
            if (pressed) {
                /* Buy menu shortcut: primary rifle and kevlar + helmet. Online the server decides:
                 * [23, type u16] -> [23, player, type | 250 refused, money u16, u8] */
                uint8_t buy_wep = (pl->team == CS2D_TEAM_T) ? CS2D_WEP_AK47 : CS2D_WEP_M4A1;
                if (online) {
                    uint8_t pkt1[3] = { (uint8_t)CS2D_NET_BUY, buy_wep, 0 };
                    uint8_t pkt2[3] = { (uint8_t)CS2D_NET_BUY, CS2D_WEP_KEVLARHELM, 0 };
                    cs2d_client_send_udp(client, pkt1, sizeof(pkt1), true);
                    cs2d_client_send_udp(client, pkt2, sizeof(pkt2), true);
                } else {
                    if (!cs2d_player_buy_weapon(pl, buy_wep)) {
                        cs2d_player_buy_weapon(pl, CS2D_WEP_DEAGLE);
                    }
                    cs2d_player_buy_weapon(pl, CS2D_WEP_KEVLARHELM);
                }
            }
            break;
        case 'G': case 'g':
            if (!pressed) break;
            if (online) {
                /* [24, type, clip u16, reserve u16, 0]; the server's drop record removes it */
                const cs2d_weapon_def_t *w = cs2d_weapon_get(pl->current_weapon);
                if (w && w->slot != CS2D_SLOT_MELEE) {
                    int c = pl->clip_ammo[pl->current_weapon], r = pl->reserve_ammo[pl->current_weapon];
                    uint8_t pkt[7] = { (uint8_t)CS2D_NET_DROP, pl->current_weapon,
                                       (uint8_t)(c & 0xFF), (uint8_t)(c >> 8), (uint8_t)(r & 0xFF), (uint8_t)(r >> 8), 0 };
                    cs2d_client_send_udp(client, pkt, sizeof(pkt), true);
                }
            } else {
                cs2d_player_drop_weapon(pl);
            }
            break;
        case 'E': case 'e':
            /* Use key: the server evaluates Trigger_Use online; offline the client does */
            if (pressed && pl->alive) {
                if (client->connected) {
                    /* [26][mode 100 = Trigger_Use][target i32 = 0][x u16][y u16] (FUN_14014f820) */
                    int use = cs2d_ents_find_use_target(&client->ents, pl->pos.x, pl->pos.y, pl->angle, pl->team);
                    if (use >= 0) {
                        const cs2d_entity_t *e = &client->game.map.entities[use];
                        uint8_t use_pkt[10] = { (uint8_t)CS2D_NET_USE, 100, 0, 0, 0, 0,
                                                (uint8_t)(e->x & 0xFF), (uint8_t)(e->x >> 8),
                                                (uint8_t)(e->y & 0xFF), (uint8_t)(e->y >> 8) };
                        cs2d_client_send_udp(client, use_pkt, sizeof(use_pkt), true);
                    }
                } else {
                    cs2d_ents_try_use(&client->ents, pl->pos.x, pl->pos.y, pl->angle, pl->team);
                }
            }
            break;
        case '1':
            if (pressed) switch_slot(client, pl, CS2D_SLOT_PRIMARY);
            break;
        case '2':
            if (pressed) switch_slot(client, pl, CS2D_SLOT_SECONDARY);
            break;
        case '3':
            if (pressed) switch_slot(client, pl, CS2D_SLOT_MELEE);
            break;
        case '4':
            if (pressed) switch_slot(client, pl, CS2D_SLOT_GRENADE);
            break;
        case '5':
            if (pressed) switch_slot(client, pl, CS2D_SLOT_BOMB);
            break;
        case VK_SPACE:    /* sys/controls.cfg: bind "space" "specplayer" */
            if (pressed) cs2d_scene_game_specplayer(client);
            break;
        case VK_LCONTROL: /* bind "leftctrl" "specmode" */
            if (pressed) cs2d_scene_game_specmode(client);
            break;
        default:
            break;
    }
}

static void game_handle_mouse(cs2d_client_t *client, float mx, float my, int button, bool pressed)
{
    if (pressed && !client->game.players[client->local_player_id].alive && spectator_hud_click(client, mx, my, button)) {
        return;
    }
    if (button == 1) {        /* Left click: attack */
        client->mouse_fire = pressed;
    } else if (button == 2) { /* Right click: attack2 */
        client->mouse_attack2 = pressed;
    }
}

static void game_handle_escape(cs2d_client_t *client)
{
    /* Leaving the game disconnects and returns to the main menu */
    cs2d_client_set_scene(client, CS2D_SCENE_LOBBY);
    cs2d_client_console_print(client, 255, 220, 0, "Disconnected from server.");
}

const cs2d_scene_t cs2d_scene_game = {
    .name          = "Game",
    .enter         = game_enter,
    .leave         = game_leave,
    .update        = game_update,
    .render        = game_render,
    .handle_key    = game_handle_key,
    .handle_mouse  = game_handle_mouse,
    .handle_escape = game_handle_escape
};
