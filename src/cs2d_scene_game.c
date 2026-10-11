#include "cs2d_client.h"
#include "cs2d_weapons.h"
#include "cs2d_player.h"
#include "cs2d_game.h"
#include "cs2d_network.h"
#include <stdio.h>
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

/* Loads maps/<name>.map and its tileset (FUN_140113d50) into the running game */
static bool game_load_map(cs2d_client_t *client, const char *map_name)
{
    char map_path[128];
    snprintf(map_path, sizeof(map_path), "maps/%s.map", map_name);
    if (!cs2d_game_load_map(&client->game, map_path)) return false;

    cs2d_texture_free(&client->tex_tileset);
    cs2d_texture_load_tileset(&client->tex_tileset, client->game.map.tileset);
    cs2d_render_invalidate_shadows();
    cs2d_game_start_round(&client->game);

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

static void game_enter(cs2d_client_t *client)
{
    cs2d_game_init(&client->game, CS2D_MODE_DE);

    /* Local player (CT, Player 1) and practice bots exist before the map spawns them */
    client->local_player_id = 1;
    cs2d_game_add_player(&client->game, 1, client->config.name, CS2D_TEAM_CT);
    cs2d_game_add_player(&client->game, 2, "Bot_Bravo", CS2D_TEAM_T);
    cs2d_game_add_player(&client->game, 3, "Bot_Charlie", CS2D_TEAM_T);

    const char *map_name = client->pending_map[0] ? client->pending_map : "de_dust2";
    if (!game_load_map(client, map_name)) {
        cs2d_client_console_print(client, 255, 45, 45, "Error: Unable to load map 'maps/%s.map'", map_name);
        game_load_map(client, "cs_assault"); /* Fallback */
    }

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

    /* Load original sprite assets with exact CS2D magenta color-key maskMode = 2 and 32x32 cell padding */
    cs2d_texture_load_grid(&client->tex_ct,   "gfx/player/ct1.bmp",     2, 32, 32);
    cs2d_texture_load_grid(&client->tex_t,    "gfx/player/t1.bmp",      2, 32, 32);
    cs2d_texture_load_ex(&client->tex_ak47,   "gfx/weapons/ak47.bmp",   2);
    cs2d_texture_load_ex(&client->tex_m4a1,   "gfx/weapons/m4a1.bmp",   2);
    cs2d_texture_load_ex(&client->tex_deagle, "gfx/weapons/deagle.bmp", 2);

    cs2d_client_connect(client, client->config.server_ip, client->config.server_port);
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

    cs2d_game_free(&client->game);
    memset(&client->game, 0, sizeof(client->game));
    client->pending_map[0] = '\0';
}

static void game_update(cs2d_client_t *client, float delta_time)
{
    /* Update game simulation */
    cs2d_game_update(&client->game, delta_time);

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

        /* Movement */
        float speed = client->key_walk ? 100.0f : 190.0f; /* pixels/second */
        const cs2d_weapon_def_t *cur_wep = cs2d_weapon_get(pl->current_weapon);
        if (cur_wep) speed *= cur_wep->speed_modifier;

        float move_x = 0.0f, move_y = 0.0f;
        if (client->key_forward) move_y -= 1.0f;
        if (client->key_back)    move_y += 1.0f;
        if (client->key_left)    move_x -= 1.0f;
        if (client->key_right)   move_x += 1.0f;

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

        /* Firing */
        if (client->mouse_fire) {
            if (cs2d_weapon_fire(pl)) {
                /* Send fire event over network */
                if (client->connected && client->join_stage == 5) {
                    uint8_t fire_pkt[1] = { (uint8_t)CS2D_NET_FIRE };
                    cs2d_client_send_udp(client, fire_pkt, sizeof(fire_pkt), true);
                }
            }
        }
    }

    /* Camera tracks player smoothly */
    float target_cam_x = pl->pos.x - (float)client->config.window_width * 0.5f;
    float target_cam_y = pl->pos.y - (float)client->config.window_height * 0.5f;
    client->cam_x += (target_cam_x - client->cam_x) * 0.15f;
    client->cam_y += (target_cam_y - client->cam_y) * 0.15f;

    /* Network synchronization (every 33ms ~ 30Hz) */
    client->net_sync_timer += delta_time;
    if (client->net_sync_timer >= 0.033f) {
        client->net_sync_timer = 0.0f;
        if (client->connected && client->join_stage == 5 && pl->alive) {
            uint8_t sync_buf[16];
            /* Client -> Server posrot has player_id = 0 (7 bytes payload: [13, x, y, rot]) */
            size_t sync_len = cs2d_net_build_posrot_packet(0, pl->pos.x, pl->pos.y, pl->angle, sync_buf, sizeof(sync_buf));
            if (sync_len > 0) {
                cs2d_client_send_udp(client, sync_buf, sync_len, false);
            }
        }
    }

    /* Poll network packets */
    cs2d_client_poll_network(client);
}

