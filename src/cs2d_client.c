#include "cs2d_client.h"
#include "cs2d_weapons.h"
#include "cs2d_player.h"
#include "cs2d_map.h"
#include "cs2d_game.h"
#include "cs2d_network.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

void cs2d_client_console_print(cs2d_client_t *client, uint8_t r, uint8_t g, uint8_t b, const char *fmt, ...)
{
    if (!client || !fmt) return;

    char buf[CS2D_CONSOLE_LINE_LEN];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (client->console_line_count >= CS2D_CONSOLE_MAX_LINES) {
        memmove(&client->console_lines[0],
                &client->console_lines[1],
                sizeof(cs2d_console_line_t) * (CS2D_CONSOLE_MAX_LINES - 1));
        client->console_line_count = CS2D_CONSOLE_MAX_LINES - 1;
    }

    cs2d_console_line_t *line = &client->console_lines[client->console_line_count++];
    strncpy(line->text, buf, CS2D_CONSOLE_LINE_LEN - 1);
    line->text[CS2D_CONSOLE_LINE_LEN - 1] = '\0';
    line->r = r;
    line->g = g;
    line->b = b;

    /* Auto-scroll to bottom when new output arrives */
    client->console_scroll = 0;
}

bool cs2d_client_init(cs2d_client_t *client, const char *map_name)
{
    if (!client) return false;
    memset(client, 0, sizeof(*client));

    cs2d_ui_init(&client->ui);
    client->show_fps    = true;
    client->fps_current = 63;

    /* Default Create Server & Options UI state matching CS2D screenshots */
    strncpy(client->srv_name, "Unreal Software's CS2D", sizeof(client->srv_name) - 1);
    client->srv_name_cursor     = (int)strlen(client->srv_name);
    strncpy(client->srv_port, "36963", sizeof(client->srv_port) - 1);
    client->srv_port_cursor     = (int)strlen(client->srv_port);
    client->srv_max_players_idx = 1; /* "12" */
    client->srv_gamemode_idx    = 0; /* "Standard" */
    client->srv_fow_idx         = 0; /* "Off" */
    client->srv_reg_usgn        = true;
    client->srv_auto_balance    = true;
    client->srv_selected_map    = 4; /* "de_dust2" */

    client->opt_gore            = true;
    client->opt_auto_switch     = true;
    client->opt_hud_msgs        = true;
    client->opt_action_texts    = true;
    client->opt_dmg_indicator   = true;
    client->opt_chat_history    = true;

    /* Initialize authentic weapon table and load sys/weapons_recoil.cfg (FUN_1400e0fb0 & FUN_140112bc0) */
    cs2d_weapons_init();

    /* Load user config from sys/config.cfg */
    cs2d_config_load(&client->config, "sys/config.cfg");
    client->opt_name_cursor = (int)strlen(client->config.name);

    /* Initialize game state */
    cs2d_game_init(&client->game, CS2D_MODE_DE);

    const char *actual_map = (map_name && map_name[0]) ? map_name : "de_dust2";
    char map_path[128];
    snprintf(map_path, sizeof(map_path), "maps/%s.map", actual_map);
    if (!cs2d_game_load_map(&client->game, map_path)) {
        /* Fallback */
        actual_map = "cs_assault";
        cs2d_game_load_map(&client->game, "maps/cs_assault.map");
    }

    /* Load map tileset texture from gfx/tiles/<map.tileset> (FUN_140113d50) */
    cs2d_texture_load_tileset(&client->tex_tileset, client->game.map.tileset);
    cs2d_render_invalidate_shadows();

    /* Initialize local player as CT (Player 1) */
    client->local_player_id = 1;
    cs2d_game_add_player(&client->game, 1, client->config.name, CS2D_TEAM_CT);

    /* Add enemy bots at T spawn for practice */
    cs2d_game_add_player(&client->game, 2, "Bot_Bravo", CS2D_TEAM_T);
    cs2d_game_add_player(&client->game, 3, "Bot_Charlie", CS2D_TEAM_T);

    /* Start Round */
    cs2d_game_start_round(&client->game);

    /* Configure authentic player skins (look 0..3) and starting loadout */
    client->game.players[1].look  = 0; /* ct1.bmp */
    client->game.players[1].money = 16000;
    client->game.players[1].armor = 0;
    /* If on de_dust2, place player at exact CT spawn tile (31, 44.5) facing East */
    if (_stricmp(actual_map, "de_dust2") == 0) {
        client->game.players[1].pos.x = 31.0f * CS2D_TILE_SIZE;
        client->game.players[1].pos.y = 44.5f * CS2D_TILE_SIZE;
    }

    client->game.players[2].look = 0; /* t1.bmp */
    cs2d_player_give_weapon(&client->game.players[2], CS2D_WEP_KEVLARHELM);
    cs2d_player_give_weapon(&client->game.players[2], CS2D_WEP_BOMB);
    cs2d_player_give_weapon(&client->game.players[2], CS2D_WEP_AK47);

    client->game.players[3].look = 2; /* t3.bmp */
    cs2d_player_give_weapon(&client->game.players[3], CS2D_WEP_DEAGLE);

    /* Center camera and default mouse aim on local player immediately */
    client->cam_x = client->game.players[1].pos.x - (float)client->config.window_width * 0.5f;
    client->cam_y = client->game.players[1].pos.y - (float)client->config.window_height * 0.5f;
    client->mouse_screen_x = (float)client->config.window_width * 0.5f + 110.0f;
    client->mouse_screen_y = (float)client->config.window_height * 0.5f - 10.0f;

    /* Initialize global CS2D sprite bank (player skins, legs, equipment, weapons, HUD, entities) */
    cs2d_gfx_bank_init(cs2d_render_get_gfx_bank());

    /* Load original sprite assets with exact CS2D magenta color-key maskMode = 2 and 32x32 cell padding */
    cs2d_texture_load_grid(&client->tex_ct,   "gfx/player/ct1.bmp",     2, 32, 32);
    cs2d_texture_load_grid(&client->tex_t,    "gfx/player/t1.bmp",      2, 32, 32);
    cs2d_texture_load_ex(&client->tex_ak47,   "gfx/weapons/ak47.bmp",   2);
    cs2d_texture_load_ex(&client->tex_m4a1,   "gfx/weapons/m4a1.bmp",   2);
    cs2d_texture_load_ex(&client->tex_deagle, "gfx/weapons/deagle.bmp", 2);

    /* Populate authentic startup Console log lines matching CS2D.exe */
    cs2d_client_console_print(client, 255, 220, 0,   "Steam: Not logged in");
    cs2d_client_console_print(client, 170, 170, 170, "UDP socket initialized using port 59960");
    cs2d_client_console_print(client, 255, 45,  45,  "U.S.G.N.: Transmission cancelled by user. Unable to check version/news");
    cs2d_client_console_print(client, 170, 170, 170, "load map '%s'", actual_map);

    /* Setup network */
    client->net_sock = INVALID_SOCKET;
    client->connected = false;
    cs2d_client_connect(client, client->config.server_ip, client->config.server_port);

    return true;
}

void cs2d_client_shutdown(cs2d_client_t *client)
{
    if (!client) return;

    if (client->net_sock != INVALID_SOCKET) {
        if (client->join_stage >= 1) {
            uint8_t leave_pkt[2] = { (uint8_t)CS2D_NET_LEAVE, 0 };
            cs2d_client_send_udp(client, leave_pkt, sizeof(leave_pkt), true);
        }
        closesocket(client->net_sock);
        client->net_sock = INVALID_SOCKET;
    }

    cs2d_texture_free(&client->tex_tileset);
    cs2d_texture_free(&client->tex_ct);
    cs2d_texture_free(&client->tex_t);
    cs2d_texture_free(&client->tex_ak47);
    cs2d_texture_free(&client->tex_m4a1);
    cs2d_texture_free(&client->tex_deagle);
    cs2d_gfx_bank_free(cs2d_render_get_gfx_bank());

    cs2d_game_free(&client->game);
}

void cs2d_client_send_udp(cs2d_client_t *client, const uint8_t *payload, size_t len, bool reliable)
{
    if (!client || client->net_sock == INVALID_SOCKET || !payload || len == 0 || len > 2046) return;

    uint8_t dgram[2048];
    if (client->net_send_seq == 0) client->net_send_seq = 2;

    uint16_t hdr = reliable ? client->net_send_seq : (uint16_t)(client->net_send_seq - 1);
    if (reliable) {
        client->net_send_seq += 2;
        if (client->net_send_seq == 0) client->net_send_seq = 2;
    }

    memcpy(dgram, &hdr, 2);
    memcpy(dgram + 2, payload, len);
    int sent = sendto(client->net_sock, (const char *)dgram, (int)(len + 2), 0,
                      (struct sockaddr *)&client->server_addr, sizeof(client->server_addr));
    if (sent > 0) {
        client->net_bytes_up_accum += (uint32_t)sent;
    }
}

static void cs2d_client_send_ack(cs2d_client_t *client, uint16_t srv_seq)
{
    if (!client || client->net_sock == INVALID_SOCKET) return;
    if (client->net_send_seq == 0) client->net_send_seq = 2;

    uint8_t dgram[5];
    uint16_t hdr = (uint16_t)(client->net_send_seq - 1);
    memcpy(dgram, &hdr, 2);
    dgram[2] = 0x01;
    memcpy(dgram + 3, &srv_seq, 2);
    int sent = sendto(client->net_sock, (const char *)dgram, 5, 0,
                      (struct sockaddr *)&client->server_addr, sizeof(client->server_addr));
    if (sent > 0) {
        client->net_bytes_up_accum += (uint32_t)sent;
    }
}

bool cs2d_client_connect(cs2d_client_t *client, const char *ip, int port)
{
    if (!client) return false;

    if (client->net_sock != INVALID_SOCKET) {
        closesocket(client->net_sock);
        client->net_sock = INVALID_SOCKET;
    }

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;

    client->net_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (client->net_sock == INVALID_SOCKET) return false;

    u_long non_blocking = 1;
    ioctlsocket(client->net_sock, FIONBIO, &non_blocking);

    memset(&client->server_addr, 0, sizeof(client->server_addr));
    client->server_addr.sin_family = AF_INET;
    client->server_addr.sin_port = htons((u_short)(port > 0 ? port : 36963));
    client->server_addr.sin_addr.s_addr = inet_addr((ip && ip[0]) ? ip : "127.0.0.1");

    client->net_send_seq = 2;
    client->join_stage   = 0;
    client->connected    = false;

    /* Initiate Opcode 252 Stage 0 Join Request ([252, 0, ""]) */
    cs2d_pkt_t p0;
    cs2d_net_build_join_stage0(&p0);
    cs2d_client_send_udp(client, p0.buf, (size_t)p0.pos, true);

    return true;
}

