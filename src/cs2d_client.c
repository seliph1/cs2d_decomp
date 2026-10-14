#include "cs2d_client.h"
#include "cs2d_weapons.h"
#include "cs2d_player.h"
#include "cs2d_map.h"
#include "cs2d_game.h"
#include "cs2d_network.h"
#include "cs2d_light.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <time.h>

/* Lines that fit in the log box (1 until the console has been laid out) */
static int console_visible(const cs2d_client_t *client)
{
    return (client->console_visible_lines > 0) ? client->console_visible_lines : 1;
}

void cs2d_client_console_print(cs2d_client_t *client, uint8_t r, uint8_t g, uint8_t b, const char *fmt, ...)
{
    if (!client || !fmt) return;

    char buf[CS2D_CONSOLE_LINE_LEN];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    /* Echo to stdout while tracing the network protocol (CS2D_NET_LOG) */
    static int echo = -1;
    if (echo < 0) echo = (getenv("CS2D_NET_LOG") != NULL);
    if (echo) {
        printf("[console] %s\n", buf);
        fflush(stdout);
    }

    int  visible = console_visible(client);
    bool at_end  = client->console_scroll.top >= cs2d_ui_scroll_max(visible, client->console_line_count);

    /* Ring buffer: when full, the new line overwrites the oldest one */
    int  slot;
    bool dropped = false;
    if (client->console_line_count < CS2D_CONSOLE_MAX_LINES) {
        slot = (client->console_line_head + client->console_line_count++) % CS2D_CONSOLE_MAX_LINES;
    } else {
        slot = client->console_line_head;
        client->console_line_head = (client->console_line_head + 1) % CS2D_CONSOLE_MAX_LINES;
        dropped = true;
    }

    cs2d_console_line_t *line = &client->console_lines[slot];
    memcpy(line->text, buf, sizeof(line->text));
    line->r = r;
    line->g = g;
    line->b = b;

    /* At the bottom the view follows new output; scrolled up, it stays on the same lines */
    if (at_end) {
        client->console_scroll.top = cs2d_ui_scroll_max(visible, client->console_line_count);
    } else if (dropped && client->console_scroll.top > 0) {
        client->console_scroll.top--;
    }
}

const cs2d_console_line_t *cs2d_client_console_line(const cs2d_client_t *client, int i)
{
    if (!client || i < 0 || i >= client->console_line_count) return NULL;
    return &client->console_lines[(client->console_line_head + i) % CS2D_CONSOLE_MAX_LINES];
}

/* Up / Down by a line, PageUp / PageDown by a page (page = true; rows is then the direction) */
static void server_info_scroll_by(cs2d_client_t *client, int rows, bool page)
{
    int visible = (client->server_info_visible_lines > 0) ? client->server_info_visible_lines : 1;
    int total   = client->server_info_line_count;
    if (page) {
        cs2d_ui_scroll_page(&client->server_info_scroll, rows, visible, total);
    } else {
        cs2d_ui_scroll_by(&client->server_info_scroll, rows, visible, total);
    }
}

/*
 * Decodes a UTF-16LE string from opcode 240 (CS2D_NET_MSG2) or sys/serverinfo.txt:
 *   - Leading U+00A9 ('©') + 9 ASCII digits RRRGGGBBB sets line RGB and strips the 10-char prefix.
 *   - Leading U+00BB ('»') highlights link lines in CS2D gold (255, 220, 0).
 *   - Remaining code units are encoded as UTF-8 for GDI+ Liberation Sans rendering.
 */
static void utf16_to_colored_utf8(
    const uint8_t *utf16_le,
    int char_len,
    uint8_t *out_r,
    uint8_t *out_g,
    uint8_t *out_b,
    char *out_utf8,
    size_t max_utf8
)
{
    if (out_r) *out_r = 185;
    if (out_g) *out_g = 185;
    if (out_b) *out_b = 185;
    if (!out_utf8 || max_utf8 == 0) return;
    out_utf8[0] = '\0';
    if (!utf16_le || char_len <= 0) return;

    int start = 0;
    uint16_t c0 = (uint16_t)utf16_le[0] | ((uint16_t)utf16_le[1] << 8);
    if (c0 == 0x00A9 && char_len >= 10) {
        bool digits_ok = true;
        int d[9];
        for (int i = 0; i < 9; i++) {
            uint16_t ch = (uint16_t)utf16_le[(i + 1) * 2] | ((uint16_t)utf16_le[(i + 1) * 2 + 1] << 8);
            if (ch < '0' || ch > '9') {
                digits_ok = false;
                break;
            }
            d[i] = (int)(ch - '0');
        }
        if (digits_ok) {
            int r = d[0] * 100 + d[1] * 10 + d[2];
            int g = d[3] * 100 + d[4] * 10 + d[5];
            int b = d[6] * 100 + d[7] * 10 + d[8];
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;
            if (out_r) *out_r = (uint8_t)r;
            if (out_g) *out_g = (uint8_t)g;
            if (out_b) *out_b = (uint8_t)b;
            start = 10;
        }
    } else if (c0 == 0x00BB) {
        if (out_r) *out_r = 255;
        if (out_g) *out_g = 220;
        if (out_b) *out_b = 0;
    }

    size_t out_pos = 0;
    for (int i = start; i < char_len; i++) {
        uint16_t wc = (uint16_t)utf16_le[i * 2] | ((uint16_t)utf16_le[i * 2 + 1] << 8);
        if (wc == '\r' || wc == '\n' || wc == 0) continue;
        if (wc < 0x80) {
            if (out_pos + 1 >= max_utf8) break;
            out_utf8[out_pos++] = (char)wc;
        } else if (wc < 0x800) {
            if (out_pos + 2 >= max_utf8) break;
            out_utf8[out_pos++] = (char)(0xC0 | (wc >> 6));
            out_utf8[out_pos++] = (char)(0x80 | (wc & 0x3F));
        } else {
            if (out_pos + 3 >= max_utf8) break;
            out_utf8[out_pos++] = (char)(0xE0 | (wc >> 12));
            out_utf8[out_pos++] = (char)(0x80 | ((wc >> 6) & 0x3F));
            out_utf8[out_pos++] = (char)(0x80 | (wc & 0x3F));
        }
    }
    out_utf8[out_pos] = '\0';
}

void cs2d_client_serverinfo_add_utf16(cs2d_client_t *client, const uint8_t *utf16_le, int char_len)
{
    if (!client || client->server_info_line_count >= CS2D_SERVER_INFO_MAX_LINES) return;
    cs2d_console_line_t *ln = &client->server_info_lines[client->server_info_line_count++];
    utf16_to_colored_utf8(utf16_le, char_len, &ln->r, &ln->g, &ln->b, ln->text, sizeof(ln->text));
}

/* Text file lines (UTF-8 or Latin-1) with the '©RRRGGGBBB' / '»' prefixes of utf16_to_colored_utf8 */
static int load_text_lines(const char *filepath, cs2d_console_line_t *lines, int max)
{
    FILE *f = fopen(filepath, "rb");
    if (!f) return -1;
    int count = 0;
    char raw_line[256];
    while (fgets(raw_line, sizeof(raw_line), f) && count < max) {
        uint8_t wbuf[512];
        int wlen = 0;
        for (size_t i = 0; raw_line[i] != '\0' && wlen < 250; i++) {
            uint8_t b0 = (uint8_t)raw_line[i];
            if (b0 == '\r' || b0 == '\n') continue;
            uint16_t wc = b0;
            /* Support both UTF-8 (0xC2/0xC3 prefix) and raw Latin-1 / CP1252 */
            if ((b0 == 0xC2 || b0 == 0xC3) && ((uint8_t)raw_line[i + 1] & 0xC0) == 0x80) {
                wc = (uint16_t)(((b0 & 0x1F) << 6) | ((uint8_t)raw_line[i + 1] & 0x3F));
                i++;
            }
            wbuf[wlen * 2]     = (uint8_t)(wc & 0xFF);
            wbuf[wlen * 2 + 1] = (uint8_t)(wc >> 8);
            wlen++;
        }
        cs2d_console_line_t *ln = &lines[count++];
        utf16_to_colored_utf8(wbuf, wlen, &ln->r, &ln->g, &ln->b, ln->text, sizeof(ln->text));
    }
    fclose(f);
    return count;
}

bool cs2d_client_load_serverinfo(cs2d_client_t *client, const char *filepath)
{
    if (!client || !filepath) return false;
    int n = load_text_lines(filepath, client->server_info_lines, CS2D_SERVER_INFO_MAX_LINES);
    if (n < 0) return false;
    client->server_info_line_count = n;
    client->server_info_scroll.top = 0;
    return true;
}

/*
 * Menu button 1: the "Map Briefing" window shows maps/<map>.txt, which the original loads with the map
 * (0x1401d3450). Without lines it prints "there is no briefing for this map" (lang 0233) and returns false.
 */
bool cs2d_client_briefing_open(cs2d_client_t *client)
{
    char path[192];
    snprintf(path, sizeof(path), "maps/%s.txt", client->net_map_name);
    int n = load_text_lines(path, client->briefing_lines, CS2D_SERVER_INFO_MAX_LINES);
    client->briefing_line_count = n > 0 ? n : 0;
    client->briefing_scroll.top = 0;
    if (client->briefing_line_count < 1) {
        cs2d_client_center_msg(client, 255, 0, 0, "there is no briefing for this map");
        return false;
    }
    client->briefing_open = true;
    return true;
}

static void count_team_players(const cs2d_client_t *client, int *out_t, int *out_ct, int t_looks[4], int ct_looks[4])
{
    int nt = 0, nct = 0;
    if (t_looks)  memset(t_looks,  0, sizeof(int) * 4);
    if (ct_looks) memset(ct_looks, 0, sizeof(int) * 4);
    if (!client) {
        if (out_t)  *out_t  = 0;
        if (out_ct) *out_ct = 0;
        return;
    }
    for (int i = 1; i <= CS2D_MAX_PLAYERS; i++) {
        const cs2d_player_t *p = &client->game.players[i];
        if (p->id == 0 && p->name[0] == '\0') continue;
        if (p->team == CS2D_TEAM_T) {
            nt++;
            if (t_looks) t_looks[p->look & 3]++;
        } else if (p->team == CS2D_TEAM_CT || p->team == CS2D_TEAM_VIP) {
            nct++;
            if (ct_looks) ct_looks[p->look & 3]++;
        }
    }
    if (out_t)  *out_t  = nt;
    if (out_ct) *out_ct = nct;
}

void cs2d_client_select_team(cs2d_client_t *client, uint8_t team, uint8_t look)
{
    if (!client) return;

    /* team >= 4 means Main Menu "5 Auto-Select": pick the team with fewer players and auto look (4) */
    if (team >= 4) {
        int nt = 0, nct = 0;
        count_team_players(client, &nt, &nct, NULL, NULL);
        if (nt < nct)      team = (uint8_t)CS2D_TEAM_T;
        else if (nct < nt) team = (uint8_t)CS2D_TEAM_CT;
        else               team = (uint8_t)((rand() & 1) ? CS2D_TEAM_CT : CS2D_TEAM_T);
        look = 4;
    }

    client->team_menu_open   = false;
    client->team_menu_page   = 0;
    client->server_info_open = false;
    client->mouse_fire       = false;
    client->spec.idle_cam    = false; /* Every Tgui_hudmen button ends the spawn showcase (DAT_140ddf328 = 0) */

    cs2d_player_t *pl = &client->game.players[client->local_player_id];

    if (client->connected && client->join_stage == 5) {
        /* Online (FUN_1401e92d0 & ded FUN_1401cd140): send [20, team, look] (Spec sends [20, 0, 5], Auto skin sends look=4) */
        uint8_t wire_look = (team == CS2D_TEAM_SPEC) ? 5 : ((look <= 4) ? look : 4);
        uint8_t team_pkt[4];
        size_t team_len = cs2d_net_build_team_packet(0, team, wire_look, team_pkt, sizeof(team_pkt));
        if (team_len > 0) {
            cs2d_client_send_udp(client, team_pkt, team_len, true);
        }
        if (team == CS2D_TEAM_SPEC) {
            pl->team   = CS2D_TEAM_SPEC;
            pl->alive  = false;
            pl->health = 0;
        } else {
            pl->team = (cs2d_team_t)team;
            if (wire_look <= 3) pl->look = wire_look;
        }
    } else {
        /* Offline: apply team and look immediately and spawn if dead */
        if (team == CS2D_TEAM_SPEC) {
            pl->team   = CS2D_TEAM_SPEC;
            pl->alive  = false;
            pl->health = 0;
        } else {
            pl->team = (cs2d_team_t)team;
            pl->look = (look <= 3) ? look : (uint8_t)(rand() & 3);
            if (!pl->alive) {
                cs2d_vec2_t sp = pl->pos;
                cs2d_map_get_spawn(&client->game.map, pl->team, 0, &sp);
                cs2d_player_spawn(pl, pl->team, sp);
            }
        }
    }
}

/* Console output of the file transfer module */
static void xfer_console_log(void *user, uint8_t r, uint8_t g, uint8_t b, const char *text)
{
    cs2d_client_console_print((cs2d_client_t *)user, r, g, b, "%s", text);
}


/* light / mapshadows / shadows / playershadows -> the light engine (FUN_1401733b0) */
static void apply_light_config(const cs2d_client_t *client)
{
    /* sv_forcelight turns the engine on at quality 1 for a player with "light 0" ([237] case 0x13) */
    int light = client->config.light == 0 && client->net_forcelight ? 1 : client->config.light;
    cs2d_light_config_t lc = {light, client->config.mapshadows, client->config.shadows, client->config.playershadows};
    cs2d_light_configure(&lc);
    cs2d_light_set_daylight(client->net_daylight);
}

bool cs2d_client_init(cs2d_client_t *client, const char *map_name)
{
    if (!client) return false;
    memset(client, 0, sizeof(*client));

    cs2d_ui_init(&client->ui);
    srand((unsigned)time(NULL)); /* Random spawn points, like the dedicated server */
    client->show_fps    = true;
    client->fps_current = 63;

    /* Default Server Info state (overwritten by [252, 6] and [240] when joining a server) */
    strncpy(client->net_server_name, "CS2D Server", sizeof(client->net_server_name) - 1);
    client->net_hovertext = 31; /* The dedicated's default mp_hovertext (proxy capture of [252,6]) */
    client->net_hudscale  = 0;  /* mp_hudscale default (0x140a14de8) */
    client->net_daylight = -1;
    client->net_ticks     = -1;
    cs2d_client_load_serverinfo(client, "sys/serverinfo.txt");

    /* Default Create Server & Options UI state matching CS2D screenshots */
    strncpy(client->srv_name, "Unreal Software's CS2D", sizeof(client->srv_name) - 1);
    client->srv_name_cursor     = (int)strlen(client->srv_name);
    client->srv_port            = 36963;
    client->srv_max_players     = 12;
    client->srv_gamemode_idx    = 0; /* "Standard" */
    client->srv_fow_idx         = 0; /* "Off" */
    client->srv_reg_usgn        = true;
    client->srv_auto_balance    = true;
    /* The original preselects sv_map (0x140a14ec0[0], sys/server.cfg), else the first map of the list */
    strncpy(client->srv_selected_map, "de_dust2", sizeof(client->srv_selected_map) - 1);

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
    cs2d_render_set_shadow_intensity(client->config.shadows);
    cs2d_render_set_player_shadows(client->config.playershadows);
    cs2d_render_set_item_shadows(client->config.itemshadows);
    cs2d_render_set_vsync(client->config.vsync);
    apply_light_config(client);
    cs2d_binds_defaults(&client->binds);
    cs2d_binds_load(&client->binds, "sys/controls.cfg");
    client->opt_name_cursor = (int)strlen(client->config.name);
    cs2d_xfer_init(&client->xfer);
    client->xfer.accept   = client->config.download;
    client->xfer.log      = xfer_console_log;
    client->xfer.log_user = client;
    cs2d_projectile_init(&client->projectiles);

    client->mouse_screen_x = (float)client->config.window_width * 0.5f + 110.0f;
    client->mouse_screen_y = (float)client->config.window_height * 0.5f - 10.0f;

    /* Initialize global CS2D sprite bank (player skins, legs, equipment, weapons, HUD, entities) */
    cs2d_gfx_bank_init(cs2d_render_get_gfx_bank());

    /* Populate authentic startup Console log lines matching CS2D.exe */
    cs2d_client_console_print(client, 255, 220, 0,   "Steam: Not logged in");
    cs2d_client_console_print(client, 170, 170, 170, "UDP socket initialized using port 59960");
    cs2d_client_console_print(client, 255, 45,  45,  "U.S.G.N.: Transmission cancelled by user. Unable to check version/news");

    client->net_sock  = INVALID_SOCKET;
    client->connected = false;
    client->servers.sock     = INVALID_SOCKET;
    client->servers.epoch_ms = (uint32_t)GetTickCount();   /* MilliSecs() base for pings and request ticks */

    /* Enter the first scene right away so callers can rely on its state after init */
    if (map_name && map_name[0]) {
        cs2d_client_start_game(client, map_name);
    } else {
        cs2d_client_set_scene(client, CS2D_SCENE_LOBBY);
    }
    client->scene = client->pending_scene;
    client->scene_switch_pending = false;
    cs2d_scene_get(client->scene)->enter(client);

    return true;
}

void cs2d_client_shutdown(cs2d_client_t *client)
{
    if (!client) return;

    cs2d_scene_get(client->scene)->leave(client);
    cs2d_sv_browser_close(&client->servers);
    cs2d_xfer_free(&client->xfer);
    cs2d_gfx_bank_free(cs2d_render_get_gfx_bank());
}

/* Number of active entries in sys/filters.cfg, shown on the "Filters (N)" button */
static int count_server_filters(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char line[128];
    int n = 0;
    while (fgets(line, sizeof(line), f)) {
        char key[64], val[64] = "";
        if (sscanf(line, "%63s %63s", key, val) < 1 || strncmp(key, "filter_", 7) != 0) continue;
        if (val[0] != '\0' && strcmp(val, "0") != 0) n++;
    }
    fclose(f);
    return n;
}

/*
 * "Find Servers". There is no U.S.G.N. connection, so the Internet list is the cache
 * sys/core/svl.cache: loaded here like at the original's startup (offlineserverlist_load(1)),
 * then cleared and reloaded when the 15 s U.S.G.N. wait runs out (servers_use_cache).
 */
void cs2d_client_open_servers(cs2d_client_t *client)
{
    if (!client) return;
    uint32_t now = (uint32_t)GetTickCount();
    cs2d_sv_browser_t *b = &client->servers;

    if (b->lists[CS2D_SV_TAB_INTERNET].count == 0) {
        int n = cs2d_sv_load_cache("sys/core/svl.cache", &b->lists[CS2D_SV_TAB_INTERNET], now);
        if (n > 0) cs2d_client_console_print(client, 220, 220, 220, "Cached servers added to list: %d", n);
    } else {
        cs2d_sv_browser_refresh(b, CS2D_SV_TAB_INTERNET, now);
    }
    b->using_cache = false;
    if (b->lists[CS2D_SV_TAB_FAVORITES].count == 0) {
        cs2d_sv_load_address_file("sys/favorites.lst", &b->lists[CS2D_SV_TAB_FAVORITES], now);
    }
    if (b->lists[CS2D_SV_TAB_RECENT].count == 0) {
        cs2d_sv_load_address_file("sys/recentlyjoined.lst", &b->lists[CS2D_SV_TAB_RECENT], now);
    }
    cs2d_sv_browser_open(b, now);
    b->open_ms = now;

    client->servers_filters    = count_server_filters("sys/filters.cfg");
    client->servers_open       = true;
    client->servers_scroll.top = 0;
    client->create_server_open = false;
    client->options_open       = false;
}

/* Joins a server from the browser; the entry is remembered in the Recently Joined tab */
static void servers_join(cs2d_client_t *client, uint32_t ip, uint16_t port)
{
    char addr[16], cmd[64];
    cs2d_sv_ip_to_str(ip, addr);
    snprintf(cmd, sizeof(cmd), "connect %s %u", addr, (unsigned)port);
    cs2d_sv_t *src    = NULL;
    cs2d_sv_t *recent = cs2d_sv_list_add(&client->servers.lists[CS2D_SV_TAB_RECENT], ip, port, (uint32_t)GetTickCount());
    for (int t = 0; t < CS2D_SV_TAB_COUNT && !src; t++) {
        src = cs2d_sv_list_find(&client->servers.lists[t], ip, port);
        if (src == recent) src = NULL;
    }
    if (recent && src && src->active) *recent = *src;
    client->servers_open    = false;
    client->connect_ip_open = false;
    cs2d_sv_browser_close(&client->servers);
    cs2d_client_console_exec(client, cmd);
}

const cs2d_scene_t *cs2d_scene_get(cs2d_scene_id_t id)
{
    switch (id) {
        case CS2D_SCENE_GAME:   return &cs2d_scene_game;
        case CS2D_SCENE_EDITOR: return &cs2d_scene_editor;
        case CS2D_SCENE_LOBBY:
        default:                return &cs2d_scene_lobby;
    }
}

void cs2d_client_set_scene(cs2d_client_t *client, cs2d_scene_id_t scene)
{
    if (!client) return;
    client->pending_scene        = scene;
    client->scene_switch_pending = true;
}

void cs2d_client_start_game(cs2d_client_t *client, const char *map_name)
{
    if (!client) return;
    const char *name = (map_name && map_name[0]) ? map_name : "de_dust2";

    if (client->scene == CS2D_SCENE_GAME && !client->scene_switch_pending) {
        cs2d_scene_game_change_map(client, name);
        return;
    }
    snprintf(client->pending_map, sizeof(client->pending_map), "%s", name);
    cs2d_client_set_scene(client, CS2D_SCENE_GAME);
}

static void apply_pending_scene(cs2d_client_t *client)
{
    if (!client->scene_switch_pending) return;
    client->scene_switch_pending = false;

    /* Drop held input so nothing leaks from one scene into the next */
    client->key_forward   = false;
    client->key_back      = false;
    client->key_left      = false;
    client->key_right     = false;
    client->key_walk      = false;
    client->mouse_fire    = false;
    client->mouse_attack2 = false;

    cs2d_scene_get(client->scene)->leave(client);
    client->scene = client->pending_scene;
    cs2d_scene_get(client->scene)->enter(client);
}

void cs2d_client_handle_escape(cs2d_client_t *client)
{
    if (!client) return;
    if (client->chat_open) {
        client->chat_open = false;
        client->chat_input[0] = '\0';
        client->chat_cursor = 0;
        return;
    }
    cs2d_scene_get(client->scene)->handle_escape(client);
}

/* Raw datagram trace to stdout when the CS2D_NET_LOG environment variable is set (protocol research) */
static void net_log_datagram(const char *dir, const uint8_t *data, int len)
{
    static int enabled = -1;
    if (enabled < 0) enabled = (getenv("CS2D_NET_LOG") != NULL);
    if (!enabled || len <= 0) return;
    printf("[net %s %4d]", dir, len);
    for (int i = 0; i < len; i++) printf(" %02x", data[i]);
    printf("\n");
    fflush(stdout);
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
    net_log_datagram("out", dgram, (int)(len + 2));
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
    net_log_datagram("ack", dgram, 5);
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
    client->net_sent_x   = client->net_sent_y = client->net_sent_rot = -1;
    client->fire_seed    = -1;
    client->download_open = false;
    client->join_menu     = 0;
    memset(client->net_seen_seq, 0, sizeof(client->net_seen_seq));
    cs2d_xfer_reset(&client->xfer);

    /* Initiate Opcode 252 Stage 0 Join Request ([252, 0, ""]) */
    cs2d_pkt_t p0;
    cs2d_net_build_join_stage0(&p0);
    cs2d_client_send_udp(client, p0.buf, (size_t)p0.pos, true);

    return true;
}

static void add_ground_item(cs2d_client_t *client, uint16_t id, uint8_t type, int tx, int ty, int16_t clip, int16_t reserve)
{
    cs2d_ground_item_t *slot = NULL;
    for (int i = 0; i < CS2D_MAX_GROUND_ITEMS && !slot; i++) {
        if (client->ground_items[i].active && client->ground_items[i].id == id) slot = &client->ground_items[i];
    }
    for (int i = 0; i < CS2D_MAX_GROUND_ITEMS && !slot; i++) {
        if (!client->ground_items[i].active) slot = &client->ground_items[i];
    }
    if (!slot) return;
    slot->active  = true;
    slot->id      = id;
    slot->type    = type;
    slot->x       = (float)tx * CS2D_TILE_SIZE + 16.0f;
    slot->y       = (float)ty * CS2D_TILE_SIZE + 16.0f;
    slot->clip    = clip;
    slot->reserve = reserve;
}

/*
 * Titem.give (FUN_14020ce60) case 0x46 / 0x47: a player touches a flag ([25] collect, type 70 red / 71 blue). The
 * enemy team takes it (Tpl.flag = 1, sys/language 0707 / 0708 in the team color, 0709 to the carrier); the owner
 * team returns it, which the server reports with [31] (cs2d_scene_game_apply_entity_state: "... Flag returned").
 * The money bonus of a return is not shown here: [252] / [53] carry the money. Proxy: ctf_probe.lua, collect
 * [25, 1, 47, 71, ...] then [31, 20, 17, 1] when a Terrorist takes the CT flag on ctf_cs2dfortress.
 */
static void collect_flag(cs2d_client_t *client, int pid, uint8_t type)
{
    cs2d_player_t *p = &client->game.players[pid];
    bool red = (type == CS2D_WEP_REDFLAG);
    if (red ? p->team < CS2D_TEAM_CT : p->team != CS2D_TEAM_T) return;
    p->has_flag = true;
    char line[96];
    snprintf(line, sizeof(line), red ? "Red Flag taken by %s" : "Blue Flag taken by %s", p->name);
    if (red) cs2d_client_center_msg(client, 255, 25, 0, line);
    else     cs2d_client_center_msg(client, 50, 150, 255, line);
    if (pid == client->local_player_id) cs2d_client_center_msg(client, 0, 255, 0, "You have the flag! Bring it to your base!");
}

/* FUN_1401f8350 ([24] drop) for item 70 / 71: the carrier loses Tpl.flag; sys/language 0704 / 0705 */
static void drop_flag(cs2d_client_t *client, int pid, uint8_t type)
{
    cs2d_player_t *p = &client->game.players[pid];
    bool red = (type == CS2D_WEP_REDFLAG);
    if (p->has_flag && (red ? p->team >= CS2D_TEAM_CT : p->team == CS2D_TEAM_T)) p->has_flag = false;
    char line[96];
    snprintf(line, sizeof(line), red ? "Red Flag dropped by %s" : "Blue Flag dropped by %s", p->name);
    if (red) cs2d_client_center_msg(client, 255, 25, 0, line);
    else     cs2d_client_center_msg(client, 50, 150, 255, line);
}

bool cs2d_strip_center_tag(char *text)
{
    if (!text) return false;
    size_t len = strlen(text);
    if (len >= 2 && text[len - 2] == '@' && (text[len - 1] == 'C' || text[len - 1] == 'c')) {
        text[len - 2] = '\0';
        return true;
    }
    return false;
}

bool cs2d_parse_color_code(const char *in, uint8_t *out_r, uint8_t *out_g, uint8_t *out_b, const char **out_text)
{
    if (out_r) *out_r = 255;
    if (out_g) *out_g = 220;
    if (out_b) *out_b = 0;
    if (out_text) *out_text = in;
    if (!in) return false;

    /* Single byte 0xA9 (Latin-1 '©') + 9 ASCII digits RRRGGGBBB */
    if ((unsigned char)in[0] == 0xA9) {
        bool ok = true;
        for (int i = 1; i <= 9; i++) {
            if (in[i] < '0' || in[i] > '9') { ok = false; break; }
        }
        if (ok) {
            int r = (in[1] - '0') * 100 + (in[2] - '0') * 10 + (in[3] - '0');
            int g = (in[4] - '0') * 100 + (in[5] - '0') * 10 + (in[6] - '0');
            int b = (in[7] - '0') * 100 + (in[8] - '0') * 10 + (in[9] - '0');
            if (out_r) *out_r = (uint8_t)(r > 255 ? 255 : r);
            if (out_g) *out_g = (uint8_t)(g > 255 ? 255 : g);
            if (out_b) *out_b = (uint8_t)(b > 255 ? 255 : b);
            if (out_text) *out_text = in + 10;
            return true;
        }
    }
    /* UTF-8 encoded "\xC2\xA9" ('©') + 9 ASCII digits RRRGGGBBB */
    if ((unsigned char)in[0] == 0xC2 && (unsigned char)in[1] == 0xA9) {
        bool ok = true;
        for (int i = 2; i <= 10; i++) {
            if (in[i] < '0' || in[i] > '9') { ok = false; break; }
        }
        if (ok) {
            int r = (in[2] - '0') * 100 + (in[3] - '0') * 10 + (in[4] - '0');
            int g = (in[5] - '0') * 100 + (in[6] - '0') * 10 + (in[7] - '0');
            int b = (in[8] - '0') * 100 + (in[9] - '0') * 10 + (in[10] - '0');
            if (out_r) *out_r = (uint8_t)(r > 255 ? 255 : r);
            if (out_g) *out_g = (uint8_t)(g > 255 ? 255 : g);
            if (out_b) *out_b = (uint8_t)(b > 255 ? 255 : b);
            if (out_text) *out_text = in + 11;
            return true;
        }
    }
    return false;
}

