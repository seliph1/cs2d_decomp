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

/* Effects of offline entity logic (online the server performs them) */
static void drain_entity_events(cs2d_client_t *client)
{
    cs2d_ent_event_t ev;
    while (cs2d_ents_pop_event(&client->ents, &ev)) {
        const cs2d_entity_t *e = &client->game.map.entities[ev.entity];
        if (ev.type == CS2D_ENT_EVENT_MESSAGE && e->str_settings[0][0]) {
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

        /* Movement */
        float speed = client->key_walk ? 100.0f : 190.0f; /* pixels/second */
        const cs2d_weapon_def_t *cur_wep = cs2d_weapon_get(pl->current_weapon);
        if (cur_wep) speed *= cur_wep->speed_modifier;

        float move_x = 0.0f, move_y = 0.0f;
        if (client->key_forward) move_y -= 1.0f;
        if (client->key_back)    move_y += 1.0f;
        if (client->key_left)    move_x -= 1.0f;
        if (client->key_right)   move_x += 1.0f;

        /* "walkto" test command: steer through the path waypoints like a held movement key */
        while (client->walk_active) {
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

    /* Offline the client runs the entity logic itself; online the server does and sends the results */
    if (!client->connected) {
        if (client->ents_round != client->game.round_number) {
            client->ents_round = client->game.round_number;
            cs2d_ents_round_start(&client->ents);
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