void cs2d_client_poll_network(cs2d_client_t *client)
{
    if (!client || client->net_sock == INVALID_SOCKET) return;

    uint8_t buffer[4096];
    struct sockaddr_in from;
    int fromlen = sizeof(from);

    int recvd = recvfrom(client->net_sock, (char *)buffer, sizeof(buffer), 0,
                         (struct sockaddr *)&from, &fromlen);

    while (recvd >= 2) {
        client->net_bytes_down_accum += (uint32_t)recvd;

        uint16_t srv_seq = 0;
        memcpy(&srv_seq, buffer, 2);
        int pos = 2;

        if ((srv_seq & 1) == 1) {
            /* Unreliable Tudp packet: strip leading 3-byte ACK/control triples (1..4) */
            while (pos + 3 <= recvd && buffer[pos] >= 1 && buffer[pos] <= 4) {
                pos += 3;
            }
        } else if (srv_seq != 0) {
            /* Reliable Tudp packet: immediately acknowledge srv_seq */
            cs2d_client_send_ack(client, srv_seq);
        }

        while (pos < recvd) {
            uint8_t opcode = buffer[pos];

            if (opcode == CS2D_NET_JOIN) {
                pos++;
                if (pos >= recvd) break;
                uint8_t sub = buffer[pos++];

                if (sub == 0 && client->join_stage == 0) {
                    /* Stage 0 reply: [252, 0, RStr(raw_secret)] -> Send [252, 1] */
                    cs2d_pkt_r_str(buffer, recvd, &pos, client->net_raw_secret, sizeof(client->net_raw_secret));
                    cs2d_pkt_t p1;
                    cs2d_net_build_join_stage1(
                        &p1,
                        client->config.name,
                        "",
                        client->net_raw_secret,
                        client->config.window_width,
                        client->config.window_height
                    );
                    cs2d_client_send_udp(client, p1.buf, (size_t)p1.pos, true);
                    client->join_stage = 1;
                    break;
                } else if (sub == 2) {
                    /* Stage 1 reply: [252, 2, status, pid, RStr(map_name), RStr(raw_secret)] */
                    if (pos >= recvd) break;
                    uint8_t status = buffer[pos++];
                    if (status == 0 && pos < recvd) {
                        uint8_t pid = buffer[pos++];
                        cs2d_pkt_r_str(buffer, recvd, &pos, client->net_map_name, sizeof(client->net_map_name));
                        cs2d_pkt_r_str(buffer, recvd, &pos, client->net_raw_secret, sizeof(client->net_raw_secret));

                        /* Clear offline bots and bind local player to server-assigned ID */
                        for (int i = 1; i <= CS2D_MAX_PLAYERS; i++) {
                            memset(&client->game.players[i], 0, sizeof(cs2d_player_t));
                        }
                        client->local_player_id = (pid > 0 && pid <= CS2D_MAX_PLAYERS) ? pid : 1;
                        cs2d_game_add_player(&client->game, client->local_player_id, client->config.name, CS2D_TEAM_CT);

                        char map_path[256];
                        snprintf(map_path, sizeof(map_path), "maps/%s.map", client->net_map_name);
                        cs2d_net_sha256_file(map_path, client->net_map_checksum);

                        cs2d_pkt_t p3;
                        cs2d_net_build_join_stage2(&p3, client->net_map_checksum, client->net_raw_secret);
                        cs2d_client_send_udp(client, p3.buf, (size_t)p3.pos, true);
                        client->join_stage = 2;
                    }
                    break;
                } else if (sub == 4) {
                    /* Stage 2 reply: [252, 4, status] -> Send [252, 5] */
                    if (pos >= recvd) break;
                    uint8_t status = buffer[pos++];
                    if (status == 0) {
                        cs2d_pkt_t p5;
                        cs2d_net_build_join_stage3(&p5, client->net_map_name, client->net_map_checksum);
                        cs2d_client_send_udp(client, p5.buf, (size_t)p5.pos, true);
                        client->join_stage = 3;
                    }
                    break;
                } else if (sub == 6) {
                    /* Stage 3 reply: [252, 6, status, ...] */
                    if (pos >= recvd) break;
                    uint8_t status = buffer[pos++];
                    if (status == 0) {
                        client->join_stage = 4;
                    }
                    break;
                } else if (sub == 7) {
                    /* Stage 4 snapshot chunks: [252, 7, snap_type, ...] */
                    if (pos >= recvd) break;
                    uint8_t snap_type = buffer[pos++];
                    if (snap_type == 200) {
                        client->join_stage = 5;
                        client->connected  = true;
                        printf("[CS2D Net] Connected to server '%s' (player_id=%u)! Joining CT...\n",
                               client->net_map_name, client->local_player_id);
                        cs2d_client_console_print(client, 100, 255, 100,
                                                  "Connected to server (map: %s, player ID: %u)",
                                                  client->net_map_name, client->local_player_id);

                        /* Send Team Selection [20, 2 (CT), 0 (look 0)] */
                        uint8_t team_pkt[4];
                        size_t team_len = cs2d_net_build_team_packet(0, CS2D_TEAM_CT, 0, team_pkt, sizeof(team_pkt));
                        if (team_len > 0) {
                            cs2d_client_send_udp(client, team_pkt, team_len, true);
                        }
                    }
                    break;
                } else {
                    break;
                }
            } else if (opcode == CS2D_NET_TEAM) {
                /* Server -> Client: [20, pid, team, look] (4 bytes) */
                if (pos + 4 > recvd) break;
                uint8_t pid  = buffer[pos + 1];
                uint8_t team = buffer[pos + 2];
                uint8_t look = buffer[pos + 3];
                if (pid > 0 && pid <= CS2D_MAX_PLAYERS) {
                    client->game.players[pid].id   = pid;
                    client->game.players[pid].team = (cs2d_team_t)team;
                    client->game.players[pid].look = look;
                }
                pos += 4;
            } else if (opcode == CS2D_NET_SPAWN) {
                uint8_t pid = 0, wep = 0, arm = 0, item_cnt = 0;
                uint16_t sx = 0, sy = 0, money = 0;
                uint8_t items[32];
                size_t bytes_read = 0;
                if (cs2d_net_parse_spawn_packet(buffer + pos, (size_t)(recvd - pos),
                                                &pid, &sx, &sy, &wep, &arm, &money,
                                                items, &item_cnt, &bytes_read)) {
                    if (pid > 0 && pid <= CS2D_MAX_PLAYERS) {
                        cs2d_player_t *pl = &client->game.players[pid];
                        pl->id             = pid;
                        pl->alive          = true;
                        pl->health         = 100;
                        pl->max_health     = 100;
                        pl->armor          = arm;
                        pl->money          = money;
                        pl->pos.x          = (float)sx;
                        pl->pos.y          = (float)sy;
                        cs2d_player_give_weapon(pl, CS2D_WEP_KNIFE);
                        for (uint8_t k = 0; k < item_cnt && k < 32; k++) {
                            cs2d_player_give_weapon(pl, items[k]);
                        }
                        if (wep > 0) {
                            pl->current_weapon = wep;
                        }
                        if (pid == client->local_player_id) {
                            client->cam_x = pl->pos.x - (float)client->config.window_width * 0.5f;
                            client->cam_y = pl->pos.y - (float)client->config.window_height * 0.5f;
                            printf("[CS2D Net] Local player %u spawned at (%u, %u) money=$%u!\n",
                                   pid, sx, sy, money);
                        }
                    }
                    pos += (int)bytes_read;
                } else {
                    break;
                }
            } else if (opcode == CS2D_NET_POS_RUN || opcode == CS2D_NET_POS_WALK) {
                /* Server -> Client: [10/11, pid, uint16_t x, uint16_t y] (6 bytes) */
                if (pos + 6 > recvd) break;
                uint8_t pid = buffer[pos + 1];
                uint16_t ix = (uint16_t)buffer[pos + 2] | ((uint16_t)buffer[pos + 3] << 8);
                uint16_t iy = (uint16_t)buffer[pos + 4] | ((uint16_t)buffer[pos + 5] << 8);
                if (pid > 0 && pid <= CS2D_MAX_PLAYERS && pid != client->local_player_id) {
                    client->game.players[pid].pos.x = (float)ix;
                    client->game.players[pid].pos.y = (float)iy;
                    client->game.players[pid].alive = true;
                }
                pos += 6;
            } else if (opcode == CS2D_NET_ROT) {
                /* Server -> Client: [12, pid, uint16_t rot] (4 bytes) */
                if (pos + 4 > recvd) break;
                uint8_t pid = buffer[pos + 1];
                uint16_t irot = (uint16_t)buffer[pos + 2] | ((uint16_t)buffer[pos + 3] << 8);
                if (pid > 0 && pid <= CS2D_MAX_PLAYERS && pid != client->local_player_id) {
                    client->game.players[pid].angle = (float)irot * CS2D_NET_ROT_SCALE;
                }
                pos += 4;
            } else if (opcode == CS2D_NET_POSROT || opcode == CS2D_NET_POSROT2) {
                if (pos + 8 > recvd) break;
                uint8_t pid = 0;
                float px = 0.0f, py = 0.0f, prot = 0.0f;
                if (cs2d_net_parse_posrot_packet(buffer + pos, 8, &pid, &px, &py, &prot)) {
                    if (pid > 0 && pid <= CS2D_MAX_PLAYERS && pid != client->local_player_id) {
                        client->game.players[pid].pos.x = px;
                        client->game.players[pid].pos.y = py;
                        client->game.players[pid].angle = prot;
                        client->game.players[pid].alive = true;
                    }
                }
                pos += 8;
            } else if (opcode == CS2D_NET_DAMAGE) {
                if (pos + 5 > recvd) break;
                uint8_t victim = 0, attacker = 0, new_hp = 0, new_arm = 0;
                if (cs2d_net_parse_damage_packet(buffer + pos, 5, &victim, &attacker, &new_hp, &new_arm)) {
                    if (victim > 0 && victim <= CS2D_MAX_PLAYERS) {
                        client->game.players[victim].health = new_hp;
                        client->game.players[victim].armor  = new_arm;
                        if (new_hp == 0) {
                            client->game.players[victim].alive = false;
                        }
                    }
                }
                pos += 5;
            } else if (opcode == CS2D_NET_PING_INGAME) {
                /* Server -> Client: [249, int32_t ts] -> Reply [249, int32_t ts] (0x1401d9f10) */
                if (pos + 5 > recvd) break;
                uint8_t pong[5];
                memcpy(pong, buffer + pos, 5);
                cs2d_client_send_udp(client, pong, 5, false);
                pos += 5;
            } else if (opcode == CS2D_NET_CLOCK_PING) {
                if (pos + 5 > recvd) break;
                pos += 5;
            } else {
                break;
            }
        }

        recvd = recvfrom(client->net_sock, (char *)buffer, sizeof(buffer), 0,
                         (struct sockaddr *)&from, &fromlen);
    }
}

