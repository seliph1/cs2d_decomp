/*
 * Dedicated server Lua subsystem (Phase 4).
 *
 * Implements:
 * - Lua 5.1.4 state lifecycle and script loading (server.lua, autorun scripts, maps/<map>.lua)
 * - Hooks system: addhook, freehook, sethookstate with priority sorting and return value semantics
 * - Console command execution via parse(cmd), including HUD text commands (opcode 234 & 54)
 * - player(id, prop), playerweapons(id), playerammo(id, type)
 * - Dynamic Lua images (Tdo 40, opcode 51): image, freeimage, imagepos, imagealpha, imagecolor,
 *   imagescale, imageblend, imageframe, imageshadow, imagehitzone, imageparam
 * - Image tweens (opcode 54): tween_alpha, tween_color, tween_move, tween_rotate,
 *   tween_rotateconstantly, tween_scale, tween_frame, tween_animate
 * - Server timers: timer(ms, func, param, count), freetimer(func, param)
 * - Lua server menus (opcode 235): menu(id, text) and client selection response
 * - msg, msg2, print, game
 */

#include "cs2d_sv_lua.h"
#include "cs2d_weapons.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Helper for UTF-16LE conversion */
static int to_utf16le(const char *src, uint16_t *dst, int max_chars)
{
    int len = 0;
    while (*src && len < max_chars - 1) {
        dst[len++] = (uint16_t)(uint8_t)*src++;
    }
    dst[len] = 0;
    return len;
}

static void send_to_player(cs2d_server_t *sv, int id, const uint8_t *data, int len, bool reliable)
{
    if (id <= 0 || id > CS2D_SV_MAX_PLAYERS) return;
    cs2d_sv_player_t *pl = &sv->players[id];
    if (!pl->used || pl->conn < 0) return;
    cs2d_sv_conn_t *c = &sv->conns[pl->conn];
    if (!c->active) return;
    cs2d_pkt_t *p = cs2d_sv_packet(sv, c, reliable, true);
    if (!p) return;
    if (p->pos + len <= (int)sizeof(p->buf)) {
        memcpy(p->buf + p->pos, data, (size_t)len);
        p->pos += len;
    }
}

static void broadcast_to_joined(cs2d_server_t *sv, const uint8_t *data, int len, bool reliable)
{
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        cs2d_sv_player_t *pl = &sv->players[i];
        if (!pl->used || !pl->joined || pl->conn < 0) continue;
        send_to_player(sv, i, data, len, reliable);
    }
}

/* Helper to resolve a dot-separated function (e.g. "sample.ut.kill" or "myfunc") onto Lua stack */
static bool lua_push_func(lua_State *L, const char *name)
{
    char buf[128];
    snprintf(buf, sizeof(buf), "%s", name);
    char *dot = strchr(buf, '.');
    if (!dot) {
        lua_getglobal(L, buf);
        if (!lua_isfunction(L, -1)) {
            lua_pop(L, 1);
            return false;
        }
        return true;
    }

    *dot = '\0';
    lua_getglobal(L, buf);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return false;
    }

    char *curr = dot + 1;
    while (curr) {
        char *next = strchr(curr, '.');
        if (next) *next = '\0';
        lua_getfield(L, -1, curr);
        lua_remove(L, -2); /* remove parent table */
        if (next) {
            if (!lua_istable(L, -1)) {
                lua_pop(L, 1);
                return false;
            }
            curr = next + 1;
        } else {
            break;
        }
    }

    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return false;
    }
    return true;
}

/* ---------------------------------------------------------------------------
 * Lua Native Function Implementations
 * --------------------------------------------------------------------------- */

static cs2d_server_t *get_sv_from_lua(lua_State *L)
{
    lua_getfield(L, LUA_REGISTRYINDEX, "cs2d_sv");
    cs2d_server_t *sv = (cs2d_server_t *)lua_touserdata(L, -1);
    lua_pop(L, 1);
    return sv;
}

/* addhook(hook_name, func_name [, priority]) */
static int l_addhook(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv) return 0;
    const char *hook_name = luaL_checkstring(L, 1);
    const char *func_name = luaL_checkstring(L, 2);
    int priority = (int)luaL_optinteger(L, 3, 0);

    cs2d_sv_lua_addhook(sv, hook_name, func_name, priority);
    return 0;
}

/* freehook(hook_name, func_name) */
static int l_freehook(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv) return 0;
    const char *hook_name = luaL_checkstring(L, 1);
    const char *func_name = luaL_checkstring(L, 2);

    cs2d_sv_lua_freehook(sv, hook_name, func_name);
    return 0;
}

/* sethookstate(hook_name, state) */
static int l_sethookstate(lua_State *L)
{
    /* State: 1 = enabled, 0 = disabled (CS2D default is all active) */
    (void)L;
    return 0;
}

/* Forward declaration of hudtxt parsing */
static bool cs2d_sv_lua_parse_hudtxt(cs2d_server_t *sv, const char *cmd);

/* parse(cmd) */
static int l_parse(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv) return 0;
    const char *cmd = luaL_checkstring(L, 1);

    /* Hook "parse" can intercept and cancel */
    if (cs2d_sv_lua_hook_parse(sv, cmd) == 1) {
        return 0;
    }

    /* Check if it is a HUD text or Lua helper command */
    if (cs2d_sv_lua_parse_hudtxt(sv, cmd)) {
        return 0;
    }

    if (strncmp(cmd, "lua ", 4) == 0) {
        cs2d_sv_lua_dostring(sv, cmd + 4);
        return 0;
    }
    if (strcmp(cmd, "luareset") == 0) {
        cs2d_sv_lua_close(sv);
        cs2d_sv_lua_init(sv);
        return 0;
    }

    cs2d_sv_exec(sv, cmd);
    return 0;
}

/* player(id, property) */
static int l_player(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv) return 0;
    int id = (int)luaL_checkinteger(L, 1);
    const char *prop = luaL_checkstring(L, 2);

    /* ID 0: player collection queries returning Lua tables */
    if (id == 0) {
        lua_newtable(L);
        int idx = 1;
        if (strcmp(prop, "table") == 0) {
            for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
                if (sv->players[i].used) {
                    lua_pushinteger(L, i);
                    lua_rawseti(L, -2, idx++);
                }
            }
            return 1;
        }
        if (strcmp(prop, "tableliving") == 0) {
            for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
                if (sv->players[i].used && sv->players[i].alive && sv->players[i].team >= 1) {
                    lua_pushinteger(L, i);
                    lua_rawseti(L, -2, idx++);
                }
            }
            return 1;
        }
        if (strcmp(prop, "team1") == 0) {
            for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
                if (sv->players[i].used && sv->players[i].team == 1) {
                    lua_pushinteger(L, i);
                    lua_rawseti(L, -2, idx++);
                }
            }
            return 1;
        }
        if (strcmp(prop, "team2") == 0) {
            for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
                if (sv->players[i].used && sv->players[i].team == 2) {
                    lua_pushinteger(L, i);
                    lua_rawseti(L, -2, idx++);
                }
            }
            return 1;
        }
        if (strcmp(prop, "team1living") == 0) {
            for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
                if (sv->players[i].used && sv->players[i].alive && sv->players[i].team == 1) {
                    lua_pushinteger(L, i);
                    lua_rawseti(L, -2, idx++);
                }
            }
            return 1;
        }
        if (strcmp(prop, "team2living") == 0) {
            for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
                if (sv->players[i].used && sv->players[i].alive && sv->players[i].team == 2) {
                    lua_pushinteger(L, i);
                    lua_rawseti(L, -2, idx++);
                }
            }
            return 1;
        }
        if (strcmp(prop, "spectators") == 0) {
            for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
                if (sv->players[i].used && sv->players[i].team == 0) {
                    lua_pushinteger(L, i);
                    lua_rawseti(L, -2, idx++);
                }
            }
            return 1;
        }
        if (strcmp(prop, "tablebot") == 0) {
            for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
                if (sv->players[i].used && sv->players[i].bot) {
                    lua_pushinteger(L, i);
                    lua_rawseti(L, -2, idx++);
                }
            }
            return 1;
        }
        if (strcmp(prop, "tablehuman") == 0) {
            for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
                if (sv->players[i].used && !sv->players[i].bot) {
                    lua_pushinteger(L, i);
                    lua_rawseti(L, -2, idx++);
                }
            }
            return 1;
        }
        if (strcmp(prop, "players") == 0) {
            int cnt = 0;
            for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) if (sv->players[i].used) cnt++;
            lua_pop(L, 1); /* remove table */
            lua_pushinteger(L, cnt);
            return 1;
        }
        return 1;
    }

    if (id < 1 || id > CS2D_SV_MAX_PLAYERS) {
        if (strcmp(prop, "exists") == 0) { lua_pushboolean(L, 0); return 1; }
        return 0;
    }

    cs2d_sv_player_t *pl = &sv->players[id];
    if (!pl->used) {
        if (strcmp(prop, "exists") == 0) { lua_pushboolean(L, 0); return 1; }
        return 0;
    }

    if (strcmp(prop, "exists") == 0) { lua_pushboolean(L, 1); return 1; }
    if (strcmp(prop, "name") == 0) { lua_pushstring(L, pl->name); return 1; }
    if (strcmp(prop, "ip") == 0) { lua_pushstring(L, pl->ip); return 1; }
    if (strcmp(prop, "port") == 0) { lua_pushinteger(L, pl->port); return 1; }
    if (strcmp(prop, "usgn") == 0) { lua_pushinteger(L, pl->usgn); return 1; }
    if (strcmp(prop, "usgnname") == 0) { lua_pushstring(L, pl->usgn_name); return 1; }
    if (strcmp(prop, "steamid") == 0) {
        char sbuf[32];
        snprintf(sbuf, sizeof(sbuf), "%lld", (long long)pl->steam_id);
        lua_pushstring(L, sbuf);
        return 1;
    }
    if (strcmp(prop, "steamname") == 0) { lua_pushstring(L, pl->steam_name); return 1; }
    if (strcmp(prop, "ping") == 0) { lua_pushinteger(L, pl->ping); return 1; }
    if (strcmp(prop, "idle") == 0) {
        uint32_t now = cs2d_sv_now();
        lua_pushinteger(L, (now >= pl->last_move_ms) ? (now - pl->last_move_ms) / 1000 : 0);
        return 1;
    }
    if (strcmp(prop, "bot") == 0) { lua_pushboolean(L, pl->bot); return 1; }
    if (strcmp(prop, "team") == 0) { lua_pushinteger(L, pl->team); return 1; }
    if (strcmp(prop, "look") == 0) { lua_pushinteger(L, pl->look); return 1; }
    if (strcmp(prop, "x") == 0) { lua_pushnumber(L, (double)pl->x); return 1; }
    if (strcmp(prop, "y") == 0) { lua_pushnumber(L, (double)pl->y); return 1; }
    if (strcmp(prop, "rot") == 0) { lua_pushnumber(L, (double)pl->rot); return 1; }
    if (strcmp(prop, "tilex") == 0) { lua_pushinteger(L, (int)(pl->x / 32.0f)); return 1; }
    if (strcmp(prop, "tiley") == 0) { lua_pushinteger(L, (int)(pl->y / 32.0f)); return 1; }
    if (strcmp(prop, "mousemapx") == 0 || strcmp(prop, "mapmousex") == 0) { lua_pushinteger(L, (int)pl->x); return 1; }
    if (strcmp(prop, "mousemapy") == 0 || strcmp(prop, "mapmousey") == 0) { lua_pushinteger(L, (int)pl->y); return 1; }
    if (strcmp(prop, "mousex") == 0 || strcmp(prop, "mousey") == 0 || strcmp(prop, "mousedist") == 0) { lua_pushinteger(L, 0); return 1; }
    if (strcmp(prop, "language") == 0 || strcmp(prop, "language_iso") == 0) { lua_pushstring(L, "en"); return 1; }
    if (strcmp(prop, "favweapon") == 0) { lua_pushinteger(L, pl->weapon); return 1; }
    if (strcmp(prop, "health") == 0) { lua_pushinteger(L, pl->health); return 1; }
    if (strcmp(prop, "armor") == 0) { lua_pushinteger(L, pl->armor); return 1; }
    if (strcmp(prop, "money") == 0) { lua_pushinteger(L, pl->money); return 1; }
    if (strcmp(prop, "score") == 0) { lua_pushinteger(L, pl->score); return 1; }
    if (strcmp(prop, "assists") == 0) { lua_pushinteger(L, pl->assists); return 1; }
    if (strcmp(prop, "mvp") == 0) { lua_pushinteger(L, pl->mvp); return 1; }
    if (strcmp(prop, "deaths") == 0) { lua_pushinteger(L, pl->deaths); return 1; }
    if (strcmp(prop, "living") == 0 || strcmp(prop, "alive") == 0) { lua_pushboolean(L, pl->alive); return 1; }
    if (strcmp(prop, "weapontype") == 0 || strcmp(prop, "weptype") == 0) { lua_pushinteger(L, pl->weapon); return 1; }
    if (strcmp(prop, "weapon") == 0) {
        const cs2d_weapon_def_t *def = cs2d_weapon_get((uint8_t)pl->weapon);
        lua_pushstring(L, (def && def->name) ? def->name : "");
        return 1;
    }
    if (strcmp(prop, "weaponmode") == 0 || strcmp(prop, "wepmode") == 0) { lua_pushinteger(L, 0); return 1; }
    if (strcmp(prop, "nightvision") == 0) { lua_pushboolean(L, 0); return 1; }
    if (strcmp(prop, "defusekit") == 0) { lua_pushboolean(L, 0); return 1; }
    if (strcmp(prop, "gasmask") == 0) { lua_pushboolean(L, 0); return 1; }
    if (strcmp(prop, "bomb") == 0) { lua_pushboolean(L, 0); return 1; }
    if (strcmp(prop, "flag") == 0) { lua_pushboolean(L, 0); return 1; }
    if (strcmp(prop, "reloading") == 0) { lua_pushboolean(L, pl->is_reloading ? 1 : 0); return 1; }
    if (strcmp(prop, "process") == 0) { lua_pushinteger(L, 0); return 1; }
    if (strcmp(prop, "speedmod") == 0) { lua_pushinteger(L, pl->speedmod); return 1; }
    if (strcmp(prop, "maxhealth") == 0) { lua_pushinteger(L, pl->maxhealth); return 1; }
    if (strcmp(prop, "rcon") == 0) { lua_pushboolean(L, pl->rcon_auth); return 1; }
    if (strcmp(prop, "spectating") == 0) { lua_pushinteger(L, pl->spectating_player); return 1; }
    if (strcmp(prop, "screenw") == 0) { lua_pushinteger(L, 850); return 1; }
    if (strcmp(prop, "screenh") == 0) { lua_pushinteger(L, 480); return 1; }

    return 0;
}

/* playerweapons(id) -> { [1] = type, [2] = type, ... } */
static int l_playerweapons(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv) return 0;
    int id = (int)luaL_checkinteger(L, 1);
    lua_newtable(L);
    if (id < 1 || id > CS2D_SV_MAX_PLAYERS || !sv->players[id].used) return 1;

    cs2d_sv_player_t *pl = &sv->players[id];
    int idx = 1;
    for (int i = 0; i < pl->item_count; i++) {
        if (pl->items[i] > 0) {
            lua_pushinteger(L, pl->items[i]);
            lua_rawseti(L, -2, idx++);
        }
    }
    return 1;
}

/* playerammo(id, type) -> clip, reserve or false */
static int l_playerammo(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv) return 0;
    int id = (int)luaL_checkinteger(L, 1);
    int type = (int)luaL_checkinteger(L, 2);
    if (id < 1 || id > CS2D_SV_MAX_PLAYERS || !sv->players[id].used || type < 0 || type >= CS2D_MAX_WEAPONS) {
        lua_pushboolean(L, 0);
        return 1;
    }
    cs2d_sv_player_t *pl = &sv->players[id];
    bool has_weapon = false;
    for (int i = 0; i < pl->item_count; i++) {
        if (pl->items[i] == type) { has_weapon = true; break; }
    }
    if (!has_weapon) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushinteger(L, pl->clip[type]);
    lua_pushinteger(L, pl->reserve[type]);
    return 2;
}

/* timer(ms, func, param [, count]) */
static int l_timer(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv || !sv->lua) return 0;
    cs2d_sv_lua_t *lua = (cs2d_sv_lua_t *)sv->lua;

    int ms = (int)luaL_checkinteger(L, 1);
    const char *func = luaL_checkstring(L, 2);
    const char *param = luaL_optstring(L, 3, "");
    int count = (int)luaL_optinteger(L, 4, 1);

    for (int i = 0; i < CS2D_SV_MAX_TIMERS; i++) {
        if (!lua->timers[i].active) {
            lua->timers[i].active = true;
            lua->timers[i].start_ms = cs2d_sv_now();
            lua->timers[i].interval_ms = ms;
            lua->timers[i].count = count;
            snprintf(lua->timers[i].func, sizeof(lua->timers[i].func), "%s", func);
            snprintf(lua->timers[i].param, sizeof(lua->timers[i].param), "%s", param);
            lua_pushinteger(L, 1);
            return 1;
        }
    }
    lua_pushinteger(L, 0);
    return 1;
}