void cs2d_client_center_msg(cs2d_client_t *client, uint8_t r, uint8_t g, uint8_t b, const char *text)
{
    if (!client || !text || text[0] == '\0') return;
    char buf[96];
    snprintf(buf, sizeof(buf), "%s", text);
    cs2d_strip_center_tag(buf);

    int slot = -1;
    for (int i = 0; i < 4 && slot < 0; i++) {
        if (client->center_ms[i] && strcmp(client->center_msg[i], buf) == 0) slot = i;
    }
    for (int i = 0; i < 4 && slot < 0; i++) {
        if (!client->center_ms[i]) slot = i;
    }
    if (slot < 0) { /* All four in use: replace the oldest */
        slot = 0;
        for (int i = 1; i < 4; i++) if (client->center_ms[i] < client->center_ms[slot]) slot = i;
    }
    snprintf(client->center_msg[slot], sizeof(client->center_msg[slot]), "%s", buf);
    client->center_rgb[slot][0] = r;
    client->center_rgb[slot][1] = g;
    client->center_rgb[slot][2] = b;
    client->center_ms[slot] = GetTickCount() | 1;
}

/* Thud_msg.tintTeamColor (0x140083a40): spectator gold, T red and CT blue in team games, green otherwise */
static void killmsg_tint(const cs2d_client_t *client, cs2d_killmsg_t *m, int i, const cs2d_player_t *p)
{
    static const uint8_t gold[3] = {255, 220, 0}, t[3] = {255, 25, 0}, ct[3] = {50, 150, 255}, dm[3] = {0, 255, 0};
    const uint8_t *c = p->team == CS2D_TEAM_SPEC ? gold
                     : client->net_sv_gamemode == 1 ? dm
                     : p->team == CS2D_TEAM_T ? t : ct;
    memcpy(m->rgb[i], c, 3);
}

/* Weapon name of a kill (0x1409fff98, or the specials of add_kill: sys/language 0737 / 0736 / 0735 and the building
 * names 0x1409f79f8 [8] / [6] / [2]); "" for an unknown weapon */
static const char *killmsg_weapon_name(int weapon)
{
    switch (weapon) {
    case 0xf8: return "NPC";
    case 0xf9: return "Creature";
    case 0xfb: return "Explosion";
    case 0xfd: return "Turret";
    case 0xfe: return "Gate Field";
    case 0xff: return "Barbed Wire";
    }
    const cs2d_weapon_def_t *def = (weapon > 0 && weapon < 0xf8) ? cs2d_weapon_get((uint8_t)weapon) : NULL;
    return def && def->name ? def->name : "";
}

static const cs2d_player_t *killmsg_player(const cs2d_client_t *client, int id)
{
    if (id <= 0 || id > CS2D_MAX_PLAYERS || client->game.players[id].id == 0) return NULL; /* Tpl::get */
    return &client->game.players[id];
}

void cs2d_client_add_kill(cs2d_client_t *client, int killer, int weapon, int victim, int assist)
{
    if (!client) return;
    if (client->killmsg_count >= CS2D_KILLMSG_MAX) { /* Count > 4: the oldest goes */
        memmove(&client->killmsgs[0], &client->killmsgs[1], sizeof(cs2d_killmsg_t) * (CS2D_KILLMSG_MAX - 1));
        client->killmsg_count = CS2D_KILLMSG_MAX - 1;
    }
    cs2d_killmsg_t *m = &client->killmsgs[client->killmsg_count++];
    memset(m, 0, sizeof(*m));
    const char *wname = killmsg_weapon_name(weapon);
    const cs2d_player_t *k = killmsg_player(client, killer);
    const cs2d_player_t *v = killmsg_player(client, victim);
    static const uint8_t gold[3] = {255, 220, 0};
    memcpy(m->rgb[1], gold, 3);

    if (killer == victim) {
        if (k) { snprintf(m->txt[0], sizeof(m->txt[0]), "%s", k->name); killmsg_tint(client, m, 0, k); }
        m->wpn = weapon;
        snprintf(m->txt[1], sizeof(m->txt[1]), "%s", wname);
        cs2d_client_console_print(client, 255, 255, 255, "%s killed himself with %s", m->txt[0], wname);
    } else if (!wname[0]) {
        if (v) { snprintf(m->txt[0], sizeof(m->txt[0]), "%s", v->name); killmsg_tint(client, m, 0, v); }
        snprintf(m->txt[1], sizeof(m->txt[1]), "died"); /* 0738 */
        cs2d_client_console_print(client, 255, 255, 255, "%s died", m->txt[0]);
    } else {
        if (k) { snprintf(m->txt[0], sizeof(m->txt[0]), "%s", k->name); killmsg_tint(client, m, 0, k); }
        m->wpn = weapon;
        snprintf(m->txt[1], sizeof(m->txt[1]), "%s", wname);
        if (v) {
            snprintf(m->txt[2], sizeof(m->txt[2]), "%s", v->name);
            killmsg_tint(client, m, 2, v);
            const cs2d_player_t *a = assist > 0 ? killmsg_player(client, assist) : NULL;
            if (a) { snprintf(m->txt[3], sizeof(m->txt[3]), "%s", a->name); killmsg_tint(client, m, 3, a); }
        }
        if (!m->txt[0][0]) cs2d_client_console_print(client, 255, 255, 255, "%s killed by %s", m->txt[2], wname);
        else cs2d_client_console_print(client, 255, 255, 255, "%s killed %s with %s", m->txt[0], m->txt[2], wname);
    }
    m->timer_ms = GetTickCount();
    m->show_ms  = (uint32_t)client->config.hud_deathnotice_time * 1000u;
    m->fade     = 1.0f;
    m->offset   = 17.0f;
}

/*
 * Adds an in-game chat message to the client's HUD chat log (Thud_msg.add_chat 0x140117a90).
 * Team colors (tintTeamColor 0x140083a40):
 *   Spec (0) = (255, 220, 0)
 *   T    (1) = (255, 25, 0)
 *   CT   (2) = (50, 150, 255)
 */
void cs2d_client_add_chat(cs2d_client_t *client, uint8_t sender_id, uint8_t mode,
                          uint8_t r, uint8_t g, uint8_t b, const char *text)
{
    if (!client || !text || text[0] == '\0') return;

    cs2d_chat_line_t line;
    memset(&line, 0, sizeof(line));
    line.time_ms = GetTickCount() | 1;
    line.fade = 1.0f;
    line.text_rgb[0] = r;
    line.text_rgb[1] = g;
    line.text_rgb[2] = b;
    snprintf(line.text, sizeof(line.text), "%s", text);

    if (sender_id > 0 && sender_id <= CS2D_MAX_PLAYERS) {
        const cs2d_player_t *p = &client->game.players[sender_id];
        const char *pname = p->name[0] ? p->name : "Player";
        uint8_t team = p->team;
        bool dead = !p->alive || team == CS2D_TEAM_SPEC;

        if (team == CS2D_TEAM_T) {
            line.name_rgb[0] = 255; line.name_rgb[1] = 25; line.name_rgb[2] = 0;
        } else if (team == CS2D_TEAM_CT) {
            line.name_rgb[0] = 50; line.name_rgb[1] = 150; line.name_rgb[2] = 255;
        } else {
            line.name_rgb[0] = 255; line.name_rgb[1] = 220; line.name_rgb[2] = 0;
        }

        if (mode == 2) {
            if (dead) snprintf(line.name, sizeof(line.name), "*DEAD*(TEAM) %s: ", pname);
            else      snprintf(line.name, sizeof(line.name), "(TEAM) %s: ", pname);
        } else {
            if (dead) snprintf(line.name, sizeof(line.name), "*DEAD* %s: ", pname);
            else      snprintf(line.name, sizeof(line.name), "%s: ", pname);
        }
        cs2d_client_console_print(client, line.name_rgb[0], line.name_rgb[1], line.name_rgb[2],
                                  "%s%s", line.name, line.text);
    } else {
        line.name[0] = '\0';
        line.name_rgb[0] = 255; line.name_rgb[1] = 220; line.name_rgb[2] = 0;
        cs2d_client_console_print(client, r, g, b, "%s", text);
    }

    if (client->chat_line_count < CS2D_CHAT_MAX_LINES) {
        client->chat_lines[client->chat_line_count++] = line;
    } else {
        memmove(&client->chat_lines[0], &client->chat_lines[1],
                sizeof(cs2d_chat_line_t) * (CS2D_CHAT_MAX_LINES - 1));
        client->chat_lines[CS2D_CHAT_MAX_LINES - 1] = line;
    }
}

/*
 * Routes incoming server/broadcast messages (opcode 226 / opcode 240 / sv_msg / msg):
 * If message ends with "@C" or "@c" (or mode == 2 for center), strips the tag and routes
 * to screen center (Thud_msg.add_center). Otherwise routes to HUD chat log (Thud_msg.add_chat).
 */
void cs2d_client_handle_server_msg(cs2d_client_t *client, uint8_t sender_id, uint8_t mode,
                                   uint8_t r, uint8_t g, uint8_t b, const char *text)
{
    if (!client || !text || text[0] == '\0') return;

    /* Parse any leading \xA9RRRGGGBBB color code */
    uint8_t cr = r, cg = g, cb = b;
    const char *cleaned = text;
    cs2d_parse_color_code(text, &cr, &cg, &cb, &cleaned);

    char buf[CS2D_CHAT_LINE_LEN];
    snprintf(buf, sizeof(buf), "%s", cleaned);
    bool center = cs2d_strip_center_tag(buf) || (mode == 2 && sender_id == 0);

    if (center) {
        cs2d_client_center_msg(client, cr, cg, cb, buf);
        cs2d_client_console_print(client, cr, cg, cb, "%s", buf);
    } else {
        cs2d_client_add_chat(client, sender_id, mode, cr, cg, cb, buf);
    }
}

/*
 * Sends a chat message (mode 1 = say, mode 2 = sayteam).
 * Online: sends opcode 240 packet [240, mode, len_u16, len*2 bytes UTF-16LE].
 * Offline: adds directly to local chat.
 */
void cs2d_client_send_chat(cs2d_client_t *client, uint8_t mode, const char *text)
{
    if (!client || !text || text[0] == '\0') return;

    if (client->connected && client->join_stage == 5) {
        size_t slen = strlen(text);
        if (slen > 120) slen = 120;
        uint8_t pkt[4 + 128 * 2];
        pkt[0] = (uint8_t)CS2D_NET_MSG2; /* 240 */
        pkt[1] = mode;
        pkt[2] = (uint8_t)(slen & 0xFF);
        pkt[3] = (uint8_t)((slen >> 8) & 0xFF);
        for (size_t i = 0; i < slen; i++) {
            pkt[4 + i * 2]     = (uint8_t)text[i];
            pkt[4 + i * 2 + 1] = 0;
        }
        cs2d_client_send_udp(client, pkt, 4 + slen * 2, true);
    } else {
        cs2d_client_add_chat(client, client->local_player_id, mode, 255, 220, 0, text);
    }
}

/* Sends Lua menu response [235, pid, slen:u16, title:u16*, button:u8] (FUN_14015c320) */
void cs2d_client_send_luamenu(cs2d_client_t *client, uint8_t button)
{
    if (!client || !client->connected) return;
    const char *title = client->luamenu.title;
    uint16_t slen = (uint16_t)strlen(title);
    if (slen > 400) slen = 400;

    /* Packet: 1 byte opcode (235), 1 byte pid, 2 bytes slen (LE), slen*2 bytes UTF-16LE, 1 byte button */
    size_t pkt_len = 1 + 1 + 2 + slen * 2 + 1;
    uint8_t pkt[1024];
    if (pkt_len > sizeof(pkt)) return;

    pkt[0] = (uint8_t)CS2D_NET_LUAMENU; /* 235 */
    pkt[1] = client->local_player_id;
    pkt[2] = (uint8_t)(slen & 0xFF);
    pkt[3] = (uint8_t)((slen >> 8) & 0xFF);
    for (uint16_t i = 0; i < slen; i++) {
        pkt[4 + i * 2]     = (uint8_t)title[i];
        pkt[4 + i * 2 + 1] = 0;
    }
    pkt[4 + slen * 2] = button;

    cs2d_client_send_udp(client, pkt, pkt_len, true);
}

static void remove_ground_item(cs2d_client_t *client, uint16_t id)
{
    for (int i = 0; i < CS2D_MAX_GROUND_ITEMS; i++) {
        if (client->ground_items[i].active && client->ground_items[i].id == id) client->ground_items[i].active = false;
    }
}

/* FUN_1402c9f40: SHA-256 of maps/<map>.map (maps/temp.map in temporary mode), "" when the file is missing */
static void join_map_checksum(cs2d_client_t *client)
{
    char map_path[256];
    if (client->xfer.map_mode == 1) {
        snprintf(map_path, sizeof(map_path), "maps/temp.map");
    } else {
        snprintf(map_path, sizeof(map_path), "maps/%s.map", client->net_map_name);
    }
    cs2d_net_sha256_file(map_path, client->net_map_checksum);
}

static void join_menu_submit(cs2d_client_t *client);

/* Gives up a join that is still in the handshake: [253,reason], socket closed, back to the menu */
static void join_abort(cs2d_client_t *client, uint8_t reason)
{
    if (client->net_sock != INVALID_SOCKET) {
        uint8_t leave_pkt[2] = { (uint8_t)CS2D_NET_LEAVE, reason };
        cs2d_client_send_udp(client, leave_pkt, sizeof(leave_pkt), true);
        closesocket(client->net_sock);
        client->net_sock = INVALID_SOCKET;
    }
    client->join_stage     = 0;
    client->connected      = false;
    client->download_open  = false;
    client->join_menu      = 0;
    client->xfer.map_mode  = 0;
    if (client->scene != CS2D_SCENE_LOBBY) cs2d_client_set_scene(client, CS2D_SCENE_LOBBY);
}

/*
 * Refused join (Tgui_joinmen.errorprompt 0x14008b090, menu 2): the window stays open on the server's socket
 * until Okay / Cancel; status 11 (map differs) also shows the three map choices
 */
static void join_error_prompt(cs2d_client_t *client, uint8_t status)
{
    client->join_stage = 0;
    client->join_menu  = 2;
    client->join_error = status;
    client->join_opt   = 0;
}

/*
 * [252,4,0]: the original stays in the menu during the handshake and download, and enters the game only
 * now, with the server's map. Joining from the menu switches scenes right here, so the snapshot that
 * follows lands on the loaded map; the game scene sees the open socket and keeps it.
 */
static void join_load_map(cs2d_client_t *client, const char *map)
{
    if (client->scene != CS2D_SCENE_GAME || client->scene_switch_pending) {
        snprintf(client->pending_map, sizeof(client->pending_map), "%s", map);
        cs2d_client_set_scene(client, CS2D_SCENE_GAME);
        apply_pending_scene(client);
    } else if (_stricmp(map, client->game_map_name) != 0) {
        cs2d_scene_game_change_map(client, map);
    }
    /* Keep local player in Spectator after map load so Select Team starts at (0) */
    client->game.players[client->local_player_id].team  = CS2D_TEAM_SPEC;
    client->game.players[client->local_player_id].alive = false;
}

/*
 * [34] from the server (FUN_1402da280 -> FUN_1401fab30 with param_10 = 0), 15 bytes:
 *   [34, player, team, type, tile x u16, tile y u16, mode, rot + 1000 u16, money u16, object id u16]
 * A type of 1..239 is a new object (normally a Construction Site whose mode is the building it becomes); the
 * builder's money is set. 0xf8..0xff refuse the local player's build with a red center message
 * (0716, 0715, 0714, 0713, 0712, 0711, 0562, 0710: the g_lang offsets at 0x1401fbece).
 */
static bool net_build_result(cs2d_client_t *client, const uint8_t *buf, int len, int pos)
{
    if (pos + 15 > len) return false;
    const uint8_t *b = &buf[pos + 1];
    int pid = b[0], typ = b[2];
    if (pid < 1 || pid > CS2D_MAX_PLAYERS || client->game.players[pid].id == 0) return true;
    cs2d_player_t *pl = &client->game.players[pid];
    if (typ >= 1 && typ < 0xf0) {
        if (pid == client->local_player_id) pl->money = b[10] | (b[11] << 8);
        cs2d_dynobj_t *o = cs2d_dynobj_add(&client->dynobj, b[12] | (b[13] << 8), typ,
                                           (b[3] | (b[4] << 8)) * 32, (b[5] | (b[6] << 8)) * 32);
        if (o) {
            o->player = pid;
            o->mode   = b[7];
            o->health = 100;
            o->team   = b[1];
            o->rot    = (float)((b[8] | (b[9] << 8)) - 1000);
            if (typ == CS2D_DO_TURRET) {
                o->rot  = (float)(1 + rand() % 360);
                o->rrot = o->rot;
            }
        }
    } else if (pid == client->local_player_id && typ >= 0xf8) {
        static const char *const msgs[8] = {
            "Your last building has not been finished yet",                /* 0xf8: 0716 */
            "It's not allowed to build that on this server",               /* 0xf9: 0715 */
            "You can't build here - buildings of same type are too close", /* 0xfa: 0714 */
            "You can't build more buildings of this type",                 /* 0xfb: 0713 */
            "You can't build here - other players are too close",          /* 0xfc: 0712 */
            "You are not allowed to build this",                           /* 0xfd: 0711 */
            "You have insufficient funds",                                 /* 0xfe: 0562 */
            "You can't build here"                                         /* 0xff: 0710 */
        };
        cs2d_client_center_msg(client, 255, 0, 0, msgs[typ - 0xf8]);
    }
    return true;
}

/*
 * [252,7,1]: the players already in the game (FUN_1402ce7c0 at 0x1402cd9a0, proxy): count u8, then per player
 *   id, name (u8 length, cipher key "name"), spray (key "spr"), usgn id i32, steam os u8, steam name, steam id i64,
 *   usgn name (the two names: u16 count + UTF-16, empty in every capture), team, spec state, look,
 *   score u16 (+1000), deaths u16, assists u16, mvp u16, rank, x u16, y u16, rot u16 (360 / 65536 deg),
 *   health, armor, special byte, weapon, weapon mode, votekick, votemap u16
 * The local player's own record is skipped. Without it a spectator has nobody to follow.
 */
/* Tpl.votemap as stored (index + 1, 0 = none); the dedicated sends 0xffff and other codes >= 0xff9b for none */
static int vote_map_slot(int v)
{
    return (v < 0xff9b) ? v + 1 : 0;
}

static void join_players(cs2d_client_t *client, const uint8_t *buf, int len, int *pos)
{
    int p = *pos;
    if (p >= len) return;
    int count = buf[p++];
    for (int k = 0; k < count; k++) {
        if (p + 2 > len) break;
        int pid = buf[p++];
        char name[CS2D_MAX_NAME_LEN];
        int nlen = buf[p++];
        if (p + nlen > len) break;
        cs2d_net_decrypt(&buf[p], nlen, "name", name, sizeof(name));
        p += nlen;
        if (p + 1 > len || p + 1 + buf[p] > len) break;
        p += 1 + buf[p];                              /* spray name */
        p += 4 + 1;                                   /* usgn id, steam os */
        if (p + 2 > len) break;
        p += 2 + 2 * (buf[p] | (buf[p + 1] << 8));    /* steam name */
        p += 8;                                       /* steam id */
        if (p + 2 > len) break;
        p += 2 + 2 * (buf[p] | (buf[p + 1] << 8));    /* usgn name */
        if (p + 26 > len) break;
        const uint8_t *r = &buf[p];
        p += 26;
        if (pid < 1 || pid > CS2D_MAX_PLAYERS || pid == client->local_player_id) continue;

        cs2d_player_t *pl = &client->game.players[pid];
        memset(pl, 0, sizeof(*pl));
        cs2d_game_add_player(&client->game, pid, name, r[0] <= CS2D_TEAM_VIP ? (cs2d_team_t)r[0] : CS2D_TEAM_SPEC);
        pl->look   = (r[2] <= 3) ? r[2] : 0;
        pl->score  = (r[3] | (r[4] << 8)) - 1000;
        pl->deaths = r[5] | (r[6] << 8);
        pl->assists = r[7] | (r[8] << 8);
        pl->mvp     = r[9] | (r[10] << 8);
        pl->pos.x  = (float)(r[12] | (r[13] << 8));
        pl->pos.y  = (float)(r[14] | (r[15] << 8));
        pl->angle  = (float)(r[16] | (r[17] << 8)) * 0.005493164f;
        pl->health = r[18];
        pl->armor  = r[19];
        pl->alive  = pl->team != CS2D_TEAM_SPEC && pl->health > 0;
        pl->votekick = r[23];
        pl->votemap  = vote_map_slot(r[24] | (r[25] << 8));
        if (r[21] != 0) {
            cs2d_player_give_weapon(pl, r[21]);
            cs2d_player_select_weapon(pl, r[21]);
        }
    }
    *pos = p;
}

/* [252,50,...]: file list, cache checksums and file blocks (FUN_1402cd970) */
static void join_transfer(cs2d_client_t *client, const uint8_t *buf, int len, int *pos)
{
    cs2d_xfer_event_t ev = cs2d_xfer_on_message(&client->xfer, buf, len, pos);
    if (ev == CS2D_XFER_EV_LIST_DONE) {
        static cs2d_pkt_t pkts[CS2D_XFER_MAX_PKTS];
        int n = cs2d_xfer_build_requests(&client->xfer, pkts, CS2D_XFER_MAX_PKTS);
        for (int i = 0; i < n; i++) cs2d_client_send_udp(client, pkts[i].buf, (size_t)pkts[i].pos, true);
    } else if (ev == CS2D_XFER_EV_MAP_FAILED) {
        join_abort(client, 0);
    }
}

void cs2d_client_leaderboard_reset(cs2d_client_t *client)
{
    memset(client->lb_rows, 0, sizeof(client->lb_rows));
    memset(client->lb_requested, 0, sizeof(client->lb_requested));
    client->lb_queue_count = 0;
    client->lb_queue_timer = 0;
}

/* Menu button 3 (Tgui_hudmen.draw menu 2): [230,0] asks for the timestamp, then the window opens */
void cs2d_client_leaderboard_open(cs2d_client_t *client)
{
    uint8_t pkt[2] = { (uint8_t)CS2D_NET_LEADERBOARD, 0 };
    cs2d_client_send_udp(client, pkt, sizeof(pkt), false);
    client->leaderboard_open = true;
    client->lb_scroll.top    = 0;
}

/* Tgui_leadlist.draw: every visible rank not asked yet joins LeaderboardRequest.list */
static void leaderboard_request_rows(cs2d_client_t *client, int top, int rows)
{
    for (int r = top + 1; r <= top + rows && r <= CS2D_LEADERBOARD_MAX; r++) {
        if (r < 1 || client->lb_requested[r]) continue;
        client->lb_requested[r] = true;
        if (client->lb_queue_count < CS2D_LEADERBOARD_MAX) client->lb_queue[client->lb_queue_count++] = (uint8_t)r;
    }
}

/* LeaderboardRequest.update (FUN_14005eab0), once per 60 Hz frame: the first request waits 6 frames, then [230,1,rank] */
static void leaderboard_update(cs2d_client_t *client)
{
    if (client->lb_queue_count == 0) return;
    if (client->lb_queue_timer < 6) {
        client->lb_queue_timer++;
        return;
    }
    cs2d_pkt_t p = { .pos = 0 };
    cs2d_pkt_w_byte(&p, (uint8_t)CS2D_NET_LEADERBOARD);
    cs2d_pkt_w_byte(&p, 1);
    cs2d_pkt_w_short(&p, client->lb_queue[0]);
    cs2d_client_send_udp(client, p.buf, (size_t)p.pos, false);
    memmove(&client->lb_queue[0], &client->lb_queue[1], (size_t)(client->lb_queue_count - 1));
    client->lb_queue_count--;
    client->lb_queue_timer = 0;
}

/* TVote.sendVoteAsClient (0x1400536b0): [43, type (1 kick, 2 map), value u16] */
void cs2d_client_send_vote(cs2d_client_t *client, int type, int value)
{
    cs2d_pkt_t p = { .pos = 0 };
    cs2d_pkt_w_byte(&p, (uint8_t)CS2D_NET_VOTE);
    cs2d_pkt_w_byte(&p, (uint8_t)type);
    cs2d_pkt_w_short(&p, (uint16_t)value);
    cs2d_client_send_udp(client, p.buf, (size_t)p.pos, true);
}

/* TVote.votekick / votemap (0x1401c98b0 / 0x1401be500) for a vote the server forwarded */
void cs2d_client_on_vote(cs2d_client_t *client, int voter, int type, int value)
{
    if (voter < 1 || voter > CS2D_MAX_PLAYERS || client->game.players[voter].id == 0) return;
    cs2d_player_t *v = &client->game.players[voter];
    bool self = voter == client->local_player_id;
    char line[160];
    if (type == 1) {
        if (value < 0xff9b) {
            if (value < 1 || value > CS2D_MAX_PLAYERS || client->game.players[value].id == 0) return;
            /* "$voter votes to kick $target" (lang 0723) */
            snprintf(line, sizeof(line), "votes to kick %s", client->game.players[value].name);
            cs2d_client_add_chat(client, (uint8_t)voter, 0, 255, 150, 0, line);
            cs2d_client_console_print(client, 255, 150, 0, "%s votes to kick %s", v->name,
                                      client->game.players[value].name);
            v->votekick = value;
        } else if (self) {
            /* 0xfff5: lang 0720, 0xfff4: 0911, 0xfffe: 0722 */
            const char *msg = value == 0xfff5 ? "Kick voting is disabled on this server"
                            : value == 0xfff4 ? "You can't vote to kick yourself!"
                            : value == 0xfffe ? "Please wait 15 seconds before voting again" : NULL;
            if (msg) cs2d_client_center_msg(client, 255, 0, 0, msg);
        }
    } else if (type == 2) {
        if (value < 0xff9b) {
            if (value >= client->vote_map_count) return;
            /* "$voter votes the map $map" (lang 0725) */
            snprintf(line, sizeof(line), "votes the map %s", client->vote_maps[value]);
            cs2d_client_add_chat(client, (uint8_t)voter, 0, 255, 150, 0, line);
            cs2d_client_console_print(client, 255, 150, 0, "%s votes the map %s", v->name, client->vote_maps[value]);
            v->votemap = value + 1;
        } else if (self) {
            /* 0xffeb: lang 0721, 0xfffe: 0722 */
            const char *msg = value == 0xffeb ? "Map voting is disabled on this server"
                            : value == 0xfffe ? "Please wait 15 seconds before voting again" : NULL;
            if (msg) cs2d_client_center_msg(client, 255, 0, 0, msg);
        }
    }
}

/* Latin-1 bytes (rstr names) to UTF-8 */
static void latin1_to_utf8(const uint8_t *in, int n, char *out, size_t max)
{
    size_t o = 0;
    for (int i = 0; i < n && o + 3 < max; i++) {
        uint8_t c = in[i];
        if (c < 0x80) {
            out[o++] = (char)c;
        } else {
            out[o++] = (char)(0xC0 | (c >> 6));
            out[o++] = (char)(0x80 | (c & 0x3F));
        }
    }
    if (max > 0) out[o < max ? o : max - 1] = '\0';
}

/*
 * [230] leaderboard (FUN_1402e4220): [230,0, timestamp i32] answers the opening request; [230,1, rank u16,
 * timestamp i32, name (u16 len), id type u8 (0 usgn, 1 steam), id i64, score, kills, deaths, assists, mvp,
 * time played (i32 each)] one rank. A new timestamp drops the rows (LeaderboardPlayer.list_top).
 */