static bool str_contains_icase(const char *haystack, const char *needle)
{
    if (!haystack || !needle || !needle[0]) return false;
    size_t nlen = strlen(needle);
    for (const char *p = haystack; *p != '\0'; p++) {
        if (_strnicmp(p, needle, nlen) == 0) return true;
    }
    return false;
}

void cs2d_client_console_exec(cs2d_client_t *client, const char *cmd_line)
{
    if (!client || !cmd_line) return;

    while (*cmd_line == ' ' || *cmd_line == '\t') cmd_line++;
    if (*cmd_line == '\0') return;

    /* Echo entered command in yellow like CS2D */
    cs2d_client_console_print(client, 255, 220, 0, "> %s", cmd_line);

    char cmd[64]  = {0};
    char arg1[64] = {0};
    char arg2[64] = {0};
    int n = sscanf(cmd_line, "%63s %63s %63s", cmd, arg1, arg2);
    if (n <= 0) return;

    cs2d_player_t *pl = &client->game.players[client->local_player_id];

    if (_stricmp(cmd, "help") == 0 || _stricmp(cmd, "cmdlist") == 0 || _stricmp(cmd, "?") == 0) {
        cs2d_client_console_print(client, 255, 220, 0,   "--- CS2D Console Commands ---");
        cs2d_client_console_print(client, 220, 220, 220, "map <name>          - Load map from maps/<name>.map (see: maps)");
        cs2d_client_console_print(client, 220, 220, 220, "maps / maplist      - List all maps in maps/ directory");
        cs2d_client_console_print(client, 220, 220, 220, "equip <id|name>     - Equip weapon/item (e.g. equip ak47, equip awp, equip 30)");
        cs2d_client_console_print(client, 220, 220, 220, "weapons / itemlist  - List all weapons & IDs");
        cs2d_client_console_print(client, 220, 220, 220, "sun <0..360>        - Set map sun angle (daylight_time) & rebuild shadows");
        cs2d_client_console_print(client, 220, 220, 220, "shadows [0|1]       - Toggle map shadow & lighting overlay");
        cs2d_client_console_print(client, 220, 220, 220, "tp <tx> <ty>        - Teleport player to tile (tx, ty) (see: pos)");
        cs2d_client_console_print(client, 220, 220, 220, "hp <val> / armor <v>- Set player health / armor (money <val>, team <t|ct>)");
        cs2d_client_console_print(client, 220, 220, 220, "bot_add [t|ct]      - Spawn a bot (bot_kill to remove bots)");
        cs2d_client_console_print(client, 220, 220, 220, "status / entlist    - Inspect map, entities, player & network state");
        cs2d_client_console_print(client, 220, 220, 220, "connect <ip> [port] - Connect to CS2D server (disconnect, fps, clear, quit)");
    } else if (_stricmp(cmd, "clear") == 0 || _stricmp(cmd, "cls") == 0) {
        client->console_line_count = 0;
        client->console_scroll     = 0;
    } else if (_stricmp(cmd, "fps") == 0 || _stricmp(cmd, "showfps") == 0) {
        if (n >= 2) {
            client->show_fps = (atoi(arg1) != 0);
        } else {
            client->show_fps = !client->show_fps;
        }
        cs2d_client_console_print(client, 170, 170, 170, "FPS display: %s", client->show_fps ? "ON" : "OFF");
    } else if (_stricmp(cmd, "shadows") == 0 || _stricmp(cmd, "shadow") == 0) {
        bool en = (n >= 2) ? (atoi(arg1) != 0) : !cs2d_render_get_shadows_enabled();
        cs2d_render_set_shadows_enabled(en);
        cs2d_client_console_print(client, 170, 170, 170, "Map shadows & lighting: %s", en ? "ENABLED" : "DISABLED");
    } else if (_stricmp(cmd, "sun") == 0 || _stricmp(cmd, "daylight") == 0 || _stricmp(cmd, "light") == 0) {
        if (n < 2) {
            cs2d_client_console_print(client, 170, 170, 170,
                                      "Current map sun angle (daylight_time): %d deg (Usage: sun <0..360>)",
                                      client->game.map.daylight_time);
        } else {
            int ang = atoi(arg1);
            if (ang < 0) ang = 0;
            if (ang > 360) ang = 360;
            client->game.map.daylight_time = ang;
            cs2d_render_invalidate_shadows();
            cs2d_client_console_print(client, 100, 255, 100,
                                      "Updated map sun angle (daylight_time) to %d deg and rebuilt shadow map.", ang);
        }
    } else if (_stricmp(cmd, "map") == 0 || _stricmp(cmd, "changemap") == 0 || _stricmp(cmd, "loadmap") == 0) {
        if (n < 2) {
            cs2d_client_console_print(client, 255, 45, 45, "Usage: map <map_name> (e.g. map de_dust2, map cs_office)");
            return;
        }
        /* Strip .map suffix if provided */
        char mname[64];
        strncpy(mname, arg1, sizeof(mname) - 1);
        mname[sizeof(mname) - 1] = '\0';
        char *dot = strstr(mname, ".map");
        if (dot) *dot = '\0';

        char map_path[128];
        snprintf(map_path, sizeof(map_path), "maps/%s.map", mname);
        if (!cs2d_game_load_map(&client->game, map_path)) {
            cs2d_client_console_print(client, 255, 45, 45, "Error: Unable to load map '%s'", map_path);
            return;
        }

        cs2d_texture_free(&client->tex_tileset);
        cs2d_texture_load_tileset(&client->tex_tileset, client->game.map.tileset);
        cs2d_render_invalidate_shadows();
        cs2d_game_start_round(&client->game);

        if (_stricmp(mname, "de_dust2") == 0) {
            pl->pos.x = 31.0f * CS2D_TILE_SIZE;
            pl->pos.y = 44.5f * CS2D_TILE_SIZE;
        }
        client->cam_x = pl->pos.x - (float)client->config.window_width * 0.5f;
        client->cam_y = pl->pos.y - (float)client->config.window_height * 0.5f;

        cs2d_client_console_print(client, 170, 170, 170, "load map '%s' (%ux%u, tileset: %s, sun: %d deg)",
                                  mname, client->game.map.width, client->game.map.height,
                                  client->game.map.tileset, client->game.map.daylight_time);
    } else if (_stricmp(cmd, "maps") == 0 || _stricmp(cmd, "maplist") == 0) {
        WIN32_FIND_DATAA fd;
        HANDLE hFind = FindFirstFileA("maps\\*.map", &fd);
        if (hFind == INVALID_HANDLE_VALUE) {
            cs2d_client_console_print(client, 255, 45, 45, "No maps found in maps/ directory.");
        } else {
            char line_buf[CS2D_CONSOLE_LINE_LEN] = {0};
            int count = 0;
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    char mname[64];
                    strncpy(mname, fd.cFileName, sizeof(mname) - 1);
                    mname[sizeof(mname) - 1] = '\0';
                    char *dot = strstr(mname, ".map");
                    if (dot) *dot = '\0';

                    if (strlen(line_buf) + strlen(mname) + 3 >= sizeof(line_buf)) {
                        cs2d_client_console_print(client, 170, 170, 170, "%s", line_buf);
                        line_buf[0] = '\0';
                    }
                    if (line_buf[0] != '\0') strcat(line_buf, ", ");
                    strcat(line_buf, mname);
                    count++;
                }
            } while (FindNextFileA(hFind, &fd));
            FindClose(hFind);
            if (line_buf[0] != '\0') {
                cs2d_client_console_print(client, 170, 170, 170, "%s", line_buf);
            }
            cs2d_client_console_print(client, 255, 220, 0, "Total maps available: %d", count);
        }
    } else if (_stricmp(cmd, "equip") == 0 || _stricmp(cmd, "give") == 0 || _stricmp(cmd, "item") == 0) {
        if (n < 2) {
            cs2d_client_console_print(client, 255, 45, 45, "Usage: equip <weapon_id | weapon_name> (e.g. equip ak47, equip awp, equip 45)");
            return;
        }
        int wep_id = atoi(arg1);
        if (wep_id <= 0) {
            const char *query = cmd_line + strlen(cmd);
            while (*query == ' ') query++;
            for (int i = 1; i < CS2D_MAX_WEAPONS; i++) {
                const cs2d_weapon_def_t *w = cs2d_weapon_get(i);
                if (w && w->name && w->name[0] && _stricmp(w->name, query) == 0) {
                    wep_id = i;
                    break;
                }
            }
            if (wep_id <= 0) {
                for (int i = 1; i < CS2D_MAX_WEAPONS; i++) {
                    const cs2d_weapon_def_t *w = cs2d_weapon_get(i);
                    if (w && w->name && w->name[0] && str_contains_icase(w->name, query)) {
                        wep_id = i;
                        break;
                    }
                }
            }
        }
        const cs2d_weapon_def_t *wdef = cs2d_weapon_get(wep_id);
        if (!wdef || !wdef->name || !wdef->name[0]) {
            cs2d_client_console_print(client, 255, 45, 45, "Unknown weapon/item '%s'. Type 'weapons' to list all IDs.", arg1);
            return;
        }
        cs2d_player_give_weapon(pl, (uint8_t)wep_id);
        if (wdef->slot <= CS2D_SLOT_BOMB) {
            pl->current_weapon = (uint8_t)wep_id;
        }
        cs2d_client_console_print(client, 100, 255, 100, "Equipped [%d] %s (dmg=%d, clip=%d/%d)",
                                  wep_id, wdef->name, wdef->damage, wdef->clip_size, wdef->max_reserve);
    } else if (_stricmp(cmd, "weapons") == 0 || _stricmp(cmd, "itemlist") == 0) {
        char line_buf[CS2D_CONSOLE_LINE_LEN] = {0};
        for (int i = 1; i < CS2D_MAX_WEAPONS; i++) {
            const cs2d_weapon_def_t *w = cs2d_weapon_get(i);
            if (!w || !w->name || !w->name[0] || strcmp(w->name, "Unknown") == 0) continue;
            char entry[48];
            snprintf(entry, sizeof(entry), "[%d] %s", i, w->name);
            if (strlen(line_buf) + strlen(entry) + 3 >= sizeof(line_buf)) {
                cs2d_client_console_print(client, 170, 170, 170, "%s", line_buf);
                line_buf[0] = '\0';
            }
            if (line_buf[0] != '\0') strcat(line_buf, "  ");
            strcat(line_buf, entry);
        }
        if (line_buf[0] != '\0') {
            cs2d_client_console_print(client, 170, 170, 170, "%s", line_buf);
        }
    } else if (_stricmp(cmd, "tp") == 0 || _stricmp(cmd, "setpos") == 0) {
        if (n < 3) {
            cs2d_client_console_print(client, 255, 45, 45, "Usage: tp <tile_x> <tile_y>");
            return;
        }
        float x = (float)atof(arg1);
        float y = (float)atof(arg2);
        if (x <= (float)client->game.map.width && y <= (float)client->game.map.height) {
            pl->pos.x = x * CS2D_TILE_SIZE + 16.0f;
            pl->pos.y = y * CS2D_TILE_SIZE + 16.0f;
        } else {
            pl->pos.x = x;
            pl->pos.y = y;
        }
        client->cam_x = pl->pos.x - (float)client->config.window_width * 0.5f;
        client->cam_y = pl->pos.y - (float)client->config.window_height * 0.5f;
        cs2d_client_console_print(client, 100, 255, 100, "Teleported to tile (%.1f, %.1f) [px: %.0f, %.0f]",
                                  pl->pos.x / 32.0f, pl->pos.y / 32.0f, pl->pos.x, pl->pos.y);
    } else if (_stricmp(cmd, "pos") == 0 || _stricmp(cmd, "where") == 0) {
        cs2d_client_console_print(client, 170, 170, 170,
                                  "Player '%s' at tile (%.1f, %.1f) | pixel (%.1f, %.1f) | angle %.1f deg",
                                  pl->name, pl->pos.x / 32.0f, pl->pos.y / 32.0f, pl->pos.x, pl->pos.y, pl->angle);
    } else if (_stricmp(cmd, "hp") == 0 || _stricmp(cmd, "sethealth") == 0 || _stricmp(cmd, "god") == 0) {
        int hp = (_stricmp(cmd, "god") == 0) ? 250 : ((n >= 2) ? atoi(arg1) : 100);
        if (hp < 0) hp = 0;
        if (hp > 250) hp = 250;
        pl->health     = (uint8_t)hp;
        pl->max_health = (uint8_t)(hp > 100 ? hp : 100);
        pl->alive      = (hp > 0);
        cs2d_client_console_print(client, 100, 255, 100, "Set local player health to %d", hp);
    } else if (_stricmp(cmd, "armor") == 0 || _stricmp(cmd, "setarmor") == 0) {
        int arm = (n >= 2) ? atoi(arg1) : 200;
        if (arm < 0) arm = 0;
        if (arm > 250) arm = 250;
        pl->armor = (uint8_t)arm;
        cs2d_client_console_print(client, 100, 255, 100, "Set local player armor to %d", arm);
    } else if (_stricmp(cmd, "money") == 0 || _stricmp(cmd, "setmoney") == 0) {
        int m = (n >= 2) ? atoi(arg1) : 16000;
        if (m < 0) m = 0;
        if (m > 16000) m = 16000;
        pl->money = (uint16_t)m;
        cs2d_client_console_print(client, 100, 255, 100, "Set local player money to $%d", m);
    } else if (_stricmp(cmd, "team") == 0) {
        if (n >= 2 && (_stricmp(arg1, "t") == 0 || _stricmp(arg1, "1") == 0)) {
            pl->team = CS2D_TEAM_T;
            cs2d_client_console_print(client, 255, 220, 0, "Switched local player to Terrorists (T)");
        } else {
            pl->team = CS2D_TEAM_CT;
            cs2d_client_console_print(client, 100, 255, 100, "Switched local player to Counter-Terrorists (CT)");
        }
    } else if (_stricmp(cmd, "bot_add") == 0 || _stricmp(cmd, "addbot") == 0) {
        cs2d_team_t bteam = CS2D_TEAM_T;
        if (n >= 2 && (_stricmp(arg1, "ct") == 0 || _stricmp(arg1, "2") == 0)) {
            bteam = CS2D_TEAM_CT;
        }
        int slot = -1;
        for (int i = 2; i <= CS2D_MAX_PLAYERS; i++) {
            if (!client->game.players[i].alive && client->game.players[i].id == 0) {
                slot = i;
                break;
            }
        }
        if (slot == -1) {
            for (int i = 2; i <= CS2D_MAX_PLAYERS; i++) {
                if (!client->game.players[i].alive) {
                    slot = i;
                    break;
                }
            }
        }
        if (slot == -1) {
            cs2d_client_console_print(client, 255, 45, 45, "No free player slots for bot.");
        } else {
            char bname[32];
            if (n >= 3) {
                snprintf(bname, sizeof(bname), "%.31s", arg2);
            } else {
                snprintf(bname, sizeof(bname), "Bot_%d", slot);
            }
            cs2d_game_add_player(&client->game, (uint8_t)slot, bname, bteam);
            cs2d_player_t *bot = &client->game.players[slot];
            bot->alive      = true;
            bot->health     = 100;
            bot->max_health = 100;
            bot->pos.x      = pl->pos.x + 64.0f;
            bot->pos.y      = pl->pos.y;
            cs2d_player_give_weapon(bot, (bteam == CS2D_TEAM_T) ? CS2D_WEP_AK47 : CS2D_WEP_M4A1);
            cs2d_client_console_print(client, 100, 255, 100, "Spawned %s (%s) at (%.0f, %.0f)",
                                      bname, (bteam == CS2D_TEAM_T) ? "T" : "CT", bot->pos.x / 32.0f, bot->pos.y / 32.0f);
        }
    } else if (_stricmp(cmd, "bot_kill") == 0 || _stricmp(cmd, "killbots") == 0) {
        int killed = 0;
        for (int i = 1; i <= CS2D_MAX_PLAYERS; i++) {
            if (i != client->local_player_id && client->game.players[i].alive) {
                client->game.players[i].alive  = false;
                client->game.players[i].health = 0;
                killed++;
            }
        }
        cs2d_client_console_print(client, 255, 220, 0, "Killed %d bot(s).", killed);
    } else if (_stricmp(cmd, "status") == 0 || _stricmp(cmd, "info") == 0) {
        const cs2d_weapon_def_t *wdef = cs2d_weapon_get(pl->current_weapon);
        cs2d_client_console_print(client, 255, 220, 0,
                                  "Map: %ux%u tiles | Tileset: %s | Sun Angle: %d deg | Entities: %u",
                                  client->game.map.width, client->game.map.height,
                                  client->game.map.tileset, client->game.map.daylight_time,
                                  client->game.map.entity_count);
        cs2d_client_console_print(client, 170, 170, 170,
                                  "Player [%u] '%s' | HP: %u | Armor: %u | Money: $%u | Weapon: %s",
                                  pl->id, pl->name, pl->health, pl->armor, pl->money,
                                  (wdef && wdef->name) ? wdef->name : "None");
        cs2d_client_console_print(client, 170, 170, 170,
                                  "Network: %s (join_stage=%d, up=%ub/s, down=%ub/s)",
                                  client->connected ? "CONNECTED" : "OFFLINE",
                                  client->join_stage, client->net_bps_up, client->net_bps_down);
    } else if (_stricmp(cmd, "entlist") == 0 || _stricmp(cmd, "entities") == 0) {
        int filter_type = (n >= 2) ? atoi(arg1) : -1;
        int shown = 0;
        for (uint32_t i = 0; i < client->game.map.entity_count; i++) {
            const cs2d_entity_t *e = &client->game.map.entities[i];
            if (filter_type >= 0 && (int)e->type != filter_type) continue;
            cs2d_client_console_print(client, 170, 170, 170,
                                      "#%u type=%u '%s' at (%d,%d) int=[%d,%d,%d,%d] str0='%s'",
                                      i, (unsigned)e->type, e->name, e->x, e->y,
                                      e->int_settings[0], e->int_settings[1],
                                      e->int_settings[2], e->int_settings[3],
                                      e->str_settings[0]);
            if (++shown >= 40 && filter_type < 0) {
                cs2d_client_console_print(client, 255, 220, 0,
                                          "... (%u total entities; use 'entlist <type>' to filter)",
                                          client->game.map.entity_count);
                break;
            }
        }
    } else if (_stricmp(cmd, "connect") == 0) {
        const char *ip = (n >= 2) ? arg1 : client->config.server_ip;
        int port = (n >= 3) ? atoi(arg2) : client->config.server_port;
        if (port <= 0) port = 36963;
        cs2d_client_console_print(client, 170, 170, 170, "Connecting to %s:%d ...", ip, port);
        cs2d_client_connect(client, ip, port);
    } else if (_stricmp(cmd, "disconnect") == 0) {
        if (client->net_sock != INVALID_SOCKET) {
            closesocket(client->net_sock);
            client->net_sock = INVALID_SOCKET;
        }
        client->connected  = false;
        client->join_stage = 0;
        cs2d_client_console_print(client, 255, 220, 0, "Disconnected from server.");
    } else if (_stricmp(cmd, "name") == 0) {
        if (n >= 2) {
            strncpy(client->config.name, arg1, sizeof(client->config.name) - 1);
            strncpy(pl->name, arg1, sizeof(pl->name) - 1);
            client->opt_name_cursor = (int)strlen(client->config.name);
            cs2d_client_console_print(client, 100, 255, 100, "Player name changed to '%s'", pl->name);
        } else {
            cs2d_client_console_print(client, 170, 170, 170, "Current player name: '%s'", pl->name);
        }
    } else if (_stricmp(cmd, "menu") == 0 || _stricmp(cmd, "createserver") == 0 || _stricmp(cmd, "newgame") == 0) {
        client->create_server_open = true;
        client->options_open       = false;
        client->console_open       = false;
    } else if (_stricmp(cmd, "options") == 0 || _stricmp(cmd, "settings") == 0) {
        client->options_open       = true;
        client->create_server_open = false;
        client->console_open       = false;
    } else if (_stricmp(cmd, "quit") == 0 || _stricmp(cmd, "exit") == 0) {
        client->quit_requested = true;
    } else {
        cs2d_client_console_print(client, 255, 45, 45, "Unknown command '%s'. Type 'help' for a list of commands.", cmd);
    }
}