/* freetimer(func [, param]) */
static int l_freetimer(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv || !sv->lua) return 0;
    cs2d_sv_lua_t *lua = (cs2d_sv_lua_t *)sv->lua;
    const char *func = luaL_optstring(L, 1, "");
    const char *param = luaL_optstring(L, 2, "");

    for (int i = 0; i < CS2D_SV_MAX_TIMERS; i++) {
        if (!lua->timers[i].active) continue;
        if (func[0] == '\0') {
            lua->timers[i].active = false;
        } else if (strcmp(lua->timers[i].func, func) == 0) {
            if (param[0] == '\0' || strcmp(lua->timers[i].param, param) == 0) {
                lua->timers[i].active = false;
            }
        }
    }
    return 0;
}

/* menu(id, text) */
static int l_menu(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv) return 0;
    int id = (int)luaL_checkinteger(L, 1);
    const char *text = luaL_checkstring(L, 2);

    cs2d_sv_menu_send(sv, id, text);
    return 0;
}

/* msg(text) */
static int l_msg(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv) return 0;
    const char *text = luaL_checkstring(L, 1);
    cs2d_sv_msg(sv, text, 0);
    return 0;
}

/* msg2(id, text) */
static int l_msg2(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv) return 0;
    int id = (int)luaL_checkinteger(L, 1);
    const char *text = luaL_checkstring(L, 2);
    cs2d_sv_msg(sv, text, id);
    return 0;
}

/* print(text) */
static int l_print(lua_State *L)
{
    const char *text = luaL_checkstring(L, 1);
    cs2d_server_print("%s", text);
    return 0;
}

/* image(path, x, y, mode [, player]) */
static int l_image(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv) return 0;
    const char *path = luaL_checkstring(L, 1);
    float x = (float)luaL_checknumber(L, 2);
    float y = (float)luaL_checknumber(L, 3);
    int mode = (int)luaL_checkinteger(L, 4);
    int player = (int)luaL_optinteger(L, 5, 0);

    int id = cs2d_sv_image_create(sv, path, x, y, mode, player);
    lua_pushinteger(L, id);
    return 1;
}

/* freeimage(id) */
static int l_freeimage(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv) return 0;
    int id = (int)luaL_checkinteger(L, 1);
    cs2d_sv_image_free(sv, id);
    return 0;
}

/* imagepos(id, x, y, rot) */
static int l_imagepos(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv) return 0;
    int id = (int)luaL_checkinteger(L, 1);
    float x = (float)luaL_checknumber(L, 2);
    float y = (float)luaL_checknumber(L, 3);
    float rot = (float)luaL_checknumber(L, 4);
    cs2d_sv_image_pos(sv, id, x, y, rot);
    return 0;
}

/* imagealpha(id, alpha) */
static int l_imagealpha(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv) return 0;
    int id = (int)luaL_checkinteger(L, 1);
    float alpha = (float)luaL_checknumber(L, 2);
    cs2d_sv_image_alpha(sv, id, alpha);
    return 0;
}

/* imagecolor(id, r, g, b) */
static int l_imagecolor(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv) return 0;
    int id = (int)luaL_checkinteger(L, 1);
    int r = (int)luaL_checkinteger(L, 2);
    int g = (int)luaL_checkinteger(L, 3);
    int b = (int)luaL_checkinteger(L, 4);
    cs2d_sv_image_color(sv, id, r, g, b);
    return 0;
}

/* imagescale(id, sx, sy) */
static int l_imagescale(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv) return 0;
    int id = (int)luaL_checkinteger(L, 1);
    float sx = (float)luaL_checknumber(L, 2);
    float sy = (float)luaL_checknumber(L, 3);
    cs2d_sv_image_scale(sv, id, sx, sy);
    return 0;
}

/* imageblend(id, blend) */
static int l_imageblend(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv) return 0;
    int id = (int)luaL_checkinteger(L, 1);
    int blend = (int)luaL_checkinteger(L, 2);
    cs2d_sv_image_blend(sv, id, blend);
    return 0;
}

/* imageframe(id, frame) */
static int l_imageframe(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv) return 0;
    int id = (int)luaL_checkinteger(L, 1);
    int frame = (int)luaL_checkinteger(L, 2);
    cs2d_sv_image_frame(sv, id, frame);
    return 0;
}

/* imageshadow(id, shadow) */
static int l_imageshadow(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv) return 0;
    int id = (int)luaL_checkinteger(L, 1);
    int shadow = (int)luaL_checkinteger(L, 2);
    cs2d_sv_image_shadow(sv, id, shadow);
    return 0;
}

/* imagehitzone(id, mode, xo, yo, w, h) */
static int l_imagehitzone(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv) return 0;
    int id = (int)luaL_checkinteger(L, 1);
    int mode = (int)luaL_checkinteger(L, 2);
    int xo = (int)luaL_checkinteger(L, 3);
    int yo = (int)luaL_checkinteger(L, 4);
    int w = (int)luaL_checkinteger(L, 5);
    int h = (int)luaL_checkinteger(L, 6);
    cs2d_sv_image_hitzone(sv, id, mode, xo, yo, w, h);
    return 0;
}

/* imageparam(id, param) */
static int l_imageparam(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv || !sv->lua) return 0;
    cs2d_sv_lua_t *lua = (cs2d_sv_lua_t *)sv->lua;
    int id = (int)luaL_checkinteger(L, 1);
    const char *param = luaL_checkstring(L, 2);

    for (int i = 0; i < CS2D_SV_MAX_IMAGES; i++) {
        if (lua->images[i].active && lua->images[i].id == id) {
            cs2d_sv_image_t *img = &lua->images[i];
            if (strcmp(param, "x") == 0) { lua_pushnumber(L, (double)img->x); return 1; }
            if (strcmp(param, "y") == 0) { lua_pushnumber(L, (double)img->y); return 1; }
            if (strcmp(param, "rot") == 0) { lua_pushnumber(L, (double)img->rot); return 1; }
            if (strcmp(param, "alpha") == 0) { lua_pushnumber(L, (double)img->alpha); return 1; }
            if (strcmp(param, "blend") == 0) { lua_pushinteger(L, img->blend); return 1; }
            if (strcmp(param, "mode") == 0) { lua_pushinteger(L, img->mode); return 1; }
            if (strcmp(param, "frame") == 0) { lua_pushinteger(L, img->frame); return 1; }
            if (strcmp(param, "shadow") == 0) { lua_pushinteger(L, img->shadow); return 1; }
            break;
        }
    }
    return 0;
}

/* Tween Lua functions */
static int l_tween_alpha(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv) return 0;
    int id = (int)luaL_checkinteger(L, 1);
    int dur = (int)luaL_checkinteger(L, 2);
    float a = (float)luaL_checknumber(L, 3);
    cs2d_sv_tween_alpha(sv, id, dur, a);
    return 0;
}

static int l_tween_color(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv) return 0;
    int id = (int)luaL_checkinteger(L, 1);
    int dur = (int)luaL_checkinteger(L, 2);
    int r = (int)luaL_checkinteger(L, 3);
    int g = (int)luaL_checkinteger(L, 4);
    int b = (int)luaL_checkinteger(L, 5);
    cs2d_sv_tween_color(sv, id, dur, r, g, b);
    return 0;
}

static int l_tween_move(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv) return 0;
    int id = (int)luaL_checkinteger(L, 1);
    int dur = (int)luaL_checkinteger(L, 2);
    float x = (float)luaL_checknumber(L, 3);
    float y = (float)luaL_checknumber(L, 4);
    cs2d_sv_tween_move(sv, id, dur, x, y);
    return 0;
}

static int l_tween_rotate(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv) return 0;
    int id = (int)luaL_checkinteger(L, 1);
    int dur = (int)luaL_checkinteger(L, 2);
    float rot = (float)luaL_checknumber(L, 3);
    cs2d_sv_tween_rotate(sv, id, dur, rot);
    return 0;
}

static int l_tween_rotateconstantly(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv) return 0;
    int id = (int)luaL_checkinteger(L, 1);
    float speed = (float)luaL_checknumber(L, 2);
    cs2d_sv_tween_rotateconstantly(sv, id, speed);
    return 0;
}

static int l_tween_scale(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv) return 0;
    int id = (int)luaL_checkinteger(L, 1);
    int dur = (int)luaL_checkinteger(L, 2);
    float sx = (float)luaL_checknumber(L, 3);
    float sy = (float)luaL_checknumber(L, 4);
    cs2d_sv_tween_scale(sv, id, dur, sx, sy);
    return 0;
}

static int l_tween_frame(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv) return 0;
    int id = (int)luaL_checkinteger(L, 1);
    int dur = (int)luaL_checkinteger(L, 2);
    int frame = (int)luaL_checkinteger(L, 3);
    cs2d_sv_tween_frame(sv, id, dur, frame);
    return 0;
}

static int l_tween_animate(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv) return 0;
    int id = (int)luaL_checkinteger(L, 1);
    int dur = (int)luaL_checkinteger(L, 2);
    int frame = (int)luaL_checkinteger(L, 3);
    int mode = (int)luaL_optinteger(L, 4, 0);
    cs2d_sv_tween_animate(sv, id, dur, frame, mode);
    return 0;
}

/* game(key) */
static int l_game(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv) return 0;
    const char *key = luaL_checkstring(L, 1);
    if (strcmp(key, "version") == 0) { lua_pushstring(L, "1.0.1.8"); return 1; }
    if (strcmp(key, "dedicated") == 0) { lua_pushinteger(L, 1); return 1; }
    if (strcmp(key, "map") == 0) { lua_pushstring(L, sv->set.sv_map); return 1; }
    if (strcmp(key, "phase") == 0) { lua_pushinteger(L, sv->round_end ? 3 : 1); return 1; }
    if (strcmp(key, "round") == 0) { lua_pushinteger(L, 1); return 1; }
    if (strcmp(key, "score_t") == 0) { lua_pushinteger(L, sv->score_t); return 1; }
    if (strcmp(key, "score_ct") == 0) { lua_pushinteger(L, sv->score_ct); return 1; }
    if (strcmp(key, "time") == 0) { lua_pushinteger(L, cs2d_sv_now() - sv->start_ms); return 1; }
    if (strcmp(key, "maptimeleft") == 0) { lua_pushinteger(L, 0); return 1; }
    if (strcmp(key, "ticks") == 0) { lua_pushinteger(L, sv->set.ticks); return 1; }
    if (strcmp(key, "port") == 0) { lua_pushinteger(L, sv->set.sv_hostport); return 1; }
    if (strcmp(key, "sysfolder") == 0) { lua_pushstring(L, "sys"); return 1; }
    if (strcmp(key, "bombplanted") == 0) { lua_pushboolean(L, sv->bomb_planted); return 1; }

    /* General cvar fallback (FUN_14013b6a0) */
    const char *cvar = cs2d_sv_settings_get(&sv->set, key);
    if (cvar) {
        lua_pushstring(L, cvar);
        return 1;
    }
    return 0;
}

/* map(prop) */
static int l_map(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv) return 0;
    const char *prop = luaL_checkstring(L, 1);
    if (strcmp(prop, "name") == 0) { lua_pushstring(L, sv->set.sv_map); return 1; }
    if (strcmp(prop, "xsize") == 0) { lua_pushinteger(L, sv->map.width); return 1; }
    if (strcmp(prop, "ysize") == 0) { lua_pushinteger(L, sv->map.height); return 1; }
    if (strcmp(prop, "tileset") == 0) { lua_pushstring(L, sv->map.tileset); return 1; }
    if (strcmp(prop, "tilecount") == 0) { lua_pushinteger(L, sv->map.tile_count); return 1; }
    if (strcmp(prop, "back_scrollx") == 0) { lua_pushinteger(L, sv->map.bg_scroll_x); return 1; }
    if (strcmp(prop, "back_scrolly") == 0) { lua_pushinteger(L, sv->map.bg_scroll_y); return 1; }
    if (strcmp(prop, "back_tile") == 0) { lua_pushinteger(L, sv->map.bg_image[0] ? 1 : 0); return 1; }
    if (strcmp(prop, "back_r") == 0) { lua_pushinteger(L, sv->map.bg_r); return 1; }
    if (strcmp(prop, "back_g") == 0) { lua_pushinteger(L, sv->map.bg_g); return 1; }
    if (strcmp(prop, "back_b") == 0) { lua_pushinteger(L, sv->map.bg_b); return 1; }
    if (strcmp(prop, "mission_vips") == 0) {
        int cnt = 0;
        for (uint32_t i = 0; i < sv->map.entity_count; i++) if (sv->map.entities[i].type == CS2D_ENT_INFO_VIP) cnt++;
        lua_pushinteger(L, cnt); return 1;
    }
    if (strcmp(prop, "mission_hostages") == 0) {
        lua_pushinteger(L, sv->hostage_count); return 1;
    }
    if (strcmp(prop, "mission_bombspots") == 0) {
        int cnt = 0;
        for (uint32_t i = 0; i < sv->map.entity_count; i++) if (sv->map.entities[i].type == CS2D_ENT_INFO_BOMBSPOT) cnt++;
        lua_pushinteger(L, cnt); return 1;
    }
    if (strcmp(prop, "mission_ctfflags") == 0) {
        int cnt = 0;
        for (uint32_t i = 0; i < sv->map.entity_count; i++) if (sv->map.entities[i].type == CS2D_ENT_INFO_CTF_FLAG) cnt++;
        lua_pushinteger(L, cnt); return 1;
    }
    if (strcmp(prop, "mission_dompoints") == 0) {
        int cnt = 0;
        for (uint32_t i = 0; i < sv->map.entity_count; i++) if (sv->map.entities[i].type == CS2D_ENT_INFO_DOM_POINT) cnt++;
        lua_pushinteger(L, cnt); return 1;
    }
    if (strcmp(prop, "noweapons") == 0) { lua_pushboolean(L, 0); return 1; }
    if (strcmp(prop, "teleporters") == 0) {
        int cnt = 0;
        for (uint32_t i = 0; i < sv->map.entity_count; i++) if (sv->map.entities[i].type == CS2D_ENT_FUNC_TELEPORT) cnt++;
        lua_pushinteger(L, cnt); return 1;
    }
    return 0;
}

/* tile(x, y, prop) */
static int l_tile(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv || !sv->map_loaded) return 0;
    int tx = (int)luaL_checkinteger(L, 1);
    int ty = (int)luaL_checkinteger(L, 2);
    const char *prop = luaL_checkstring(L, 3);

    int w = (int)sv->map.width;
    int h = (int)sv->map.height;
    if (tx < 0 || tx >= w || ty < 0 || ty >= h) {
        if (strcmp(prop, "frame") == 0 || strcmp(prop, "originalframe") == 0 || strcmp(prop, "property") == 0) {
            lua_pushinteger(L, 0);
            return 1;
        }
        return 0;
    }

    int idx = ty * w + tx;
    uint8_t frame = sv->map.tiles[idx];
    uint8_t mode = sv->map.tile_modes[frame];

    if (strcmp(prop, "frame") == 0) { lua_pushinteger(L, frame); return 1; }
    if (strcmp(prop, "originalframe") == 0) { lua_pushinteger(L, frame); return 1; }
    if (strcmp(prop, "hascustomframe") == 0) { lua_pushboolean(L, 0); return 1; }
    if (strcmp(prop, "property") == 0) { lua_pushinteger(L, mode); return 1; }
    if (strcmp(prop, "wall") == 0) {
        lua_pushboolean(L, mode == 1 || mode == 3 || mode == 5);
        return 1;
    }
    if (strcmp(prop, "obstacle") == 0) {
        lua_pushboolean(L, mode == 2 || mode == 4);
        return 1;
    }
    if (strcmp(prop, "deadly") == 0) {
        lua_pushboolean(L, mode >= 50 && mode <= 53);
        return 1;
    }
    if (strcmp(prop, "entity") == 0) {
        for (uint32_t i = 0; i < sv->map.entity_count; i++) {
            if (sv->map.entities[i].x == tx && sv->map.entities[i].y == ty) {
                lua_pushinteger(L, sv->map.entities[i].type);
                return 1;
            }
        }
        lua_pushinteger(L, 0);
        return 1;
    }
    if (strcmp(prop, "blend") == 0) { lua_pushinteger(L, 0); return 1; }
    if (strcmp(prop, "color") == 0) { lua_pushstring(L, "255 255 255"); return 1; }
    if (strcmp(prop, "brightness") == 0) { lua_pushinteger(L, 255); return 1; }
    if (strcmp(prop, "height") == 0) { lua_pushinteger(L, 0); return 1; }
    return 0;
}

/* tileproperty(frame) */
static int l_tileproperty(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv || !sv->map_loaded) return 0;
    int frame = (int)luaL_checkinteger(L, 1);
    if (frame >= 0 && frame < 256) {
        lua_pushinteger(L, sv->map.tile_modes[frame]);
    } else {
        lua_pushinteger(L, 0);
    }
    return 1;
}