static bool leaderboard_on_message(cs2d_client_t *client, const uint8_t *buf, int len, int *pos)
{
    int p = *pos + 1;
    if (p >= len) return false;
    int sub = buf[p++];
    if (sub == 0) {
        if (p + 4 > len) return false;
        int32_t ts = (int32_t)(buf[p] | (buf[p + 1] << 8) | (buf[p + 2] << 16) | ((uint32_t)buf[p + 3] << 24));
        p += 4;
        if (ts != client->lb_timestamp) cs2d_client_leaderboard_reset(client);
        client->lb_timestamp = ts;
    } else if (sub == 1) {
        if (p + 8 > len) return false;
        int rank = buf[p] | (buf[p + 1] << 8);
        int32_t ts = (int32_t)(buf[p + 2] | (buf[p + 3] << 8) | (buf[p + 4] << 16) | ((uint32_t)buf[p + 5] << 24));
        int nlen = buf[p + 6] | (buf[p + 7] << 8);
        p += 8;
        if (p + nlen + 1 + 8 + 24 > len) return false;
        if (ts != client->lb_timestamp) cs2d_client_leaderboard_reset(client);
        client->lb_timestamp = ts;
        cs2d_lb_row_t row = { .have = true };
        latin1_to_utf8(&buf[p], nlen, row.name, sizeof(row.name));
        p += nlen;
        row.id_type = buf[p++];
        uint64_t id = 0;
        for (int i = 7; i >= 0; i--) id = (id << 8) | buf[p + i];
        row.id = (int64_t)id;
        p += 8;
        int32_t v[6];
        for (int i = 0; i < 6; i++, p += 4) {
            v[i] = (int32_t)(buf[p] | (buf[p + 1] << 8) | (buf[p + 2] << 16) | ((uint32_t)buf[p + 3] << 24));
        }
        row.score = v[0]; row.kills = v[1]; row.deaths = v[2]; row.assists = v[3]; row.mvp = v[4];
        row.time_played = v[5];
        if (rank >= 1 && rank <= CS2D_LEADERBOARD_MAX) client->lb_rows[rank] = row;
    } else {
        return false;
    }
    *pos = p;
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
        net_log_datagram("in ", buffer, recvd);

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
            /*
             * The server resends a reliable packet until it sees the ACK: handle each sequence number once
             * (a repeated file block would corrupt the download). The bit half the sequence space ahead is
             * cleared so the numbers can wrap around.
             */
            uint16_t ahead = (uint16_t)(srv_seq + 32768);
            client->net_seen_seq[ahead >> 3] &= (uint8_t)~(1u << (ahead & 7));
            if (client->net_seen_seq[srv_seq >> 3] & (1u << (srv_seq & 7))) {
                recvd = recvfrom(client->net_sock, (char *)buffer, sizeof(buffer), 0, (struct sockaddr *)&from, &fromlen);
                continue;
            }
            client->net_seen_seq[srv_seq >> 3] |= (uint8_t)(1u << (srv_seq & 7));
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
                        client->net_password,
                        client->net_raw_secret,
                        client->config.window_width,
                        client->config.window_height,
                        client->xfer.map_mode
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

                        /* Clear offline bots and bind local player to server-assigned ID as Spectator until team is chosen */
                        for (int i = 1; i <= CS2D_MAX_PLAYERS; i++) {
                            memset(&client->game.players[i], 0, sizeof(cs2d_player_t));
                        }
                        client->local_player_id = (pid > 0 && pid <= CS2D_MAX_PLAYERS) ? pid : 1;
                        client->game.score_t = client->game.score_ct = 0;
                        client->net_ticks = -1;
                        cs2d_game_add_player(&client->game, client->local_player_id, client->config.name, CS2D_TEAM_SPEC);
                        client->game.players[client->local_player_id].alive = false;
                        cs2d_spec_init(&client->spec, 1); /* sv_specmode default until [252,6] */

                        /* FUN_1402c9f40: checksum of the local copy ("" when missing, the server then sends it) */
                        join_map_checksum(client);
                        cs2d_pkt_t p3;
                        cs2d_net_build_join_stage2(&p3, client->net_map_checksum, client->net_raw_secret,
                                                   client->xfer.accept);
                        cs2d_client_send_udp(client, p3.buf, (size_t)p3.pos, true);
                        client->join_stage = 2;
                    } else if (status == 1) {
                        /* Missing or wrong password: Tgui_joinmen.pwprompt (0x140055e40) via vtable +0xa0 in the
                         * join handler (0x1402cf1c1 area); no [253] is sent, the prompt restarts the join */
                        client->join_stage      = 0;
                        client->join_menu       = 1;
                        client->join_pw_text[0] = '\0';
                        client->join_pw_cursor  = 0;
                        client->ui.focused_id   = CS2D_UI_ID("JoinPwInput"); /* pwprompt focuses the input */
                    } else {
                        cs2d_client_console_print(client, 255, 0, 0, "Failed to join the server (join status %u)", status);
                        join_error_prompt(client, status);
                    }
                    break;
                } else if (sub == 4) {
                    /* Stage 2 reply: [252, 4, status] -> Send [252, 5] */
                    if (pos >= recvd) break;
                    uint8_t status = buffer[pos++];
                    if (status == 13) {
                        /* The server has files for us: [252,50] follows (Tgui_joinmen.mapdownload) */
                        client->download_open = true;
                        cs2d_xfer_reset(&client->xfer);
                    } else if (status != 0) {
                        cs2d_client_console_print(client, 255, 0, 0, "Failed to join the server (map status %u)", status);
                        join_error_prompt(client, status);
                    } else {
                        client->download_open = false;
                        /* Play on the server's map (maps/temp.map in temporary mode), not the one loaded before */
                        join_load_map(client, (client->xfer.map_mode == 1) ? "temp" : client->net_map_name);
                        join_map_checksum(client);
                        cs2d_pkt_t p5;
                        cs2d_net_build_join_stage3(&p5, client->net_map_name, client->net_map_checksum);
                        cs2d_client_send_udp(client, p5.buf, (size_t)p5.pos, true);
                        client->join_stage = 3;
                    }
                    break;
                } else if (sub == 6) {
                    /* Stage 3 reply: [252, 6, status, RStr(map_name), RStr(server_name), ...] */
                    if (pos >= recvd) break;
                    uint8_t status = buffer[pos++];
                    if (status == 0) {
                        char srv_map[128] = {0};
                        cs2d_pkt_r_str(buffer, recvd, &pos, srv_map, sizeof(srv_map));
                        cs2d_pkt_r_str(buffer, recvd, &pos, client->net_server_name, sizeof(client->net_server_name));
                        if (client->net_server_name[0] == '\0') {
                            snprintf(client->net_server_name, sizeof(client->net_server_name), "CS2D Server");
                        }
                        /*
                         * Game settings (FUN_1402ce7c0 at 0x1402d0560, varied on the dedicated): u16, u16, roundtime,
                         * freezetime, c4timer, time i32, u8, u8, then when more than 5 bytes remain maxplayers, sv_fow,
                         * sv_specmode, sv_gamemode, respawn delay, ...
                         */
                        /* T and CT team scores (DAT_140ddf450 / DAT_140ddf44c, 0x1402d0560) */
                        if (pos + 4 <= recvd) {
                            client->game.score_t  = buffer[pos] | (buffer[pos + 1] << 8);
                            client->game.score_ct = buffer[pos + 2] | (buffer[pos + 3] << 8);
                        }
                        int s = pos + 2 + 2 + 3 + 4 + 2;
                        if (recvd - s > 5) {
                            client->net_maxplayers   = buffer[s];
                            client->net_sv_fow       = buffer[s + 1];
                            client->spec.server_mode = buffer[s + 2];
                            client->net_sv_gamemode  = buffer[s + 3];
                            client->net_respawn_delay = buffer[s + 4];
                            /* Then infammo, u8, two strings, u8, u8, u8 and mp_hovertext (0x140a14e00), at s + 12 + both
                             * string lengths; 31 with the dedicated's default config (proxy) */
                            if (s + 5 < recvd) client->net_infammo = buffer[s + 5] != 0;
                            if (s + 7 < recvd) {
                                int h = s + 8 + buffer[s + 7];
                                if (h < recvd) h += 1 + buffer[h];
                                if (h + 3 < recvd) client->net_hovertext = buffer[h + 3];
                                if (h + 4 < recvd) client->net_ff = buffer[h + 4] != 0;
                                /* Then a u8, the background image (str), its scroll x / y and a header i32, sv_forcelight
                                 * u8 and sv_daylighttime + 1 as u16 (0x1402d1144; proxy: 0 = the map's own time) */
                                int b = h + 5;
                                if (b < recvd) b += 1 + buffer[b];
                                if (b + 15 <= recvd) {
                                    client->net_forcelight = buffer[b + 12] != 0;
                                    client->net_daylight = (buffer[b + 13] | (buffer[b + 14] << 8)) - 1;
                                    apply_light_config(client);
                                    /* buffer[b + 15] is setting 21 (0x140a14df0); buffer[b + 16] is mp_hudscale (1402d0000.c:1169) */
                                    if (b + 17 <= recvd) {
                                        client->net_hudscale = buffer[b + 16];
                                    }
                                    /* Shown by the ingame menu (Tgui_hudmen.draw menu 2): b + 15 recoil (0x140a14df0),
                                     * b + 17 sv_voicechat (0x140a14ed8), b + 18 OS, b + 19 lag compensation, b + 20 Lua,
                                     * b + 21 ticks (DAT_140ddf13c), then n u16 Lua binds, seed i32, off-screen damage
                                     * (0x140a14de0), leaderboard (0x140a14dd8), physics (0x140a14df8) (0x1402d1144..) */
                                    if (b + 23 <= recvd) {
                                        client->net_recoil    = buffer[b + 15] == 1;
                                        client->net_voicechat = buffer[b + 17];
                                        client->net_os        = buffer[b + 18];
                                        client->net_lagcomp   = buffer[b + 19];
                                        client->net_lua       = buffer[b + 20] != 0;
                                        client->net_lag_ticks = buffer[b + 21];
                                        int t = b + 23 + 2 * buffer[b + 22] + 4;
                                        if (t + 3 <= recvd) {
                                            client->net_offscreen   = buffer[t] == 1;
                                            client->net_leaderboard = buffer[t + 1] != 0;
                                            client->net_physics     = buffer[t + 2] == 1;
                                        }
                                    }
                                }
                            }
                        }
                        client->join_stage = 4;
                    }
                    break;
                } else if (sub == 50) {
                    /* File transfer [252,50,sub,...]: the module reads the whole message */
                    join_transfer(client, buffer, recvd, &pos);
                } else if (sub == 7) {
                    /* Stage 4 snapshot chunks: [252, 7, snap_type, ...] */
                    if (pos >= recvd) break;
                    uint8_t snap_type = buffer[pos++];
                    if (snap_type == 1) {
                        join_players(client, buffer, recvd, &pos);
                    } else if (snap_type == 2) {
                        /* Hostages (FUN_1402ce7c0, "receiving hostage data"): replace the map spawns */
                        cs2d_hostages_read_snapshot(&client->hostages, buffer, recvd, &pos);
                    } else if (snap_type == 3) {
                        /* Items on the ground (FUN_1402ce7c0 case 3): count u8, then {id u16, type, tile x u16,
                         * tile y u16, clip u16, reserve u16} (11 bytes) */
                        if (pos < recvd) {
                            int count = buffer[pos++];
                            for (int k = 0; k < count && pos + 11 <= recvd; k++, pos += 11) {
                                const uint8_t *r = &buffer[pos];
                                add_ground_item(client, (uint16_t)(r[0] | (r[1] << 8)), r[2], r[3] | (r[4] << 8),
                                                r[5] | (r[6] << 8), (int16_t)(r[7] | (r[8] << 8)),
                                                (int16_t)(r[9] | (r[10] << 8)));
                            }
                        }
                    } else if (snap_type == 5) {
                        /* Dynamic objects (FUN_1402ce7c0 case 5, "receiving dynamic object data") */
                        cs2d_dynobj_read_snapshot(&client->dynobj, buffer, recvd, &pos);
                    } else if (snap_type == 4) {
                        /* Entity states: [count u8] then {x u16, y u16, state u8}; only non-zero states are sent */
                        if (!client->snapshot_states_reset) {
                            for (uint32_t i = 0; i < client->game.map.entity_count; i++) {
                                cs2d_ents_set_state(&client->ents, (int)i, 0);
                            }
                            client->snapshot_states_reset = true;
                        }
                        if (pos < recvd) {
                            int count = buffer[pos++];
                            for (int k = 0; k < count && pos + 5 <= recvd; k++, pos += 5) {
                                int sx = buffer[pos] | (buffer[pos + 1] << 8);
                                int sy = buffer[pos + 2] | (buffer[pos + 3] << 8);
                                cs2d_scene_game_apply_entity_state(client, sx, sy, buffer[pos + 4]);
                            }
                        }
                        /* Joining shows the map as it is: no break effects for what broke earlier */
                        client->ents.event_count = 0;
                    } else if (snap_type == 6) {
                        /* Ground projectiles (FUN_1402ce7c0 case 6): count u8, then {id u8, typ u8, player u8, x u16, y u16} (7 bytes) */
                        if (pos < recvd) {
                            int count = buffer[pos++];
                            for (int k = 0; k < count && pos + 7 <= recvd; k++, pos += 7) {
                                uint8_t id = buffer[pos];
                                uint8_t typ = buffer[pos + 1];
                                uint8_t player = buffer[pos + 2];
                                uint16_t rx = (uint16_t)(buffer[pos + 3] | (buffer[pos + 4] << 8));
                                uint16_t ry = (uint16_t)(buffer[pos + 5] | (buffer[pos + 6] << 8));
                                float speed, lifetime, def_fly;
                                cs2d_projectile_get_default_properties(typ, &speed, &lifetime, &def_fly);
                                cs2d_projectile_t *p = cs2d_projectile_spawn_with_id(&client->projectiles, id, player, typ,
                                                                                     (float)rx, (float)ry, 0.0f, 0.0f, 0.0f, lifetime);
                                if (p) {
                                    p->state = CS2D_PROJ_STATE_GROUND;
                                }
                            }
                        }
                    } else if (snap_type == 9) {
                        /* Map cycle for the vote screen (FUN_1402ce7c0 case 9): n = 0xff then u16 size; n = 0 done;
                         * else n * {index u16, name (u8 len)} into 0x140a0fd98 */
                        if (pos < recvd) {
                            int n = buffer[pos++];
                            if (n == 0xff) {
                                if (pos + 2 <= recvd) {
                                    int size = buffer[pos] | (buffer[pos + 1] << 8);
                                    client->vote_map_count = size < CS2D_VOTE_MAX_MAPS ? size : CS2D_VOTE_MAX_MAPS;
                                    for (int k = 0; k < client->vote_map_count; k++) client->vote_maps[k][0] = '\0';
                                }
                            } else {
                                for (int k = 0; k < n && pos + 2 <= recvd; k++) {
                                    int idx = buffer[pos] | (buffer[pos + 1] << 8);
                                    pos += 2;
                                    char name[64];
                                    cs2d_pkt_r_str(buffer, recvd, &pos, name, sizeof(name));
                                    if (idx < client->vote_map_count) {
                                        snprintf(client->vote_maps[idx], sizeof(client->vote_maps[idx]), "%s", name);
                                    }
                                }
                            }
                        }
                    } else if (snap_type == 200) {
                        client->join_stage     = 5;
                        client->connected      = true;
                        client->team_menu_open = true;
                        client->team_menu_page = 0;
                        printf("[CS2D Net] Connected to server '%s' (map='%s', player_id=%u)!\n",
                               client->net_server_name[0] ? client->net_server_name : "CS2D Server",
                               client->net_map_name, client->local_player_id);
                        cs2d_client_console_print(client, 100, 255, 100,
                                                  "Connected to server (map: %s, player ID: %u)",
                                                  client->net_map_name, client->local_player_id);

                        /*
                         * Client data, as CS2D.exe sends it right after the snapshot: [252, 10, 28, 2,
                         * size u16, raw size u32, zlib] carrying the 32x32 spray logo (1024 bytes, blank
                         * here). Only after this does the server run its "join" (Lua hook, player list
                         * entry and the [248] announcement to the other players). The [67, player] that
                         * followed it in earlier captures is the spectator camera picking its first
                         * target: the game scene sends it on the next frame, and not at all when nobody
                         * is alive (proxy, 140.238.183.194:36969).
                         */
                        static const uint8_t client_data[] = {
                            0xfc, 0x0a, 0x1c, 0x02, 0x15, 0x00, 0x00, 0x04, 0x00, 0x00,
                            0x78, 0xda, 0x63, 0x60, 0x18, 0x05, 0xa3, 0x60, 0x14, 0x8c, 0x54, 0x00, 0x00, 0x04, 0x00, 0x00, 0x01
                        };
                        cs2d_client_send_udp(client, client_data, sizeof(client_data), true);

                        /* The team menu opens with the spawn showcase (0x140190000: DAT_140ddf328 = 1,
                         * DAT_140ddf324 = 0, DAT_140ddf320 = Rand(1, 2)) */
                        client->spec.idle_cam   = true;
                        client->spec.idle_ticks = 0;
                        client->spec.idle_team  = 1 + rand() % 2;
                        client->spec_tick       = 1.0f;
                    }
                    break;
                } else {
                    break;
                }
            } else if (opcode == CS2D_NET_TEAM) {
                /* Server -> Client: [20, pid, team, look] (4 bytes; ded FUN_1401cd140) */
                if (pos + 4 > recvd) break;
                uint8_t pid  = buffer[pos + 1];
                uint8_t team = buffer[pos + 2];
                uint8_t look = buffer[pos + 3];
                if (pid > 0 && pid <= CS2D_MAX_PLAYERS) {
                    cs2d_player_t *p = &client->game.players[pid];
                    p->id = pid;
                    if (team <= CS2D_TEAM_VIP) {
                        p->team = (cs2d_team_t)team;
                        if (team == CS2D_TEAM_SPEC) {
                            p->alive  = false;
                            p->health = 0;
                        }
                    }
                    if (look <= 3) {
                        p->look = look;
                    }
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
                        /* The spawn record lists the whole inventory (prison_funst3r: only the knife) */
                        cs2d_player_clear_inventory(pl);
                        for (uint8_t k = 0; k < item_cnt && k < 32; k++) {
                            cs2d_player_give_weapon(pl, items[k]);
                        }
                        if (!cs2d_player_select_weapon(pl, wep)) cs2d_player_select_weapon(pl, CS2D_WEP_KNIFE);
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
                            if (client->game.players[victim].alive && victim == client->local_player_id) {
                                client->death_ms = GetTickCount();
                            }
                            client->game.players[victim].alive = false;
                        }
                    }
                }
                pos += 5;
            } else if (opcode == CS2D_NET_ROUNDSTART) {
                /* Server -> Client: [22, round u8, count u8] + count x 12-byte player records
                 * {id u8, x u16, y u16, angle f32, weapon u8, armor u8, flag u8} + the receiving player's
                 * inventory {money u16, n u8, n x item u8} (recorded with 1 and 2 players). Entity states
                 * are not resent: every client rebuilds the round start state itself (Trigger_Start is
                 * deterministic). */
                if (pos + 3 > recvd) break;
                int count = buffer[pos + 2];
                int tail = pos + 3 + count * 12;
                if (tail + 3 <= recvd) {
                    /* The first byte is the round end reason (FUN_1402520b0): team wins or a score reset */
                    cs2d_game_apply_round_reason(&client->game, buffer[pos + 1]);
                }
                if (tail + 3 > recvd) break;
                int n_items = buffer[tail + 2];
                if (tail + 3 + n_items > recvd) break;
                for (int k = 0; k < count; k++) {
                    const uint8_t *r = &buffer[pos + 3 + k * 12];
                    uint8_t pid = r[0];
                    if (pid > 0 && pid <= CS2D_MAX_PLAYERS) {
                        float angle;
                        memcpy(&angle, &r[5], 4);
                        cs2d_player_t *p = &client->game.players[pid];
                        p->pos.x  = (float)(r[1] | (r[2] << 8));
                        p->pos.y  = (float)(r[3] | (r[4] << 8));
                        p->angle  = angle;
                        p->armor  = r[10];
                        p->alive  = true;
                        p->health = 100;
                        p->has_flag = false; /* Tpl reset at round start; captures clear it too (FUN_140199580) */
                        if (pid != client->local_player_id) {
                            cs2d_player_give_weapon(p, r[9]);
                        }
                        /* r[11] = Tpl.setspecialbyte (0x1401ffdc0): 1 bomb (T) / defuse kit (CT), 2 night vision,
                         * 8 flag, 16 gas mask (4 VIP and 32 flashlight not kept by this client) */
                        p->has_defuse_kit  = (r[11] & 1) && p->team == CS2D_TEAM_CT;
                        p->has_nightvision = (r[11] & 2) != 0;
                        p->has_flag        = (r[11] & 8) != 0;
                        p->has_gasmask     = (r[11] & 16) != 0;
                        p->is_planting = p->is_defusing = false;
                    }
                }
                cs2d_player_t *me = &client->game.players[client->local_player_id];
                const uint8_t *held = NULL;
                for (int k = 0; k < count; k++) {
                    if (buffer[pos + 3 + k * 12] == client->local_player_id) held = &buffer[pos + 3 + k * 12 + 9];
                }
                me->money = buffer[tail] | (buffer[tail + 1] << 8);
                cs2d_player_clear_inventory(me);
                for (int k = 0; k < n_items; k++) cs2d_player_give_weapon(me, buffer[tail + 3 + k]);
                for (int k = 0; k < count; k++) {
                    /* setspecialbyte bit 1 on a T: Titem.give(id, 55) - the bomb is not in the inventory list */
                    const uint8_t *r = &buffer[pos + 3 + k * 12];
                    if (r[0] > 0 && r[0] <= CS2D_MAX_PLAYERS && (r[11] & 1) && client->game.players[r[0]].team == CS2D_TEAM_T) {
                        cs2d_player_give_weapon(&client->game.players[r[0]], CS2D_WEP_BOMB);
                    }
                }
                if (!held || !cs2d_player_select_weapon(me, *held)) cs2d_player_select_weapon(me, CS2D_WEP_KNIFE);

                cs2d_ents_round_start(&client->ents);
                cs2d_fx_clear(&client->fx);
                cs2d_dynobj_clear(&client->dynobj); /* FUN_1402520b0: Tdo.freeall; the server respawns them ([49]) */
                /* FUN_1402520b0: bomb timers cleared, Thostage.freeall + one per Info_Hostage when
                 * 0x140a14e48[1] == 0 (taken as sv_gamemode 0, inferred: the same flag gates achievements) */
                client->game.bomb_planted = false;
                if (client->net_sv_gamemode == 0) cs2d_hostages_spawn_from_map(&client->hostages, &client->game.map);
                else cs2d_hostages_clear(&client->hostages);
                client->fire_seed   = -1;
                client->walk_active = false;
                pos = tail + 3 + n_items;
            } else if (opcode == CS2D_NET_SETPOS) {
                /* Server -> Client: [15, player u8, x u16, y u16] (6 bytes): the server moves a player,
                 * e.g. Func_Teleport. The local player takes it too, otherwise it would keep sending
                 * its old position (rubberbanding). Recorded from CS2D.exe sessions. */
                if (pos + 6 > recvd) break;
                uint8_t pid = buffer[pos + 1];
                if (pid > 0 && pid <= CS2D_MAX_PLAYERS) {
                    cs2d_player_t *p = &client->game.players[pid];
                    p->pos.x = (float)(buffer[pos + 2] | (buffer[pos + 3] << 8));
                    p->pos.y = (float)(buffer[pos + 4] | (buffer[pos + 5] << 8));
                    if (pid == client->local_player_id) {
                        client->walk_active = false;
                        client->net_sent_x  = (int)p->pos.x; /* The server already knows this position */
                        client->net_sent_y  = (int)p->pos.y;
                    }
                }
                pos += 6;
            } else if (opcode == CS2D_NET_COLLECT) {
                /* Server -> Client: [25, player, item id u16, type u8, clip u16, reserve u16, u8] (10 bytes):
                 * the player picked the ground item up (recorded from CS2D.exe sessions) */
                if (pos + 10 > recvd) break;
                uint8_t  pid  = buffer[pos + 1];
                uint16_t iid  = (uint16_t)(buffer[pos + 2] | (buffer[pos + 3] << 8));
                uint8_t  type = buffer[pos + 4];
                int16_t  clip = (int16_t)(buffer[pos + 5] | (buffer[pos + 6] << 8));
                int16_t  res  = (int16_t)(buffer[pos + 7] | (buffer[pos + 8] << 8));
                bool taken = true;
                if (pid > 0 && pid <= CS2D_MAX_PLAYERS) {
                    if (type == CS2D_WEP_REDFLAG || type == CS2D_WEP_BLUEFLAG) collect_flag(client, pid, type);
                    else taken = cs2d_player_add_collected(&client->game.players[pid], type, clip, res);
                }
                if (taken) remove_ground_item(client, iid); /* A partly taken grenade stack stays (FUN_14020ce60) */
                pos += 10;
            } else if (opcode == CS2D_NET_USE) {
                /* Server -> Client: [26, player, mode, data i32, x u16, y u16] (11 bytes, FUN_1402d8f00 ->
                 * FUN_14014f820 with param_6 = 0): the server's echo of an accepted use. Mode 1 / 2 = hostage
                 * (data = hostage id, x = its "used" flag before), 3 = defuse start (data = defuse kit),
                 * 100 = Trigger_Use (nothing on the client), 150 = building mode */
                if (pos + 11 > recvd) break;
                uint8_t pid  = buffer[pos + 1];
                uint8_t mode = buffer[pos + 2];
                int32_t data;
                memcpy(&data, &buffer[pos + 3], 4);
                int ux = buffer[pos + 7] | (buffer[pos + 8] << 8);
                if (pid > 0 && pid <= CS2D_MAX_PLAYERS) {
                    cs2d_player_t *p = &client->game.players[pid];
                    if (mode == 1 || mode == 2) {
                        cs2d_hostage_t *h = cs2d_hostages_get(&client->hostages, data);
                        if (h && h->health > 0) {
                            h->used = ux != 0;
                            cs2d_hostages_use(&client->hostages, data, p, pid, mode == 2);
                        }
                    } else if (mode == 3) {
                        /* proc 2 (no kit, 10 s) / 3 (kit, 5 s), proctimer reset (+ sfx/c4/... not played) */
                        p->is_defusing     = true;
                        p->defuse_progress = 0.0f;
                        p->has_defuse_kit  = data != 0;
                    }
                }
                pos += 11;
            } else if (opcode == CS2D_NET_HOSTAGE) {
                /* Server -> Client: [29, hostage, kind, a u16, b u16] (7 bytes, FUN_1402d93f0): 0 = target tile
                 * (a, b), 2 = rescued by player b (+$1000), 3 = hurt to health a by player b (-$150),
                 * 4 = killed by player b (-$1500) */
                if (pos + 7 > recvd) break;
                int a = buffer[pos + 3] | (buffer[pos + 4] << 8);
                int b = buffer[pos + 5] | (buffer[pos + 6] << 8);
                int kind = cs2d_hostages_net_message(&client->hostages, buffer[pos + 1], buffer[pos + 2], a, b);
                int delta = kind == 2 ? 1000 : kind == 3 ? -150 : kind == 4 ? -1500 : 0;
                if (delta && b > 0 && b <= CS2D_MAX_PLAYERS) {
                    cs2d_player_t *p = &client->game.players[b];
                    p->money += delta;
                    if (p->money > 16000) p->money = 16000;
                    if (p->money < 0) p->money = 0;
                }
                pos += 7;
            } else if (opcode == CS2D_NET_PROCESS) {
                /* Server -> Client: [30, player, mode] (FUN_1402d9980): 1 / 2 = plant start / abort, 3 = planted
                 * [+ tile x u16, tile y u16, item id u16, c4 timer u8] (FUN_1401cb510), 4 / 5 = defuse abort / done,
                 * 6 = bomb exploded (FUN_1401f5f60) */
                if (pos + 3 > recvd) break;
                uint8_t pid  = buffer[pos + 1];
                uint8_t mode = buffer[pos + 2];
                cs2d_player_t *p = (pid > 0 && pid <= CS2D_MAX_PLAYERS) ? &client->game.players[pid] : NULL;
                if (mode == 3) {
                    if (pos + 10 > recvd) break;
                    int bx = buffer[pos + 3] | (buffer[pos + 4] << 8);
                    int by = buffer[pos + 5] | (buffer[pos + 6] << 8);
                    add_ground_item(client, (uint16_t)(buffer[pos + 7] | (buffer[pos + 8] << 8)), CS2D_WEP_PLANTEDBOMB,
                                    bx, by, 0, 0);
                    client->game.bomb_planted = true;
                    client->game.c4_time      = (float)buffer[pos + 9];
                    client->game.bomb_pos.x   = (float)(bx * CS2D_TILE_SIZE + 16);
                    client->game.bomb_pos.y   = (float)(by * CS2D_TILE_SIZE + 16);
                    if (p) {
                        p->is_planting = false;
                        cs2d_player_strip_weapon(p, CS2D_WEP_BOMB); /* Titem.freeatplayer(id, 55) */
                    }
                    cs2d_client_center_msg(client, 255, 220, 0, "Bomb has been planted!");
                    pos += 10;
                } else {
                    if (p && (mode == 1 || mode == 2)) {
                        p->is_planting    = mode == 1;
                        p->plant_progress = 0.0f;
                    } else if (p && (mode == 4 || mode == 5)) {
                        p->is_defusing     = false;
                        p->defuse_progress = 0.0f;
                    }
                    if (mode == 5 || mode == 6) {
                        /* Defused: bomb timer stops; exploded: the planted item goes with a blast */
                        client->game.bomb_planted = false;
                        for (int i = 1; i <= CS2D_MAX_PLAYERS; i++) client->game.players[i].is_defusing = false;
                    }
                    if (mode == 6) {
                        for (int i = 0; i < CS2D_MAX_GROUND_ITEMS; i++) {
                            cs2d_ground_item_t *it = &client->ground_items[i];
                            if (!it->active || it->type != CS2D_WEP_PLANTEDBOMB) continue;
                            /* FUN_1401f1130 + Tp 9 / 14 / 22 / 21 particles; approximated by the generic blast */
                            cs2d_fx_explosion(&client->fx, it->x, it->y, 0.0f);
                            it->active = false;
                        }
                    }
                    pos += 3;
                }
            } else if (opcode == CS2D_NET_SPAWNITEM) {
                /* Server -> Client: [33, type u8, item id u16, tile x u16, tile y u16, clip u16, reserve u16] (12 bytes) */
                if (pos + 12 > recvd) break;
                add_ground_item(client, (uint16_t)(buffer[pos + 2] | (buffer[pos + 3] << 8)), buffer[pos + 1],
                                buffer[pos + 4] | (buffer[pos + 5] << 8), buffer[pos + 6] | (buffer[pos + 7] << 8),
                                (int16_t)(buffer[pos + 8] | (buffer[pos + 9] << 8)),
                                (int16_t)(buffer[pos + 10] | (buffer[pos + 11] << 8)));
                pos += 12;
            } else if (opcode == CS2D_NET_DROP) {
                /* Server -> Client: [24, player, type, clip u16, reserve u16, u8, item id u16, tile x u16,
                 * tile y u16, u8] (15 bytes): the weapon leaves the player and lies on that tile */
                if (pos + 15 > recvd) break;
                uint8_t pid  = buffer[pos + 1];
                uint8_t type = buffer[pos + 2];
                int16_t clip = (int16_t)(buffer[pos + 3] | (buffer[pos + 4] << 8));
                int16_t res  = (int16_t)(buffer[pos + 5] | (buffer[pos + 6] << 8));
                if (pid > 0 && pid <= CS2D_MAX_PLAYERS) {
                    if (type == CS2D_WEP_REDFLAG || type == CS2D_WEP_BLUEFLAG) drop_flag(client, pid, type);
                    else cs2d_player_strip_weapon(&client->game.players[pid], type);
                }
                add_ground_item(client, (uint16_t)(buffer[pos + 8] | (buffer[pos + 9] << 8)), type,
                                buffer[pos + 10] | (buffer[pos + 11] << 8), buffer[pos + 12] | (buffer[pos + 13] << 8),
                                clip, res);
                pos += 15;
            } else if (opcode == CS2D_NET_SELECT_WEAPON) {
                /* Server -> Client: [9, player, type, mode] (4 bytes): weapon held (e.g. Lua setweapon) */
                if (pos + 4 > recvd) break;
                uint8_t pid = buffer[pos + 1];
                if (pid > 0 && pid <= CS2D_MAX_PLAYERS) {
                    cs2d_player_t *p = &client->game.players[pid];
                    if (!cs2d_player_select_weapon(p, buffer[pos + 2]) && pid != client->local_player_id) {
                        /* Other players' inventories are not sent: show what they hold */
                        cs2d_player_give_weapon(p, buffer[pos + 2]);
                    }
                }
                pos += 4;
            } else if (opcode == CS2D_NET_RELOAD) {
                /* Server -> Client: [16, player, 1 = reload started | 2 = reload finished] (3 bytes) */
                if (pos + 3 > recvd) break;
                uint8_t pid = buffer[pos + 1];
                if (pid > 0 && pid <= CS2D_MAX_PLAYERS) {
                    cs2d_player_t *p = &client->game.players[pid];
                    if (buffer[pos + 2] == 2) {
                        p->reload_timer = 0.0f;
                        cs2d_weapon_finish_reload(p);
                    } else if (buffer[pos + 2] == 1 && p->reload_timer <= 0.0f && pid != client->local_player_id) {
                        cs2d_weapon_reload(p);
                    }
                }
                pos += 3;
            } else if (opcode == CS2D_NET_SETAMMO) {
                /* Server -> Client: [61, player, type, clip u16, reserve u16] (7 bytes) */
                if (pos + 7 > recvd) break;
                uint8_t pid = buffer[pos + 1], type = buffer[pos + 2];
                if (pid > 0 && pid <= CS2D_MAX_PLAYERS) {
                    client->game.players[pid].clip_ammo[type]    = (int16_t)(buffer[pos + 3] | (buffer[pos + 4] << 8));
                    client->game.players[pid].reserve_ammo[type] = (int16_t)(buffer[pos + 5] | (buffer[pos + 6] << 8));
                }
                pos += 7;
            } else if (opcode == CS2D_NET_BUY) {
                /* Server -> Client: [23, player, type (250 = refused), money u16, u8] (6 bytes) */
                if (pos + 6 > recvd) break;
                uint8_t pid = buffer[pos + 1], type = buffer[pos + 2];
                if (pid > 0 && pid <= CS2D_MAX_PLAYERS) {
                    cs2d_player_t *p = &client->game.players[pid];
                    p->money = buffer[pos + 3] | (buffer[pos + 4] << 8);
                    if (type != 250) {
                        const cs2d_weapon_def_t *w = cs2d_weapon_get(type);
                        /* Bought armor arrives as playerdata (armor); weapons go into their slot */
                        if (w && w->slot != CS2D_SLOT_ITEM) cs2d_player_add_bought(p, type);
                        else if (type == CS2D_WEP_DEFUSEKIT) p->has_defuse_kit = true;
                        else if (type == CS2D_WEP_KEVLARHELM) p->has_helmet = true;
                    } else if (pid == client->local_player_id) {
                        cs2d_client_console_print(client, 255, 45, 45, "You can not buy this now");
                    }
                }
                pos += 6;
            } else if (opcode == CS2D_NET_PLAYERDATA) {
                /* Server -> Client: [42, player, field, value u16] (5 bytes). Fields recorded with Lua:
                 * 0 health, 2 money, 3 armor, 5 weapon given, 6 weapon taken, 8 max health */
                if (pos + 5 > recvd) break;
                uint8_t pid = buffer[pos + 1], field = buffer[pos + 2];
                int value = buffer[pos + 3] | (buffer[pos + 4] << 8);
                if (pid > 0 && pid <= CS2D_MAX_PLAYERS) {
                    cs2d_player_t *p = &client->game.players[pid];
                    switch (field) {
                        case 0: p->health = value; if (value <= 0) p->alive = false; break;
                        /* FUN_1401cc9d0: 1 score, 4 deaths, 9 assists, 10 MVPs; a change restarts the flash */
                        case 1: if (p->score != value) p->score_inc_time = 0.0f; p->score = value; break;
                        case 4: p->deaths = value; break;
                        case 9: if (p->assists != value) p->assists_inc_time = 0.0f; p->assists = value; break;
                        case 10: p->mvp = value; break;
                        case 2: p->money = value; break;
                        case 3: p->armor = value; break;
                        case 5: {
                            uint8_t held = p->current_weapon;
                            cs2d_player_give_weapon(p, (uint8_t)value);
                            if (held) p->current_weapon = held; /* Lua equip does not switch weapons */
                            break;
                        }
                        case 6: cs2d_player_strip_weapon(p, (uint8_t)value); break;
                        case 8: p->max_health = value; break;
                        default: break;
                    }
                }
                pos += 5;
            } else if (opcode == CS2D_NET_MSG) {
                /* Server -> Client: [226, 1, u16, text length u16, text, u8] */
                if (pos + 6 > recvd) break;
                int tlen = buffer[pos + 4] | (buffer[pos + 5] << 8);
                if (pos + 6 + tlen + 1 > recvd) break;
                char msg_buf[CS2D_CHAT_LINE_LEN];
                int cp_len = tlen < (int)sizeof(msg_buf) - 1 ? tlen : (int)sizeof(msg_buf) - 1;
                memcpy(msg_buf, &buffer[pos + 6], (size_t)cp_len);
                msg_buf[cp_len] = '\0';
                cs2d_client_handle_server_msg(client, 0, 1, 255, 220, 0, msg_buf);
                pos += 6 + tlen + 1;
            } else if (opcode == CS2D_NET_MSG2) {
                /* Server -> Client: [240, sender u8, mode u8, char_len u16, char_len * 2 bytes UTF-16LE]
                 * mode 50: "1" starts serverinfo, "2" finishes serverinfo and opens the Welcome window;
                 * mode 51: appends one line to serverinfo; mode < 50: chat message (proxy_join_info.log) */
                if (pos + 5 > recvd) break;
                uint8_t sender_id = buffer[pos + 1];
                uint8_t mode = buffer[pos + 2];
                int char_len = buffer[pos + 3] | (buffer[pos + 4] << 8);
                int byte_len = char_len * 2;
                if (pos + 5 + byte_len > recvd) break;
                const uint8_t *wbytes = &buffer[pos + 5];
                if (mode == 50) {
                    if (char_len == 1 && wbytes[0] == '1' && wbytes[1] == 0) {
                        client->server_info_line_count = 0;
                        client->server_info_scroll.top = 0;
                    } else if (char_len == 1 && wbytes[0] == '2' && wbytes[1] == 0) {
                        if (client->server_info_line_count > 0) {
                            client->server_info_open   = true;
                            client->server_info_scroll.top = 0;
                        }
                    }
                } else if (mode == 51) {
                    cs2d_client_serverinfo_add_utf16(client, wbytes, char_len);
                } else {
                    uint8_t r = 255, g = 220, b = 0;
                    char utf8[CS2D_CONSOLE_LINE_LEN];
                    utf16_to_colored_utf8(wbytes, char_len, &r, &g, &b, utf8, sizeof(utf8));
                    if (utf8[0] != '\0') {
                        cs2d_client_handle_server_msg(client, sender_id, mode, r, g, b, utf8);
                    }
                }
                pos += 5 + byte_len;
            } else if (opcode == CS2D_NET_ENTITYEVENT) {
                /* Server -> Client: [31, x u16, y u16, state u8] (6 bytes): entity at tile (x, y) changed state */
                if (pos + 6 > recvd) break;
                int ex = buffer[pos + 1] | (buffer[pos + 2] << 8);
                int ey = buffer[pos + 3] | (buffer[pos + 4] << 8);
                cs2d_scene_game_apply_entity_state(client, ex, ey, buffer[pos + 5]);
                pos += 6;
            } else if (opcode == CS2D_NET_PLAYERJOINS) {
                /* Server -> Client: [248, player, name (u8 length + text), 18 bytes]: a player joined */
                if (pos + 3 > recvd) break;
                uint8_t pid = buffer[pos + 1];
                int nlen = buffer[pos + 2];
                if (pos + 3 + nlen + 18 > recvd) break;
                if (pid > 0 && pid <= CS2D_MAX_PLAYERS && pid != client->local_player_id) {
                    char name[CS2D_MAX_NAME_LEN];
                    snprintf(name, sizeof(name), "%.*s", nlen, (const char *)&buffer[pos + 3]);
                    memset(&client->game.players[pid], 0, sizeof(cs2d_player_t));
                    cs2d_game_add_player(&client->game, pid, name, CS2D_TEAM_SPEC);
                    client->game.players[pid].alive = false;
                    cs2d_client_console_print(client, 255, 220, 0, "%s has joined the game", name);
                }
                pos += 3 + nlen + 18;
            } else if (opcode == CS2D_NET_LEAVE) {
                /* Server -> Client: [253, player, reason u8] (3 bytes): the player left (the server
                 * then drops what it carried with [24] and announces it with a [226] message) */
                if (pos + 3 > recvd) break;
                uint8_t pid = buffer[pos + 1];
                if (pid > 0 && pid <= CS2D_MAX_PLAYERS && pid != client->local_player_id) {
                    memset(&client->game.players[pid], 0, sizeof(cs2d_player_t));
                }
                pos += 3;
            } else if (opcode == CS2D_NET_SPRAY) {
                /* Server -> Client: [28, player, 2, size u16, size bytes]: relay of a player's spray logo */
                if (pos + 5 > recvd) break;
                int len = buffer[pos + 3] | (buffer[pos + 4] << 8);
                if (pos + 5 + len > recvd) break;
                pos += 5 + len;
            } else if (opcode == CS2D_NET_FIRE) {
                /* Server -> Client: [7, player, seed] (3 bytes): another player fired; draw the shot */
                if (pos + 3 > recvd) break;
                uint8_t pid = buffer[pos + 1];
                if (pid > 0 && pid <= CS2D_MAX_PLAYERS && pid != client->local_player_id) {
                    cs2d_scene_game_remote_shot(client, &client->game.players[pid]);
                }
                pos += 3;
            } else if (opcode == CS2D_NET_PROJECTILE) {
                /* Server -> Client: [27, player u8, type u8, x u16, y u16, dir u16, flydist u16] (11 bytes, FUN_1402d0000 0x1402d3340) */
                if (pos + 11 > recvd) break;
                uint8_t player = buffer[pos + 1];
                uint8_t type = buffer[pos + 2];
                uint16_t rx = (uint16_t)(buffer[pos + 3] | (buffer[pos + 4] << 8));
                uint16_t ry = (uint16_t)(buffer[pos + 5] | (buffer[pos + 6] << 8));
                uint16_t rdir = (uint16_t)(buffer[pos + 7] | (buffer[pos + 8] << 8));
                uint16_t flydist = (uint16_t)(buffer[pos + 9] | (buffer[pos + 10] << 8));
                float dir_deg = (float)rdir * (360.0f / 65536.0f);
                float speed, lifetime, def_fly;
                cs2d_projectile_get_default_properties(type, &speed, &lifetime, &def_fly);
                cs2d_projectile_spawn(&client->projectiles, player, type, (float)rx, (float)ry, dir_deg, (float)flydist, speed, lifetime);
                pos += 11;
            } else if (opcode == CS2D_NET_SPAWN_PROJECTILE) {
                /* Server -> Client: [58, player u8, type u8, id u8, x_bias u16, y_bias u16, flydist u16, dir u16] (12 bytes, FUN_1400a91d0) */
                if (pos + 12 > recvd) break;
                uint8_t player = buffer[pos + 1];
                uint8_t type = buffer[pos + 2];
                uint8_t id = buffer[pos + 3];
                uint16_t rx = (uint16_t)(buffer[pos + 4] | (buffer[pos + 5] << 8));
                uint16_t ry = (uint16_t)(buffer[pos + 6] | (buffer[pos + 7] << 8));
                uint16_t flydist = (uint16_t)(buffer[pos + 8] | (buffer[pos + 9] << 8));
                uint16_t rdir = (uint16_t)(buffer[pos + 10] | (buffer[pos + 11] << 8));
                float x = (float)((int)rx - 0x8000);
                float y = (float)((int)ry - 0x8000);
                float dir_deg = (float)rdir * (360.0f / 65536.0f);
                float speed, lifetime, def_fly;
                cs2d_projectile_get_default_properties(type, &speed, &lifetime, &def_fly);
                cs2d_projectile_spawn_with_id(&client->projectiles, id, player, type, x, y, dir_deg, (float)flydist, speed, lifetime);
                pos += 12;
            } else if (opcode == CS2D_NET_SPAWN_GND_PROJ) {
                /* Server -> Client: [52, player u8, type u8, id u16, x u16, y u16, rot u32] (13 bytes, FUN_140060ee0) */
                if (pos + 13 > recvd) break;
                uint8_t player = buffer[pos + 1];
                uint8_t type = buffer[pos + 2];
                uint16_t id = (uint16_t)(buffer[pos + 3] | (buffer[pos + 4] << 8));
                uint16_t rx = (uint16_t)(buffer[pos + 5] | (buffer[pos + 6] << 8));
                uint16_t ry = (uint16_t)(buffer[pos + 7] | (buffer[pos + 8] << 8));
                uint32_t rot;
                memcpy(&rot, buffer + pos + 9, 4);
                float speed, lifetime, def_fly;
                cs2d_projectile_get_default_properties(type, &speed, &lifetime, &def_fly);
                cs2d_projectile_t *p = cs2d_projectile_spawn_with_id(&client->projectiles, (uint8_t)id, player, type,
                                                                     (float)rx, (float)ry, 0.0f, 0.0f, 0.0f, lifetime);
                if (p) {
                    p->state = CS2D_PROJ_STATE_GROUND;
                    p->rot = (float)rot;
                }
                pos += 13;
            } else if (opcode == CS2D_NET_MOVE_PROJECTILE) {
                /* Server -> Client: [63, id u8, player u8, x_bias u16, y_bias u16] (7 bytes, FUN_14007dce0) */
                if (pos + 7 > recvd) break;
                uint8_t id = buffer[pos + 1];
                uint8_t player = buffer[pos + 2];
                uint16_t rx = (uint16_t)(buffer[pos + 3] | (buffer[pos + 4] << 8));
                uint16_t ry = (uint16_t)(buffer[pos + 5] | (buffer[pos + 6] << 8));
                cs2d_projectile_t *p = cs2d_projectile_get(&client->projectiles, id, player);
                if (p) {
                    p->x = (float)((int)rx - 0x8000);
                    p->y = (float)((int)ry - 0x8000);
                }
                pos += 7;
            } else if (opcode == CS2D_NET_KILL_PROJECTILE) {
                /* Server -> Client: [41, id u8, player u8] (3 bytes, FUN_1402daf80) */
                if (pos + 3 > recvd) break;
                uint8_t id = buffer[pos + 1];
                uint8_t player = buffer[pos + 2];
                cs2d_projectile_remove(client, id, player);
                pos += 3;
            } else if (opcode == CS2D_NET_KILL_PROJECTILE_SILENT) {
                /* Server -> Client: [55, id u8, player u8] (3 bytes, FUN_1402dd7a0) */
                if (pos + 3 > recvd) break;
                uint8_t id = buffer[pos + 1];
                uint8_t player = buffer[pos + 2];
                cs2d_projectile_kill(&client->projectiles, id, player);
                pos += 3;
            } else if (opcode == CS2D_NET_DYNOBJ_REMOVE || opcode == CS2D_NET_DYNOBJ_MODE ||
                       opcode == CS2D_NET_DYNOBJ_HEALTH || opcode == CS2D_NET_DO_SPAWN) {
                /* Dynamic objects: [35] remove, [36] mode, [37] health, [49] spawn (cs2d_dynobj.c) */
                cs2d_dynobj_fx_t dfx = { 0 };
                int n = cs2d_dynobj_net_message(&client->dynobj, buffer, recvd, pos, &dfx);
                if (n < 0) break;
                if (dfx.kind == 1) cs2d_fx_explosion(&client->fx, dfx.x, dfx.y, 32.0f);
                pos += n;
            } else if (opcode == CS2D_NET_BUILD) {
                if (!net_build_result(client, buffer, recvd, pos)) break;
                pos += 15;
            } else if (opcode == CS2D_NET_KILL) {
                /* Server -> Client: [19, victim, killer, assist, weapon, x u16, y u16] (9 bytes, FUN_1402d7d90 ->
                 * FUN_1401eaf60(victim, killer, assist, weapon, x, y): the assist byte goes to Tpl::get, the weapon
                 * byte is compared with 0xfa / 0xfc) */
                if (pos + 9 > recvd) break;
                uint8_t victim = buffer[pos + 1];
                if (victim > 0 && victim <= CS2D_MAX_PLAYERS) {
                    cs2d_player_t *v = &client->game.players[victim];
                    if (v->alive && victim == client->local_player_id) client->death_ms = GetTickCount();
                    int killer = buffer[pos + 2], assist = buffer[pos + 3], weapon = buffer[pos + 4];
                    /* FUN_1401eaf60: Thud_msg.add_kill(killer, weapon, victim, assist, -1, "") unless the weapon is
                     * 0xfa / 0xfc; the assist only shows when killer and victim are T against CT. Read the teams
                     * before cs2d_game_apply_kill */
                    if ((weapon - 0xfa) & ~2) {
                        const cs2d_player_t *k = (killer > 0 && killer <= CS2D_MAX_PLAYERS)
                                               ? &client->game.players[killer] : NULL;
                        bool enemies = k && k->id && ((v->team == CS2D_TEAM_T && k->team >= CS2D_TEAM_CT) ||
                                                      (v->team >= CS2D_TEAM_CT && k->team == CS2D_TEAM_T));
                        cs2d_client_add_kill(client, killer, weapon, victim, enemies ? assist : 0);
                    }
                    cs2d_game_apply_kill(&client->game, victim, killer, assist, weapon, client->net_sv_gamemode != 1);
                }
                pos += 9;
            } else if (opcode == CS2D_NET_SETTEAMSCORES) {
                /* [64, t u16, ct u16] (FUN_1402de510, "recv 64 setteamscores") */
                if (pos + 5 > recvd) break;
                client->game.score_t  = buffer[pos + 1] | (buffer[pos + 2] << 8);
                client->game.score_ct = buffer[pos + 3] | (buffer[pos + 4] << 8);
                pos += 5;
            } else if (opcode == CS2D_NET_POSITIONALSOUND) {
                /* [65, x u16 + 32768, y u16 + 32768, len, path] (FUN_1402de5f0 -> FUN_1401e3890(x, y, path, 0)).
                 * Lua servers send it with sfx/player/build.wav right before a spawnobject [49] in the same
                 * datagram (proxy, 157.90.170.75:10003). No audio yet: the message is only skipped */
                if (pos + 6 > recvd || pos + 6 + buffer[pos + 5] > recvd) break;
                pos += 6 + buffer[pos + 5];
            } else if (opcode == CS2D_NET_PINGLIST) {
                /* [247, n, ticks u8, u16, u16, n x {pid, ping u16}] (FUN_140060f80): server ticks (DAT_140ddf3e0) and
                 * every player's latency; the two u16 (x 8) go to DAT_140ddf3ec / DAT_140ddf3e8 */
                if (pos + 7 > recvd) break;
                int n = buffer[pos + 1];
                if (pos + 7 + n * 3 > recvd) break;
                client->net_ticks = buffer[pos + 2];
                for (int k = 0; k < n; k++) {
                    const uint8_t *r = &buffer[pos + 7 + k * 3];
                    if (r[0] >= 1 && r[0] <= CS2D_MAX_PLAYERS) client->game.players[r[0]].ping = r[1] | (r[2] << 8);
                }
                pos += 7 + n * 3;
            } else if (opcode == CS2D_NET_CHANGESETTINGS) {
                /* [237, setting, value] (FUN_1401e6c10 -> FUN_1401e3f10): settings 1, 11 and 12 carry a string
                 * (u8 length + text), 2..10 and 13..24 an i32; 3 is sv_specmode */
                if (pos + 2 > recvd) break;
                uint8_t setting = buffer[pos + 1];
                if (setting == 1 || setting == 11 || setting == 12) {
                    if (pos + 3 > recvd || pos + 3 + buffer[pos + 2] > recvd) break;
                    pos += 3 + buffer[pos + 2];
                } else if ((setting >= 2 && setting <= 10) || (setting >= 13 && setting <= 24)) {
                    if (pos + 6 > recvd) break;
                    int32_t value;
                    memcpy(&value, buffer + pos + 2, 4);
                    if (setting == 3) {
                        client->spec.server_mode = value & 0xFF;
                        cs2d_client_console_print(client, 255, 220, 0, "spec mode changed to %d", value);
                    }
                    if (setting == 16) client->net_hovertext = value; /* mp_hovertext (FUN_1401e3f10 case 0x10) */
                    if (setting == 19) {
                        /* sv_forcelight (case 0x13): the engine turns on and builds the map lights (FUN_1401d6790) */
                        client->net_forcelight = value != 0;
                        cs2d_client_console_print(client, value ? 0 : 255, value ? 255 : 0, 0, "force lighting %s",
                                                  value ? "ENABLED" : "DISABLED");
                        apply_light_config(client);
                        cs2d_light_load_map(&client->game.map);
                    }
                    if (setting == 20) {
                        client->net_daylight = value;   /* sv_daylighttime (case 0x14) */
                        apply_light_config(client);
                    }
                    if (setting == 22) {
                        client->net_hudscale = value;   /* mp_hudscale (FUN_1401e3f10 case 0x16) */
                    }
                    pos += 6;
                } else {
                    break;
                }
            } else if (opcode == CS2D_NET_HUDTXT) {
                /* Server -> Client: [234] hudtxt / hudtxt2 / hudtxtclear (FUN_1400a3530) */
                if (!cs2d_hudtxt_on_message(&client->hudtxt, buffer, recvd, &pos)) break;
            } else if (opcode == CS2D_NET_TWEEN) {
                /* Server -> Client: [54] Ttween.receive: hudtxtmove / hudtxtalphafade / hudtxtcolorfade */
                if (!cs2d_hudtxt_on_tween(&client->hudtxt, buffer, recvd, &pos, (uint32_t)SDL_GetTicks())) break;
            } else if (opcode == CS2D_NET_VOTE) {
                /* Server -> Client: [43, voter, type u8 (1 kick, 2 map), value u16] (FUN_1402db210 -> TVote.votekick /
                 * votemap with p3 = 0). Values >= 0xff9b are refusals: 0xfffe / 0xfff5 / 0xfff4 (lang 0720..0722, 0911) */
                if (pos + 5 > recvd) break;
                cs2d_client_on_vote(client, buffer[pos + 1], buffer[pos + 2], buffer[pos + 3] | (buffer[pos + 4] << 8));
                pos += 5;
            } else if (opcode == CS2D_NET_LEADERBOARD) {
                if (!leaderboard_on_message(client, buffer, recvd, &pos)) break;
            } else if (opcode == CS2D_NET_LUAMENU) {
                /* Server -> Client: [235, pid:u8, slen:u16, text:u16*] (FUN_1402e4820 / FUN_14015c320) */
                if (pos + 4 > recvd) break;
                uint8_t pid = buffer[pos + 1];
                uint16_t slen = (uint16_t)(buffer[pos + 2] | (buffer[pos + 3] << 8));
                if (pos + 4 + slen * 2 > recvd) break;

                /* Decode UTF-16LE string into UTF-8 */
                char raw_text[1024];
                utf16_to_colored_utf8(buffer + pos + 4, slen, NULL, NULL, NULL, raw_text, sizeof(raw_text));
                pos += 4 + slen * 2;

                /* Check if optional button byte follows (server->client might have 0xff) */
                if (pos < recvd) {
                    pos++; /* consume trailing byte (0xff) */
                }

                /* Check if target player matches local player (or broadcast pid == 0) */
                if (pid == 0 || pid == client->local_player_id) {
                    cs2d_luamenu_parse(&client->luamenu, raw_text);
                }
            } else if (opcode == CS2D_NET_PING_INGAME) {
                /* Server -> Client: [249, int32_t ts] -> Reply [249, int32_t ts] (0x1401d9f10) */
                if (pos + 5 > recvd) break;
                uint8_t pong[5];
                memcpy(pong, buffer + pos, 5);
                cs2d_client_send_udp(client, pong, 5, false);
                pos += 5;
            } else if (opcode == CS2D_NET_CLOCK_PING) {
                /* Server -> Client: [229, int32_t srv_ts] -> Reply [229, int32_t srv_ts, int32_t cli_ts] (0x1401d7690, 0x1402e2ed0) */
                if (pos + 5 > recvd) break;
                uint8_t pong[9];
                pong[0] = CS2D_NET_CLOCK_PING;
                memcpy(&pong[1], buffer + pos + 1, 4);
                uint32_t now_ms = (uint32_t)GetTickCount();
                memcpy(&pong[5], &now_ms, 4);
                cs2d_client_send_udp(client, pong, sizeof(pong), false);
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

/* Map shown by the active scene (game or editor); NULL in the lobby */
static cs2d_map_t *client_active_map(cs2d_client_t *client)
{
    if (client->scene == CS2D_SCENE_GAME)   return &client->game.map;
    if (client->scene == CS2D_SCENE_EDITOR) return &client->editor_map;
    return NULL;
}

/* Commands that act on the local player and therefore need a running game */
static bool is_game_only_command(const char *cmd)
{
    static const char *const cmds[] = {
        "equip", "give", "item", "tp", "setpos", "pos", "where", "hp", "sethealth", "god",
        "armor", "setarmor", "money", "setmoney", "team", "bot_add", "addbot", "bot_kill", "killbots", "walkto"
    };
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        if (_stricmp(cmd, cmds[i]) == 0) return true;
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
    char arg3[64] = {0};
    int n = sscanf(cmd_line, "%63s %63s %63s %63s", cmd, arg1, arg2, arg3);
    if (n <= 0) return;

    cs2d_player_t *pl  = &client->game.players[client->local_player_id];
    cs2d_map_t    *map = client_active_map(client);

    if (is_game_only_command(cmd) && client->scene != CS2D_SCENE_GAME) {
        cs2d_client_console_print(client, 255, 45, 45, "'%s' is only available in game (use: map <name>)", cmd);
        return;
    }

    if (_stricmp(cmd, "help") == 0 || _stricmp(cmd, "cmdlist") == 0 || _stricmp(cmd, "?") == 0) {
        cs2d_client_console_print(client, 255, 220, 0,   "--- CS2D Console Commands ---");
        cs2d_client_console_print(client, 220, 220, 220, "map <name>          - Load map from maps/<name>.map (see: maps)");
        cs2d_client_console_print(client, 220, 220, 220, "maps / maplist      - List all maps in maps/ directory");
        cs2d_client_console_print(client, 220, 220, 220, "equip <id|name>     - Equip weapon/item (e.g. equip ak47, equip awp, equip 30)");
        cs2d_client_console_print(client, 220, 220, 220, "weapons / itemlist  - List all weapons & IDs");
        cs2d_client_console_print(client, 220, 220, 220, "sun <0..360>        - Set map sun angle (daylight_time) & rebuild shadows");
        cs2d_client_console_print(client, 220, 220, 220, "shadows [0..100]    - Set shadow opacity (0 = disabled)");
        cs2d_client_console_print(client, 220, 220, 220, "playershadows [0|1] - Toggle player & hostage drop shadows");
        cs2d_client_console_print(client, 220, 220, 220, "itemshadows [0|1]   - Toggle ground item & projectile shadows");
        cs2d_client_console_print(client, 220, 220, 220, "vsync [0|1]         - Toggle vertical sync (OpenGL swap interval)");
        cs2d_client_console_print(client, 220, 220, 220, "tp <tx> <ty>        - Teleport player to tile (tx, ty) (see: pos)");
        cs2d_client_console_print(client, 220, 220, 220, "hp <val> / armor <v>- Set player health / armor (money <val>, team <t|ct>)");
        cs2d_client_console_print(client, 220, 220, 220, "bot_add [t|ct]      - Spawn a bot (bot_kill to remove bots)");
        cs2d_client_console_print(client, 220, 220, 220, "status / entlist    - Inspect map, entities, player & network state");
        cs2d_client_console_print(client, 220, 220, 220, "connect <ip> [port] - Connect to CS2D server (disconnect, fps, clear, quit)");
        cs2d_client_console_print(client, 220, 220, 220, "menu / editor [map] - Return to the main menu / open the map editor");
    } else if (_stricmp(cmd, "clear") == 0 || _stricmp(cmd, "cls") == 0) {
        client->console_line_head  = 0;
        client->console_line_count = 0;
        client->console_scroll.top = 0;
    } else if (_stricmp(cmd, "restart") == 0 || _stricmp(cmd, "sv_restart") == 0) {
        if (client->scene != CS2D_SCENE_GAME) {
            cs2d_client_console_print(client, 255, 45, 45, "'restart' is only available in game");
        } else if (client->connected) {
            cs2d_client_console_print(client, 255, 45, 45, "restart: rounds are controlled by the server");
        } else {
            cs2d_scene_game_restart_round(client);
        }
    } else if (cs2d_scene_game_inv_cmd(client, cmd)) {
        /* invnext / invprev / slot0..slot10 (weapon selection HUD) */
    } else if (cs2d_scene_game_buy_cmd(client, cmd)) {
        /* buy / buyequip / buyammo1 / buyammo2 (Buy menu) */
    } else if (_stricmp(cmd, "bind") == 0) {
        /* bind "<key>" "<command>" like sys/controls.cfg; without a command it shows the binding */
        char key[64], bcmd[CS2D_BIND_CMD_LEN];
        snprintf(key, sizeof(key), "%s", arg1[0] == '"' ? arg1 + 1 : arg1);
        snprintf(bcmd, sizeof(bcmd), "%s", arg2[0] == '"' ? arg2 + 1 : arg2);
        key[strcspn(key, "\"")] = '\0';
        bcmd[strcspn(bcmd, "\"")] = '\0';
        if (!key[0]) {
            cs2d_client_console_print(client, 255, 45, 45, "bind <key> <command>");
        } else if (bcmd[0]) {
            cs2d_binds_set(&client->binds, key, bcmd);
        } else {
            const char *b = cs2d_binds_find(&client->binds, key);
            cs2d_client_console_print(client, 220, 220, 220, "%s = %s", key, b ? b : "-");
        }
    } else if (_stricmp(cmd, "specmode") == 0 || _stricmp(cmd, "specplayer") == 0) {
        /* Spectator bindings of sys/controls.cfg (leftctrl / space); only in game (DAT_140ddf418 1..2) */
        if (client->scene == CS2D_SCENE_GAME) {
            if (_stricmp(cmd, "specmode") == 0) cs2d_scene_game_specmode(client);
            else cs2d_scene_game_specplayer(client);
        }
    } else if (_stricmp(cmd, "spec") == 0) {
        /* Test helper: spectator state */
        const cs2d_player_t *t = (client->spec.target > 0 && client->spec.target <= CS2D_MAX_PLAYERS)
                                 ? &client->game.players[client->spec.target] : NULL;
        cs2d_client_console_print(client, 220, 220, 220, "spec sv_specmode=%d mode=%d target=%d (%s) cam=%d,%d",
                                  client->spec.server_mode, client->spec.mode, client->spec.target,
                                  (t && t->id) ? t->name : "-", (int)client->cam_x, (int)client->cam_y);
    } else if (_stricmp(cmd, "entdump") == 0) {
        /* Positions of every entity in state 1, in map order (compared against the server in tests) */
        if (!map) {
            cs2d_client_console_print(client, 255, 45, 45, "No map loaded.");
            return;
        }
        char out[CS2D_CONSOLE_LINE_LEN] = "entdump:";
        int count = 0;
        for (uint32_t i = 0; i < map->entity_count; i++) {
            if (!map->entity_state[i]) continue;
            char item[24];
            snprintf(item, sizeof(item), " %d,%d", map->entities[i].x, map->entities[i].y);
            if (strlen(out) + strlen(item) >= sizeof(out) - 1) {
                cs2d_client_console_print(client, 170, 170, 170, "%s", out);
                snprintf(out, sizeof(out), "entdump:");
            }
            strcat(out, item);
            count++;
        }
        cs2d_client_console_print(client, 170, 170, 170, "%s", out);
        cs2d_client_console_print(client, 170, 170, 170, "entdump: %d entities in state 1", count);
    } else if (_stricmp(cmd, "entstate") == 0) {
        if (!map || n < 3) {
            cs2d_client_console_print(client, 255, 45, 45, "Usage: entstate <tile_x> <tile_y> (map required)");
            return;
        }
        int tx = atoi(arg1), ty = atoi(arg2), shown = 0;
        for (uint32_t i = 0; i < map->entity_count; i++) {
            const cs2d_entity_t *e = &map->entities[i];
            if (e->x != tx || e->y != ty) continue;
            cs2d_client_console_print(client, 170, 170, 170, "#%u %s name='%s' trigger='%s' state=%u",
                                      i, cs2d_entity_type_name(e->type), e->name, e->trigger, map->entity_state[i]);
            shown++;
        }
        if (!shown) cs2d_client_console_print(client, 170, 170, 170, "No entity at (%d, %d)", tx, ty);
    } else if (_stricmp(cmd, "inv") == 0) {
        /* Inventory of the local player (compared against the server's Lua view in tests) */
        const cs2d_player_t *ip = &client->game.players[client->local_player_id];
        char out[CS2D_CONSOLE_LINE_LEN];
        int len = snprintf(out, sizeof(out), "inv: hp=%d armor=%d money=%d held=%u weapons:", ip->health, ip->armor,
                           ip->money, ip->current_weapon);
        for (int s = 0; s < CS2D_SLOT_COUNT && len < (int)sizeof(out) - 24; s++) {
            uint8_t w = ip->inventory[s];
            if (w) len += snprintf(out + len, sizeof(out) - (size_t)len, " %u(%d/%d)", w, ip->clip_ammo[w], ip->reserve_ammo[w]);
        }
        cs2d_client_console_print(client, 170, 170, 170, "%s", out);
    } else if (_stricmp(cmd, "walkto") == 0) {
        if (n < 3) {
            cs2d_client_console_print(client, 255, 45, 45, "Usage: walkto <tile_x> <tile_y>");
            return;
        }
        if (!cs2d_scene_game_walk_to(client, atoi(arg1), atoi(arg2))) {
            cs2d_client_console_print(client, 255, 45, 45, "walkto: no path to tile (%s, %s)", arg1, arg2);
        }
    } else if (_stricmp(cmd, "net_send") == 0) {
        /* Protocol research: send raw payload bytes (hex) as one reliable message */
        uint8_t payload[256];
        int     plen = 0;
        const char *p = cmd_line + strlen(cmd);
        unsigned int byte;
        int consumed;
        while (plen < (int)sizeof(payload) && sscanf(p, " %2x%n", &byte, &consumed) == 1) {
            payload[plen++] = (uint8_t)byte;
            p += consumed;
        }
        if (plen == 0 || client->net_sock == INVALID_SOCKET) {
            cs2d_client_console_print(client, 255, 45, 45, "Usage: net_send <hex bytes> (requires a server connection)");
        } else {
            cs2d_client_send_udp(client, payload, (size_t)plen, true);
            cs2d_client_console_print(client, 170, 170, 170, "net_send: %d byte(s) sent", plen);
        }
    } else if (_stricmp(cmd, "consolebench") == 0) {
        /* Floods the console with varied lines to measure print cost and scrolling with a full history */
        int lines = (n >= 2) ? atoi(arg1) : 5000;
        if (lines < 1) lines = 1;
        if (lines > 1000000) lines = 1000000;
        uint64_t t0 = SDL_GetPerformanceCounter();
        for (int i = 0; i < lines; i++) {
            cs2d_client_console_print(client, (uint8_t)(120 + (i * 37) % 136), (uint8_t)(120 + (i * 53) % 136), 170,
                                      "bench %06d | %.*s", i, 8 + (i * 13) % 60,
                                      "the quick brown fox jumps over the lazy dog while CS2D prints console lines");
        }
        double ms = (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency();
        cs2d_client_console_print(client, 100, 255, 100, "consolebench: %d lines in %.2f ms (%.3f us/line), %d kept",
                                  lines, ms, ms * 1000.0 / lines, client->console_line_count);
    } else if (_stricmp(cmd, "fps") == 0 || _stricmp(cmd, "showfps") == 0) {
        if (n >= 2) {
            client->show_fps = (atoi(arg1) != 0);
        } else {
            client->show_fps = !client->show_fps;
        }
        cs2d_client_console_print(client, 170, 170, 170, "FPS display: %s", client->show_fps ? "ON" : "OFF");
    } else if (_stricmp(cmd, "shadows") == 0 || _stricmp(cmd, "shadow") == 0) {
        if (n >= 2) {
            int val = atoi(arg1);
            if (val <= 1 && strcmp(arg1, "1") == 0) val = 50;
            cs2d_render_set_shadow_intensity(val);
            cs2d_render_set_shadows_enabled(val > 0);
            client->config.shadows = val;
            apply_light_config(client);
        } else {
            bool en = !cs2d_render_get_shadows_enabled();
            cs2d_render_set_shadows_enabled(en);
            if (en && cs2d_render_get_shadow_intensity() == 0) {
                cs2d_render_set_shadow_intensity(50);
                client->config.shadows = 50;
            }
        }
        cs2d_client_console_print(client, 170, 170, 170, "Shadows: %s (intensity: %d%%)",
                                  cs2d_render_get_shadows_enabled() ? "ENABLED" : "DISABLED",
                                  cs2d_render_get_shadow_intensity());
    } else if (_stricmp(cmd, "playershadows") == 0 || _stricmp(cmd, "playershadow") == 0) {
        bool en = (n >= 2) ? (atoi(arg1) != 0) : !cs2d_render_get_player_shadows();
        cs2d_render_set_player_shadows(en);
        client->config.playershadows = en;
        apply_light_config(client);
        cs2d_client_console_print(client, 170, 170, 170, "Player shadows: %s", en ? "ENABLED" : "DISABLED");
    } else if (_stricmp(cmd, "itemshadows") == 0 || _stricmp(cmd, "itemshadow") == 0) {
        bool en = (n >= 2) ? (atoi(arg1) != 0) : !cs2d_render_get_item_shadows();
        cs2d_render_set_item_shadows(en);
        client->config.itemshadows = en;
        cs2d_client_console_print(client, 170, 170, 170, "Item shadows: %s", en ? "ENABLED" : "DISABLED");
    } else if (_stricmp(cmd, "vsync") == 0) {
        /* sys/config.cfg "vsync": the OpenGL swap interval (DAT_140a12960) */
        bool en = (n >= 2) ? (atoi(arg1) != 0) : !cs2d_render_get_vsync();
        cs2d_render_set_vsync(en);
        client->config.vsync = en;
        cs2d_client_console_print(client, 170, 170, 170, "VSync: %s", en ? "ENABLED" : "DISABLED");
    } else if (_stricmp(cmd, "light") == 0 || _stricmp(cmd, "mapshadows") == 0) {
        /* Settings of sys/config.cfg: light 0..5 (light engine quality, 0 = off), mapshadows 0..4 */
        int *val = _stricmp(cmd, "light") == 0 ? &client->config.light : &client->config.mapshadows;
        if (n >= 2) *val = atoi(arg1);
        apply_light_config(client);
        if (map) cs2d_light_load_map(map);
        cs2d_client_console_print(client, 170, 170, 170, "%s %d", cmd, *val);
    } else if (_stricmp(cmd, "lightdump") == 0) {
        /* Scene of the light engine for tools/agent/emu_light.py */
        const char *path = n >= 2 ? arg1 : "tools/agent/out/light.scene";
        FILE *f = fopen(path, "w");
        if (f) {
            cs2d_light_dump_scene(f, (int)roundf(client->cam_x), (int)roundf(client->cam_y));
            fclose(f);
        }
        cs2d_client_console_print(client, 170, 170, 170, "lightdump: %s %s", path, f ? "written" : "failed");
    } else if (_stricmp(cmd, "sun") == 0 || _stricmp(cmd, "daylight") == 0) {
        if (!map) {
            cs2d_client_console_print(client, 255, 45, 45, "No map loaded.");
        } else if (n < 2) {
            cs2d_client_console_print(client, 170, 170, 170,
                                      "Current map sun angle (daylight_time): %d deg (Usage: sun <0..360>)",
                                      map->daylight_time);
        } else {
            int ang = atoi(arg1);
            if (ang < 0) ang = 0;
            if (ang > 360) ang = 360;
            map->daylight_time = ang;
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
        if (GetFileAttributesA(map_path) == INVALID_FILE_ATTRIBUTES) {
            cs2d_client_console_print(client, 255, 45, 45, "Error: Unable to load map '%s'", map_path);
            return;
        }
        cs2d_client_start_game(client, mname);
    } else if (_stricmp(cmd, "editor") == 0) {
        snprintf(client->editor_map_name, sizeof(client->editor_map_name), "%s", (n >= 2) ? arg1 : "");
        char *dot = strstr(client->editor_map_name, ".map");
        if (dot) *dot = '\0';
        cs2d_client_set_scene(client, CS2D_SCENE_EDITOR);
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
    } else if (_stricmp(cmd, "objlist") == 0) {
        /* Debug: the client's dynamic objects (Tdo.list) */
        for (int i = 0; i < CS2D_DYNOBJ_MAX; i++) {
            const cs2d_dynobj_t *o = &client->dynobj.objs[i];
            if (!o->active) continue;
            cs2d_client_console_print(client, 170, 170, 170, "obj %d typ %d tile (%d, %d) hp %d mode %d team %d pl %d",
                                      o->id, o->typ, (int)(o->x / 32.0f), (int)(o->y / 32.0f), o->health, o->mode,
                                      o->team, o->player);
        }
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
        if (n < 2) {
            client->team_menu_open = true;
            client->team_menu_page = 0;
            client->console_open   = false;
        } else {
            uint8_t look = 4; /* Auto-Select skin */
            if (n >= 3 && arg2[0] >= '1' && arg2[0] <= '4') {
                look = (uint8_t)(arg2[0] - '1');
            }
            if (_stricmp(arg1, "t") == 0 || _stricmp(arg1, "1") == 0) {
                cs2d_client_select_team(client, CS2D_TEAM_T, look);
            } else if (_stricmp(arg1, "ct") == 0 || _stricmp(arg1, "2") == 0) {
                cs2d_client_select_team(client, CS2D_TEAM_CT, look);
            } else if (_stricmp(arg1, "auto") == 0 || _stricmp(arg1, "5") == 0) {
                cs2d_client_select_team(client, 4, 4);
            } else if (_stricmp(arg1, "spec") == 0 || _stricmp(arg1, "spectator") == 0 || _stricmp(arg1, "6") == 0) {
                cs2d_client_select_team(client, CS2D_TEAM_SPEC, 5);
            } else {
                cs2d_client_select_team(client, CS2D_TEAM_CT, look);
            }
        }
    } else if (_stricmp(cmd, "serverinfo") == 0 || _stricmp(cmd, "motd") == 0) {
        client->server_info_open = true;
        client->console_open     = false;
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
            bot->bot        = true;
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
    } else if (_stricmp(cmd, "killmsg") == 0) {
        /* Debug: killmsg <killer> <weapon> <victim> [assist] adds a kill message like [19] */
        int k = 0, w = 0, v = 0, a = 0;
        if (sscanf(cmd_line, "%*s %d %d %d %d", &k, &w, &v, &a) < 3) {
            cs2d_client_console_print(client, 255, 45, 45, "killmsg <killer> <weapon> <victim> [assist]");
        } else {
            cs2d_client_add_kill(client, k, w, v, a);
        }
    } else if (_stricmp(cmd, "status") == 0 || _stricmp(cmd, "info") == 0) {
        cs2d_client_console_print(client, 255, 220, 0, "Scene: %s", cs2d_scene_get(client->scene)->name);
        if (map) {
            cs2d_client_console_print(client, 255, 220, 0,
                                      "Map: %ux%u tiles | Tileset: %s | Sun Angle: %d deg | Entities: %u",
                                      map->width, map->height, map->tileset, map->daylight_time,
                                      map->entity_count);
        }
        if (client->scene == CS2D_SCENE_GAME) {
            const cs2d_weapon_def_t *wdef = cs2d_weapon_get(pl->current_weapon);
            cs2d_client_console_print(client, 170, 170, 170,
                                      "Player [%u] '%s' | HP: %u | Armor: %u | Money: $%u | Weapon: %s",
                                      pl->id, pl->name, pl->health, pl->armor, pl->money,
                                      (wdef && wdef->name) ? wdef->name : "None");
        }
        cs2d_client_console_print(client, 170, 170, 170,
                                  "Network: %s (join_stage=%d, up=%ub/s, down=%ub/s)",
                                  client->connected ? "CONNECTED" : "OFFLINE",
                                  client->join_stage, client->net_bps_up, client->net_bps_down);
    } else if (_stricmp(cmd, "entlist") == 0 || _stricmp(cmd, "entities") == 0) {
        if (!map) {
            cs2d_client_console_print(client, 255, 45, 45, "No map loaded.");
            return;
        }
        int filter_type = (n >= 2) ? atoi(arg1) : -1;
        int shown = 0;
        for (uint32_t i = 0; i < map->entity_count; i++) {
            const cs2d_entity_t *e = &map->entities[i];
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
                                          map->entity_count);
                break;
            }
        }
    } else if (_stricmp(cmd, "connect") == 0) {
        const char *ip = (n >= 2) ? arg1 : client->config.server_ip;
        int port = (n >= 3) ? atoi(arg2) : client->config.server_port;
        if (port <= 0) port = 36963;
        snprintf(client->net_password, sizeof(client->net_password), "%s", (n >= 4) ? arg3 : "");
        client->xfer.map_mode = 0; /* A new join starts in normal map mode; error 11 picks another one */
        cs2d_client_console_print(client, 170, 170, 170, "Connecting to %s:%d ...", ip, port);
        /* The handshake runs in the current scene (the menu); [252,4,0] then enters the game on the server's map */
        snprintf(client->config.server_ip, sizeof(client->config.server_ip), "%s", ip);
        client->config.server_port = port;
        if (client->scene == CS2D_SCENE_EDITOR) cs2d_client_set_scene(client, CS2D_SCENE_LOBBY);
        cs2d_client_connect(client, ip, port);
    } else if (_stricmp(cmd, "disconnect") == 0) {
        if (client->scene == CS2D_SCENE_GAME) {
            cs2d_client_set_scene(client, CS2D_SCENE_LOBBY);
            cs2d_client_console_print(client, 255, 220, 0, "Disconnected from server.");
        } else {
            cs2d_client_console_print(client, 170, 170, 170, "Not connected.");
        }
    } else if (_stricmp(cmd, "name") == 0) {
        if (n >= 2) {
            strncpy(client->config.name, arg1, sizeof(client->config.name) - 1);
            strncpy(pl->name, arg1, sizeof(pl->name) - 1);
            client->opt_name_cursor = (int)strlen(client->config.name);
            cs2d_client_console_print(client, 100, 255, 100, "Player name changed to '%s'", pl->name);
        } else {
            cs2d_client_console_print(client, 170, 170, 170, "Current player name: '%s'", pl->name);
        }
    } else if (_stricmp(cmd, "menu") == 0 || _stricmp(cmd, "lobby") == 0) {
        cs2d_client_set_scene(client, CS2D_SCENE_LOBBY);
        client->console_open = false;
    } else if (_stricmp(cmd, "createserver") == 0 || _stricmp(cmd, "newgame") == 0) {
        client->create_server_open = true;
        client->options_open       = false;
        client->console_open       = false;
        client->ui.focused_id      = CS2D_UI_ID("SrvNameInput");
    } else if (_stricmp(cmd, "options") == 0 || _stricmp(cmd, "settings") == 0) {
        client->options_open       = true;
        client->create_server_open = false;
        client->console_open       = false;
        client->ui.focused_id      = CS2D_UI_ID("OptPlayerName");
    } else if (_stricmp(cmd, "say") == 0 || _stricmp(cmd, "sayteam") == 0) {
        bool is_team = (_stricmp(cmd, "sayteam") == 0);
        const char *p = cmd_line + strlen(cmd);
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') {
            if (client->scene == CS2D_SCENE_GAME) {
                client->chat_open = true;
                client->chat_just_opened = true;
                client->chat_mode = is_team ? 2 : 1;
                client->chat_input[0] = '\0';
                client->chat_cursor = 0;
                client->key_forward = client->key_back = client->key_left = client->key_right = false;
                client->mouse_fire = client->mouse_attack2 = false;
            }
        } else {
            cs2d_client_send_chat(client, is_team ? 2 : 1, p);
        }
    } else if (_stricmp(cmd, "mp_hudscale") == 0) {
        if (n >= 2) {
            int v = atoi(arg1);
            if (v < 0) v = 0;
            if (v > 3) v = 3;
            client->net_hudscale = v;
            cs2d_client_console_print(client, 255, 220, 0, "mp_hudscale: %d", v);
        } else {
            cs2d_client_console_print(client, 170, 170, 170, "mp_hudscale: %d", client->net_hudscale);
        }
    } else if (_stricmp(cmd, "quit") == 0 || _stricmp(cmd, "exit") == 0) {
        client->quit_requested = true;
    } else {
        cs2d_client_console_print(client, 255, 45, 45, "Unknown command '%s'. Type 'help' for a list of commands.", cmd);
    }
}

