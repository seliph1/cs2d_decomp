#include "cs2d_client.h"
#include "cs2d_weapons.h"
#include "cs2d_player.h"
#include "cs2d_game.h"
#include "cs2d_network.h"
#include "cs2d_scoreboard.h"
#include "cs2d_editor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "cs2d_light.h"
#include "cs2d_atlas.h"
#include <SDL3/SDL.h>

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
static cs2d_texture_t s_tex_shade_c; /* gfx/hud_shade_c.bmp 250x20, maskMode 3, top-left handle (0x140a0dd28) */
static cs2d_texture_t s_tex_gui_c;  /* gfx/gui_c.bmp: 4 rounded corners of 16x16, maskMode 3 (0x140a0dea0) */

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

/*
 * Spectator minimap (FUN_140299d70): the editor's 1 px per tile pixmap (cs2d_ed_minimap_build, no entities) scaled by
 * 5 / 4 / 3 / 2 / 1 for a largest map index below 21 / 31 / 61 / 121 / above, in the top right corner below the band.
 * Shown while the "Map" button flag DAT_140a15478 is set (1 in .data, never reset; the TSmartMinimap variant behind
 * DAT_140ddf195 is not reconstructed). With sv_specmode 1 a held left button on it switches to free look and moves
 * the camera there.
 */
static bool           s_spec_map = true;   /* DAT_140a15478 */
static bool           s_spec_map_drag;
static cs2d_texture_t s_tex_spec_map;
static uint32_t      *s_spec_map_px, *s_spec_map_prev;
static size_t         s_spec_map_cap;

static int spec_map_scale(const cs2d_map_t *map)
{
    int m = (int)(map->width > map->height ? map->width : map->height) - 1;
    return m < 21 ? 5 : m < 31 ? 4 : m < 61 ? 3 : m < 121 ? 2 : 1;
}

/* x = screen width - (map_xmax - 1) * scale (the image overhangs the right edge by two tiles, as in the original) */
static float spec_map_x(const cs2d_client_t *client)
{
    const cs2d_map_t *map = &client->game.map;
    return (float)client->config.window_width - (float)(((int)map->width - 2) * spec_map_scale(map));
}

static bool spec_map_hovered(const cs2d_client_t *client)
{
    const cs2d_map_t *map = &client->game.map;
    if (!s_spec_map || client->spec.server_mode != 1 || !map->tiles || spec_menu_open(client)) return false;
    return client->mouse_screen_y <= SPEC_BAR_H + (float)(map->height * (uint32_t)spec_map_scale(map)) &&
           client->mouse_screen_x >= spec_map_x(client);
}

/* camx = ((mx - x) / scale - 13) * 32 - 16, camy = ((my - 40) / scale - 6) * 32 - 16 */
static void spec_map_navigate(cs2d_client_t *client)
{
    float sc = (float)spec_map_scale(&client->game.map);
    client->spec.mode = CS2D_SPEC_FREE; /* DAT_140ddf2f8 = 1, no [67] */
    client->cam_x = (float)(int)(((client->mouse_screen_x - spec_map_x(client)) / sc - 13.0f) * 32.0f - 16.0f);
    client->cam_y = (float)(int)(((client->mouse_screen_y - 40.0f) / sc - 6.0f) * 32.0f - 16.0f);
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
        if (s_spec_map_drag && spec_map_hovered(client)) {
            spec_map_navigate(client);
            cx = (int)client->cam_x;
            cy = (int)client->cam_y;
        }
        /* Own Team only: a dead target is replaced by the next player (HUD, 0x14029b574) */
        const cs2d_player_t *t = spec_target(client);
        if (s->server_mode == 2 && t && !t->alive) spec_pick(client, 1);
    }
    client->cam_x = (float)cx;
    client->cam_y = (float)cy;
}

static bool respawn_mode(const cs2d_client_t *client);
static int  respawn_wait(const cs2d_client_t *client);

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

/* FUN_140299d70: pixmap at alpha 0.75, a white frame, living players as scale-sized dots, then the 27x15 tile camera
 * view in additive yellow at |sin(ms / 10 deg)| * 0.2. Hostages, flags, bombs and Info_Dom_Point dots are not drawn. */
static void render_spec_map(cs2d_client_t *client)
{
    const cs2d_map_t *map = &client->game.map;
    if (!map->tiles || map->width == 0 || map->height == 0) return;
    size_t n = (size_t)map->width * map->height;
    if (n > s_spec_map_cap) {
        free(s_spec_map_px);
        free(s_spec_map_prev);
        s_spec_map_px = (uint32_t *)malloc(n * 4);
        s_spec_map_prev = (uint32_t *)calloc(n, 4);
        s_spec_map_cap = (s_spec_map_px && s_spec_map_prev) ? n : 0;
        cs2d_texture_free(&s_tex_spec_map);
        if (!s_spec_map_cap) return;
    }
    cs2d_ed_minimap_build(map, false, s_spec_map_px);
    if (!s_tex_spec_map.id || s_tex_spec_map.width != (int)map->width || s_tex_spec_map.height != (int)map->height ||
        memcmp(s_spec_map_px, s_spec_map_prev, n * 4) != 0) {
        cs2d_texture_upload_rgba(&s_tex_spec_map, s_spec_map_px, (int)map->width, (int)map->height);
        memcpy(s_spec_map_prev, s_spec_map_px, n * 4);
    }

    const cs2d_spec_t *sp = &client->spec;
    float sc = (float)spec_map_scale(map), x0 = spec_map_x(client), y0 = SPEC_BAR_H;
    float w = (float)map->width * sc, h = (float)map->height * sc;
    cs2d_render_sprite_frame(&s_tex_spec_map, (int)map->width, (int)map->height, 0, x0 + w * 0.5f, y0 + h * 0.5f, w, h,
                             0.0f, 1.0f, 1.0f, 1.0f, 0.75f, 0);
    cs2d_render_rect(x0, y0, w, h, 1.0f, 1.0f, 1.0f, 0.75f, false);

    if (sp->server_mode != 0) {
        /* Colors (0x14029a150): T (255,25,0), CT (50,150,255), green without teams. Under sv_specmode 2 a team
         * member sees only its side; the per-player fog test is not reconstructed */
        bool teams = client->net_sv_gamemode != 1;
        int lt = (int)client->game.players[client->local_player_id].team;
        for (int i = 1; i <= CS2D_MAX_PLAYERS; i++) {
            const cs2d_player_t *pl = &client->game.players[i];
            if (!pl->alive || pl->health <= 0 || pl->team == CS2D_TEAM_SPEC) continue;
            float r = 0.0f, g = 1.0f, b = 0.0f;
            bool is_t = pl->team == CS2D_TEAM_T;
            if (teams) {
                if (sp->server_mode != 1 && lt != CS2D_TEAM_SPEC && (lt == CS2D_TEAM_T) != is_t) continue;
                if (is_t) { r = 1.0f; g = 25.0f / 255.0f; b = 0.0f; }
                else      { r = 50.0f / 255.0f; g = 150.0f / 255.0f; b = 1.0f; }
            }
            float px = floorf(pl->pos.x / 32.0f) * sc + x0, py = floorf(pl->pos.y / 32.0f) * sc + y0;
            cs2d_render_rect(px, py, sc, sc, r, g, b, 0.75f, true);
        }
    }

    float a = (float)fabs(sin((double)(GetTickCount() / 10) * 0.017453292519943295)) * 0.2f;
    float vx = floorf(client->cam_x / 32.0f) * sc + x0, vy = floorf(client->cam_y / 32.0f) * sc + y0;
    cs2d_render_rect_add(vx, vy, 27.0f * sc, 15.0f * sc, 1.0f, 1.0f, 0.0f, a, true);
}

/* Spectator HUD (FUN_14029ac10): bands, "Spectating", Menu / Map / Fog, name box and mode button */
/*
 * The small round timer and money of the spectator HUD, in the top band (FUN_14029ac10 at 0x14029c5xx), sized by
 * TextHeight("0") and TextWidth("ITEM:0000"); hud_symbols (0x140a0dd50) frames are drawn additive:
 * - mp_hud 4 (time): (155, 155, 155) frame 2 (clock; 6 = C4 while the bomb is planted) at (w - TextWidth, 2),
 *   TextHeight + 4 square; mp_roundtime 100 without a bomb shows frame 11 (infinity) at w - 17, else "M:SS" right
 *   aligned at w - 2, y 4.
 * - mp_hud 16 (money): (150, 150, 150) frame 4 (cart) at (w - TextWidth + 2, 40 - TextHeight - 2), TextHeight + 2
 *   square; the local player's money right aligned at w - 2, y 40 - TextHeight - 1.
 * Assumed: mp_hud bits on, DAT_140ddf1cc 0 and the game mode table 0x1409f77e8 1 (like our main HUD timer); the text
 * is drawn with alpha blending (the original keeps the additive blend for it).
 */