/* itemtype(type, prop) */
static int l_itemtype(lua_State *L)
{
    int type = (int)luaL_checkinteger(L, 1);
    const char *prop = luaL_checkstring(L, 2);
    const cs2d_weapon_def_t *def = cs2d_weapon_get((uint8_t)type);
    if (!def) return 0;

    if (strcmp(prop, "name") == 0) { lua_pushstring(L, def->name ? def->name : ""); return 1; }
    if (strcmp(prop, "dmg") == 0) { lua_pushinteger(L, def->damage); return 1; }
    if (strcmp(prop, "dmg_z1") == 0) { lua_pushinteger(L, def->damage_z1); return 1; }
    if (strcmp(prop, "dmg_z2") == 0) { lua_pushinteger(L, def->damage_z2); return 1; }
    if (strcmp(prop, "rate") == 0) { lua_pushinteger(L, def->fire_delay_ticks); return 1; }
    if (strcmp(prop, "reload") == 0) { lua_pushinteger(L, def->reload_ticks); return 1; }
    if (strcmp(prop, "ammo") == 0) { lua_pushinteger(L, def->max_reserve); return 1; }
    if (strcmp(prop, "ammoin") == 0) { lua_pushinteger(L, def->clip_size); return 1; }
    if (strcmp(prop, "price") == 0) { lua_pushinteger(L, def->price); return 1; }
    if (strcmp(prop, "range") == 0) { lua_pushinteger(L, def->range); return 1; }
    if (strcmp(prop, "dispersion") == 0) { lua_pushinteger(L, def->dispersion); return 1; }
    if (strcmp(prop, "slot") == 0) { lua_pushinteger(L, def->slot); return 1; }
    if (strcmp(prop, "recoil") == 0) { lua_pushinteger(L, def->recoil_accuracy); return 1; }
    return 0;
}

/* item(id, prop) */
static int l_item(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv) return 0;
    int id = (int)luaL_checkinteger(L, 1);
    const char *prop = luaL_checkstring(L, 2);

    if (id == 0) {
        if (strcmp(prop, "table") == 0) {
            lua_newtable(L);
            int idx = 1;
            for (int i = 0; i < CS2D_SV_MAX_GROUND_ITEMS; i++) {
                if (sv->items[i].active) {
                    lua_pushinteger(L, sv->items[i].id);
                    lua_rawseti(L, -2, idx++);
                }
            }
            return 1;
        }
        return 0;
    }

    cs2d_sv_item_t *it = NULL;
    for (int i = 0; i < CS2D_SV_MAX_GROUND_ITEMS; i++) {
        if (sv->items[i].active && sv->items[i].id == id) {
            it = &sv->items[i];
            break;
        }
    }
    if (!it) {
        if (strcmp(prop, "exists") == 0) { lua_pushboolean(L, 0); return 1; }
        return 0;
    }

    if (strcmp(prop, "exists") == 0) { lua_pushboolean(L, 1); return 1; }
    if (strcmp(prop, "type") == 0) { lua_pushinteger(L, it->type); return 1; }
    if (strcmp(prop, "name") == 0) {
        const cs2d_weapon_def_t *def = cs2d_weapon_get(it->type);
        lua_pushstring(L, (def && def->name) ? def->name : "");
        return 1;
    }
    if (strcmp(prop, "player") == 0) { lua_pushinteger(L, 0); return 1; }
    if (strcmp(prop, "ammo") == 0) { lua_pushinteger(L, it->reserve); return 1; }
    if (strcmp(prop, "ammoin") == 0) { lua_pushinteger(L, it->clip); return 1; }
    if (strcmp(prop, "mode") == 0) { lua_pushinteger(L, it->sub); return 1; }
    if (strcmp(prop, "x") == 0) { lua_pushnumber(L, (double)(it->tile_x * 32 + 16)); return 1; }
    if (strcmp(prop, "y") == 0) { lua_pushnumber(L, (double)(it->tile_y * 32 + 16)); return 1; }
    if (strcmp(prop, "dropped") == 0) { lua_pushboolean(L, it->dropped); return 1; }
    if (strcmp(prop, "droptimer") == 0) {
        uint32_t now = cs2d_sv_now();
        lua_pushinteger(L, (now >= it->drop_time_ms) ? (now - it->drop_time_ms) : 0);
        return 1;
    }
    if (strcmp(prop, "physicsid") == 0) { lua_pushinteger(L, 0); return 1; }
    return 0;
}

/* object(id, prop) */
static int l_object(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv) return 0;
    int id = (int)luaL_checkinteger(L, 1);
    const char *prop = luaL_checkstring(L, 2);

    if (id == 0) {
        if (strcmp(prop, "table") == 0) {
            lua_newtable(L);
            int idx = 1;
            for (int i = 0; i < CS2D_DYNOBJ_MAX; i++) {
                if (sv->dynobj.objs[i].active) {
                    lua_pushinteger(L, sv->dynobj.objs[i].id);
                    lua_rawseti(L, -2, idx++);
                }
            }
            return 1;
        }
        return 0;
    }

    const cs2d_dynobj_t *obj = NULL;
    for (int i = 0; i < CS2D_DYNOBJ_MAX; i++) {
        if (sv->dynobj.objs[i].active && sv->dynobj.objs[i].id == id) {
            obj = &sv->dynobj.objs[i];
            break;
        }
    }
    if (!obj) {
        if (strcmp(prop, "exists") == 0) { lua_pushboolean(L, 0); return 1; }
        return 0;
    }

    if (strcmp(prop, "exists") == 0) { lua_pushboolean(L, 1); return 1; }
    if (strcmp(prop, "type") == 0) { lua_pushinteger(L, obj->typ); return 1; }
    if (strcmp(prop, "name") == 0) {
        const cs2d_dynobj_def_t *def = cs2d_dynobj_def(obj->typ);
        lua_pushstring(L, (def && def->name) ? def->name : "");
        return 1;
    }
    if (strcmp(prop, "health") == 0) { lua_pushinteger(L, obj->health); return 1; }
    if (strcmp(prop, "mode") == 0) { lua_pushinteger(L, obj->mode); return 1; }
    if (strcmp(prop, "team") == 0) { lua_pushinteger(L, obj->team); return 1; }
    if (strcmp(prop, "player") == 0) { lua_pushinteger(L, obj->player); return 1; }
    if (strcmp(prop, "x") == 0) { lua_pushnumber(L, (double)obj->x); return 1; }
    if (strcmp(prop, "y") == 0) { lua_pushnumber(L, (double)obj->y); return 1; }
    if (strcmp(prop, "rot") == 0) { lua_pushnumber(L, (double)obj->rot); return 1; }
    if (strcmp(prop, "tilex") == 0) { lua_pushinteger(L, (int)(obj->x * 0.03125f)); return 1; }
    if (strcmp(prop, "tiley") == 0) { lua_pushinteger(L, (int)(obj->y * 0.03125f)); return 1; }
    if (strcmp(prop, "countdown") == 0) { lua_pushinteger(L, obj->countdown); return 1; }
    if (strcmp(prop, "idle") == 0) { lua_pushboolean(L, obj->countdown == 0); return 1; }
    if (strcmp(prop, "upgrade") == 0) { lua_pushinteger(L, obj->upgrade); return 1; }
    if (strcmp(prop, "alpha") == 0) { lua_pushnumber(L, (double)obj->alpha); return 1; }
    return 0;
}

/* objectat(x, y [, type]) */
static int l_objectat(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv) return 0;
    int x = (int)luaL_checkinteger(L, 1);
    int y = (int)luaL_checkinteger(L, 2);
    int type = (int)luaL_optinteger(L, 3, 0);

    for (int i = 0; i < CS2D_DYNOBJ_MAX; i++) {
        const cs2d_dynobj_t *obj = &sv->dynobj.objs[i];
        if (!obj->active) continue;
        if (type > 0 && obj->typ != type) continue;
        int ox = (int)(obj->x * 0.03125f);
        int oy = (int)(obj->y * 0.03125f);
        if (ox == x && oy == y) {
            lua_pushinteger(L, obj->id);
            return 1;
        }
    }
    lua_pushinteger(L, 0);
    return 1;
}

/* closeobjects(x, y, radius [, type]) */
static int l_closeobjects(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv) return 0;
    float x = (float)luaL_checknumber(L, 1);
    float y = (float)luaL_checknumber(L, 2);
    float r = (float)luaL_checknumber(L, 3);
    int type = (int)luaL_optinteger(L, 4, 0);

    lua_newtable(L);
    int idx = 1;
    float r2 = r * r;
    for (int i = 0; i < CS2D_DYNOBJ_MAX; i++) {
        const cs2d_dynobj_t *obj = &sv->dynobj.objs[i];
        if (!obj->active) continue;
        if (type > 0 && obj->typ != type) continue;
        float dx = (obj->x + 16.0f) - x;
        float dy = (obj->y + 16.0f) - y;
        if (dx * dx + dy * dy <= r2) {
            lua_pushinteger(L, obj->id);
            lua_rawseti(L, -2, idx++);
        }
    }
    return 1;
}

/* closeplayers(x, y, radius [, team]) */
static int l_closeplayers(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv) return 0;
    float x = (float)luaL_checknumber(L, 1);
    float y = (float)luaL_checknumber(L, 2);
    float r = (float)luaL_checknumber(L, 3);
    int team = (int)luaL_optinteger(L, 4, 0);

    lua_newtable(L);
    int idx = 1;
    float r2 = r * r;
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        const cs2d_sv_player_t *pl = &sv->players[i];
        if (team == 1 && pl->team != 1) continue;
        if (team == 2 && pl->team < 2) continue;
        if (team > 2 && pl->team != team) continue;
        float dx = pl->x - x;
        float dy = pl->y - y;
        if (dx * dx + dy * dy <= r2) {
            lua_pushinteger(L, i);
            lua_rawseti(L, -2, idx++);
        }
    }
    return 1;
}

/* hascloseplayers(x, y, radius [, team]) - FUN_14004ce50 */
static int l_hascloseplayers(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv) return 0;
    float x = (float)luaL_checknumber(L, 1);
    float y = (float)luaL_checknumber(L, 2);
    float r = (float)luaL_checknumber(L, 3);
    int team = (int)luaL_optinteger(L, 4, 0);

    float r2 = r * r;
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        const cs2d_sv_player_t *pl = &sv->players[i];
        if (!pl->used || !pl->alive) continue;
        if (team == 1 && pl->team != 1) continue;
        if (team == 2 && pl->team < 2) continue;
        if (team > 2 && pl->team != team) continue;
        float dx = pl->x - x;
        float dy = pl->y - y;
        if (dx * dx + dy * dy <= r2) {
            lua_pushboolean(L, 1);
            return 1;
        }
    }
    lua_pushboolean(L, 0);
    return 1;
}

/* freeline(x1, y1, x2, y2 [, mode]) */
static int l_freeline(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv || !sv->map_loaded) return 0;
    float x1 = (float)luaL_checknumber(L, 1);
    float y1 = (float)luaL_checknumber(L, 2);
    float x2 = (float)luaL_checknumber(L, 3);
    float y2 = (float)luaL_checknumber(L, 4);
    int mode = (int)luaL_optinteger(L, 5, 0);

    float dx = x2 - x1;
    float dy = y2 - y1;
    float dist = sqrtf(dx * dx + dy * dy);
    if (dist <= 0.001f) {
        lua_pushboolean(L, 1);
        return 1;
    }

    int steps = (int)(dist / 5.0f);
    if (steps < 1) steps = 1;
    float step_x = dx / (float)steps;
    float step_y = dy / (float)steps;

    float cx = x1;
    float cy = y1;
    int mw = (int)sv->map.width;
    int mh = (int)sv->map.height;

    for (int s = 0; s <= steps; s++) {
        int tx = (int)(cx / 32.0f);
        int ty = (int)(cy / 32.0f);
        if (tx < 0 || tx >= mw || ty < 0 || ty >= mh) {
            lua_pushboolean(L, 0);
            return 1;
        }
        uint8_t frame = sv->map.tiles[ty * mw + tx];
        uint8_t tmode = sv->map.tile_modes[frame];
        if (tmode == 1 || tmode == 3 || tmode == 5 || (mode != 1 && (tmode == 2 || tmode == 4))) {
            lua_pushboolean(L, 0);
            return 1;
        }
        if (mode >= 2) {
            for (int p = 1; p <= CS2D_SV_MAX_PLAYERS; p++) {
                const cs2d_sv_player_t *pl = &sv->players[p];
                if (!pl->used || !pl->alive) continue;
                float pdx = pl->x - cx;
                float pdy = pl->y - cy;
                if (pdx * pdx + pdy * pdy <= 144.0f) { /* 12px player radius */
                    lua_pushboolean(L, 0);
                    return 1;
                }
            }
        }
        cx += step_x;
        cy += step_y;
    }

    lua_pushboolean(L, 1);
    return 1;
}

/* entity(x, y, prop) */
static int l_entity(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv || !sv->map_loaded) return 0;
    int x = (int)luaL_checkinteger(L, 1);
    int y = (int)luaL_checkinteger(L, 2);
    const char *prop = luaL_checkstring(L, 3);

    const cs2d_entity_t *ent = NULL;
    int ent_idx = -1;
    for (uint32_t i = 0; i < sv->map.entity_count; i++) {
        if (sv->map.entities[i].x == x && sv->map.entities[i].y == y) {
            ent = &sv->map.entities[i];
            ent_idx = (int)i;
            break;
        }
    }
    if (!ent) {
        if (strcmp(prop, "exists") == 0) { lua_pushboolean(L, 0); return 1; }
        return 0;
    }

    if (strcmp(prop, "exists") == 0) { lua_pushboolean(L, 1); return 1; }
    if (strcmp(prop, "typename") == 0) {
        const char *tname = cs2d_entity_type_name((cs2d_entity_type_t)ent->type);
        lua_pushstring(L, tname ? tname : "");
        return 1;
    }
    if (strcmp(prop, "type") == 0) { lua_pushinteger(L, ent->type); return 1; }
    if (strcmp(prop, "name") == 0) { lua_pushstring(L, ent->name); return 1; }
    if (strcmp(prop, "trigger") == 0) { lua_pushstring(L, ent->trigger); return 1; }
    if (strcmp(prop, "state") == 0) {
        lua_pushinteger(L, (ent_idx >= 0 && ent_idx < CS2D_MAX_ENTITIES) ? sv->map.entity_state[ent_idx] : 0);
        return 1;
    }
    if (strcmp(prop, "aistate") == 0) { lua_pushinteger(L, 0); return 1; }

    /* int0 .. int9 */
    if (strncmp(prop, "int", 3) == 0 && prop[3] >= '0' && prop[3] <= '9' && prop[4] == '\0') {
        int idx = prop[3] - '0';
        lua_pushinteger(L, ent->int_settings[idx]);
        return 1;
    }

    /* str0 .. str9 */
    if (strncmp(prop, "str", 3) == 0 && prop[3] >= '0' && prop[3] <= '9' && prop[4] == '\0') {
        int idx = prop[3] - '0';
        lua_pushstring(L, ent->str_settings[idx]);
        return 1;
    }

    return 0;
}

/* entitylist([type]) */
static int l_entitylist(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv || !sv->map_loaded) { lua_newtable(L); return 1; }
    int type = (int)luaL_optinteger(L, 1, -1);

    lua_newtable(L);
    int idx = 1;
    for (uint32_t i = 0; i < sv->map.entity_count; i++) {
        const cs2d_entity_t *e = &sv->map.entities[i];
        if (type != -1 && (int)e->type != type) continue;

        /* Create sub-table { x = e->x, y = e->y } */
        lua_createtable(L, 0, 2);
        lua_pushinteger(L, e->x);
        lua_setfield(L, -2, "x");
        lua_pushinteger(L, e->y);
        lua_setfield(L, -2, "y");

        /* Set string key "X Y" and sequential index idx */
        char key[32];
        snprintf(key, sizeof(key), "%d %d", e->x, e->y);
        lua_pushvalue(L, -1);
        lua_setfield(L, -3, key);

        lua_rawseti(L, -2, idx++);
    }
    return 1;
}

/* stats(id, prop) */
static int l_stats(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv) return 0;
    int id = (int)luaL_checkinteger(L, 1);
    const char *prop = luaL_checkstring(L, 2);

    if (id == 0) {
        if (strcmp(prop, "count") == 0) {
            int cnt = 0;
            for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) if (sv->players[i].used) cnt++;
            lua_pushinteger(L, cnt);
            return 1;
        }
        return 0;
    }

    if (id < 1 || id > CS2D_SV_MAX_PLAYERS || !sv->players[id].used) {
        if (strcmp(prop, "exists") == 0) { lua_pushboolean(L, 0); return 1; }
        return 0;
    }

    const cs2d_sv_player_t *pl = &sv->players[id];
    if (strcmp(prop, "exists") == 0) { lua_pushboolean(L, 1); return 1; }
    if (strcmp(prop, "rank") == 0) { lua_pushinteger(L, pl->rank); return 1; }
    if (strcmp(prop, "killsperdeath") == 0) {
        float kpd = pl->deaths > 0 ? (float)pl->score / (float)pl->deaths : (float)pl->score;
        lua_pushnumber(L, (double)kpd);
        return 1;
    }
    if (strcmp(prop, "score") == 0) { lua_pushinteger(L, pl->score); return 1; }
    if (strcmp(prop, "frags") == 0) { lua_pushinteger(L, pl->score); return 1; }
    if (strcmp(prop, "deaths") == 0) { lua_pushinteger(L, pl->deaths); return 1; }
    if (strcmp(prop, "secs") == 0) {
        uint32_t now = cs2d_sv_now();
        lua_pushinteger(L, (now >= sv->start_ms) ? (now - sv->start_ms) / 1000 : 0);
        return 1;
    }
    if (strcmp(prop, "assists") == 0) { lua_pushinteger(L, pl->assists); return 1; }
    return 0;
}