bool cs2d_client_any_ui_open(const cs2d_client_t *client)
{
    return client && (client->console_open || client->create_server_open ||
                      client->options_open || client->server_info_open || client->briefing_open ||
                      client->leaderboard_open ||
                      client->team_menu_open || client->servers_open || client->connect_ip_open ||
                      client->download_open || client->join_menu != 0 || client->chat_open);
}

void cs2d_client_handle_text_input(cs2d_client_t *client, const char *text)
{
    if (!client || !text) return;
    if (client->chat_open) {
        if (client->chat_just_opened) {
            client->chat_just_opened = false;
            return;
        }
        size_t cur_len = strlen(client->chat_input);
        size_t add_len = strlen(text);
        if (cur_len + add_len < CS2D_CHAT_LINE_LEN - 1) {
            strcat(client->chat_input, text);
            client->chat_cursor = (int)strlen(client->chat_input);
        }
        return;
    }
    if (cs2d_client_any_ui_open(client)) {
        cs2d_ui_feed_text(&client->ui, text);
    }
}

/* Replaces the console input line (history recall) and resets the textbox selection / undo state */
static void console_set_input(cs2d_client_t *client, const char *text)
{
    snprintf(client->console_input, sizeof(client->console_input), "%s", text);
    client->console_cursor = (int)strlen(client->console_input);
    cs2d_ui_edit_reset(&client->ui);
}