static bool client_any_ui_open(const cs2d_client_t *client)
{
    return client && (client->console_open || client->create_server_open || client->options_open);
}

void cs2d_client_handle_text_input(cs2d_client_t *client, const char *text)
{
    if (!client || !text) return;

    if (client->create_server_open || client->options_open) {
        cs2d_ui_feed_text(&client->ui, text);
    }

    if (!client->console_open) return;

    for (const char *p = text; *p != '\0'; p++) {
        unsigned char ch = (unsigned char)*p;
        /* Ignore grave/tilde key used to toggle the console */
        if (ch == '`' || ch == '~' || ch < 32) continue;

        int len = (int)strlen(client->console_input);
        if (len + 1 < CS2D_CONSOLE_LINE_LEN) {
            memmove(&client->console_input[client->console_cursor + 1],
                    &client->console_input[client->console_cursor],
                    (size_t)(len - client->console_cursor + 1));
            client->console_input[client->console_cursor] = (char)ch;
            client->console_cursor++;
        }
    }
}

void cs2d_client_handle_console_key(cs2d_client_t *client, int sdl_key)
{
    if (!client) return;

    if (sdl_key == SDLK_GRAVE || sdl_key == SDLK_F1) {
        client->console_open = !client->console_open;
        if (client->console_open) {
            client->ui.focused_id = CS2D_UI_ID("ConsoleInput");
            client->key_forward   = false;
            client->key_back      = false;
            client->key_left      = false;
            client->key_right     = false;
            client->mouse_fire    = false;
            client->mouse_attack2 = false;
        }
        return;
    }

    if (sdl_key == SDLK_F2) {
        client->create_server_open = !client->create_server_open;
        if (client->create_server_open) {
            client->options_open = false;
            client->console_open = false;
            client->ui.focused_id = CS2D_UI_ID("SrvNameInput");
        }
        return;
    }

    if (sdl_key == SDLK_F3) {
        client->options_open = !client->options_open;
        if (client->options_open) {
            client->create_server_open = false;
            client->console_open       = false;
            client->ui.focused_id      = CS2D_UI_ID("OptPlayerName");
        }
        return;
    }

    if (client->create_server_open || client->options_open) {
        if (sdl_key == SDLK_ESCAPE) {
            client->create_server_open = false;
            client->options_open       = false;
            return;
        }
        cs2d_ui_feed_key(&client->ui, sdl_key);
        return;
    }

    if (!client->console_open) return;

    int len = (int)strlen(client->console_input);

    switch (sdl_key) {
        case SDLK_ESCAPE:
            client->console_open = false;
            break;

        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (len > 0) {
                /* Save to history */
                if (client->console_history_count < CS2D_CONSOLE_HIST_MAX) {
                    strncpy(client->console_history[client->console_history_count++],
                            client->console_input, CS2D_CONSOLE_LINE_LEN - 1);
                } else {
                    memmove(&client->console_history[0],
                            &client->console_history[1],
                            sizeof(client->console_history[0]) * (CS2D_CONSOLE_HIST_MAX - 1));
                    strncpy(client->console_history[CS2D_CONSOLE_HIST_MAX - 1],
                            client->console_input, CS2D_CONSOLE_LINE_LEN - 1);
                }
                client->console_history_pos = client->console_history_count;

                char cmd_copy[CS2D_CONSOLE_LINE_LEN];
                strncpy(cmd_copy, client->console_input, sizeof(cmd_copy));
                client->console_input[0] = '\0';
                client->console_cursor   = 0;

                cs2d_client_console_exec(client, cmd_copy);
            }
            break;

        case SDLK_BACKSPACE:
            if (client->console_cursor > 0) {
                memmove(&client->console_input[client->console_cursor - 1],
                        &client->console_input[client->console_cursor],
                        (size_t)(len - client->console_cursor + 1));
                client->console_cursor--;
            }
            break;

        case SDLK_DELETE:
            if (client->console_cursor < len) {
                memmove(&client->console_input[client->console_cursor],
                        &client->console_input[client->console_cursor + 1],
                        (size_t)(len - client->console_cursor));
            }
            break;

        case SDLK_LEFT:
            if (client->console_cursor > 0) client->console_cursor--;
            break;

        case SDLK_RIGHT:
            if (client->console_cursor < len) client->console_cursor++;
            break;

        case SDLK_HOME:
            client->console_cursor = 0;
            break;

        case SDLK_END:
            client->console_cursor = len;
            break;

        case SDLK_UP:
            if (client->console_history_count > 0 && client->console_history_pos > 0) {
                client->console_history_pos--;
                strncpy(client->console_input,
                        client->console_history[client->console_history_pos],
                        CS2D_CONSOLE_LINE_LEN - 1);
                client->console_input[CS2D_CONSOLE_LINE_LEN - 1] = '\0';
                client->console_cursor = (int)strlen(client->console_input);
            }
            break;

        case SDLK_DOWN:
            if (client->console_history_pos + 1 < client->console_history_count) {
                client->console_history_pos++;
                strncpy(client->console_input,
                        client->console_history[client->console_history_pos],
                        CS2D_CONSOLE_LINE_LEN - 1);
                client->console_input[CS2D_CONSOLE_LINE_LEN - 1] = '\0';
                client->console_cursor = (int)strlen(client->console_input);
            } else {
                client->console_history_pos = client->console_history_count;
                client->console_input[0] = '\0';
                client->console_cursor   = 0;
            }
            break;

        case SDLK_PAGEUP:
            client->console_scroll += 5;
            if (client->console_scroll > client->console_line_count - 1) {
                client->console_scroll = (client->console_line_count > 0) ? (client->console_line_count - 1) : 0;
            }
            break;

        case SDLK_PAGEDOWN:
            client->console_scroll -= 5;
            if (client->console_scroll < 0) client->console_scroll = 0;
            break;

        default:
            break;
    }
}