/* addbind(key) */
static int l_addbind(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv || !sv->lua) return 0;
    cs2d_sv_lua_t *lua = (cs2d_sv_lua_t *)sv->lua;
    const char *key = luaL_checkstring(L, 1);
    for (int i = 0; i < lua->bind_key_count; i++) {
        if (_stricmp(lua->bind_keys[i], key) == 0) {
            lua_pushboolean(L, 1);
            return 1;
        }
    }
    if (lua->bind_key_count < 64) {
        snprintf(lua->bind_keys[lua->bind_key_count++], 32, "%s", key);
    }
    lua_pushboolean(L, 1);
    return 1;
}

/* removebind(key) */
static int l_removebind(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv || !sv->lua) return 0;
    cs2d_sv_lua_t *lua = (cs2d_sv_lua_t *)sv->lua;
    const char *key = luaL_checkstring(L, 1);
    for (int i = 0; i < lua->bind_key_count; i++) {
        if (_stricmp(lua->bind_keys[i], key) == 0) {
            memmove(&lua->bind_keys[i], &lua->bind_keys[i + 1], (size_t)(lua->bind_key_count - 1 - i) * 32);
            lua->bind_key_count--;
            lua_pushboolean(L, 1);
            return 1;
        }
    }
    lua_pushboolean(L, 0);
    return 1;
}

/* removeallbinds() */
static int l_removeallbinds(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv || !sv->lua) return 0;
    cs2d_sv_lua_t *lua = (cs2d_sv_lua_t *)sv->lua;
    lua->bind_key_count = 0;
    lua_pushboolean(L, 1);
    return 1;
}

/* reqcld(...) */
static int l_reqcld(lua_State *L)
{
    (void)L;
    return 0;
}

/* reqhttp(...) */
static int l_reqhttp(lua_State *L)
{
    (void)L;
    return 0;
}

/* checksumfile("path") */
static int l_checksumfile(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    char sha[65] = "";
    if (cs2d_net_sha256_file(path, sha)) {
        lua_pushstring(L, sha);
    } else {
        lua_pushstring(L, "");
    }
    return 1;
}

/* checksumstring("str") */
static int l_checksumstring(lua_State *L)
{
    const char *str = luaL_checkstring(L, 1);
    /* Simple hash representation */
    uint32_t h = 5381;
    while (*str) h = ((h << 5) + h) + (uint8_t)(*str++);
    char buf[32];
    snprintf(buf, sizeof(buf), "%08x", h);
    lua_pushstring(L, buf);
    return 1;
}

/* closehostage(playerid) -> hostageid or 0 */
static int l_closehostage(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv) return 0;
    int pid = (int)luaL_checkinteger(L, 1);
    if (pid < 1 || pid > CS2D_SV_MAX_PLAYERS || !sv->players[pid].used) {
        lua_pushinteger(L, 0);
        return 1;
    }
    float px = sv->players[pid].x, py = sv->players[pid].y;
    int closest = 0;
    float min_d2 = 64.0f * 64.0f;
    for (int i = 0; i < sv->hostage_count; i++) {
        if (sv->hostages[i].health <= 0) continue;
        float dx = sv->hostages[i].x - px, dy = sv->hostages[i].y - py;
        float d2 = dx * dx + dy * dy;
        if (d2 < min_d2) {
            min_d2 = d2;
            closest = sv->hostages[i].id;
        }
    }
    lua_pushinteger(L, closest);
    return 1;
}

/* closeitems(id, range) -> table of item ids */
static int l_closeitems(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv) return 0;
    int id = (int)luaL_checkinteger(L, 1);
    float r = (float)luaL_optnumber(L, 2, 64.0);
    float r2 = r * r;
    lua_newtable(L);
    if (id < 1 || id > CS2D_SV_MAX_PLAYERS || !sv->players[id].used) return 1;
    float px = sv->players[id].x, py = sv->players[id].y;
    int idx = 1;
    for (int i = 0; i < CS2D_SV_MAX_GROUND_ITEMS; i++) {
        if (!sv->items[i].active) continue;
        float ix = sv->items[i].tile_x * 32.0f + 16.0f;
        float iy = sv->items[i].tile_y * 32.0f + 16.0f;
        float dx = ix - px, dy = iy - py;
        if (dx * dx + dy * dy <= r2) {
            lua_pushinteger(L, sv->items[i].id);
            lua_rawseti(L, -2, idx++);
        }
    }
    return 1;
}

/* fow_in(id, x, y) -> 1/0 visible in fog of war */
static int l_fow_in(lua_State *L)
{
    lua_pushboolean(L, 1);
    return 1;
}

/* freerect(x, y, w, h, mode) -> 1/0 */
static int l_freerect(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv || !sv->map_loaded) { lua_pushboolean(L, 0); return 1; }
    int x = (int)luaL_checkinteger(L, 1);
    int y = (int)luaL_checkinteger(L, 2);
    int w = (int)luaL_checkinteger(L, 3);
    int h = (int)luaL_checkinteger(L, 4);
    for (int cy = y; cy < y + h; cy++) {
        for (int cx = x; cx < x + w; cx++) {
            if (cx < 0 || cy < 0 || (uint32_t)cx >= sv->map.width || (uint32_t)cy >= sv->map.height) {
                lua_pushboolean(L, 0);
                return 1;
            }
            uint8_t m = cs2d_map_get_tile_mode(&sv->map, (uint32_t)cx, (uint32_t)cy);
            if (m == 1 || m == 2) {
                lua_pushboolean(L, 0);
                return 1;
            }
        }
    }
    lua_pushboolean(L, 1);
    return 1;
}

/* funcs() -> table of registered function names */
static int l_funcs(lua_State *L)
{
    lua_newtable(L);
    return 1;
}

/* vars() -> table of global variable names */
static int l_vars(lua_State *L)
{
    lua_newtable(L);
    return 1;
}

/* hostage(id, prop) */
static int l_hostage(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv) return 0;
    int id = (int)luaL_checkinteger(L, 1);
    const char *prop = luaL_checkstring(L, 2);
    for (int i = 0; i < sv->hostage_count; i++) {
        if (sv->hostages[i].id == id) {
            cs2d_sv_hostage_t *h = &sv->hostages[i];
            if (strcmp(prop, "exists") == 0) { lua_pushboolean(L, 1); return 1; }
            if (strcmp(prop, "x") == 0) { lua_pushnumber(L, h->x); return 1; }
            if (strcmp(prop, "y") == 0) { lua_pushnumber(L, h->y); return 1; }
            if (strcmp(prop, "tilex") == 0) { lua_pushinteger(L, (int)h->x / 32); return 1; }
            if (strcmp(prop, "tiley") == 0) { lua_pushinteger(L, (int)h->y / 32); return 1; }
            if (strcmp(prop, "health") == 0) { lua_pushinteger(L, h->health); return 1; }
            if (strcmp(prop, "follow") == 0) { lua_pushinteger(L, h->follow); return 1; }
            if (strcmp(prop, "look") == 0) { lua_pushinteger(L, h->look); return 1; }
            break;
        }
    }
    if (strcmp(prop, "exists") == 0) { lua_pushboolean(L, 0); return 1; }
    lua_pushnil(L);
    return 1;
}

/* randomhostage() -> id */
static int l_randomhostage(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv || sv->hostage_count == 0) { lua_pushinteger(L, 0); return 1; }
    int pick = sv->hostages[cs2d_sv_rand(0, sv->hostage_count - 1)].id;
    lua_pushinteger(L, pick);
    return 1;
}

/* inentityzone(x, y, type) -> boolean */
static int l_inentityzone(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv || !sv->map_loaded) { lua_pushboolean(L, 0); return 1; }
    int x = (int)luaL_checkinteger(L, 1);
    int y = (int)luaL_checkinteger(L, 2);
    int type = (int)luaL_checkinteger(L, 3);
    for (uint32_t i = 0; i < sv->map.entity_count; i++) {
        const cs2d_entity_t *e = &sv->map.entities[i];
        if (e->type == (cs2d_entity_type_t)type && (int)e->x == x && (int)e->y == y) {
            lua_pushboolean(L, 1);
            return 1;
        }
    }
    lua_pushboolean(L, 0);
    return 1;
}

/* objecttype(type, prop) */
static int l_objecttype(lua_State *L)
{
    int type = (int)luaL_checkinteger(L, 1);
    const char *prop = luaL_checkstring(L, 2);
    if (strcmp(prop, "name") == 0) {
        if (type == 1) lua_pushstring(L, "Barricade");
        else if (type == 2) lua_pushstring(L, "Barbed Wire");
        else if (type == 3) lua_pushstring(L, "Wall I");
        else if (type == 4) lua_pushstring(L, "Wall II");
        else if (type == 5) lua_pushstring(L, "Wall III");
        else if (type == 6) lua_pushstring(L, "Gate Field");
        else if (type == 7) lua_pushstring(L, "Dispenser");
        else if (type == 8) lua_pushstring(L, "Turret");
        else if (type == 9) lua_pushstring(L, "Supply");
        else if (type == 10) lua_pushstring(L, "Teleporter Entrance");
        else if (type == 11) lua_pushstring(L, "Teleporter Exit");
        else lua_pushstring(L, "Building");
        return 1;
    }
    if (strcmp(prop, "health") == 0) {
        lua_pushinteger(L, 500);
        return 1;
    }
    lua_pushnil(L);
    return 1;
}

/* projectile(id, player, prop) */
static int l_projectile(lua_State *L)
{
    const char *prop = luaL_checkstring(L, 3);
    if (strcmp(prop, "exists") == 0) { lua_pushboolean(L, 0); return 1; }
    if (strcmp(prop, "x") == 0 || strcmp(prop, "y") == 0) { lua_pushnumber(L, 0); return 1; }
    lua_pushnil(L);
    return 1;
}

/* projectilelist(...) -> table */
static int l_projectilelist(lua_State *L)
{
    lua_newtable(L);
    return 1;
}

/* randomentity(type) -> {x, y} */
static int l_randomentity(lua_State *L)
{
    cs2d_server_t *sv = get_sv_from_lua(L);
    if (!sv || !sv->map_loaded) return 0;
    int type = (int)luaL_checkinteger(L, 1);
    int matches[256];
    int count = 0;
    for (uint32_t i = 0; i < sv->map.entity_count && count < 256; i++) {
        if (sv->map.entities[i].type == (cs2d_entity_type_t)type) {
            matches[count++] = (int)i;
        }
    }
    if (count > 0) {
        const cs2d_entity_t *e = &sv->map.entities[matches[cs2d_sv_rand(0, count - 1)]];
        lua_newtable(L);
        lua_pushinteger(L, e->x);
        lua_setfield(L, -2, "x");
        lua_pushinteger(L, e->y);
        lua_setfield(L, -2, "y");
        return 1;
    }
    return 0;
}

/* setentityaistate(x, y, ai) */
static int l_setentityaistate(lua_State *L)
{
    (void)L;
    return 0;
}

/* steamstats(steamid, mode) */
static int l_steamstats(lua_State *L)
{
    lua_newtable(L);
    return 1;
}

/* AI functions */
static int l_ai_aim(lua_State *L) { (void)L; return 0; }
static int l_ai_attack(lua_State *L) { (void)L; return 0; }
static int l_ai_build(lua_State *L) { (void)L; return 0; }
static int l_ai_buy(lua_State *L) { (void)L; return 0; }
static int l_ai_debug(lua_State *L) { (void)L; return 0; }
static int l_ai_drop(lua_State *L) { (void)L; return 0; }
static int l_ai_findtarget(lua_State *L) { lua_pushinteger(L, 0); return 1; }
static int l_ai_freeline(lua_State *L) { lua_pushboolean(L, 1); return 1; }
static int l_ai_goto(lua_State *L) { (void)L; return 0; }
static int l_ai_iattack(lua_State *L) { (void)L; return 0; }
static int l_ai_move(lua_State *L) { (void)L; return 0; }
static int l_ai_radio(lua_State *L) { (void)L; return 0; }
static int l_ai_reload(lua_State *L) { (void)L; return 0; }
static int l_ai_respawn(lua_State *L) { (void)L; return 0; }
static int l_ai_rotate(lua_State *L) { (void)L; return 0; }
static int l_ai_say(lua_State *L) { (void)L; return 0; }
static int l_ai_sayteam(lua_State *L) { (void)L; return 0; }
static int l_ai_selectweapon(lua_State *L) { (void)L; return 0; }
static int l_ai_spray(lua_State *L) { (void)L; return 0; }
static int l_ai_use(lua_State *L) { (void)L; return 0; }

/* ---------------------------------------------------------------------------
 * Initialization and Shutdown
 * --------------------------------------------------------------------------- */

bool cs2d_sv_lua_init(cs2d_server_t *sv)
{
    if (sv->lua) cs2d_sv_lua_close(sv);

    cs2d_sv_lua_t *lua = (cs2d_sv_lua_t *)calloc(1, sizeof(cs2d_sv_lua_t));
    if (!lua) return false;
    lua->sv = sv;
    lua->next_image_id = 1;
    sv->lua = lua;
    if (!cs2d_sv_get_active()) cs2d_sv_set_active(sv);

    lua->L = luaL_newstate();
    if (!lua->L) {
        free(lua);
        sv->lua = NULL;
        return false;
    }
    luaL_openlibs(lua->L);

    /* Store sv in registry for native cfunctions */
    lua_pushlightuserdata(lua->L, sv);
    lua_setfield(lua->L, LUA_REGISTRYINDEX, "cs2d_sv");

    /* Global constants */
    lua_pushstring(lua->L, "1.0.1.8");
    lua_setglobal(lua->L, "_CS2DVERSION");

    /* Register CS2D Lua API */
    lua_register(lua->L, "addhook", l_addhook);
    lua_register(lua->L, "freehook", l_freehook);
    lua_register(lua->L, "sethookstate", l_sethookstate);
    lua_register(lua->L, "parse", l_parse);
    lua_register(lua->L, "player", l_player);
    lua_register(lua->L, "playerweapons", l_playerweapons);
    lua_register(lua->L, "playerammo", l_playerammo);
    lua_register(lua->L, "timer", l_timer);
    lua_register(lua->L, "freetimer", l_freetimer);
    lua_register(lua->L, "menu", l_menu);
    lua_register(lua->L, "msg", l_msg);
    lua_register(lua->L, "msg2", l_msg2);
    lua_register(lua->L, "print", l_print);
    lua_register(lua->L, "game", l_game);

    /* Images */
    lua_register(lua->L, "image", l_image);
    lua_register(lua->L, "freeimage", l_freeimage);
    lua_register(lua->L, "imagepos", l_imagepos);
    lua_register(lua->L, "imagealpha", l_imagealpha);
    lua_register(lua->L, "imagecolor", l_imagecolor);
    lua_register(lua->L, "imagescale", l_imagescale);
    lua_register(lua->L, "imageblend", l_imageblend);
    lua_register(lua->L, "imageframe", l_imageframe);
    lua_register(lua->L, "imageshadow", l_imageshadow);
    lua_register(lua->L, "imagehitzone", l_imagehitzone);
    lua_register(lua->L, "imageparam", l_imageparam);

    /* Tweens */
    lua_register(lua->L, "tween_alpha", l_tween_alpha);
    lua_register(lua->L, "tween_color", l_tween_color);
    lua_register(lua->L, "tween_move", l_tween_move);
    lua_register(lua->L, "tween_rotate", l_tween_rotate);
    lua_register(lua->L, "tween_rotateconstantly", l_tween_rotateconstantly);
    lua_register(lua->L, "tween_scale", l_tween_scale);
    lua_register(lua->L, "tween_frame", l_tween_frame);
    lua_register(lua->L, "tween_animate", l_tween_animate);

    /* Game and world queries (Phase 6) */
    lua_register(lua->L, "map", l_map);
    lua_register(lua->L, "tile", l_tile);
    lua_register(lua->L, "tileproperty", l_tileproperty);
    lua_register(lua->L, "itemtype", l_itemtype);
    lua_register(lua->L, "item", l_item);
    lua_register(lua->L, "object", l_object);
    lua_register(lua->L, "objectat", l_objectat);
    lua_register(lua->L, "closeobjects", l_closeobjects);
    lua_register(lua->L, "closeplayers", l_closeplayers);
    lua_register(lua->L, "hascloseplayers", l_hascloseplayers);
    lua_register(lua->L, "freeline", l_freeline);
    lua_register(lua->L, "entity", l_entity);
    lua_register(lua->L, "entitylist", l_entitylist);
    lua_register(lua->L, "stats", l_stats);
    lua_register(lua->L, "addbind", l_addbind);
    lua_register(lua->L, "removebind", l_removebind);
    lua_register(lua->L, "removeallbinds", l_removeallbinds);
    lua_register(lua->L, "reqcld", l_reqcld);
    lua_register(lua->L, "reqhttp", l_reqhttp);

    /* Newly implemented commands */
    lua_register(lua->L, "checksumfile", l_checksumfile);
    lua_register(lua->L, "checksumstring", l_checksumstring);
    lua_register(lua->L, "closehostage", l_closehostage);
    lua_register(lua->L, "closeitems", l_closeitems);
    lua_register(lua->L, "fow_in", l_fow_in);
    lua_register(lua->L, "freerect", l_freerect);
    lua_register(lua->L, "funcs", l_funcs);
    lua_register(lua->L, "vars", l_vars);
    lua_register(lua->L, "hostage", l_hostage);
    lua_register(lua->L, "randomhostage", l_randomhostage);
    lua_register(lua->L, "inentityzone", l_inentityzone);
    lua_register(lua->L, "objecttype", l_objecttype);
    lua_register(lua->L, "projectile", l_projectile);
    lua_register(lua->L, "projectilelist", l_projectilelist);
    lua_register(lua->L, "randomentity", l_randomentity);
    lua_register(lua->L, "setentityaistate", l_setentityaistate);
    lua_register(lua->L, "steamstats", l_steamstats);

    /* AI commands */
    lua_register(lua->L, "ai_aim", l_ai_aim);
    lua_register(lua->L, "ai_attack", l_ai_attack);
    lua_register(lua->L, "ai_build", l_ai_build);
    lua_register(lua->L, "ai_buy", l_ai_buy);
    lua_register(lua->L, "ai_debug", l_ai_debug);
    lua_register(lua->L, "ai_drop", l_ai_drop);
    lua_register(lua->L, "ai_findtarget", l_ai_findtarget);
    lua_register(lua->L, "ai_freeline", l_ai_freeline);
    lua_register(lua->L, "ai_goto", l_ai_goto);
    lua_register(lua->L, "ai_iattack", l_ai_iattack);
    lua_register(lua->L, "ai_move", l_ai_move);
    lua_register(lua->L, "ai_radio", l_ai_radio);
    lua_register(lua->L, "ai_reload", l_ai_reload);
    lua_register(lua->L, "ai_respawn", l_ai_respawn);
    lua_register(lua->L, "ai_rotate", l_ai_rotate);
    lua_register(lua->L, "ai_say", l_ai_say);
    lua_register(lua->L, "ai_sayteam", l_ai_sayteam);
    lua_register(lua->L, "ai_selectweapon", l_ai_selectweapon);
    lua_register(lua->L, "ai_spray", l_ai_spray);
    lua_register(lua->L, "ai_use", l_ai_use);

    /* Load wrapper.lua if present */
    cs2d_sv_lua_dofile(sv, "sys/lua/wrapper.lua");

    /* Load server.lua (or mp_luaserver) if present */
    if (sv->set.mp_luaserver[0] != '\0') {
        char p[128];
        snprintf(p, sizeof(p), "sys/lua/%s", sv->set.mp_luaserver);
        cs2d_sv_lua_dofile(sv, p);
    } else {
        cs2d_sv_lua_dofile(sv, "sys/lua/server.lua");
    }

    /* Load autorun */
    cs2d_sv_lua_load_autorun(sv);

    /* Load map script if present and mp_luamap != 0 */
    if (sv->set.mp_luamap && sv->set.sv_map[0] != '\0') {
        cs2d_sv_lua_load_map(sv, sv->set.sv_map);
    }

    return true;
}