static void game_render(cs2d_client_t *client, int screen_w, int screen_h)
{
    /* 1. Map Pass 0 & 1 (background, tileset, tile modifiers, ground entities, items, hostages) */
    cs2d_render_map_ex(&client->game.map, &client->tex_tileset, client->cam_x, client->cam_y, screen_w, screen_h);

    /* 2. Players (Tpl.draw: shadow, legs, torso frame, armor/helmet, held weapon, objective icon) */
    for (int i = 1; i <= CS2D_MAX_PLAYERS; i++) {
        const cs2d_player_t *p = &client->game.players[i];
        if (p->alive) {
            const cs2d_texture_t *stex = (p->team == CS2D_TEAM_CT) ? &client->tex_ct : &client->tex_t;
            const cs2d_texture_t *wtex = NULL;
            if (p->current_weapon == CS2D_WEP_AK47) wtex = &client->tex_ak47;
            else if (p->current_weapon == CS2D_WEP_M4A1) wtex = &client->tex_m4a1;
            else if (p->current_weapon == CS2D_WEP_DEAGLE) wtex = &client->tex_deagle;

            cs2d_render_player(p, stex, wtex, client->cam_x, client->cam_y);
        }
    }

    /* 3. Map Pass 2 (Overhead entities: Env_Sprite, Env_Image over players, Env_Object trees/lamps, Shadow Map) */
    cs2d_render_map_overhead(&client->game.map, client->cam_x, client->cam_y, screen_w, screen_h);

    /* 4. In-Game HUD (hud_nums.bmp, hud_symbols.bmp, hud_radar_advanced.png) */
    const cs2d_player_t *local_pl = &client->game.players[client->local_player_id];
    cs2d_render_hud(local_pl, &client->game, screen_w, screen_h);
}

static void game_handle_key(cs2d_client_t *client, int key, bool pressed)
{
    cs2d_player_t *pl = &client->game.players[client->local_player_id];

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
            if (pressed) cs2d_weapon_reload(pl);
            break;
        case 'B': case 'b':
            if (pressed) {
                /* Buy menu shortcut: purchase primary rifle / armor */
                uint8_t buy_wep = (pl->team == CS2D_TEAM_T) ? CS2D_WEP_AK47 : CS2D_WEP_M4A1;
                if (!cs2d_player_buy_weapon(pl, buy_wep)) {
                    cs2d_player_buy_weapon(pl, CS2D_WEP_DEAGLE);
                }
                cs2d_player_buy_weapon(pl, CS2D_WEP_KEVLARHELM);
            }
            break;
        case 'G': case 'g':
            if (pressed) cs2d_player_drop_weapon(pl);
            break;
        case '1':
            if (pressed) cs2d_player_switch_weapon(pl, CS2D_SLOT_PRIMARY);
            break;
        case '2':
            if (pressed) cs2d_player_switch_weapon(pl, CS2D_SLOT_SECONDARY);
            break;
        case '3':
            if (pressed) cs2d_player_switch_weapon(pl, CS2D_SLOT_MELEE);
            break;
        case '4':
            if (pressed) cs2d_player_switch_weapon(pl, CS2D_SLOT_GRENADE);
            break;
        case '5':
            if (pressed) cs2d_player_switch_weapon(pl, CS2D_SLOT_BOMB);
            break;
        default:
            break;
    }
}

static void game_handle_mouse(cs2d_client_t *client, float mx, float my, int button, bool pressed)
{
    (void)mx;
    (void)my;
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