/* Enter in the console textbox: store in history and queue the command for the next update */
static void console_submit(cs2d_client_t *client)
{
    if (client->console_input[0] == '\0') return;

    if (client->console_history_count < CS2D_CONSOLE_HIST_MAX) {
        snprintf(client->console_history[client->console_history_count++],
                 CS2D_CONSOLE_LINE_LEN, "%s", client->console_input);
    } else {
        memmove(&client->console_history[0],
                &client->console_history[1],
                sizeof(client->console_history[0]) * (CS2D_CONSOLE_HIST_MAX - 1));
        snprintf(client->console_history[CS2D_CONSOLE_HIST_MAX - 1],
                 CS2D_CONSOLE_LINE_LEN, "%s", client->console_input);
    }
    client->console_history_pos = client->console_history_count;

    snprintf(client->console_pending_cmd, sizeof(client->console_pending_cmd), "%s", client->console_input);
    console_set_input(client, "");
}

void cs2d_client_handle_console_key(cs2d_client_t *client, int sdl_key)
{
    if (!client) return;

    if (client->chat_open) {
        if (sdl_key == SDLK_RETURN || sdl_key == SDLK_KP_ENTER || sdl_key == '\r') {
            if (client->chat_input[0] == '/') {
                /* "/cmd args" in chat runs a console command instead of sending text. */
                char cmd[sizeof(client->chat_input)];
                snprintf(cmd, sizeof(cmd), "%s", client->chat_input + 1);
                if (cmd[0] != '\0') cs2d_client_console_exec(client, cmd);
            } else if (client->chat_input[0] != '\0') {
                cs2d_client_send_chat(client, (uint8_t)client->chat_mode, client->chat_input);
            }
            client->chat_open = false;
            client->chat_just_opened = false;
            client->chat_input[0] = '\0';
            client->chat_cursor = 0;
            return;
        }
        if (sdl_key == SDLK_ESCAPE || sdl_key == '\x1b') {
            client->chat_open = false;
            client->chat_just_opened = false;
            client->chat_input[0] = '\0';
            client->chat_cursor = 0;
            return;
        }
        if (sdl_key == SDLK_BACKSPACE || sdl_key == '\b') {
            size_t len = strlen(client->chat_input);
            if (len > 0) {
                client->chat_input[len - 1] = '\0';
                client->chat_cursor = (int)(len - 1);
            }
            return;
        }
        return;
    }


    if (sdl_key == SDLK_GRAVE) {
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

    if (client->create_server_open || client->options_open) {
        if (sdl_key == SDLK_ESCAPE && !cs2d_ui_capture_escape(&client->ui)) {
            client->create_server_open = false;
            client->options_open       = false;
            return;
        }
        cs2d_ui_feed_key(&client->ui, sdl_key);
        return;
    }

    if (!client->console_open && client->join_menu != 0) {
        if (sdl_key == SDLK_ESCAPE && !cs2d_ui_capture_escape(&client->ui)) {
            join_abort(client, 9);
            return;
        }
        if (sdl_key == SDLK_RETURN || sdl_key == SDLK_KP_ENTER) {
            join_menu_submit(client);
            return;
        }
        cs2d_ui_feed_key(&client->ui, sdl_key);
        return;
    }

    if (!client->console_open && client->connect_ip_open) {
        if (sdl_key == SDLK_ESCAPE && !cs2d_ui_capture_escape(&client->ui)) {
            client->connect_ip_open = false;
            return;
        }
        if (sdl_key == SDLK_RETURN || sdl_key == SDLK_KP_ENTER) {
            uint32_t ip;
            uint16_t port;
            if (cs2d_sv_parse_address(client->connect_ip_text, &ip, &port)) servers_join(client, ip, port);
            return;
        }
        cs2d_ui_feed_key(&client->ui, sdl_key);
        return;
    }

    if (!client->console_open && client->servers_open) {
        if (sdl_key == SDLK_ESCAPE) {
            client->servers_open = false;
            cs2d_sv_browser_close(&client->servers);
        }
        return;
    }

    if (!client->console_open) {
        if (client->server_info_open) {
            switch (sdl_key) {
                case SDLK_ESCAPE:
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                case SDLK_SPACE:
                case SDLK_0:
                    client->server_info_open = false;
                    break;
                case SDLK_UP:       server_info_scroll_by(client, -1, false); break;
                case SDLK_DOWN:     server_info_scroll_by(client, 1, false); break;
                case SDLK_PAGEUP:   server_info_scroll_by(client, -1, true); break;
                case SDLK_PAGEDOWN: server_info_scroll_by(client, 1, true); break;
                default:
                    break;
            }
            return;
        }

        if (client->briefing_open) {
            int visible = client->briefing_visible_lines > 0 ? client->briefing_visible_lines : 1;
            switch (sdl_key) {
                case SDLK_ESCAPE:
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                case SDLK_SPACE:
                case SDLK_0:
                    client->briefing_open = false;
                    break;
                case SDLK_UP:       cs2d_ui_scroll_by(&client->briefing_scroll, -1, visible, client->briefing_line_count); break;
                case SDLK_DOWN:     cs2d_ui_scroll_by(&client->briefing_scroll, 1, visible, client->briefing_line_count); break;
                case SDLK_PAGEUP:   cs2d_ui_scroll_page(&client->briefing_scroll, -1, visible, client->briefing_line_count); break;
                case SDLK_PAGEDOWN: cs2d_ui_scroll_page(&client->briefing_scroll, 1, visible, client->briefing_line_count); break;
                default:
                    break;
            }
            return;
        }

        if (client->leaderboard_open) {
            if (sdl_key == SDLK_ESCAPE || sdl_key == SDLK_RETURN || sdl_key == SDLK_KP_ENTER || sdl_key == SDLK_0) {
                client->leaderboard_open = false;
            } else if (sdl_key == SDLK_UP || sdl_key == SDLK_DOWN) {
                cs2d_ui_scroll_by(&client->lb_scroll, sdl_key == SDLK_UP ? -1 : 1, 16, CS2D_LEADERBOARD_MAX);
            } else if (sdl_key == SDLK_PAGEUP || sdl_key == SDLK_PAGEDOWN) {
                cs2d_ui_scroll_page(&client->lb_scroll, sdl_key == SDLK_PAGEUP ? -1 : 1, 16, CS2D_LEADERBOARD_MAX);
            }
            return;
        }

        if (client->team_menu_open) {
            if (sdl_key == SDLK_0 || (sdl_key >= (int)SDLK_1 && sdl_key <= (int)SDLK_6) || sdl_key == SDLK_BACKSPACE) {
                client->spec.idle_cam = false; /* A menu button (Tgui_hudmen.button 0x140129f40) */
            }
            if (sdl_key == SDLK_ESCAPE || sdl_key == SDLK_0 || sdl_key == SDLK_M) {
                client->team_menu_open = false;
                client->team_menu_page = 0;
                return;
            }
            if (client->team_menu_page == 0) {
                if (sdl_key == SDLK_1) {
                    client->team_menu_page = 1;
                } else if (sdl_key == SDLK_2) {
                    client->team_menu_page = 2;
                } else if (sdl_key == SDLK_5) {
                    cs2d_client_select_team(client, 4, 4);
                } else if (sdl_key == SDLK_6) {
                    cs2d_client_select_team(client, CS2D_TEAM_SPEC, 5);
                }
            } else {
                uint8_t team = (client->team_menu_page == 1) ? CS2D_TEAM_T : CS2D_TEAM_CT;
                if (sdl_key >= (int)SDLK_1 && sdl_key <= (int)SDLK_4) {
                    cs2d_client_select_team(client, team, (uint8_t)(sdl_key - (int)SDLK_1));
                } else if (sdl_key == (int)SDLK_5) {
                    cs2d_client_select_team(client, team, 4);
                } else if (sdl_key == SDLK_BACKSPACE) {
                    client->team_menu_page = 0;
                }
            }
            return;
        }
        return;
    }

    /* Console-only keys; everything else (editing, selection, clipboard, Enter) goes to the textbox */
    switch (sdl_key) {
        case SDLK_ESCAPE:
            client->console_open = false;
            break;

        case SDLK_UP:
            if (client->console_history_count > 0 && client->console_history_pos > 0) {
                client->console_history_pos--;
                console_set_input(client, client->console_history[client->console_history_pos]);
            }
            break;

        case SDLK_DOWN:
            if (client->console_history_pos + 1 < client->console_history_count) {
                client->console_history_pos++;
                console_set_input(client, client->console_history[client->console_history_pos]);
            } else {
                client->console_history_pos = client->console_history_count;
                console_set_input(client, "");
            }
            break;

        case SDLK_PAGEUP:
        case SDLK_PAGEDOWN:
            /* Page by one screen minus a line of overlap */
            cs2d_ui_scroll_page(&client->console_scroll, (sdl_key == SDLK_PAGEUP) ? -1 : 1,
                                console_visible(client), client->console_line_count);
            break;

        default:
            cs2d_ui_feed_key(&client->ui, sdl_key);
            break;
    }
}

/* The wheel goes to the UI: each scrollable view takes it while laid out (cs2d_ui_scroll_wheel). In game with no
 * window open each notch runs the command bound to "mwheelup" / "mwheeldown" (sys/controls.cfg: invprev / invnext) */
void cs2d_client_handle_mouse_wheel(cs2d_client_t *client, int delta_y)
{
    if (!client) return;
    if (client->scene == CS2D_SCENE_GAME && !cs2d_client_any_ui_open(client)) {
        const char *cmd = cs2d_binds_find(&client->binds, delta_y > 0 ? "mwheelup" : "mwheeldown");
        for (int i = 0; cmd && i < abs(delta_y); i++) cs2d_scene_game_inv_cmd(client, cmd);
        return;
    }
    client->ui_wheel_delta += delta_y;
}

void cs2d_client_handle_key(cs2d_client_t *client, int key, bool pressed)
{
    if (!client) return;
    if (cs2d_client_any_ui_open(client)) {
        client->show_scores = false; /* The key release goes to the window */
        if (pressed && !client->console_open && !client->create_server_open && !client->options_open) {
            cs2d_client_handle_console_key(client, tolower((unsigned char)key));
        }
        return;
    }
    if (client->scene == CS2D_SCENE_GAME && pressed && (key == 'M' || key == 'm')) {
        client->team_menu_open = true;
        client->team_menu_page = 0;
        client->key_forward    = false;
        client->key_back       = false;
        client->key_left       = false;
        client->key_right      = false;
        client->mouse_fire     = false;
        client->mouse_attack2  = false;
        return;
    }
    cs2d_scene_get(client->scene)->handle_key(client, key, pressed);
}

void cs2d_client_handle_mouse(cs2d_client_t *client, float mx, float my, int button, bool pressed)
{
    if (!client) return;

    client->mouse_screen_x = mx;
    client->mouse_screen_y = my;

    if (cs2d_client_any_ui_open(client)) {
        if (button == 1) {
            client->mouse_fire = pressed;
        }
        client->mouse_attack2 = false;
        return;
    }

    cs2d_scene_get(client->scene)->handle_mouse(client, mx, my, button, pressed);
}

void cs2d_client_update(cs2d_client_t *client, float delta_time)
{
    if (!client) return;

    /* Console commands submitted with Enter run here, outside the UI build */
    if (client->console_pending_cmd[0] != '\0') {
        char cmd[CS2D_CONSOLE_LINE_LEN];
        snprintf(cmd, sizeof(cmd), "%s", client->console_pending_cmd);
        client->console_pending_cmd[0] = '\0';
        cs2d_client_console_exec(client, cmd);
    }

    if (client->chat_just_opened) {
        client->chat_just_opened = false;
    }

    apply_pending_scene(client);

    /* LeaderboardRequest.update runs once per 60 Hz frame */
    client->lb_frame_accum += delta_time * 60.0f;
    if (client->lb_frame_accum > 4.0f) client->lb_frame_accum = 4.0f;
    for (; client->lb_frame_accum >= 1.0f; client->lb_frame_accum -= 1.0f) {
        if (client->connected) leaderboard_update(client);
    }

    /* Server browser queries (pingServers) for the visible tab, LAN scan and replies */
    if (client->servers_open) {
        cs2d_sv_browser_update(&client->servers, client->servers_tab, (uint32_t)GetTickCount(), delta_time * 60.0f);
    } else if (client->servers.open && !client->connect_ip_open) {
        cs2d_sv_browser_close(&client->servers);
    }

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

    cs2d_scene_get(client->scene)->update(client, delta_time);
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

/* A console line whose inline "©RRRGGGBBB" codes switch colour mid-line: one text run per colour */
static void console_text_colored(cs2d_ui_context_t *ui, const char *text, uint8_t r, uint8_t g, uint8_t b)
{
    const char *p = text;
    bool multi = false;
    for (const char *q = text; *q && !multi; q++) {
        multi = cs2d_parse_color_code(q, NULL, NULL, NULL, NULL);
    }
    if (!multi) {
        cs2d_ui_text(ui, text, CS2D_FONT_REGULAR, CS2D_UI_RGB(r, g, b));
        return;
    }
    CS2D_UI_ELEMENT(ui, 0, {
        .layout = {
            .sizing    = { CS2D_UI_FIT(0, 0), CS2D_UI_FIT(0, 0) },
            .child_gap = 0.0f,
            .dir       = CS2D_UI_LEFT_TO_RIGHT
        }
    }) {
        char seg[CS2D_CHAT_LINE_LEN + 32];
        while (*p) {
            const char *next;
            uint8_t nr, ng, nb;
            if (cs2d_parse_color_code(p, &nr, &ng, &nb, &next)) { r = nr; g = ng; b = nb; p = next; continue; }
            size_t n = 0;
            while (p[n] && n < sizeof(seg) - 1) {
                if (n > 0 && cs2d_parse_color_code(p + n, NULL, NULL, NULL, NULL)) break;
                n++;
            }
            memcpy(seg, p, n);
            seg[n] = '\0';
            cs2d_ui_text(ui, seg, CS2D_FONT_REGULAR, CS2D_UI_RGB(r, g, b));
            p += n;
        }
    }
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
            int total = client->console_line_count;

            /* A view at the bottom stays there when the window changes height */
            if (client->console_scroll.top >= cs2d_ui_scroll_max(console_visible(client), total)) {
                client->console_scroll.top = cs2d_ui_scroll_max(max_vis, total);
            }
            client->console_visible_lines = max_vis;
            cs2d_ui_scroll_wheel(ui, &client->console_scroll, NULL, 3, max_vis, total);
            cs2d_ui_scroll_clamp(&client->console_scroll, max_vis, total);

            int start_idx = client->console_scroll.top;
            int end_idx   = start_idx + max_vis;
            if (end_idx > total) end_idx = total;

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
                    const cs2d_console_line_t *ln = cs2d_client_console_line(client, i);
                    console_text_colored(ui, ln->text, ln->r, ln->g, ln->b);
                }
            }

            /* Right: vertical scrollbar (top = oldest line) */
            cs2d_ui_scrollbar(ui, CS2D_UI_ID("ConsoleScrollbar"), &client->console_scroll, max_vis, total, log_h, NULL);
        }

        /* 2. Bottom command input textbox */
        if (ui->focused_id == 0) {
            ui->focused_id = CS2D_UI_ID("ConsoleInput");
        }
        if (cs2d_ui_textbox(ui, CS2D_UI_ID("ConsoleInput"),
                            client->console_input, sizeof(client->console_input),
                            &client->console_cursor, 0.0f, false)) {
            console_submit(client);
        }

        cs2d_ui_window_end(ui);
    }
}