void cs2d_sv_lua_close(cs2d_server_t *sv)
{
    if (!sv || !sv->lua) return;
    cs2d_sv_lua_t *lua = (cs2d_sv_lua_t *)sv->lua;
    if (lua->L) {
        lua_close(lua->L);
        lua->L = NULL;
    }
    for (int i = 0; i < lua->hook_count; i++) {
        cs2d_hook_node_t *n = lua->hooks[i].head;
        while (n) {
            cs2d_hook_node_t *next = n->next;
            free(n);
            n = next;
        }
    }
    if (cs2d_sv_get_active() == sv) cs2d_sv_set_active(NULL);
    free(lua);
    sv->lua = NULL;
}

bool cs2d_sv_lua_dofile(cs2d_server_t *sv, const char *path)
{
    if (!sv || !sv->lua) return false;
    cs2d_sv_lua_t *lua = (cs2d_sv_lua_t *)sv->lua;
    if (luaL_dofile(lua->L, path) != 0) {
        /* If file does not exist, silent return false; if script syntax error, log */
        const char *err = lua_tostring(lua->L, -1);
        if (err && strstr(err, "cannot open") == NULL) {
            cs2d_server_print("Lua Error (%s): %s", path, err);
        }
        lua_pop(lua->L, 1);
        return false;
    }
    return true;
}

bool cs2d_sv_lua_dostring(cs2d_server_t *sv, const char *code)
{
    if (!sv || !sv->lua) return false;
    cs2d_sv_lua_t *lua = (cs2d_sv_lua_t *)sv->lua;
    if (luaL_dostring(lua->L, code) != 0) {
        const char *err = lua_tostring(lua->L, -1);
        cs2d_server_print("Lua Error: %s", err ? err : "unknown error");
        lua_pop(lua->L, 1);
        return false;
    }
    return true;
}

typedef struct {
    char names[64][64];
    int count;
} autorun_list_t;

static void autorun_dir_cb(const char *name, void *user)
{
    autorun_list_t *list = (autorun_list_t *)user;
    if (list->count < 64) {
        snprintf(list->names[list->count++], 64, "%s", name);
    }
}

void cs2d_sv_lua_load_autorun(cs2d_server_t *sv)
{
    autorun_list_t list = {0};
    cs2d_platform_list_dir("sys/lua/autorun", autorun_dir_cb, &list);
    int count = list.count;
    /* Simple bubble sort for alphabetical execution */
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (_stricmp(list.names[i], list.names[j]) > 0) {
                char tmp[64];
                memcpy(tmp, list.names[i], sizeof(tmp));
                memcpy(list.names[i], list.names[j], sizeof(tmp));
                memcpy(list.names[j], tmp, sizeof(tmp));
            }
        }
    }
    for (int i = 0; i < count; i++) {
        size_t len = strlen(list.names[i]);
        if (len > 4 && _stricmp(list.names[i] + len - 4, ".lua") == 0) {
            char path[128];
            snprintf(path, sizeof(path), "sys/lua/autorun/%s", list.names[i]);
            cs2d_sv_lua_dofile(sv, path);
        }
    }
}

void cs2d_sv_lua_load_map(cs2d_server_t *sv, const char *mapname)
{
    char path[128];
    snprintf(path, sizeof(path), "maps/%s.lua", mapname);
    cs2d_sv_lua_dofile(sv, path);
}

/* ---------------------------------------------------------------------------
 * Hook Management & Invocation
 * --------------------------------------------------------------------------- */

int cs2d_sv_lua_addhook(cs2d_server_t *sv, const char *hook_name, const char *func_name, int priority)
{
    if (!sv || !sv->lua) return 0;
    cs2d_sv_lua_t *lua = (cs2d_sv_lua_t *)sv->lua;

    /* Find or create hook */
    cs2d_hook_t *h = NULL;
    for (int i = 0; i < lua->hook_count; i++) {
        if (_stricmp(lua->hooks[i].name, hook_name) == 0) {
            h = &lua->hooks[i];
            break;
        }
    }
    if (!h) {
        if (lua->hook_count >= CS2D_SV_MAX_HOOKS) return 0;
        h = &lua->hooks[lua->hook_count++];
        snprintf(h->name, sizeof(h->name), "%s", hook_name);
        h->head = NULL;
    }

    /* Remove any existing registration of func_name in this hook */
    cs2d_hook_node_t **prev = &h->head;
    while (*prev) {
        if (strcmp((*prev)->func, func_name) == 0) {
            cs2d_hook_node_t *del = *prev;
            *prev = (*prev)->next;
            free(del);
            break;
        }
        prev = &(*prev)->next;
    }

    /* Insert sorted by priority ascending (lower runs earlier, higher runs later) */
    cs2d_hook_node_t *node = (cs2d_hook_node_t *)malloc(sizeof(cs2d_hook_node_t));
    if (!node) return 0;
    snprintf(node->func, sizeof(node->func), "%s", func_name);
    node->priority = priority;
    node->next = NULL;

    prev = &h->head;
    while (*prev && (*prev)->priority <= priority) {
        prev = &(*prev)->next;
    }
    node->next = *prev;
    *prev = node;
    return 1;
}

int cs2d_sv_lua_freehook(cs2d_server_t *sv, const char *hook_name, const char *func_name)
{
    if (!sv || !sv->lua) return 0;
    cs2d_sv_lua_t *lua = (cs2d_sv_lua_t *)sv->lua;
    for (int i = 0; i < lua->hook_count; i++) {
        if (_stricmp(lua->hooks[i].name, hook_name) == 0) {
            cs2d_hook_node_t **prev = &lua->hooks[i].head;
            while (*prev) {
                if (strcmp((*prev)->func, func_name) == 0) {
                    cs2d_hook_node_t *del = *prev;
                    *prev = (*prev)->next;
                    free(del);
                    return 1;
                }
                prev = &(*prev)->next;
            }
        }
    }
    return 0;
}

/* Generic internal hook caller */
static int dispatch_hook(cs2d_server_t *sv, const char *hook_name, int nargs, int (*push_args)(lua_State *L, void *ctx), void *ctx,
                         char *out_str, size_t max_out_str)
{
    if (!sv || !sv->lua) return 0;
    cs2d_sv_lua_t *lua = (cs2d_sv_lua_t *)sv->lua;
    cs2d_hook_t *h = NULL;
    for (int i = 0; i < lua->hook_count; i++) {
        if (_stricmp(lua->hooks[i].name, hook_name) == 0) {
            h = &lua->hooks[i];
            break;
        }
    }
    if (!h || !h->head) return 0;

    int last_int_res = 0;
    cs2d_hook_node_t *curr = h->head;
    while (curr) {
        if (lua_push_func(lua->L, curr->func)) {
            if (push_args) {
                push_args(lua->L, ctx);
            }
            if (lua_pcall(lua->L, nargs, 1, 0) != 0) {
                const char *err = lua_tostring(lua->L, -1);
                cs2d_server_print("Lua Hook Error (%s:%s): %s", hook_name, curr->func, err ? err : "unknown");
                lua_pop(lua->L, 1);
            } else {
                if (lua_isstring(lua->L, -1) && out_str && max_out_str > 0) {
                    snprintf(out_str, max_out_str, "%s", lua_tostring(lua->L, -1));
                }
                if (lua_isnumber(lua->L, -1)) {
                    last_int_res = (int)lua_tointeger(lua->L, -1);
                } else if (lua_isboolean(lua->L, -1)) {
                    last_int_res = lua_toboolean(lua->L, -1);
                }
                lua_pop(lua->L, 1);
            }
        }
        curr = curr->next;
    }
    return last_int_res;
}

/* Specific Hook Implementations */

typedef struct { int a, b, c, d, e, f, g; const char *s1, *s2, *s3; } hook_args_t;

static int push_1int(lua_State *L, void *ctx) {
    hook_args_t *a = (hook_args_t *)ctx;
    lua_pushinteger(L, a->a);
    return 1;
}

void cs2d_sv_lua_hook_join(cs2d_server_t *sv, int id)
{
    hook_args_t a = { .a = id };
    dispatch_hook(sv, "join", 1, push_1int, &a, NULL, 0);
}

static int push_leave(lua_State *L, void *ctx) {
    hook_args_t *a = (hook_args_t *)ctx;
    lua_pushinteger(L, a->a);
    lua_pushstring(L, a->s1 ? a->s1 : "");
    return 2;
}

void cs2d_sv_lua_hook_leave(cs2d_server_t *sv, int id, const char *reason)
{
    hook_args_t a = { .a = id, .s1 = reason };
    dispatch_hook(sv, "leave", 2, push_leave, &a, NULL, 0);
}

static int push_team(lua_State *L, void *ctx) {
    hook_args_t *a = (hook_args_t *)ctx;
    lua_pushinteger(L, a->a);
    lua_pushinteger(L, a->b);
    lua_pushinteger(L, a->c);
    return 3;
}

int cs2d_sv_lua_hook_team(cs2d_server_t *sv, int id, int team, int look)
{
    hook_args_t a = { .a = id, .b = team, .c = look };
    return dispatch_hook(sv, "team", 3, push_team, &a, NULL, 0);
}

int cs2d_sv_lua_hook_spawn(cs2d_server_t *sv, int id, char *out_items, size_t max_items)
{
    hook_args_t a = { .a = id };
    return dispatch_hook(sv, "spawn", 1, push_1int, &a, out_items, max_items);
}

static int push_die_kill(lua_State *L, void *ctx) {
    hook_args_t *a = (hook_args_t *)ctx;
    lua_pushinteger(L, a->a);
    lua_pushinteger(L, a->b);
    lua_pushinteger(L, a->c);
    lua_pushinteger(L, a->d);
    lua_pushinteger(L, a->e);
    lua_pushinteger(L, a->f);
    return 6;
}

void cs2d_sv_lua_hook_die(cs2d_server_t *sv, int victim, int killer, int weapon, int x, int y, int killerweapon)
{
    hook_args_t a = { .a = victim, .b = killer, .c = weapon, .d = x, .e = y, .f = killerweapon };
    dispatch_hook(sv, "die", 6, push_die_kill, &a, NULL, 0);
}

void cs2d_sv_lua_hook_kill(cs2d_server_t *sv, int killer, int victim, int weapon, int x, int y, int killerweapon)
{
    hook_args_t a = { .a = killer, .b = victim, .c = weapon, .d = x, .e = y, .f = killerweapon };
    dispatch_hook(sv, "kill", 6, push_die_kill, &a, NULL, 0);
}

static int push_menu(lua_State *L, void *ctx) {
    hook_args_t *a = (hook_args_t *)ctx;
    lua_pushinteger(L, a->a);
    lua_pushstring(L, a->s1 ? a->s1 : "");
    lua_pushinteger(L, a->b);
    return 3;
}

void cs2d_sv_lua_hook_menu(cs2d_server_t *sv, int id, const char *title, int button)
{
    hook_args_t a = { .a = id, .s1 = title, .b = button };
    dispatch_hook(sv, "menu", 3, push_menu, &a, NULL, 0);
}

static int push_2int(lua_State *L, void *ctx) {
    hook_args_t *a = (hook_args_t *)ctx;
    lua_pushinteger(L, a->a);
    lua_pushinteger(L, a->b);
    return 2;
}

void cs2d_sv_lua_hook_serveraction(cs2d_server_t *sv, int id, int action)
{
    hook_args_t a = { .a = id, .b = action };
    dispatch_hook(sv, "serveraction", 2, push_2int, &a, NULL, 0);
}

static int push_say(lua_State *L, void *ctx) {
    hook_args_t *a = (hook_args_t *)ctx;
    lua_pushinteger(L, a->a);
    lua_pushstring(L, a->s1 ? a->s1 : "");
    return 2;
}

int cs2d_sv_lua_hook_say(cs2d_server_t *sv, int id, const char *text)
{
    hook_args_t a = { .a = id, .s1 = text };
    return dispatch_hook(sv, "say", 2, push_say, &a, NULL, 0);
}

int cs2d_sv_lua_hook_sayteam(cs2d_server_t *sv, int id, const char *text)
{
    hook_args_t a = { .a = id, .s1 = text };
    return dispatch_hook(sv, "sayteam", 2, push_say, &a, NULL, 0);
}

void cs2d_sv_lua_hook_startround(cs2d_server_t *sv, int mode)
{
    hook_args_t a = { .a = mode };
    dispatch_hook(sv, "startround", 1, push_1int, &a, NULL, 0);
}

void cs2d_sv_lua_hook_endround(cs2d_server_t *sv, int mode)
{
    hook_args_t a = { .a = mode };
    dispatch_hook(sv, "endround", 1, push_1int, &a, NULL, 0);
}

void cs2d_sv_lua_hook_attack(cs2d_server_t *sv, int id)
{
    hook_args_t a = { .a = id };
    dispatch_hook(sv, "attack", 1, push_1int, &a, NULL, 0);
}

void cs2d_sv_lua_hook_attack2(cs2d_server_t *sv, int id, int mode)
{
    hook_args_t a = { .a = id, .b = mode };
    dispatch_hook(sv, "attack2", 2, push_2int, &a, NULL, 0);
}

static int push_3int(lua_State *L, void *ctx) {
    hook_args_t *a = (hook_args_t *)ctx;
    lua_pushinteger(L, a->a);
    lua_pushinteger(L, a->b);
    lua_pushinteger(L, a->c);
    return 3;
}

void cs2d_sv_lua_hook_select(cs2d_server_t *sv, int id, int type, int mode)
{
    hook_args_t a = { .a = id, .b = type, .c = mode };
    dispatch_hook(sv, "select", 3, push_3int, &a, NULL, 0);
}

void cs2d_sv_lua_hook_reload(cs2d_server_t *sv, int id, int mode)
{
    hook_args_t a = { .a = id, .b = mode };
    dispatch_hook(sv, "reload", 2, push_2int, &a, NULL, 0);
}

static int push_5int(lua_State *L, void *ctx) {
    hook_args_t *a = (hook_args_t *)ctx;
    lua_pushinteger(L, a->a);
    lua_pushinteger(L, a->b);
    lua_pushinteger(L, a->c);
    lua_pushinteger(L, a->d);
    lua_pushinteger(L, a->e);
    return 5;
}

int cs2d_sv_lua_hook_drop(cs2d_server_t *sv, int id, int itemid, int type, int ammo, int ammoin)
{
    hook_args_t a = { .a = id, .b = itemid, .c = type, .d = ammo, .e = ammoin };
    return dispatch_hook(sv, "drop", 5, push_5int, &a, NULL, 0);
}