void cs2d_client_handle_mouse_wheel(cs2d_client_t *client, int delta_y)
{
    if (!client) return;
    client->ui_wheel_delta += delta_y;
    if (!client->console_open) return;
    client->console_scroll += delta_y * 2;
    if (client->console_scroll < 0) client->console_scroll = 0;
    if (client->console_scroll > client->console_line_count - 1) {
        client->console_scroll = (client->console_line_count > 0) ? (client->console_line_count - 1) : 0;
    }
}

void cs2d_client_handle_key(cs2d_client_t *client, int key, bool pressed)
{
    if (!client) return;
    if (client_any_ui_open(client)) return;

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
            if (pressed) {
                cs2d_player_t *pl = &client->game.players[client->local_player_id];
                cs2d_weapon_reload(pl);
            }
            break;
        case 'B': case 'b':
            if (pressed) {
                /* Buy menu shortcut: purchase primary rifle / armor */
                cs2d_player_t *pl = &client->game.players[client->local_player_id];
                uint8_t buy_wep = (pl->team == CS2D_TEAM_T) ? CS2D_WEP_AK47 : CS2D_WEP_M4A1;
                if (!cs2d_player_buy_weapon(pl, buy_wep)) {
                    cs2d_player_buy_weapon(pl, CS2D_WEP_DEAGLE);
                }
                cs2d_player_buy_weapon(pl, CS2D_WEP_KEVLARHELM);
            }
            break;
        case 'G': case 'g':
            if (pressed) {
                cs2d_player_t *pl = &client->game.players[client->local_player_id];
                cs2d_player_drop_weapon(pl);
            }
            break;
        case '1':
            if (pressed) cs2d_player_switch_weapon(&client->game.players[client->local_player_id], CS2D_SLOT_PRIMARY);
            break;
        case '2':
            if (pressed) cs2d_player_switch_weapon(&client->game.players[client->local_player_id], CS2D_SLOT_SECONDARY);
            break;
        case '3':
            if (pressed) cs2d_player_switch_weapon(&client->game.players[client->local_player_id], CS2D_SLOT_MELEE);
            break;
        case '4':
            if (pressed) cs2d_player_switch_weapon(&client->game.players[client->local_player_id], CS2D_SLOT_GRENADE);
            break;
        case '5':
            if (pressed) cs2d_player_switch_weapon(&client->game.players[client->local_player_id], CS2D_SLOT_BOMB);
            break;
        default:
            break;
    }
}

void cs2d_client_handle_mouse(cs2d_client_t *client, float mx, float my, int button, bool pressed)
{
    if (!client) return;

    client->mouse_screen_x = mx;
    client->mouse_screen_y = my;

    if (client_any_ui_open(client)) {
        if (button == 1) {
            client->mouse_fire = pressed;
        }
        client->mouse_attack2 = false;
        return;
    }

    if (button == 1) { /* Left click: attack */
        client->mouse_fire = pressed;
    } else if (button == 2) { /* Right click: attack2 */
        client->mouse_attack2 = pressed;
    }
}