static void render_spectator_time_money(cs2d_client_t *client, int screen_w)
{
    cs2d_gfx_bank_t *bank = cs2d_render_get_gfx_bank();
    if (!bank || !bank->tex_hud_symbols.id) return;
    const cs2d_game_t *game = &client->game;
    const cs2d_font_id_t f = CS2D_FONT_REGULAR;
    int cell = bank->tex_hud_symbols.height >= 64 ? 64 : 32;
    float tw0, th;
    cs2d_render_text_measure(f, "0", &tw0, &th);
    int text_h = (int)th, ref_w = (int)cs2d_render_text_width_ex(f, "ITEM:0000");
    char buf[32];

    float sz = (float)(text_h + 4), g = 155.0f / 255.0f;
    int frame = game->bomb_planted ? 6 : 2;
    cs2d_render_sprite_frame(&bank->tex_hud_symbols, cell, cell, frame, (float)(screen_w - ref_w) + sz * 0.5f,
                             2.0f + sz * 0.5f, sz, sz, 0.0f, g, g, g, 1.0f, 4);
    if (!game->bomb_planted && game->cfg_roundtime == 6000.0f) {
        cs2d_render_sprite_frame(&bank->tex_hud_symbols, cell, cell, 11, (float)(screen_w - 17) + sz * 0.5f,
                                 2.0f + sz * 0.5f, sz, sz, 0.0f, g, g, g, 1.0f, 4);
    } else {
        int sec = (int)(game->bomb_planted ? game->c4_time : game->round_time);
        if (sec < 0) sec = 0;
        snprintf(buf, sizeof(buf), "%d:%02d", sec / 60, sec % 60);
        float x = (float)(screen_w - 2 - (int)cs2d_render_text_width_ex(f, buf));
        cs2d_render_text_ex(f, x, 4.0f, buf, g, g, g, 1.0f);
    }

    const cs2d_player_t *me = &client->game.players[client->local_player_id];
    if (me->id == 0) return;
    sz = (float)(text_h + 2);
    g = 150.0f / 255.0f;
    cs2d_render_sprite_frame(&bank->tex_hud_symbols, cell, cell, 4, (float)(screen_w - ref_w + 2) + sz * 0.5f,
                             SPEC_BAR_H - (float)text_h - 2.0f + sz * 0.5f, sz, sz, 0.0f, g, g, g, 1.0f, 4);
    snprintf(buf, sizeof(buf), "%d", me->money);
    float x = (float)(screen_w - 2 - (int)cs2d_render_text_width_ex(f, buf));
    cs2d_render_text_ex(f, x, SPEC_BAR_H - (float)text_h - 1.0f, buf, g, g, g, 1.0f);
}

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
    if (s_spec_map) render_spec_map(client);

    /* A dead team member: "You are dead!" at the top. Respawn game modes (DM, TDM, Construction, Zombies)
     * pulse yellow-red and respawn on a click ([39]), after "Respawn possible in $1 sec" in (200, 0, 0) while
     * mp_respawndelay runs (FUN_14029ac10 at 0x14029c3xx) */
    if (me->team != CS2D_TEAM_SPEC) {
        double t = (double)GetTickCount();
        const char *msg;
        char wait_msg[64];
        float r, g;
        int wait = respawn_mode(client) ? respawn_wait(client) : 0;
        if (respawn_mode(client) && wait > 0) {
            snprintf(wait_msg, sizeof(wait_msg), "You are dead! - Respawn possible in %d sec", wait); /* 0767 */
            msg = wait_msg;
            r = 200.0f / 255.0f;
            g = 0.0f;
        } else if (respawn_mode(client)) {
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
    render_spectator_time_money(client, screen_w);

    /* Menu, Map and Fog (the last only with sv_fow and sv_specmode 1); Map toggles the minimap, the Menu and Fog
     * windows are not reconstructed */
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

    if (button == 1 && spec_map_hovered(client)) {
        s_spec_map_drag = true;
        spec_map_navigate(client);
        return true;
    }
    if (point_in_rect(mx, my, 56.0f, by, 37.0f, SPEC_BTN_H)) {
        s_spec_map = !s_spec_map; /* The original also plays a GUI sound (0x140a0d628 / 0x140a0d630) */
        return true;
    }
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
    /* Loading screen steps (strings of cs2d_load_map 0x1401da1e0); the original's PROCESS_MAPS step count
     * is not recovered, so progress is our own step index / LOAD_STEPS */
    enum { LOAD_STEPS = 6 };
    cs2d_render_loadscreen("Loading Map", 0.0f);
    char map_path[128];
    snprintf(map_path, sizeof(map_path), "maps/%s.map", map_name);
    cs2d_render_loadscreen("Loading Map Header", 1.0f / LOAD_STEPS);
    if (!cs2d_game_load_map(&client->game, map_path)) return false;
    snprintf(client->game_map_name, sizeof(client->game_map_name), "%s", map_name);

    cs2d_render_loadscreen("Loading Tileset", 2.0f / LOAD_STEPS);
    cs2d_texture_free(&client->tex_tileset);
    cs2d_texture_load_tileset(&client->tex_tileset, client->game.map.tileset);
    cs2d_render_invalidate_shadows();
    cs2d_render_loadscreen("Initializing Lighting", 3.0f / LOAD_STEPS);
    cs2d_light_load_map(&client->game.map);
    cs2d_render_loadscreen("Caching Entities", 4.0f / LOAD_STEPS);
    cs2d_gfx_atlas_load_map(cs2d_render_get_gfx_bank(), &client->game.map);
    cs2d_render_loadscreen("Loading Entities", 5.0f / LOAD_STEPS);
    cs2d_game_start_round(&client->game);

    /* Entity logic: offline the client runs it (round start fires every Trigger_Start);
     * online the join snapshot replaces these states with the server's */
    cs2d_ents_init(&client->ents, &client->game.map);
    cs2d_ents_round_start(&client->ents);
    cs2d_fx_clear(&client->fx);
    cs2d_gen_init(&client->gen, &client->game.map);
    memset(client->ground_items, 0, sizeof(client->ground_items));

    /* Dynamic objects: offline the map's Env_Building entities (the server spawns them at round start);
     * online the join snapshot [252,7,5] and [49] bring the server's */
    cs2d_dynobj_init(&client->dynobj, &client->game.map);
    if (client->net_sock == INVALID_SOCKET) cs2d_dynobj_spawn_map_buildings(&client->dynobj);
    client->build_mode = client->build_menu_open = false;
    client->buy.menu   = 0;
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
    cs2d_render_loadscreen("Loading Entities", 1.0f);

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
    cs2d_dynobj_clear(&client->dynobj);
    cs2d_dynobj_spawn_map_buildings(&client->dynobj);
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

static bool team_game(const cs2d_client_t *client);

/*
 * Debris where a shot reaches a building (Tshoot.Shoot 0x1402073ed, run for every player's shot): the ray
 * from the shooter to (hx, hy) (the trace stops inside solid buildings, Barricades let it through) checks
 * each tile it enters for an object of category 1 or 2 within 457 x 272 px of the shooter. A Wrench on an
 * own building (Tdo.sameteam 0x140005460) repairs it instead; other hits throw Tp.breakable material 1.
 */
static void building_hit_debris(cs2d_client_t *client, const cs2d_player_t *pl, const cs2d_weapon_def_t *wep,
                                float hx, float hy)
{
    if (wep->damage <= 0) return;
    float dx = hx - pl->pos.x, dy = hy - pl->pos.y;
    float dist = sqrtf(dx * dx + dy * dy);
    if (dist < 1.0f) dist = 1.0f;
    float ux = dx / dist, uy = dy / dist;
    int last_tx = -1, last_ty = -1;
    for (float d = 0.0f; d <= dist + 1.0f; d += 1.0f) { /* + 1: the trace ends on the first pixel of a solid tile */
        float px = pl->pos.x + ux * d, py = pl->pos.y + uy * d;
        if (px < 0.0f || py < 0.0f) return;
        int tx = (int)(px * 0.03125f), ty = (int)(py * 0.03125f);
        if (tx == last_tx && ty == last_ty) continue;
        last_tx = tx;
        last_ty = ty;
        const cs2d_dynobj_t *o = cs2d_dynobj_at_tile(&client->dynobj, tx, ty);
        if (!o || !cs2d_dynobj_hit_debris(o->typ)) continue;
        if (fabsf(o->x - pl->pos.x) > 457.0f || fabsf(o->y - pl->pos.y) > 272.0f) continue;
        if (pl->current_weapon == CS2D_WEP_WRENCH) {
            bool same = team_game(client) ? ((pl->team == CS2D_TEAM_T && o->team == 1) || (pl->team > CS2D_TEAM_T && o->team > 1))
                                          : (o->player == pl->id);
            if (same) continue;
        }
        cs2d_fx_building_hit(&client->fx, px, py, pl->angle);
    }
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

    /* Projectile weapons: throwables (grenades, flare, molotov, gutbomb, etc.) and rockets */
    if (cs2d_projectile_is_throwable(pl->current_weapon) || cs2d_projectile_is_rocket(pl->current_weapon)) {
        float world_mx = client->mouse_screen_x + client->cam_x;
        float world_my = client->mouse_screen_y + client->cam_y;
        float dx = world_mx - pl->pos.x;
        float dy = world_my - pl->pos.y;
        float dist = sqrtf(dx * dx + dy * dy);
        if (dist > 385.0f && cs2d_projectile_is_throwable(pl->current_weapon)) {
            dist = 385.0f; /* 0x181 = 385 px max throw distance in CS2D.exe FUN_140217d80 */
        }
        float dir = pl->angle;
        float def_speed, def_lifetime, def_flydist;
        cs2d_projectile_get_default_properties(pl->current_weapon, &def_speed, &def_lifetime, &def_flydist);
        float flydist = cs2d_projectile_is_throwable(pl->current_weapon) ? dist : def_flydist;

        cs2d_projectile_spawn(&client->projectiles, pl->id, pl->current_weapon,
                              pl->pos.x, pl->pos.y, dir, flydist, def_speed, def_lifetime);

        if (client->connected && client->join_stage == 5) {
            /* Client -> Server: [27, type, x u16, y u16, dir u16, flydist u16] (10 bytes) */
            uint16_t x_u16 = (uint16_t)(pl->pos.x < 0.0f ? 0 : (pl->pos.x > 65535.0f ? 65535 : (int)pl->pos.x));
            uint16_t y_u16 = (uint16_t)(pl->pos.y < 0.0f ? 0 : (pl->pos.y > 65535.0f ? 65535 : (int)pl->pos.y));
            float norm_dir = fmodf(fmodf(dir, 360.0f) + 360.0f, 360.0f);
            uint16_t dir_u16 = (uint16_t)(norm_dir * (65536.0f / 360.0f));
            uint16_t fly_u16 = (uint16_t)(flydist < 0.0f ? 0 : (flydist > 65535.0f ? 65535 : (int)flydist));
            uint8_t pkt[10];
            pkt[0] = (uint8_t)CS2D_NET_PROJECTILE;
            pkt[1] = pl->current_weapon;
            pkt[2] = (uint8_t)(x_u16 & 0xFF);
            pkt[3] = (uint8_t)((x_u16 >> 8) & 0xFF);
            pkt[4] = (uint8_t)(y_u16 & 0xFF);
            pkt[5] = (uint8_t)((y_u16 >> 8) & 0xFF);
            pkt[6] = (uint8_t)(dir_u16 & 0xFF);
            pkt[7] = (uint8_t)((dir_u16 >> 8) & 0xFF);
            pkt[8] = (uint8_t)(fly_u16 & 0xFF);
            pkt[9] = (uint8_t)((fly_u16 >> 8) & 0xFF);
            cs2d_client_send_udp(client, pkt, sizeof(pkt), true);
        }

        /* Throwables disappear from inventory after throw; only Flashbang starts with 2 charges */
        if (cs2d_projectile_is_throwable(pl->current_weapon)) {
            cs2d_player_t *mutable_pl = &client->game.players[client->local_player_id];
            if (pl->current_weapon == CS2D_WEP_FLASHBANG && mutable_pl->reserve_ammo[CS2D_WEP_FLASHBANG] > 0) {
                mutable_pl->clip_ammo[CS2D_WEP_FLASHBANG] = 1;
                mutable_pl->reserve_ammo[CS2D_WEP_FLASHBANG]--;
            } else {
                cs2d_player_strip_weapon(mutable_pl, pl->current_weapon);
            }
            return;
        }

        /* For rockets (ammo of rocket launcher / RPG), continue to gun recoil and tracer below */
    }

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
    building_hit_debris(client, pl, wep, hx, hy);
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

    if (cs2d_projectile_is_rocket(pl->current_weapon)) {
        float def_speed, def_lifetime, def_flydist;
        cs2d_projectile_get_default_properties(pl->current_weapon, &def_speed, &def_lifetime, &def_flydist);
        cs2d_projectile_spawn(&client->projectiles, pl->id, pl->current_weapon,
                              pl->pos.x, pl->pos.y, pl->angle, def_flydist, def_speed, def_lifetime);
    }

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
    building_hit_debris(client, pl, wep, hx, hy);
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
    memset(&client->invsel, 0, sizeof(client->invsel));

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
        client->game.players[2].bot = client->game.players[3].bot = true;
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
    cs2d_texture_load_ex(&s_tex_shade_c,      "gfx/hud_shade_c.bmp",    3);
    cs2d_hudmen_load();

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
    cs2d_texture_free(&s_tex_shade_c);
    cs2d_hudmen_unload();

    cs2d_game_free(&client->game);
    memset(&client->game, 0, sizeof(client->game));
    cs2d_projectile_clear(&client->projectiles);
    client->pending_map[0] = '\0';
    client->chat_open = false;
    client->chat_just_opened = false;
    client->chat_input[0] = '\0';
    client->chat_cursor = 0;
}

static void update_dynobjs(cs2d_client_t *client, float delta_time);

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

    cs2d_scoreboard_tick(client->game.players, delta_time);

    cs2d_player_t *pl = &client->game.players[client->local_player_id];
    /* Weapon selection HUD: closed while dead; the icons of the open slot turn 3 degrees per 60 Hz frame
     * (FUN_140295190: Titem +0x28 += time factor * 3) */
    if (!pl->alive) {
        client->invsel.slot = 0;
    } else if (client->invsel.slot != 0) {
        int n = cs2d_invsel_count(pl, client->invsel.slot);
        for (int i = 0; i < n; i++) {
            uint8_t w = cs2d_invsel_item(pl, client->invsel.slot, i);
            client->invsel.rot[w] = fmodf(client->invsel.rot[w] + 3.0f * delta_time * 60.0f, 360.0f);
        }
    }
    /* Buy menu section preview: Tgui_hudmen.rot += g_dt * 3 */
    if (client->buy.menu) client->buy.rot = fmodf(client->buy.rot + 3.0f * delta_time * 60.0f, 360.0f);
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
        if (!cs2d_client_any_ui_open(client) && (client->mouse_fire || melee_atk2) && !client->build_menu_open && cs2d_weapon_fire(pl)) {
            if (melee_atk2 && !client->mouse_fire) {
                pl->fire_cooldown = 1.2f; /* Tpl.attack2 knife / claw: firetimer.reset(1200) (0x1401fdde8) */
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
            cs2d_dynobj_clear(&client->dynobj);
            cs2d_dynobj_spawn_map_buildings(&client->dynobj);
            cs2d_projectile_clear(&client->projectiles);
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
    update_dynobjs(client, delta_time);
    cs2d_fx_update(&client->fx, delta_time);
    cs2d_gen_update(&client->gen, &client->game.map, client->cam_x, client->cam_y,
                    (float)client->config.window_width * 0.5f, (float)client->config.window_height * 0.5f, delta_time);
    cs2d_projectile_update(client, delta_time);

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

/* ---------------------------------------------------------------------------
 * Construction game mode: dynamic objects, build helper, "Construct" menu, respawn
 * --------------------------------------------------------------------------- */

/* Respawn game modes (0x1409f57b8[sv_gamemode]): Deathmatch, Team Deathmatch, Construction, Zombies */
static bool respawn_mode(const cs2d_client_t *client)
{
    return client->net_sv_gamemode >= 1 && client->net_sv_gamemode <= 4;
}

/* Team colors for buildings (0x140a015d0[sv_gamemode]): every mode but Deathmatch is a team game (assumed) */
static bool team_game(const cs2d_client_t *client)
{
    return client->net_sv_gamemode != 1;
}

/* Turret animation; each burst frame shows a tracer from every barrel (Tdo.shoot, method +0x120) */
static void update_dynobjs(cs2d_client_t *client, float delta_time)
{
    cs2d_dynobj_update(&client->dynobj, client->game.players, delta_time * 60.0f);
    for (int i = 0; i < CS2D_DYNOBJ_MAX; i++) {
        cs2d_dynobj_t *o = &client->dynobj.objs[i];
        if (!o->active || o->shots <= 0) continue;
        int barrels = (o->typ == CS2D_DO_TRIPLE_TURRET) ? 3 : (o->typ == CS2D_DO_DUAL_TURRET ? 2 : 1);
        float cx = o->x + 16.0f, cy = o->y + 16.0f;
        for (; o->shots > 0; o->shots--) {
            for (int b = 0; b < barrels; b++) {
                /* Dual: +-6 px beside the center; Triple: center and +-8 px (FUN_14028c630) */
                float side = (barrels == 2) ? (b ? -6.0f : 6.0f) : (barrels == 3 ? (b == 0 ? 0.0f : (b == 1 ? 8.0f : -8.0f)) : 0.0f);
                double a = (double)(o->rot + 90.0f) * 0.017453292519943295;
                float bx = cx + (float)sin(a) * side, by = cy - (float)cos(a) * side;
                /* Reach of the turret weapon (0xfd) not recovered: 300 px (calibrated like the rifles) */
                float hx, hy;
                int hit = cs2d_ents_trace_shot(&client->ents, bx, by, o->rot, 300.0f, &hx, &hy);
                float ddx = hx - bx, ddy = hy - by;
                cs2d_fx_shot(&client->fx, bx, by, o->rot, 16.0f, hx, hy, hit < 0 && sqrtf(ddx * ddx + ddy * ddy) < 299.0f, 1);
            }
        }
    }
}

/* Tile under the cursor */
static void cursor_tile(const cs2d_client_t *client, int *tx, int *ty)
{
    *tx = (int)((client->cam_x + client->mouse_screen_x) * 0.03125f);
    *ty = (int)((client->cam_y + client->mouse_screen_y) * 0.03125f);
}

static bool local_can_build_mode(const cs2d_client_t *client)
{
    const cs2d_player_t *pl = &client->game.players[client->local_player_id];
    return pl->alive && pl->current_weapon == CS2D_WEP_WRENCH;
}

/*
 * Tpl.attack2 with the Wrench (0x1401fd6c0): the first right click shows the build helper; the second one
 * checks the tile (FUN_14014ef60) and opens the "Construct" menu, or shows 0694 / 0695 in red.
 */
static void wrench_attack2(cs2d_client_t *client)
{
    cs2d_player_t *pl = &client->game.players[client->local_player_id];
    if (!client->build_mode) {
        client->build_mode = true;
        return;
    }
    client->build_mode = false;
    client->build_rot  = pl->angle;
    cursor_tile(client, &client->build_tx, &client->build_ty);
    int ok = cs2d_dynobj_can_build(&client->dynobj, client->game.players, client->local_player_id,
                                   (int)(pl->pos.x * 0.03125f), (int)(pl->pos.y * 0.03125f),
                                   client->build_tx, client->build_ty, false);
    if (ok == 1) {
        client->build_menu_open = true;
    } else {
        cs2d_client_center_msg(client, 255, 0, 0, ok == 0 ? "you can't build here"                                /* 0694 */
                                                          : "you can't build here - other players are too close"); /* 0695 */
    }
}

/*
 * Build request (FUN_1401fab30 with param_10 = 1): [34, type, tile x u16, tile y u16, mode, rot + 1000 u16],
 * reliable. Only Barbed Wire passes the player's rotation; the "X" buttons send tile 0xffff / 0xffff to
 * remove the player's building of that type.
 */
static void send_build(cs2d_client_t *client, int typ, int tx, int ty, float rot)
{
    client->build_menu_open = false;
    if (!client->connected || client->join_stage != 5) return;
    int r = (int)(rot + 1000.0f) & 0xFFFF;
    uint8_t pkt[9] = { (uint8_t)CS2D_NET_BUILD, (uint8_t)typ, (uint8_t)(tx & 0xFF), (uint8_t)((tx >> 8) & 0xFF),
                       (uint8_t)(ty & 0xFF), (uint8_t)((ty >> 8) & 0xFF), 0, (uint8_t)(r & 0xFF), (uint8_t)(r >> 8) };
    cs2d_client_send_udp(client, pkt, sizeof(pkt), true);
}

/*
 * "Construct" menu (Tgui_hudmen menu 60, 0x140228xxx): the hud menu window (125, 20) 600x440 on 850x480
 * (0x1401aba29), 240 px buttons at window + (16, y); "X" buttons 25 px wide at x 261 / 559.
 */
typedef struct { int typ, key, x, y; bool remove_btn; } build_btn_t;
static const build_btn_t s_build_btns[] = {
    { CS2D_DO_BARRICADE,     1,  16,  30, false }, { CS2D_DO_BARBED_WIRE,  2,  16,  60, false },
    { CS2D_DO_WALL_1,        3,  16, 110, false }, { CS2D_DO_WALL_2,       4,  16, 140, false },
    { CS2D_DO_WALL_3,        5,  16, 170, false }, { CS2D_DO_GATE_FIELD,   6,  16, 220, true  },
    { CS2D_DO_TURRET,        7,  16, 250, true  }, { CS2D_DO_DISPENSER,    8,  16, 280, true  },
    { CS2D_DO_TELE_ENTRANCE, -1, 314, 280, true  }, { CS2D_DO_SUPPLY,       9,  16, 310, true  },
    { CS2D_DO_TELE_EXIT,     -1, 314, 310, true  },
};

static void build_menu_origin(const cs2d_client_t *client, float *wx, float *wy)
{
    cs2d_hudmen_origin(client->config.window_width, client->config.window_height, wx, wy);
}

static void render_build_menu(cs2d_client_t *client)
{
    float wx, wy, mx = client->mouse_screen_x, my = client->mouse_screen_y;
    build_menu_origin(client, &wx, &wy);
    cs2d_hudmen_window(wx, wy, "Construct"); /* 0372 */
    for (size_t i = 0; i < sizeof(s_build_btns) / sizeof(s_build_btns[0]); i++) {
        const build_btn_t *b = &s_build_btns[i];
        const cs2d_dynobj_def_t *d = cs2d_dynobj_def(b->typ);
        char price[16];
        snprintf(price, sizeof(price), "%d$", d->price);
        cs2d_hudmen_button(mx, my, wx + b->x, wy + b->y, 240.0f, b->key, d->name, price, true, NULL);
        if (b->remove_btn) cs2d_hudmen_fbutton(mx, my, wx + b->x + 245.0f, wy + b->y, 25.0f, "X", true);
    }
    cs2d_hudmen_button(mx, my, wx + 16.0f, wy + 440.0f - 46.0f, 240.0f, 0, "Cancel", "", true, NULL); /* 0027 */
}

/* Clicks in the "Construct" menu; returns true when the click belongs to the menu */
static bool build_menu_click(cs2d_client_t *client, float mx, float my)
{
    float wx, wy;
    build_menu_origin(client, &wx, &wy);
    for (size_t i = 0; i < sizeof(s_build_btns) / sizeof(s_build_btns[0]); i++) {
        const build_btn_t *b = &s_build_btns[i];
        if (point_in_rect(mx, my, wx + b->x, wy + b->y, 240.0f, 25.0f)) {
            send_build(client, b->typ, client->build_tx, client->build_ty,
                       b->typ == CS2D_DO_BARBED_WIRE ? client->build_rot : 0.0f);
            return true;
        }
        if (b->remove_btn && point_in_rect(mx, my, wx + b->x + 245.0f, wy + b->y, 25.0f, 25.0f)) {
            send_build(client, b->typ, 0xFFFF, 0xFFFF, 0.0f);
            return true;
        }
    }
    if (point_in_rect(mx, my, wx + 16.0f, wy + 394.0f, 240.0f, 25.0f)) client->build_menu_open = false;
    return true; /* The menu is modal: clicks never reach the game */
}

/* Number keys in the "Construct" menu (Tgui_hudmen.button: DAT_140ddf340 == key) */
static void build_menu_key(cs2d_client_t *client, int digit)
{
    if (digit == 0) { client->build_menu_open = false; return; }
    for (size_t i = 0; i < sizeof(s_build_btns) / sizeof(s_build_btns[0]); i++) {
        const build_btn_t *b = &s_build_btns[i];
        if (b->key == digit) {
            send_build(client, b->typ, client->build_tx, client->build_ty,
                       b->typ == CS2D_DO_BARBED_WIRE ? client->build_rot : 0.0f);
            return;
        }
    }
}

/* ---------------------------------------------------------------------------
 * Buy menu (Tgui_hudmen menus 20..26, cs2d_hudmen.c)
 * --------------------------------------------------------------------------- */

static cs2d_buyctx_t buy_ctx(const cs2d_client_t *client)
{
    const cs2d_player_t *me = &client->game.players[client->local_player_id];
    cs2d_buyctx_t ctx = { (int)me->team, me->money, false };
    /* DAT_140ddf078: Info_BombSpot entities counted at map load (0x1401d0000 "Counting Map Mission Entities") */
    for (uint32_t i = 0; i < client->game.map.entity_count; i++) {
        if (client->game.map.entities[i].type == CS2D_ENT_INFO_BOMBSPOT) ctx.bomb_spots = true;
    }
    return ctx;
}

/* FUN_1401fff70(player, item, 0, 1, 0): online [23, item, 0] (the server answers [23, player, item | 250, money]);
 * offline the item is bought on the spot */
static void buy_item(cs2d_client_t *client, int item)
{
    cs2d_player_t *me = &client->game.players[client->local_player_id];
    if (!me->alive || me->team < 1) return;
    if (client->connected && client->join_stage == 5) {
        uint8_t pkt[3] = { (uint8_t)CS2D_NET_BUY, (uint8_t)item, 0 };
        cs2d_client_send_udp(client, pkt, sizeof(pkt), true);
    } else {
        cs2d_player_buy_weapon(me, (uint8_t)item);
    }
}

static void buy_menu_result(cs2d_client_t *client, cs2d_buy_act_t act, int item)
{
    if (act != CS2D_BUY_ACT_NONE) client->spec.idle_cam = false; /* Tgui_hudmen.button: DAT_140ddf328 = 0 */
    if (act == CS2D_BUY_ACT_ITEM) buy_item(client, item);
    else if (act == CS2D_BUY_ACT_AUTOBUY || act == CS2D_BUY_ACT_REBUY) {
        cs2d_scene_game_buy_cmd(client, act == CS2D_BUY_ACT_AUTOBUY ? "autobuy" : "rebuy");
    }
}

/*
 * Command dispatcher at 0x1401b3e30 (0x1401b546d..): in game (DAT_140ddf418 1..2) "buy" opens menu 20 and
 * "buyequip" menu 26 (FUN_14014ee60) unless the buy menu setting 0x140a14e20[1] starts with "-"; "buyammo1" /
 * "buyammo2" buy item 61 / 62 at once. The menu also needs the local player alive (0x140a1afd8 method +0x90,
 * assumed to be its health).
 */
bool cs2d_scene_game_buy_cmd(cs2d_client_t *client, const char *cmd)
{
    int menu = 0, item = 0;
    if (_stricmp(cmd, "buy") == 0) menu = CS2D_BUYMENU_MAIN;
    else if (_stricmp(cmd, "buyequip") == 0) menu = CS2D_BUYMENU_EQUIPMENT;
    else if (_stricmp(cmd, "buyammo1") == 0) item = CS2D_WEP_PRIMARYAMMO;
    else if (_stricmp(cmd, "buyammo2") == 0) item = CS2D_WEP_SECONDARYAMMO;
    else if (_stricmp(cmd, "autobuy") == 0 || _stricmp(cmd, "rebuy") == 0) {
        cs2d_client_console_print(client, 255, 220, 0, "'%s' is not reconstructed yet", cmd);
        return true;
    } else return false;

    if (client->scene != CS2D_SCENE_GAME) return true;
    const cs2d_player_t *me = &client->game.players[client->local_player_id];
    if ((me->team != CS2D_TEAM_T && me->team != CS2D_TEAM_CT) || !me->alive) return true;
    if (item) {
        buy_item(client, item);
    } else {
        client->build_menu_open = false;
        client->buy.menu = menu;
    }
    return true;
}

/* [39]: respawn request (FUN_140298b30 -> FUN_14020c120), sent unless a living player is under the cursor */
static void request_respawn(cs2d_client_t *client)
{
    const cs2d_player_t *me = &client->game.players[client->local_player_id];
    if (!client->connected || client->join_stage != 5 || me->team == CS2D_TEAM_SPEC || me->alive) return;
    float wx = client->cam_x + client->mouse_screen_x, wy = client->cam_y + client->mouse_screen_y;
    for (int i = 1; i <= CS2D_MAX_PLAYERS; i++) {
        const cs2d_player_t *p = &client->game.players[i];
        if (p->alive && p->team != CS2D_TEAM_SPEC && fabsf(wx - p->pos.x) < 20.0f && fabsf(wy - p->pos.y) < 20.0f) return;
    }
    uint8_t pkt[1] = { (uint8_t)CS2D_NET_RESPAWN_REQ };
    cs2d_client_send_udp(client, pkt, sizeof(pkt), true);
}

/* Seconds until a respawn is allowed (mp_respawndelay - time since death), 0 when it is */
static int respawn_wait(const cs2d_client_t *client)
{
    int delay = client->net_respawn_delay;
    if (delay <= 0) return 0;
    int left = delay - (int)((GetTickCount() - client->death_ms) / 1000u);
    if (left > delay) left = delay;
    return left < 1 ? 0 : left;
}

/* Thud_msg.draw_center (0x14024d230): from screen_h / 2 - 10 n - 50 down, 5 s each (fade calibrated: 1 s) */
static void render_center_msgs(cs2d_client_t *client, int screen_w, int screen_h)
{
    uint32_t now = GetTickCount();
    int n = 0;
    for (int i = 0; i < 4; i++) {
        if (client->center_ms[i] && now - client->center_ms[i] >= 6000u) client->center_ms[i] = 0;
        if (client->center_ms[i]) n++;
    }
    float y = (float)(screen_h / 2 - 10 * n - 50);
    for (int i = 0; i < 4; i++) {
        if (!client->center_ms[i]) continue;
        uint32_t age = now - client->center_ms[i];
        float a = age < 5000u ? 1.0f : 1.0f - (float)(age - 5000u) / 1000.0f;
        float tw, th;
        cs2d_render_text_measure(CS2D_FONT_REGULAR, client->center_msg[i], &tw, &th);
        cs2d_render_rect((float)(screen_w / 2) - tw * 0.5f - 20.0f, y - 2.0f, tw + 40.0f, th + 4.0f, 0, 0, 0, 0.25f * a, true);
        /* Drop shadow at +1, +1 (0x14024d230) */
        cs2d_render_text_ex(CS2D_FONT_REGULAR, (float)(screen_w / 2 - (int)tw / 2) + 1.0f, y + 1.0f, client->center_msg[i],
                            0.0f, 0.0f, 0.0f, a);
        cs2d_render_text_ex(CS2D_FONT_REGULAR, (float)(screen_w / 2 - (int)tw / 2), y, client->center_msg[i],
                            client->center_rgb[i][0] / 255.0f, client->center_rgb[i][1] / 255.0f,
                            client->center_rgb[i][2] / 255.0f, a);
        y += th + 2.0f;
    }
}

/* Max2D TextWidth of a string (int) */
static int kill_tw(const char *s)
{
    return (int)cs2d_render_text_width_ex(CS2D_FONT_REGULAR, s);
}

/* Text with its black shadow (the shadow at x + 1, sy) */
static void kill_text(const char *s, int x, float y, float sy, const uint8_t *rgb, float a)
{
    cs2d_render_text_ex(CS2D_FONT_REGULAR, (float)(x + 1), sy, s, 0.0f, 0.0f, 0.0f, a);
    cs2d_render_text_ex(CS2D_FONT_REGULAR, (float)x, y, s, rgb[0] / 255.0f, rgb[1] / 255.0f, rgb[2] / 255.0f, a);
}

/*
 * Thud_msg.draw_kill (0x14010f8d0), right aligned at screen_w - 3. Rows start at y 3, or below the spectator
 * minimap (25 + 15 = 40, + map height * scale while it is shown) when the local player is not alive. A row stays
 * hud_deathnotice_time s, then fades by 0.05 per 60 Hz frame and its height (TextHeight("I") + 4) shrinks by 1 per
 * frame. deathnoticeimg 1: "killer[ + assist] <weapon _k.bmp> victim" on a mirrored hud_shade_c bar (black,
 * fade * 0.35, scaled to row width / 350 and offset / 10); the weapon image is additive (230, 180, 0) and scaled to
 * fit offset - 2. deathnoticeimg 0: "killer [+ assist ]weapon victim" as text.
 * Not done: the small font under 700 px (DAT_140a14f88), the mp_hud kill flag (0x40, assumed on), Lua images (-1).
 */
static void render_kill_msgs(cs2d_client_t *client, int screen_w)
{
    static uint32_t last_ms;
    uint32_t now = GetTickCount();
    float dtf = last_ms ? (float)(now - last_ms) * 0.06f : 0.0f; /* g_dt: 1 per 60 Hz frame */
    last_ms = now;

    const cs2d_player_t *local_pl = &client->game.players[client->local_player_id];
    int y0 = 3;
    if (!local_pl->alive) {
        y0 = 40;
        if (s_spec_map && client->game.map.tiles) y0 += (int)client->game.map.height * spec_map_scale(&client->game.map);
    }
    cs2d_gfx_bank_t *bank = cs2d_render_get_gfx_bank();
    float tw_i, th;
    cs2d_render_text_measure(CS2D_FONT_REGULAR, "I", &tw_i, &th);

    for (int i = 0; i < client->killmsg_count; ) {
        cs2d_killmsg_t *m = &client->killmsgs[i];
        float y = (float)y0;
        char buf[3 * (CS2D_MAX_NAME_LEN + 32) + 8];
        if (m->fade > 0.0f && client->config.deathnoticeimg) {
            y = (float)(y0 + 1);
            const cs2d_texture_t *img = (bank && m->wpn >= 1 && m->wpn <= 99 && m->wpn < CS2D_MAX_WEAPONS &&
                                         bank->tex_weapons_k[m->wpn].id) ? &bank->tex_weapons_k[m->wpn] : NULL;
            float sc = 1.0f;
            if (img) {
                sc = m->offset - 2.0f;
                if (sc <= 1.0f) sc = 1.0f;
                sc /= (float)img->height;
                if (sc >= 1.0f) sc = 1.0f;
            }
            snprintf(buf, sizeof(buf), "%s ", m->txt[0]);
            int x = screen_w - 3 - kill_tw(buf);
            if (img) {
                x = (int)((float)x - (float)(img->width + kill_tw(" ")) * sc);
            } else {
                snprintf(buf, sizeof(buf), "%s ", m->txt[1]);
                x -= kill_tw(buf);
            }
            x -= kill_tw(m->txt[2]);
            if (m->txt[3][0]) {
                snprintf(buf, sizeof(buf), "%s + ", m->txt[3]);
                x -= kill_tw(buf);
            }
            /* Shade bar: two mirrored halves from the row's center (the image is symmetric vertically, so the
             * mirrored half is the image turned by 180 degrees) */
            float w = (float)(screen_w - x), hw = 250.0f * w / 350.0f, h = 20.0f * m->offset * 0.1f;
            float cx = (float)x + w * 0.5f, top = (float)(y0 - 2) - m->offset * 0.5f;
            cs2d_render_sprite_frame(&s_tex_shade_c, 0, 0, 0, cx + hw * 0.5f, top + h * 0.5f, hw, h, 0.0f,
                                     0.0f, 0.0f, 0.0f, m->fade * 0.35f, 0);
            cs2d_render_sprite_frame(&s_tex_shade_c, 0, 0, 0, cx - hw * 0.5f, top + h * 0.5f, hw, h, 180.0f,
                                     0.0f, 0.0f, 0.0f, m->fade * 0.35f, 0);

            float sy = (float)(y0 + 2);
            kill_text(m->txt[0], x, y, sy, m->rgb[0], m->fade);
            if (m->txt[3][0]) {
                x += kill_tw(m->txt[0]);
                kill_text(" + ", x, y, sy, m->rgb[1], m->fade);
                x += kill_tw(" + ");
                kill_text(m->txt[3], x, y, sy, m->rgb[3], m->fade);
                snprintf(buf, sizeof(buf), "%s ", m->txt[3]);
            } else {
                snprintf(buf, sizeof(buf), "%s ", m->txt[0]);
            }
            x += kill_tw(buf);
            if (img) {
                float iw = (float)img->width * sc, ih = (float)img->height * sc;
                cs2d_render_sprite_frame(img, 0, 0, 0, (float)x + iw * 0.5f, (m->offset - ih) * 0.5f + y + ih * 0.5f,
                                         iw, ih, 0.0f, 230.0f / 255.0f, 180.0f / 255.0f, 0.0f, m->fade, 4);
                x = (int)((float)(kill_tw(" ") + img->width) * sc + (float)x);
            } else {
                kill_text(m->txt[1], x, y, sy, m->rgb[1], m->fade);
                snprintf(buf, sizeof(buf), "%s ", m->txt[1]);
                x += kill_tw(buf);
            }
            kill_text(m->txt[2], x, y, sy, m->rgb[2], m->fade);
        } else if (m->fade > 0.0f) {
            snprintf(buf, sizeof(buf), "%s %s %s", m->txt[0], m->txt[1], m->txt[2]);
            int x = screen_w - 3 - kill_tw(buf);
            if (m->txt[3][0]) {
                snprintf(buf, sizeof(buf), "%s + ", m->txt[3]);
                x -= kill_tw(buf);
            }
            float sy = (float)(y0 + 1);
            kill_text(m->txt[0], x, y, sy, m->rgb[0], m->fade);
            snprintf(buf, sizeof(buf), "%s ", m->txt[0]);
            x += kill_tw(buf);
            if (m->txt[3][0]) {
                kill_text("+ ", x, y, sy, m->rgb[1], m->fade);
                x += kill_tw("+ ");
                snprintf(buf, sizeof(buf), "%s ", m->txt[3]);
                kill_text(buf, x, y, sy, m->rgb[3], m->fade);
                x += kill_tw(buf);
            }
            kill_text(m->txt[1], x, y, sy, m->rgb[1], m->fade);
            snprintf(buf, sizeof(buf), "%s ", m->txt[1]);
            kill_text(m->txt[2], x + kill_tw(buf), y, sy, m->rgb[2], m->fade);
        }

        if (now - m->timer_ms > m->show_ms) {
            m->fade -= dtf * 0.05f;
            if (m->fade <= 0.0f && (m->offset -= dtf) <= 0.0f) {
                memmove(&client->killmsgs[i], &client->killmsgs[i + 1],
                        sizeof(cs2d_killmsg_t) * (size_t)(client->killmsg_count - 1 - i));
                client->killmsg_count--;
                continue;
            }
        } else {
            m->offset = (float)((int)th + 4);
        }
        y0 = (int)(y + m->offset + 2.0f);
        i++;
    }
}

/*
 * In-game HUD Chat log (Thud_msg.draw_chat 0x14024d8a0, FUN_14029f7b0):
 * Starts at screen_h - 85 - (line_h * 2), stacking upwards.
 * Text at x = 3.0f, drop shadow at x = 4.0f, y + 1.0f.
 * Chat input prompt at bottom when chat_open.
 */
static void render_chat(cs2d_client_t *client, int screen_w, int screen_h)
{
    (void)screen_w;
    uint32_t now = GetTickCount();
    float line_h = 16.0f;

    /* 1. Update line expirations (10 s display, then fade out) */
    for (int i = 0; i < client->chat_line_count; ) {
        cs2d_chat_line_t *ln = &client->chat_lines[i];
        uint32_t age = now - ln->time_ms;
        if (age > 10000u) {
            float fade_age = (float)(age - 10000u) * 0.001f;
            ln->fade = 1.0f - fade_age * 1.5f;
            if (ln->fade <= 0.0f) {
                memmove(&client->chat_lines[i], &client->chat_lines[i + 1],
                        sizeof(cs2d_chat_line_t) * (size_t)(client->chat_line_count - 1 - i));
                client->chat_line_count--;
                continue;
            }
        }
        i++;
    }

    float base_y = (float)screen_h - 85.0f - (line_h * 2.0f);

    /* 2. Chat input prompt (FUN_14029f7b0) */
    if (client->chat_open) {
        const char *prompt = (client->chat_mode == 2) ? "Say (Team) : " : "Say : ";
        char full[CS2D_CHAT_LINE_LEN + 32];
        snprintf(full, sizeof(full), "%s%s", prompt, client->chat_input);

        float tw, th;
        cs2d_render_text_measure(CS2D_FONT_REGULAR, full, &tw, &th);
        float pw = cs2d_render_text_width_ex(CS2D_FONT_REGULAR, prompt);
        float inp_w = cs2d_render_text_width_ex(CS2D_FONT_REGULAR, client->chat_input);

        /* Background rect (alpha 0.4 black) */
        cs2d_render_rect(0.0f, base_y, tw + 10.0f, line_h, 0, 0, 0, 0.4f, true);

        /* Text drop shadow at (4, base_y + 1) */
        cs2d_render_text_ex(CS2D_FONT_REGULAR, 4.0f, base_y + 1.0f, full, 0.0f, 0.0f, 0.0f, 1.0f);
        /* Main text in CS2D gold (255, 220, 0) at (3, base_y) */
        cs2d_render_text_ex(CS2D_FONT_REGULAR, 3.0f, base_y, full, 1.0f, 220.0f / 255.0f, 0.0f, 1.0f);

        /* Blinking cursor */
        if ((now / 400u) % 2u == 0) {
            float cur_x = 3.0f + pw + inp_w + 1.0f;
            cs2d_render_rect(cur_x, base_y + 2.0f, 1.0f, line_h - 4.0f, 1.0f, 1.0f, 1.0f, 1.0f, true);
        }

        base_y -= line_h;
    }

    /* 3. Draw chat log lines from newest to oldest stacking upwards */
    float y = base_y;
    for (int i = client->chat_line_count - 1; i >= 0; i--) {
        if (y < 180.0f) break; /* Avoid overlapping radar / top screen */
        const cs2d_chat_line_t *ln = &client->chat_lines[i];
        float a = ln->fade;

        float x = 3.0f;
        if (ln->name[0] != '\0') {
            float nw = cs2d_render_text_width_ex(CS2D_FONT_REGULAR, ln->name);
            /* Name shadow */
            cs2d_render_text_ex(CS2D_FONT_REGULAR, x + 1.0f, y + 1.0f, ln->name, 0.0f, 0.0f, 0.0f, a);
            /* Name in team color */
            cs2d_render_text_ex(CS2D_FONT_REGULAR, x, y, ln->name,
                                ln->name_rgb[0] / 255.0f, ln->name_rgb[1] / 255.0f, ln->name_rgb[2] / 255.0f, a);
            x += nw;
        }

        /* Message text shadow */
        cs2d_render_text_ex(CS2D_FONT_REGULAR, x + 1.0f, y + 1.0f, ln->text, 0.0f, 0.0f, 0.0f, a);
        /* Message text */
        cs2d_render_text_ex(CS2D_FONT_REGULAR, x, y, ln->text,
                            ln->text_rgb[0] / 255.0f, ln->text_rgb[1] / 255.0f, ln->text_rgb[2] / 255.0f, a);

        y -= line_h;
    }
}


/* Item 70 / 71 in the inventory: the player carries a flag (Titem.give sets Tpl.flag) */
static bool carries_flag(const cs2d_player_t *p)
{
    for (int i = 0; i < CS2D_SLOT_COUNT; i++) {
        if (p->inventory[i] == CS2D_WEP_REDFLAG || p->inventory[i] == CS2D_WEP_BLUEFLAG) return true;
    }
    return false;
}

/*
 * Tpl.draw (0x140121xxx, self->flag == 1): the flag on the carrier's back, 7 px behind along the aim, rotated
 * by the weapon angle + 160 at scale 0.8. A Terrorist carries the blue flag, everybody else the red one.
 */
static void render_carried_flag(const cs2d_player_t *p, float cam_x, float cam_y)
{
    cs2d_gfx_bank_t *bank = cs2d_render_get_gfx_bank();
    if (!bank || !carries_flag(p)) return;
    const cs2d_texture_t *t = (p->team == CS2D_TEAM_T) ? &bank->tex_blueflag : &bank->tex_redflag;
    if (!t->id) return;
    double a = (double)p->angle * 0.017453292519943295;
    float x = p->pos.x - cam_x - (float)(sin(a) * 7.0), y = p->pos.y - cam_y + (float)(cos(a) * 7.0);
    cs2d_render_sprite_frame(t, t->width, t->height, 0, x, y, t->width * 0.8f, t->height * 0.8f,
                             p->draw_angle + 160.0f, 1, 1, 1, 1, 0);
}

/* Weapons lying on the ground (gfx/weapons/<name>_d.bmp, like Env_Item) */
static void render_ground_items(const cs2d_client_t *client)
{
    cs2d_gfx_bank_t *bank = cs2d_render_get_gfx_bank();
    if (!bank) return;
    float cx = roundf(client->cam_x), cy = roundf(client->cam_y);
    bool do_shadow = cs2d_render_get_shadows_enabled() && cs2d_render_get_item_shadows() &&
                     cs2d_render_get_shadow_intensity() > 0;
    float shadow_a = do_shadow ? ((float)cs2d_render_get_shadow_intensity() * 0.01f) : 0.0f;
    const cs2d_texture_t *stex = bank->tex_shadow.id ? &bank->tex_shadow : &bank->tex_leaf;

    for (int i = 0; i < CS2D_MAX_GROUND_ITEMS; i++) {
        const cs2d_ground_item_t *it = &client->ground_items[i];
        if (!it->active || it->type == 0 || it->type >= CS2D_MAX_WEAPONS) continue;
        const cs2d_texture_t *dtex = &bank->tex_weapons_d[it->type];
        if (!dtex->id) continue;
        float dw = (dtex->width > 64) ? (float)(dtex->width / 2) : (float)dtex->width;
        float dh = (dtex->height > 32) ? (float)(dtex->height / 2) : (float)dtex->height;
        float ix = it->x - cx;
        float iy = it->y - cy;

        /* Titem.draw (0x1401483a0): incbindata/shadow.bmp at (x, y) + 1px drop shadow of item image at (x+1, y+1) */
        if (do_shadow) {
            if (stex && stex->id) {
                cs2d_render_sprite_frame(stex, 32, 32, 0, ix, iy, 32.0f, 32.0f, 0.0f,
                                         0.0f, 0.0f, 0.0f, shadow_a, 0);
            }
            cs2d_render_sprite(dtex, ix + 1.0f, iy + 1.0f, dw, dh, 0.0f, 0.0f, 0.0f, 0.0f, shadow_a);
        }

        cs2d_render_sprite(dtex, ix, iy, dw, dh, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    }
}

/* Ambient fields (fire, gas, smoke) over the players: the smoke cloud is a Tp 0x1b, drawn by Tp.draw
 * after Tpl.draw, so it hides whoever stands in it */
static void render_fields(const cs2d_client_t *client)
{
    float cx = roundf(client->cam_x), cy = roundf(client->cam_y);
    cs2d_gfx_bank_t *bank = cs2d_render_get_gfx_bank();

    for (int i = 0; i < CS2D_MAX_FIELDS; i++) {
        const cs2d_field_t *f = &client->projectiles.fields[i];
        if (!f->active) continue;
        float fx = f->x - cx, fy = f->y - cy;
        if (f->type == CS2D_FIELD_FIRE) {
            cs2d_render_circle(fx, fy, f->radius, 1.0f, 0.35f, 0.05f, 0.25f, true);
        } else if (f->type == CS2D_FIELD_GAS) {
            cs2d_render_circle(fx, fy, f->radius, 0.2f, 0.85f, 0.1f, 0.25f, true);
        } else if (f->type == CS2D_FIELD_SMOKE && bank) {
            /* Tp 0x1b draw: sgsmoke at scale 4, ALPHABLEND, grey Rand(50,70); fades out after 30 s */
            float a = f->lifetime / CS2D_SMOKE_FADE_SECONDS;
            if (a > 1.0f) a = 1.0f;
            float w = (float)bank->tex_sgsmoke.width * 4.0f, h = (float)bank->tex_sgsmoke.height * 4.0f;
            cs2d_render_sprite_frame(&bank->tex_sgsmoke, 0, 0, 0, fx, fy, w, h, f->rot,
                                     f->shade, f->shade, f->shade, a, 0);
        }
    }

}

/* Projectiles resting on ground (glist in CS2D.exe) */
static void render_projectiles_ground(const cs2d_client_t *client)
{
    float cx = roundf(client->cam_x), cy = roundf(client->cam_y);
    cs2d_gfx_bank_t *bank = cs2d_render_get_gfx_bank();

    /* Render ground projectiles (Tpro.draw 0x140288702) */
    bool do_shadow = cs2d_render_get_shadows_enabled() && cs2d_render_get_item_shadows() &&
                     cs2d_render_get_shadow_intensity() > 0;
    float shadow_a = do_shadow ? ((float)cs2d_render_get_shadow_intensity() * 0.01f) : 0.0f;
    const cs2d_texture_t *stex = (bank && bank->tex_shadow.id) ? &bank->tex_shadow : (bank ? &bank->tex_leaf : NULL);

    for (int i = 0; i < CS2D_MAX_PROJECTILES; i++) {
        const cs2d_projectile_t *p = &client->projectiles.projectiles[i];
        if (!p->active || p->state != CS2D_PROJ_STATE_GROUND) continue;
        const cs2d_texture_t *tex = cs2d_render_get_projectile_texture(p->type);
        if (!tex || !tex->id) continue;
        float w = (tex->width > 64) ? (float)(tex->width / 2) : (float)tex->width;
        float h = (tex->height > 32) ? (float)(tex->height / 2) : (float)tex->height;
        if (w < 8.0f) w = 16.0f;
        if (h < 8.0f) h = 16.0f;
        float px = p->x - cx;
        float py = p->y - cy;
        if (do_shadow) {
            if (stex && stex->id) {
                cs2d_render_sprite_frame(stex, 32, 32, 0, px, py, 32.0f, 32.0f, p->rot,
                                         0.0f, 0.0f, 0.0f, shadow_a, 0);
            }
            cs2d_render_sprite(tex, px + 1.0f, py + 1.0f, w, h, p->rot, 0.0f, 0.0f, 0.0f, shadow_a);
        }
        cs2d_render_sprite(tex, px, py, w, h, p->rot, 1.0f, 1.0f, 1.0f, 1.0f);
    }
}

/* Flying projectiles (list in CS2D.exe) */
static void render_projectiles_flying(const cs2d_client_t *client)
{
    float cx = roundf(client->cam_x), cy = roundf(client->cam_y);

    for (int i = 0; i < CS2D_MAX_PROJECTILES; i++) {
        const cs2d_projectile_t *p = &client->projectiles.projectiles[i];
        if (!p->active || p->state != CS2D_PROJ_STATE_FLYING) continue;
        const cs2d_texture_t *tex = cs2d_render_get_projectile_texture(p->type);
        if (!tex || !tex->id) continue;
        float w = (tex->width > 64) ? (float)(tex->width / 2) : (float)tex->width;
        float h = (tex->height > 32) ? (float)(tex->height / 2) : (float)tex->height;
        if (w < 8.0f) w = 16.0f;
        if (h < 8.0f) h = 16.0f;
        float rot = cs2d_projectile_is_rocket(p->type) ? p->dir : p->rot;
        cs2d_render_sprite(tex, p->x - cx, p->y - cy, w, h, rot, 1.0f, 1.0f, 1.0f, 1.0f);
    }
}

/*
 * FUN_14024fb10: text centered on (x, y) with a black shadow at +1, over a black box at alpha 0.4 that has
 * gfx/gui_c.bmp corners (8 px at font scale 1), 8 px wider than the text (width rounded up to even) and
 * max(text height + 2, 16) high (rounded up to even).
 */
static void hover_text(const char *text, int x, int y, int r, int g, int b)
{
    if (!text[0]) return;
    if (!s_tex_gui_c.id) cs2d_texture_load_grid(&s_tex_gui_c, "gfx/gui_c.bmp", 3, 16, 16);
    const int c = 8;
    int th = (int)cs2d_render_font_line_height(CS2D_FONT_REGULAR);
    int tw = (int)cs2d_render_text_width_ex(CS2D_FONT_REGULAR, text);
    tw += tw % 2 == 1;
    int h = th + 2 < 2 * c ? 2 * c : th + 2;
    h += h & 1;
    int tx = x - tw / 2, left = tx - c / 2, top = y - h / 2;
    int inner = tw - c > 0 ? tw - c : 0, right = left + c + inner;
    float hc = c * 0.5f;
    cs2d_render_sprite_frame(&s_tex_gui_c, 16, 16, 0, left + hc, top + hc, c, c, 0.0f, 0, 0, 0, 0.4f, 0);
    cs2d_render_sprite_frame(&s_tex_gui_c, 16, 16, 2, left + hc, top + h - c + hc, c, c, 0.0f, 0, 0, 0, 0.4f, 0);
    cs2d_render_sprite_frame(&s_tex_gui_c, 16, 16, 1, right + hc, top + hc, c, c, 0.0f, 0, 0, 0, 0.4f, 0);
    cs2d_render_sprite_frame(&s_tex_gui_c, 16, 16, 3, right + hc, top + h - c + hc, c, c, 0.0f, 0, 0, 0, 0.4f, 0);
    if (h > 2 * c) {
        cs2d_render_rect((float)left, (float)(top + c), c, (float)(h - 2 * c), 0, 0, 0, 0.4f, true);
        cs2d_render_rect((float)right, (float)(top + c), c, (float)(h - 2 * c), 0, 0, 0, 0.4f, true);
    }
    if (inner > 0) cs2d_render_rect((float)(left + c), (float)top, (float)inner, (float)h, 0, 0, 0, 0.4f, true);
    int ty = y - th / 2;
    cs2d_render_text_ex(CS2D_FONT_REGULAR, (float)(tx + 1), (float)(ty + 1), text, 0, 0, 0, 1.0f);
    cs2d_render_text_ex(CS2D_FONT_REGULAR, (float)tx, (float)ty, text, r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
}

/*
 * Building hover text (FUN_1402a12d0, mp_hovertext bit 8; FUN_1402a2cf0 only calls it with the cursor on the
 * 850x480 screen): at the cursor - 25 the type name (0x1409f79f8), "$1 by $2" (0777) when the owner is in game;
 * at + 25 the health "n%"; at + 45 "Upgrade n%" (0778) in yellow while an upgrade is under way.
 * Title colors: without a team game the local player's buildings yellow and the others green; in a team game
 * (and for unowned buildings) T (255,25,0), CT (50,150,255), else yellow (an unowned building of another team
 * value gets no title).
 */
static void render_building_hover(cs2d_client_t *client, int screen_w, int screen_h)
{
    float mx = client->mouse_screen_x, my = client->mouse_screen_y;
    if (!(client->net_hovertext & 8) || mx < 0 || my < 0 || mx > screen_w || my > screen_h) return;
    const cs2d_dynobj_t *o = cs2d_dynobj_hovered(&client->dynobj, client->cam_x + mx, client->cam_y + my);
    if (!o) return;
    const char *name = cs2d_dynobj_def(o->typ)->name;
    const cs2d_player_t *owner = NULL;
    if (o->player >= 1 && o->player <= CS2D_MAX_PLAYERS) {
        owner = &client->game.players[o->player];
        if (owner->id == 0 && owner->name[0] == '\0') owner = NULL;
    }
    char title[160];
    snprintf(title, sizeof(title), "%s", name);
    if (owner) snprintf(title, sizeof(title), "%s by %s", name, owner->name);
    int c[3] = { 255, 255, 0 };
    bool show = true;
    if (o->player != 0 && !team_game(client)) {
        if (o->player != client->local_player_id) { c[0] = 0; c[1] = 255; c[2] = 0; }
    } else if (o->team == 1) {
        c[0] = 255; c[1] = 25; c[2] = 0;
    } else if (o->team == 2) {
        c[0] = 50; c[1] = 150; c[2] = 255;
    } else if (o->player == 0 && o->team != 0) {
        show = false;
    }
    int x = (int)mx;
    if (show) hover_text(title, x, (int)(my - 25.0f), c[0], c[1], c[2]);

    int pct, upgrade, rgb[3];
    char line[64];
    cs2d_dynobj_hover_health(o, &pct, &upgrade, rgb);
    snprintf(line, sizeof(line), "%d%%", pct);
    hover_text(line, x, (int)(my + 25.0f), rgb[0], rgb[1], rgb[2]);
    if (upgrade > 0) {
        snprintf(line, sizeof(line), "Upgrade %d%%", upgrade);
        hover_text(line, x, (int)(my + 45.0f), 255, 255, 0);
    }
}

/*
 * Player hover text (FUN_1402a12d0, mp_hovertext bits 1 | 2, checked before hostages and buildings): the first other
 * living player in a team within 20 px of the cursor (Tpl.fog must be 0: no fog of war yet). Bit 1 draws the title
 * at the cursor - 25 (cs2d_player_hover_kind), bit 2 "health%" at + 25 in ((100 - hp) * 5.1, hp * 5.1, 0).
 * Returns true when a player is hovered.
 */
static bool render_player_hover(cs2d_client_t *client, int screen_w, int screen_h)
{
    float mx = client->mouse_screen_x, my = client->mouse_screen_y;
    if (!(client->net_hovertext & 3) || mx < 0 || my < 0 || mx > screen_w || my > screen_h) return false;
    float wx = client->cam_x + mx, wy = client->cam_y + my;
    int me = client->local_player_id;
    const cs2d_player_t *target = NULL;
    for (int i = 1; i <= CS2D_MAX_PLAYERS && !target; i++) {
        const cs2d_player_t *p = &client->game.players[i];
        if (i == me || p->team < CS2D_TEAM_T || !p->alive || p->health < 1) continue;
        if (fabsf(wx - p->pos.x) < 20.0f && fabsf(wy - p->pos.y) < 20.0f) target = p;
    }
    if (!target) return false;
    const cs2d_player_t *local = (me >= 1 && me <= CS2D_MAX_PLAYERS) ? &client->game.players[me] : NULL;
    bool local_alive = local && local->alive && local->team >= CS2D_TEAM_T;
    bool show_health;
    cs2d_pl_hover_t kind = cs2d_player_hover_kind(local, target, team_game(client), client->spec.server_mode,
                                                  local_alive, &show_health);
    int x = (int)mx;
    if (client->net_hovertext & 1) {
        char title[96];
        if (kind == CS2D_PL_HOVER_NEUTRAL) {
            hover_text(target->name, x, (int)(my - 25.0f), 255, 255, 0);
        } else if (kind == CS2D_PL_HOVER_FRIEND) {
            snprintf(title, sizeof(title), "Friend: %s", target->name);
            hover_text(title, x, (int)(my - 25.0f), 0, 255, 0);
        } else {
            snprintf(title, sizeof(title), "Enemy: %s", target->name);
            hover_text(title, x, (int)(my - 25.0f), 255, 0, 0);
        }
    }
    if ((client->net_hovertext & 2) && show_health) {
        char line[32];
        int hp = target->health;
        snprintf(line, sizeof(line), "%d%%", hp);
        float r = (100 - hp) * 5.1f, g = hp * 5.1f;
        hover_text(line, x, (int)(my + 25.0f), r < 0 ? 0 : (r > 255 ? 255 : (int)r), g < 0 ? 0 : (g > 255 ? 255 : (int)g), 0);
    }
    return true;
}

static void game_render(cs2d_client_t *client, int screen_w, int screen_h)
{
    /* Light engine (FUN_140262ed0): light buffers for this frame, at the integer camera; dt in 60 Hz frames */
    static uint64_t last_ms;
    uint64_t now_ms = SDL_GetTicks();
    float light_dt = last_ms ? (float)(now_ms - last_ms) * 0.06f : 1.0f;
    last_ms = now_ms;
    /* cs2d_draw_world (0x140290310): the screen shake (Info_Quake, ...) moves the camera for the world only */
    float world_cam_x = client->cam_x, world_cam_y = client->cam_y;
    int shake_x, shake_y;
    if (cs2d_gen_shake_offset(&client->gen, &shake_x, &shake_y)) {
        client->cam_x += (float)shake_x;
        client->cam_y += (float)shake_y;
    }
    /* The player silhouettes are drawn under the bodies by cs2d_render_player, not in the day shadows */
    uint64_t freq = SDL_GetPerformanceFrequency();
    uint64_t t0 = SDL_GetPerformanceCounter();
    cs2d_light_frame(&client->game.map, (int)roundf(client->cam_x), (int)roundf(client->cam_y), light_dt);
    uint64_t t1 = SDL_GetPerformanceCounter();

    /* 1. Map Pass 0 & 1 (background, tileset, tile modifiers, ground entities, items, hostages) */
    cs2d_render_map_ex(&client->game.map, &client->tex_tileset, client->cam_x, client->cam_y, screen_w, screen_h);
    uint64_t t2 = SDL_GetPerformanceCounter();
    bool tg = team_game(client);
    int  me = client->local_player_id;
    cs2d_render_dynobjs(&client->dynobj, CS2D_DO_LAYER_GROUND, client->cam_x, client->cam_y, screen_w, screen_h, tg, me);
    render_ground_items(client);
    cs2d_render_gen(&client->gen, CS2D_GEN_LAYER_FLOOR, client->cam_x, client->cam_y, screen_w, screen_h);
    render_projectiles_ground(client);
    cs2d_render_dynobjs(&client->dynobj, CS2D_DO_LAYER_OBSTACLE, client->cam_x, client->cam_y, screen_w, screen_h, tg, me);
    cs2d_light_draw_floor();   /* DrawLayer1: floor occlusion after the items (FUN_140281fd0) */

    /* CPU shadow chunks (light 0): FUN_1401165a0 runs right after Te.draw(1), before the players */
    cs2d_render_map_shadows(&client->game.map, client->cam_x, client->cam_y);

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
            render_carried_flag(p, client->cam_x, client->cam_y);
        }
    }
    cs2d_light_draw_color();   /* DrawLightsColorList: additive light color after the players (FUN_140281fd0) */
    render_projectiles_flying(client);
    render_fields(client);
    cs2d_render_dynobjs(&client->dynobj, CS2D_DO_LAYER_WALL, client->cam_x, client->cam_y, screen_w, screen_h, tg, me);

    /* Breakable debris and explosions */
    cs2d_render_fx(&client->fx, &client->game.map, &client->tex_tileset, client->cam_x, client->cam_y);
    uint64_t t3 = SDL_GetPerformanceCounter();

    /* 3. Map Pass 2 (Overhead entities: Env_Sprite, Env_Image over players, Env_Object trees/lamps) */
    cs2d_render_map_overhead(&client->game.map, client->cam_x, client->cam_y, screen_w, screen_h);

    /* Gen_* particles: Tp lists 5, 7 and 6 follow Te.draw pass 2 (FUN_1402802xx), then the thunder flash */
    cs2d_render_gen(&client->gen, CS2D_GEN_LAYER_TOP, client->cam_x, client->cam_y, screen_w, screen_h);
    cs2d_render_gen(&client->gen, CS2D_GEN_LAYER_FOG, client->cam_x, client->cam_y, screen_w, screen_h);
    /* DrawLayer2: the light buffer (day shadows of walls, obstacles and buildings) multiplied over the whole world,
     * players included, last in FUN_140281fd0 */
    cs2d_light_draw_buffer();
    /* Rain and snow (Tp list 6) over the light buffer, so the day shadows do not darken them */
    cs2d_render_gen(&client->gen, CS2D_GEN_LAYER_WEATHER, client->cam_x, client->cam_y, screen_w, screen_h);
    cs2d_render_gen_flash(&client->gen, screen_w, screen_h);
    client->cam_x = world_cam_x;
    client->cam_y = world_cam_y;
    uint64_t t4 = SDL_GetPerformanceCounter();

    /* 4. In-Game HUD (hud_nums.bmp, hud_symbols.bmp, hud_radar_advanced.png) */
    const cs2d_player_t *local_pl = &client->game.players[client->local_player_id];
    /* FUN_140295190: the build helper follows the cursor while the Wrench is held (DAT_140ddf474) */
    if (client->build_mode && !local_can_build_mode(client)) client->build_mode = false;
    if (client->build_mode && !client->build_menu_open) {
        int tx, ty;
        cursor_tile(client, &tx, &ty);
        int ptx = (int)(local_pl->pos.x * 0.03125f), pty = (int)(local_pl->pos.y * 0.03125f);
        bool ok = cs2d_dynobj_can_build(&client->dynobj, client->game.players, me, ptx, pty, tx, ty, false) == 1;
        cs2d_render_build_helper(ptx, pty, tx, ty, ok, client->cam_x, client->cam_y);
    }
    if (!render_player_hover(client, screen_w, screen_h)) render_building_hover(client, screen_w, screen_h);
    if (local_pl->alive) {
        /* FUN_140291bd0 fills the radar icon queue, cs2d_draw_line_3 draws it */
        cs2d_radar_env_t renv = { client->ground_items, CS2D_MAX_GROUND_ITEMS, team_game(client),
                                  (uint32_t)SDL_GetTicks() };
        cs2d_radar_blip_t blips[CS2D_RADAR_MAX_BLIPS];
        int nblips = cs2d_radar_collect(&client->game, local_pl, &renv, blips);
        cs2d_render_hud(local_pl, &client->game, &client->tex_tileset, blips, nblips, screen_w, screen_h);
        /* Right of the radar (drawradar != 0: radarsize + 100 + 8), else at 8 (FUN_140295190) */
        float sel_x = client->config.drawradar ? (float)(client->config.radarsize + 108) : 8.0f;
        cs2d_render_weapon_select(local_pl, &client->invsel, sel_x);
    } else {
        render_spectator_hud(client, screen_w, screen_h);
    }
    uint64_t t5 = SDL_GetPerformanceCounter();
    static int g_print_render_pass = 0;
    if (++g_print_render_pass == 60) {
        printf("[game_render breakdown] light_frame=%.2fms map_pass01=%.2fms actors=%.2fms overhead=%.2fms hud=%.2fms\n",
               (double)(t1 - t0)*1000.0/(double)freq,
               (double)(t2 - t1)*1000.0/(double)freq,
               (double)(t3 - t2)*1000.0/(double)freq,
               (double)(t4 - t3)*1000.0/(double)freq,
               (double)(t5 - t4)*1000.0/(double)freq);
    }
    if (client->build_menu_open) {
        if (local_pl->alive) render_build_menu(client);
        else client->build_menu_open = false;
    }
    if (client->buy.menu) {
        cs2d_buyctx_t ctx = buy_ctx(client);
        if (local_pl->alive) cs2d_buymenu_render(&client->buy, &ctx, client->mouse_screen_x, client->mouse_screen_y,
                                                 screen_w, screen_h);
        else client->buy.menu = 0;
    }
    render_kill_msgs(client, screen_w);
    render_chat(client, screen_w, screen_h);
    render_center_msgs(client, screen_w, screen_h);
    /* Thud_scoreboard.draw(1) after the chat and the HUD menus (0x1402a0000 game HUD) */
    if (client->show_scores) {
        cs2d_scoreboard_view_t v = {
            .players     = client->game.players,
            .local_id    = client->local_player_id,
            .teams       = team_game(client),
            .zombies     = client->net_sv_gamemode == 4,
            .host        = !client->connected,
            .server_name = client->net_server_name,
            .map_name    = client->connected ? client->net_map_name : client->game_map_name,
            .score_t     = client->game.score_t,
            .score_ct    = client->game.score_ct,
            .ticks       = client->connected ? client->net_ticks : -1,
            .spec_mode   = client->spec.mode,
            .spec_target = client->spec.target,
            .mouse_x     = client->mouse_screen_x,
            .mouse_y     = client->mouse_screen_y
        };
        cs2d_scoreboard_render(&v, screen_w, screen_h);
    }
}

/* Holds weapon_id (FUN_1401caca0); online the new weapon is announced as [9, type, 0] (recorded from CS2D.exe) */
static void hold_weapon(cs2d_client_t *client, cs2d_player_t *pl, uint8_t weapon_id)
{
    uint8_t before = pl->current_weapon;
    if (weapon_id == CS2D_WEP_NONE || !cs2d_player_select_weapon(pl, weapon_id) || pl->current_weapon == before) return;
    if (client->connected && client->join_stage == 5) {
        uint8_t pkt[3] = { (uint8_t)CS2D_NET_SELECT_WEAPON, pl->current_weapon, 0 };
        cs2d_client_send_udp(client, pkt, sizeof(pkt), true);
    }
}

/*
 * Command dispatcher at 0x1401b3e30: invprev -> FUN_140166fb0(-1), invnext -> FUN_140166fb0(1), slotN ->
 * FUN_1401cc180(N, 0), only in game (DAT_140ddf418 1..2). The HUD sounds (0x140a0d628 / d630 / d640) are not played:
 * there is no audio yet.
 */
bool cs2d_scene_game_inv_cmd(cs2d_client_t *client, const char *cmd)
{
    int dir = 0, slot = -1;
    if (_stricmp(cmd, "invnext") == 0) dir = 1;
    else if (_stricmp(cmd, "invprev") == 0) dir = -1;
    else if (_strnicmp(cmd, "slot", 4) == 0 && cmd[4] >= '0' && cmd[4] <= '9') slot = atoi(cmd + 4);
    else return false;

    if (client->scene != CS2D_SCENE_GAME) return true;
    cs2d_player_t *pl = &client->game.players[client->local_player_id];
    if (!pl->alive) return true;
    if (dir != 0) {
        cs2d_invsel_cycle(&client->invsel, pl, dir);
    } else {
        hold_weapon(client, pl, cs2d_invsel_slot(&client->invsel, pl, slot, client->config.hud_fastswitch));
    }
    return true;
}

static void game_handle_key(cs2d_client_t *client, int key, bool pressed)
{
    cs2d_player_t *pl = &client->game.players[client->local_player_id];
    bool online = client->connected && client->join_stage == 5;

    if (client->build_menu_open && key >= '0' && key <= '9') {
        if (pressed) build_menu_key(client, key - '0');
        return;
    }
    /* Number keys pick Buy menu buttons (Tgui_hudmen.button: DAT_140ddf340 == key) */
    if (client->buy.menu && key >= '0' && key <= '9') {
        if (pressed) {
            int item;
            cs2d_buyctx_t ctx = buy_ctx(client);
            cs2d_buy_act_t act = cs2d_buymenu_key(&client->buy, &ctx, key - '0', &item);
            buy_menu_result(client, act, item);
        }
        return;
    }
    /* "showscores" (bind "tab") is held, not pressed: the command dispatcher sets DAT_140ddf480 while the key is down */
    const char *held_cmd = cs2d_binds_find(&client->binds, cs2d_bind_key_name(key));
    if (held_cmd && _stricmp(held_cmd, "showscores") == 0) {
        client->show_scores = pressed;
        return;
    }
    /* Bindings of sys/controls.cfg: weapon selection ("1".."9", "0"), buying ("B", "C", ",", ".") and chat ("Y", "U") */
    const char *bound = pressed ? cs2d_binds_find(&client->binds, cs2d_bind_key_name(key)) : NULL;
    if (bound) {
        if (_stricmp(bound, "say") == 0 || _stricmp(bound, "sayteam") == 0) {
            cs2d_client_console_exec(client, bound);
            return;
        }
        if (cs2d_scene_game_inv_cmd(client, bound) || cs2d_scene_game_buy_cmd(client, bound)) return;
    }

    switch (key) {
        case 'Y': case 'y':
            if (pressed) { cs2d_client_console_exec(client, "say"); return; }
            break;
        case 'U': case 'u':
            if (pressed) { cs2d_client_console_exec(client, "sayteam"); return; }
            break;

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
    cs2d_player_t *me = &client->game.players[client->local_player_id];
    if (client->build_menu_open) {
        if (pressed && button == 1) build_menu_click(client, mx, my);
        if (!pressed) { client->mouse_fire = false; client->mouse_attack2 = false; }
        return;
    }
    if (client->buy.menu) {
        /* The menu is modal: clicks never reach the game */
        if (pressed && button == 1) {
            int item;
            cs2d_buyctx_t ctx = buy_ctx(client);
            cs2d_buy_act_t act = cs2d_buymenu_click(&client->buy, &ctx, mx, my, client->config.window_width,
                                                    client->config.window_height, &item);
            buy_menu_result(client, act, item);
        }
        if (!pressed) { client->mouse_fire = false; client->mouse_attack2 = false; }
        return;
    }
    /* attack / attack2 with the weapon selection HUD open hold the highlighted item instead of firing, and the
     * fire timer waits 400 ms (FUN_140219820 / FUN_14021b910) */
    if (pressed && (button == 1 || button == 2) && me->alive && client->invsel.slot != 0) {
        uint8_t w = cs2d_invsel_confirm(&client->invsel, me);
        if (w != CS2D_WEP_NONE) {
            hold_weapon(client, me, w);
            me->fire_cooldown = 0.4f;
        }
        return;
    }
    if (button == 1 && !pressed) s_spec_map_drag = false;
    if (pressed && !me->alive && spectator_hud_click(client, mx, my, button)) {
        return;
    }
    /* Dead in a respawn mode: a left click between the HUD bands asks to respawn (FUN_14029ac10) */
    if (pressed && button == 1 && !me->alive && respawn_mode(client) && respawn_wait(client) == 0 &&
        my > SPEC_BAR_H && my < (float)client->config.window_height - SPEC_BAR_H) {
        request_respawn(client);
        return;
    }
    if (pressed && button == 2 && local_can_build_mode(client)) {
        wrench_attack2(client);
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
    if (client->buy.menu) {
        client->buy.menu = 0;
        return;
    }
    if (client->build_menu_open || client->build_mode) {
        client->build_menu_open = client->build_mode = false;
        return;
    }
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