int cs2d_sv_lua_hook_collect(cs2d_server_t *sv, int id, int itemid, int type, int ammo, int ammoin)
{
    hook_args_t a = { .a = id, .b = itemid, .c = type, .d = ammo, .e = ammoin };
    return dispatch_hook(sv, "collect", 5, push_5int, &a, NULL, 0);
}

int cs2d_sv_lua_hook_buy(cs2d_server_t *sv, int id, int type)
{
    hook_args_t a = { .a = id, .b = type };
    return dispatch_hook(sv, "buy", 2, push_2int, &a, NULL, 0);
}

int cs2d_sv_lua_hook_hit(cs2d_server_t *sv, int victim, int source, int weapon, int hpdmg, int apdmg, int rawdmg)
{
    hook_args_t a = { .a = victim, .b = source, .c = weapon, .d = hpdmg, .e = apdmg, .f = rawdmg };
    return dispatch_hook(sv, "hit", 6, push_die_kill, &a, NULL, 0);
}

static int push_rcon(lua_State *L, void *ctx) {
    hook_args_t *a = (hook_args_t *)ctx;
    lua_pushinteger(L, a->a);
    lua_pushstring(L, a->s1 ? a->s1 : "");
    lua_pushinteger(L, a->b);
    lua_pushstring(L, a->s2 ? a->s2 : "");
    return 4;
}

int cs2d_sv_lua_hook_rcon(cs2d_server_t *sv, int id, const char *ip, int port, const char *password, const char *cmd)
{
    (void)cmd;
    hook_args_t a = { .a = id, .s1 = ip, .b = port, .s2 = password };
    return dispatch_hook(sv, "rcon", 4, push_rcon, &a, NULL, 0);
}

static int push_name(lua_State *L, void *ctx) {
    hook_args_t *a = (hook_args_t *)ctx;
    lua_pushinteger(L, a->a);
    lua_pushstring(L, a->s1 ? a->s1 : "");
    lua_pushstring(L, a->s2 ? a->s2 : "");
    lua_pushboolean(L, a->b);
    return 4;
}

int cs2d_sv_lua_hook_name(cs2d_server_t *sv, int id, const char *oldname, const char *newname, bool forced)
{
    hook_args_t a = { .a = id, .s1 = oldname, .s2 = newname, .b = forced ? 1 : 0 };
    return dispatch_hook(sv, "name", 4, push_name, &a, NULL, 0);
}

static int push_str(lua_State *L, void *ctx) {
    const char *s = (const char *)ctx;
    lua_pushstring(L, s ? s : "");
    return 1;
}

int cs2d_sv_lua_hook_parse(cs2d_server_t *sv, const char *cmd)
{
    return dispatch_hook(sv, "parse", 1, push_str, (void *)cmd, NULL, 0);
}

void cs2d_sv_lua_log(cs2d_server_t *sv, const char *msg)
{
    if (!sv || !sv->lua || !msg) return;
    static bool s_in_log = false;
    if (s_in_log) return;
    s_in_log = true;
    dispatch_hook(sv, "log", 1, push_str, (void *)msg, NULL, 0);
    s_in_log = false;
}

static int push_key(lua_State *L, void *ctx) {
    hook_args_t *a = (hook_args_t *)ctx;
    lua_pushinteger(L, a->a);
    lua_pushstring(L, a->s1 ? a->s1 : "");
    lua_pushinteger(L, a->b);
    return 3;
}

void cs2d_sv_lua_hook_key(cs2d_server_t *sv, int id, const char *key, int state)
{
    if (!sv || !sv->lua || !key) return;
    hook_args_t a = { .a = id, .s1 = key, .b = state, .c = 0, .d = 0, .e = 0, .f = 0, .g = 0, .s2 = NULL };
    dispatch_hook(sv, "key", 3, push_key, &a, NULL, 0);
}

void cs2d_sv_lua_hook_bombplant(cs2d_server_t *sv, int id, int x, int y)
{
    hook_args_t a = { .a = id, .b = x, .c = y };
    dispatch_hook(sv, "bombplant", 3, push_3int, &a, NULL, 0);
}

void cs2d_sv_lua_hook_bombdefuse(cs2d_server_t *sv, int id)
{
    hook_args_t a = { .a = id };
    dispatch_hook(sv, "bombdefuse", 1, push_1int, &a, NULL, 0);
}

void cs2d_sv_lua_hook_bombexplode(cs2d_server_t *sv, int id, int x, int y)
{
    hook_args_t a = { .a = id, .b = x, .c = y };
    dispatch_hook(sv, "bombexplode", 3, push_3int, &a, NULL, 0);
}

static int push_5int_vals(lua_State *L, void *ctx) {
    hook_args_t *a = (hook_args_t *)ctx;
    lua_pushinteger(L, a->a);
    lua_pushinteger(L, a->b);
    lua_pushinteger(L, a->c);
    lua_pushinteger(L, a->d);
    lua_pushinteger(L, a->e);
    return 5;
}

void cs2d_sv_lua_hook_use(cs2d_server_t *sv, int id, int event, int data, int x, int y)
{
    hook_args_t a = { .a = id, .b = event, .c = data, .d = x, .e = y };
    dispatch_hook(sv, "use", 5, push_5int_vals, &a, NULL, 0);
}

void cs2d_sv_lua_hook_hostageuse(cs2d_server_t *sv, int id, int hostage_id, int mode)
{
    hook_args_t a = { .a = id, .b = hostage_id, .c = mode };
    dispatch_hook(sv, "hostageuse", 3, push_3int, &a, NULL, 0);
}

void cs2d_sv_lua_hook_hostagedamage(cs2d_server_t *sv, int id, int hostage_id, int damage)
{
    hook_args_t a = { .a = id, .b = hostage_id, .c = damage };
    dispatch_hook(sv, "hostagedamage", 3, push_3int, &a, NULL, 0);
}

void cs2d_sv_lua_hook_hostagekill(cs2d_server_t *sv, int id, int hostage_id, int damage)
{
    hook_args_t a = { .a = id, .b = hostage_id, .c = damage };
    dispatch_hook(sv, "hostagekill", 3, push_3int, &a, NULL, 0);
}

void cs2d_sv_lua_hook_hostagerescue(cs2d_server_t *sv, int id, int x, int y)
{
    hook_args_t a = { .a = id, .b = x, .c = y };
    dispatch_hook(sv, "hostagerescue", 3, push_3int, &a, NULL, 0);
}

void cs2d_sv_lua_hook_flashlight(cs2d_server_t *sv, int id, int state)
{
    hook_args_t a = { .a = id, .b = state };
    dispatch_hook(sv, "flashlight", 2, push_2int, &a, NULL, 0);
}

void cs2d_sv_lua_hook_projectile(cs2d_server_t *sv, int id, int type, int x, int y, int projectileid)
{
    hook_args_t a = { .a = id, .b = type, .c = x, .d = y, .e = projectileid };
    dispatch_hook(sv, "projectile", 5, push_5int_vals, &a, NULL, 0);
}

static int push_6int_vals(lua_State *L, void *ctx) {
    hook_args_t *a = (hook_args_t *)ctx;
    lua_pushinteger(L, a->a);
    lua_pushinteger(L, a->b);
    lua_pushinteger(L, a->c);
    lua_pushinteger(L, a->d);
    lua_pushinteger(L, a->e);
    lua_pushinteger(L, a->f);
    return 6;
}

void cs2d_sv_lua_hook_projectile_impact(cs2d_server_t *sv, int id, int type, int x, int y, int mode, int projectileid)
{
    hook_args_t a = { .a = id, .b = type, .c = x, .d = y, .e = mode, .f = projectileid };
    dispatch_hook(sv, "projectile_impact", 6, push_6int_vals, &a, NULL, 0);
}

static int push_4int_vals(lua_State *L, void *ctx) {
    hook_args_t *a = (hook_args_t *)ctx;
    lua_pushinteger(L, a->a);
    lua_pushinteger(L, a->b);
    lua_pushinteger(L, a->c);
    lua_pushinteger(L, a->d);
    return 4;
}

void cs2d_sv_lua_hook_break(cs2d_server_t *sv, int x, int y, int player)
{
    hook_args_t a = { .a = x, .b = y, .c = player };
    dispatch_hook(sv, "break", 3, push_3int, &a, NULL, 0);
}

void cs2d_sv_lua_hook_mapchange(cs2d_server_t *sv, const char *newmap)
{
    dispatch_hook(sv, "mapchange", 1, push_str, (void *)newmap, NULL, 0);
}

void cs2d_sv_lua_hook_spray(cs2d_server_t *sv, int id)
{
    hook_args_t a = { .a = id };
    dispatch_hook(sv, "spray", 1, push_1int, &a, NULL, 0);
}

void cs2d_sv_lua_hook_radio(cs2d_server_t *sv, int id, int message)
{
    hook_args_t a = { .a = id, .b = message };
    dispatch_hook(sv, "radio", 2, push_2int, &a, NULL, 0);
}

void cs2d_sv_lua_hook_startround_prespawn(cs2d_server_t *sv, int mode)
{
    hook_args_t a = { .a = mode };
    dispatch_hook(sv, "startround_prespawn", 1, push_1int, &a, NULL, 0);
}

void cs2d_sv_lua_hook_assist(cs2d_server_t *sv, int assist, int victim, int killer)
{
    hook_args_t a = { .a = assist, .b = victim, .c = killer };
    dispatch_hook(sv, "assist", 3, push_3int, &a, NULL, 0);
}

void cs2d_sv_lua_hook_suicide(cs2d_server_t *sv, int id)
{
    hook_args_t a = { .a = id };
    dispatch_hook(sv, "suicide", 1, push_1int, &a, NULL, 0);
}

void cs2d_sv_lua_hook_specswitch(cs2d_server_t *sv, int id, int target)
{
    hook_args_t a = { .a = id, .b = target };
    dispatch_hook(sv, "specswitch", 2, push_2int, &a, NULL, 0);
}

void cs2d_sv_lua_hook_voice(cs2d_server_t *sv, int id)
{
    hook_args_t a = { .a = id };
    dispatch_hook(sv, "voice", 1, push_1int, &a, NULL, 0);
}

static int push_vote(lua_State *L, void *ctx) {
    hook_args_t *a = (hook_args_t *)ctx;
    lua_pushinteger(L, a->a);
    lua_pushinteger(L, a->b);
    lua_pushstring(L, a->s1 ? a->s1 : "");
    return 3;
}

void cs2d_sv_lua_hook_vote(cs2d_server_t *sv, int id, int mode, const char *param)
{
    hook_args_t a = { .a = id, .b = mode, .s1 = param };
    dispatch_hook(sv, "vote", 3, push_vote, &a, NULL, 0);
}

void cs2d_sv_lua_hook_walkover(cs2d_server_t *sv, int id, int iid, int type, int ain, int a, int mode)
{
    hook_args_t args = { .a = id, .b = iid, .c = type, .d = ain, .e = a, .f = mode };
    dispatch_hook(sv, "walkover", 6, push_6int_vals, &args, NULL, 0);
}

void cs2d_sv_lua_hook_usebutton(cs2d_server_t *sv, int id, int x, int y)
{
    hook_args_t a = { .a = id, .b = x, .c = y };
    dispatch_hook(sv, "usebutton", 3, push_3int, &a, NULL, 0);
}

static int push_trigger(lua_State *L, void *ctx) {
    hook_args_t *a = (hook_args_t *)ctx;
    lua_pushstring(L, a->s1 ? a->s1 : "");
    lua_pushinteger(L, a->a);
    return 2;
}

void cs2d_sv_lua_hook_trigger(cs2d_server_t *sv, const char *trigger, int source)
{
    hook_args_t a = { .s1 = trigger, .a = source };
    dispatch_hook(sv, "trigger", 2, push_trigger, &a, NULL, 0);
}

void cs2d_sv_lua_hook_triggerentity(cs2d_server_t *sv, int x, int y)
{
    hook_args_t a = { .a = x, .b = y };
    dispatch_hook(sv, "triggerentity", 2, push_2int, &a, NULL, 0);
}

void cs2d_sv_lua_hook_vipescape(cs2d_server_t *sv, int id, int x, int y)
{
    hook_args_t a = { .a = id, .b = x, .c = y };
    dispatch_hook(sv, "vipescape", 3, push_3int, &a, NULL, 0);
}

void cs2d_sv_lua_hook_dominate(cs2d_server_t *sv, int id, int team, int x, int y)
{
    hook_args_t a = { .a = id, .b = team, .c = x, .d = y };
    dispatch_hook(sv, "dominate", 4, push_4int_vals, &a, NULL, 0);
}

void cs2d_sv_lua_hook_flagtake(cs2d_server_t *sv, int id, int team, int x, int y)
{
    hook_args_t a = { .a = id, .b = team, .c = x, .d = y };
    dispatch_hook(sv, "flagtake", 4, push_4int_vals, &a, NULL, 0);
}

void cs2d_sv_lua_hook_flagcapture(cs2d_server_t *sv, int id, int team, int x, int y)
{
    hook_args_t a = { .a = id, .b = team, .c = x, .d = y };
    dispatch_hook(sv, "flagcapture", 4, push_4int_vals, &a, NULL, 0);
}

int cs2d_sv_lua_hook_build(cs2d_server_t *sv, int id, int type, int x, int y, int mode, int objectid)
{
    hook_args_t a = { .a = id, .b = type, .c = x, .d = y, .e = mode, .f = objectid };
    return dispatch_hook(sv, "build", 6, push_6int_vals, &a, NULL, 0);
}

int cs2d_sv_lua_hook_buildattempt(cs2d_server_t *sv, int id, int type, int x, int y, int mode)
{
    hook_args_t a = { .a = id, .b = type, .c = x, .d = y, .e = mode };
    return dispatch_hook(sv, "buildattempt", 5, push_5int_vals, &a, NULL, 0);
}

int cs2d_sv_lua_hook_objectdamage(cs2d_server_t *sv, int id, int damage, int player)
{
    hook_args_t a = { .a = id, .b = damage, .c = player };
    return dispatch_hook(sv, "objectdamage", 3, push_3int, &a, NULL, 0);
}

void cs2d_sv_lua_hook_objectkill(cs2d_server_t *sv, int id, int player)
{
    hook_args_t a = { .a = id, .b = player };
    dispatch_hook(sv, "objectkill", 2, push_2int, &a, NULL, 0);
}

void cs2d_sv_lua_hook_objectupgrade(cs2d_server_t *sv, int id, int player, int progress, int total)
{
    hook_args_t a = { .a = id, .b = player, .c = progress, .d = total };
    dispatch_hook(sv, "objectupgrade", 4, push_4int_vals, &a, NULL, 0);
}

void cs2d_sv_lua_hook_move(cs2d_server_t *sv, int id, int x, int y, int walk)
{
    hook_args_t a = { .a = id, .b = x, .c = y, .d = walk };
    dispatch_hook(sv, "move", 4, push_4int_vals, &a, NULL, 0);
}

void cs2d_sv_lua_hook_movetile(cs2d_server_t *sv, int id, int x, int y)
{
    hook_args_t a = { .a = id, .b = x, .c = y };
    dispatch_hook(sv, "movetile", 3, push_3int, &a, NULL, 0);
}

void cs2d_sv_lua_hook_clientdata(cs2d_server_t *sv, int id, int mode, int data1, int data2)
{
    hook_args_t a = { .a = id, .b = mode, .c = data1, .d = data2 };
    dispatch_hook(sv, "clientdata", 4, push_4int_vals, &a, NULL, 0);
}

void cs2d_sv_lua_hook_clientsetting(cs2d_server_t *sv, int id, int setting, int val1, int val2)
{
    hook_args_t a = { .a = id, .b = setting, .c = val1, .d = val2 };
    dispatch_hook(sv, "clientsetting", 4, push_4int_vals, &a, NULL, 0);
}

void cs2d_sv_lua_hook_itemfadeout(cs2d_server_t *sv, int iid, int type, int x, int y)
{
    hook_args_t a = { .a = iid, .b = type, .c = x, .d = y };
    dispatch_hook(sv, "itemfadeout", 4, push_4int_vals, &a, NULL, 0);
}

void cs2d_sv_lua_hook_shutdown(cs2d_server_t *sv)
{
    dispatch_hook(sv, "shutdown", 0, NULL, NULL, NULL, 0);
}

static int push_connect(lua_State *L, void *ctx) {
    hook_args_t *a = (hook_args_t *)ctx;
    lua_pushstring(L, a->s1 ? a->s1 : "");
    lua_pushinteger(L, a->a);
    lua_pushstring(L, a->s2 ? a->s2 : "");
    lua_pushstring(L, a->s3 ? a->s3 : "");
    return 4;
}

int cs2d_sv_lua_hook_connect(cs2d_server_t *sv, const char *ip, int port, const char *usgn, const char *name)
{
    hook_args_t a = { .s1 = ip, .a = port, .s2 = usgn, .s3 = name };
    return dispatch_hook(sv, "connect", 4, push_connect, &a, NULL, 0);
}

/*
 * connect_attempt: 7 arguments in the order the original pushes them (cs2d_dedicated.exe
 * 0x1401de995, the +0xc8 slot of the hook list array - the client dump uses the same 200):
 * name (0x90 rsi), DottedIP(ip) (0x14041f690), port (u16 of the connection +0x10), usgn id,
 * usgn name, the steam id as a string (bbStringFromLong) and the steam name. The returned
 * value is read back as a string; the join path only looks at whether it is empty.
 */