void cs2d_client_update(cs2d_client_t *client, float delta_time)
{
    if (!client) return;

    /* Update FPS & Network Traffic telemetry (1-second window) */
    client->fps_frames++;
    client->fps_timer += delta_time;
    if (client->fps_timer >= 0.5f) {
        client->fps_current          = (int)((float)client->fps_frames / client->fps_timer + 0.5f);
        client->net_bps_up           = (uint32_t)((float)client->net_bytes_up_accum / client->fps_timer);
        client->net_bps_down         = (uint32_t)((float)client->net_bytes_down_accum / client->fps_timer);
        client->fps_frames           = 0;
        client->fps_timer            = 0.0f;
        client->net_bytes_up_accum   = 0;
        client->net_bytes_down_accum = 0;
    }

    /* Update game simulation */
    cs2d_game_update(&client->game, delta_time);

    cs2d_player_t *pl = &client->game.players[client->local_player_id];
    if (pl->alive && !client_any_ui_open(client)) {
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

/* ============================================================================
 * Declarative CS2D UI Windows (Built with cs2d_ui Layout Engine)
 * ============================================================================ */

static void ui_form_row_begin(cs2d_ui_context_t *ui, const char *label, float label_w)
{
    cs2d_ui_open_element(ui, 0, &(cs2d_ui_element_decl_t){
        .layout = {
            .sizing    = { CS2D_UI_GROW(0, 0), CS2D_UI_FIXED(20.0f) },
            .child_gap = 6.0f,
            .dir       = CS2D_UI_LEFT_TO_RIGHT,
            .align_y   = CS2D_UI_ALIGN_Y_CENTER
        }
    });
    CS2D_UI_ELEMENT(ui, 0, {
        .layout = {
            .sizing  = { CS2D_UI_FIXED(label_w), CS2D_UI_FIXED(18.0f) },
            .align_y = CS2D_UI_ALIGN_Y_CENTER
        }
    }) {
        cs2d_ui_text(ui, label, CS2D_FONT_REGULAR, CS2D_UI_RGB(185, 185, 185));
    }
}

static void ui_form_row_end(cs2d_ui_context_t *ui)
{
    cs2d_ui_close_element(ui);
}

static void cs2d_client_build_console_ui(cs2d_client_t *client, int screen_w, int screen_h)
{
    if (!client->console_open) return;

    cs2d_ui_context_t *ui = &client->ui;
    float win_x = 4.0f;
    float win_y = 24.0f;
    float win_w = (float)screen_w - 8.0f;
    float win_h = (float)screen_h - 28.0f;

    if (cs2d_ui_window_begin(ui, CS2D_UI_ID("ConsoleWin"), "Console", win_x, win_y, win_w, win_h, &client->console_open)) {
        /* 1. Horizontal container: Log Area (GROW) + Right Vertical Scrollbar (9px) */
        CS2D_UI_ELEMENT(ui, CS2D_UI_ID("ConsoleBodyRow"), {
            .layout = {
                .sizing    = { CS2D_UI_GROW(0, 0), CS2D_UI_GROW(0, 0) },
                .child_gap = 1.0f,
                .dir       = CS2D_UI_LEFT_TO_RIGHT
            }
        }) {
            float log_h = win_h - 49.0f;
            const float line_h = 15.0f;
            int max_vis = (int)((log_h - 8.0f) / line_h);
            if (max_vis < 1) max_vis = 1;

            int end_idx = client->console_line_count - client->console_scroll;
            if (end_idx > client->console_line_count) end_idx = client->console_line_count;
            if (end_idx < 0) end_idx = 0;
            int start_idx = end_idx - max_vis;
            if (start_idx < 0) start_idx = 0;

            /* Left: Near-black clipped console output box */
            CS2D_UI_ELEMENT(ui, CS2D_UI_ID("ConsoleLogBox"), {
                .layout = {
                    .sizing    = { CS2D_UI_GROW(0, 0), CS2D_UI_GROW(0, 0) },
                    .padding   = CS2D_UI_PAD(5, 5, 4, 4),
                    .child_gap = 0.0f,
                    .dir       = CS2D_UI_TOP_TO_BOTTOM
                },
                .bg_color = CS2D_UI_RGBA(22, 22, 22, 246),
                .border   = { .width = 1.0f, .color = CS2D_UI_RGBA(92, 92, 92, 245) },
                .clip     = { .enabled = true }
            }) {
                for (int i = start_idx; i < end_idx; i++) {
                    const cs2d_console_line_t *ln = &client->console_lines[i];
                    cs2d_ui_text(ui, ln->text, CS2D_FONT_REGULAR, CS2D_UI_RGB(ln->r, ln->g, ln->b));
                }
            }

            /* Right: Vertical scrollbar track + proportional thumb */
            float sb_inner_h = log_h - 4.0f;
            if (sb_inner_h < 20.0f) sb_inner_h = 20.0f;
            float thumb_h = sb_inner_h;
            float top_pad = 0.0f;
            if (client->console_line_count > max_vis) {
                thumb_h = sb_inner_h * ((float)max_vis / (float)client->console_line_count);
                if (thumb_h < 20.0f) thumb_h = 20.0f;
                int max_scroll = client->console_line_count - max_vis;
                int cur_scroll = client->console_scroll;
                if (cur_scroll > max_scroll) cur_scroll = max_scroll;
                float frac = 1.0f - ((float)cur_scroll / (float)max_scroll);
                top_pad = frac * (sb_inner_h - thumb_h);
            }

            CS2D_UI_ELEMENT(ui, CS2D_UI_ID("ConsoleScrollbar"), {
                .layout = {
                    .sizing  = { CS2D_UI_FIXED(9.0f), CS2D_UI_GROW(0, 0) },
                    .padding = CS2D_UI_PAD(2.0f, 2.0f, 2.0f + top_pad, 2.0f),
                    .dir     = CS2D_UI_TOP_TO_BOTTOM
                },
                .bg_color = CS2D_UI_RGBA(18, 18, 18, 246),
                .border   = { .width = 1.0f, .color = CS2D_UI_RGBA(92, 92, 92, 245) }
            }) {
                CS2D_UI_ELEMENT(ui, CS2D_UI_ID("ConsoleScrollThumb"), {
                    .layout   = { .sizing = { CS2D_UI_GROW(0, 0), CS2D_UI_FIXED(thumb_h) } },
                    .bg_color = CS2D_UI_RGBA(74, 74, 74, 245)
                }) {}
            }
        }

        /* 2. Bottom command input textbox */
        if (ui->focused_id == 0) {
            ui->focused_id = CS2D_UI_ID("ConsoleInput");
        }
        cs2d_ui_textbox(ui, CS2D_UI_ID("ConsoleInput"),
                        client->console_input, sizeof(client->console_input),
                        &client->console_cursor, 0.0f, false);

        cs2d_ui_window_end(ui);
    }
}

static void cs2d_client_build_create_server_ui(cs2d_client_t *client, int screen_w, int screen_h)
{
    if (!client->create_server_open) return;

    cs2d_ui_context_t *ui = &client->ui;
    float win_w = 460.0f;
    float win_h = 420.0f;
    float win_x = floorf(((float)screen_w - win_w) * 0.5f);
    float win_y = floorf(((float)screen_h - win_h) * 0.5f);

    static const char *srv_tabs[] = { "Server", "Map", "Settings", "Bots", "Mods", "More Settings" };
    static const char *max_pl_opts[] = { "8", "12", "16", "24", "32" };
    static const char *gm_opts[]     = { "Standard", "Deathmatch", "Team Deathmatch", "Construction", "Zombies!" };
    static const char *fow_opts[]    = { "Off", "On", "Line of Sight" };
    static const char *map_names[]   = {
        "de_aztec", "de_cbble", "de_cs2d", "de_dust", "de_dust2",
        "de_inferno", "de_nuke", "cs_assault", "cs_italia", "cs_office"
    };

    if (cs2d_ui_window_begin(ui, CS2D_UI_ID("CreateServerWin"), "Create Server",
                             win_x, win_y, win_w, win_h, &client->create_server_open)) {
        /* Inner Tabbed Content Box */
        CS2D_UI_ELEMENT(ui, CS2D_UI_ID("CreateSrvInner"), {
            .layout = {
                .sizing    = { CS2D_UI_GROW(0, 0), CS2D_UI_GROW(0, 0) },
                .child_gap = 0.0f,
                .dir       = CS2D_UI_TOP_TO_BOTTOM
            },
            .bg_color = CS2D_UI_RGBA(48, 48, 48, 235),
            .border   = { .width = 1.0f, .color = CS2D_UI_RGBA(95, 95, 95, 245) }
        }) {
            cs2d_ui_tab_bar(ui, CS2D_UI_ID("CreateSrvTabs"), srv_tabs, 6, &client->create_server_tab);

            CS2D_UI_ELEMENT(ui, CS2D_UI_ID("CreateSrvBody"), {
                .layout = {
                    .sizing    = { CS2D_UI_GROW(0, 0), CS2D_UI_GROW(0, 0) },
                    .padding   = CS2D_UI_PAD_ALL(10.0f),
                    .child_gap = 6.0f,
                    .dir       = CS2D_UI_TOP_TO_BOTTOM
                }
            }) {
                if (client->create_server_tab == 0) {
                    /* Tab 0: Server Settings (matches Screenshot 2) */
                    ui_form_row_begin(ui, "Servername", 105.0f);
                    cs2d_ui_textbox(ui, CS2D_UI_ID("SrvNameInput"), client->srv_name, sizeof(client->srv_name),
                                    &client->srv_name_cursor, 210.0f, false);
                    ui_form_row_end(ui);

                    ui_form_row_begin(ui, "Password", 105.0f);
                    cs2d_ui_textbox(ui, CS2D_UI_ID("SrvPassInput"), client->srv_password, sizeof(client->srv_password),
                                    &client->srv_password_cursor, 210.0f, true);
                    ui_form_row_end(ui);

                    ui_form_row_begin(ui, "Port", 105.0f);
                    cs2d_ui_textbox(ui, CS2D_UI_ID("SrvPortInput"), client->srv_port, sizeof(client->srv_port),
                                    &client->srv_port_cursor, 68.0f, false);
                    ui_form_row_end(ui);

                    ui_form_row_begin(ui, "Max. Players", 105.0f);
                    cs2d_ui_select(ui, CS2D_UI_ID("SrvMaxPlSel"), max_pl_opts, 5, &client->srv_max_players_idx, 68.0f);
                    ui_form_row_end(ui);

                    cs2d_ui_checkbox(ui, CS2D_UI_ID("SrvRegUsgn"), "Register in Serverlist (U.S.G.N., Internet)", &client->srv_reg_usgn);
                    cs2d_ui_checkbox(ui, CS2D_UI_ID("SrvReqLogin"), "Require login (U.S.G.N. or Steam)", &client->srv_req_login);
                    cs2d_ui_checkbox(ui, CS2D_UI_ID("SrvLanMode"), "Offline / LAN Mode (never connect to U.S.G.N. or Steam)", &client->srv_lan_mode);

                    ui_form_row_begin(ui, "Game Mode", 105.0f);
                    cs2d_ui_select(ui, CS2D_UI_ID("SrvGameModeSel"), gm_opts, 5, &client->srv_gamemode_idx, 130.0f);
                    ui_form_row_end(ui);

                    cs2d_ui_checkbox(ui, CS2D_UI_ID("SrvFF"), "Friendly Fire", &client->srv_friendly_fire);
                    cs2d_ui_checkbox(ui, CS2D_UI_ID("SrvBalance"), "Auto Team Balance", &client->srv_auto_balance);

                    ui_form_row_begin(ui, "Fog Of War", 105.0f);
                    cs2d_ui_select(ui, CS2D_UI_ID("SrvFowSel"), fow_opts, 3, &client->srv_fow_idx, 130.0f);
                    ui_form_row_end(ui);
                } else {
                    /* Tab 1+: Map Selector & Map Cycle (matches Screenshot 3) */
                    CS2D_UI_ELEMENT(ui, CS2D_UI_ID("MapSplitRow"), {
                        .layout = {
                            .sizing    = { CS2D_UI_GROW(0, 0), CS2D_UI_GROW(0, 0) },
                            .child_gap = 8.0f,
                            .dir       = CS2D_UI_LEFT_TO_RIGHT
                        }
                    }) {
                        /* Left Column: Available Maps with Gradient Section Headers */
                        CS2D_UI_ELEMENT(ui, CS2D_UI_ID("MapColLeft"), {
                            .layout = {
                                .sizing    = { CS2D_UI_GROW(0, 0), CS2D_UI_GROW(0, 0) },
                                .child_gap = 4.0f,
                                .dir       = CS2D_UI_TOP_TO_BOTTOM
                            }
                        }) {
                            cs2d_ui_text(ui, "Map:", CS2D_FONT_REGULAR, CS2D_UI_RGB(185, 185, 185));

                            CS2D_UI_ELEMENT(ui, CS2D_UI_ID("MapListBox"), {
                                .layout = {
                                    .sizing    = { CS2D_UI_GROW(0, 0), CS2D_UI_GROW(0, 0) },
                                    .padding   = CS2D_UI_PAD_ALL(2.0f),
                                    .child_gap = 1.0f,
                                    .dir       = CS2D_UI_TOP_TO_BOTTOM
                                },
                                .bg_color = CS2D_UI_RGBA(22, 22, 22, 248),
                                .border   = { .width = 1.0f, .color = CS2D_UI_RGB(100, 100, 100) },
                                .clip     = { .enabled = true }
                            }) {
                                cs2d_ui_section_header(ui, CS2D_UI_ID("HdrDE"), "DE - Bomb Defuse");
                                for (int m = 0; m < 7; m++) {
                                    cs2d_ui_id_t mid = CS2D_UI_IDI("MapItem", m);
                                    bool sel = (client->srv_selected_map == m);
                                    bool hov = cs2d_ui_hovered(ui, mid);
                                    CS2D_UI_ELEMENT(ui, mid, {
                                        .layout = {
                                            .sizing  = { CS2D_UI_GROW(0, 0), CS2D_UI_FIXED(16.0f) },
                                            .padding = CS2D_UI_PAD(6, 4, 0, 0),
                                            .align_y = CS2D_UI_ALIGN_Y_CENTER
                                        },
                                        .bg_color = sel ? CS2D_UI_RGBA(70, 70, 70, 240)
                                                        : (hov ? CS2D_UI_RGBA(42, 42, 42, 220) : CS2D_UI_RGBA(0, 0, 0, 0))
                                    }) {
                                        cs2d_ui_text(ui, map_names[m], CS2D_FONT_REGULAR,
                                                     sel ? CS2D_UI_RGB(255, 220, 0) : CS2D_UI_RGB(190, 190, 190));
                                    }
                                    if (cs2d_ui_clicked(ui, mid)) {
                                        client->srv_selected_map = m;
                                    }
                                }

                                cs2d_ui_section_header(ui, CS2D_UI_ID("HdrCS"), "CS - Hostage Rescue");
                                for (int m = 7; m < 10; m++) {
                                    cs2d_ui_id_t mid = CS2D_UI_IDI("MapItem", m);
                                    bool sel = (client->srv_selected_map == m);
                                    bool hov = cs2d_ui_hovered(ui, mid);
                                    CS2D_UI_ELEMENT(ui, mid, {
                                        .layout = {
                                            .sizing  = { CS2D_UI_GROW(0, 0), CS2D_UI_FIXED(16.0f) },
                                            .padding = CS2D_UI_PAD(6, 4, 0, 0),
                                            .align_y = CS2D_UI_ALIGN_Y_CENTER
                                        },
                                        .bg_color = sel ? CS2D_UI_RGBA(70, 70, 70, 240)
                                                        : (hov ? CS2D_UI_RGBA(42, 42, 42, 220) : CS2D_UI_RGBA(0, 0, 0, 0))
                                    }) {
                                        cs2d_ui_text(ui, map_names[m], CS2D_FONT_REGULAR,
                                                     sel ? CS2D_UI_RGB(255, 220, 0) : CS2D_UI_RGB(190, 190, 190));
                                    }
                                    if (cs2d_ui_clicked(ui, mid)) {
                                        client->srv_selected_map = m;
                                    }
                                }
                            }
                        }

                        /* Right Column: Map Cycle */
                        CS2D_UI_ELEMENT(ui, CS2D_UI_ID("MapColRight"), {
                            .layout = {
                                .sizing    = { CS2D_UI_GROW(0, 0), CS2D_UI_GROW(0, 0) },
                                .child_gap = 4.0f,
                                .dir       = CS2D_UI_TOP_TO_BOTTOM
                            }
                        }) {
                            cs2d_ui_text(ui, "Map Cycle:", CS2D_FONT_REGULAR, CS2D_UI_RGB(185, 185, 185));

                            CS2D_UI_ELEMENT(ui, CS2D_UI_ID("MapCycleBox"), {
                                .layout = {
                                    .sizing  = { CS2D_UI_GROW(0, 0), CS2D_UI_GROW(0, 0) },
                                    .padding = CS2D_UI_PAD_ALL(5.0f),
                                    .dir     = CS2D_UI_TOP_TO_BOTTOM
                                },
                                .bg_color = CS2D_UI_RGBA(22, 22, 22, 248),
                                .border   = { .width = 1.0f, .color = CS2D_UI_RGB(100, 100, 100) }
                            }) {
                                cs2d_ui_text(ui, map_names[client->srv_selected_map], CS2D_FONT_REGULAR, CS2D_UI_RGB(190, 190, 190));
                            }
                        }
                    }
                }
            }
        }

        /* Bottom Action Bar: [Help] ... [Start] [Cancel] */
        CS2D_UI_ELEMENT(ui, CS2D_UI_ID("CreateSrvFooter"), {
            .layout = {
                .sizing    = { CS2D_UI_GROW(0, 0), CS2D_UI_FIXED(20.0f) },
                .child_gap = 4.0f,
                .dir       = CS2D_UI_LEFT_TO_RIGHT,
                .align_y   = CS2D_UI_ALIGN_Y_CENTER
            }
        }) {
            cs2d_ui_button(ui, CS2D_UI_ID("BtnSrvHelp"), "Help", 58.0f, 19.0f);

            CS2D_UI_ELEMENT(ui, 0, {
                .layout = { .sizing = { CS2D_UI_GROW(0, 0), CS2D_UI_FIXED(1.0f) } }
            }) {}

            if (cs2d_ui_button(ui, CS2D_UI_ID("BtnSrvStart"), "Start", 76.0f, 19.0f)) {
                char cmd[128];
                snprintf(cmd, sizeof(cmd), "map %s", map_names[client->srv_selected_map]);
                cs2d_client_console_exec(client, cmd);
                client->create_server_open = false;
            }
            if (cs2d_ui_button(ui, CS2D_UI_ID("BtnSrvCancel"), "Cancel", 76.0f, 19.0f)) {
                client->create_server_open = false;
            }
        }

        cs2d_ui_window_end(ui);
    }
}

static void cs2d_client_build_options_ui(cs2d_client_t *client, int screen_w, int screen_h)
{
    if (!client->options_open) return;

    cs2d_ui_context_t *ui = &client->ui;
    float win_w = 460.0f;
    float win_h = 420.0f;
    float win_x = floorf(((float)screen_w - win_w) * 0.5f);
    float win_y = floorf(((float)screen_h - win_h) * 0.5f);

    static const char *opt_tabs[]   = { "Player", "Controls", "Game", "Graphics", "Sound", "Net", "More" };
    static const char *spray_opts[] = { "Unreal Software", "Counter-Strike", "Lambda", "Skull" };
    static const char *lang_opts[]  = { "English", "Portuguese", "German" };

    if (cs2d_ui_window_begin(ui, CS2D_UI_ID("OptionsWin"), "Options",
                             win_x, win_y, win_w, win_h, &client->options_open)) {
        CS2D_UI_ELEMENT(ui, CS2D_UI_ID("OptionsInner"), {
            .layout = {
                .sizing    = { CS2D_UI_GROW(0, 0), CS2D_UI_GROW(0, 0) },
                .child_gap = 0.0f,
                .dir       = CS2D_UI_TOP_TO_BOTTOM
            },
            .bg_color = CS2D_UI_RGBA(48, 48, 48, 235),
            .border   = { .width = 1.0f, .color = CS2D_UI_RGBA(95, 95, 95, 245) }
        }) {
            cs2d_ui_tab_bar(ui, CS2D_UI_ID("OptionsTabs"), opt_tabs, 7, &client->options_tab);

            CS2D_UI_ELEMENT(ui, CS2D_UI_ID("OptionsBody"), {
                .layout = {
                    .sizing    = { CS2D_UI_GROW(0, 0), CS2D_UI_GROW(0, 0) },
                    .padding   = CS2D_UI_PAD_ALL(10.0f),
                    .child_gap = 6.0f,
                    .dir       = CS2D_UI_TOP_TO_BOTTOM
                }
            }) {
                if (client->options_tab == 0) {
                    /* Tab 0: Player (matches Screenshot 4) */
                    ui_form_row_begin(ui, "Name", 95.0f);
                    cs2d_ui_textbox(ui, CS2D_UI_ID("OptPlayerName"), client->config.name, sizeof(client->config.name),
                                    &client->opt_name_cursor, 210.0f, false);
                    ui_form_row_end(ui);

                    ui_form_row_begin(ui, "Spraylogo", 95.0f);
                    cs2d_ui_select(ui, CS2D_UI_ID("OptSpraySel"), spray_opts, 4, &client->opt_spray_idx, 140.0f);
                    ui_form_row_end(ui);
                } else if (client->options_tab == 1) {
                    /* Tab 1: Controls (matches Screenshot 5) */
                    CS2D_UI_ELEMENT(ui, CS2D_UI_ID("CtrlSplitRow"), {
                        .layout = {
                            .sizing    = { CS2D_UI_GROW(0, 0), CS2D_UI_GROW(0, 0) },
                            .child_gap = 10.0f,
                            .dir       = CS2D_UI_LEFT_TO_RIGHT
                        }
                    }) {
                        /* Left keybind list */
                        CS2D_UI_ELEMENT(ui, CS2D_UI_ID("CtrlKeyList"), {
                            .layout = {
                                .sizing    = { CS2D_UI_FIXED(215.0f), CS2D_UI_GROW(0, 0) },
                                .padding   = CS2D_UI_PAD_ALL(2.0f),
                                .child_gap = 2.0f,
                                .dir       = CS2D_UI_TOP_TO_BOTTOM
                            },
                            .bg_color = CS2D_UI_RGBA(22, 22, 22, 248),
                            .border   = { .width = 1.0f, .color = CS2D_UI_RGB(100, 100, 100) },
                            .clip     = { .enabled = true }
                        }) {
                            cs2d_ui_section_header(ui, CS2D_UI_ID("HdrComm"), "Communication");
                            cs2d_ui_text(ui, "Say                         y", CS2D_FONT_REGULAR, CS2D_UI_RGB(190, 190, 190));
                            cs2d_ui_text(ui, "Team Say                    u", CS2D_FONT_REGULAR, CS2D_UI_RGB(190, 190, 190));
                            cs2d_ui_text(ui, "Radio 1                     z", CS2D_FONT_REGULAR, CS2D_UI_RGB(190, 190, 190));
                            cs2d_ui_text(ui, "Radio 2                     x", CS2D_FONT_REGULAR, CS2D_UI_RGB(190, 190, 190));
                            cs2d_ui_text(ui, "Radio 3                     c", CS2D_FONT_REGULAR, CS2D_UI_RGB(190, 190, 190));

                            cs2d_ui_section_header(ui, CS2D_UI_ID("HdrMisc"), "Misc");
                            cs2d_ui_text(ui, "Buy Menu                    b", CS2D_FONT_REGULAR, CS2D_UI_RGB(190, 190, 190));
                            cs2d_ui_text(ui, "Team Menu                   m", CS2D_FONT_REGULAR, CS2D_UI_RGB(190, 190, 190));
                            cs2d_ui_text(ui, "Drop Weapon                 g", CS2D_FONT_REGULAR, CS2D_UI_RGB(190, 190, 190));
                            cs2d_ui_text(ui, "Scores                      Tab", CS2D_FONT_REGULAR, CS2D_UI_RGB(190, 190, 190));
                            cs2d_ui_text(ui, "Flashlight                  f", CS2D_FONT_REGULAR, CS2D_UI_RGB(190, 190, 190));
                            cs2d_ui_text(ui, "Screenshot                  F5", CS2D_FONT_REGULAR, CS2D_UI_RGB(190, 190, 190));
                        }

                        /* Right Controls settings */
                        CS2D_UI_ELEMENT(ui, CS2D_UI_ID("CtrlRightCol"), {
                            .layout = {
                                .sizing    = { CS2D_UI_GROW(0, 0), CS2D_UI_GROW(0, 0) },
                                .child_gap = 6.0f,
                                .dir       = CS2D_UI_TOP_TO_BOTTOM
                            }
                        }) {
                            cs2d_ui_text(ui, "Movement Directions", CS2D_FONT_REGULAR, CS2D_UI_RGB(185, 185, 185));
                            cs2d_ui_radio(ui, CS2D_UI_ID("RadAbs"), "Absolute", 0, &client->opt_move_dir_mode);
                            cs2d_ui_radio(ui, CS2D_UI_ID("RadRel"), "Relative To Dir.", 1, &client->opt_move_dir_mode);
                        }
                    }
                } else {
                    /* Tab 2+: Game Settings */
                    cs2d_ui_checkbox(ui, CS2D_UI_ID("OptGore"), "Gore (Blood Effects)", &client->opt_gore);
                    cs2d_ui_checkbox(ui, CS2D_UI_ID("OptAutoSw"), "Automatic Weapon Switch", &client->opt_auto_switch);
                    cs2d_ui_checkbox(ui, CS2D_UI_ID("OptHudMsg"), "HUD Messages", &client->opt_hud_msgs);
                    cs2d_ui_checkbox(ui, CS2D_UI_ID("OptActTxt"), "Action Texts", &client->opt_action_texts);
                    cs2d_ui_checkbox(ui, CS2D_UI_ID("OptSimpRad"), "Simple Radar", &client->opt_simple_radar);
                    cs2d_ui_checkbox(ui, CS2D_UI_ID("OptDmgInd"), "Damage Indicator", &client->opt_dmg_indicator);
                    cs2d_ui_checkbox(ui, CS2D_UI_ID("OptChatHist"), "Chat History", &client->opt_chat_history);

                    ui_form_row_begin(ui, "Language", 95.0f);
                    cs2d_ui_select(ui, CS2D_UI_ID("OptLangSel"), lang_opts, 3, &client->opt_lang_idx, 130.0f);
                    ui_form_row_end(ui);
                }
            }
        }

        /* Bottom Action Bar: [Help] ... [Okay] [Cancel] */
        CS2D_UI_ELEMENT(ui, CS2D_UI_ID("OptionsFooter"), {
            .layout = {
                .sizing    = { CS2D_UI_GROW(0, 0), CS2D_UI_FIXED(20.0f) },
                .child_gap = 4.0f,
                .dir       = CS2D_UI_LEFT_TO_RIGHT,
                .align_y   = CS2D_UI_ALIGN_Y_CENTER
            }
        }) {
            cs2d_ui_button(ui, CS2D_UI_ID("BtnOptHelp"), "Help", 58.0f, 19.0f);

            CS2D_UI_ELEMENT(ui, 0, {
                .layout = { .sizing = { CS2D_UI_GROW(0, 0), CS2D_UI_FIXED(1.0f) } }
            }) {}

            if (cs2d_ui_button(ui, CS2D_UI_ID("BtnOptOkay"), "Okay", 76.0f, 19.0f)) {
                strncpy(client->game.players[client->local_player_id].name,
                        client->config.name,
                        sizeof(client->game.players[client->local_player_id].name) - 1);
                client->options_open = false;
            }
            if (cs2d_ui_button(ui, CS2D_UI_ID("BtnOptCancel"), "Cancel", 76.0f, 19.0f)) {
                client->options_open = false;
            }
        }

        cs2d_ui_window_end(ui);
    }
}

static void cs2d_client_render_ui(cs2d_client_t *client, int screen_w, int screen_h)
{
    if (!client_any_ui_open(client)) {
        client->ui_wheel_delta = 0;
        return;
    }

    cs2d_ui_begin_layout(
        &client->ui,
        screen_w, screen_h,
        client->mouse_screen_x, client->mouse_screen_y,
        client->mouse_fire,
        client->ui_wheel_delta
    );
    client->ui_wheel_delta = 0;

    cs2d_client_build_console_ui(client, screen_w, screen_h);
    cs2d_client_build_create_server_ui(client, screen_w, screen_h);
    cs2d_client_build_options_ui(client, screen_w, screen_h);

    cs2d_ui_end_layout(&client->ui);
    cs2d_ui_render(&client->ui);
}

/*
 * Renders the top-left CS2D telemetry overlay:
 *   <N> FPS
 *   GL
 *   Network Traffic (up: <up>b/s | down: <down>b/s)
 */
static void cs2d_client_render_fps_overlay(const cs2d_client_t *client)
{
    if (!client || !client->show_fps) return;

    char line1[64];
    char line3[128];
    snprintf(line1, sizeof(line1), "%d FPS", client->fps_current > 0 ? client->fps_current : 60);
    snprintf(line3, sizeof(line3), "Network Traffic (up: %ub/s | down: %ub/s)",
             client->net_bps_up, client->net_bps_down);

    /* 1px dark drop shadow for readability over both map radar and console frame */
    cs2d_render_text_small(3.0f, 1.0f,  line1, 0.0f, 0.0f, 0.0f, 0.75f);
    cs2d_render_text_small(3.0f, 9.0f,  "GL",  0.0f, 0.0f, 0.0f, 0.75f);
    cs2d_render_text_small(3.0f, 17.0f, line3, 0.0f, 0.0f, 0.0f, 0.75f);

    cs2d_render_text_small(2.0f, 0.0f,  line1, 0.80f, 0.80f, 0.80f, 0.92f);
    cs2d_render_text_small(2.0f, 8.0f,  "GL",  0.80f, 0.80f, 0.80f, 0.92f);
    cs2d_render_text_small(2.0f, 16.0f, line3, 0.80f, 0.80f, 0.80f, 0.92f);
}

void cs2d_client_render(cs2d_client_t *client, int screen_w, int screen_h)
{
    if (!client) return;

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

    /* 5. Declarative CS2D UI Windows (Console, Create Server, Options) */
    cs2d_client_render_ui(client, screen_w, screen_h);

    /* 6. Top-Left FPS / Renderer / Network Traffic Display */
    cs2d_client_render_fps_overlay(client);

    /* 7. Crosshair (gfx/pointer.bmp frame 0) drawn on top of everything */
    cs2d_render_crosshair(client->mouse_screen_x, client->mouse_screen_y);
}