static int maplist_cmp(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* Append a group: title row, then its maps sorted (Tgui_ml::Load sorts each group, FUN_1408d9e30) */
static void maplist_add_group(cs2d_maplist_t *ml, const char *title, char **maps, int n)
{
    if (n <= 0) return;
    qsort(maps, (size_t)n, sizeof(maps[0]), maplist_cmp);
    if (ml->count >= CS2D_MAPLIST_MAX_ROWS) return;
    snprintf(ml->rows[ml->count].name, sizeof(ml->rows[0].name), "%s", title);
    ml->rows[ml->count++].header = true;
    for (int i = 0; i < n && ml->count < CS2D_MAPLIST_MAX_ROWS; i++) {
        snprintf(ml->rows[ml->count].name, sizeof(ml->rows[0].name), "%s", maps[i]);
        ml->rows[ml->count++].header = false;
    }
}

static char *maplist_trim(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r' || s[len - 1] == '\n')) {
        s[--len] = '\0';
    }
    return s;
}

int cs2d_maplist_load(cs2d_maplist_t *ml, const char *maps_dir, const char *types_path)
{
    if (!ml) return 0;
    ml->count = 0;

    /* sys/maptypes.lst (FUN_1401a4610): "<prefix><tab or space><title>", both sides trimmed, empty ones skipped */
    static char type_prefix[CS2D_MAPLIST_MAX_TYPES][16];
    static char type_title[CS2D_MAPLIST_MAX_TYPES][64];
    int ntypes = 0;
    FILE *f = types_path ? fopen(types_path, "rb") : NULL;
    if (f) {
        char line[256];
        while (ntypes < CS2D_MAPLIST_MAX_TYPES && fgets(line, sizeof(line), f)) {
            char *sep = strpbrk(line, "\t ");
            if (!sep) continue;
            *sep = '\0';
            char *prefix = maplist_trim(line);
            char *title  = maplist_trim(sep + 1);
            if (!prefix[0] || !title[0]) continue;
            snprintf(type_prefix[ntypes], sizeof(type_prefix[0]), "%s", prefix);
            snprintf(type_title[ntypes], sizeof(type_title[0]), "%s", title);
            ntypes++;
        }
        fclose(f);
    }

    /* Tgui_ml::Load: every file of maps/ whose lowercase extension is "map", except temp.map and
     * autosave.map, stored as Lower(StripExt(name)) */
    static char names[CS2D_MAPLIST_MAX_ROWS][64];
    int nnames = 0;
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*", maps_dir ? maps_dir : "maps");
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            if (strcmp(fd.cFileName, "temp.map") == 0 || strcmp(fd.cFileName, "autosave.map") == 0) continue;
            const char *dot = strrchr(fd.cFileName, '.');
            if (!dot || _stricmp(dot + 1, "map") != 0) continue;
            size_t len = (size_t)(dot - fd.cFileName);
            if (len == 0 || len >= sizeof(names[0]) || nnames >= CS2D_MAPLIST_MAX_ROWS) continue;
            for (size_t i = 0; i < len; i++) names[nnames][i] = (char)tolower((unsigned char)fd.cFileName[i]);
            names[nnames][len] = '\0';
            nnames++;
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }

    /* "All Maps" view: one group per map type in file order (a map joins the first prefix it starts with),
     * then the rest under "Other Maps" (sys/language 0182) */
    static char *group[CS2D_MAPLIST_MAX_ROWS];
    bool taken[CS2D_MAPLIST_MAX_ROWS] = {0};
    for (int t = 0; t < ntypes; t++) {
        int n = 0;
        size_t plen = strlen(type_prefix[t]);
        for (int i = 0; i < nnames; i++) {
            if (!taken[i] && strncmp(names[i], type_prefix[t], plen) == 0) {
                taken[i] = true;
                group[n++] = names[i];
            }
        }
        maplist_add_group(ml, type_title[t], group, n);
    }
    int n = 0;
    for (int i = 0; i < nnames; i++) {
        if (!taken[i]) group[n++] = names[i];
    }
    maplist_add_group(ml, "Other Maps", group, n);
    return nnames;
}