typedef struct {
    const char *name, *ip, *usgn_name, *steam_name;
    int64_t     steam_id;
    int         port, usgn;
} connect_attempt_args_t;

static int push_connect_attempt(lua_State *L, void *ctx) {
    connect_attempt_args_t *a = (connect_attempt_args_t *)ctx;
    char steam[32];
    snprintf(steam, sizeof(steam), "%lld", (long long)a->steam_id);   /* help.php: steamID is a string */
    lua_pushstring(L, a->name ? a->name : "");
    lua_pushstring(L, a->ip ? a->ip : "");
    lua_pushinteger(L, a->port);
    lua_pushinteger(L, a->usgn);
    lua_pushstring(L, a->usgn_name ? a->usgn_name : "");
    lua_pushstring(L, steam);
    lua_pushstring(L, a->steam_name ? a->steam_name : "");
    return 7;
}

int cs2d_sv_lua_hook_connect_attempt(cs2d_server_t *sv, const char *name, const char *ip, int port,
                                     int usgn, const char *usgn_name, int64_t steam_id,
                                     const char *steam_name, char *out_reason, size_t max_reason)
{
    if (out_reason && max_reason > 0) out_reason[0] = '\0';
    connect_attempt_args_t a = { .name = name, .ip = ip, .usgn_name = usgn_name, .steam_name = steam_name,
                                 .steam_id = steam_id, .port = port, .usgn = usgn };
    dispatch_hook(sv, "connect_attempt", 7, push_connect_attempt, &a, out_reason, max_reason);
    return (out_reason && out_reason[0] != '\0') ? 1 : 0;
}

void cs2d_sv_lua_hook_connect_initplayer(cs2d_server_t *sv, int id)
{
    hook_args_t a = { .a = id };
    dispatch_hook(sv, "connect_initplayer", 1, push_1int, &a, NULL, 0);
}

void cs2d_sv_lua_hook_disconnect(cs2d_server_t *sv, int id, const char *reason)
{
    hook_args_t a = { .a = id, .s1 = reason };
    dispatch_hook(sv, "disconnect", 2, push_leave, &a, NULL, 0);
}

static int push_hitzone(lua_State *L, void *ctx) {
    hook_args_t *a = (hook_args_t *)ctx;
    lua_pushinteger(L, a->a);
    lua_pushinteger(L, a->b);
    lua_pushnumber(L, (double)a->c);
    lua_pushnumber(L, (double)a->d);
    return 4;
}

int cs2d_sv_lua_hook_hitzone(cs2d_server_t *sv, int image_id, int player, float x, float y)
{
    hook_args_t a = { .a = image_id, .b = player, .c = (int)x, .d = (int)y };
    return dispatch_hook(sv, "hitzone", 4, push_hitzone, &a, NULL, 0);
}

void cs2d_sv_lua_hook_httpdata(cs2d_server_t *sv, const char *data, int id)
{
    hook_args_t a = { .s1 = data, .a = id };
    dispatch_hook(sv, "httpdata", 2, push_trigger, &a, NULL, 0);
}

int cs2d_sv_lua_hook_sayutf8(cs2d_server_t *sv, int id, const char *text)
{
    hook_args_t a = { .a = id, .s1 = text };
    return dispatch_hook(sv, "sayutf8", 2, push_say, &a, NULL, 0);
}

int cs2d_sv_lua_hook_sayteamutf8(cs2d_server_t *sv, int id, const char *text)
{
    hook_args_t a = { .a = id, .s1 = text };
    return dispatch_hook(sv, "sayteamutf8", 2, push_say, &a, NULL, 0);
}

void cs2d_sv_lua_hook_shieldhit(cs2d_server_t *sv, int id, int weapon, int damage, int attacker)
{
    hook_args_t a = { .a = id, .b = weapon, .c = damage, .d = attacker };
    dispatch_hook(sv, "shieldhit", 4, push_4int_vals, &a, NULL, 0);
}

int cs2d_sv_lua_hook_turretscan(cs2d_server_t *sv, int object_id, int team, int x, int y)
{
    hook_args_t a = { .a = object_id, .b = team, .c = x, .d = y };
    return dispatch_hook(sv, "turretscan", 4, push_4int_vals, &a, NULL, 0);
}

/* ---------------------------------------------------------------------------
 * Timers and Frame Tick
 * --------------------------------------------------------------------------- */

void cs2d_sv_lua_step_timers(cs2d_server_t *sv)
{
    if (!sv || !sv->lua) return;
    cs2d_sv_lua_t *lua = (cs2d_sv_lua_t *)sv->lua;
    uint32_t now = cs2d_sv_now();

    for (int i = 0; i < CS2D_SV_MAX_TIMERS; i++) {
        if (!lua->timers[i].active) continue;
        cs2d_sv_timer_t *t = &lua->timers[i];
        if (now - t->start_ms >= (uint32_t)t->interval_ms) {
            t->start_ms = now;
            if (lua_push_func(lua->L, t->func)) {
                lua_pushstring(lua->L, t->param);
                if (lua_pcall(lua->L, 1, 0, 0) != 0) {
                    const char *err = lua_tostring(lua->L, -1);
                    cs2d_server_print("Lua Timer Error (%s): %s", t->func, err ? err : "unknown");
                    lua_pop(lua->L, 1);
                }
            }
            if (t->count > 0) {
                t->count--;
                if (t->count < 1) {
                    t->active = false;
                }
            }
        }
    }
}

void cs2d_sv_lua_frame(cs2d_server_t *sv)
{
    if (!sv || !sv->lua) return;
    cs2d_sv_lua_t *lua = (cs2d_sv_lua_t *)sv->lua;
    uint32_t now = cs2d_sv_now();

    /* hook "always": every frame */
    dispatch_hook(sv, "always", 0, NULL, NULL, NULL, 0);

    /* Timers */
    cs2d_sv_lua_step_timers(sv);

    /* hook "ms100": every 100 ms */
    if (now - lua->last_ms100 >= 100) {
        lua->last_ms100 = now;
        dispatch_hook(sv, "ms100", 0, NULL, NULL, NULL, 0);
    }

    /* hook "second": every 1000 ms */
    if (now - lua->last_second >= 1000) {
        lua->last_second = now;
        dispatch_hook(sv, "second", 0, NULL, NULL, NULL, 0);
    }

    /* hook "minute": every 60 s */
    if (now - lua->last_minute >= 60000) {
        lua->last_minute = now;
        dispatch_hook(sv, "minute", 0, NULL, NULL, NULL, 0);
    }
}

/* ---------------------------------------------------------------------------
 * Server Menu Sending (Opcode 235)
 * --------------------------------------------------------------------------- */

void cs2d_sv_menu_send(cs2d_server_t *sv, int id, const char *menu_str)
{
    uint16_t u16_buf[400];
    int slen = to_utf16le(menu_str, u16_buf, 400);

    /* [235, id u8, slen u16, slen * u16, 0xff] */
    int pkt_size = 1 + 1 + 2 + slen * 2 + 1;
    uint8_t *pkt = (uint8_t *)malloc((size_t)pkt_size);
    if (!pkt) return;

    pkt[0] = 235;
    pkt[1] = (uint8_t)id;
    pkt[2] = (uint8_t)(slen & 0xff);
    pkt[3] = (uint8_t)((slen >> 8) & 0xff);
    for (int i = 0; i < slen; i++) {
        pkt[4 + i * 2] = (uint8_t)(u16_buf[i] & 0xff);
        pkt[4 + i * 2 + 1] = (uint8_t)((u16_buf[i] >> 8) & 0xff);
    }
    pkt[pkt_size - 1] = 0xff;

    if (id > 0) {
        send_to_player(sv, id, pkt, pkt_size, true);
    } else {
        broadcast_to_joined(sv, pkt, pkt_size, true);
    }
    free(pkt);
}

/* ---------------------------------------------------------------------------
 * HUD Text Packet Helpers (Opcode 234 & 54)
 * --------------------------------------------------------------------------- */

void cs2d_sv_hudtxt(cs2d_server_t *sv, int target_pid, int id, const char *text, int x, int y, int align, int valign, int size)
{
    uint16_t u16_buf[256];
    int chars = to_utf16le(text, u16_buf, 256);

    /* [234, id u8, chars u16, chars*u16, (x+0x4000) u16, (y+0x4000) u16, align u8, valign u8, size u8] */
    int pkt_size = 1 + 1 + 2 + chars * 2 + 2 + 2 + 1 + 1 + 1;
    uint8_t *pkt = (uint8_t *)malloc((size_t)pkt_size);
    if (!pkt) return;

    int p = 0;
    pkt[p++] = 234;
    pkt[p++] = (uint8_t)id;
    pkt[p++] = (uint8_t)(chars & 0xff);
    pkt[p++] = (uint8_t)((chars >> 8) & 0xff);
    for (int i = 0; i < chars; i++) {
        pkt[p++] = (uint8_t)(u16_buf[i] & 0xff);
        pkt[p++] = (uint8_t)((u16_buf[i] >> 8) & 0xff);
    }
    uint16_t bx = (uint16_t)(x + 0x4000);
    uint16_t by = (uint16_t)(y + 0x4000);
    pkt[p++] = (uint8_t)(bx & 0xff);
    pkt[p++] = (uint8_t)((bx >> 8) & 0xff);
    pkt[p++] = (uint8_t)(by & 0xff);
    pkt[p++] = (uint8_t)((by >> 8) & 0xff);
    pkt[p++] = (uint8_t)align;
    pkt[p++] = (uint8_t)valign;
    pkt[p++] = (uint8_t)size;

    if (target_pid > 0) {
        send_to_player(sv, target_pid, pkt, p, true);
    } else {
        broadcast_to_joined(sv, pkt, p, true);
    }
    free(pkt);
}

void cs2d_sv_hudtxt_clear(cs2d_server_t *sv, int target_pid, int id)
{
    if (id < 0 || id == 255) {
        uint8_t pkt[2] = { 234, 255 };
        if (target_pid > 0) send_to_player(sv, target_pid, pkt, sizeof(pkt), true);
        else broadcast_to_joined(sv, pkt, sizeof(pkt), true);
    } else {
        cs2d_sv_hudtxt(sv, target_pid, id, "", 0, 0, 0, 0, 0);
    }
}

void cs2d_sv_hudtxt_alphafade(cs2d_server_t *sv, int target_pid, int id, int duration, float alpha)
{
    /* [54, mode 4, target (200 - id) u16, duration i32, alpha u8] */
    uint16_t target = (uint16_t)(200 - id);
    uint8_t a = (uint8_t)(alpha * 255.0f);
    uint8_t pkt[9];
    pkt[0] = 54;
    pkt[1] = 4;
    pkt[2] = (uint8_t)(target & 0xff);
    pkt[3] = (uint8_t)((target >> 8) & 0xff);
    pkt[4] = (uint8_t)(duration & 0xff);
    pkt[5] = (uint8_t)((duration >> 8) & 0xff);
    pkt[6] = (uint8_t)((duration >> 16) & 0xff);
    pkt[7] = (uint8_t)((duration >> 24) & 0xff);
    pkt[8] = a;

    if (target_pid > 0) send_to_player(sv, target_pid, pkt, sizeof(pkt), true);
    else broadcast_to_joined(sv, pkt, sizeof(pkt), true);
}

void cs2d_sv_hudtxt_colorfade(cs2d_server_t *sv, int target_pid, int id, int duration, int r, int g, int b)
{
    /* [54, mode 5, target (200 - id) u16, duration i32, r u8, g u8, b u8] */
    uint16_t target = (uint16_t)(200 - id);
    uint8_t pkt[11];
    pkt[0] = 54;
    pkt[1] = 5;
    pkt[2] = (uint8_t)(target & 0xff);
    pkt[3] = (uint8_t)((target >> 8) & 0xff);
    pkt[4] = (uint8_t)(duration & 0xff);
    pkt[5] = (uint8_t)((duration >> 8) & 0xff);
    pkt[6] = (uint8_t)((duration >> 16) & 0xff);
    pkt[7] = (uint8_t)((duration >> 24) & 0xff);
    pkt[8] = (uint8_t)r;
    pkt[9] = (uint8_t)g;
    pkt[10] = (uint8_t)b;

    if (target_pid > 0) send_to_player(sv, target_pid, pkt, sizeof(pkt), true);
    else broadcast_to_joined(sv, pkt, sizeof(pkt), true);
}

void cs2d_sv_hudtxt_move(cs2d_server_t *sv, int target_pid, int id, int duration, int x, int y)
{
    /* [54, mode 0, target (200 - id) u16, duration i32, (x+32768) u16, (y+32768) u16] */
    uint16_t target = (uint16_t)(200 - id);
    uint16_t bx = (uint16_t)(x + 32768);
    uint16_t by = (uint16_t)(y + 32768);
    uint8_t pkt[12];
    pkt[0] = 54;
    pkt[1] = 0;
    pkt[2] = (uint8_t)(target & 0xff);
    pkt[3] = (uint8_t)((target >> 8) & 0xff);
    pkt[4] = (uint8_t)(duration & 0xff);
    pkt[5] = (uint8_t)((duration >> 8) & 0xff);
    pkt[6] = (uint8_t)((duration >> 16) & 0xff);
    pkt[7] = (uint8_t)((duration >> 24) & 0xff);
    pkt[8] = (uint8_t)(bx & 0xff);
    pkt[9] = (uint8_t)((bx >> 8) & 0xff);
    pkt[10] = (uint8_t)(by & 0xff);
    pkt[11] = (uint8_t)((by >> 8) & 0xff);

    if (target_pid > 0) send_to_player(sv, target_pid, pkt, sizeof(pkt), true);
    else broadcast_to_joined(sv, pkt, sizeof(pkt), true);
}

static bool cs2d_sv_lua_parse_hudtxt(cs2d_server_t *sv, const char *cmd)
{
    if (strncmp(cmd, "hudtxt2 ", 8) == 0) {
        int pl = 0, id = 0, x = 0, y = 0, align = 0, valign = 0, size = 0;
        char text[128] = {0};
        const char *p = cmd + 8;
        if (sscanf(p, "%d %d", &pl, &id) == 2) {
            /* skip pl and id */
            while (*p && *p == ' ') p++;
            while (*p && *p != ' ') p++;
            while (*p && *p == ' ') p++;
            while (*p && *p != ' ') p++;
            while (*p && *p == ' ') p++;
            if (*p == '"') {
                p++;
                int ti = 0;
                while (*p && *p != '"' && ti < (int)sizeof(text) - 1) text[ti++] = *p++;
                if (*p == '"') p++;
            } else {
                int ti = 0;
                while (*p && *p != ' ' && ti < (int)sizeof(text) - 1) text[ti++] = *p++;
            }
            sscanf(p, "%d %d %d %d %d", &x, &y, &align, &valign, &size);
            cs2d_sv_hudtxt(sv, pl, id, text, x, y, align, valign, size);
            return true;
        }
    }
    if (strncmp(cmd, "hudtxt ", 7) == 0) {
        int id = 0, x = 0, y = 0, align = 0, valign = 0, size = 0;
        char text[128] = {0};
        const char *p = cmd + 7;
        if (sscanf(p, "%d", &id) == 1) {
            while (*p && *p == ' ') p++;
            while (*p && *p != ' ') p++;
            while (*p && *p == ' ') p++;
            if (*p == '"') {
                p++;
                int ti = 0;
                while (*p && *p != '"' && ti < (int)sizeof(text) - 1) text[ti++] = *p++;
                if (*p == '"') p++;
            } else {
                int ti = 0;
                while (*p && *p != ' ' && ti < (int)sizeof(text) - 1) text[ti++] = *p++;
            }
            sscanf(p, "%d %d %d %d %d", &x, &y, &align, &valign, &size);
            cs2d_sv_hudtxt(sv, 0, id, text, x, y, align, valign, size);
            return true;
        }
    }
    if (strncmp(cmd, "hudtxtclear", 11) == 0) {
        int pl = 0, id = 255;
        if (sscanf(cmd + 11, "%d %d", &pl, &id) == 2) {
            cs2d_sv_hudtxt_clear(sv, pl, id);
        } else if (sscanf(cmd + 11, "%d", &id) == 1) {
            cs2d_sv_hudtxt_clear(sv, 0, id);
        } else {
            cs2d_sv_hudtxt_clear(sv, 0, 255);
        }
        return true;
    }
    if (strncmp(cmd, "hudtxtalphafade ", 16) == 0) {
        int pl = 0, id = 0, dur = 0;
        float alpha = 1.0f;
        if (sscanf(cmd + 16, "%d %d %d %f", &pl, &id, &dur, &alpha) >= 4) {
            cs2d_sv_hudtxt_alphafade(sv, pl, id, dur, alpha);
            return true;
        }
    }
    if (strncmp(cmd, "hudtxtcolorfade ", 16) == 0) {
        int pl = 0, id = 0, dur = 0, r = 255, g = 255, b = 255;
        if (sscanf(cmd + 16, "%d %d %d %d %d %d", &pl, &id, &dur, &r, &g, &b) >= 6) {
            cs2d_sv_hudtxt_colorfade(sv, pl, id, dur, r, g, b);
            return true;
        }
    }
    if (strncmp(cmd, "hudtxtmove ", 11) == 0) {
        int pl = 0, id = 0, dur = 0, x = 0, y = 0;
        if (sscanf(cmd + 11, "%d %d %d %d %d", &pl, &id, &dur, &x, &y) >= 5) {
            cs2d_sv_hudtxt_move(sv, pl, id, dur, x, y);
            return true;
        }
    }
    return false;
}