static void cs2d_client_build_create_server_ui(cs2d_client_t *client, int screen_w, int screen_h)
{
    if (!client->create_server_open) {
        client->srv_maplist_loaded = false;
        return;
    }
    if (!client->srv_maplist_loaded) {
        cs2d_maplist_load(&client->srv_maplist, "maps", "sys/maptypes.lst");
        client->srv_maplist_loaded = true;
        client->srv_maplist_scroll.top = 0;

        /* Tgui_ml::Load picks the first map of the list when the selection is empty; we also do it when
         * the selected map is not in maps/ */
        bool found = false;
        for (int i = 0; i < client->srv_maplist.count && !found; i++) {
            const cs2d_maplist_row_t *r = &client->srv_maplist.rows[i];
            found = !r->header && strcmp(r->name, client->srv_selected_map) == 0;
        }
        for (int i = 0; i < client->srv_maplist.count && !found; i++) {
            const cs2d_maplist_row_t *r = &client->srv_maplist.rows[i];
            if (!r->header) {
                snprintf(client->srv_selected_map, sizeof(client->srv_selected_map), "%s", r->name);
                found = true;
            }
        }
    }

    cs2d_ui_context_t *ui = &client->ui;
    float win_w = 460.0f;
    float win_h = 420.0f;
    float win_x = floorf(((float)screen_w - win_w) * 0.5f);
    float win_y = floorf(((float)screen_h - win_h) * 0.5f);

    static const char *srv_tabs[] = { "Server", "Map", "Settings", "Bots", "Mods", "More Settings" };
    static const char *gm_opts[]     = { "Standard", "Deathmatch", "Team Deathmatch", "Construction", "Zombies!" };
    static const char *fow_opts[]    = { "Off", "On", "Line of Sight" };

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
                    cs2d_ui_numberbox(ui, CS2D_UI_ID("SrvPortInput"), &client->srv_port, 1, 65535, 68.0f);
                    ui_form_row_end(ui);

                    ui_form_row_begin(ui, "Max. Players", 105.0f);
                    cs2d_ui_numberbox(ui, CS2D_UI_ID("SrvMaxPlayers"), &client->srv_max_players, 1, 32, 40.0f);
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

                            /* Rows that fit (Tgui_ml scrolls in 17 px rows, scrv 0x11). Our headers are 18 px,
                             * so count 19 px per row to never cut the last one. Fixed height: fills the tab */
                            const float list_h = 300.0f;
                            cs2d_ui_rect_t list_box = cs2d_ui_get_box(ui, CS2D_UI_ID("MapListRow"));
                            int total   = client->srv_maplist.count;
                            int max_vis = (int)((list_h - 4.0f) / 19.0f);
                            cs2d_ui_scroll_wheel(ui, &client->srv_maplist_scroll, &list_box, 3, max_vis, total);
                            cs2d_ui_scroll_clamp(&client->srv_maplist_scroll, max_vis, total);
                            int first = client->srv_maplist_scroll.top;
                            /* Fill the box with the real row heights (16 px items, 18 px headers, 1 px gap)
                             * instead of the conservative 19 px estimate, which left a gap at the bottom */
                            int last = first;
                            float used = 0.0f;
                            while (last < total) {
                                float h = (client->srv_maplist.rows[last].header ? 18.0f : 16.0f)
                                        + (last > first ? 1.0f : 0.0f);
                                if (used + h > list_h - 4.0f) break;
                                used += h;
                                last++;
                            }
                            if (last - first > max_vis) max_vis = last - first;

                            CS2D_UI_ELEMENT(ui, CS2D_UI_ID("MapListRow"), {
                                .layout = {
                                    .sizing    = { CS2D_UI_GROW(0, 0), CS2D_UI_FIXED(list_h) },
                                    .child_gap = 1.0f,
                                    .dir       = CS2D_UI_LEFT_TO_RIGHT
                                }
                            }) {
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
                                    for (int m = first; m < last; m++) {
                                        const cs2d_maplist_row_t *row = &client->srv_maplist.rows[m];
                                        if (row->header) {
                                            cs2d_ui_section_header(ui, CS2D_UI_IDI("MapHdr", m), row->name);
                                            continue;
                                        }
                                        cs2d_ui_id_t mid = CS2D_UI_IDI("MapItem", m);
                                        bool sel = (strcmp(client->srv_selected_map, row->name) == 0);
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
                                            cs2d_ui_text(ui, row->name, CS2D_FONT_REGULAR,
                                                         sel ? CS2D_UI_RGB(255, 220, 0) : CS2D_UI_RGB(190, 190, 190));
                                        }
                                        if (cs2d_ui_clicked(ui, mid)) {
                                            snprintf(client->srv_selected_map, sizeof(client->srv_selected_map), "%s", row->name);
                                        }
                                    }
                                }
                                cs2d_ui_scrollbar(ui, CS2D_UI_ID("MapListScrollbar"), &client->srv_maplist_scroll,
                                                  max_vis, total, list_h, NULL);
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
                                cs2d_ui_text(ui, client->srv_selected_map, CS2D_FONT_REGULAR, CS2D_UI_RGB(190, 190, 190));
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

            if (cs2d_ui_button(ui, CS2D_UI_ID("BtnSrvStart"), "Start", 76.0f, 19.0f) && client->srv_selected_map[0]) {
                char cmd[128];
                snprintf(cmd, sizeof(cmd), "map %s", client->srv_selected_map);
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

/*
 * 240x25 numbered menu option button used by "Select Team" and its Terrorists /
 * Counter-Terrorists skin subcategories (verified from reference screenshots):
 *   - Left 18px slot: grey key number ('1'..'6', '0') or blank ("Back")
 *   - Label in white/light-grey
 *   - Optional right-aligned player count "(N)" in grey when count >= 0
 */
static bool ui_menu_option_button(
    cs2d_ui_context_t *ui,
    cs2d_ui_id_t id,
    const char *key_num,
    const char *label,
    int count
)
{
    bool hov  = cs2d_ui_hovered(ui, id);
    bool down = (hov && ui->mouse_down && ui->active_id == id);

    cs2d_ui_color_t top_col = down ? CS2D_UI_RGBA(25, 25, 25, 250)
                                   : (hov ? CS2D_UI_RGBA(58, 58, 58, 250) : CS2D_UI_RGBA(38, 38, 38, 250));
    cs2d_ui_color_t bot_col = down ? CS2D_UI_RGBA(35, 35, 35, 250)
                                   : (hov ? CS2D_UI_RGBA(38, 38, 38, 250) : CS2D_UI_RGBA(22, 22, 22, 250));
    cs2d_ui_color_t brd_col = hov  ? CS2D_UI_RGBA(185, 185, 185, 255) : CS2D_UI_RGBA(115, 115, 115, 245);

    CS2D_UI_ELEMENT(ui, id, {
        .layout = {
            .sizing  = { CS2D_UI_FIXED(240.0f), CS2D_UI_FIXED(25.0f) },
            .padding = CS2D_UI_PAD(7, 8, 0, 0),
            .dir     = CS2D_UI_LEFT_TO_RIGHT,
            .align_y = CS2D_UI_ALIGN_Y_CENTER
        },
        .bg_color           = top_col,
        .bg_gradient_bottom = bot_col,
        .border             = { .width = 1.0f, .color = brd_col }
    }) {
        CS2D_UI_ELEMENT(ui, 0, {
            .layout = {
                .sizing  = { CS2D_UI_FIXED(18.0f), CS2D_UI_FIXED(16.0f) },
                .align_y = CS2D_UI_ALIGN_Y_CENTER
            }
        }) {
            if (key_num && key_num[0] != '\0') {
                cs2d_ui_text(ui, key_num, CS2D_FONT_REGULAR, CS2D_UI_RGB(160, 160, 160));
            }
        }

        cs2d_ui_text(ui, label ? label : "", CS2D_FONT_REGULAR,
                     hov ? CS2D_UI_RGB(255, 255, 255) : CS2D_UI_RGB(230, 230, 230));

        if (count >= 0) {
            char cnt_buf[16];
            snprintf(cnt_buf, sizeof(cnt_buf), "(%d)", count);

            CS2D_UI_ELEMENT(ui, 0, {
                .layout = { .sizing = { CS2D_UI_GROW(0, 0), CS2D_UI_FIXED(1.0f) } }
            }) {}

            cs2d_ui_text(ui, cnt_buf, CS2D_FONT_REGULAR, CS2D_UI_RGB(160, 160, 160));
        }
    }

    return cs2d_ui_clicked(ui, id);
}

/*
 * Builds the 600x440 "Select Team" window and its Terrorists / Counter-Terrorists
 * character selection subcategories (Screenshots 2 & 3).
 * Hidden while "<Server Name> - Info" is open on top so the semi-transparent
 * server info window renders cleanly over the map and reveals "Select Team" on close.
 */
static void cs2d_client_build_team_menu_ui(cs2d_client_t *client, int screen_w, int screen_h)
{
    if (!client->team_menu_open || client->server_info_open) return;

    cs2d_ui_context_t *ui = &client->ui;
    float win_w = 600.0f;
    float win_h = 440.0f;
    float win_x = floorf(((float)screen_w - win_w) * 0.5f);
    float win_y = floorf(((float)screen_h - win_h) * 0.5f);

    const char *win_title = "Select Team";
    if (client->team_menu_page == 1) {
        win_title = "Select Team - Terrorists";
    } else if (client->team_menu_page == 2) {
        win_title = "Select Team - Counter-Terrorists";
    }

    static const char *t_classes[4] = {
        "Phoenix Connexion",
        "L337 Krew",
        "Arctic Avengers",
        "Guerilla Warfare"
    };
    static const char *ct_classes[4] = {
        "Seal Team 6 (DEVGRU)",
        "German GSG-9",
        "UK SAS",
        "French GIGN"
    };
    static const char *slot_keys[4] = { "1", "2", "3", "4" };

    if (cs2d_ui_window_begin(ui, CS2D_UI_ID("SelectTeamWin"), win_title,
                             win_x, win_y, win_w, win_h, &client->team_menu_open)) {
        /* 1px horizontal divider separating the title bar from the window body */
        CS2D_UI_ELEMENT(ui, 0, {
            .layout   = { .sizing = { CS2D_UI_GROW(0, 0), CS2D_UI_FIXED(1.0f) } },
            .bg_color = CS2D_UI_RGBA(96, 96, 96, 245)
        }) {}

        CS2D_UI_ELEMENT(ui, CS2D_UI_ID("TeamMenuBody"), {
            .layout = {
                .sizing    = { CS2D_UI_GROW(0, 0), CS2D_UI_GROW(0, 0) },
                .padding   = CS2D_UI_PAD(74, 10, 72, 9),
                .child_gap = 5.0f,
                .dir       = CS2D_UI_TOP_TO_BOTTOM
            }
        }) {
            int count_t  = 0;
            int count_ct = 0;
            int t_looks[4]  = { 0, 0, 0, 0 };
            int ct_looks[4] = { 0, 0, 0, 0 };
            count_team_players(client, &count_t, &count_ct, t_looks, ct_looks);

            if (client->team_menu_page == 0) {
                if (ui_menu_option_button(ui, CS2D_UI_ID("BtnTeamT"), "1", "Terrorists", count_t)) {
                    client->team_menu_page = 1;
                    client->spec.idle_cam = false;
                }
                if (ui_menu_option_button(ui, CS2D_UI_ID("BtnTeamCT"), "2", "Counter-Terrorists", count_ct)) {
                    client->team_menu_page = 2;
                    client->spec.idle_cam = false;
                }

                /* Empty 25px slot between team options and Auto-Select / Spectator */
                CS2D_UI_ELEMENT(ui, 0, {
                    .layout = { .sizing = { CS2D_UI_FIXED(240.0f), CS2D_UI_FIXED(25.0f) } }
                }) {}

                /* Auto-Select and Spectator have no subcategory: they select immediately */
                if (ui_menu_option_button(ui, CS2D_UI_ID("BtnTeamAuto"), "5", "Auto-Select", -1)) {
                    cs2d_client_select_team(client, 4, 4);
                }
                if (ui_menu_option_button(ui, CS2D_UI_ID("BtnTeamSpec"), "6", "Spectator", -1)) {
                    cs2d_client_select_team(client, CS2D_TEAM_SPEC, 5);
                }

                CS2D_UI_ELEMENT(ui, 0, {
                    .layout = { .sizing = { CS2D_UI_FIXED(240.0f), CS2D_UI_GROW(0, 0) } }
                }) {}

                if (ui_menu_option_button(ui, CS2D_UI_ID("BtnTeamClose"), "0", "Close", -1)) {
                    client->team_menu_open = false;
                    client->team_menu_page = 0;
                    client->spec.idle_cam = false;
                }
            } else {
                uint8_t team = (client->team_menu_page == 1) ? CS2D_TEAM_T : CS2D_TEAM_CT;
                const char **classes = (team == CS2D_TEAM_T) ? t_classes : ct_classes;
                const int   *looks   = (team == CS2D_TEAM_T) ? t_looks   : ct_looks;

                for (int i = 0; i < 4; i++) {
                    if (ui_menu_option_button(ui, CS2D_UI_IDI("BtnTeamSkin", i),
                                              slot_keys[i], classes[i], looks[i])) {
                        cs2d_client_select_team(client, team, (uint8_t)i);
                    }
                }

                /* Empty 25px slot before 5 Auto-Select */
                CS2D_UI_ELEMENT(ui, 0, {
                    .layout = { .sizing = { CS2D_UI_FIXED(240.0f), CS2D_UI_FIXED(25.0f) } }
                }) {}

                if (ui_menu_option_button(ui, CS2D_UI_ID("BtnSkinAuto"), "5", "Auto-Select", -1)) {
                    cs2d_client_select_team(client, team, 4);
                }

                CS2D_UI_ELEMENT(ui, 0, {
                    .layout = { .sizing = { CS2D_UI_FIXED(240.0f), CS2D_UI_GROW(0, 0) } }
                }) {}

                if (ui_menu_option_button(ui, CS2D_UI_ID("BtnSkinBack"), "", "Back", -1)) {
                    client->team_menu_page = 0;
                    client->spec.idle_cam = false;
                }
                if (ui_menu_option_button(ui, CS2D_UI_ID("BtnSkinClose"), "0", "Close", -1)) {
                    client->team_menu_open = false;
                    client->team_menu_page = 0;
                    client->spec.idle_cam = false;
                }
            }
        }

        cs2d_ui_window_end(ui);
    }

    if (!client->team_menu_open) {
        client->team_menu_page = 0;
    }
}

/*
 * Builds the 600x440 "<Server Name> - Info" welcome / MOTD window (Screenshot 1):
 *   - 553x374 scrollable text box + 15x374 scrollbar on the right
 *   - Full-width 568x20 "Close" button at the bottom
 */
static void build_text_window(cs2d_client_t *client, int screen_w, int screen_h, int kind, const char *win_title,
                              const cs2d_console_line_t *lines, int total, cs2d_ui_scroll_t *scroll,
                              int *visible_lines, bool *open)
{
    if (!*open) return;

    cs2d_ui_context_t *ui = &client->ui;
    float win_w = 600.0f;
    float win_h = 440.0f;
    float win_x = floorf(((float)screen_w - win_w) * 0.5f);
    float win_y = floorf(((float)screen_h - win_h) * 0.5f);

    if (cs2d_ui_window_begin(ui, CS2D_UI_IDI("ServerInfoWin", kind), win_title,
                             win_x, win_y, win_w, win_h, open)) {
        /* 1px horizontal divider separating the title bar from the window body */
        CS2D_UI_ELEMENT(ui, 0, {
            .layout   = { .sizing = { CS2D_UI_GROW(0, 0), CS2D_UI_FIXED(1.0f) } },
            .bg_color = CS2D_UI_RGBA(96, 96, 96, 245)
        }) {}

        CS2D_UI_ELEMENT(ui, CS2D_UI_IDI("ServerInfoBody", kind), {
            .layout = {
                .sizing    = { CS2D_UI_GROW(0, 0), CS2D_UI_GROW(0, 0) },
                .padding   = CS2D_UI_PAD(10, 10, 2, 4),
                .child_gap = 6.0f,
                .dir       = CS2D_UI_TOP_TO_BOTTOM
            }
        }) {
            CS2D_UI_ELEMENT(ui, CS2D_UI_IDI("ServerInfoRow", kind), {
                .layout = {
                    .sizing    = { CS2D_UI_GROW(0, 0), CS2D_UI_GROW(0, 0) },
                    .child_gap = 0.0f,
                    .dir       = CS2D_UI_LEFT_TO_RIGHT
                }
            }) {
                float box_h        = 374.0f;
                const float line_h = 15.0f;
                int max_vis = (int)((box_h - 8.0f) / line_h);
                if (max_vis < 1) max_vis = 1;
                *visible_lines = max_vis;

                if (!client->console_open) {
                    cs2d_ui_rect_t win = cs2d_ui_get_box(ui, CS2D_UI_IDI("ServerInfoWin", kind));
                    cs2d_ui_scroll_wheel(ui, scroll, &win, 3, max_vis, total);
                }
                cs2d_ui_scroll_clamp(scroll, max_vis, total);

                int start_idx = scroll->top;
                int end_idx   = start_idx + max_vis;
                if (end_idx > total) end_idx = total;

                /* Left 553x374 dark text box */
                CS2D_UI_ELEMENT(ui, CS2D_UI_IDI("ServerInfoBox", kind), {
                    .layout = {
                        .sizing    = { CS2D_UI_GROW(0, 0), CS2D_UI_GROW(0, 0) },
                        .padding   = CS2D_UI_PAD(6, 6, 4, 4),
                        .child_gap = 0.0f,
                        .dir       = CS2D_UI_TOP_TO_BOTTOM
                    },
                    .bg_color = CS2D_UI_RGBA(28, 28, 27, 246),
                    .border   = { .width = 1.0f, .color = CS2D_UI_RGBA(140, 140, 140, 245) },
                    .clip     = { .enabled = true }
                }) {
                    for (int i = start_idx; i < end_idx; i++) {
                        const cs2d_console_line_t *ln = &lines[i];
                        if (ln->text[0] == '\0') {
                            CS2D_UI_ELEMENT(ui, 0, {
                                .layout = { .sizing = { CS2D_UI_FIXED(1.0f), CS2D_UI_FIXED(line_h) } }
                            }) {}
                        } else {
                            cs2d_ui_text(ui, ln->text, CS2D_FONT_REGULAR, CS2D_UI_RGB(ln->r, ln->g, ln->b));
                        }
                    }
                }

                /* Right 15x374 scrollbar (top = line 0) */
                static const cs2d_ui_scrollbar_style_t sb_style = {
                    .width = 15.0f, .pad = 2.0f, .min_thumb = 24.0f,
                    .track_bg = { 32, 32, 32, 246 }, .track_border = { 140, 140, 140, 245 },
                    .thumb = { 66, 66, 66, 245 }, .thumb_hot = { 104, 104, 104, 250 },
                    .thumb_border = { 140, 140, 140, 245 }
                };
                cs2d_ui_scrollbar(ui, CS2D_UI_IDI("ServerInfoScrollbar", kind), scroll, max_vis, total,
                                  box_h, &sb_style);
            }

            if (cs2d_ui_button(ui, CS2D_UI_IDI("BtnServerInfoClose", kind), "Close", 568.0f, 20.0f)) {
                *open = false;
            }
        }

        cs2d_ui_window_end(ui);
    }
}

static void cs2d_client_build_server_info_ui(cs2d_client_t *client, int screen_w, int screen_h)
{
    char title[96];
    snprintf(title, sizeof(title), "%s - Info", client->net_server_name[0] ? client->net_server_name : "CS2D Server");
    build_text_window(client, screen_w, screen_h, 0, title, client->server_info_lines, client->server_info_line_count,
                      &client->server_info_scroll, &client->server_info_visible_lines, &client->server_info_open);
}

/* "Map Briefing" (lang 0727): the same window as Server Info (Tgui_tb (16,25) 568x374, Close (16,404) 568, 0x1401a6850) */
static void cs2d_client_build_briefing_ui(cs2d_client_t *client, int screen_w, int screen_h)
{
    build_text_window(client, screen_w, screen_h, 1, "Map Briefing", client->briefing_lines, client->briefing_line_count,
                      &client->briefing_scroll, &client->briefing_visible_lines, &client->briefing_open);
}

/* Floating text at an absolute position over a window (z 11, above the window's z 10) */
static void lb_text(cs2d_ui_context_t *ui, float x, float y, float w, const char *text, cs2d_ui_color_t c)
{
    CS2D_UI_ELEMENT(ui, 0, {
        .layout   = { .sizing = { CS2D_UI_FIXED(w), CS2D_UI_FIXED(16.0f) } },
        .floating = { .enabled = true, .x = x, .y = y, .z_index = 11 },
        .clip     = { .enabled = true }
    }) {
        cs2d_ui_text(ui, text, CS2D_FONT_REGULAR, c);
    }
}

/*
 * "Server Leaderboard" window (0x1401a6c00): Tgui_leadlist (16,26) 568x340 with a header row, 20 px rows,
 * scroll 1..100 (Tgui_leadlist.draw 0x1402e3150), Close (16,404) 568. Columns: rank +5, name +55 (squeezed to
 * 120 px, MVP star after it), score +210, kills +270, assists +330, deaths +390, time played +460.
 * The avatars (+36, 18x18) are not drawn.
 */
static void cs2d_client_build_leaderboard_ui(cs2d_client_t *client, int screen_w, int screen_h)
{
    if (!client->leaderboard_open) return;
    cs2d_ui_context_t *ui = &client->ui;
    float win_x = floorf(((float)screen_w - 600.0f) * 0.5f);
    float win_y = floorf(((float)screen_h - 440.0f) * 0.5f);
    const int rows = 16;

    if (cs2d_ui_window_begin(ui, CS2D_UI_ID("LeaderboardWin"), "Server Leaderboard", win_x, win_y, 600.0f, 440.0f,
                             &client->leaderboard_open)) {
        CS2D_UI_ELEMENT(ui, 0, {
            .layout   = { .sizing = { CS2D_UI_GROW(0, 0), CS2D_UI_FIXED(1.0f) } },
            .bg_color = CS2D_UI_RGBA(96, 96, 96, 245)
        }) {}
        float lx = win_x + 16.0f, ly = win_y + 26.0f;
        const cs2d_ui_color_t gray = CS2D_UI_RGB(150, 150, 150);
        static const char *heads[] = { "Player", "Score", "Kills", "Assists", "Deaths", "Time Played" };
        static const float cols[]  = { 35.0f, 210.0f, 270.0f, 330.0f, 390.0f, 460.0f };
        for (int i = 0; i < 6; i++) lb_text(ui, lx + cols[i], ly, 100.0f, heads[i], gray);

        /* Rows box: black at 0.5 with a 150 frame */
        CS2D_UI_ELEMENT(ui, 0, {
            .layout   = { .sizing = { CS2D_UI_FIXED(552.0f), CS2D_UI_FIXED(320.0f) } },
            .floating = { .enabled = true, .x = lx, .y = ly + 20.0f, .z_index = 11 },
            .bg_color = CS2D_UI_RGBA(0, 0, 0, 128),
            .border   = { .width = 1.0f, .color = gray }
        }) {}

        cs2d_ui_rect_t area = { lx, ly + 20.0f, 568.0f, 320.0f };
        cs2d_ui_scroll_wheel(ui, &client->lb_scroll, &area, 1, rows, CS2D_LEADERBOARD_MAX);
        cs2d_ui_scroll_clamp(&client->lb_scroll, rows, CS2D_LEADERBOARD_MAX);
        int top = client->lb_scroll.top;
        leaderboard_request_rows(client, top, rows);

        for (int i = 0; i < rows; i++) {
            int rank = top + 1 + i;
            if (rank > CS2D_LEADERBOARD_MAX) break;
            const cs2d_lb_row_t *r = &client->lb_rows[rank];
            if (!client->lb_requested[rank]) continue;
            float y = ly + 21.0f + 20.0f * (float)i;
            char buf[48];
            snprintf(buf, sizeof(buf), "%d", rank);
            lb_text(ui, lx + 5.0f, y, 48.0f, buf, gray);
            if (!r->have) continue;
            if (r->name[0] == '\0') {
                lb_text(ui, lx + 55.0f, y, 150.0f, "???", gray);
                continue;
            }
            float nw = cs2d_render_text_width_ex(CS2D_FONT_REGULAR, r->name);
            lb_text(ui, lx + 55.0f, y, 120.0f, r->name, gray);
            if (r->mvp != 0) {
                /* MVP: star (gui_icons frame 19 in the original) and the count in (255,200,0) */
                snprintf(buf, sizeof(buf), "*%d", r->mvp);
                lb_text(ui, lx + 57.0f + fminf(nw, 120.0f), y - 2.0f, 50.0f, buf, CS2D_UI_RGB(255, 200, 0));
            }
            snprintf(buf, sizeof(buf), "%d", r->score);   lb_text(ui, lx + 210.0f, y, 60.0f, buf, gray);
            snprintf(buf, sizeof(buf), "%d", r->kills);   lb_text(ui, lx + 270.0f, y, 60.0f, buf, gray);
            snprintf(buf, sizeof(buf), "%d", r->assists); lb_text(ui, lx + 330.0f, y, 60.0f, buf, gray);
            snprintf(buf, sizeof(buf), "%d", r->deaths);  lb_text(ui, lx + 390.0f, y, 70.0f, buf, gray);
            cs2d_format_playtime(r->time_played, buf, sizeof(buf));
            lb_text(ui, lx + 460.0f, y, 90.0f, buf, gray);
        }

        static const cs2d_ui_scrollbar_style_t sb_style = {
            .width = 16.0f, .pad = 2.0f, .min_thumb = 24.0f,
            .track_bg = { 32, 32, 32, 246 }, .track_border = { 140, 140, 140, 245 },
            .thumb = { 66, 66, 66, 245 }, .thumb_hot = { 104, 104, 104, 250 },
            .thumb_border = { 140, 140, 140, 245 }
        };
        cs2d_ui_scrollbar_at(ui, CS2D_UI_ID("LeaderboardScroll"), &client->lb_scroll, rows, CS2D_LEADERBOARD_MAX,
                             lx + 552.0f, ly + 20.0f, 320.0f, 11, &sb_style);

        /* Below the list: "Last updated <date>" (lang 0963) in 155, or "Leaderboard is unavailable" (0962) in red */
        char foot[96];
        cs2d_ui_color_t fc = CS2D_UI_RGB(155, 155, 155);
        if (client->lb_timestamp < 1) {
            snprintf(foot, sizeof(foot), "Leaderboard is unavailable");
            fc = CS2D_UI_RGB(255, 0, 0);
        } else {
            char date[32];
            cs2d_format_timestamp(client->lb_timestamp, date, sizeof(date));
            snprintf(foot, sizeof(foot), "Last updated %s", date);
        }
        float fw = cs2d_render_text_width_ex(CS2D_FONT_REGULAR, foot);
        lb_text(ui, floorf(lx + 284.0f - fw * 0.5f), ly + 350.0f, fw + 4.0f, foot, fc);

        CS2D_UI_ELEMENT(ui, 0, {
            .layout = { .sizing = { CS2D_UI_GROW(0, 0), CS2D_UI_GROW(0, 0) } }
        }) {}
        CS2D_UI_ELEMENT(ui, 0, {
            .layout = { .sizing = { CS2D_UI_GROW(0, 0), CS2D_UI_FIT(0, 0) }, .padding = CS2D_UI_PAD(10, 10, 0, 4) }
        }) {
            if (cs2d_ui_button(ui, CS2D_UI_ID("BtnLeaderboardClose"), "Close", 568.0f, 20.0f)) {
                client->leaderboard_open = false;
            }
        }
        cs2d_ui_window_end(ui);
    }
}

/*
 * "Servers" window (Find Servers), Tgui_sl (draw 0x14016b5d0, draw_stats 0x1401083f0).
 * Geometry measured on CS2D.exe at 850x480: window (5,5) 840x470, tabs at y 30..50, column header
 * 50..75, list box (21,75)-(838,433) with 15 px rows and a 15 px scrollbar, buttons 20 px high at
 * y 439: Filters x 21, Refresh x 126 (100 wide), Connect to IP x 604 (120), Connect x 729 (100).
 */
#define SERVERS_ROW_H      15.0f
#define SERVERS_LOADING_MS 15000u  /* "Failed to load online serverlist within 15 sec" */

static int servers_visible(cs2d_client_t *client, cs2d_sv_t **out, int max_out)
{
    cs2d_sv_list_t *list = &client->servers.lists[client->servers_tab];
    cs2d_sv_sort_by_humans(list);
    int n = 0;
    for (int i = 0; i < list->count && n < max_out; i++) {
        if (list->entries[i].active) out[n++] = &list->entries[i];
    }
    return n;
}

static void servers_clamp_scroll(cs2d_client_t *client, int total)
{
    cs2d_ui_scroll_clamp(&client->servers_scroll, client->servers_visible_rows, total);
}

/* List scrollbar: 15 px track inside the right border (Tgui_sl.draw 0x14016b5d0) */
static const cs2d_ui_scrollbar_style_t s_servers_scrollbar = {
    .width = 16.0f, .pad = 2.0f, .min_thumb = 24.0f,
    .track_bg = { 6, 6, 6, 255 }, .track_border = { 150, 150, 150, 255 },
    .thumb = { 53, 53, 53, 255 }, .thumb_hot = { 53, 53, 53, 255 }, .thumb_border = { 100, 100, 100, 255 }
};

/* "Loading Serverlist": waiting for U.S.G.N., or no Internet server answered while requests are pending */
static bool servers_loading(const cs2d_client_t *client, int visible)
{
    const cs2d_sv_list_t *list = &client->servers.lists[CS2D_SV_TAB_INTERNET];
    if (client->servers_tab != CS2D_SV_TAB_INTERNET || visible > 0 || list->count == 0) return false;
    if (!client->servers.using_cache) return true;
    for (int i = 0; i < list->count; i++) {
        if (!list->entries[i].info_done && list->entries[i].info_reqs < CS2D_SV_INFO_TRIES) return true;
    }
    return false;
}

/*
 * No U.S.G.N. answer within 15 s (always the case here): offlineserverlist_load(0) clears the Internet
 * list and reloads sys/core/svl.cache, so every entry starts again with 3 pings and 5 info requests
 * (proxy capture: the original re-queries the cached server 15 s after Find Servers was opened).
 */
static void servers_use_cache(cs2d_client_t *client, uint32_t now)
{
    cs2d_sv_browser_t *b = &client->servers;
    cs2d_client_console_print(client, 255, 0, 0, "WARNING: Failed to load online serverlist within 15 sec! Trying To Load cached list...");
    b->lists[CS2D_SV_TAB_INTERNET].count = 0;
    int n = cs2d_sv_load_cache("sys/core/svl.cache", &b->lists[CS2D_SV_TAB_INTERNET], now);
    cs2d_client_console_print(client, 255, 0, 0, "WARNING: Using cached list (servers: %d)", n > 0 ? n : 0);
    b->using_cache = true;
}

static void cs2d_client_build_servers_ui(cs2d_client_t *client, int screen_w, int screen_h)
{
    (void)screen_w;
    (void)screen_h;
    if (!client->servers_open) return;

    cs2d_ui_context_t *ui = &client->ui;
    cs2d_sv_browser_t *b  = &client->servers;

    if (!b->using_cache && (uint32_t)GetTickCount() - b->open_ms >= SERVERS_LOADING_MS) {
        servers_use_cache(client, (uint32_t)GetTickCount());
    }

    char tab_text[CS2D_SV_TAB_COUNT][40];
    const char *tab_labels[CS2D_SV_TAB_COUNT];
    static const char *tab_names[CS2D_SV_TAB_COUNT] = { "Internet", "LAN", "Favorites", "Recently Joined" };
    for (int t = 0; t < CS2D_SV_TAB_COUNT; t++) {
        snprintf(tab_text[t], sizeof(tab_text[t]), "%s (%d)", tab_names[t], b->lists[t].count);
        tab_labels[t] = tab_text[t];
    }

    static cs2d_sv_t *rows[CS2D_SV_MAX];
    int total = servers_visible(client, rows, CS2D_SV_MAX);

    /* Row clicks: the first click selects, a click on the selected row joins (0x14016e7e6) */
    cs2d_ui_rect_t box = cs2d_ui_get_box(ui, CS2D_UI_ID("SrvListBox"));
    client->servers_visible_rows = (box.h > 2.0f) ? (int)((box.h - 2.0f) / SERVERS_ROW_H) : 23;
    if (!client->connect_ip_open && !client->console_open) {
        cs2d_ui_scroll_wheel(ui, &client->servers_scroll, &box, 3, client->servers_visible_rows, total);
    }
    servers_clamp_scroll(client, total);
    if (ui->mouse_pressed && !client->connect_ip_open && box.w > 0.0f &&
        ui->mouse_x >= box.x + 1.0f && ui->mouse_x < box.x + box.w - 16.0f &&
        ui->mouse_y >= box.y + 1.0f && ui->mouse_y < box.y + box.h - 1.0f) {
        int row = client->servers_scroll.top + (int)((ui->mouse_y - box.y - 1.0f) / SERVERS_ROW_H);
        if (row < total) {
            cs2d_sv_t *sv = rows[row];
            if (sv->ip == client->servers_sel_ip && sv->port == client->servers_sel_port) {
                servers_join(client, sv->ip, sv->port);
                return;
            }
            client->servers_sel_ip   = sv->ip;
            client->servers_sel_port = sv->port;
        }
    }

    const char *title = b->using_cache ? "Servers - using cached list - see console" : "Servers";
    if (cs2d_ui_window_begin(ui, CS2D_UI_ID("ServersWin"), title, 5.0f, 5.0f, 840.0f, 470.0f, &client->servers_open)) {
        CS2D_UI_ELEMENT(ui, CS2D_UI_ID("ServersBody"), {
            .layout = {
                .sizing    = { CS2D_UI_GROW(0, 0), CS2D_UI_GROW(0, 0) },
                .padding   = CS2D_UI_PAD(10, 0, 2, 0),
                .child_gap = 0.0f,
                .dir       = CS2D_UI_TOP_TO_BOTTOM
            }
        }) {
            int prev_tab = client->servers_tab;
            cs2d_ui_tab_bar(ui, CS2D_UI_ID("ServersTabs"), tab_labels, CS2D_SV_TAB_COUNT, &client->servers_tab);
            if (client->servers_tab != prev_tab) client->servers_scroll.top = 0;

            /* Column header row, drawn in cs2d_client_render_servers_rows */
            CS2D_UI_ELEMENT(ui, 0, {
                .layout = { .sizing = { CS2D_UI_GROW(0, 0), CS2D_UI_FIXED(25.0f) } }
            }) {}

            /* Rows are drawn in cs2d_client_render_servers_rows; the scrollbar is a UI element */
            CS2D_UI_ELEMENT(ui, CS2D_UI_ID("SrvListBox"), {
                .layout   = {
                    .sizing  = { CS2D_UI_GROW(0, 0), CS2D_UI_FIXED(359.0f) },
                    .dir     = CS2D_UI_LEFT_TO_RIGHT,
                    .align_x = CS2D_UI_ALIGN_X_RIGHT
                },
                .bg_color = CS2D_UI_RGBA(28, 28, 30, 246),
                .border   = { .width = 1.0f, .color = CS2D_UI_RGB(150, 150, 150) }
            }) {
                cs2d_ui_scrollbar(ui, CS2D_UI_ID("SrvListScrollbar"), &client->servers_scroll,
                                  client->servers_visible_rows, total, 359.0f, &s_servers_scrollbar);
            }

            CS2D_UI_ELEMENT(ui, 0, {
                .layout = { .sizing = { CS2D_UI_GROW(0, 0), CS2D_UI_FIXED(5.0f) } }
            }) {}

            CS2D_UI_ELEMENT(ui, CS2D_UI_ID("ServersFooter"), {
                .layout = {
                    .sizing    = { CS2D_UI_GROW(0, 0), CS2D_UI_FIXED(20.0f) },
                    .padding   = CS2D_UI_PAD(0, 10, 0, 0),
                    .child_gap = 5.0f,
                    .dir       = CS2D_UI_LEFT_TO_RIGHT
                }
            }) {
                char filters[32];
                snprintf(filters, sizeof(filters), "Filters (%d)", client->servers_filters);
                if (cs2d_ui_button(ui, CS2D_UI_ID("BtnSrvFilters"), filters, 100.0f, 20.0f)) {
                    cs2d_client_console_print(client, 255, 45, 45, "'Filters' is not reconstructed yet.");
                }
                /* "Stop" while the U.S.G.N. list is awaited (stops waiting and takes the cache) */
                bool waiting = !b->using_cache && client->servers_tab == CS2D_SV_TAB_INTERNET;
                if (cs2d_ui_button(ui, CS2D_UI_ID("BtnSrvRefresh"), waiting ? "Stop" : "Refresh", 100.0f, 20.0f)) {
                    uint32_t now = (uint32_t)GetTickCount();
                    if (waiting) {
                        servers_use_cache(client, now);
                    } else {
                        cs2d_sv_browser_refresh(b, client->servers_tab, now);
                    }
                }

                CS2D_UI_ELEMENT(ui, 0, {
                    .layout = { .sizing = { CS2D_UI_GROW(0, 0), CS2D_UI_FIXED(1.0f) } }
                }) {}

                if (cs2d_ui_button(ui, CS2D_UI_ID("BtnSrvConnectIP"), "Connect to IP", 120.0f, 20.0f)) {
                    client->connect_ip_open = true;
                    if (client->connect_ip_text[0] == '\0') {
                        snprintf(client->connect_ip_text, sizeof(client->connect_ip_text), "%.40s:%d",
                                 client->config.server_ip[0] ? client->config.server_ip : "127.0.0.1",
                                 client->config.server_port > 0 ? client->config.server_port : CS2D_SV_LAN_PORT);
                    }
                    client->connect_ip_cursor = (int)strlen(client->connect_ip_text);
                    ui->focused_id = CS2D_UI_ID("ConnectIPInput");
                }
                if (cs2d_ui_button(ui, CS2D_UI_ID("BtnSrvConnect"), "Connect", 100.0f, 20.0f) && client->servers_sel_port) {
                    servers_join(client, client->servers_sel_ip, client->servers_sel_port);
                }
            }
        }
        cs2d_ui_window_end(ui);
    }

    if (!client->servers_open) cs2d_sv_browser_close(b);
}

/* "Connect to IP" dialog (layout not measured on the original yet) */
static void cs2d_client_build_connect_ip_ui(cs2d_client_t *client, int screen_w, int screen_h)
{
    if (!client->connect_ip_open) return;

    cs2d_ui_context_t *ui = &client->ui;
    float win_w = 300.0f;
    float win_h = 88.0f;
    float win_x = floorf(((float)screen_w - win_w) * 0.5f);
    float win_y = floorf(((float)screen_h - win_h) * 0.5f);

    bool join = false;
    if (cs2d_ui_window_begin(ui, CS2D_UI_ID("ConnectIPWin"), "Connect to IP",
                             win_x, win_y, win_w, win_h, &client->connect_ip_open)) {
        CS2D_UI_ELEMENT(ui, 0, {
            .layout = {
                .sizing    = { CS2D_UI_GROW(0, 0), CS2D_UI_GROW(0, 0) },
                .padding   = CS2D_UI_PAD(10, 10, 4, 0),
                .child_gap = 8.0f,
                .dir       = CS2D_UI_TOP_TO_BOTTOM
            }
        }) {
            cs2d_ui_textbox(ui, CS2D_UI_ID("ConnectIPInput"), client->connect_ip_text, sizeof(client->connect_ip_text),
                            &client->connect_ip_cursor, 276.0f, false);
            CS2D_UI_ELEMENT(ui, 0, {
                .layout = {
                    .sizing    = { CS2D_UI_GROW(0, 0), CS2D_UI_FIXED(20.0f) },
                    .child_gap = 5.0f,
                    .dir       = CS2D_UI_LEFT_TO_RIGHT
                }
            }) {
                CS2D_UI_ELEMENT(ui, 0, {
                    .layout = { .sizing = { CS2D_UI_GROW(0, 0), CS2D_UI_FIXED(1.0f) } }
                }) {}
                join = cs2d_ui_button(ui, CS2D_UI_ID("BtnConnectIPOk"), "Connect", 100.0f, 20.0f);
                if (cs2d_ui_button(ui, CS2D_UI_ID("BtnConnectIPCancel"), "Cancel", 100.0f, 20.0f)) {
                    client->connect_ip_open = false;
                }
            }
        }
        cs2d_ui_window_end(ui);
    }

    uint32_t ip;
    uint16_t port;
    if (join && cs2d_sv_parse_address(client->connect_ip_text, &ip, &port)) servers_join(client, ip, port);
}

static cs2d_texture_t s_tex_gui_server; /* gfx/gui_server.bmp: 9 icons of 10x10, maskMode 3 (0x140a0de98) */
static cs2d_texture_t s_tex_netspeed;   /* gfx/netspeed.bmp: 3x11 latency bar, maskMode 2 (0x140a0de90) */
static cs2d_texture_t s_tex_gui_load;   /* gfx/gui_load.bmp: 16x16 spinner, midhandle (0x140a0de80) */

/* Game mode letter and color, initialised in FUN_140304090 (0x1403181a8..): S D T C Z */
static const char    *s_mode_letters[5]   = { "S", "D", "T", "C", "Z" };
static const uint8_t  s_mode_colors[5][3] = {
    { 175, 175,  50 }, { 200, 100, 100 }, { 200, 125,  50 }, { 100, 150, 175 }, { 100, 175, 100 }
};

static void servers_icon(int frame, float x, float y, float c, float a)
{
    cs2d_render_sprite_frame(&s_tex_gui_server, 10, 10, frame, x + 5.0f, y + 5.0f, 10.0f, 10.0f,
                             0.0f, c, c, c, a, 0);
}

static void servers_count_line(float right, float y, int n, const char *one, const char *many, int paren)
{
    char text[64];
    snprintf(text, sizeof(text), "%d %s (%d)", n, (n == 1) ? one : many, paren);
    float w = cs2d_render_text_width_ex(CS2D_FONT_SMALL, text);
    cs2d_render_text_ex(CS2D_FONT_SMALL, right - w, y, text, 150.0f / 255.0f, 150.0f / 255.0f, 150.0f / 255.0f, 1.0f);
}

/* Header, rows, scrollbar and messages over the list box (Tgui_sl.draw 0x14016b5d0) */
static void cs2d_client_render_servers_rows(cs2d_client_t *client)
{
    if (!client->servers_open) return;
    if (!s_tex_gui_server.id) {
        cs2d_texture_load_grid(&s_tex_gui_server, "gfx/gui_server.bmp", 3, 10, 10);
        cs2d_texture_load_ex(&s_tex_netspeed, "gfx/netspeed.bmp", 2);
        cs2d_texture_load_ex(&s_tex_gui_load, "gfx/gui_load.bmp", 3);
    }

    cs2d_ui_rect_t box = cs2d_ui_get_box(&client->ui, CS2D_UI_ID("SrvListBox"));
    if (box.w <= 0.0f) return;
    const float g150 = 150.0f / 255.0f;
    float right = box.x + box.w - 1.0f;

    static cs2d_sv_t *rows[CS2D_SV_MAX];
    int total = servers_visible(client, rows, CS2D_SV_MAX);
    servers_clamp_scroll(client, total);

    /* Stats (draw_stats): servers (answered), human players, bots */
    int humans = 0, bots = 0;
    for (int i = 0; i < total; i++) {
        humans += cs2d_sv_humans(rows[i]);
        bots   += rows[i]->bots;
    }
    int count = client->servers.lists[client->servers_tab].count;
    servers_count_line(right, box.y - 49.0f, count,  "Server", "Servers", total);
    servers_count_line(right, box.y - 37.0f, humans, "Player", "Players", humans);
    servers_count_line(right, box.y - 25.0f, bots,   "Bot",    "Bots",    bots);

    /* Column header: flag icons (lock, U.S.G.N., fog, light, recoil, off-screen, transfers, M) */
    static const int header_frames[8] = { 0, 1, 2, 6, 7, 8, 3, 5 };
    float hy = box.y - 15.0f;
    for (int k = 0; k < 8; k++) servers_icon(header_frames[k], box.x + 4.0f + 13.0f * (float)k, hy - 1.0f, g150, 1.0f);
    cs2d_render_text_ex(CS2D_FONT_REGULAR, box.x + 109.0f, hy - 2.0f, "Name",    g150, g150, g150, 1.0f);
    cs2d_render_text_ex(CS2D_FONT_REGULAR, box.x + 401.0f, hy - 2.0f, "Map",     g150, g150, g150, 1.0f);
    cs2d_render_text_ex(CS2D_FONT_REGULAR, box.x + 693.0f, hy - 2.0f, "Players", 1.0f, 1.0f, 1.0f, 1.0f);
    float pw = cs2d_render_text_width_ex(CS2D_FONT_REGULAR, "Players");
    servers_icon(3, box.x + 695.0f + pw, hy - 1.0f, 1.0f, 1.0f);
    cs2d_render_text_ex(CS2D_FONT_REGULAR, box.x + 755.0f, hy - 2.0f, "Latency", g150, g150, g150, 1.0f);

    int vis = client->servers_visible_rows;

    float mx = client->mouse_screen_x;
    float my = client->mouse_screen_y;

    /* The rows are drawn over the UI pass, so they would cover the Connect to IP dialog */
    if (client->connect_ip_open) return;

    if (total == 0) {
        float cx = box.x + (box.w - 16.0f) * 0.5f;
        float cy = box.y + box.h * 0.5f;
        const char *l1 = NULL, *l2 = NULL;
        if (servers_loading(client, total)) {
            l1 = "Loading Serverlist";
            cs2d_render_sprite(&s_tex_gui_load, cx, cy + 9.0f, 16.0f, 16.0f,
                               (float)(GetTickCount() % 1000u) * 0.36f, 1.0f, 1.0f, 1.0f, 1.0f);
        } else if (client->servers_tab == CS2D_SV_TAB_INTERNET) {
            l1 = "No Internet Servers online";
        } else if (client->servers_tab == CS2D_SV_TAB_LAN) {
            l1 = "Trying to find Servers in LAN";
            l2 = "(only servers with port 36963 will be listed)";
        } else if (client->servers_tab == CS2D_SV_TAB_FAVORITES) {
            l1 = "You have no Favorite Servers";
            l2 = "(rightclick a server to make it a favorite)";
        } else {
            l1 = "No servers have been joined recently";
            l2 = "(this list will be filled automatically when you join servers)";
        }
        float w1 = cs2d_render_text_width_ex(CS2D_FONT_REGULAR, l1);
        float c1 = l2 ? g150 : 1.0f;
        cs2d_render_text_ex(CS2D_FONT_REGULAR, floorf(cx - w1 * 0.5f), cy - 22.0f, l1, c1, c1, c1, 1.0f);
        if (l2) {
            float w2 = cs2d_render_text_width_ex(CS2D_FONT_REGULAR, l2);
            cs2d_render_text_ex(CS2D_FONT_REGULAR, floorf(cx - w2 * 0.5f), cy - 2.0f, l2, 0.39f, 0.39f, 0.39f, 1.0f);
        }
        return;
    }

    cs2d_render_scissor_begin((int)box.x + 1, (int)box.y + 1, (int)(box.w - 17.0f), (int)(box.h - 2.0f));
    const cs2d_sv_t *hover_sv = NULL;
    for (int r = 0; r < vis && client->servers_scroll.top + r < total; r++) {
        const cs2d_sv_t *sv = rows[client->servers_scroll.top + r];
        float ry = box.y + 1.0f + SERVERS_ROW_H * (float)r;
        bool sel = (sv->ip == client->servers_sel_ip && sv->port == client->servers_sel_port);
        bool hot = mx >= box.x + 1.0f && mx < right - 15.0f && my >= ry && my < ry + SERVERS_ROW_H;
        if (hot) hover_sv = sv;

        /* Highlight: light blend over the row (alpha calibrated by eye) */
        if (sel || hot) {
            cs2d_render_rect(box.x + 1.0f, ry, box.w - 17.0f, SERVERS_ROW_H, 1.0f, 1.0f, 1.0f, sel ? 0.16f : 0.08f, true);
        }
        float base = (sel || hot) ? 1.0f : g150;

        /* Flag icons in the header columns */
        float icon_c = base * 0.75f;
        if (sv->password)         servers_icon(0, box.x + 4.0f,  ry + 2.0f, icon_c, 1.0f);
        if (sv->usgn_only)        servers_icon(1, box.x + 17.0f, ry + 2.0f, icon_c, 1.0f);
        if (sv->fog)              servers_icon(2, box.x + 30.0f, ry + 2.0f, icon_c, 1.0f);
        if (sv->light)            servers_icon(6, box.x + 43.0f, ry + 2.0f, icon_c, 1.0f);
        if (sv->dyn_recoil)       servers_icon(7, box.x + 56.0f, ry + 2.0f, icon_c, 1.0f);
        if (sv->offscreen_damage) servers_icon(8, box.x + 69.0f, ry + 2.0f, icon_c, 1.0f);
        if (sv->transfers)        servers_icon(3, box.x + 82.0f, ry + 2.0f, icon_c, 1.0f);

        /* Game mode letter and Lua "s" (115,110,255) */
        if (sv->gamemode >= 0 && sv->gamemode < 5) {
            const uint8_t *mc = s_mode_colors[sv->gamemode];
            cs2d_render_text_ex(CS2D_FONT_REGULAR, box.x + 94.0f, ry - 1.0f, s_mode_letters[sv->gamemode],
                                mc[0] / 255.0f, mc[1] / 255.0f, mc[2] / 255.0f, 1.0f);
        }
        if (sv->lua) {
            cs2d_render_text_ex(CS2D_FONT_SMALL, box.x + 100.0f, ry + 4.0f, "s", 115.0f / 255.0f, 110.0f / 255.0f, 1.0f, 1.0f);
        }

        /* Name (max 225 px) and map (max 188 px, half bright when maps/<map>.map is missing) */
        cs2d_render_scissor_begin((int)box.x + 109, (int)ry, 225, (int)SERVERS_ROW_H);
        cs2d_render_text_ex(CS2D_FONT_REGULAR, box.x + 109.0f, ry - 1.0f, sv->name, base, base, base, 1.0f);
        cs2d_render_scissor_end();
        float mapc = sv->existing_map ? base : base * 0.5f;
        cs2d_render_scissor_begin((int)box.x + 401, (int)ry, 188, (int)SERVERS_ROW_H);
        cs2d_render_text_ex(CS2D_FONT_REGULAR, box.x + 401.0f, ry - 1.0f, sv->map, mapc, mapc, mapc, 1.0f);
        cs2d_render_scissor_end();
        cs2d_render_scissor_begin((int)box.x + 1, (int)box.y + 1, (int)(box.w - 17.0f), (int)(box.h - 2.0f));

        /* Players: empty half bright, humans green (0,175,0), full red (175,0,0), bots yellow */
        char pl[16];
        snprintf(pl, sizeof(pl), "%d/%d", sv->players, sv->max_players);
        float pr = base, pg = base, pb = base;
        if (sv->players == 0) {
            pr = pg = pb = base * 0.5f;
        } else if (sv->players >= sv->max_players) {
            pr = (sel || hot) ? 1.0f : 175.0f / 255.0f;
            pg = pb = (sel || hot) ? 50.0f / 255.0f : 0.0f;
        } else if (sv->players > sv->bots) {
            pg = (sel || hot) ? 225.0f / 255.0f : 175.0f / 255.0f;
            pr = pb = (sel || hot) ? 50.0f / 255.0f : 0.0f;
        }
        cs2d_render_text_ex(CS2D_FONT_REGULAR, box.x + 693.0f, ry - 1.0f, pl, pr, pg, pb, 1.0f);
        if (sv->bots > 0) {
            char bt[8];
            snprintf(bt, sizeof(bt), "%d", sv->bots);
            float yc = (sel || hot) ? 1.0f : 200.0f / 255.0f;
            cs2d_render_text_ex(CS2D_FONT_REGULAR, box.x + 731.0f, ry - 1.0f, bt, yc, yc, 0.0f, 1.0f);
        }

        /* Latency: 10 bars, 4 px apart; lit = ceil(10 * (1 - ping/500)), color r = 510 f, g = 510 (1 - f) */
        float bx = box.x + 757.0f;
        for (int k = 0; k < 10; k++) {
            cs2d_render_sprite(&s_tex_netspeed, bx + 4.0f * (float)k + 1.5f, ry + 7.5f, 3.0f, 11.0f, 0.0f,
                               50.0f / 255.0f, 50.0f / 255.0f, 50.0f / 255.0f, 1.0f);
        }
        if (sv->got_ping) {
            float f = (sv->ping >= 500) ? 1.0f : (float)sv->ping * 0.002f;
            float lr = fminf(510.0f * f, 255.0f) / 255.0f;
            float lg = fminf(510.0f * (1.0f - f), 255.0f) / 255.0f;
            float lb = sv->ping_blend / 255.0f;
            int lit = (int)ceilf((1.0f - f) * 10.0f);
            if (lit < 1) lit = 1;
            for (int k = 0; k < lit && k < 10; k++) {
                cs2d_render_sprite(&s_tex_netspeed, bx + 4.0f * (float)k + 1.5f, ry + 7.5f, 3.0f, 11.0f, 0.0f,
                                   lr, lg, lb, 1.0f);
            }
        } else {
            float a = fabsf(sinf((float)(GetTickCount() / 3u) * 0.017453292f)) * 0.7f;
            float qw = cs2d_render_text_width_ex(CS2D_FONT_REGULAR, "?");
            cs2d_render_text_ex(CS2D_FONT_REGULAR, bx + 18.0f - qw * 0.5f, ry - 1.0f, "?",
                                100.0f / 255.0f, 100.0f / 255.0f, 100.0f / 255.0f, a);
        }
    }
    cs2d_render_scissor_end();

    /* Tooltip with the address of the hovered row, at mouse + (15, 5) (Tgui_sl.draw, DAT_140ddf358 / 354) */
    if (hover_sv) {
        char addr[16], tip[32];
        cs2d_sv_ip_to_str(hover_sv->ip, addr);
        snprintf(tip, sizeof(tip), "%s:%u", addr, (unsigned)hover_sv->port);
        float tw = cs2d_render_text_width_ex(CS2D_FONT_REGULAR, tip);
        cs2d_render_rect(mx + 15.0f, my + 5.0f, tw + 8.0f, 17.0f, 0.0f, 0.0f, 0.0f, 0.8f, true);
        cs2d_render_rect(mx + 15.0f, my + 5.0f, tw + 8.0f, 17.0f, g150, g150, g150, 1.0f, false);
        cs2d_render_text_ex(CS2D_FONT_REGULAR, mx + 19.0f, my + 6.0f, tip, 1.0f, 1.0f, 1.0f, 1.0f);
    }
}

/*
 * "Downloading" window while the server sends files: Tgui_win 350x200 created next to Tgui_joinmen
 * (0x1401a3604), centered, Cancel button at (234, 164) 100 px wide. Its contents are drawn by
 * cs2d_client_render_download.
 */
#define DL_WIN_W 350.0f
#define DL_WIN_H 200.0f

static void download_window_pos(int screen_w, int screen_h, float *x, float *y)
{
    *x = floorf(((float)screen_w - DL_WIN_W) * 0.5f);
    *y = floorf(((float)screen_h - DL_WIN_H) * 0.5f);
}

static void cs2d_client_build_download_ui(cs2d_client_t *client, int screen_w, int screen_h)
{
    if (!client->download_open) return;

    cs2d_ui_context_t *ui = &client->ui;
    float win_x, win_y;
    download_window_pos(screen_w, screen_h, &win_x, &win_y);
    bool open = true;
    bool cancel = false;
    if (cs2d_ui_window_begin(ui, CS2D_UI_ID("DownloadWin"), "Downloading", win_x, win_y, DL_WIN_W, DL_WIN_H, &open)) {
        /* Content starts 23 px below the window top; the button sits at y 164 */
        CS2D_UI_ELEMENT(ui, 0, {
            .layout = { .sizing = { CS2D_UI_GROW(0, 0), CS2D_UI_FIXED(137.0f) } }
        }) {}
        CS2D_UI_ELEMENT(ui, 0, {
            .layout = {
                .sizing = { CS2D_UI_GROW(0, 0), CS2D_UI_FIXED(20.0f) },
                .dir    = CS2D_UI_LEFT_TO_RIGHT
            }
        }) {
            CS2D_UI_ELEMENT(ui, 0, {
                .layout = { .sizing = { CS2D_UI_FIXED(228.0f), CS2D_UI_FIXED(1.0f) } }
            }) {}
            cancel = cs2d_ui_button(ui, CS2D_UI_ID("BtnDownloadCancel"), "Cancel", 100.0f, 20.0f);
        }
        cs2d_ui_window_end(ui);
    }
    if (cancel || !open) {
        /* Cancel leaves with reason 9 ([253,9], join window buttons in cs2d_menu_screen 0x140249280) */
        join_abort(client, 9);
    }
}

/*
 * Reasons of a refused join, drawn under "Failed to join $1:$2" (Tgui_joinmen.draw menu 2, 0x140301640):
 * error -> sys/language entry, '|' separates lines; $1 is the map name, $2 the map load error
 */
static const struct { int error; const char *text; } s_join_errors[] = {
    {   2, "Only registered U.S.G.N. users allowed!|Login (Options -> Net) with your account|or get an account at UnrealSoftware.de!" }, /* 0378 */
    {   3, "Server is full!|Please try again later!" },                                                       /* 0379 */
    {   4, "You are banned!|Please contact the admin of this server|in order to get more info about this.|Please do NOT contact Unreal Software." }, /* 0380 */
    {   5, "Server uses another game version!|Either your client or the version of the|server is outdated." }, /* 0381 */
    {   7, "Can't validate user (U.S.G.N. timeout)|Maybe the U.S.G.N. is offline/overloaded.|Please try again!" }, /* 0382 */
    {   9, "Connection with server already exists!|Please try again later!" },                               /* 0383 */
    {  10, "Your client does not suit the server!|Do not modify your client!|Re-download at www.CS2D.com or|contact the server admin!" }, /* 0384 */
    {  11, "Your map differs from the server's!" },                                                          /* 0385 */
    {  12, "You do not have the map|'$1'|and the server disabled map transfer.|You can't join without this map." }, /* 0386 */
    {  20, "Failed to load the map|'$1'|$2" },                                                               /* 0387 */
    {  21, "Server changed map during join-process!|Please try again!" },                                    /* 0388 */
    {  22, "Authentication failed!|Your or the server's version is modified|or outdated!|Please check your version and try again!" }, /* 0389 */
    {  23, "Too many clients with your IP!|There are too many clients with the same IP|on this server!Please try again later!" }, /* 0390 */
    { 100, "Transfer problems!|Please try again!" },                                                         /* 0391 */
    { 101, "Connection lost!|Please try again!" },                                                           /* 0392 */
    { 102, "Server does not answer!|Maybe it is offline or busy. Try again later.|Please change your router/firewall settings if|it is your server! (read www.CS2D.com/faq)" }, /* 0393 */
    { 103, "Failed to save downloaded map!|Make sure that there is enough space and that|you're allowed to save files in your maps-folder!" }, /* 0394 */
    { 200, "Timeout! Server does not answer!|Maybe it is offline or busy. Try again later.|Please change your router/firewall settings if|it is your server! (read www.CS2D.com/faq)" }, /* 0395 */
    { 201, "Connection problems!|Failed to receive all server data.|Please try again!" },                   /* 0396 */
};

/* Error 11 choices (lang 0860, Tgui_opt at (16, 85)) */
static const char *s_join_map_opts[3] = {
    "Download map temporarily, keep existing", "Download map, overwrite existing", "Cancel joining, keep existing"
};

/*
 * Okay / Connect button (0x140a10ed0) in cs2d_menu_screen (0x140249280):
 *   menu 1: the textbox text becomes the join password (0x140a18368) and the join restarts from [252,0]
 *           (FUN_14016a3c0(0));
 *   menu 2, error 11: choice 0 / 1 set the map transfer mode (DAT_140ddf3dc) to 1 / 2 and restart the join;
 *   otherwise: [253,9] and back to the menu.
 */
static void join_menu_submit(cs2d_client_t *client)
{
    if (client->join_menu == 1) {
        snprintf(client->net_password, sizeof(client->net_password), "%s", client->join_pw_text);
    } else if (client->join_error == 11 && client->join_opt != 2) {
        client->xfer.map_mode = client->join_opt + 1;
    } else {
        join_abort(client, 9);
        return;
    }
    client->join_menu = 0;
    cs2d_client_connect(client, client->config.server_ip, client->config.server_port);
}

/*
 * Tgui_joinmen window: the same 350x200 Tgui_win as the download window (0x1401a3604). Buttons at y 164:
 * Okay / Connect (124) and Cancel (234), 100 px wide.
 *   menu 1 (pwprompt 0x140055e40): "Password required" (lang 0782), password input at (16, 64) 318 px wide,
 *           button label "Connect" (lang 0106);
 *   menu 2 (errorprompt 0x14008b090): "Error" (lang 0034), button "Okay" (lang 0030), map choices for error 11.
 * The text lines are drawn by cs2d_client_render_join_menu.
 */
static void cs2d_client_build_join_menu_ui(cs2d_client_t *client, int screen_w, int screen_h)
{
    if (client->join_menu == 0) return;

    cs2d_ui_context_t *ui = &client->ui;
    float win_x, win_y;
    download_window_pos(screen_w, screen_h, &win_x, &win_y);
    bool pw = (client->join_menu == 1);
    bool open = true;
    bool submit = false, cancel = false;
    if (cs2d_ui_window_begin(ui, CS2D_UI_ID("JoinMenuWin"), pw ? "Password required" : "Error",
                             win_x, win_y, DL_WIN_W, DL_WIN_H, &open)) {
        /* Content starts 23 px below the window top and 6 px right of its left edge (see the download window) */
        if (pw) {
            CS2D_UI_ELEMENT(ui, 0, {
                .layout = { .sizing = { CS2D_UI_GROW(0, 0), CS2D_UI_FIXED(41.0f) } }
            }) {}
            CS2D_UI_ELEMENT(ui, 0, {
                .layout = {
                    .sizing = { CS2D_UI_GROW(0, 0), CS2D_UI_FIXED(18.0f) },
                    .dir    = CS2D_UI_LEFT_TO_RIGHT
                }
            }) {
                CS2D_UI_ELEMENT(ui, 0, {
                    .layout = { .sizing = { CS2D_UI_FIXED(10.0f), CS2D_UI_FIXED(1.0f) } }
                }) {}
                cs2d_ui_textbox(ui, CS2D_UI_ID("JoinPwInput"), client->join_pw_text, sizeof(client->join_pw_text),
                                &client->join_pw_cursor, 318.0f, true);
            }
            CS2D_UI_ELEMENT(ui, 0, {
                .layout = { .sizing = { CS2D_UI_GROW(0, 0), CS2D_UI_FIXED(82.0f) } }
            }) {}
        } else {
            CS2D_UI_ELEMENT(ui, 0, {
                .layout = { .sizing = { CS2D_UI_GROW(0, 0), CS2D_UI_FIXED(62.0f) } }
            }) {}
            CS2D_UI_ELEMENT(ui, 0, {
                .layout = {
                    .sizing  = { CS2D_UI_GROW(0, 0), CS2D_UI_FIXED(79.0f) },
                    .padding = CS2D_UI_PAD(10, 0, 0, 0),
                    .dir     = CS2D_UI_TOP_TO_BOTTOM
                }
            }) {
                if (client->join_error == 11) {
                    for (int i = 0; i < 3; i++) {
                        cs2d_ui_radio(ui, CS2D_UI_IDI("JoinMapOpt", i), s_join_map_opts[i], i, &client->join_opt);
                    }
                }
            }
        }
        CS2D_UI_ELEMENT(ui, 0, {
            .layout = {
                .sizing    = { CS2D_UI_GROW(0, 0), CS2D_UI_FIXED(20.0f) },
                .child_gap = 10.0f,
                .dir       = CS2D_UI_LEFT_TO_RIGHT
            }
        }) {
            CS2D_UI_ELEMENT(ui, 0, {
                .layout = { .sizing = { CS2D_UI_FIXED(108.0f), CS2D_UI_FIXED(1.0f) } }
            }) {}
            submit = cs2d_ui_button(ui, CS2D_UI_ID("BtnJoinMenuOk"), pw ? "Connect" : "Okay", 100.0f, 20.0f);
            cancel = cs2d_ui_button(ui, CS2D_UI_ID("BtnJoinMenuCancel"), "Cancel", 100.0f, 20.0f);
        }
        cs2d_ui_window_end(ui);
    }
    if (submit) {
        join_menu_submit(client);
    } else if (cancel || !open) {
        /* Cancel (0x140a10ed8) leaves with reason 9, as in the download window */
        join_abort(client, 9);
    }
}

/*
 * Tgui_joinmen.draw (0x140301640): menu 1 draws lang 0376 at (16, 32) in the gui color; menu 2 draws
 * "Failed to join $1:$2" (lang 0377) at (16, 32) and the reason lines from (16, 52) every 20 px, in red
 * (SetColor(255 + alpha, alpha, alpha) with the window alpha 0)
 */
static void cs2d_client_render_join_menu(cs2d_client_t *client, int screen_w, int screen_h)
{
    if (client->join_menu == 0) return;
    float win_x, win_y;
    download_window_pos(screen_w, screen_h, &win_x, &win_y);
    float tx = win_x + 16.0f;
    if (client->join_menu == 1) {
        const float c = 150.0f / 255.0f;
        cs2d_render_text_ex(CS2D_FONT_REGULAR, tx, win_y + 32.0f, "Please enter the right password!", c, c, c, 1.0f);
        return;
    }

    char line[256];
    snprintf(line, sizeof(line), "Failed to join %s:%d", client->config.server_ip, client->config.server_port);
    cs2d_render_text_ex(CS2D_FONT_REGULAR, tx, win_y + 32.0f, line, 1.0f, 0.0f, 0.0f, 1.0f);

    const char *text = NULL;
    for (size_t i = 0; i < sizeof(s_join_errors) / sizeof(s_join_errors[0]); i++) {
        if (s_join_errors[i].error == client->join_error) text = s_join_errors[i].text;
    }
    float y = win_y + 52.0f;
    while (text && *text) {
        const char *bar = strchr(text, '|');
        size_t n = bar ? (size_t)(bar - text) : strlen(text);
        char raw[128];
        snprintf(raw, sizeof(raw), "%.*s", (int)n, text);
        /* $1 = map name; $2 (the map load error) is not tracked and stays empty */
        char *d = strstr(raw, "$1");
        if (d) {
            snprintf(line, sizeof(line), "%.*s%s%s", (int)(d - raw), raw, client->net_map_name, d + 2);
        } else {
            snprintf(line, sizeof(line), "%s", strcmp(raw, "$2") == 0 ? "" : raw);
        }
        cs2d_render_text_ex(CS2D_FONT_REGULAR, tx, y, line, 1.0f, 0.0f, 0.0f, 1.0f);
        y += 20.0f;
        text = bar ? bar + 1 : NULL;
    }
}

static cs2d_texture_t s_tex_gui_shade; /* gfx/gui_shade.bmp: 10x10 vertical gradient, maskMode 3 (0x140a0de78) */

/* Progress bar: 1 px frame, black inside, gui_shade stretched to the filled width (Tgui_joinmen.draw) */
static void download_bar(float x, float y, float bar_w, float frac, float c)
{
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    cs2d_render_rect(x, y, bar_w, 16.0f, c, c, c, 1.0f, true);
    cs2d_render_rect(x + 1.0f, y + 1.0f, bar_w - 2.0f, 14.0f, 0.0f, 0.0f, 0.0f, 1.0f, true);
    float fill_w = (bar_w - 6.0f) * frac;
    if (fill_w >= 1.0f) {
        cs2d_render_sprite_frame(&s_tex_gui_shade, 10, 10, 0, x + 3.0f + fill_w * 0.5f, y + 8.0f, fill_w, 10.0f,
                                 0.0f, c, c, c, 1.0f, 0);
    }
}

/* Tgui_joinmen.draw, menu 10 (0x140301640): two bars (all files, current file), file name, kb, speed */
static void cs2d_client_render_download(cs2d_client_t *client, int screen_w, int screen_h)
{
    if (!client->download_open) return;
    if (!s_tex_gui_shade.id) cs2d_texture_load_ex(&s_tex_gui_shade, "gfx/gui_shade.bmp", 3);

    const cs2d_xfer_t *x = &client->xfer;
    float win_x, win_y;
    download_window_pos(screen_w, screen_h, &win_x, &win_y);
    const float c = 150.0f / 255.0f; /* gui color 255 x (alpha 0 + 150) / 255, measured on a screenshot */
    float tx = win_x + 16.0f;
    float bar_w = DL_WIN_W - 32.0f;
    char line[CS2D_XFER_PATH + 32];

    cs2d_render_text_ex(CS2D_FONT_REGULAR, tx, win_y + 32.0f, "Downloading data...", c, c, c, 1.0f);
    float all = (float)(x->files_received - 1) + x->perc;
    download_bar(tx, win_y + 55.0f, bar_w, x->files_total > 0 ? all / (float)x->files_total : 0.0f, c);
    if (x->files_received > 0) {
        snprintf(line, sizeof(line), "%s (%d of %d)", x->file, x->files_received, x->files_total);
    } else {
        snprintf(line, sizeof(line), "Waiting for transmission... (0 of %d)", x->files_total);
    }
    /* Squeezed to the bar width when longer (SetScale(318 / TextWidth, 1)) */
    float line_w = cs2d_render_text_width_ex(CS2D_FONT_REGULAR, line);
    float sx = (line_w > bar_w) ? bar_w / line_w : 1.0f;
    cs2d_render_text_scaled_ex(CS2D_FONT_REGULAR, tx, win_y + 79.0f, line, sx, c, c, c, 1.0f);
    download_bar(tx, win_y + 105.0f, bar_w, x->perc, c);
    cs2d_render_text_ex(CS2D_FONT_REGULAR, tx, win_y + 127.0f, x->prog, c, c, c, 1.0f);
    snprintf(line, sizeof(line), "Download Speed: %u kb/s", client->net_bps_down >> 10);
    cs2d_render_text_ex(CS2D_FONT_REGULAR, tx, win_y + 152.0f, line, c, c, c, 1.0f);
}

void cs2d_client_build_ui(cs2d_client_t *client, int screen_w, int screen_h)
{
    cs2d_ui_begin_layout(
        &client->ui,
        screen_w, screen_h,
        client->mouse_screen_x, client->mouse_screen_y,
        client->mouse_fire,
        client->ui_wheel_delta
    );
    client->ui_wheel_delta = 0;

    /* Editor sidebar/windows first: the client windows (console, options, ...) open on top of them */
    if (client->scene == CS2D_SCENE_EDITOR) cs2d_scene_editor_build_ui(client, screen_w, screen_h);
    cs2d_client_build_team_menu_ui(client, screen_w, screen_h);
    cs2d_client_build_server_info_ui(client, screen_w, screen_h);
    cs2d_client_build_briefing_ui(client, screen_w, screen_h);
    cs2d_client_build_leaderboard_ui(client, screen_w, screen_h);
    cs2d_client_build_console_ui(client, screen_w, screen_h);
    cs2d_client_build_create_server_ui(client, screen_w, screen_h);
    cs2d_client_build_options_ui(client, screen_w, screen_h);
    cs2d_client_build_servers_ui(client, screen_w, screen_h);
    cs2d_client_build_connect_ip_ui(client, screen_w, screen_h);
    cs2d_client_build_download_ui(client, screen_w, screen_h);
    cs2d_client_build_join_menu_ui(client, screen_w, screen_h);

    cs2d_ui_end_layout(&client->ui);
}

static void cs2d_client_render_ui(cs2d_client_t *client, int screen_w, int screen_h)
{
    /* The editor's sidebar widgets and windows are UI elements too, so it builds the UI every frame */
    if (!cs2d_client_any_ui_open(client) && client->scene != CS2D_SCENE_EDITOR) {
        client->ui_wheel_delta = 0;
        cs2d_ui_clear_input(&client->ui);
        return;
    }
    cs2d_client_build_ui(client, screen_w, screen_h);
    cs2d_ui_render(&client->ui);
    cs2d_client_render_servers_rows(client);
    if (client->scene == CS2D_SCENE_EDITOR) cs2d_scene_editor_render_overlay(client, screen_w, screen_h);
    cs2d_client_render_download(client, screen_w, screen_h);
    cs2d_client_render_join_menu(client, screen_w, screen_h);
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

    /* 1. Active scene (lobby menu, game world + HUD, or editor view) */
    cs2d_scene_get(client->scene)->render(client, screen_w, screen_h);

    /* 2. Declarative CS2D UI Windows (Console, Create Server, Options) */
    cs2d_client_render_ui(client, screen_w, screen_h);

    /* 3. Top-Left FPS / Renderer / Network Traffic Display (in game only; menu and editor use that corner) */
    if (client->scene == CS2D_SCENE_GAME) {
        cs2d_client_render_fps_overlay(client);
    }

    /* 4. Crosshair (gfx/pointer.bmp frame 0) drawn on top of everything */
    cs2d_render_crosshair(client->mouse_screen_x, client->mouse_screen_y);
}