/* ---------------------------------------------------------------------------
 * Image Helpers (Opcode 51 & 54)
 * --------------------------------------------------------------------------- */

static cs2d_sv_image_t *find_image(cs2d_server_t *sv, int id)
{
    if (!sv || !sv->lua) return NULL;
    cs2d_sv_lua_t *lua = (cs2d_sv_lua_t *)sv->lua;
    for (int i = 0; i < CS2D_SV_MAX_IMAGES; i++) {
        if (lua->images[i].active && lua->images[i].id == id) {
            return &lua->images[i];
        }
    }
    return NULL;
}

int cs2d_sv_image_create(cs2d_server_t *sv, const char *path, float x, float y, int mode, int player)
{
    if (!sv || !sv->lua) return 0;
    cs2d_sv_lua_t *lua = (cs2d_sv_lua_t *)sv->lua;

    cs2d_sv_image_t *img = NULL;
    for (int i = 0; i < CS2D_SV_MAX_IMAGES; i++) {
        if (!lua->images[i].active) {
            img = &lua->images[i];
            break;
        }
    }
    if (!img) return 0;

    int id = lua->next_image_id++;
    if (lua->next_image_id > 20000) lua->next_image_id = 1;

    memset(img, 0, sizeof(*img));
    img->active = true;
    img->id = (uint16_t)id;
    snprintf(img->path, sizeof(img->path), "%s", path);
    img->x = x;
    img->y = y;
    img->mode = (uint8_t)mode;
    img->player = (uint8_t)player;
    img->alpha = 1.0f;
    img->r = 255; img->g = 255; img->b = 255;
    img->sx = 1.0f; img->sy = 1.0f;
    img->frame = 1;

    /* [51, 0, path (len u8 + bytes), id u16, (x+32768) u16, (y+32768) u16, mode u8, player u8] */
    uint8_t plen = (uint8_t)strlen(path);
    int pkt_size = 2 + 1 + plen + 2 + 2 + 2 + 1 + 1;
    uint8_t *pkt = (uint8_t *)malloc((size_t)pkt_size);
    if (!pkt) return id;

    int p = 0;
    pkt[p++] = 51;
    pkt[p++] = 0;
    pkt[p++] = plen;
    memcpy(pkt + p, path, plen); p += plen;
    pkt[p++] = (uint8_t)(id & 0xff);
    pkt[p++] = (uint8_t)((id >> 8) & 0xff);
    uint16_t bx = (uint16_t)((int)x + 32768);
    uint16_t by = (uint16_t)((int)y + 32768);
    pkt[p++] = (uint8_t)(bx & 0xff);
    pkt[p++] = (uint8_t)((bx >> 8) & 0xff);
    pkt[p++] = (uint8_t)(by & 0xff);
    pkt[p++] = (uint8_t)((by >> 8) & 0xff);
    pkt[p++] = (uint8_t)mode;
    pkt[p++] = (uint8_t)player;

    if (mode == 2 && player > 0) {
        send_to_player(sv, player, pkt, p, true);
    } else {
        broadcast_to_joined(sv, pkt, p, true);
    }
    free(pkt);
    return id;
}

void cs2d_sv_image_free(cs2d_server_t *sv, int id)
{
    cs2d_sv_image_t *img = find_image(sv, id);
    if (!img) return;
    img->active = false;

    /* [51, 6, id u16] */
    uint8_t pkt[4] = { 51, 6, (uint8_t)(id & 0xff), (uint8_t)((id >> 8) & 0xff) };
    if (img->mode == 2 && img->player > 0) send_to_player(sv, img->player, pkt, sizeof(pkt), true);
    else broadcast_to_joined(sv, pkt, sizeof(pkt), true);
}

void cs2d_sv_image_pos(cs2d_server_t *sv, int id, float x, float y, float rot)
{
    cs2d_sv_image_t *img = find_image(sv, id);
    if (!img) return;
    img->x = x; img->y = y; img->rot = rot;

    /* [51, 5, id u16, (x+32768) u16, (y+32768) u16, rot_u16] */
    uint16_t bx = (uint16_t)((int)x + 32768);
    uint16_t by = (uint16_t)((int)y + 32768);
    uint16_t brot = (uint16_t)(rot * 182.04445f);
    uint8_t pkt[10] = {
        51, 5,
        (uint8_t)(id & 0xff), (uint8_t)((id >> 8) & 0xff),
        (uint8_t)(bx & 0xff), (uint8_t)((bx >> 8) & 0xff),
        (uint8_t)(by & 0xff), (uint8_t)((by >> 8) & 0xff),
        (uint8_t)(brot & 0xff), (uint8_t)((brot >> 8) & 0xff)
    };
    if (img->mode == 2 && img->player > 0) send_to_player(sv, img->player, pkt, sizeof(pkt), true);
    else broadcast_to_joined(sv, pkt, sizeof(pkt), true);
}

void cs2d_sv_image_alpha(cs2d_server_t *sv, int id, float alpha)
{
    cs2d_sv_image_t *img = find_image(sv, id);
    if (!img) return;
    img->alpha = alpha;

    /* [51, 2, id u16, alpha u8] */
    uint8_t a = (uint8_t)(alpha * 255.0f);
    uint8_t pkt[5] = { 51, 2, (uint8_t)(id & 0xff), (uint8_t)((id >> 8) & 0xff), a };
    if (img->mode == 2 && img->player > 0) send_to_player(sv, img->player, pkt, sizeof(pkt), true);
    else broadcast_to_joined(sv, pkt, sizeof(pkt), true);
}

void cs2d_sv_image_color(cs2d_server_t *sv, int id, int r, int g, int b)
{
    cs2d_sv_image_t *img = find_image(sv, id);
    if (!img) return;
    img->r = (uint8_t)r; img->g = (uint8_t)g; img->b = (uint8_t)b;

    /* [51, 1, id u16, r u8, g u8, b u8] */
    uint8_t pkt[7] = { 51, 1, (uint8_t)(id & 0xff), (uint8_t)((id >> 8) & 0xff), (uint8_t)r, (uint8_t)g, (uint8_t)b };
    if (img->mode == 2 && img->player > 0) send_to_player(sv, img->player, pkt, sizeof(pkt), true);
    else broadcast_to_joined(sv, pkt, sizeof(pkt), true);
}

void cs2d_sv_image_scale(cs2d_server_t *sv, int id, float sx, float sy)
{
    cs2d_sv_image_t *img = find_image(sv, id);
    if (!img) return;
    img->sx = sx; img->sy = sy;

    /* [51, 4, id u16, sx f32, sy f32] */
    uint8_t pkt[12];
    pkt[0] = 51; pkt[1] = 4;
    pkt[2] = (uint8_t)(id & 0xff); pkt[3] = (uint8_t)((id >> 8) & 0xff);
    memcpy(pkt + 4, &sx, 4);
    memcpy(pkt + 8, &sy, 4);
    if (img->mode == 2 && img->player > 0) send_to_player(sv, img->player, pkt, sizeof(pkt), true);
    else broadcast_to_joined(sv, pkt, sizeof(pkt), true);
}

void cs2d_sv_image_blend(cs2d_server_t *sv, int id, int blend)
{
    cs2d_sv_image_t *img = find_image(sv, id);
    if (!img) return;
    img->blend = (uint8_t)blend;

    /* [51, 3, id u16, blend u8] */
    uint8_t pkt[5] = { 51, 3, (uint8_t)(id & 0xff), (uint8_t)((id >> 8) & 0xff), (uint8_t)blend };
    if (img->mode == 2 && img->player > 0) send_to_player(sv, img->player, pkt, sizeof(pkt), true);
    else broadcast_to_joined(sv, pkt, sizeof(pkt), true);
}

void cs2d_sv_image_frame(cs2d_server_t *sv, int id, int frame)
{
    cs2d_sv_image_t *img = find_image(sv, id);
    if (!img) return;
    img->frame = (uint8_t)frame;

    /* [51, 7, id u16, frame u8] */
    uint8_t pkt[5] = { 51, 7, (uint8_t)(id & 0xff), (uint8_t)((id >> 8) & 0xff), (uint8_t)frame };
    if (img->mode == 2 && img->player > 0) send_to_player(sv, img->player, pkt, sizeof(pkt), true);
    else broadcast_to_joined(sv, pkt, sizeof(pkt), true);
}

void cs2d_sv_image_shadow(cs2d_server_t *sv, int id, int shadow)
{
    cs2d_sv_image_t *img = find_image(sv, id);
    if (!img) return;
    img->shadow = (uint8_t)shadow;

    /* [51, 9, id u16, shadow u8] */
    uint8_t pkt[5] = { 51, 9, (uint8_t)(id & 0xff), (uint8_t)((id >> 8) & 0xff), (uint8_t)shadow };
    if (img->mode == 2 && img->player > 0) send_to_player(sv, img->player, pkt, sizeof(pkt), true);
    else broadcast_to_joined(sv, pkt, sizeof(pkt), true);
}

void cs2d_sv_image_hitzone(cs2d_server_t *sv, int id, int mode, int xo, int yo, int w, int h)
{
    cs2d_sv_image_t *img = find_image(sv, id);
    if (!img) return;

    /* [59, id u16, mode u8, (xo+32768) u16, (yo+32768) u16, w u16, h u16] */
    uint16_t bxo = (uint16_t)(xo + 32768);
    uint16_t byo = (uint16_t)(yo + 32768);
    uint8_t pkt[12] = {
        59,
        (uint8_t)(id & 0xff), (uint8_t)((id >> 8) & 0xff),
        (uint8_t)mode,
        (uint8_t)(bxo & 0xff), (uint8_t)((bxo >> 8) & 0xff),
        (uint8_t)(byo & 0xff), (uint8_t)((byo >> 8) & 0xff),
        (uint8_t)(w & 0xff), (uint8_t)((w >> 8) & 0xff),
        (uint8_t)(h & 0xff), (uint8_t)((h >> 8) & 0xff)
    };
    if (img->mode == 2 && img->player > 0) send_to_player(sv, img->player, pkt, sizeof(pkt), true);
    else broadcast_to_joined(sv, pkt, sizeof(pkt), true);
}

void cs2d_sv_tween_alpha(cs2d_server_t *sv, int id, int duration, float alpha)
{
    cs2d_sv_image_t *img = find_image(sv, id);
    if (!img) return;
    uint16_t target = (uint16_t)(id + 200);
    uint8_t a = (uint8_t)(alpha * 255.0f);
    uint8_t pkt[9] = {
        54, 4,
        (uint8_t)(target & 0xff), (uint8_t)((target >> 8) & 0xff),
        (uint8_t)(duration & 0xff), (uint8_t)((duration >> 8) & 0xff),
        (uint8_t)((duration >> 16) & 0xff), (uint8_t)((duration >> 24) & 0xff),
        a
    };
    if (img->mode == 2 && img->player > 0) send_to_player(sv, img->player, pkt, sizeof(pkt), true);
    else broadcast_to_joined(sv, pkt, sizeof(pkt), true);
}

void cs2d_sv_tween_color(cs2d_server_t *sv, int id, int duration, int r, int g, int b)
{
    cs2d_sv_image_t *img = find_image(sv, id);
    if (!img) return;
    uint16_t target = (uint16_t)(id + 200);
    uint8_t pkt[11] = {
        54, 5,
        (uint8_t)(target & 0xff), (uint8_t)((target >> 8) & 0xff),
        (uint8_t)(duration & 0xff), (uint8_t)((duration >> 8) & 0xff),
        (uint8_t)((duration >> 16) & 0xff), (uint8_t)((duration >> 24) & 0xff),
        (uint8_t)r, (uint8_t)g, (uint8_t)b
    };
    if (img->mode == 2 && img->player > 0) send_to_player(sv, img->player, pkt, sizeof(pkt), true);
    else broadcast_to_joined(sv, pkt, sizeof(pkt), true);
}

void cs2d_sv_tween_move(cs2d_server_t *sv, int id, int duration, float x, float y)
{
    cs2d_sv_image_t *img = find_image(sv, id);
    if (!img) return;
    uint16_t target = (uint16_t)(id + 200);
    uint16_t bx = (uint16_t)((int)x + 32768);
    uint16_t by = (uint16_t)((int)y + 32768);
    uint8_t pkt[12] = {
        54, 0,
        (uint8_t)(target & 0xff), (uint8_t)((target >> 8) & 0xff),
        (uint8_t)(duration & 0xff), (uint8_t)((duration >> 8) & 0xff),
        (uint8_t)((duration >> 16) & 0xff), (uint8_t)((duration >> 24) & 0xff),
        (uint8_t)(bx & 0xff), (uint8_t)((bx >> 8) & 0xff),
        (uint8_t)(by & 0xff), (uint8_t)((by >> 8) & 0xff)
    };
    if (img->mode == 2 && img->player > 0) send_to_player(sv, img->player, pkt, sizeof(pkt), true);
    else broadcast_to_joined(sv, pkt, sizeof(pkt), true);
}

void cs2d_sv_tween_rotate(cs2d_server_t *sv, int id, int duration, float rot)
{
    cs2d_sv_image_t *img = find_image(sv, id);
    if (!img) return;
    uint16_t target = (uint16_t)(id + 200);
    uint16_t brot = (uint16_t)(rot * 182.04445f);
    uint8_t pkt[10] = {
        54, 2,
        (uint8_t)(target & 0xff), (uint8_t)((target >> 8) & 0xff),
        (uint8_t)(duration & 0xff), (uint8_t)((duration >> 8) & 0xff),
        (uint8_t)((duration >> 16) & 0xff), (uint8_t)((duration >> 24) & 0xff),
        (uint8_t)(brot & 0xff), (uint8_t)((brot >> 8) & 0xff)
    };
    if (img->mode == 2 && img->player > 0) send_to_player(sv, img->player, pkt, sizeof(pkt), true);
    else broadcast_to_joined(sv, pkt, sizeof(pkt), true);
}

void cs2d_sv_tween_rotateconstantly(cs2d_server_t *sv, int id, float speed)
{
    cs2d_sv_image_t *img = find_image(sv, id);
    if (!img) return;
    uint16_t target = (uint16_t)(id + 200);
    uint8_t pkt[8];
    pkt[0] = 54; pkt[1] = 3;
    pkt[2] = (uint8_t)(target & 0xff); pkt[3] = (uint8_t)((target >> 8) & 0xff);
    memcpy(pkt + 4, &speed, 4);
    if (img->mode == 2 && img->player > 0) send_to_player(sv, img->player, pkt, sizeof(pkt), true);
    else broadcast_to_joined(sv, pkt, sizeof(pkt), true);
}

void cs2d_sv_tween_scale(cs2d_server_t *sv, int id, int duration, float sx, float sy)
{
    cs2d_sv_image_t *img = find_image(sv, id);
    if (!img) return;
    uint16_t target = (uint16_t)(id + 200);
    uint8_t pkt[16];
    pkt[0] = 54; pkt[1] = 6;
    pkt[2] = (uint8_t)(target & 0xff); pkt[3] = (uint8_t)((target >> 8) & 0xff);
    pkt[4] = (uint8_t)(duration & 0xff); pkt[5] = (uint8_t)((duration >> 8) & 0xff);
    pkt[6] = (uint8_t)((duration >> 16) & 0xff); pkt[7] = (uint8_t)((duration >> 24) & 0xff);
    memcpy(pkt + 8, &sx, 4);
    memcpy(pkt + 12, &sy, 4);
    if (img->mode == 2 && img->player > 0) send_to_player(sv, img->player, pkt, sizeof(pkt), true);
    else broadcast_to_joined(sv, pkt, sizeof(pkt), true);
}

void cs2d_sv_tween_frame(cs2d_server_t *sv, int id, int duration, int frame)
{
    cs2d_sv_image_t *img = find_image(sv, id);
    if (!img) return;
    uint16_t target = (uint16_t)(id + 200);
    uint8_t pkt[9] = {
        54, 7,
        (uint8_t)(target & 0xff), (uint8_t)((target >> 8) & 0xff),
        (uint8_t)(duration & 0xff), (uint8_t)((duration >> 8) & 0xff),
        (uint8_t)((duration >> 16) & 0xff), (uint8_t)((duration >> 24) & 0xff),
        (uint8_t)frame
    };
    if (img->mode == 2 && img->player > 0) send_to_player(sv, img->player, pkt, sizeof(pkt), true);
    else broadcast_to_joined(sv, pkt, sizeof(pkt), true);
}

void cs2d_sv_tween_animate(cs2d_server_t *sv, int id, int duration, int frame, int mode)
{
    cs2d_sv_image_t *img = find_image(sv, id);
    if (!img) return;
    uint16_t target = (uint16_t)(id + 200);
    uint8_t pkt[10] = {
        54, 8,
        (uint8_t)(target & 0xff), (uint8_t)((target >> 8) & 0xff),
        (uint8_t)(duration & 0xff), (uint8_t)((duration >> 8) & 0xff),
        (uint8_t)((duration >> 16) & 0xff), (uint8_t)((duration >> 24) & 0xff),
        (uint8_t)frame, (uint8_t)mode
    };
    if (img->mode == 2 && img->player > 0) send_to_player(sv, img->player, pkt, sizeof(pkt), true);
    else broadcast_to_joined(sv, pkt, sizeof(pkt), true);
}

