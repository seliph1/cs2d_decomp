/*
 * Dedicated server core (cs2d_dedicated.exe 1.0.1.8 p1): Tudp transport, connections and the join.
 *
 * The join handler is FUN_1401dc530 (recv 252, Ghidra dump docs/dump/dedicated/FUN_1401dc530_join.c).
 * Its packet writer is the connection's Tudp_p (vtable: +0xa0 wbyte, +0xa8 wshort, +0xb0 wint,
 * +0xb8 wlong, +0xd0 wstr (u8 length), +0xd8 wlstr (u16 length), +0xe8 wbank); its reader
 * PTR_DAT_1405f7998 (+0x80 bytes left, +0xf0 rbyte, +0x100 rshort, +0x108 rint, +0x110 rlong,
 * +0x128 rstr, +0x130 rlstr). Createpacket(reliable, new) is the connection's vtable +0x98.
 */
#include "cs2d_server.h"
#include "cs2d_sv_ai.h"
#include "cs2d_weapons.h"
#include "cs2d_sv_lua.h"
#include "cs2d_bind.h"
#include "cs2d_serverlist.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

/* Reliable datagrams are sent again until their ACK arrives. The interval is not measured yet
 * (estimated); the original client never needed a resend on loopback. */
#define SV_RESEND_MS 500
/* A connection that sends nothing for this long is dropped (estimated, the original's Tudp timeout
 * is not identified yet). */
#define SV_TIMEOUT_MS 30000

#define sv_packet cs2d_sv_packet
#define sv_flush cs2d_sv_flush
#define conn_of_player cs2d_sv_conn_of_player

/* ------------------------------------------------------------------------------------------------ */
/* Console                                                                                          */
/* ------------------------------------------------------------------------------------------------ */

static cs2d_server_t *s_active_sv = NULL;

void cs2d_sv_set_active(cs2d_server_t *sv)
{
    s_active_sv = sv;
}

cs2d_server_t *cs2d_sv_get_active(void)
{
    return s_active_sv;
}

/* FUN_1400ce590 prints a line on the console; strips all "©RRRGGGBBB" color tags */
void cs2d_server_print(const char *fmt, ...)
{
    char line[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    char clean[2048];
    size_t d = 0;
    const char *s = line;
    while (*s && d + 1 < sizeof(clean)) {
        if ((uint8_t)s[0] == 0xa9) {
            bool is_col = true;
            for (int k = 1; k <= 9; k++) {
                if (s[k] < '0' || s[k] > '9') { is_col = false; break; }
            }
            if (is_col) {
                s += 10;
                continue;
            }
        } else if ((uint8_t)s[0] == 0xc2 && (uint8_t)s[1] == 0xa9) {
            bool is_col = true;
            for (int k = 2; k <= 10; k++) {
                if (s[k] < '0' || s[k] > '9') { is_col = false; break; }
            }
            if (is_col) {
                s += 11;
                continue;
            }
        }
        clean[d++] = *s++;
    }
    clean[d] = '\0';

    printf("%s\n", clean);
    fflush(stdout);

    if (s_active_sv && s_active_sv->lua) {
        cs2d_sv_lua_log(s_active_sv, clean);
    }
}

/* ------------------------------------------------------------------------------------------------ */
/* Settings                                                                                         */
/* ------------------------------------------------------------------------------------------------ */

void cs2d_sv_settings_default(cs2d_sv_settings_t *s)
{
    memset(s, 0, sizeof(*s));
    /* The values of the proxy capture of [252,6] with the shipped sys/server.cfg (proxy_ph1.log) */
    snprintf(s->sv_name, sizeof(s->sv_name), "CS2D Server");
    snprintf(s->sv_map, sizeof(s->sv_map), "de_dust2");
    s->sv_hostport        = 36963;
    s->sv_maxplayers      = 12;
    s->sv_specmode        = 1;
    s->sv_offscreendamage = 1;
    s->sv_spraytransfer   = 1;
    s->sv_maptransfer     = 1;
    s->mp_anticlock       = 1;   /* the capture pings with [229] */
    s->sv_daylighttime    = -1;
    s->sv_maxclientsperip = 5;   /* DAT_1405a6514 initial value */
    s->mp_startmoney      = 800; /* Fallback (CS default); shipped sys/server.cfg sets 16000; REA 0x14014c217 clamps to 0x3e80 = 16000 */
    s->mp_postspawn       = 30;  /* DAT_1405a64c4 initial value */
    s->mp_autoteambalance = 1;  /* [252,6] carries it + 1 = 0: the map's own time (proxy_ph1.log) */
    s->mp_roundtime       = 5;
    s->mp_c4timer         = 35;
    s->s_65d0             = 1;
    snprintf(s->mp_supply_items, sizeof(s->mp_supply_items), "51,58,11,22,32,35,80,73");
    s->s_65b8             = 127;
    s->s_65b0             = 1;
    s->mp_flashlight      = 1;
    s->mp_hovertext       = 31;
    s->lagcompensation    = 2;
    s->ticks              = 16;
    s->leaderboard        = 1;
    s->mp_luamap          = 1;
    snprintf(s->mp_luaserver, sizeof(s->mp_luaserver), "server.lua");
    s->mp_zombiespeedmod  = 100;
    s->mp_teleportreload  = 5;   /* DAT_140a14d48 in CS2D.exe .data: the 5 s the captures show */
    s->mp_dispenser_health = 10; /* DAT_140a14dac in CS2D.exe .data */
    s->mp_dispenser_money  = 500;/* DAT_140a14db0 in CS2D.exe .data (Changelog: "defaults to 500") */
    s->mp_dmspawnmoney     = 2000;/* DAT_1405a6554 in cs2d_dedicated.exe .data (Changelog: "2000$ by default") */
    s->debuglua            = 1;   /* DAT_1405a6398 in cs2d_dedicated.exe .data: 1 by default */
    /* P1 rules: defaults keep the behaviour verified before these cvars were configurable */
    s->mp_damagefactor     = 1.0;
    s->mp_weaponfadeout    = 15;
    s->mp_dropgrenades     = 1;
    s->mp_grenaderebuy     = 1;
    s->mp_floodprot        = 1;
    s->mp_floodprotignoretime = 600;  /* help.php: default duration 600 s */
    s->mp_antispeeder      = 1;
    s->mp_vulnerablehostages = 1;  /* keep the verified damage behaviour unless configured */
    s->mp_tempbantime      = 30;
    s->mp_idletime         = 60;
    s->sv_checkusgnlogin   = 1;
    /* P2 rules (help.php / measured defaults) */
    s->mp_kevlar           = 0.35;  /* DAT_1405a6508 (help.php says 0.3; the binary and tests use 0.35) */
    s->mp_turretdamage     = 5;     /* SV_TURRET_DAMAGE in cs2d_sv_game.c */
    s->mp_zombiedmg        = 0.5;
    s->mp_zombiekillscore  = 3;
    s->mp_zombiekillequip  = 86;    /* gut bomb */
    s->mp_zombierecover    = 3;
    s->mp_killbuildingmoney = 1;
    s->mp_mapgoalscore     = 1;
    /* Bot / AI settings of the shipped sys/server.cfg (bot_* are generic cvars: the AI script reads
     * them through game("bot_skill") etc. and the engine reads bot_freeze / bot_count / autofill). */
    {
        static const struct { const char *key, *val; } bot_defaults[] = {
            { "bot_prefix", "[b]" }, { "bot_count", "0" }, { "bot_autofill", "0" },
            { "bot_keepfreeslots", "0" }, { "bot_jointeam", "0" }, { "bot_skill", "4" },
            { "bot_weapons", "0" }, { "bot_freeze", "0" }, { "debugai", "0" }
        };
        s->cvar_count = 0;
        for (size_t i = 0; i < sizeof(bot_defaults) / sizeof(bot_defaults[0]); i++) {
            snprintf(s->cvars[s->cvar_count].key, sizeof(s->cvars[0].key), "%s", bot_defaults[i].key);
            snprintf(s->cvars[s->cvar_count].val, sizeof(s->cvars[0].val), "%s", bot_defaults[i].val);
            s->cvar_count++;
        }
    }
}

static void cfg_value(char *v)
{
    /* Strip the line end and optional quotes */
    size_t n = strlen(v);
    while (n > 0 && (v[n - 1] == '\r' || v[n - 1] == '\n' || v[n - 1] == ' ' || v[n - 1] == '\t')) v[--n] = '\0';
    if (n >= 2 && v[0] == '"' && v[n - 1] == '"') {
        memmove(v, v + 1, n - 2);
        v[n - 2] = '\0';
    }
}

void cs2d_sv_settings_set(cs2d_sv_settings_t *s, const char *key, const char *val)
{
    if (!s || !key || !key[0]) return;
    /* help.php aliases */
    if (_stricmp(key, "sv_gm") == 0) key = "sv_gamemode";
    else if (_stricmp(key, "mp_maxclientsip") == 0) key = "sv_maxclientsperip";
    if (_stricmp(key, "sv_rcon") == 0) {
        snprintf(s->sv_rcon, sizeof(s->sv_rcon), "%.50s", val);
    }
#define SV_INT(name) else if (_stricmp(key, #name) == 0) { s->name = atoi(val); }
#define SV_FLT(name) else if (_stricmp(key, #name) == 0) { s->name = atof(val); }
#define SV_STR(name) else if (_stricmp(key, #name) == 0) { snprintf(s->name, sizeof(s->name), "%s", val); }
    SV_STR(sv_name) SV_STR(sv_map) SV_STR(sv_password) SV_STR(mp_supply_items) SV_STR(sv_rconusers) SV_STR(mp_luaserver)
    SV_STR(mp_buymenu) SV_STR(mp_reservations) SV_STR(mp_unbuyable) SV_STR(mp_unbuildable)
    SV_INT(sv_hostport) SV_INT(sv_maxplayers) SV_INT(sv_fow) SV_INT(sv_specmode) SV_INT(sv_gamemode)
    SV_INT(sv_usgnonly) SV_INT(sv_friendlyfire) SV_INT(sv_forcelight) SV_INT(sv_daylighttime)
    SV_INT(sv_voicechat) SV_INT(sv_offscreendamage) SV_INT(sv_maxclientsperip) SV_INT(mp_anticlock)
    SV_INT(mp_roundtime) SV_INT(mp_freezetime) SV_INT(mp_c4timer) SV_INT(mp_respawndelay) SV_INT(mp_deadchat)
    SV_INT(mp_infammo) SV_INT(mp_flashlight) SV_INT(mp_hovertext) SV_INT(mp_recoil) SV_INT(mp_hudscale)
    SV_INT(sv_spraytransfer) SV_INT(sv_maptransfer) SV_INT(mp_startmoney) SV_INT(mp_postspawn) SV_INT(mp_autoteambalance)
    SV_INT(lagcompensation) SV_INT(ticks) SV_INT(leaderboard) SV_INT(physics)
    SV_INT(mp_luamap) SV_INT(mp_randomspawn) SV_INT(mp_curtailedexplosions) SV_INT(mp_zombiespeedmod)
    SV_INT(mp_timelimit) SV_INT(debuglua)
    else if (_stricmp(key, "gamemode") == 0) { s->sv_gamemode = atoi(val); }
    else if (_stricmp(key, "timelimit") == 0) { s->mp_timelimit = atoi(val); }
    else if (_stricmp(key, "randomspawn") == 0) { s->mp_randomspawn = atoi(val); }
    /* Construction (read from the CS2D.exe .data defaults; not in the [237] live broadcast of the original) */
    SV_INT(mp_teleportreload) SV_INT(mp_dispenser_health) SV_INT(mp_dispenser_money) SV_INT(mp_dmspawnmoney)
    /* P1 rules and admin settings (help.php) */
    SV_INT(mp_roundlimit) SV_INT(mp_winlimit) SV_INT(mp_weaponfadeout) SV_INT(mp_dropgrenades)
    SV_INT(mp_grenaderebuy) SV_INT(mp_teamkillpenalty) SV_INT(mp_tkpunish) SV_INT(mp_idlekick)
    SV_INT(mp_idletime) SV_INT(mp_idleaction) SV_INT(mp_pinglimit) SV_INT(mp_floodprot)
    SV_INT(mp_floodprotignoretime) SV_INT(mp_antispeeder) SV_INT(mp_maxrconfails) SV_INT(mp_tempbantime)
    SV_INT(mp_connectionlimit) SV_INT(mp_vulnerablehostages) SV_INT(mp_hostagepenalty) SV_INT(mp_deathdrop)
    SV_INT(sv_lan) SV_INT(sv_checkusgnlogin)
    SV_INT(mp_turretdamage) SV_INT(mp_zombiekillscore) SV_INT(mp_zombiekillequip) SV_INT(mp_zombierecover)
    SV_INT(mp_killbuildingmoney) SV_INT(mp_killteambuildings) SV_INT(mp_killbuildings)
    SV_INT(mp_mapgoalscore) SV_INT(mp_autogamemode)
    SV_FLT(mp_buytime) SV_FLT(mp_damagefactor) SV_FLT(mp_kevlar) SV_FLT(mp_zombiedmg)
    SV_FLT(mp_kickpercent) SV_FLT(mp_mapvoteratio)
#undef SV_INT
#undef SV_FLT
#undef SV_STR

    /* Store in generic cvar array */
    for (int i = 0; i < s->cvar_count; i++) {
        if (_stricmp(s->cvars[i].key, key) == 0) {
            snprintf(s->cvars[i].val, sizeof(s->cvars[i].val), "%s", val);
            return;
        }
    }
    if (s->cvar_count < CS2D_SV_MAX_CVARS) {
        snprintf(s->cvars[s->cvar_count].key, sizeof(s->cvars[s->cvar_count].key), "%s", key);
        snprintf(s->cvars[s->cvar_count].val, sizeof(s->cvars[s->cvar_count].val), "%s", val);
        s->cvar_count++;
    }
}

const char *cs2d_sv_settings_get(const cs2d_sv_settings_t *s, const char *key)
{
    if (!s || !key || !key[0]) return NULL;

    /* Typed struct fields have priority so live runtime mutations reflect immediately */
    if (_stricmp(key, "sv_name") == 0) return s->sv_name;
    if (_stricmp(key, "sv_map") == 0) return s->sv_map;
    if (_stricmp(key, "sv_password") == 0) return s->sv_password;
    if (_stricmp(key, "mp_luaserver") == 0) return s->mp_luaserver;
    if (_stricmp(key, "mp_supply_items") == 0) return s->mp_supply_items;
    if (_stricmp(key, "mp_buymenu") == 0) return s->mp_buymenu;
    if (_stricmp(key, "mp_reservations") == 0) return s->mp_reservations;
    if (_stricmp(key, "mp_unbuyable") == 0) return s->mp_unbuyable;
    if (_stricmp(key, "mp_unbuildable") == 0) return s->mp_unbuildable;

    if (_stricmp(key, "sv_gm") == 0) key = "sv_gamemode";
    else if (_stricmp(key, "mp_maxclientsip") == 0) key = "sv_maxclientsperip";
    static char num[24];
#define SV_GET_INT(name) if (_stricmp(key, #name) == 0) { snprintf(num, sizeof(num), "%d", (int)s->name); return num; }
    SV_GET_INT(sv_fow) SV_GET_INT(sv_specmode) SV_GET_INT(sv_gamemode) SV_GET_INT(mp_roundtime)
    SV_GET_INT(mp_freezetime) SV_GET_INT(mp_c4timer) SV_GET_INT(mp_respawndelay) SV_GET_INT(mp_infammo)
    SV_GET_INT(mp_flashlight) SV_GET_INT(mp_hovertext) SV_GET_INT(sv_maxplayers) SV_GET_INT(sv_friendlyfire)
    SV_GET_INT(sv_forcelight) SV_GET_INT(sv_daylighttime) SV_GET_INT(mp_hudscale) SV_GET_INT(sv_voicechat)
    SV_GET_INT(sv_offscreendamage) SV_GET_INT(mp_luamap) SV_GET_INT(debuglua)
    SV_GET_INT(mp_teleportreload) SV_GET_INT(mp_dispenser_health) SV_GET_INT(mp_dispenser_money)
    SV_GET_INT(mp_dmspawnmoney) SV_GET_INT(mp_timelimit) SV_GET_INT(mp_randomspawn)
    SV_GET_INT(mp_roundlimit) SV_GET_INT(mp_winlimit) SV_GET_INT(mp_weaponfadeout) SV_GET_INT(mp_dropgrenades)
    SV_GET_INT(mp_grenaderebuy) SV_GET_INT(mp_teamkillpenalty) SV_GET_INT(mp_tkpunish) SV_GET_INT(mp_idlekick)
    SV_GET_INT(mp_idletime) SV_GET_INT(mp_idleaction) SV_GET_INT(mp_pinglimit) SV_GET_INT(mp_floodprot)
    SV_GET_INT(mp_floodprotignoretime) SV_GET_INT(mp_antispeeder) SV_GET_INT(mp_anticlock)
    SV_GET_INT(mp_maxrconfails)
    SV_GET_INT(mp_tempbantime) SV_GET_INT(mp_connectionlimit) SV_GET_INT(mp_vulnerablehostages)
    SV_GET_INT(mp_hostagepenalty) SV_GET_INT(mp_deathdrop) SV_GET_INT(sv_lan) SV_GET_INT(sv_checkusgnlogin)
    SV_GET_INT(mp_turretdamage) SV_GET_INT(mp_zombiekillscore) SV_GET_INT(mp_zombiekillequip)
    SV_GET_INT(mp_zombierecover) SV_GET_INT(mp_killbuildingmoney) SV_GET_INT(mp_killteambuildings)
    SV_GET_INT(mp_killbuildings) SV_GET_INT(mp_mapgoalscore) SV_GET_INT(mp_autogamemode)
#undef SV_GET_INT
#define SV_GET_FLT(name) if (_stricmp(key, #name) == 0) { snprintf(num, sizeof(num), "%g", s->name); return num; }
    SV_GET_FLT(mp_buytime) SV_GET_FLT(mp_damagefactor) SV_GET_FLT(mp_kevlar) SV_GET_FLT(mp_zombiedmg)
    SV_GET_FLT(mp_kickpercent) SV_GET_FLT(mp_mapvoteratio)
#undef SV_GET_FLT
    if (_stricmp(key, "gamemode") == 0) { snprintf(num, sizeof(num), "%d", (int)s->sv_gamemode); return num; }
    if (_stricmp(key, "timelimit") == 0) { snprintf(num, sizeof(num), "%d", (int)s->mp_timelimit); return num; }
    if (_stricmp(key, "randomspawn") == 0) { snprintf(num, sizeof(num), "%d", (int)s->mp_randomspawn); return num; }

    /* Generic cvar fallback */
    for (int i = 0; i < s->cvar_count; i++) {
        if (_stricmp(s->cvars[i].key, key) == 0) {
            return s->cvars[i].val;
        }
    }
    return NULL;
}

static void cfg_set(cs2d_sv_settings_t *s, const char *key, const char *val)
{
    cs2d_sv_settings_set(s, key, val);
}

bool cs2d_sv_settings_load(cs2d_sv_settings_t *s, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (p[0] == '/' && p[1] == '/') continue;
        char key[64];
        int k = 0;
        while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && k < 63) key[k++] = *p++;
        key[k] = '\0';
        if (k == 0) continue;
        while (*p == ' ' || *p == '\t') p++;
        char val[448];
        snprintf(val, sizeof(val), "%.*s", (int)sizeof(val) - 1, p);
        cfg_value(val);
        cfg_set(s, key, val);
    }
    fclose(f);
    return true;
}

/* ------------------------------------------------------------------------------------------------ */
/* Tudp transport                                                                                   */
/* ------------------------------------------------------------------------------------------------ */

uint32_t cs2d_sv_now(void) { return GetTickCount(); }
#define sv_now cs2d_sv_now

static void sv_sendto(cs2d_server_t *sv, cs2d_sv_conn_t *c, const uint8_t *d, int n)
{
    sendto(sv->sock, (const char *)d, n, 0, (struct sockaddr *)&c->addr, sizeof(c->addr));
    c->bytes_out += (uint32_t)n;
    sv->bytes_out_acc += (uint32_t)n;
}

/* The datagram header of an unreliable packet is the last reliable id + 1 (proxy: "35 00" after 0x34) */
static uint16_t sv_unreliable_hdr(const cs2d_sv_conn_t *c) { return (uint16_t)(c->next_seq - 1); }

void sv_flush(cs2d_server_t *sv, cs2d_sv_conn_t *c)
{
    if (!c->out_open || c->out.pos == 0) {
        c->out_open = false;
        return;
    }
    uint8_t d[2048];
    uint16_t hdr;
    if (c->out_reliable) {
        hdr = c->next_seq;
        c->next_seq = (uint16_t)(c->next_seq + 2);
        if (c->next_seq == 0) c->next_seq = 2;
    } else {
        hdr = sv_unreliable_hdr(c);
    }
    memcpy(d, &hdr, 2);
    memcpy(d + 2, c->out.buf, (size_t)c->out.pos);
    int n = c->out.pos + 2;
    sv_sendto(sv, c, d, n);
    if (c->out_reliable && n <= (int)sizeof(c->pending[0].data)) {
        for (int i = 0; i < CS2D_SV_MAX_PENDING; i++) {
            cs2d_sv_pending_t *pd = &c->pending[i];
            if (pd->len == 0) {
                pd->seq = hdr;
                pd->len = (uint16_t)n;
                pd->last_send = sv_now();
                memcpy(pd->data, d, (size_t)n);
                break;
            }
        }
    }
    c->out.pos = 0;
    c->out_open = false;
}

/* Tudp_con.Createpacket(reliable, new) (vtable +0x98): new = 1 always starts a datagram, new = 0 appends
 * to the open one when it has the same reliability and room (the [240] lines of the server info share
 * one datagram, proxy_ph1.log) */
cs2d_pkt_t *cs2d_sv_packet(cs2d_server_t *sv, cs2d_sv_conn_t *c, bool reliable, bool fresh)
{
    if (c->out_open && (fresh || c->out_reliable != reliable || c->out.pos > 1000)) sv_flush(sv, c);
    if (!c->out_open) {
        c->out.pos = 0;
        c->out_reliable = reliable;
        c->out_open = true;
    }
    return &c->out;
}

static void sv_ack(cs2d_server_t *sv, cs2d_sv_conn_t *c, uint16_t seq)
{
    uint8_t d[5];
    uint16_t hdr = sv_unreliable_hdr(c);
    memcpy(d, &hdr, 2);
    d[2] = 0x01;
    memcpy(d + 3, &seq, 2);
    sv_sendto(sv, c, d, 5);
}

static void sv_remove_player(cs2d_server_t *sv, int id, int reason, const char *reason_str);

static void sv_conn_reset(cs2d_sv_conn_t *c)
{
    cs2d_sv_pending_t *keep = c->pending;
    memset(c, 0, sizeof(*c));
    c->pending = keep;
    if (c->pending) memset(c->pending, 0, sizeof(cs2d_sv_pending_t) * CS2D_SV_MAX_PENDING);
    c->next_seq = 2;
}

static cs2d_sv_conn_t *sv_find_conn(cs2d_server_t *sv, const struct sockaddr_in *a, bool hello)
{
    int free_slot = -1;
    for (int i = 0; i < (int)(sizeof(sv->conns) / sizeof(sv->conns[0])); i++) {
        cs2d_sv_conn_t *c = &sv->conns[i];
        if (c->active) {
            if (c->addr.sin_addr.s_addr == a->sin_addr.s_addr && c->addr.sin_port == a->sin_port) {
                if (hello) {
                    if (c->player > 0) sv_remove_player(sv, c->player, 0, "");
                    sv_conn_reset(c);
                    c->active = true;
                    c->addr = *a;
                    c->last_recv = sv_now();
                }
                return c;
            }
        } else if (free_slot < 0) {
            free_slot = i;
        }
    }
    if (!hello || free_slot < 0) return NULL;
    cs2d_sv_conn_t *c = &sv->conns[free_slot];
    if (!c->pending) c->pending = (cs2d_sv_pending_t *)calloc(CS2D_SV_MAX_PENDING, sizeof(cs2d_sv_pending_t));
    sv_conn_reset(c);
    c->active = true;
    c->addr = *a;
    c->last_recv = sv_now();
    return c;
}

static int sv_conn_index(cs2d_server_t *sv, cs2d_sv_conn_t *c) { return (int)(c - sv->conns); }

/* ------------------------------------------------------------------------------------------------ */
/* Packet reader (PTR_DAT_1405f7998)                                                                */
/* ------------------------------------------------------------------------------------------------ */

typedef struct {
    const uint8_t *b;
    int            n, p;
} sv_rd_t;

static int rd_left(const sv_rd_t *r) { return r->n - r->p; }
static uint8_t rd_byte(sv_rd_t *r) { return r->p < r->n ? r->b[r->p++] : 0; }
static uint16_t rd_short(sv_rd_t *r)
{
    uint16_t v = 0;
    if (r->p + 2 <= r->n) { memcpy(&v, r->b + r->p, 2); r->p += 2; } else r->p = r->n;
    return v;
}
static int32_t rd_int(sv_rd_t *r)
{
    int32_t v = 0;
    if (r->p + 4 <= r->n) { memcpy(&v, r->b + r->p, 4); r->p += 4; } else r->p = r->n;
    return v;
}
static int64_t rd_long(sv_rd_t *r)
{
    int64_t v = 0;
    if (r->p + 8 <= r->n) { memcpy(&v, r->b + r->p, 8); r->p += 8; } else r->p = r->n;
    return v;
}
/* rstr: u8 length; rlstr: u16 length. Raw bytes are kept (encrypted fields compare byte for byte) */
static int rd_bytes(sv_rd_t *r, int len, char *out, int max)
{
    int k = 0;
    for (int i = 0; i < len && r->p < r->n; i++) {
        uint8_t ch = r->b[r->p++];
        if (k < max - 1) out[k++] = (char)ch;
    }
    out[k] = '\0';
    return k;
}
static int rd_str(sv_rd_t *r, char *out, int max) { return rd_bytes(r, rd_byte(r), out, max); }
static int rd_lstr(sv_rd_t *r, char *out, int max) { return rd_bytes(r, rd_short(r), out, max); }

/* ------------------------------------------------------------------------------------------------ */
/* Players                                                                                          */
/* ------------------------------------------------------------------------------------------------ */

static int sv_player_count(const cs2d_server_t *sv)
{
    int n = 0;
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) if (sv->players[i].used) n++;
    return n;
}

cs2d_sv_conn_t *cs2d_sv_conn_of_player(cs2d_server_t *sv, int id)
{
    if (id < 1 || id > CS2D_SV_MAX_PLAYERS || !sv->players[id].used || sv->players[id].conn < 0) return NULL;
    cs2d_sv_conn_t *c = &sv->conns[sv->players[id].conn];
    return c->active ? c : NULL;
}

/* CS2D.exe lua_bind.SendBindListToClients (0x14005be90): when addbind/removebind changes the
 * list, every joined player gets a reliable [224, count u16, count * key u16] packet. */
void cs2d_sv_send_lua_binds(cs2d_server_t *sv)
{
    if (!sv || !sv->lua) return;
    const cs2d_sv_lua_t *lua = (const cs2d_sv_lua_t *)sv->lua;
    uint16_t codes[64];
    int n = 0;
    for (int i = 0; i < lua->bind_key_count && n < 64; i++) {
        int code = cs2d_bind_key_code(lua->bind_keys[i]);
        if (code > 0) codes[n++] = (uint16_t)code;
    }
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        cs2d_sv_conn_t *c = cs2d_sv_conn_of_player(sv, i);
        if (!c || !sv->players[i].joined) continue;
        cs2d_pkt_t *p = cs2d_sv_packet(sv, c, true, false);
        cs2d_pkt_w_byte(p, 224);
        cs2d_pkt_w_short(p, (uint16_t)n);
        for (int k = 0; k < n; k++) cs2d_pkt_w_short(p, codes[k]);
    }
}

static void trim(char *s)
{
    size_t n = strlen(s), a = 0;
    while (a < n && (s[a] == ' ' || s[a] == '\t')) a++;
    while (n > a && (s[n - 1] == ' ' || s[n - 1] == '\t')) n--;
    memmove(s, s + a, n - a);
    s[n - a] = '\0';
}

static void replace_all(char *s, size_t cap, const char *from, const char *to)
{
    size_t fl = strlen(from), tl = strlen(to);
    char *p = s;
    while ((p = strstr(p, from)) != NULL) {
        size_t rest = strlen(p + fl);
        if ((size_t)(p - s) + tl + rest + 1 > cap) return;
        memmove(p + tl, p + fl, rest + 1);
        memcpy(p, to, tl);
        p += tl;
    }
}

static bool name_taken(const cs2d_server_t *sv, const char *name)
{
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        if (sv->players[i].used && strcmp(sv->players[i].name, name) == 0) return true;
    }
    return false;
}

void cs2d_sv_sanitize_name(const cs2d_server_t *sv, const char *in, char *out, size_t out_size)
{
    /* FUN_14010d970: trim, cut to 25, "PIayer" -> "Player", remove "*DEAD*", "(Radio)" and the colour
     * mark (U+00A9), then keep only characters >= 0x20 other than ',' ';' 0x7f..0xa0 0xa6 0xae
     * (mask 0xf7ffefff00000000 for 0x20..0x3b) */
    char s[256];
    snprintf(s, sizeof(s), "%s", in ? in : "");
    trim(s);
    if (strlen(s) > 25) s[25] = '\0';
    trim(s);
    replace_all(s, sizeof(s), "PIayer", "Player");
    replace_all(s, sizeof(s), "*DEAD*", "");
    replace_all(s, sizeof(s), "(Radio)", "");
    replace_all(s, sizeof(s), "\xa9", "");
    char f[256];
    int k = 0;
    for (int i = 0; s[i]; i++) {
        uint8_t ch = (uint8_t)s[i];
        bool keep;
        if (ch < 0x3c) keep = ch >= 0x20 && ((0xf7ffefffu >> (ch - 0x20)) & 1u);
        else keep = !((ch & 0xf7) == 0xa6 || (uint8_t)(ch - 0x7f) < 0x22);
        if (keep) f[k++] = (char)ch;
    }
    f[k] = '\0';
    trim(f);
    /* A name that is only a number is not allowed (String(Int(name)) = name), nor an empty one: the
     * default name (PTR_DAT_1405adb10 +0x120, the language's player name, "Player" in English) */
    char num[32];
    snprintf(num, sizeof(num), "%d", atoi(f));
    if (f[0] == '\0' || strcmp(num, f) == 0) snprintf(f, sizeof(f), "Player");
    if (!name_taken(sv, f)) {
        snprintf(out, out_size, "%s", f);
        return;
    }
    /* Taken: "name 2" .. "name 65" */
    for (int n = 2; n < 0x42; n++) {
        char cand[300];
        snprintf(cand, sizeof(cand), "%s %d", f, n);
        if (!name_taken(sv, cand)) {
            snprintf(out, out_size, "%s", cand);
            return;
        }
    }
    out[0] = '\0';
}

void cs2d_sv_sanitize_map_name(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) return;
    dst[0] = '\0';
    if (!src || !src[0]) return;

    /* Skip leading whitespace / quotes */
    while (*src == ' ' || *src == '\t' || *src == '"' || *src == '\'') src++;
    /* Strip directory prefix (e.g. maps/ or maps\) */
    const char *slash1 = strrchr(src, '/');
    const char *slash2 = strrchr(src, '\\');
    const char *slash = slash1 > slash2 ? slash1 : slash2;
    if (slash) src = slash + 1;

    snprintf(dst, dst_size, "%s", src);

    /* Strip trailing quotes or whitespace */
    size_t len = strlen(dst);
    while (len > 0 && (dst[len - 1] == ' ' || dst[len - 1] == '\t' ||
                       dst[len - 1] == '"' || dst[len - 1] == '\'' ||
                       dst[len - 1] == '\r' || dst[len - 1] == '\n')) {
        dst[--len] = '\0';
    }

    /* Strip .map extension if present (case-insensitive) */
    if (len >= 4 && _stricmp(dst + len - 4, ".map") == 0) {
        dst[len - 4] = '\0';
    }
}

/* FUN_140098050(1, -1): the lowest free player id in 1..32 (0x20), i.e. the first id no Tpl in the player list holds;
 * none free returns null (verified in REA: loop iVar10 = 1..0x20 over DAT_1407dfe58) */
static int sv_alloc_player(cs2d_server_t *sv)
{
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        if (!sv->players[i].used) return i;
    }
    return 0;
}

/* ------------------------------------------------------------------------------------------------ */
/* Join messages                                                                                    */
/* ------------------------------------------------------------------------------------------------ */

/* mp_supply_items: the comma separated ids, sent as a byte string (proxy: "51,58,..." -> 33 3a ...) */
static void w_id_list(cs2d_pkt_t *p, const char *list)
{
    uint8_t ids[255];
    int n = 0;
    const char *q = list;
    while (*q && n < 255) {
        while (*q == ',' || *q == ' ') q++;
        if (!*q) break;
        ids[n++] = (uint8_t)atoi(q);
        while (*q && *q != ',') q++;
    }
    cs2d_pkt_w_raw_str(p, ids, n);
}

static void sv_write_join_settings_tail(cs2d_pkt_t *p, const cs2d_sv_settings_t *s, const cs2d_map_t *m,
                                        int32_t seed, const struct cs2d_sv_lua *lua);
void cs2d_sv_build_join_settings(cs2d_pkt_t *p, const cs2d_sv_settings_t *s, const cs2d_map_t *m, int score_t,
                                 int score_ct, int32_t round_ms, int32_t seed, const struct cs2d_sv_lua *lua)
{
    /* FUN_1401dc530, [252,5] accepted (0x1401ded48 writes the closing "ACK") */
    cs2d_pkt_w_byte(p, 252);
    cs2d_pkt_w_byte(p, 6);
    cs2d_pkt_w_byte(p, 0);
    cs2d_pkt_w_str(p, s->sv_map);
    cs2d_pkt_w_str(p, s->sv_name);
    cs2d_pkt_w_short(p, (uint16_t)score_t);    /* DAT_1407dfe98 */
    cs2d_pkt_w_short(p, (uint16_t)score_ct);   /* DAT_1407dfe94 */
    cs2d_pkt_w_byte(p, (uint8_t)s->mp_roundtime);
    cs2d_pkt_w_byte(p, (uint8_t)s->mp_freezetime);
    cs2d_pkt_w_byte(p, (uint8_t)s->mp_c4timer);
    cs2d_pkt_w_int(p, round_ms);               /* DAT_1407e000c - round start */
    cs2d_pkt_w_byte(p, 1);                     /* DAT_1407dfea0 (1 in the capture) */
    cs2d_pkt_w_byte(p, 0);                     /* bomb timer: (now - plant) / 1000, 0 when no bomb */
    cs2d_pkt_w_byte(p, (uint8_t)s->sv_maxplayers);
    cs2d_pkt_w_byte(p, (uint8_t)s->sv_fow);
    cs2d_pkt_w_byte(p, (uint8_t)s->sv_specmode);
    cs2d_pkt_w_byte(p, (uint8_t)s->sv_gamemode);
    cs2d_pkt_w_byte(p, (uint8_t)s->mp_respawndelay);
    cs2d_pkt_w_byte(p, (uint8_t)s->mp_infammo);
    cs2d_pkt_w_byte(p, (uint8_t)s->s_65d0);
    w_id_list(p, s->mp_supply_items);
    cs2d_pkt_w_str(p, s->s_65c0);
    cs2d_pkt_w_byte(p, (uint8_t)s->s_65b8);
    cs2d_pkt_w_byte(p, (uint8_t)s->s_65b0);
    cs2d_pkt_w_byte(p, (uint8_t)s->mp_flashlight);
    cs2d_pkt_w_byte(p, (uint8_t)s->mp_hovertext);
    cs2d_pkt_w_byte(p, (uint8_t)s->sv_friendlyfire);
    sv_write_join_settings_tail(p, s, m, seed, lua);
}

/* The map part of [252,6]: background image, its scroll, the header int, then the rest (FUN_1401dc530) */
static void sv_write_join_settings_tail(cs2d_pkt_t *p, const cs2d_sv_settings_t *s, const cs2d_map_t *m,
                                        int32_t seed, const struct cs2d_sv_lua *lua)
{
    cs2d_pkt_w_str(p, m ? m->bg_image : "");           /* PTR_PTR_1405a1880 */
    cs2d_pkt_w_int(p, m ? m->bg_scroll_x : 0);         /* DAT_1407dfa8c */
    cs2d_pkt_w_int(p, m ? m->bg_scroll_y : 0);         /* _DAT_1407dfa88 */
    cs2d_pkt_w_int(p, m ? m->scroll_map : 0);          /* DAT_1407dfa80: header byte 0 (FUN_140113d50) */
    cs2d_pkt_w_byte(p, (uint8_t)s->sv_forcelight);
    cs2d_pkt_w_short(p, (uint16_t)(s->sv_daylighttime + 1));
    cs2d_pkt_w_byte(p, (uint8_t)s->mp_recoil);
    cs2d_pkt_w_byte(p, (uint8_t)s->mp_hudscale);
    cs2d_pkt_w_byte(p, (uint8_t)s->sv_voicechat);
    cs2d_pkt_w_byte(p, 1);                              /* DAT_1407e0020: the server OS, 1 Windows */
    cs2d_pkt_w_byte(p, (uint8_t)s->lagcompensation);    /* DAT_1405a64cc */
    cs2d_pkt_w_byte(p, lua ? 1 : 0);                    /* 0 < DAT_1407dfcac: Lua scripts loaded */
    cs2d_pkt_w_byte(p, (uint8_t)s->ticks);              /* DAT_1405a64c8 */
    /* Lua bind count (DAT_1407df810) and the Win32 VK codes. The original sends this list in the
     * join snapshot (captured: 7 binds Q/E/leftshift/space/mouse1/mouse2/kp_5 ->
     * 07 51 00 45 00 a0 00 20 00 01 00 02 00 65 00), and the client only reports [227]/[228]
     * for keys it finds here. */
    {
        uint16_t codes[64];
        int n = 0;
        if (lua) {
            for (int i = 0; i < lua->bind_key_count && n < 64; i++) {
                int code = cs2d_bind_key_code(lua->bind_keys[i]);
                if (code > 0) codes[n++] = (uint16_t)code;
            }
        }
        if (n > 255) n = 255;
        cs2d_pkt_w_byte(p, (uint8_t)n);
        for (int i = 0; i < n; i++) cs2d_pkt_w_short(p, codes[i]);
    }
    cs2d_pkt_w_int(p, seed);                            /* DAT_1407df3d4 */
    cs2d_pkt_w_byte(p, (uint8_t)s->sv_offscreendamage);
    cs2d_pkt_w_byte(p, (uint8_t)(s->leaderboard != 0)); /* && DAT_1405a63e8 */
    cs2d_pkt_w_byte(p, (uint8_t)s->physics);
    /* FUN_1401dba60: count byte + entries of PhysicsPlatform / PhysicsObject / PhysicsController (client twin in
     * docs/dump 1402e0000.c); we model no physics objects, so the three counts are 0 */
    cs2d_pkt_w_byte(p, 0);
    cs2d_pkt_w_byte(p, 0);
    cs2d_pkt_w_byte(p, 0);
    cs2d_pkt_w_str(p, "ACK");
}

void cs2d_sv_write_player_record(cs2d_pkt_t *p, int id, const cs2d_sv_player_t *pl)
{
    uint8_t enc[256];
    int n = 0;
    cs2d_pkt_w_byte(p, (uint8_t)id);
    cs2d_net_encrypt(pl->name, "name", enc, &n);       /* FUN_140055400(name, "name") */
    cs2d_pkt_w_raw_str(p, enc, n);
    cs2d_net_encrypt(pl->spray, "spr", enc, &n);
    cs2d_pkt_w_raw_str(p, enc, n);
    cs2d_pkt_w_int(p, pl->usgn);
    cs2d_pkt_w_byte(p, (uint8_t)pl->os);
    cs2d_pkt_w_lstr(p, pl->steam_name);
    cs2d_pkt_w_long(p, (uint64_t)pl->steam_id);
    cs2d_pkt_w_lstr(p, pl->usgn_name);
    cs2d_pkt_w_byte(p, (uint8_t)pl->team);
    cs2d_pkt_w_byte(p, (uint8_t)pl->spec_state);
    cs2d_pkt_w_byte(p, (uint8_t)pl->look);
    cs2d_pkt_w_short(p, (uint16_t)(pl->score + 1000));
    cs2d_pkt_w_short(p, (uint16_t)pl->deaths);         /* +0x298 */
    cs2d_pkt_w_short(p, (uint16_t)pl->assists);        /* +0x28c */
    cs2d_pkt_w_short(p, (uint16_t)pl->mvp);            /* +0x294 */
    cs2d_pkt_w_byte(p, (uint8_t)pl->rank);             /* +0x90 */
    cs2d_pkt_w_short(p, (uint16_t)(int)pl->x);         /* +0x240 */
    cs2d_pkt_w_short(p, (uint16_t)(int)pl->y);         /* +0x244 */
    float r = pl->rot;                                 /* +0x248, wrapped into 0..360 */
    while (r < 0.0f) r += 360.0f;
    while (r > 360.0f) r -= 360.0f;
    cs2d_pkt_w_short(p, (uint16_t)(int)(r * 182.04445f));
    cs2d_pkt_w_byte(p, (uint8_t)pl->health);           /* +0x260 object, vtable +0x90 (0 when null) */
    cs2d_pkt_w_byte(p, (uint8_t)pl->armor);            /* +0x270 object, vtable +0x90 (0 when null) */
    cs2d_pkt_w_byte(p, 0);                              /* vtable +0x200: the special byte */
    cs2d_pkt_w_byte(p, 0);                              /* +0x2e0 weapon object: +0xc type */
    cs2d_pkt_w_byte(p, 0);                              /*                       +0x1c mode */
    cs2d_pkt_w_byte(p, (uint8_t)pl->votekick);          /* +0x3dc votekick */
    cs2d_pkt_w_short(p, (uint16_t)(pl->votemap ? pl->votemap : 0xffff)); /* +0x3e0 votemap: -1 = no vote (proxy) */
}

/* Snapshot section 4 (entity states): [252,7,4,count] then count x {x u16, y u16, state u8}.
 * Only non-zero states are sent; the client clears every entity when the first section arrives
 * (FUN_1402ce7c0 case 4). The server keeps them in map.entity_state, updated by [31] and by the
 * Trigger_Use path, and resets them at round start (knowledge/entities.md). */
int cs2d_sv_write_entity_states(const cs2d_server_t *sv, cs2d_pkt_t *p)
{
    int count = 0;
    uint32_t n = sv->map.entity_count;
    if (n > CS2D_MAX_ENTITIES) n = CS2D_MAX_ENTITIES;
    for (uint32_t i = 0; i < n; i++) {
        if (sv->map.entity_state[i] != 0) count++;
    }
    if (count > 255) count = 255;

    cs2d_pkt_w_byte(p, 252);
    cs2d_pkt_w_byte(p, 7);
    cs2d_pkt_w_byte(p, 4);
    cs2d_pkt_w_byte(p, (uint8_t)count);
    int sent = 0;
    for (uint32_t i = 0; i < n && sent < count; i++) {
        if (sv->map.entity_state[i] == 0) continue;
        cs2d_pkt_w_short(p, sv->map.entities[i].x);
        cs2d_pkt_w_short(p, sv->map.entities[i].y);
        cs2d_pkt_w_byte(p, sv->map.entity_state[i]);
        sent++;
    }
    return sent;
}

/* Snapshot section 6 (ground projectiles): [252,7,6,count] then count x
 * {id u8, type u8, player u8, x u16, y u16} (7 bytes, FUN_1402ce7c0 case 6). Only projectiles that
 * already landed are part of it; the client reconstructs them in state ground. */
int cs2d_sv_write_ground_projectiles(const cs2d_server_t *sv, cs2d_pkt_t *p)
{
    int count = 0;
    for (int i = 0; i < CS2D_SV_MAX_PROJECTILES; i++) {
        if (sv->projectiles[i].active && !sv->projectiles[i].is_flying) count++;
    }
    if (count > 255) count = 255;

    cs2d_pkt_w_byte(p, 252);
    cs2d_pkt_w_byte(p, 7);
    cs2d_pkt_w_byte(p, 6);
    cs2d_pkt_w_byte(p, (uint8_t)count);
    int sent = 0;
    for (int i = 0; i < CS2D_SV_MAX_PROJECTILES && sent < count; i++) {
        const cs2d_sv_projectile_t *pr = &sv->projectiles[i];
        if (!pr->active || pr->is_flying) continue;
        cs2d_pkt_w_byte(p, (uint8_t)pr->id);
        cs2d_pkt_w_byte(p, pr->type);
        cs2d_pkt_w_byte(p, pr->player);
        cs2d_pkt_w_short(p, (uint16_t)(int)pr->x);
        cs2d_pkt_w_short(p, (uint16_t)(int)pr->y);
        sent++;
    }
    return sent;
}

void cs2d_sv_send_join_snapshot(cs2d_server_t *sv, cs2d_sv_conn_t *c)
{
    cs2d_pkt_t *p;

    /* Section 1: players, 8 per message, then an empty one */
    int total = sv_player_count(sv), k = 0;
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        if (!sv->players[i].used) continue;
        if ((k & 7) == 0) {
            p = sv_packet(sv, c, true, true);
            cs2d_pkt_w_byte(p, 252); cs2d_pkt_w_byte(p, 7); cs2d_pkt_w_byte(p, 1);
            cs2d_pkt_w_byte(p, (uint8_t)(total - k > 8 ? 8 : total - k));
        }
        k++;
        cs2d_sv_write_player_record(&c->out, i, &sv->players[i]);
    }
    /* Section 1 terminator */
    p = sv_packet(sv, c, true, true);
    cs2d_pkt_w_byte(p, 252); cs2d_pkt_w_byte(p, 7); cs2d_pkt_w_byte(p, 1); cs2d_pkt_w_byte(p, 0);

    /* Section 2: hostages, 50 per message */
    int living_hostages = 0;
    for (int i = 0; i < sv->hostage_count; i++) {
        if (sv->hostages[i].health > 0) living_hostages++;
    }
    if (living_hostages > 0) {
        int hk = 0;
        for (int i = 0; i < sv->hostage_count; i++) {
            const cs2d_sv_hostage_t *h = &sv->hostages[i];
            if (h->health <= 0) continue;
            if ((hk % 50) == 0) {
                p = sv_packet(sv, c, true, true);
                int rem = living_hostages - hk;
                cs2d_pkt_w_byte(p, 252); cs2d_pkt_w_byte(p, 7); cs2d_pkt_w_byte(p, 2);
                cs2d_pkt_w_byte(p, (uint8_t)(rem > 50 ? 50 : rem));
            }
            hk++;
            cs2d_pkt_w_byte(&c->out, (uint8_t)h->id);
            cs2d_pkt_w_byte(&c->out, (uint8_t)h->health);
            cs2d_pkt_w_byte(&c->out, (uint8_t)h->look);
            cs2d_pkt_w_byte(&c->out, (uint8_t)h->follow);
            cs2d_pkt_w_short(&c->out, (uint16_t)(int)h->x);
            cs2d_pkt_w_short(&c->out, (uint16_t)(int)h->y);
        }
    }
    p = sv_packet(sv, c, true, true);
    cs2d_pkt_w_byte(p, 252); cs2d_pkt_w_byte(p, 7); cs2d_pkt_w_byte(p, 2); cs2d_pkt_w_byte(p, 0);

    /* Section 3: ground items, 60 per message */
    int active_items = 0;
    for (int i = 0; i < CS2D_SV_MAX_GROUND_ITEMS; i++) {
        if (sv->items[i].active) active_items++;
    }
    if (active_items > 0) {
        int ik = 0;
        for (int i = 0; i < CS2D_SV_MAX_GROUND_ITEMS; i++) {
            const cs2d_sv_item_t *it = &sv->items[i];
            if (!it->active) continue;
            if ((ik % 60) == 0) {
                p = sv_packet(sv, c, true, true);
                int rem = active_items - ik;
                cs2d_pkt_w_byte(p, 252); cs2d_pkt_w_byte(p, 7); cs2d_pkt_w_byte(p, 3);
                cs2d_pkt_w_byte(p, (uint8_t)(rem > 60 ? 60 : rem));
            }
            ik++;
            cs2d_pkt_w_short(&c->out, (uint16_t)it->id);
            cs2d_pkt_w_byte(&c->out, (uint8_t)it->type);
            cs2d_pkt_w_short(&c->out, (uint16_t)it->tile_x);
            cs2d_pkt_w_short(&c->out, (uint16_t)it->tile_y);
            cs2d_pkt_w_short(&c->out, (uint16_t)it->clip);
            cs2d_pkt_w_short(&c->out, (uint16_t)it->reserve);
        }
    }
    p = sv_packet(sv, c, true, true);
    cs2d_pkt_w_byte(p, 252); cs2d_pkt_w_byte(p, 7); cs2d_pkt_w_byte(p, 3); cs2d_pkt_w_byte(p, 0);

    /* Section 4: entity states (doors opened, breakables broken, flags taken, ...). Like sections
     * 1/2/3/5 this is a chunked list closed by a count-0 packet. The client stays on "joindata 4"
     * until that terminator arrives; without it every later section is "unexpected joindata". */
    {
        int total_states = 0;
        uint32_t n = sv->map.entity_count;
        if (n > CS2D_MAX_ENTITIES) n = CS2D_MAX_ENTITIES;
        for (uint32_t i = 0; i < n; i++) {
            if (sv->map.entity_state[i] != 0) total_states++;
        }
        uint32_t cursor = 0;
        while (total_states > 0) {
            int chunk = total_states > 180 ? 180 : total_states;
            p = sv_packet(sv, c, true, true);
            cs2d_pkt_w_byte(p, 252);
            cs2d_pkt_w_byte(p, 7);
            cs2d_pkt_w_byte(p, 4);
            cs2d_pkt_w_byte(p, (uint8_t)chunk);
            int written = 0;
            for (; cursor < n && written < chunk; cursor++) {
                if (sv->map.entity_state[cursor] == 0) continue;
                cs2d_pkt_w_short(p, sv->map.entities[cursor].x);
                cs2d_pkt_w_short(p, sv->map.entities[cursor].y);
                cs2d_pkt_w_byte(p, sv->map.entity_state[cursor]);
                written++;
            }
            total_states -= written;
        }
        p = sv_packet(sv, c, true, true);
        cs2d_pkt_w_byte(p, 252); cs2d_pkt_w_byte(p, 7); cs2d_pkt_w_byte(p, 4); cs2d_pkt_w_byte(p, 0);
    }

    /* Section 5: dynamic objects (Tdo), 60 per message */
    int active_objs = 0;
    for (int i = 0; i < CS2D_DYNOBJ_MAX; i++) {
        const cs2d_dynobj_t *o = &sv->dynobj.objs[i];
        if (o->active) {
            if (o->typ == CS2D_DO_IMAGE && o->mode == 2 && o->player > 0 && o->player != c->player) continue;
            active_objs++;
        }
    }
    if (active_objs > 0) {
        int ok = 0;
        for (int i = 0; i < CS2D_DYNOBJ_MAX; i++) {
            const cs2d_dynobj_t *o = &sv->dynobj.objs[i];
            if (!o->active) continue;
            if (o->typ == CS2D_DO_IMAGE && o->mode == 2 && o->player > 0 && o->player != c->player) continue;
            if ((ok % 60) == 0) {
                p = sv_packet(sv, c, true, true);
                int rem = active_objs - ok;
                cs2d_pkt_w_byte(p, 252); cs2d_pkt_w_byte(p, 7); cs2d_pkt_w_byte(p, 5);
                cs2d_pkt_w_byte(p, (uint8_t)(rem > 60 ? 60 : rem));
            }
            ok++;
            cs2d_pkt_w_short(&c->out, (uint16_t)o->id);
            cs2d_pkt_w_byte(&c->out, (uint8_t)o->typ);
            cs2d_pkt_w_byte(&c->out, (uint8_t)cs2d_dynobj_health_percent(o));
            cs2d_pkt_w_byte(&c->out, (uint8_t)o->mode);
            cs2d_pkt_w_short(&c->out, (uint16_t)(int)(o->x + 0x8000));
            cs2d_pkt_w_short(&c->out, (uint16_t)(int)(o->y + 0x8000));
            cs2d_pkt_w_byte(&c->out, (uint8_t)o->player);
            cs2d_pkt_w_byte(&c->out, (uint8_t)o->team);
            float rot = o->rot;
            uint8_t rot_bytes[4];
            memcpy(rot_bytes, &rot, 4);
            for (int b = 0; b < 4; b++) cs2d_pkt_w_byte(&c->out, rot_bytes[b]);
        }
    }
    p = sv_packet(sv, c, true, true);
    cs2d_pkt_w_byte(p, 252); cs2d_pkt_w_byte(p, 7); cs2d_pkt_w_byte(p, 5); cs2d_pkt_w_byte(p, 0);

    /* Section 6: ground projectiles (grenades that already landed) */
    p = sv_packet(sv, c, true, true);
    cs2d_sv_write_ground_projectiles(sv, p);

    /* Section 10: resource paths (DAT_1407df6f0), 15 per message (lVar38 * -0x11111111 < 0x11111112 is
     * "index % 15 == 0") */
    {
        const char *paths[256];
        int np = 0;
        for (uint32_t e = 0; e < sv->map.entity_count && np < 256; e++) {
            const cs2d_entity_t *en = &sv->map.entities[e];
            /* Assumption: the table holds the image of every Env_Sprite / Env_Image in entity order, first
             * occurrence only (matches the 21 paths of de_dust2 in proxy_ph1.log) */
            if (en->type != CS2D_ENT_ENV_SPRITE && en->type != CS2D_ENT_ENV_IMAGE) continue;
            const char *img = en->str_settings[0];
            if (!img[0]) continue;
            bool dup = false;
            for (int j = 0; j < np; j++) if (strcmp(paths[j], img) == 0) { dup = true; break; }
            if (!dup) paths[np++] = img;
        }
        for (int i = 0; i < np; i++) {
            if (i % 15 == 0) {
                p = sv_packet(sv, c, true, true);
                cs2d_pkt_w_byte(p, 252); cs2d_pkt_w_byte(p, 7); cs2d_pkt_w_byte(p, 10);
                cs2d_pkt_w_byte(p, (uint8_t)(np - i > 15 ? 15 : np - i));
            }
            cs2d_pkt_w_str(&c->out, paths[i]);
        }
        p = sv_packet(sv, c, true, true);
        cs2d_pkt_w_byte(p, 252); cs2d_pkt_w_byte(p, 7); cs2d_pkt_w_byte(p, 10); cs2d_pkt_w_byte(p, 0);
    }

    /* Section 7: image data (FUN_1402ce7c0 case 4), 20 per message */
    int active_imgs = 0;
    for (int i = 0; i < CS2D_DYNOBJ_MAX; i++) {
        const cs2d_dynobj_t *o = &sv->dynobj.objs[i];
        if (o->active && o->typ == CS2D_DO_IMAGE) {
            if (o->mode == 2 && o->player > 0 && o->player != c->player) continue;
            active_imgs++;
        }
    }
    if (active_imgs > 0) {
        int ok = 0;
        for (int i = 0; i < CS2D_DYNOBJ_MAX; i++) {
            const cs2d_dynobj_t *o = &sv->dynobj.objs[i];
            if (!o->active || o->typ != CS2D_DO_IMAGE) continue;
            if (o->mode == 2 && o->player > 0 && o->player != c->player) continue;
            if ((ok % 20) == 0) {
                p = sv_packet(sv, c, true, true);
                int rem = active_imgs - ok;
                cs2d_pkt_w_byte(p, 252); cs2d_pkt_w_byte(p, 7); cs2d_pkt_w_byte(p, 7);
                cs2d_pkt_w_byte(p, (uint8_t)(rem > 20 ? 20 : rem));
            }
            ok++;
            cs2d_pkt_w_short(&c->out, (uint16_t)o->id);
            cs2d_pkt_w_str(&c->out, o->path);
            cs2d_pkt_w_byte(&c->out, o->color[0]);
            cs2d_pkt_w_byte(&c->out, o->color[1]);
            cs2d_pkt_w_byte(&c->out, o->color[2]);
            float a = o->alpha;
            float sx = o->scalex;
            float sy = o->scaley;
            uint8_t ab[4], sxb[4], syb[4];
            memcpy(ab, &a, 4);
            memcpy(sxb, &sx, 4);
            memcpy(syb, &sy, 4);
            for (int b = 0; b < 4; b++) cs2d_pkt_w_byte(&c->out, ab[b]);
            for (int b = 0; b < 4; b++) cs2d_pkt_w_byte(&c->out, sxb[b]);
            for (int b = 0; b < 4; b++) cs2d_pkt_w_byte(&c->out, syb[b]);
            cs2d_pkt_w_byte(&c->out, (uint8_t)o->blend);
            cs2d_pkt_w_short(&c->out, (uint16_t)o->frame);
            cs2d_pkt_w_byte(&c->out, 0); /* parent typ = 0 */
            cs2d_pkt_w_byte(&c->out, (uint8_t)o->shadow);
            if (o->hit_mode != 0) {
                cs2d_pkt_w_byte(&c->out, 1);
                cs2d_pkt_w_byte(&c->out, (uint8_t)o->hit_mode);
                cs2d_pkt_w_short(&c->out, (uint16_t)(o->hit_xo + 0x8000));
                cs2d_pkt_w_short(&c->out, (uint16_t)(o->hit_yo + 0x8000));
                cs2d_pkt_w_short(&c->out, (uint16_t)o->hit_w);
                cs2d_pkt_w_short(&c->out, (uint16_t)o->hit_h);
            } else {
                cs2d_pkt_w_byte(&c->out, 0);
            }
        }
    }
    p = sv_packet(sv, c, true, true);
    cs2d_pkt_w_byte(p, 252); cs2d_pkt_w_byte(p, 7); cs2d_pkt_w_byte(p, 7); cs2d_pkt_w_byte(p, 0);

    /* Section 8: tweens (empty until needed) */
    p = sv_packet(sv, c, true, true);
    cs2d_pkt_w_byte(p, 252); cs2d_pkt_w_byte(p, 7); cs2d_pkt_w_byte(p, 8); cs2d_pkt_w_byte(p, 0);

    /* Section 9: custom tiles (Tcustomtile, FUN_1401e1130) */
    if (sv->map.custom_tile_count == 0) {
        p = sv_packet(sv, c, true, true);
        cs2d_pkt_w_byte(p, 252); cs2d_pkt_w_byte(p, 7); cs2d_pkt_w_byte(p, 9); cs2d_pkt_w_byte(p, 0);
    } else {
        uint32_t sent = 0;
        while (sent < sv->map.custom_tile_count) {
            uint8_t chunk = (uint8_t)(sv->map.custom_tile_count - sent > 70 ? 70 : sv->map.custom_tile_count - sent);
            p = sv_packet(sv, c, true, true);
            cs2d_pkt_w_byte(p, 252); cs2d_pkt_w_byte(p, 7); cs2d_pkt_w_byte(p, 9); cs2d_pkt_w_byte(p, chunk);
            for (uint8_t j = 0; j < chunk; j++) {
                const cs2d_customtile_t *ct = &sv->map.custom_tiles[sent + j];
                size_t idx = (size_t)ct->y * sv->map.width + ct->x;
                uint8_t tile = sv->map.tiles[idx];
                uint8_t mod = sv->map.modifiers ? sv->map.modifiers[idx] : 0;
                const uint8_t *d = sv->map.tile_data ? &sv->map.tile_data[idx * 4] : NULL;
                uint8_t r = d ? d[0] : 255;
                uint8_t g = d ? d[1] : 255;
                uint8_t b = d ? d[2] : 255;
                uint8_t overlay = (d && (mod & 0xc0) != 0x40) ? d[3] : 0;
                uint8_t blend = (d && (mod & 0xc0) == 0x40) ? d[3] : 0;
                cs2d_pkt_w_short(p, ct->x);
                cs2d_pkt_w_short(p, ct->y);
                cs2d_pkt_w_byte(p, tile);
                cs2d_pkt_w_byte(p, (uint8_t)(mod & 3));
                cs2d_pkt_w_byte(p, r);
                cs2d_pkt_w_byte(p, g);
                cs2d_pkt_w_byte(p, b);
                cs2d_pkt_w_byte(p, overlay);
                cs2d_pkt_w_byte(p, blend);
            }
            sent += chunk;
        }
        /* Second [252,7,9] with count 0 to close (FUN_1401e1555) */
        p = sv_packet(sv, c, true, true);
        cs2d_pkt_w_byte(p, 252); cs2d_pkt_w_byte(p, 7); cs2d_pkt_w_byte(p, 9); cs2d_pkt_w_byte(p, 0);
    }

    /* Section 11: weapon settings, wbank of PTR_DAT_14059d3a8 = [u16 size][u32 raw size][zlib]. The
     * raw table is built by cs2d_weapons_wbank_raw (FUN_140112bc0) */
    {
        uint8_t raw[256];
        size_t raw_n = cs2d_weapons_wbank_raw(sv->set.mp_recoil, raw, sizeof(raw));
        uint8_t z[512];
        uLongf zn = sizeof(z);
        compress2(z, &zn, raw, (uLong)raw_n, Z_DEFAULT_COMPRESSION);
        p = sv_packet(sv, c, true, true);
        cs2d_pkt_w_byte(p, 252); cs2d_pkt_w_byte(p, 7); cs2d_pkt_w_byte(p, 11);
        cs2d_pkt_w_short(p, (uint16_t)(zn + 4));
        cs2d_pkt_w_int(p, (int32_t)raw_n);
        for (uLongf i = 0; i < zn; i++) cs2d_pkt_w_byte(p, z[i]);
    }

    /* Section 12: the map list (PTR_DAT_1405a1538): 0xff + u16 count, then 10 maps per message */
    p = sv_packet(sv, c, true, true);
    cs2d_pkt_w_byte(p, 252); cs2d_pkt_w_byte(p, 7); cs2d_pkt_w_byte(p, 12); cs2d_pkt_w_byte(p, 0xff);
    cs2d_pkt_w_short(p, (uint16_t)sv->map_count);
    for (int i = 0; i < sv->map_count; i++) {
        if (i % 10 == 0) {
            p = sv_packet(sv, c, true, true);
            cs2d_pkt_w_byte(p, 252); cs2d_pkt_w_byte(p, 7); cs2d_pkt_w_byte(p, 12);
            cs2d_pkt_w_byte(p, (uint8_t)(sv->map_count - i > 10 ? 10 : sv->map_count - i));
        }
        cs2d_pkt_w_short(&c->out, (uint16_t)i);
        cs2d_pkt_w_str(&c->out, sv->maps[i]);
    }
    p = sv_packet(sv, c, true, true);
    cs2d_pkt_w_byte(p, 252); cs2d_pkt_w_byte(p, 7); cs2d_pkt_w_byte(p, 12); cs2d_pkt_w_byte(p, 0);

    /* Section 13: the mod list (FUN_140107b70, "" without mods), u16 string */
    p = sv_packet(sv, c, true, true);
    cs2d_pkt_w_byte(p, 252); cs2d_pkt_w_byte(p, 7); cs2d_pkt_w_byte(p, 13);
    cs2d_pkt_w_lstr(p, "");

    /* Section 200: done */
    p = sv_packet(sv, c, true, true);
    cs2d_pkt_w_byte(p, 252); cs2d_pkt_w_byte(p, 7); cs2d_pkt_w_byte(p, 200);
    cs2d_pkt_w_str(p, "ACK");

    /* sv_spraytransfer: [28,0,1] appended, then FUN_14002e9e0 per other connected player (their sprays) */
    if (sv->set.sv_spraytransfer == 1) {
        p = sv_packet(sv, c, true, false);
        cs2d_pkt_w_byte(p, 28); cs2d_pkt_w_byte(p, 0); cs2d_pkt_w_byte(p, 1);
        for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
            if (i == c->player || !sv->players[i].joined || sv->players[i].spray_logo_len <= 0) continue;
            p = sv_packet(sv, c, true, false);
            cs2d_pkt_w_byte(p, 28);
            cs2d_pkt_w_byte(p, (uint8_t)i);
            cs2d_pkt_w_byte(p, 2);
            cs2d_pkt_w_short(p, (uint16_t)sv->players[i].spray_logo_len);
            for (int b = 0; b < sv->players[i].spray_logo_len; b++) {
                cs2d_pkt_w_byte(p, sv->players[i].spray_logo[b]);
            }
        }
    }

    /* Server info: [240,0,50,"1"], one [240,0,51,line] per line of sys/serverinfo.txt, [240,0,50,"2"].
     * FUN_1400ce360 writes a u16 count + UTF-16LE; a line of 256+ characters is cut */
    if (sv->info_count > 0) {
        p = sv_packet(sv, c, true, true);
        cs2d_pkt_w_byte(p, 240); cs2d_pkt_w_byte(p, 0); cs2d_pkt_w_byte(p, 50);
        cs2d_pkt_w_short(p, 1); cs2d_pkt_w_short(p, '1');
        for (int i = 0; i < sv->info_count; i++) {
            /* sys/serverinfo.txt is UTF-8; the wire field is UTF-16LE. */
            uint16_t units[256];
            int len = cs2d_net_utf8_to_utf16(sv->info_lines[i], units, 255);
            p = sv_packet(sv, c, true, c->out.pos >= 1000 - len * 2);
            cs2d_pkt_w_byte(p, 240); cs2d_pkt_w_byte(p, 0); cs2d_pkt_w_byte(p, 51);
            cs2d_pkt_w_short(p, (uint16_t)len);
            for (int j = 0; j < len; j++) cs2d_pkt_w_short(p, units[j]);
        }
        p = sv_packet(sv, c, true, true);
        cs2d_pkt_w_byte(p, 240); cs2d_pkt_w_byte(p, 0); cs2d_pkt_w_byte(p, 50);
        cs2d_pkt_w_short(p, 1); cs2d_pkt_w_short(p, '2');
    }
    sv_flush(sv, c);
}

/* ------------------------------------------------------------------------------------------------ */
/* recv 252                                                                                         */
/* ------------------------------------------------------------------------------------------------ */

/* ------------------------------------------------------------------------------------------------
 * P1: per-IP flood guard (FUN_1400d4c90) and RCON failure cache (mp_maxrconfails)
 * ---------------------------------------------------------------------------------------------- */

static cs2d_sv_iplimit_t *sv_iplimit_get(cs2d_server_t *sv, const char *ip)
{
    if (!sv || !ip || !ip[0]) return NULL;
    for (int i = 0; i < sv->iplimit_count; i++) {
        if (_stricmp(sv->iplimits[i].ip, ip) == 0) return &sv->iplimits[i];
    }
    if (sv->iplimit_count >= CS2D_SV_MAX_BANS) return NULL;
    cs2d_sv_iplimit_t *e = &sv->iplimits[sv->iplimit_count++];
    memset(e, 0, sizeof(*e));
    snprintf(e->ip, sizeof(e->ip), "%s", ip);
    return e;
}

/* True while the IP is in the flood ignore window (mp_floodprotignoretime seconds). */
static bool sv_ip_ignored(cs2d_server_t *sv, const char *ip)
{
    if (!sv || !ip || !ip[0] || sv->set.mp_floodprot == 0) return false;
    for (int i = 0; i < sv->iplimit_count; i++) {
        cs2d_sv_iplimit_t *e = &sv->iplimits[i];
        if (_stricmp(e->ip, ip) != 0 || e->ignore_until == 0) continue;
        if ((int32_t)(sv_now() - e->ignore_until) >= 0) {
            e->ignore_until = 0;
            return false;
        }
        return true;
    }
    return false;
}

/* FUN_1400d4c90(ip, 0xfc, max, window_ms): max messages per window; on excess the IP is ignored
 * for mp_floodprotignoretime seconds (help.php: default 600, max 86400). */
static bool sv_flood_allow(cs2d_server_t *sv, const char *ip, int max, uint32_t window_ms)
{
    if (!sv || !ip || sv->set.mp_floodprot == 0 || max <= 0) return true;
    uint32_t now = sv_now();
    cs2d_sv_iplimit_t *e = sv_iplimit_get(sv, ip);
    if (!e) return true;
    if (e->window_ms == 0 || now - e->window_ms >= window_ms) {
        e->window_ms = now;
        e->count = 0;
    }
    if (++e->count > max) {
        int secs = sv->set.mp_floodprotignoretime;
        if (secs <= 0) secs = 600;
        if (secs > 86400) secs = 86400;
        if (e->ignore_until == 0) {
            cs2d_server_print("FLOOD PROTECTION: ignoring %s for %d seconds", ip, secs);
        }
        e->ignore_until = now + (uint32_t)secs * 1000u;
        e->count = 0;
        e->window_ms = now;
        return false;
    }
    return true;
}

static bool sv_rcon_allow(cs2d_server_t *sv, const char *ip)
{
    if (!sv || !ip || sv->set.mp_maxrconfails <= 0) return true;
    for (int i = 0; i < sv->iplimit_count; i++) {
        cs2d_sv_iplimit_t *e = &sv->iplimits[i];
        if (_stricmp(e->ip, ip) == 0) return e->rcon_fails < sv->set.mp_maxrconfails;
    }
    return true;
}

static void sv_rcon_fail(cs2d_server_t *sv, const char *ip)
{
    if (!sv || !ip || sv->set.mp_maxrconfails <= 0) return;
    cs2d_sv_iplimit_t *e = sv_iplimit_get(sv, ip);
    if (!e) return;
    if (e->rcon_fails < 1000000) e->rcon_fails++;
    if (e->rcon_fails == sv->set.mp_maxrconfails) {
        cs2d_server_print("RCON: too many failed attempts from %s - ignoring it", ip);
    }
}

/* Comma/space separated id list (sv_rconusers, mp_reservations, mp_supply_items style). */
static bool sv_usgn_listed(const char *list, int32_t usgn)
{
    if (!list || !list[0] || usgn <= 0) return false;
    const char *p = list;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ',' || *p == '|') p++;
        if (!*p) break;
        int v = atoi(p);
        if (v == (int)usgn) return true;
        while (*p && *p != ',' && *p != '|' && *p != ' ' && *p != '\t') p++;
    }
    return false;
}

static int sv_count_ip(const cs2d_server_t *sv, const char *ip)
{
    int n = 0;
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        if (sv->players[i].used && _stricmp(sv->players[i].ip, ip) == 0) n++;
    }
    return n;
}

static int sv_count_active_conns(const cs2d_server_t *sv)
{
    int n = 0;
    for (size_t i = 0; i < sizeof(sv->conns) / sizeof(sv->conns[0]); i++) {
        if (sv->conns[i].active) n++;
    }
    return n;
}

static void sv_join_fail_log(cs2d_sv_conn_t *c, const char *name, const char *why)
{
    cs2d_server_print("JOIN FAILURE: %s (%s:%d) %s", name, inet_ntoa(c->addr.sin_addr), ntohs(c->addr.sin_port), why);
}

static void sv_recv_join1(cs2d_server_t *sv, cs2d_sv_conn_t *c, sv_rd_t *r)
{
    char name[256], pw[256], ver[256], usgn_name[256], steam_name[256], spray[256], secret[256], scramble[256], cid[256];
    rd_str(r, name, sizeof(name));
    rd_str(r, pw, sizeof(pw));
    rd_str(r, ver, sizeof(ver));
    int ver_short = rd_short(r);
    int32_t usgn = rd_int(r);
    rd_lstr(r, usgn_name, sizeof(usgn_name));
    int os = rd_byte(r);
    rd_lstr(r, steam_name, sizeof(steam_name));
    int64_t steam_id = rd_long(r);
    rd_str(r, spray, sizeof(spray));
    rd_str(r, secret, sizeof(secret));
    rd_str(r, scramble, sizeof(scramble));
    rd_str(r, cid, sizeof(cid));
    /* map transfer mode, screen size, language and 3 flags follow (unused before the Lua hooks) */

    cs2d_pkt_t *p = sv_packet(sv, c, true, false);
    cs2d_pkt_w_byte(p, 252);
    cs2d_pkt_w_byte(p, 2);

    /*
     * connect_attempt runs before every sanity check: the dedicated parses the whole [252,1],
     * calls the per-IP flood guard FUN_1400d4c90(ip, 0xfc, 200, 60000) (0x1401dcd12) and then
     * the whole hook list of the +0xc8 slot (0x1401dcd41 -> 0x1401de995). The hook therefore
     * sees the unverified name / USGN / Steam fields, exactly as help.php warns. A non-empty
     * returned string aborts the join: the server logs
     *   JOIN FAILURE: <name> (<ip>:<port>) can't join (Lua connect_attempt returned: <value>)
     * and answers [252, 2, 4, <value>] (0x1401dcdd8, 0x1401dce77, 0x1401dcec7): status 4 is the
     * ban status and the string is the ban reason the client displays.
     */
    char ip_str[32];
    snprintf(ip_str, sizeof(ip_str), "%s", inet_ntoa(c->addr.sin_addr));
    int port = ntohs(c->addr.sin_port);
    char reason[192];
    if (cs2d_sv_lua_hook_connect_attempt(sv, name, ip_str, port, (int)usgn, usgn_name, steam_id, steam_name,
                                         reason, sizeof(reason)) == 1) {
        char why[256];
        snprintf(why, sizeof(why), "can't join (Lua connect_attempt returned: %s)", reason);
        sv_join_fail_log(c, name, why);
        cs2d_pkt_w_byte(p, 4);
        cs2d_pkt_w_lstr(p, reason);
        sv_flush(sv, c);
        return;
    }

    int status = 0;
    int len = (int)strlen(name);

    char want_secret[128];
    cs2d_net_secret(sv->secret, want_secret);  /* FUN_1401be680(PTR_PTR_1405a6800) */
    uint8_t enc[256];
    int en = 0;

    if (len < 1 || len > 25) {
        sv_join_fail_log(c, name, "can't join (illegal name length)");
        status = 22;
    } else if (strcmp(want_secret, secret) != 0) {
        sv_join_fail_log(c, name, "can't join (precon. auth failed)");
        status = 22;
    } else if (scramble[0] != '\0') {
        /* FUN_1401be810 builds a string but returns the empty one: the field must be empty */
        sv_join_fail_log(c, name, "can't join (auth failed)");
        status = 22;
    } else if ((uint8_t)cid[0] != 0xfa) {
        sv_join_fail_log(c, name, "can't join (auth failed [v])");
        status = 22;
    } else {
        cs2d_net_encrypt(sv->set.sv_password, "j01nk3y", enc, &en);
        bool pw_ok = sv->set.sv_password[0] == '\0' ||
                     ((int)strlen(pw) == en && memcmp(pw, enc, (size_t)en) == 0);
        cs2d_net_encrypt("1.0.1.8", "funkymonky", enc, &en);
        bool ver_ok = (int)strlen(ver) == en && memcmp(ver, enc, (size_t)en) == 0 && ver_short == 0x3fa;
        if (!pw_ok) {
            if (pw[0]) sv_join_fail_log(c, name, "can't join (wrong password)");
            status = 1;
        } else if (sv->set.mp_connectionlimit > 0 &&
                   sv_count_active_conns(sv) >= sv->set.mp_connectionlimit &&
                   c->player == 0) {
            /* help.php: mp_connectionlimit caps concurrent UDP connections */
            sv_join_fail_log(c, name, "can't join (connection limit reached)");
            status = 3;
        } else if (sv_player_count(sv) >= sv->set.sv_maxplayers &&
                   !sv_usgn_listed(sv->set.mp_reservations, usgn)) {
            sv_join_fail_log(c, name, "can't join (server is full)");
            status = 3;
        } else if (sv_player_count(sv) >= sv->set.sv_maxplayers) {
            /* mp_reservations: a reserved U.S.G.N. user joins a full server; kick a non-reserved player */
            int kick_id = 0;
            for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
                if (!sv->players[i].used) continue;
                if (sv_usgn_listed(sv->set.mp_reservations, sv->players[i].usgn)) continue;
                if (sv->players[i].bot) { kick_id = i; break; }
                kick_id = i;
            }
            if (kick_id > 0) {
                cs2d_server_print("Reserved slot: kicking %s for %s", sv->players[kick_id].name, name);
                cs2d_sv_kick(sv, kick_id, "Reserved slot");
            } else {
                sv_join_fail_log(c, name, "can't join (server is full)");
                status = 3;
            }
        } else if (sv->set.sv_maxclientsperip > 0 &&
                   sv_count_ip(sv, ip_str) >= sv->set.sv_maxclientsperip) {
            /* DAT_1405a6514 clients per IP -> status 23 */
            sv_join_fail_log(c, name, "can't join (too many clients with this ip)");
            status = 23;
        } else if (!ver_ok) {
            sv_join_fail_log(c, name, "can't join (wrong game version)");
            status = 5;
        } else if (c->player != 0) {
            sv_join_fail_log(c, name, "can't join (already connected)");
            status = 9;
        } else if (usgn == 0 && sv->set.sv_usgnonly == 1) {
            sv_join_fail_log(c, name, "can't join (no registered U.S.G.N. user)");
            status = 2;
        } else {
            char ban_reason[64] = "";
            if (cs2d_sv_is_banned(sv, ip_str, usgn, steam_id, ban_reason, sizeof(ban_reason))) {
                sv_join_fail_log(c, name, "can't join (banned)");
                status = 4;
                cs2d_pkt_w_byte(&c->out, 4);
                cs2d_pkt_w_lstr(&c->out, ban_reason);
                sv_flush(sv, c);
                return;
            }
        }
    }
    if (status != 0) {
        cs2d_pkt_w_byte(&c->out, (uint8_t)status);
        sv_flush(sv, c);
        return;
    }

    int id = sv_alloc_player(sv);
    if (id == 0) {
        cs2d_pkt_w_byte(&c->out, 3);
        sv_flush(sv, c);
        return;
    }
    cs2d_sv_player_t *pl = &sv->players[id];
    memset(pl, 0, sizeof(*pl));
    cs2d_sv_sanitize_name(sv, name, pl->name, sizeof(pl->name));
    pl->used = true;
    pl->conn = sv_conn_index(sv, c);
    pl->usgn = usgn;
    pl->os = os;
    snprintf(pl->usgn_name, sizeof(pl->usgn_name), "%.63s", usgn_name);
    snprintf(pl->steam_name, sizeof(pl->steam_name), "%.63s", steam_name);
    pl->steam_id = steam_id;
    /* The [252,1] spray file name is not Tpl +0x3c8: that field is still empty in the join snapshot
     * (proxy_ph1.log, "00" after the name) */
    (void)spray;
    snprintf(pl->ip, sizeof(pl->ip), "%s", inet_ntoa(c->addr.sin_addr));
    pl->port = ntohs(c->addr.sin_port);
    pl->look = 0;
    pl->maxhealth = 100;
    pl->screen_w = 850;
    pl->screen_h = 480;
    /* Idle timer (mp_idlekick / mp_idletime): last_move_ms is a GetTickCount() stamp, so a fresh
     * instance left at 0 looked idle since Windows boot and sv_second kicked it on the next 1 s tick,
     * i.e. right after the join (before the player ever picks a team). The count starts at the join. */
    pl->last_move_ms = cs2d_sv_now();
    c->player = id;
    c->join_step = 2;

    /* help.php: connect_initplayer runs once the player instance exists (the hooks may already query
     * player(id, ...)); it cannot cancel the join. The reply tells the client its id afterwards. */
    cs2d_sv_lua_hook_connect_initplayer(sv, id);

    cs2d_pkt_w_byte(&c->out, 0);
    cs2d_pkt_w_byte(&c->out, (uint8_t)id);
    cs2d_pkt_w_str(&c->out, sv->set.sv_map);   /* PTR_DAT_1405a6660 +8 */
    cs2d_pkt_w_str(&c->out, sv->secret);
    sv_flush(sv, c);

    /* FUN_1401bb970 */
    char ids[128];
    if (usgn != 0) snprintf(ids, sizeof(ids), "U.S.G.N. ID #%d", usgn);
    else snprintf(ids, sizeof(ids), "no U.S.G.N. ID");
    cs2d_server_print("%s is using IP %s:%d, %s and %s", pl->name, pl->ip, pl->port, ids,
                      steam_id != 0 ? "Steam" : "no Steam");

    /* FUN_1401184f0: [248, id, name, spray, usgn i32, usgn name (u16 str), steam id i64, steam name (u16 str),
     * os] to every joined player but the new one */
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        cs2d_sv_conn_t *oc = conn_of_player(sv, i);
        if (!oc || i == id || !sv->players[i].joined) continue;
        cs2d_pkt_t *p248 = sv_packet(sv, oc, true, false);
        cs2d_pkt_w_byte(p248, 248);
        cs2d_pkt_w_byte(p248, (uint8_t)id);
        cs2d_pkt_w_str(p248, pl->name);
        cs2d_pkt_w_str(p248, pl->spray);
        cs2d_pkt_w_int(p248, pl->usgn);
        cs2d_pkt_w_lstr(p248, pl->usgn_name);
        cs2d_pkt_w_long(p248, (uint64_t)pl->steam_id);
        cs2d_pkt_w_lstr(p248, pl->steam_name);
        cs2d_pkt_w_byte(p248, (uint8_t)pl->os);
    }
}

static void sv_recv_join3(cs2d_server_t *sv, cs2d_sv_conn_t *c, sv_rd_t *r)
{
    char sha[128], enc_sec[256];
    rd_str(r, sha, sizeof(sha));
    rd_str(r, enc_sec, sizeof(enc_sec));
    int download = rd_byte(r);
    (void)download;
    cs2d_sv_player_t *pl = (c->player > 0) ? &sv->players[c->player] : NULL;

    cs2d_pkt_t *p = sv_packet(sv, c, true, false);
    cs2d_pkt_w_byte(p, 252);
    cs2d_pkt_w_byte(p, 4);

    /* The secret reversed and shifted by the key table DAT_1405f02d0 ("!aCz0jK5f\xfc", cs2d_net_build_join_stage2) */
    int n = (int)strlen(sv->secret);
    char rev[32];
    for (int i = 0; i < n; i++) rev[i] = sv->secret[n - 1 - i];
    rev[n] = '\0';
    uint8_t want[64];
    int wn = 0;
    cs2d_net_encrypt(rev, "!aCz0jK5f\xFC", want, &wn);
    if ((int)strlen(enc_sec) != wn || memcmp(enc_sec, want, (size_t)wn) != 0) {
        sv_join_fail_log(c, pl ? pl->name : "?", "can't join (client verification failed)");
        cs2d_pkt_w_byte(&c->out, 10);
        sv_flush(sv, c);
        return;
    }
    if (strcmp(sha, sv->map_sha) != 0) {
        if (!sv->set.sv_maptransfer) {
            sv_join_fail_log(c, pl ? pl->name : "?", sha[0] ? "can't join (map differs)" : "can't join (map missing)");
            cs2d_pkt_w_byte(&c->out, (uint8_t)(sha[0] ? 11 : 12));
            sv_flush(sv, c);
            return;
        }

        /* sv_maptransfer enabled: send status 13 ("Downloading" window) followed by [252,50,1,...] file list */
        cs2d_pkt_w_byte(&c->out, 13);
        sv_flush(sv, c);

        /* Send [252,50,1, n u16, n x str] file list with current map */
        char map_file[128];
        snprintf(map_file, sizeof(map_file), "maps/%s.map", sv->set.sv_map);
        p = sv_packet(sv, c, true, false);
        cs2d_pkt_w_byte(p, 252);
        cs2d_pkt_w_byte(p, 50);
        cs2d_pkt_w_byte(p, 1);
        cs2d_pkt_w_short(p, 1);
        cs2d_pkt_w_str(p, map_file);

        /* End list: [252,50,1, 0, 0] */
        p = sv_packet(sv, c, true, false);
        cs2d_pkt_w_byte(p, 252);
        cs2d_pkt_w_byte(p, 50);
        cs2d_pkt_w_byte(p, 1);
        cs2d_pkt_w_short(p, 0);
        sv_flush(sv, c);
        return;
    }
    cs2d_pkt_w_byte(&c->out, 0);
    sv_flush(sv, c);
    c->join_step = 3;
}

static void sv_recv_join5(cs2d_server_t *sv, cs2d_sv_conn_t *c, sv_rd_t *r)
{
    char map[128], sha[128];
    rd_str(r, map, sizeof(map));
    rd_str(r, sha, sizeof(sha));
    cs2d_sv_player_t *pl = (c->player > 0) ? &sv->players[c->player] : NULL;
    if (!pl) return;
    if (_stricmp(map, sv->set.sv_map) != 0 || strcmp(sha, sv->map_sha) != 0) {
        cs2d_pkt_t *p = sv_packet(sv, c, true, false);
        cs2d_pkt_w_byte(p, 252); cs2d_pkt_w_byte(p, 6); cs2d_pkt_w_byte(p, 0x18);
        sv_join_fail_log(c, pl->name, sha[0] ? "can't join (post load checksum mismatch)"
                                              : "can't join (post load checksum missing)");
        sv_flush(sv, c);
        return;
    }
    /* help.php: connect is the last step of the connection process, after connect_initplayer and
     * after the client's map is accepted; join follows once the snapshot has been sent. */
    cs2d_sv_lua_hook_connect(sv, c->player);
    pl->joined = true;   /* +0xf4 */
    cs2d_pkt_t *p = sv_packet(sv, c, true, true);
    int32_t round_ms = (int32_t)(sv_now() - sv->round_start_ms);
    cs2d_sv_build_join_settings(p, &sv->set, &sv->map, sv->score_t, sv->score_ct, round_ms, sv->seed,
                                (const struct cs2d_sv_lua *)sv->lua);
    cs2d_sv_send_join_snapshot(sv, c);
    c->join_step = 4;
    cs2d_sv_lua_hook_join(sv, c->player);
}

static void sv_recv_join(cs2d_server_t *sv, cs2d_sv_conn_t *c, sv_rd_t *r)
{
    if (rd_left(r) < 1) return;
    int sub = rd_byte(r);
    /* FUN_1400d4c90(ip, 0xfc, n, 60000) is called per sub-op (knowledge/dedicated-server.md):
     * 60 for 0 / 3 / 100, 200 for 1, 30 for 5. */
    {
        int n = (sub == 1) ? 200 : ((sub == 0 || sub == 3 || sub == 100) ? 60 : ((sub == 5) ? 30 : 0));
        if (n > 0) {
            char ip[32];
            snprintf(ip, sizeof(ip), "%s", inet_ntoa(c->addr.sin_addr));
            if (!sv_flood_allow(sv, ip, n, 60000)) return;
        }
    }
    switch (sub) {
    case 0: {
        /* [252,0] -> [252,0,secret] */
        cs2d_pkt_t *p = sv_packet(sv, c, true, false);
        cs2d_pkt_w_byte(p, 252);
        cs2d_pkt_w_byte(p, 0);
        cs2d_pkt_w_str(p, sv->secret);
        sv_flush(sv, c);
        if (c->join_step < 1) c->join_step = 1;
        break;
    }
    case 1: sv_recv_join1(sv, c, r); break;
    case 3: sv_recv_join3(sv, c, r); break;
    case 5: sv_recv_join5(sv, c, r); break;
    case 10: {
        /* Client data (spray): [252, 10, 28, 2, size u16, raw u32, zlib] (27+ bytes)
         * Stored with the player, and relayed to other joined players if sv_spraytransfer == 1 */
        if (rd_left(r) >= 4) {
            int op28 = rd_byte(r);
            int sub2 = rd_byte(r);
            if (op28 == 28 && sub2 == 2 && rd_left(r) >= 2) {
                int total_len = rd_short(r);
                if (total_len > 0 && total_len <= (int)sizeof(sv->players[0].spray_logo) && rd_left(r) >= total_len) {
                    if (c->player > 0 && c->player <= CS2D_SV_MAX_PLAYERS) {
                        cs2d_sv_player_t *pl = &sv->players[c->player];
                        pl->spray_logo_len = total_len;
                        for (int k = 0; k < total_len; k++) pl->spray_logo[k] = (uint8_t)rd_byte(r);

                        /* Relay [28, player, 2, size u16, bytes] to all other joined players */
                        if (sv->set.sv_spraytransfer == 1) {
                            for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
                                cs2d_sv_conn_t *oc = conn_of_player(sv, i);
                                if (!oc || i == c->player || !sv->players[i].joined) continue;
                                cs2d_pkt_t *p28 = sv_packet(sv, oc, true, false);
                                cs2d_pkt_w_byte(p28, 28);
                                cs2d_pkt_w_byte(p28, (uint8_t)c->player);
                                cs2d_pkt_w_byte(p28, 2);
                                cs2d_pkt_w_short(p28, (uint16_t)total_len);
                                for (int k = 0; k < total_len; k++) cs2d_pkt_w_byte(p28, pl->spray_logo[k]);
                            }
                        }
                    }
                }
            }
        }
        r->p = r->n;
        break;
    }
    case 50: {
        /* File transfer request: [252, 50, 2, n u16, n x str] */
        if (rd_left(r) < 1) break;
        int sub50 = rd_byte(r);
        if (sub50 == 2) {
            int count = rd_short(r);
            if (count > 0 && count != 0xffff) {
                for (int i = 0; i < count && rd_left(r) > 0; i++) {
                    char req_file[128];
                    rd_str(r, req_file, sizeof(req_file));
                    FILE *f = fopen(req_file, "rb");
                    if (!f) continue;
                    fseek(f, 0, SEEK_END);
                    long fsz = ftell(f);
                    fseek(f, 0, SEEK_SET);
                    if (fsz <= 0 || fsz > 16 * 1024 * 1024) { fclose(f); continue; }

                    uint8_t *raw = (uint8_t *)malloc((size_t)fsz);
                    if (!raw) { fclose(f); continue; }
                    if (fread(raw, 1, (size_t)fsz, f) != (size_t)fsz) { free(raw); fclose(f); continue; }
                    fclose(f);

                    /* Compress with zlib */
                    uLongf comp_cap = compressBound((uLong)fsz);
                    uint8_t *comp = (uint8_t *)malloc((size_t)comp_cap);
                    if (!comp) { free(raw); continue; }
                    if (compress(comp, &comp_cap, raw, (uLong)fsz) != Z_OK) {
                        free(raw); free(comp); continue;
                    }

                    /* Joined payload: [raw_len i32][zlib compressed data] */
                    int total_payload = 4 + (int)comp_cap;
                    uint8_t *payload = (uint8_t *)malloc((size_t)total_payload);
                    if (!payload) { free(raw); free(comp); continue; }
                    int32_t raw_i32 = (int32_t)fsz;
                    memcpy(payload, &raw_i32, 4);
                    memcpy(payload + 4, comp, (size_t)comp_cap);

                    /* Send in chunks of up to 480 bytes */
                    int sent_bytes = 0;
                    bool first = true;
                    while (sent_bytes < total_payload) {
                        int chunk = total_payload - sent_bytes;
                        if (chunk > 480) chunk = 480;
                        bool last = (sent_bytes + chunk >= total_payload);

                        uint8_t part_op = first ? 5 : (last ? 7 : 6);
                        cs2d_pkt_t *p = sv_packet(sv, c, true, false);
                        cs2d_pkt_w_byte(p, 252);
                        cs2d_pkt_w_byte(p, 50);
                        cs2d_pkt_w_byte(p, part_op);
                        cs2d_pkt_w_str(p, req_file);
                        if (first) {
                            cs2d_pkt_w_int(p, total_payload);
                        }
                        cs2d_pkt_w_int(p, chunk);
                        for (int b = 0; b < chunk; b++) cs2d_pkt_w_byte(p, payload[sent_bytes + b]);

                        sent_bytes += chunk;
                        first = false;
                        sv_flush(sv, c);
                    }

                    free(payload);
                    free(comp);
                    free(raw);
                }
            } else if (count == 0 || count == (int)0xffff) {
                /* Finished file requests: send [252,2] to restart the join check */
                cs2d_pkt_t *p = sv_packet(sv, c, true, false);
                cs2d_pkt_w_byte(p, 252);
                cs2d_pkt_w_byte(p, 2);
                cs2d_pkt_w_byte(p, 0);
                cs2d_pkt_w_byte(p, (uint8_t)c->player);
                cs2d_pkt_w_str(p, sv->set.sv_map);
                cs2d_pkt_w_str(p, sv->secret);
                sv_flush(sv, c);
            }
        }
        r->p = r->n;
        break;
    }
    default:
        r->p = r->n;
        break;
    }
}

static void sv_remove_player(cs2d_server_t *sv, int id, int reason, const char *reason_str)
{
    if (id < 1 || id > CS2D_SV_MAX_PLAYERS || !sv->players[id].used) return;
    bool has_reason = (reason_str && reason_str[0] != '\0');
    /* The original prints nothing to its console here (proxy + dedicated log 2026-10-24: a normal leave and a kick
     * left only the Lua hook lines); the `leave` hook gets the numeric reason id (0 / 2 / 16 observed), not the text. */
    cs2d_sv_lua_hook_leave(sv, id, reason);

    /* mp_deathdrop and mp_killbuildings also apply when a player leaves (help.php) */
    cs2d_sv_death_drop(sv, id);
    cs2d_sv_kill_player_buildings(sv, id, sv->set.mp_killbuildings);

    /* [253, id, reason] to all connected/joined players (FUN_1401502a0, cmp edi, 0xf at 0x140150ae0:
     * if reason >= 16, appends u8 length + reason string). Sent reliable.
     *
     * The player is still "used" here on purpose: the original's loop walks the joined players
     * and includes the leaving/kicked client. This is required: a client that quits sends
     * [253,0] and waits for the server's [253, id, 0] (FUN_1402eee40 -> FUN_1401c7500) before
     * closing; a kicked client needs [253, id, 16, reason] to show the kick message. The caller
     * must flush the connection before deactivating it (see the leave handler and cs2d_sv_kick). */
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        cs2d_sv_conn_t *oc = conn_of_player(sv, i);
        if (!oc || !sv->players[i].joined) continue;
        cs2d_pkt_t *p = sv_packet(sv, oc, true, false);
        cs2d_pkt_w_byte(p, 253);
        cs2d_pkt_w_byte(p, (uint8_t)id);
        cs2d_pkt_w_byte(p, (uint8_t)reason);
        /* reason >= 16 always carries the u8 string, even when it is empty (the original
         * writes the string object unconditionally at 0x140150b12). */
        if (reason >= 16) cs2d_pkt_w_str(p, has_reason ? reason_str : "");
    }

    sv->players[id].used = false;

    /* help.php: disconnect runs after the player has been fully removed (player(id) no longer works),
     * with the numeric reason id of the [253] message. */
    cs2d_sv_lua_hook_disconnect(sv, id, reason);

    /* Removing a player is one of the events that run the win check in the original: the check at
     * 0x14015085b sits in FUN_1401502a0, the removal function itself (it gates on DAT_1407dfe6c == 0
     * first, like ours). Measured 2026-10-16: a client quitting a one-player standard server drew the
     * round right away, an empty server that never had a player does not end a round at all. */
    cs2d_sv_check_win(sv);
}

/* FUN_1401502a0: kick a player with optional reason ([253, id, 2] or [253, id, 16, reason]) */
void cs2d_sv_kick(cs2d_server_t *sv, int id, const char *reason)
{
    if (id < 1 || id > CS2D_SV_MAX_PLAYERS || !sv->players[id].used) return;
    bool has_reason = (reason && reason[0] != '\0');
    cs2d_sv_conn_t *kicked_conn = conn_of_player(sv, id);
    sv_remove_player(sv, id, has_reason ? 16 : 2, reason);
    if (kicked_conn) {
        sv_flush(sv, kicked_conn);
        kicked_conn->active = false;
    }
}

/* ------------------------------------------------------------------------------------------------ */
/* RCON ([236], [241], [242]) and Server Command Parser (FUN_140140a00)                             */
/* ------------------------------------------------------------------------------------------------ */

static bool sv_is_rcon_user(const cs2d_server_t *sv, int32_t usgn)
{
    if (usgn <= 0 || sv->set.sv_rconusers[0] == '\0') return false;
    const char *p = sv->set.sv_rconusers;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        if (!*p) break;
        if (atoi(p) == usgn) return true;
        while (*p && *p != ' ' && *p != '\t' && *p != ',') p++;
    }
    return false;
}

/* FUN_1401d8d90: [236, enc_pw (rstr)] -> [236, enc_pw (wstr), ok (u8)] */
void cs2d_sv_rcon_check(cs2d_server_t *sv, int id, const uint8_t *enc_pw, int enc_pw_len)
{
    if (id < 1 || id > CS2D_SV_MAX_PLAYERS || !sv->players[id].used) return;
    cs2d_sv_player_t *pl = &sv->players[id];
    cs2d_sv_conn_t *c = conn_of_player(sv, id);

    if (!sv_rcon_allow(sv, pl->ip)) return;

    char pw[256];
    cs2d_net_decrypt(enc_pw, enc_pw_len, "mysecretremotecontrolpw", pw, sizeof(pw));

    bool ok = sv_is_rcon_user(sv, pl->usgn) ||
              (sv->set.sv_rcon[0] != '\0' && strcmp(pw, sv->set.sv_rcon) == 0);
    if (ok) pl->rcon_auth = true;
    else sv_rcon_fail(sv, pl->ip);

    if (c) {
        cs2d_pkt_t *p = sv_packet(sv, c, true, false);
        cs2d_pkt_w_byte(p, 236);
        cs2d_pkt_w_raw_str(p, enc_pw, enc_pw_len);
        cs2d_pkt_w_byte(p, (uint8_t)(ok ? 1 : 0));
    }
}

/* FUN_1401d9990 -> FUN_1401bd520: [241, enc_pw (rstr), enc_cmd (rlstr)] */
void cs2d_sv_rcon(cs2d_server_t *sv, int id, const uint8_t *enc_pw, int enc_pw_len,
                  const uint8_t *enc_cmd, int enc_cmd_len)
{
    if (id < 1 || id > CS2D_SV_MAX_PLAYERS || !sv->players[id].used) return;
    cs2d_sv_player_t *pl = &sv->players[id];
    cs2d_sv_conn_t *c = conn_of_player(sv, id);

    if (!sv_rcon_allow(sv, pl->ip)) return;

    char pw[256], cmd[1024];
    cs2d_net_decrypt(enc_pw, enc_pw_len, "mysecretremotecontrolpw", pw, sizeof(pw));
    cs2d_net_decrypt(enc_cmd, enc_cmd_len, "rconcmd", cmd, sizeof(cmd));

    bool allowed = sv_is_rcon_user(sv, pl->usgn);
    if (!allowed) {
        if (sv->set.sv_rcon[0] == '\0') {
            /* 0x1401bd826: empty sv_rcon -> [241, enc("2"), enc("")] */
            if (c) {
                uint8_t ep[16];
                int ep_len = 0;
                cs2d_net_encrypt("2", "mysecretremotecontrolpw", ep, &ep_len);
                cs2d_pkt_t *p = sv_packet(sv, c, true, false);
                cs2d_pkt_w_byte(p, 241);
                cs2d_pkt_w_raw_str(p, ep, ep_len);
                cs2d_pkt_w_lstr(p, "");
            }
            return;
        }
        if (strcmp(pw, sv->set.sv_rcon) != 0) {
            sv_rcon_fail(sv, pl->ip);
            /* 0x1401bd90d: wrong password -> [241, enc("1"), enc("")] */
            if (c) {
                uint8_t ep[16];
                int ep_len = 0;
                cs2d_net_encrypt("1", "mysecretremotecontrolpw", ep, &ep_len);
                cs2d_pkt_t *p = sv_packet(sv, c, true, false);
                cs2d_pkt_w_byte(p, 241);
                cs2d_pkt_w_raw_str(p, ep, ep_len);
                cs2d_pkt_w_lstr(p, "");
            }
            return;
        }
    }

    pl->rcon_auth = true;
    for (int i = 0; i < sv->iplimit_count; i++) {
        if (_stricmp(sv->iplimits[i].ip, pl->ip) == 0) sv->iplimits[i].rcon_fails = 0;
    }
    cs2d_server_print("Parse RCon (%s:%d): %s", pl->ip, pl->port, cmd);
    cs2d_sv_exec(sv, cmd);
}

/* FUN_1401be1c0 -> FUN_1401bd520: connectionless [242, pw (rstr), cmd (rlstr)] */
void cs2d_sv_public_rcon(cs2d_server_t *sv, const struct sockaddr_in *from,
                         const char *pw, const char *cmd)
{
    char src_ip[32] = "";
    if (from) snprintf(src_ip, sizeof(src_ip), "%s", inet_ntoa(from->sin_addr));
    if (src_ip[0] && !sv_rcon_allow(sv, src_ip)) return;

    const char *err_code = NULL;
    if (sv->set.sv_rcon[0] == '\0') {
        err_code = "2"; /* 0x1401bdc50 */
    } else if (!pw || strcmp(pw, sv->set.sv_rcon) != 0) {
        err_code = "4"; /* 0x1401bdd16 */
        if (src_ip[0]) sv_rcon_fail(sv, src_ip);
    }

    if (err_code) {
        if (from && sv->sock != INVALID_SOCKET) {
            uint8_t d[32];
            uint16_t hdr = 1;
            memcpy(d, &hdr, 2);
            cs2d_pkt_t p = { { 0 }, 0 };
            cs2d_pkt_w_byte(&p, 242);
            cs2d_pkt_w_str(&p, err_code);
            cs2d_pkt_w_lstr(&p, "");
            memcpy(d + 2, p.buf, (size_t)p.pos);
            sendto(sv->sock, (const char *)d, p.pos + 2, 0,
                   (const struct sockaddr *)from, sizeof(*from));
        }
        return;
    }

    if (from) {
        for (int i = 0; i < sv->iplimit_count; i++) {
            if (_stricmp(sv->iplimits[i].ip, src_ip) == 0) sv->iplimits[i].rcon_fails = 0;
        }
        cs2d_server_print("Parse RCon ext. (%s:%d): %s",
                          inet_ntoa(from->sin_addr), ntohs(from->sin_port), cmd ? cmd : "");
    }
    if (cmd) cs2d_sv_exec(sv, cmd);
}

/*
 * Server browser queries (Find Servers):
 * Ping echo [FA] (FUN_1400d4ed0) and Info reply [FB] (FUN_1401da400).
 */
size_t cs2d_sv_build_info_reply(const cs2d_server_t *sv, uint8_t mode, uint16_t req_ver,
                                uint8_t *out, size_t max_out)
{
    if (!sv || !out || max_out < 16) return 0;

    uint16_t hdr = 1;
    memcpy(out, &hdr, 2);

    cs2d_pkt_t p;
    p.pos = 0;
    cs2d_pkt_w_byte(&p, 0xFB);
    cs2d_pkt_w_byte(&p, mode);

    if (mode == 1 || mode == 2) {
        uint8_t flags = 0;
        if (sv->set.sv_password[0] != '\0') flags |= 0x01;
        if (sv->set.sv_usgnonly == 1)       flags |= 0x02;
        if (sv->set.sv_fow > 0)             flags |= 0x04;
        if (sv->set.sv_friendlyfire == 1)   flags |= 0x08;
        if (req_ver == CS2D_SV_VERSION)     flags |= 0x10;
        if (sv->set.sv_gamemode != 0)       flags |= 0x20;
        if (sv->lua != NULL)                flags |= 0x40;
        if (sv->set.sv_forcelight != 0)     flags |= 0x80;

        uint8_t total_players = 0;
        uint8_t bots = 0;
        for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
            if (sv->players[i].used) {
                total_players++;
                if (sv->players[i].bot) bots++;
            }
        }
        uint8_t max_players = (uint8_t)sv->set.sv_maxplayers;
        if (max_players > 32 || max_players == 0) max_players = 32;

        uint8_t flags2 = 0;
        if (sv->set.mp_recoil > 0)          flags2 |= 0x01;
        if (sv->set.sv_offscreendamage > 0) flags2 |= 0x02;

        cs2d_pkt_w_byte(&p, flags);
        cs2d_pkt_w_str(&p, sv->set.sv_name[0] ? sv->set.sv_name : "CS2D Server");
        cs2d_pkt_w_str(&p, sv->set.sv_map[0] ? sv->set.sv_map : "de_dust");
        cs2d_pkt_w_byte(&p, total_players);
        cs2d_pkt_w_byte(&p, max_players);
        if (flags & 0x20) {
            cs2d_pkt_w_byte(&p, (uint8_t)sv->set.sv_gamemode);
        }
        cs2d_pkt_w_byte(&p, bots);
        cs2d_pkt_w_byte(&p, flags2);
    } else if (mode == 3) {
        /* Mode 3: ack / alive (FUN_1401da400 line 93) */
    } else if (mode == 4) {
        /* Mode 4: player names (FUN_1401da400 line 108) */
        uint8_t total_players = 0;
        for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
            if (sv->players[i].used) total_players++;
        }
        cs2d_pkt_w_byte(&p, total_players);
        for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
            if (sv->players[i].used) {
                cs2d_pkt_w_str(&p, sv->players[i].name);
            }
        }
    } else if (mode == 5) {
        /* Mode 5: player details (FUN_1401da400 line 133) */
        uint8_t total_players = 0;
        for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
            if (sv->players[i].used) total_players++;
        }
        cs2d_pkt_w_byte(&p, total_players);
        for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
            if (sv->players[i].used) {
                cs2d_pkt_w_byte(&p, (uint8_t)i);
                cs2d_pkt_w_str(&p, sv->players[i].name);
                cs2d_pkt_w_byte(&p, (uint8_t)sv->players[i].team);
                cs2d_pkt_w_int(&p, sv->players[i].score);
                cs2d_pkt_w_int(&p, sv->players[i].deaths);
            }
        }
    } else {
        return 0;
    }

    if ((size_t)p.pos + 2 > max_out) return 0;
    memcpy(out + 2, p.buf, (size_t)p.pos);
    return (size_t)p.pos + 2;
}

void cs2d_sv_handle_ping(cs2d_server_t *sv, const struct sockaddr_in *from,
                         const uint8_t *buf, int n, int q)
{
    if (!sv || !from || sv->sock == INVALID_SOCKET) return;
    if (q + 5 > n) return;
    uint8_t out[7];
    uint16_t hdr = 1;
    memcpy(out, &hdr, 2);
    out[2] = 0xFA;
    memcpy(out + 3, buf + q + 1, 4);
    sendto(sv->sock, (const char *)out, 7, 0, (const struct sockaddr *)from, sizeof(*from));
}

void cs2d_sv_handle_info(cs2d_server_t *sv, const struct sockaddr_in *from,
                         const uint8_t *buf, int n, int q)
{
    if (!sv || !from || sv->sock == INVALID_SOCKET) return;
    uint8_t mode = (q + 1 < n) ? buf[q + 1] : 1;
    uint16_t req_ver = 0;
    if (q + 3 < n) memcpy(&req_ver, buf + q + 2, 2);

    uint8_t out[1024];
    size_t len = cs2d_sv_build_info_reply(sv, mode, req_ver, out, sizeof(out));
    if (len > 0) {
        sendto(sv->sock, (const char *)out, (int)len, 0, (const struct sockaddr *)from, sizeof(*from));
    }
}

/* FUN_14006a840: resolve player by exact name or numeric ID */
static int sv_resolve_player(const cs2d_server_t *sv, const char *tok)
{
    if (!tok || !tok[0]) return 0;
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        if (sv->players[i].used && _stricmp(sv->players[i].name, tok) == 0) return i;
    }
    int id = atoi(tok);
    if (id >= 1 && id <= CS2D_SV_MAX_PLAYERS && sv->players[id].used) return id;
    return 0;
}

/* FUN_1401532f0: kick resolves by name, IP, or numeric ID */
static int sv_resolve_kick_player(const cs2d_server_t *sv, const char *tok)
{
    if (!tok || !tok[0]) return 0;
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        if (sv->players[i].used && _stricmp(sv->players[i].name, tok) == 0) return i;
    }
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        if (sv->players[i].used && strcmp(sv->players[i].ip, tok) == 0) return i;
    }
    int id = atoi(tok);
    if (id >= 1 && id <= CS2D_SV_MAX_PLAYERS && sv->players[id].used) return id;
    return 0;
}

/* FUN_140071f40: resolve weapon by numeric ID or weapon name */
/* help.php: weapon names may also be the "simplified version (lowercase without whitespaces/special
 * characters, just letters and numbers)", e.g. "ak47" for "AK-47". */
static void sv_simplify_name(const char *in, char *out, size_t out_size)
{
    size_t k = 0;
    if (!in || out_size == 0) return;
    for (const char *p = in; *p && k + 1 < out_size; p++) {
        unsigned char c = (unsigned char)*p;
        if (c >= 'A' && c <= 'Z') c = (unsigned char)(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out[k++] = (char)c;
    }
    out[k] = '\0';
}

static int sv_resolve_weapon(const char *tok)
{
    if (!tok || !tok[0]) return 0;
    int id = atoi(tok);
    if (id > 0 && id < CS2D_MAX_WEAPONS) return id;
    char want[64], have[64];
    sv_simplify_name(tok, want, sizeof(want));
    for (int i = 1; i < CS2D_MAX_WEAPONS; i++) {
        const cs2d_weapon_def_t *w = cs2d_weapon_get((uint8_t)i);
        if (!w || !w->name || !w->name[0]) continue;
        if (_stricmp(w->name, tok) == 0) return i;
        sv_simplify_name(w->name, have, sizeof(have));
        if (want[0] && strcmp(want, have) == 0) return i;
    }
    return 0;
}

/* Read one whitespace-or-quoted argument from *cursor (port of FUN_1400cd320) */
static bool sv_next_arg(const char **cursor, char *out, size_t out_size)
{
    const char *p = *cursor;
    while (*p == ' ' || *p == '\t') p++;
    if (!*p || *p == '\r' || *p == '\n') {
        out[0] = '\0';
        *cursor = p;
        return false;
    }
    size_t k = 0;
    if (*p == '"') {
        p++;
        while (*p && *p != '"' && *p != '\r' && *p != '\n') {
            if (k + 1 < out_size) out[k++] = *p;
            p++;
        }
        if (*p == '"') p++;
    } else {
        while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
            if (k + 1 < out_size) out[k++] = *p;
            p++;
        }
    }
    out[k] = '\0';
    *cursor = p;
    return true;
}

/* Read remainder of line (stripping surrounding quotes/trailing newline) */
static void sv_rest_arg(const char *cursor, char *out, size_t out_size)
{
    while (*cursor == ' ' || *cursor == '\t') cursor++;
    snprintf(out, out_size, "%s", cursor);
    cfg_value(out);
}

/* FUN_140140a00: execute a server console / RCON command */
/* Live cvar change: [237, setting, value] reliable to every joined player (+0xf4 == 1). Sender FUN_14015d960
 * (docs/dump/dedicated/FUN_14015d960_setting_broadcast.c): each console cvar handler in FUN_140140a00 calls it
 * only when the value actually changed. Setting 1 / 11 / 12 carry a u8-length string, all others an i32
 * (the raw stored value: sv_daylighttime is sent without the +1 of [252,6]). The id of each cvar is the ecx
 * immediate of its call site (0x14014b867..0x14014d143). Settings 21 and 25 are not cvars (they print a
 * message, callers 0x14015eab9 / 0x14015fe9b), and are not sent here. */
static const struct { const char *name; int id; bool is_str; } sv_setting_ids[] = {
    { "sv_name", 1, true },       { "sv_fow", 2, false },          { "sv_specmode", 3, false },
    { "mp_roundtime", 4, false }, { "mp_freezetime", 5, false },   { "mp_c4timer", 6, false },
    { "sv_gamemode", 7, false },  { "mp_respawndelay", 8, false }, { "mp_infammo", 9, false },
    { "mp_radar", 10, false },    { "mp_supply_items", 11, true }, { "mp_buymenu", 12, true },
    { "mp_hud", 13, false },      { "mp_flashlight", 14, false },  { "mp_smokeblock", 15, false },
    { "mp_hovertext", 16, false },{ "sv_maxplayers", 17, false },  { "sv_friendlyfire", 18, false },
    { "sv_forcelight", 19, false },{ "sv_daylighttime", 20, false },{ "mp_hudscale", 22, false },
    { "sv_voicechat", 23, false },{ "sv_offscreendamage", 24, false },
};

static void sv_broadcast_setting(cs2d_server_t *sv, const char *key, const char *old_val)
{
    for (size_t k = 0; k < sizeof(sv_setting_ids) / sizeof(sv_setting_ids[0]); k++) {
        if (_stricmp(key, sv_setting_ids[k].name) != 0) continue;
        const char *cur = cs2d_sv_settings_get(&sv->set, key);
        if (!cur) cur = "";
        if (!old_val) old_val = "";
        bool is_str = sv_setting_ids[k].is_str;
        if (is_str ? strcmp(cur, old_val) == 0 : atoi(cur) == atoi(old_val)) return;
        for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
            cs2d_sv_conn_t *oc = conn_of_player(sv, i);
            if (!oc || !sv->players[i].joined) continue;
            cs2d_pkt_t *p = sv_packet(sv, oc, true, false);
            cs2d_pkt_w_byte(p, 237);
            cs2d_pkt_w_byte(p, (uint8_t)sv_setting_ids[k].id);
            if (is_str) {
                size_t n = strlen(cur);
                if (n > 255) n = 255;
                cs2d_pkt_w_byte(p, (uint8_t)n);
                for (size_t c = 0; c < n; c++) cs2d_pkt_w_byte(p, (uint8_t)cur[c]);
            } else {
                cs2d_pkt_w_int(p, atoi(cur));
            }
        }
        return;
    }
}

/* exec / include: run every line of a script file (relative paths are tried under sys/ first).
 * help.php: "Execute all scripts in a script (text) file. Same as include!" */
static int s_exec_depth;

static bool sv_exec_file(cs2d_server_t *sv, const char *path, int depth)
{
    if (!sv || !path || !path[0] || depth > 8 || s_exec_depth > 8) return false;
    char full[512];
    FILE *f = NULL;
    if (path[0] == '/' || path[0] == '\\' || strchr(path, ':') || strchr(path, '/') || strchr(path, '\\')) {
        f = fopen(path, "rb");
        if (!f) {
            snprintf(full, sizeof(full), "sys/%s", path);
            f = fopen(full, "rb");
        }
    } else {
        snprintf(full, sizeof(full), "sys/%s", path);
        f = fopen(full, "rb");
        if (!f) f = fopen(path, "rb");
    }
    if (!f) return false;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        size_t n = strlen(p);
        while (n > 0 && (p[n - 1] == '\r' || p[n - 1] == '\n' || p[n - 1] == ' ' || p[n - 1] == '\t')) p[--n] = '\0';
        if (!p[0] || (p[0] == '/' && p[1] == '/') || p[0] == '#' || p[0] == ';') continue;
        s_exec_depth = depth + 1;
        cs2d_sv_exec(sv, p);
        s_exec_depth = depth;
    }
    fclose(f);
    return true;
}

bool cs2d_sv_exec_config(cs2d_server_t *sv, const char *path)
{
    return sv_exec_file(sv, path, 0);
}

bool cs2d_sv_exec(cs2d_server_t *sv, const char *cmd_line)
{
    if (!sv || !cmd_line) return false;
    if (cs2d_sv_lua_parse_hudtxt(sv, cmd_line)) return true;
    const char *cur = cmd_line;
    char cmd[64];
    if (!sv_next_arg(&cur, cmd, sizeof(cmd))) return false;

    /* alias expansion (aliases table below) */
    for (int i = 0; i < sv->alias_count; i++) {
        if (_stricmp(sv->alias_name[i], cmd) == 0) {
            char expanded[320];
            snprintf(expanded, sizeof(expanded), "%s%s%s", sv->alias_cmd[i],
                     (cur && *cur) ? " " : "", (cur && *cur) ? cur : "");
            s_exec_depth++;
            bool r = cs2d_sv_exec(sv, expanded);
            s_exec_depth--;
            return r;
        }
    }

    char a1[256], a2[256], a3[256];
    if (_stricmp(cmd, "setmoney") == 0) { /* 0x140149bf1 -> 0x140183c00 */
        sv_next_arg(&cur, a1, sizeof(a1));
        sv_next_arg(&cur, a2, sizeof(a2));
        cs2d_sv_set_playerdata(sv, atoi(a1), 2, atoi(a2));
        return true;
    }
    if (_stricmp(cmd, "sethealth") == 0) { /* 0x140149c0d -> 0x14015b910 */
        sv_next_arg(&cur, a1, sizeof(a1));
        sv_next_arg(&cur, a2, sizeof(a2));
        cs2d_sv_set_playerdata(sv, atoi(a1), 0, atoi(a2));
        return true;
    }
    if (_stricmp(cmd, "setmaxhealth") == 0) { /* 0x140149c1b -> 0x14015b980 */
        sv_next_arg(&cur, a1, sizeof(a1));
        sv_next_arg(&cur, a2, sizeof(a2));
        cs2d_sv_set_playerdata(sv, atoi(a1), 8, atoi(a2));
        return true;
    }
    if (_stricmp(cmd, "setarmor") == 0) { /* 0x140149c29 -> 0x14015ba10 */
        sv_next_arg(&cur, a1, sizeof(a1));
        sv_next_arg(&cur, a2, sizeof(a2));
        cs2d_sv_set_playerdata(sv, atoi(a1), 3, atoi(a2));
        return true;
    }
    if (_stricmp(cmd, "setscore") == 0) { /* 0x140149b1f -> 0x14015bc20 */
        sv_next_arg(&cur, a1, sizeof(a1));
        sv_next_arg(&cur, a2, sizeof(a2));
        cs2d_sv_set_playerdata(sv, atoi(a1), 1, atoi(a2));
        return true;
    }
    if (_stricmp(cmd, "setdeaths") == 0) { /* 0x140149b57 -> 0x14015bc00 */
        sv_next_arg(&cur, a1, sizeof(a1));
        sv_next_arg(&cur, a2, sizeof(a2));
        cs2d_sv_set_playerdata(sv, atoi(a1), 4, atoi(a2));
        return true;
    }
    if (_stricmp(cmd, "setassists") == 0) { /* 0x140149b2d -> 0x14015bb20 */
        sv_next_arg(&cur, a1, sizeof(a1));
        sv_next_arg(&cur, a2, sizeof(a2));
        cs2d_sv_set_playerdata(sv, atoi(a1), 9, atoi(a2));
        return true;
    }
    if (_stricmp(cmd, "setmvp") == 0) { /* 0x140149b3b -> 0x14015bb90 */
        sv_next_arg(&cur, a1, sizeof(a1));
        sv_next_arg(&cur, a2, sizeof(a2));
        cs2d_sv_set_playerdata(sv, atoi(a1), 10, atoi(a2));
        return true;
    }
    if (_stricmp(cmd, "setrank") == 0) { /* 0x140149b49 -> 0x14015ba80 */
        sv_next_arg(&cur, a1, sizeof(a1));
        sv_next_arg(&cur, a2, sizeof(a2));
        cs2d_sv_set_playerdata(sv, atoi(a1), 11, atoi(a2));
        return true;
    }
    if (_stricmp(cmd, "speedmod") == 0) { /* 0x140149b65 -> 0x14015c000 */
        sv_next_arg(&cur, a1, sizeof(a1));
        sv_next_arg(&cur, a2, sizeof(a2));
        cs2d_sv_set_playerdata(sv, atoi(a1), 7, atoi(a2) + 100);
        return true;
    }
    if (_stricmp(cmd, "equip") == 0) { /* 0x140149c37 -> 0x14015bc60 */
        sv_next_arg(&cur, a1, sizeof(a1));
        sv_next_arg(&cur, a2, sizeof(a2));
        int wpn = sv_resolve_weapon(a2);
        if (wpn > 0) cs2d_sv_set_playerdata(sv, atoi(a1), 5, wpn);
        return true;
    }
    if (_stricmp(cmd, "strip") == 0) { /* 0x140149c45 -> 0x14015be50 */
        sv_next_arg(&cur, a1, sizeof(a1));
        sv_next_arg(&cur, a2, sizeof(a2));
        int wpn = (a2[0] == '0' && a2[1] == '\0') ? 0 : sv_resolve_weapon(a2);
        cs2d_sv_set_playerdata(sv, atoi(a1), 6, wpn);
        return true;
    }
    if (_stricmp(cmd, "setweapon") == 0) { /* 0x140149bff -> 0x140159700 */
        sv_next_arg(&cur, a1, sizeof(a1));
        sv_next_arg(&cur, a2, sizeof(a2));
        cs2d_sv_select_weapon(sv, atoi(a1), sv_resolve_weapon(a2), 0);
        return true;
    }
    if (_stricmp(cmd, "setpos") == 0) { /* 0x140149be3 -> 0x14011be70 */
        sv_next_arg(&cur, a1, sizeof(a1));
        sv_next_arg(&cur, a2, sizeof(a2));
        sv_next_arg(&cur, a3, sizeof(a3));
        cs2d_sv_set_pos(sv, atoi(a1), (float)atoi(a2), (float)atoi(a3));
        return true;
    }
    if (_stricmp(cmd, "setname") == 0) { /* 0x140149b11 -> 0x14011d180 */
        sv_next_arg(&cur, a1, sizeof(a1));
        sv_next_arg(&cur, a2, sizeof(a2));
        bool hide = sv_next_arg(&cur, a3, sizeof(a3)) && (atoi(a3) != 0);
        cs2d_sv_change_name(sv, atoi(a1), a2, true, hide);
        return true;
    }
    if (_stricmp(cmd, "setteamscores") == 0) { /* 0x14014a35a -> 0x14011f890 */
        sv_next_arg(&cur, a1, sizeof(a1));
        sv_next_arg(&cur, a2, sizeof(a2));
        cs2d_sv_set_teamscores(sv, atoi(a1), atoi(a2));
        return true;
    }
    if (_stricmp(cmd, "echo") == 0) {
        sv_rest_arg(cur, a1, sizeof(a1));
        cs2d_server_print("%s", a1);
        return true;
    }
    if (_stricmp(cmd, "lua") == 0) {
        sv_rest_arg(cur, a1, sizeof(a1));
        cs2d_sv_lua_dostring(sv, a1);
        return true;
    }
    if (_stricmp(cmd, "luareset") == 0) {
        cs2d_sv_lua_close(sv);
        cs2d_sv_lua_init(sv);
        return true;
    }
    if (_stricmp(cmd, "sv_msg") == 0 || _stricmp(cmd, "msg") == 0) { /* 0x140144a92 / wrapper msg */
        sv_rest_arg(cur, a1, sizeof(a1));
        cs2d_sv_msg(sv, a1, 0);
        return true;
    }
    if (_stricmp(cmd, "sv_msg2") == 0 || _stricmp(cmd, "msg2") == 0) { /* 0x140144b99 / wrapper msg2 */
        sv_next_arg(&cur, a1, sizeof(a1));
        sv_rest_arg(cur, a2, sizeof(a2));
        cs2d_sv_msg(sv, a2, atoi(a1));
        return true;
    }
    if (_stricmp(cmd, "maket") == 0 || _stricmp(cmd, "makect") == 0 || _stricmp(cmd, "makespec") == 0) {
        /* 0x140149c53 / 0x140149c65 / 0x140149c77 -> 0x140164590 */
        sv_next_arg(&cur, a1, sizeof(a1));
        int pid = sv_resolve_player(sv, a1);
        if (pid > 0) {
            int t = (_stricmp(cmd, "maket") == 0) ? 1 : ((_stricmp(cmd, "makect") == 0) ? 2 : 0);
            cs2d_sv_team_ex(sv, pid, t, t == 0 ? 5 : 4, true);
        }
        return true;
    }
    if (_stricmp(cmd, "slap") == 0 || _stricmp(cmd, "deathslap") == 0) {
        /* 0x140149cba / 0x140149c8d -> 0x140167e40 */
        sv_next_arg(&cur, a1, sizeof(a1));
        int pid = sv_resolve_player(sv, a1);
        if (pid > 0) cs2d_sv_slap(sv, pid, _stricmp(cmd, "deathslap") == 0);
        return true;
    }
    if (_stricmp(cmd, "kick") == 0) { /* 0x140149cf7 -> 0x1401532f0 */
        sv_next_arg(&cur, a1, sizeof(a1));
        sv_rest_arg(cur, a2, sizeof(a2));
        int pid = sv_resolve_kick_player(sv, a1);
        if (pid > 0) cs2d_sv_kick(sv, pid, a2);
        return true;
    }
    if (_stricmp(cmd, "killplayer") == 0) { /* 0x140149ae0 */
        sv_next_arg(&cur, a1, sizeof(a1));
        int pid = atoi(a1);
        if (pid >= 1 && pid <= CS2D_SV_MAX_PLAYERS && sv->players[pid].used && sv->players[pid].alive) {
            cs2d_sv_kill(sv, pid, 0, 0, 0);
        }
        return true;
    }
    if (_stricmp(cmd, "spawnplayer") == 0) { /* 0x140149ad2 -> 0x14017c910 */
        sv_next_arg(&cur, a1, sizeof(a1));
        sv_next_arg(&cur, a2, sizeof(a2));
        sv_next_arg(&cur, a3, sizeof(a3));
        cs2d_sv_spawn_at(sv, atoi(a1), (float)atoi(a2), (float)atoi(a3));
        return true;
    }
    if (_stricmp(cmd, "triggerposition") == 0) {
        sv_next_arg(&cur, a1, sizeof(a1));
        sv_next_arg(&cur, a2, sizeof(a2));
        int tx = atoi(a1), ty = atoi(a2);
        /* Toggles the entity at the tile and broadcasts [31] (knowledge: triggerposition). */
        cs2d_sv_trigger_at_position(sv, tx, ty, 1);
        return true;
    }
    if (_stricmp(cmd, "killobject") == 0) {
        sv_next_arg(&cur, a1, sizeof(a1));
        cs2d_sv_object_damage(sv, atoi(a1), 1000000, 0);
        return true;
    }
    if (_stricmp(cmd, "effect") == 0) {
        char name[32], a4[32], a5[32], a6[32], a7[32], a8[32], a9[32];
        sv_next_arg(&cur, name, sizeof(name));
        sv_next_arg(&cur, a1, sizeof(a1));   /* x (pixels) */
        sv_next_arg(&cur, a2, sizeof(a2));   /* y (pixels) */
        int p1 = sv_next_arg(&cur, a4, sizeof(a4)) ? atoi(a4) : 0;
        int p2 = sv_next_arg(&cur, a5, sizeof(a5)) ? atoi(a5) : 0;
        int r = sv_next_arg(&cur, a6, sizeof(a6)) ? atoi(a6) : 0;
        int g = sv_next_arg(&cur, a7, sizeof(a7)) ? atoi(a7) : 0;
        int b = sv_next_arg(&cur, a8, sizeof(a8)) ? atoi(a8) : 0;
        (void)a9;
        /* Console command FUN_14011e170 -> FUN_140102be0 (cs2d_dedicated.exe): the name selects the [48] type
         * (fire 6, smoke 7, blood 8, flare 100, colorsmoke 101, sparkles 102) and p1 is clamped before it is
         * sent (100 / 100 / blood 35, floor 0 / 100 / 100 / sparkles 50). Types < 100 carry no r,g,b; p2 is
         * sent unchanged. */
        int type = 0, cap = 100;
        if (_stricmp(name, "fire") == 0) type = 6;
        else if (_stricmp(name, "smoke") == 0) type = 7;
        else if (_stricmp(name, "blood") == 0) { type = 8; cap = 35; }
        else if (_stricmp(name, "flare") == 0) type = 100;
        else if (_stricmp(name, "colorsmoke") == 0) type = 101;
        else if (_stricmp(name, "sparkles") == 0) { type = 102; cap = 50; }
        if (type == 0) return false;
        if (p1 > cap) p1 = cap;
        if (type == 8 && p1 < 0) p1 = 0;
        cs2d_sv_send_fx(sv, type, atoi(a1), atoi(a2), p1, p2, r, g, b);
        return true;
    }
    if (_stricmp(cmd, "explosion") == 0) {
        char a4[32], a5[32];
        sv_next_arg(&cur, a1, sizeof(a1));
        sv_next_arg(&cur, a2, sizeof(a2));
        sv_next_arg(&cur, a3, sizeof(a3));
        int dmg = sv_next_arg(&cur, a4, sizeof(a4)) ? atoi(a4) : 0;
        int src = sv_next_arg(&cur, a5, sizeof(a5)) ? atoi(a5) : 0;
        cs2d_sv_explosion(sv, atoi(a1), atoi(a2), atoi(a3), dmg, src);
        return true;
    }
    if (_stricmp(cmd, "sv_sound") == 0) {
        sv_rest_arg(cur, a1, sizeof(a1));
        cs2d_sv_server_sound(sv, 10, a1, 0);
        return true;
    }
    if (_stricmp(cmd, "sv_sound2") == 0) {
        sv_next_arg(&cur, a1, sizeof(a1));
        sv_rest_arg(cur, a2, sizeof(a2));
        int pid = atoi(a1);
        if (pid >= 1 && pid <= CS2D_SV_MAX_PLAYERS) cs2d_sv_positional_sound(sv, 0, 0, a2, pid);
        return true;
    }
    if (_stricmp(cmd, "sv_soundpos") == 0) {
        sv_next_arg(&cur, a1, sizeof(a1));
        sv_next_arg(&cur, a2, sizeof(a2));
        sv_next_arg(&cur, a3, sizeof(a3));
        char a4[32];
        int pid = sv_next_arg(&cur, a4, sizeof(a4)) ? atoi(a4) : 0;
        cs2d_sv_positional_sound(sv, atoi(a2), atoi(a3), a1, pid);
        return true;
    }
    if (_stricmp(cmd, "sv_stopsound") == 0) {
        sv_next_arg(&cur, a1, sizeof(a1));
        sv_next_arg(&cur, a2, sizeof(a2));
        cs2d_sv_server_sound(sv, 11, a1, atoi(a2));
        return true;
    }
    if (_stricmp(cmd, "damagetile") == 0) {
        /* help.php: only static objects on that tile (buildings; Env_Breakable is client-side here) */
        sv_next_arg(&cur, a1, sizeof(a1));
        sv_next_arg(&cur, a2, sizeof(a2));
        sv_next_arg(&cur, a3, sizeof(a3));
        cs2d_dynobj_t *o = cs2d_dynobj_at_tile(&sv->dynobj, atoi(a1), atoi(a2));
        if (o) cs2d_sv_object_damage(sv, o->id, atoi(a3), 0);
        return true;
    }
    if (_stricmp(cmd, "damageobject") == 0) {
        sv_next_arg(&cur, a1, sizeof(a1));
        sv_next_arg(&cur, a2, sizeof(a2));
        int player = sv_next_arg(&cur, a3, sizeof(a3)) ? atoi(a3) : 0;
        cs2d_sv_object_damage(sv, atoi(a1), atoi(a2), player);
        return true;
    }
    if (_stricmp(cmd, "resources") == 0) {
        int imgs = 0;
        for (int i = 0; i < CS2D_DYNOBJ_MAX; i++) {
            if (sv->dynobj.objs[i].active && sv->dynobj.objs[i].typ == CS2D_DO_IMAGE) imgs++;
        }
        cs2d_server_print("Resources: %d loaded dynamic images, map '%s'", imgs, sv->set.sv_map);
        return true;
    }
    if (_stricmp(cmd, "sv_restart") == 0 || _stricmp(cmd, "sv_restartround") == 0 ||
        _stricmp(cmd, "restart") == 0 || _stricmp(cmd, "restartround") == 0) {
        /* 0x140145180 -> 0x14015d2a0 */
        int delay = sv_next_arg(&cur, a1, sizeof(a1)) ? atoi(a1) : 0;
        if (delay < 0) delay = 0;
        if (delay > 10) delay = 10;
        cs2d_sv_round_end(sv, 5, delay * 1000);
        if (delay == 0) cs2d_sv_round_start(sv, 5);
        return true;
    }
    if (_stricmp(cmd, "exec") == 0 || _stricmp(cmd, "include") == 0) {
        sv_rest_arg(cur, a1, sizeof(a1));
        return sv_exec_file(sv, a1, s_exec_depth);
    }
    if (_stricmp(cmd, "alias") == 0) {
        sv_next_arg(&cur, a1, sizeof(a1));
        sv_rest_arg(cur, a2, sizeof(a2));
        if (!a1[0] || !a2[0]) return false;
        int slot = -1;
        for (int i = 0; i < sv->alias_count; i++) {
            if (_stricmp(sv->alias_name[i], a1) == 0) { slot = i; break; }
        }
        if (slot < 0) {
            if (sv->alias_count >= (int)(sizeof(sv->alias_name) / sizeof(sv->alias_name[0]))) return false;
            slot = sv->alias_count++;
        }
        snprintf(sv->alias_name[slot], sizeof(sv->alias_name[slot]), "%.31s", a1);
        snprintf(sv->alias_cmd[slot], sizeof(sv->alias_cmd[slot]), "%.159s", a2);
        return true;
    }
    if (_stricmp(cmd, "aliases") == 0) {
        for (int i = 0; i < sv->alias_count; i++) {
            cs2d_server_print("%s = %s", sv->alias_name[i], sv->alias_cmd[i]);
        }
        return true;
    }
    if (_stricmp(cmd, "mp_wpndmg") == 0) {
        sv_next_arg(&cur, a1, sizeof(a1));
        sv_next_arg(&cur, a2, sizeof(a2));
        int wpn = sv_resolve_weapon(a1);
        if (wpn > 0 && wpn < CS2D_MAX_WEAPONS) {
            sv->wpndmg[wpn] = atoi(a2);
            sv->wpndmg_set[wpn] = true;
            return true;
        }
        return false;
    }
    if (_stricmp(cmd, "sv_gamemode") == 0 || _stricmp(cmd, "gamemode") == 0 ||
        _stricmp(cmd, "sv_gm") == 0) {
        if (!sv_next_arg(&cur, a1, sizeof(a1)) || a1[0] == '\0') {
            cs2d_server_print("sv_gamemode is \"%d\"", sv->set.sv_gamemode);
        } else {
            int old_mode = sv->set.sv_gamemode;
            int new_mode = atoi(a1);
            char old_str[16];
            snprintf(old_str, sizeof(old_str), "%d", old_mode);
            cs2d_sv_settings_set(&sv->set, "sv_gamemode", a1);
            sv_broadcast_setting(sv, "sv_gamemode", old_str);
            cs2d_server_print("sv_gamemode set to \"%d\"", new_mode);
            if (new_mode != old_mode) {
                /* The round restarts from the main loop (cs2d_sv_step_round), after that loop's Lua
                 * timers: probe on the original with maps/fun_roleplay.lua's `parse("sv_gamemode 1")`:
                 * endround 5 at once, first `second`/`ms100` hooks, then startround 5. */
                cs2d_sv_round_end(sv, 5, 0);
            }
        }
        return true;
    }
    if (_stricmp(cmd, "trigger") == 0) {
        if (sv_next_arg(&cur, a1, sizeof(a1)) && a1[0] != '\0') {
            /* help.php: one Lua trigger hook per list entry, source 1 = script command. */
            cs2d_sv_trigger_names(sv, a1, 1);
        }
        return true;
    }
    if (_stricmp(cmd, "endround") == 0) { /* 0x140145172 -> 0x14015d3c0 */
        int m = sv_next_arg(&cur, a1, sizeof(a1)) ? atoi(a1) : 0;
        int rmode = (m == 1) ? 1 : ((m == 2) ? 2 : 3);
        cs2d_sv_round_end(sv, rmode, 5000);
        return true;
    }
    if (_stricmp(cmd, "listplayers") == 0) { /* 0x1401417d9 */
        for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
            if (sv->players[i].used) {
                cs2d_server_print("#%d %s (%s:%d)", i, sv->players[i].name,
                                  sv->players[i].ip, sv->players[i].port);
            }
        }
        return true;
    }
    if (_stricmp(cmd, "bot_add") == 0) {
        int team = sv_next_arg(&cur, a1, sizeof(a1)) ? atoi(a1) : 0;
        int look = sv_next_arg(&cur, a2, sizeof(a2)) ? atoi(a2) : -1;
        sv_rest_arg(cur, a3, sizeof(a3));
        cs2d_sv_bot_add(sv, team, look, a3);
        return true;
    }
    if (_stricmp(cmd, "bot_add_t") == 0) {
        int look = sv_next_arg(&cur, a1, sizeof(a1)) ? atoi(a1) : -1;
        sv_rest_arg(cur, a2, sizeof(a2));
        cs2d_sv_bot_add(sv, 1, look, a2);
        return true;
    }
    if (_stricmp(cmd, "bot_add_ct") == 0) {
        int look = sv_next_arg(&cur, a1, sizeof(a1)) ? atoi(a1) : -1;
        sv_rest_arg(cur, a2, sizeof(a2));
        cs2d_sv_bot_add(sv, 2, look, a2);
        return true;
    }
    if (_stricmp(cmd, "bot_kick") == 0) {
        sv_rest_arg(cur, a1, sizeof(a1));
        cs2d_sv_bot_kick(sv, a1);
        return true;
    }
    if (_stricmp(cmd, "bot_kill") == 0) {
        sv_rest_arg(cur, a1, sizeof(a1));
        cs2d_sv_bot_kill(sv, a1);
        return true;
    }
    if (_stricmp(cmd, "banip") == 0) {
        sv_next_arg(&cur, a1, sizeof(a1));
        sv_rest_arg(cur, a2, sizeof(a2));
        cs2d_sv_ban_ip(sv, a1, a2);
        return true;
    }
    if (_stricmp(cmd, "banusgn") == 0) {
        sv_next_arg(&cur, a1, sizeof(a1));
        sv_rest_arg(cur, a2, sizeof(a2));
        cs2d_sv_ban_usgn(sv, atoi(a1), a2);
        return true;
    }
    if (_stricmp(cmd, "bansteam") == 0) {
        sv_next_arg(&cur, a1, sizeof(a1));
        sv_rest_arg(cur, a2, sizeof(a2));
        cs2d_sv_ban_steam(sv, _strtoi64(a1, NULL, 10), a2);
        return true;
    }
    if (_stricmp(cmd, "unban") == 0) {
        sv_next_arg(&cur, a1, sizeof(a1));
        cs2d_sv_unban(sv, a1);
        return true;
    }
    if (_stricmp(cmd, "banname") == 0) {
        /* help.php: ban a player by name (all identifiers of that player) */
        sv_next_arg(&cur, a1, sizeof(a1));
        sv_rest_arg(cur, a2, sizeof(a2));
        int pid = sv_resolve_player(sv, a1);
        if (pid > 0) {
            cs2d_sv_player_t *pl = &sv->players[pid];
            cs2d_sv_ban_ip(sv, pl->ip, a2);
            if (pl->usgn > 0) cs2d_sv_ban_usgn(sv, pl->usgn, a2);
            if (pl->steam_id > 0) cs2d_sv_ban_steam(sv, pl->steam_id, a2);
            cs2d_sv_kick(sv, pid, a2[0] ? a2 : "Banned");
        }
        return true;
    }
    if (_stricmp(cmd, "unbanall") == 0) {
        memset(sv->bans, 0, sizeof(sv->bans));
        sv->ban_count = 0;
        cs2d_server_print("All bans removed");
        return true;
    }
    if (_stricmp(cmd, "connections") == 0) {
        cs2d_server_print("Connections: %d active, %d players",
                          sv_count_active_conns(sv), sv_player_count(sv));
        return true;
    }
    if (_stricmp(cmd, "mapinfo") == 0) {
        cs2d_server_print("Map: %s (%ux%u tiles, %u entities, sha256 %s)",
                          sv->set.sv_map, sv->map.width, sv->map.height,
                          sv->map.entity_count, sv->map_sha);
        return true;
    }
    if (_stricmp(cmd, "maps") == 0) {
        for (int i = 0; i < sv->map_count; i++) cs2d_server_print("  %s", sv->maps[i]);
        return true;
    }
    if (_stricmp(cmd, "serverinfo") == 0) {
        cs2d_server_print("%s - %s - port %d - %d/%d players", sv->set.sv_name, sv->set.sv_map,
                          sv->set.sv_hostport, sv_player_count(sv), sv->set.sv_maxplayers);
        return true;
    }
    if (_stricmp(cmd, "listbans") == 0 || _stricmp(cmd, "bans") == 0) {
        for (int i = 0; i < sv->ban_count; i++) {
            const cs2d_sv_ban_t *b = &sv->bans[i];
            if (!b->active) continue;
            if (b->ip[0]) cs2d_server_print("#%d IP %s (%s)", i + 1, b->ip, b->reason);
            else if (b->usgn > 0) cs2d_server_print("#%d USGN #%d (%s)", i + 1, b->usgn, b->reason);
            else if (b->steam_id > 0) cs2d_server_print("#%d Steam %lld (%s)", i + 1, (long long)b->steam_id, b->reason);
        }
        return true;
    }
    if (_stricmp(cmd, "map") == 0 || _stricmp(cmd, "changemap") == 0) {
        sv_next_arg(&cur, a1, sizeof(a1));
        cs2d_sv_change_map(sv, a1);
        return true;
    }
    if (_stricmp(cmd, "settile") == 0) { /* 0x1401428d6 -> FUN_14011f210 -> FUN_14011ed00 */
        char a_x[32], a_y[32], a_tile[32], a_rot[32], a_r[32], a_g[32], a_b[32], a_overlay[32], a_dir[32], a_typ[32];
        if (!sv_next_arg(&cur, a_x, sizeof(a_x)) ||
            !sv_next_arg(&cur, a_y, sizeof(a_y)) ||
            !sv_next_arg(&cur, a_tile, sizeof(a_tile))) {
            return false;
        }
        cs2d_settile_t st;
        cs2d_settile_defaults(&st, atoi(a_x), atoi(a_y), atoi(a_tile));
        if (sv_next_arg(&cur, a_rot, sizeof(a_rot))) {
            st.rot = (uint8_t)atoi(a_rot);
            if (sv_next_arg(&cur, a_r, sizeof(a_r))) {
                st.r = (uint8_t)atoi(a_r);
                if (sv_next_arg(&cur, a_g, sizeof(a_g))) {
                    st.g = (uint8_t)atoi(a_g);
                    if (sv_next_arg(&cur, a_b, sizeof(a_b))) {
                        st.b = (uint8_t)atoi(a_b);
                        if (sv_next_arg(&cur, a_overlay, sizeof(a_overlay))) {
                            st.overlay = (uint8_t)atoi(a_overlay);
                            if (sv_next_arg(&cur, a_dir, sizeof(a_dir)) &&
                                sv_next_arg(&cur, a_typ, sizeof(a_typ))) {
                                st.blend = cs2d_settile_blend(a_dir, a_typ);
                            }
                        }
                    }
                }
            }
        }
        return cs2d_sv_settile(sv, &st);
    }
    if (_stricmp(cmd, "spawnitem") == 0) {
        char a4[32], a5[32];
        sv_next_arg(&cur, a1, sizeof(a1));
        sv_next_arg(&cur, a2, sizeof(a2));
        sv_next_arg(&cur, a3, sizeof(a3));
        int type = sv_resolve_weapon(a1);
        int clip = sv_next_arg(&cur, a4, sizeof(a4)) ? atoi(a4) : -1;
        int res = sv_next_arg(&cur, a5, sizeof(a5)) ? atoi(a5) : -1;
        return cs2d_sv_spawn_item(sv, type, atoi(a2), atoi(a3), clip, res) != 0;
    }
    if (_stricmp(cmd, "spawnobject") == 0) {
        /* FUN_14011d6c0: spawnobject <type> <x> <y> <rot> <mode> <team> <player> (tiles) */
        char a4[32], a5[32], a6[32], a7[32];
        sv_next_arg(&cur, a1, sizeof(a1));
        sv_next_arg(&cur, a2, sizeof(a2));
        sv_next_arg(&cur, a3, sizeof(a3));
        float rot = sv_next_arg(&cur, a4, sizeof(a4)) ? (float)atof(a4) : 0.0f;
        int mode = sv_next_arg(&cur, a5, sizeof(a5)) ? atoi(a5) : 0;
        int team = sv_next_arg(&cur, a6, sizeof(a6)) ? atoi(a6) : 0;
        int player = sv_next_arg(&cur, a7, sizeof(a7)) ? atoi(a7) : 0;
        cs2d_sv_spawn_object(sv, atoi(a1), atoi(a2), atoi(a3), mode, team, player, rot, false);
        return true;
    }
    if (_stricmp(cmd, "spawnnpc") == 0) {
        /* FUN_14011d910: spawnnpc <npc type> <x> <y> <rot> (tiles); NPCs are objects of type 30 with
         * the NPC type in the player attribute, mode / team 0, spawned at the tile centre */
        char a4[32];
        sv_next_arg(&cur, a1, sizeof(a1));
        sv_next_arg(&cur, a2, sizeof(a2));
        sv_next_arg(&cur, a3, sizeof(a3));
        float rot = sv_next_arg(&cur, a4, sizeof(a4)) ? (float)atof(a4) : 0.0f;
        cs2d_sv_spawn_object(sv, 30, atoi(a2), atoi(a3), 0, 0, atoi(a1), rot, true);
        return true;
    }
    if (_stricmp(cmd, "spawnprojectile") == 0) {
        /* help.php: spawnprojectile <player> <type> <x> <y> <flydist> <dir>; x / y and flydist
         * are in PIXELS (not tiles) and flydist 0 spawns the projectile on the ground. */
        char a4[32], a5[32], a6[32];
        sv_next_arg(&cur, a1, sizeof(a1));
        sv_next_arg(&cur, a2, sizeof(a2));
        sv_next_arg(&cur, a3, sizeof(a3));
        sv_next_arg(&cur, a4, sizeof(a4));
        int fly = sv_next_arg(&cur, a5, sizeof(a5)) ? atoi(a5) : 0;
        int dir = sv_next_arg(&cur, a6, sizeof(a6)) ? atoi(a6) : 0;
        return cs2d_sv_spawn_projectile(sv, atoi(a1), sv_resolve_weapon(a2),
                                        atoi(a3), atoi(a4), fly, dir) != 0;
    }
    if (_stricmp(cmd, "mp_building_price") == 0) {
        sv_next_arg(&cur, a1, sizeof(a1));
        sv_next_arg(&cur, a2, sizeof(a2));
        int typ = cs2d_sv_resolve_building_type(a1);
        if (typ > 0 && typ < CS2D_DYNOBJ_TYPES) {
            sv->building_price[typ] = atoi(a2);
            sv->building_price_mask |= (1ULL << typ);
            return true;
        }
        return false;
    }
    if (_stricmp(cmd, "mp_building_limit") == 0) {
        sv_next_arg(&cur, a1, sizeof(a1));
        sv_next_arg(&cur, a2, sizeof(a2));
        int typ = cs2d_sv_resolve_building_type(a1);
        if (typ > 0 && typ < CS2D_DYNOBJ_TYPES) {
            sv->building_limit[typ] = atoi(a2);
            sv->building_limit_mask |= (1ULL << typ);
            return true;
        }
        return false;
    }
    if (_stricmp(cmd, "mp_building_health") == 0) {
        sv_next_arg(&cur, a1, sizeof(a1));
        sv_next_arg(&cur, a2, sizeof(a2));
        int typ = cs2d_sv_resolve_building_type(a1);
        if (typ > 0 && typ < CS2D_DYNOBJ_TYPES) {
            int hp = atoi(a2);
            sv->building_health[typ] = hp;
            sv->building_health_mask |= (1ULL << typ);
            /* Clamp existing buildings of this type if health exceeds new max (0x1401182a6) */
            for (int i = 0; i < CS2D_DYNOBJ_MAX; i++) {
                cs2d_dynobj_t *o = &sv->dynobj.objs[i];
                if (o->active && o->typ == typ && o->health > hp) {
                    o->health = hp;
                }
            }
            return true;
        }
        return false;
    }

    /* Fall back to server settings cvars (sv_*, mp_*, etc.) */
    sv_rest_arg(cur, a1, sizeof(a1));
    if (a1[0] != '\0') {
        char old_val[256];
        const char *prev = cs2d_sv_settings_get(&sv->set, cmd);
        snprintf(old_val, sizeof(old_val), "%s", prev ? prev : "");
        cfg_set(&sv->set, cmd, a1);
        sv_broadcast_setting(sv, cmd, old_val);
        if (_stricmp(cmd, "sv_gamemode") == 0) {
            int old_mode = atoi(old_val);
            int new_mode = atoi(a1);
            if (new_mode != old_mode) {
                /* The round restarts from the main loop (cs2d_sv_step_round), after that loop's Lua
                 * timers: probe on the original with maps/fun_roleplay.lua's `parse("sv_gamemode 1")`:
                 * endround 5 at once, first `second`/`ms100` hooks, then startround 5. */
                cs2d_sv_round_end(sv, 5, 0);
            }
        }
        return true;
    } else {
        const char *cur_val = cs2d_sv_settings_get(&sv->set, cmd);
        if (cur_val) {
            cs2d_server_print("\"%s\" is \"%s\"", cmd, cur_val);
            return true;
        }
    }
    return false;
}

static const char *sv_resolve_key_name(cs2d_server_t *sv, int key, char *buf, size_t buf_sz)
{
    const char *name = cs2d_bind_vk_name(key);
    if (sv && sv->lua) {
        cs2d_sv_lua_t *lua = (cs2d_sv_lua_t *)sv->lua;
        for (int i = 0; i < lua->bind_key_count; i++) {
            if (cs2d_bind_key_code(lua->bind_keys[i]) == key) return lua->bind_keys[i];
        }
    }
    if (name) return name;
    snprintf(buf, buf_sz, "%d", key);
    return buf;
}

/* Messages of a joined player. Phase 1 handles the team choice; everything else is skipped by length
 * where it is known and ends the datagram otherwise (like the original does on an unknown opcode). */
static bool sv_recv_game(cs2d_server_t *sv, cs2d_sv_conn_t *c, sv_rd_t *r, int op)
{
    cs2d_sv_player_t *pl = (c->player > 0) ? &sv->players[c->player] : NULL;
    switch (op) {
    case 253: { /* leave: [253, reason u8, optional reason str] (FUN_1401e3790) */
        int reason = 0;
        char reason_str[128] = "";
        if (rd_left(r) > 0) reason = rd_byte(r);
        if (reason >= 16 && rd_left(r) > 0) rd_str(r, reason_str, sizeof(reason_str));
        if (pl) {
            sv_remove_player(sv, c->player, reason, reason_str);
            /* The client that sent [253] waits for the server's [253, id, reason] before it
             * closes the socket; without this flush it stays in "Disconnecting / Please wait". */
            sv_flush(sv, c);
        }
        c->active = false;
        return false;
    }
    case 20: { /* team: [20,team,look] (FUN_1401cd140 -> FUN_1401624c0) */
        if (rd_left(r) < 2) return false;
        int team = rd_byte(r), look = rd_byte(r);
        if (pl) cs2d_sv_team(sv, c->player, team, look);
        return true;
    }
    case 229: { /* clock ping reply: [229, srv ts, cli ts] */
        int32_t ts = rd_int(r);
        int32_t cli_ts = rd_int(r);
        cs2d_sv_player_t *pp = (c->player > 0) ? &sv->players[c->player] : NULL;
        if (pp && pp->used) {
            /* FUN_1401d7690 (0xe5): ping = (now - start) - echoed ts, accepted below 30 s; the client clock
             * delta over the 2 s ping interval is the anti-clock ratio. */
            uint32_t rtt = (sv_now() - sv->start_ms) - (uint32_t)ts;
            int prev = pp->clock_prev;
            if (rtt < 30000u) {
                pp->ping = (int)rtt;
                pp->clock_diff = cli_ts - pp->clock_prev;
                pp->clock_ratio = (float)pp->clock_diff * 0.0005f;
                pp->clock_prev = cli_ts;
            }
            /* ratio > 1.0 + DAT_1405a64e0 (0.1, initial value, never rewritten) is a strike, anything else
             * (or the first sample) removes one; mp_anticlock N kicks above the table {0, 8, 4, 2}[N]
             * strikes (array built at 0x140211f34 from 0x1406dafe0), "kick <id> "Speedhack Detected"". */
            static const int limit[4] = { 0, 8, 4, 2 };
            if (prev < 1 || pp->clock_ratio <= 1.1f) {
                if (pp->clock_strikes > 0) pp->clock_strikes--;
            } else {
                pp->clock_strikes++;
                int lv = sv->set.mp_anticlock;
                if (lv > 0 && lv < 4 && limit[lv] < pp->clock_strikes)
                    cs2d_sv_kick(sv, c->player, "Speedhack Detected");
            }
        }
        return true;
    }
    case 249: { /* ping reply: [249, srv ts] (0x1401d9f10 echoes it) */
        int32_t ts = rd_int(r);
        cs2d_sv_player_t *pp = (c->player > 0) ? &sv->players[c->player] : NULL;
        if (pp && pp->used) {
            uint32_t rtt = (sv_now() - sv->start_ms) - (uint32_t)ts;
            if (rtt <= 30000u) pp->ping = (int)rtt;
        }
        return true;
    }
    case 10: case 11: { /* position [10 run / 11 walk, x, y] (FUN_1401cb570 / FUN_1401cb930) */
        if (rd_left(r) < 4) return false;
        int x = rd_short(r), y = rd_short(r);
        if (pl && pl->alive) {
            cs2d_sv_validate_move(sv, c->player, (float)x, (float)y, op);
        }
        return true;
    }
    case 12: { /* rotation only: [12, rot u16] (FUN_1401cbce0) */
        if (rd_left(r) < 2) return false;
        int rot = rd_short(r);
        if (pl && pl->alive) {
            pl->rot = (float)rot * 0.005493164f;
            pl->rot_flag = 1;
        }
        return true;
    }
    case 13: case 14: { /* run + rot (13) / walk + rot (14): [13|14, x u16, y u16, rot u16] (FUN_1401cc0b0 / FUN_1401cc310) */
        if (rd_left(r) < 6) return false;
        int x = rd_short(r), y = rd_short(r), rot = rd_short(r);
        if (pl && pl->alive) {
            cs2d_sv_validate_move(sv, c->player, (float)x, (float)y, op == 13 ? 10 : 11);
            pl->rot = (float)rot * 0.005493164f;
            pl->rot_flag = 1;
        }
        return true;
    }
    case 18: { /* no-op in dedicated dispatcher (0x1401e4350: xor esi, esi; jmp next) */
        return true;
    }
    case 71: case 72: { /* run / walk + rot + aim: [71|72, x u16, y u16, rot u16, aim] (FUN_1401d3fb0 / FUN_1401d4430:
                         * both read opcode + 3 shorts + 1 byte and need > 7 bytes left; proxy: "47 10 04 6b 05 c1 d6 87").
                         * 71 used to read only x, y, aim, so 2 bytes were parsed as opcodes (e.g. 57 flashlight, 253 leave) */
        if (rd_left(r) < 7) return false;
        int x = rd_short(r), y = rd_short(r), rot = rd_short(r), aim = rd_byte(r);
        if (pl && pl->alive) {
            cs2d_sv_validate_move(sv, c->player, (float)x, (float)y, op == 71 ? 10 : 11);
            pl->rot = (float)rot / 182.04445f;
            pl->aim = (float)aim * 2.0f;
            pl->aim_valid = true;
            pl->rot_flag = 1;
        }
        return true;
    }
    case 70: { /* rotation [70, rot u16, aim / 2] (FUN_1401d3c40) */
        if (rd_left(r) < 3) return false;
        int rot = rd_short(r), aim = rd_byte(r);
        if (pl && pl->alive) {
            pl->rot = (float)rot / 182.04445f;
            pl->aim = (float)aim * 2.0f;
            pl->aim_valid = true;
            pl->rot_flag = 1;
        }
        return true;
    }
    case 39: { /* respawn request: [39] (FUN_1401cf8d0) */
        if (pl) cs2d_sv_request_respawn(sv, c->player);
        return true;
    }
    case 9: { /* select weapon: [9, weapon u8, mode u8] (FUN_1401c8f00) */
        if (rd_left(r) < 2) return false;
        int wep = rd_byte(r), mode = rd_byte(r);
        if (pl) cs2d_sv_select_weapon(sv, c->player, wep, mode);
        return true;
    }
    case 7: { /* fire: [7, seed u8] (FUN_1401c8930) */
        if (rd_left(r) < 1) return false;
        int seed = rd_byte(r);
        if (pl) cs2d_sv_fire(sv, c->player, seed);
        return true;
    }
    case 8: { /* attack2: [8, mode u8] (FUN_1401c8c50) */
        if (rd_left(r) < 1) return false;
        int mode = rd_byte(r);
        if (pl) cs2d_sv_attack2(sv, c->player, mode);
        return true;
    }
    case 17: { /* damage / hit report: [17, victim u8, dmg u8, weapon u8] (FUN_1401cc9f0) */
        if (rd_left(r) < 3) return false;
        int victim = rd_byte(r), dmg = rd_byte(r), weapon = rd_byte(r);
        if (pl) cs2d_sv_damage(sv, victim, c->player, dmg, weapon);
        return true;
    }
    case 19: { /* suicide / kill report: [19, weapon u8] (FUN_1401ccc90) */
        if (rd_left(r) < 1) return false;
        int weapon = rd_byte(r);
        if (pl) cs2d_sv_kill(sv, c->player, c->player, 0, weapon);
        return true;
    }
    case 23: { /* buy: [23, weapon_id u8, sub u8] (FUN_1401cd710) */
        if (rd_left(r) < 2) return false;
        int weapon_id = rd_byte(r);
        rd_byte(r); /* sub / mode byte, usually 0 */
        if (pl) cs2d_sv_buy(sv, c->player, weapon_id);
        return true;
    }
    case 24: { /* drop: [24, type u8, clip u16, reserve u16, sub u8] (FUN_1401cda30) */
        if (rd_left(r) < 6) return false;
        int type = rd_byte(r);
        int clip = rd_short(r);
        int res  = rd_short(r);
        int sub  = rd_byte(r);
        if (pl) cs2d_sv_drop(sv, c->player, type, clip, res, sub);
        return true;
    }
    case 16: { /* reload: [16, mode u8] (FUN_1401cc750) */
        if (rd_left(r) < 1) return false;
        int mode = rd_byte(r);
        if (pl) cs2d_sv_reload(sv, c->player, mode);
        return true;
    }
    case 26: { /* use: [26, mode u8, data i32, x u16, y u16] (FUN_1401cde00) */
        if (rd_left(r) < 9) return false;
        int mode = rd_byte(r);
        int32_t data = (int32_t)rd_int(r);
        int x = rd_short(r), y = rd_short(r);
        if (pl) cs2d_sv_use(sv, c->player, mode, data, (uint16_t)x, (uint16_t)y);
        return true;
    }
    case 27: { /* projectile: [27, type u8, x u16, y u16, dir u16, flydist u16] (FUN_1401ce010) */
        if (rd_left(r) < 9) return false;
        int type = rd_byte(r);
        int x = rd_short(r), y = rd_short(r);
        int dir = rd_short(r), flydist = rd_short(r);
        if (pl) cs2d_sv_projectile(sv, c->player, type, (uint16_t)x, (uint16_t)y, (uint16_t)dir, (uint16_t)flydist);
        return true;
    }
    case 28: { /* spray / logo: [28, sub u8, ...] (FUN_1401bcd60) */
        if (rd_left(r) < 1) return false;
        int sub = rd_byte(r);
        if (sub == 0) {
            /* in-game spray: [28, 0, x u16, y u16, mode u8] (FUN_1401bcd60 -> FUN_14015a890) */
            if (rd_left(r) < 5) return false;
            int sx = rd_short(r), sy = rd_short(r);
            int smode = rd_byte(r);
            if (pl) {
                cs2d_sv_lua_hook_spray(sv, c->player);
                /* Relay [28, player, 0, x, y, mode] to other joined players */
                for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
                    cs2d_sv_conn_t *oc = conn_of_player(sv, i);
                    if (!oc || !sv->players[i].joined || i == c->player) continue;
                    cs2d_pkt_t *p28 = sv_packet(sv, oc, true, false);
                    cs2d_pkt_w_byte(p28, 28);
                    cs2d_pkt_w_byte(p28, (uint8_t)c->player);
                    cs2d_pkt_w_byte(p28, 0);
                    cs2d_pkt_w_short(p28, (uint16_t)sx);
                    cs2d_pkt_w_short(p28, (uint16_t)sy);
                    cs2d_pkt_w_byte(p28, (uint8_t)smode);
                }
            }
        } else if (sub == 2) {
            /* logo trigger / transfer request: [28, 2] (FUN_1401bcd60: 0x1401bcf57 calls FUN_140048950) */
        } else {
            return false;
        }
        return true;
    }
    case 30: { /* process/bomb: [30, mode u8] (FUN_1401ce7c0) */
        if (rd_left(r) < 1) return false;
        int mode = rd_byte(r);
        if (pl) {
            if (mode == 1) cs2d_sv_plant_start(sv, c->player);
            else if (mode == 2) cs2d_sv_plant_abort(sv, c->player);
            else if (mode == 4) cs2d_sv_defuse_abort(sv, c->player);
        }
        return true;
    }
    case 31: { /* entity event: [31, x u16, y u16, mode u8] (FUN_1401cead0 -> FUN_14016e0a0) */
        if (rd_left(r) < 5) return false;
        int ex = rd_short(r), ey = rd_short(r);
        int emode = rd_byte(r);
        if (pl) cs2d_sv_entity_event(sv, c->player, (uint16_t)ex, (uint16_t)ey, (uint8_t)emode);
        return true;
    }
    case 41: { /* kill projectile: [41, proj_id u16] (FUN_1401cfd00) */
        if (rd_left(r) < 2) return false;
        int proj_id = rd_short(r);
        if (pl) cs2d_sv_kill_projectile(sv, c->player, proj_id);
        return true;
    }
    case 50: { /* portalfire: [50, color u8, tx i32, ty i32] (FUN_1401d11a0 -> FUN_140177230) */
        if (rd_left(r) < 9) return false;
        int color = rd_byte(r);
        int32_t tx = (int32_t)rd_int(r);
        int32_t ty = (int32_t)rd_int(r);
        if (pl) cs2d_sv_portal_fire(sv, c->player, (uint8_t)color, tx, ty);
        return true;
    }
    case 53: { /* clientdata response: [53, mode u8, ...] (FUN_1401d16f0) */
        if (rd_left(r) < 1) return false;
        int mode = rd_byte(r);
        int d1 = 0, d2 = 0;
        char sdata_buf[256];
        char *sdata = NULL;
        if (mode == 0) {
            if (rd_left(r) < 4) return false;
            d1 = rd_short(r);
            d2 = rd_short(r);
        } else if (mode == 1 || mode == 2) {
            if (rd_left(r) < 4) return false;
            d1 = rd_short(r) - 0x8000;
            d2 = rd_short(r) - 0x8000;
        } else if (mode == 3 || mode == 6) {
            if (rd_left(r) < 1) return false;
            d1 = rd_byte(r);
        } else if (mode == 4) {
            if (rd_left(r) < 1) return false;
            d1 = rd_byte(r);
            rd_str(r, sdata_buf, sizeof(sdata_buf));
            sdata = sdata_buf;
        } else if (mode == 5) {
            if (rd_left(r) < 1) return false;
            int sub = rd_byte(r);
            if (sub == 1) {
                if (rd_left(r) < 1) return false;
                d1 = rd_byte(r);
            } else {
                if (rd_left(r) < 2) return false;
                d1 = rd_short(r);
            }
            rd_str(r, sdata_buf, sizeof(sdata_buf));
            sdata = sdata_buf;
        }
        if (pl) cs2d_sv_lua_hook_clientdata(sv, c->player, mode, d1, d2, sdata);
        return true;
    }
    case 56: { /* weapon impulse: [56, value u8] (FUN_1401d26a0 -> FUN_1401694e0) */
        if (rd_left(r) < 1) return false;
        int val = rd_byte(r);
        if (pl) cs2d_sv_weapon_impulse(sv, c->player, (uint8_t)val);
        return true;
    }
    case 57: { /* flashlight: [57, state u8] (FUN_1401d28b0) */
        if (rd_left(r) < 1) return false;
        int state = rd_byte(r);
        if (pl) cs2d_sv_flashlight(sv, c->player, state);
        return true;
    }
    case 60: { /* specflash: [60, state u8] (FUN_1401d2d40 -> FUN_1401b2bf0) */
        if (rd_left(r) < 1) return false;
        int state = rd_byte(r);
        if (pl) cs2d_sv_spec_flash(sv, c->player, (uint8_t)state);
        return true;
    }
    case 68: { /* firedirection: [68, dir u16] (FUN_1401d3840 -> FUN_140192080) */
        if (rd_left(r) < 2) return false;
        int dir = rd_short(r);
        if (pl) cs2d_sv_fire_direction(sv, c->player, (uint16_t)dir);
        return true;
    }
    case 80: { /* player physics rotation: [80, flags u8, ...] (FUN_1401d4910) */
        if (rd_left(r) < 1) return false;
        uint8_t flags = (uint8_t)rd_byte(r);
        if (flags & 0x40) {
            if (rd_left(r) < 4) return false;
            int16_t u1 = (int16_t)rd_short(r);
            int16_t u2 = (int16_t)rd_short(r);
            (void)u2;
            if (u1 < 0) {
                if (rd_left(r) < 1) return false;
                rd_byte(r);
            }
        }
        if (flags & 0x80) {
            if (rd_left(r) < 2) return false;
            uint16_t dir = (uint16_t)rd_short(r);
            if (pl) pl->rot = (float)dir * 0.005493164f;
        }
        return true;
    }
    case 81:
    case 82:
    case 83:
    case 84:
    case 85:
    case 86:
    case 87: {
        /* Physics client update in dedicated returns 4 (terminate datagram, FUN_1401e3cf0) */
        return false;
    }
    case 225: { /* voice chat: [225, target u8, sub u8, ...] (FUN_1401d68b0) */
        if (rd_left(r) < 2) return false;
        int target = rd_byte(r);
        int sub = rd_byte(r);
        if (sub == 0) {
            if (rd_left(r) < 2) return false;
            (void)rd_short(r);
            if (pl) cs2d_sv_lua_hook_voice(sv, c->player);
        } else if (sub == 1) {
            if (rd_left(r) < 2) return false;
            int vlen = rd_short(r);
            if (vlen < 0 || vlen > 1024 || rd_left(r) < vlen) return false;
            uint8_t vdata[1024];
            for (int i = 0; i < vlen; i++) vdata[i] = (uint8_t)rd_byte(r);
            if (sv->set.sv_voicechat != 0) {
                for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
                    if (i == c->player || !sv->players[i].used || !sv->players[i].joined) continue;
                    if (target != 0 && target != i) continue;
                    if (sv->set.sv_voicechat == 2 && sv->players[i].team != pl->team) continue;
                    cs2d_sv_conn_t *oc = cs2d_sv_conn_of_player(sv, i);
                    if (oc) {
                        cs2d_pkt_t *p = sv_packet(sv, oc, false, false);
                        cs2d_pkt_w_byte(p, 225);
                        cs2d_pkt_w_byte(p, (uint8_t)c->player);
                        cs2d_pkt_w_byte(p, 1);
                        cs2d_pkt_w_short(p, (uint16_t)vlen);
                        for (int vi = 0; vi < vlen; vi++) cs2d_pkt_w_byte(p, vdata[vi]);
                    }
                }
            }
        } else if (sub == 2) {
            if (pl) cs2d_sv_lua_hook_voice(sv, c->player);
        }
        return true;
    }
    case 227:
    case 228: { /* key press (227) / release (228): [op, key u16] (FUN_1401bb510, FUN_1401bb620) */
        if (rd_left(r) < 2) return false;
        int key = rd_short(r);
        char kbuf[32];
        const char *kname = sv_resolve_key_name(sv, key, kbuf, sizeof(kbuf));
        int state = (op == 227) ? 1 : 0;
        if (pl) cs2d_sv_lua_hook_key(sv, c->player, kname, state);
        return true;
    }
    case 232: { /* clientsetting: [232, sub u8, ...] (FUN_1401bb730) */
        if (rd_left(r) < 1) return false;
        int sub = rd_byte(r);
        if (sub == 1) {
            if (rd_left(r) < 4) return false;
            int w = rd_short(r);
            int h = rd_short(r);
            if (pl) {
                pl->screen_w = w;
                pl->screen_h = h;
                cs2d_sv_lua_hook_clientsetting(sv, c->player, 1, w, h);
            }
        } else if (sub == 2) {
            if (rd_left(r) < 1) return false;
            int s43 = rd_byte(r);
            if (pl) {
                pl->screen_aspect = (uint8_t)s43;
                cs2d_sv_lua_hook_clientsetting(sv, c->player, 2, s43, 0);
            }
        } else if (sub == 3) {
            if (rd_left(r) < 1) return false;
            int win = rd_byte(r);
            if (pl) {
                pl->screen_windowed = (uint8_t)win;
                cs2d_sv_lua_hook_clientsetting(sv, c->player, 3, win, 0);
            }
        }
        return true;
    }
    case 235: { /* luamenu reply: [235, pid u8, slen u16, slen*2 bytes title UTF-16LE, button u8] */
        if (rd_left(r) < 4) return false;
        int pid = rd_byte(r);
        (void)pid;
        int slen = rd_short(r);
        if (slen < 0 || slen > 400 || rd_left(r) < slen * 2 + 1) return false;
        char title[400];
        int ti = 0;
        for (int i = 0; i < slen; i++) {
            uint16_t ch = (uint16_t)rd_short(r);
            if (ti < (int)sizeof(title) - 1) title[ti++] = (char)(ch < 128 ? ch : '?');
        }
        title[ti] = '\0';
        int button = rd_byte(r);
        if (pl) cs2d_sv_lua_hook_menu(sv, c->player, title, button);
        return true;
    }
    case 240: { /* chat: [240, mode u8, len u16, len * 2 bytes UTF-16LE] (FUN_1401bd120) */
        if (rd_left(r) < 3) return false;
        int mode = rd_byte(r);
        int char_len = rd_short(r);
        if (char_len < 0 || char_len > 255 || rd_left(r) < char_len * 2) return false;
        uint16_t chars[256];
        for (int i = 0; i < char_len; i++) chars[i] = (uint16_t)rd_short(r);
        if (mode == 3) {
            int radio_msg = (char_len > 0 && chars[0] >= '0' && chars[0] <= '9') ? (chars[0] - '0') : 1;
            if (pl) cs2d_sv_lua_hook_radio(sv, c->player, radio_msg);
        }
        if (mode == 4) {
            int action = (char_len > 0 && chars[0] >= '1' && chars[0] <= '9') ? (chars[0] - '0') : 1;
            if (pl) cs2d_sv_lua_hook_serveraction(sv, c->player, action);
            return true;
        }
        if (pl) cs2d_sv_chat(sv, c->player, mode, char_len, chars);
        return true;
    }
    case 67: { /* spectated player: [67, target_pid u8] (FUN_1401d3730) */
        if (rd_left(r) < 1) return false;
        int target = rd_byte(r);
        if (pl) cs2d_sv_spec_target(sv, c->player, target);
        return true;
    }
    case 32: { /* spectator camera: [32, sx u16, sy u16] (FUN_1401ced00) */
        if (rd_left(r) < 4) return false;
        int sx = rd_short(r), sy = rd_short(r);
        if (pl) cs2d_sv_spec_pos(sv, c->player, sx, sy);
        return true;
    }
    case 238: { /* changename: [238, name (rstr)] (FUN_1401d9450 -> FUN_14010ddb0) */
        if (rd_left(r) < 1) return false;
        char new_name[256];
        rd_str(r, new_name, sizeof(new_name));
        if (pl) cs2d_sv_change_name(sv, c->player, new_name, false, false);
        return true;
    }
    case 236: { /* rconcheck: [236, enc_pw (rstr)] (FUN_1401d8d90) */
        if (rd_left(r) < 1) return false;
        char enc_pw[256];
        int pw_len = rd_str(r, enc_pw, sizeof(enc_pw));
        if (pl) cs2d_sv_rcon_check(sv, c->player, (const uint8_t *)enc_pw, pw_len);
        return true;
    }
    case 241: { /* rcon: [241, enc_pw (rstr), enc_cmd (rlstr)] (FUN_1401d9990) */
        if (rd_left(r) < 3) return false;
        char enc_pw[256], enc_cmd[1024];
        int pw_len = rd_str(r, enc_pw, sizeof(enc_pw));
        int cmd_len = rd_lstr(r, enc_cmd, sizeof(enc_cmd));
        if (pl) cs2d_sv_rcon(sv, c->player, (const uint8_t *)enc_pw, pw_len,
                             (const uint8_t *)enc_cmd, cmd_len);
        return true;
    }
    case 34: { /* build: [34, typ u8, tx u16, ty u16, mode u8, rot + 1000 u16] (FUN_1401cf0c0 ->
                * FUN_140175550). The original rate-limits the opcode to 20 requests per 3000 ms
                * (FUN_1400d4f80(player, ip, 0x22, 0x14, 0xbb8) at 0x1401cf2b9) and then calls the place
                * function with (player, 0, typ, tx, ty, mode, rot, 0, 0, 1). */
        if (rd_left(r) < 8) return false;
        int typ = rd_byte(r);
        int bx = rd_short(r), by = rd_short(r);
        int mode = rd_byte(r);
        int rot = rd_short(r) - 1000;
        if (pl) {
            cs2d_sv_player_t *me = &sv->players[c->player];
            uint32_t now = sv_now();
            if (me->build_ctr == 0 || now - me->build_window_ms > 3000) {
                me->build_ctr = 0;
                me->build_window_ms = now;
            }
            if (++me->build_ctr <= 20) {
                cs2d_sv_build(sv, c->player, typ, bx, by, mode, (float)rot);
            }
        }
        return true;
    }
    case 43: { /* vote: [43, type u8, value u16] (FUN_1401d0050 -> FUN_1401527e0 / FUN_14014ee60) */
        if (rd_left(r) < 3) return false;
        int type = rd_byte(r);
        int val  = rd_short(r);
        if (pl) {
            char val_str[16];
            snprintf(val_str, sizeof(val_str), "%d", val);
            if (type == 1) {
                pl->votekick = val;
                cs2d_sv_lua_hook_vote(sv, c->player, 1, val_str);
            } else if (type == 2) {
                pl->votemap = val;
                cs2d_sv_lua_hook_vote(sv, c->player, 2, val_str);
            }
            /* mp_kickpercent / mp_mapvoteratio (help.php): the whole player base is counted */
            int total = 0;
            for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) if (sv->players[i].used) total++;
            if (type == 1 && sv->set.mp_kickpercent > 0.0 &&
                val >= 1 && val <= CS2D_SV_MAX_PLAYERS && sv->players[val].used) {
                int votes = 0;
                for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
                    if (sv->players[i].used && sv->players[i].votekick == val) votes++;
                }
                int need = (int)(sv->set.mp_kickpercent * total + 0.999999);
                if (need < 1) need = 1;
                if (votes >= need) {
                    cs2d_sv_tempban_ip(sv, sv->players[val].ip, sv->set.mp_tempbantime, "Vote kicked");
                    cs2d_sv_kick(sv, val, "Vote kicked");
                }
            } else if (type == 2 && sv->set.mp_mapvoteratio > 0.0 &&
                       val >= 0 && val < sv->map_count) {
                int votes = 0;
                for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
                    if (sv->players[i].used && sv->players[i].votemap == val) votes++;
                }
                int need = (int)(sv->set.mp_mapvoteratio * total + 0.999999);
                if (need < 1) need = 1;
                if (votes >= need) cs2d_sv_change_map(sv, sv->maps[val]);
            }
            /* Relay [43, voter, type, value] to all joined players */
            for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
                cs2d_sv_conn_t *oc = conn_of_player(sv, i);
                if (!oc || !sv->players[i].joined) continue;
                cs2d_pkt_t *pv = sv_packet(sv, oc, true, false);
                cs2d_pkt_w_byte(pv, 43);
                cs2d_pkt_w_byte(pv, (uint8_t)c->player);
                cs2d_pkt_w_byte(pv, (uint8_t)type);
                cs2d_pkt_w_short(pv, (uint16_t)val);
            }
        }
        return true;
    }
    case 230: { /* leaderboard: [230, sub u8, ...] (FUN_1401d8360) */
        if (rd_left(r) < 1) return false;
        int sub = rd_byte(r);
        if (sub == 0) {
            /* request timestamp: sends reliable [230, 0, timestamp i32] */
            cs2d_pkt_t *p = sv_packet(sv, c, true, false);
            cs2d_pkt_w_byte(p, 230);
            cs2d_pkt_w_byte(p, 0);
            cs2d_pkt_w_int(p, 0); /* 0 timestamp: leaderboard not configured / empty */
            return true;
        } else if (sub == 1) {
            /* request rank entry: [230, 1, rank u16] */
            if (rd_left(r) < 2) return false;
            int rank = rd_short(r);
            (void)rank;
            /* Returns 0 without sending if empty in original binary (FUN_1401d8360 0x1401d8650) */
            return true;
        }
        return false;
    }
    /* Server-to-Client dropped opcodes in dedicated server mode (DAT_1407dfe48 != 0):
     * Return code 3 or 4 breaks datagram parsing (FUN_1401e3cf0) cleanly dropping remainder. */
    case 15:  /* setpos / teleport (FUN_1401cc570) */
    case 21:  /* spawn record (FUN_1401cd470) */
    case 22:  /* round restart (inline 0x1401e4929 -> 0x1401e4ce0) */
    case 25:  /* ground item collect (inline 0x1401e49da -> 0x1401e4ce0) */
    case 29:  /* hostage update (FUN_1401ce2f0) */
    case 33:  /* ammo sync (inline 0x1401e48b6 -> 0x1401e4ce0) */
    case 35:  /* dynamic object remove (FUN_1401cf350) */
    case 36:  /* dynamic object mode (FUN_1401cf4b0) */
    case 37:  /* dynamic object health (FUN_1401cf630) */
    case 38:  /* sound effect (inline 0x1401e4820 -> 0x1401e4ce0) */
    case 40:  /* kill dropped weapon (FUN_1401cfba0) */
    case 42:  /* set playerdata (FUN_1401cfe80) */
    case 44:  /* radar blip / HUD (FUN_1401d03f0) */
    case 45:  /* HUD elements (FUN_1401d06b0) */
    case 46:  /* custom HUD / menus (FUN_1401d0970) */
    case 47:  /* custom HUD (FUN_1401d0ca0) */
    case 48:  /* custom sound (inline 0x1401e4769 -> 0x1401e4ce0) */
    case 49:  /* screenshake (inline 0x1401e4708 -> 0x1401e4ce0) */
    case 51:  /* dynamic image spawn (inline 0x1401e46b1 -> 0x1401e4ce0) */
    case 52:  /* dynamic image update (inline 0x1401e4652 -> 0x1401e4ce0) */
    case 55:  /* fog of war update (FUN_1401d2520) */
    case 58:  /* blend tile (inline 0x1401e456d -> 0x1401e4ce0) */
    case 59:  /* image hitzone (inline 0x1401e450e -> 0x1401e4ce0) */
    case 61:  /* custom tile (inline 0x1401e44a3 -> 0x1401e4ce0) */
    case 62:  /* settile (inline 0x1401e4444 -> 0x1401e4ce0) */
    case 63:  /* custom item (inline 0x1401e43e5 -> 0x1401e4ce0) */
    case 64:  /* team scores broadcast (FUN_1401d3280) */
    case 65:  /* positionalsound (FUN_1401d3430) */
    case 66:  /* hostage pos (inline 0x1401e436f -> 0x1401e4ce0) */
    case 69:  /* death cam (inline 0x1401e42fb -> 0x1401e4ce4) */
    case 224: /* kill streak announcement (FUN_1401d6730) */
    case 226: /* round end (inline 0x1401e4083 -> 0x1401e4ce0) */
    case 234: /* hud text (inline 0x1401e4258 -> 0x1401e4ce0) */
    case 237: /* cvar change (inline 0x1401e41af -> 0x1401e4ce0) */
    case 239: /* map reload (FUN_1401d9720) */
    case 247: /* ping list (inline 0x1401e3fdf -> 0x1401e4ce0) */
    case 248: /* join notice (inline 0x1401e3f96 -> 0x1401e4ce0) */
        return false;

    case 54: { /* dynamic tween update: [54, mode u8, target u16, ...] (FUN_1400bf930) */
        if (rd_left(r) < 3) return false;
        int tmode = rd_byte(r);
        int target = rd_short(r);
        (void)tmode; (void)target;
        return true;
    }
    case 100: { /* command relay / custom event: [100, sub u8, ...] (FUN_1401c3500) */
        if (rd_left(r) < 1) return false;
        int sub = rd_byte(r);
        if (sub == 2) {
            char cmd_buf[256];
            rd_str(r, cmd_buf, sizeof(cmd_buf));
            if (rd_left(r) >= 1) rd_byte(r);
            return true;
        }
        return false;
    }
    case 243: { /* client log / debug string: [243, len u16, len*2 bytes UTF-16LE] (FUN_1401be4a0) */
        if (rd_left(r) < 2) return false;
        int slen = rd_short(r);
        if (slen < 0 || slen > 1024 || rd_left(r) < slen * 2) return false;
        for (int i = 0; i < slen; i++) (void)rd_short(r);
        return true;
    }
    case 250: { /* Ping: [250, millisecs i32] (FUN_1400d4ed0) */
        if (rd_left(r) < 4) return false;
        int32_t ms = rd_int(r);
        cs2d_pkt_t *p = sv_packet(sv, c, false, false);
        cs2d_pkt_w_byte(p, 250);
        cs2d_pkt_w_int(p, ms);
        return true;
    }
    case 251: { /* Info query: [251, mode u8, [version u16]] (FUN_1401da400) */
        if (rd_left(r) < 1) return false;
        uint8_t mode = (uint8_t)rd_byte(r);
        uint16_t req_ver = 0;
        if (rd_left(r) >= 2) req_ver = (uint16_t)rd_short(r);
        uint8_t rep[1024];
        size_t rep_len = cs2d_sv_build_info_reply(sv, mode, req_ver, rep, sizeof(rep));
        if (rep_len > 2) {
            cs2d_pkt_t *p = sv_packet(sv, c, false, false);
            for (size_t k = 2; k < rep_len; k++) {
                cs2d_pkt_w_byte(p, rep[k]);
            }
        }
        return true;
    }
    default:
        /* Jump table default entries (0x1401e3e80) in cs2d_dedicated.exe cleanly drop
         * the remainder of the datagram (esi = 2; break;) without logging errors. */
        return false;
    }
}

/* The messages of one datagram after its header and control triples */
static void sv_recv_messages(cs2d_server_t *sv, cs2d_sv_conn_t *c, sv_rd_t *r)
{
    while (r->p < r->n) {
        int op = rd_byte(r);
        if (op == 252) {
            sv_recv_join(sv, c, r);
        } else if (op < 7) {
            /* FUN_1401e3cf0: opcodes < 7 are control/handshake or padding, not game opcodes (jump table starts at 7).
             * Drop the rest of the datagram silently matching original binary behaviour. */
            break;
        } else if (!sv_recv_game(sv, c, r, op)) {
            break;
        }
    }
}

/* Test entry: feeds a payload (no Tudp header) to the message parser; returns the bytes left unread */
int cs2d_sv_recv_payload(cs2d_server_t *sv, cs2d_sv_conn_t *c, const uint8_t *buf, int n)
{
    sv_rd_t r = { buf, n, 0 };
    sv_recv_messages(sv, c, &r);
    return n - r.p;
}

void cs2d_sv_handle_datagram(cs2d_server_t *sv, const struct sockaddr_in *from, const uint8_t *buf, int n)
{
    if (n < 2) return;
    if (from) {
        char ip[32];
        snprintf(ip, sizeof(ip), "%s", inet_ntoa(from->sin_addr));
        if (sv_ip_ignored(sv, ip)) return;
    }
    uint16_t seq;
    memcpy(&seq, buf, 2);
    sv_rd_t r = { buf, n, 2 };
    /* Only a [252,0] opens a connection; [242] public RCON is connectionless (FUN_1401be1c0) */
    bool hello = false;
    {
        int q = 2;
        while (q + 3 <= n && buf[q] >= 1 && buf[q] <= 4) q += 3;
        if (q < n && buf[q] == 242) {
            r.p = q + 1;
            char pw[256], cmd[1024];
            rd_str(&r, pw, sizeof(pw));
            rd_lstr(&r, cmd, sizeof(cmd));
            cs2d_sv_public_rcon(sv, from, pw, cmd);
            return;
        }
        if (q < n && buf[q] == 0xFA) {
            cs2d_sv_handle_ping(sv, from, buf, n, q);
            return;
        }
        if (q < n && buf[q] == 0xFB) {
            cs2d_sv_handle_info(sv, from, buf, n, q);
            return;
        }
        hello = (q + 1 < n && buf[q] == 252 && buf[q + 1] == 0);
    }
    cs2d_sv_conn_t *c = sv_find_conn(sv, from, hello);
    if (!c) return;
    c->last_recv = sv_now();
    c->bytes_in += (uint32_t)n;

    /* Control triples: 1 = ACK of our reliable id; 2..4 carry connection data (3 = 0x32 in the first
     * packet of the original client, not identified) */
    while (r.p + 3 <= n && buf[r.p] >= 1 && buf[r.p] <= 4) {
        uint8_t kind = buf[r.p];
        uint16_t v;
        memcpy(&v, buf + r.p + 1, 2);
        r.p += 3;
        if (kind == 1) {
            for (int i = 0; i < CS2D_SV_MAX_PENDING; i++) {
                if (c->pending[i].len && c->pending[i].seq == v) c->pending[i].len = 0;
            }
        }
    }
    if ((seq & 1) == 0 && seq != 0) {
        sv_ack(sv, c, seq);
        uint16_t ahead = (uint16_t)(seq + 32768);
        c->seen[ahead >> 3] &= (uint8_t)~(1u << (ahead & 7));
        if (c->seen[seq >> 3] & (1u << (seq & 7))) return;
        c->seen[seq >> 3] |= (uint8_t)(1u << (seq & 7));
    }
    sv_recv_messages(sv, c, &r);
}

/* ------------------------------------------------------------------------------------------------ */
/* Periodic messages (0x1401e505d, once per second: DAT_1407dff88)                                  */
/* ------------------------------------------------------------------------------------------------ */

/* [247] ping list (0x1401e505d): [247, n u8, ticks u8, sent/8 u16, received/8 u16, n x {id u8, ping u16}]
 * over every player in a team (Tpl +0x230 > 0). `ticks` is DAT_1407dfe28, the two u16 are
 * DAT_1407dfe34 / DAT_1407dfe30 / 8. The ping is 0xfffe for a bot (Tpl +0x198), else
 * min(Tpl +0xd0, 30000); the client writes it into Tpl.ping and draws the scoreboard latency
 * (Thud_scoreboard 0x14012f2c0). Returns the bytes written, or 0 when the buffer is too small. */
size_t cs2d_sv_build_pinglist(const cs2d_server_t *sv, uint8_t *out, size_t max_out)
{
    if (!sv || !out) return 0;
    int cnt = 0;
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        if (sv->players[i].used && sv->players[i].team > 0) cnt++;
    }
    size_t need = 7 + (size_t)cnt * 3;
    if (max_out < need) return 0;

    size_t k = 0;
    out[k++] = 247;
    out[k++] = (uint8_t)cnt;
    out[k++] = (uint8_t)sv->fps;                                  /* DAT_1407dfe28 */
    uint16_t bsent = (uint16_t)(sv->bytes_sent_sec / 8);          /* DAT_1407dfe34 / 8 */
    uint16_t brecv = (uint16_t)(sv->bytes_recv_sec / 8);          /* DAT_1407dfe30 / 8 */
    memcpy(out + k, &bsent, 2);
    k += 2;
    memcpy(out + k, &brecv, 2);
    k += 2;
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        const cs2d_sv_player_t *o = &sv->players[i];
        if (!o->used || o->team <= 0) continue;
        uint16_t ping = o->bot ? 0xfffe : (uint16_t)(o->ping > 30000 ? 30000 : o->ping);
        out[k++] = (uint8_t)i;
        memcpy(out + k, &ping, 2);
        k += 2;
    }
    return k;
}

static void sv_second(cs2d_server_t *sv)
{
    uint32_t now = sv_now();
    cs2d_sv_ai_second(sv);
    sv->frames = 0;
    sv->bytes_recv_sec = sv->bytes_in_acc;
    sv->bytes_sent_sec = sv->bytes_out_acc;
    sv->bytes_in_acc = sv->bytes_out_acc = 0;

    /* The per-player seconds counter at Tpl +0x3fc (capped at 499) drives the mp_dmspawnmoney limit */
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        if (!sv->players[i].used) continue;
        if (sv->players[i].money_secs < 499) sv->players[i].money_secs++;
    }

    /* mp_idlekick / mp_idletime / mp_idleaction and mp_pinglimit (help.php) */
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        cs2d_sv_player_t *pl = &sv->players[i];
        if (!pl->used) continue;

        if (sv->set.mp_idlekick && !pl->bot) {
            int limit = sv->set.mp_idletime;
            if (limit < 10) limit = 10;
            if (limit > 3600) limit = 3600;
            if (now - pl->last_move_ms >= (uint32_t)limit * 1000u) {
                switch (sv->set.mp_idleaction) {
                case 0: cs2d_sv_kick(sv, i, "Idle"); break;
                case 1: cs2d_sv_team_ex(sv, i, 0, 5, true); break;
                case 2: cs2d_sv_slap(sv, i, false); break;
                case 3: if (pl->alive) cs2d_sv_kill(sv, i, 0, 0, 0); break;
                default: cs2d_sv_msg(sv, "A player is idle!", 0); break;
                }
                pl->last_move_ms = now;
            }
        }

        if (sv->set.mp_pinglimit > 0 && !pl->bot && pl->ping > 0 &&
            pl->ping > sv->set.mp_pinglimit) {
            cs2d_sv_kick(sv, i, "Ping too high");
        }

        /* mp_zombierecover: zombies regain HP every second (help.php) */
        if (sv->set.sv_gamemode == 4 && pl->team == 1 && pl->alive &&
            sv->set.mp_zombierecover > 0 && pl->health < pl->maxhealth) {
            int hp = pl->health + sv->set.mp_zombierecover;
            if (hp > pl->maxhealth) hp = pl->maxhealth;
            cs2d_sv_set_playerdata(sv, i, 0, hp);
        }
    }

    /* Every 2nd second: [229, ms since start] (mp_anticlock > 0) or [249, ...] to every connection.
     * mp_anticlock is the original's cvar (0x140141e51, DAT_1405a64e4); with 0 the client's
     * normal in-game ping reply [249, srv_ts] refreshes the scoreboard latency (proxy capture). */
    sv->ping_ctr++;
    sv->pinglist_ctr++;
    if (sv->ping_ctr > 1) {
        sv->ping_ctr = 0;
        for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
            cs2d_sv_conn_t *c = conn_of_player(sv, i);
            if (!c) continue;
            cs2d_pkt_t *p = sv_packet(sv, c, false, true);
            cs2d_pkt_w_byte(p, sv->set.mp_anticlock > 0 ? 229 : 249);
            cs2d_pkt_w_int(p, (int32_t)(sv_now() - sv->start_ms));
            sv_flush(sv, c);
        }
    }
    /* Every 3rd second: the [247] ping list to the joined players */
    if (sv->pinglist_ctr > 2) {
        sv->pinglist_ctr = 0;
        uint8_t list[7 + CS2D_SV_MAX_PLAYERS * 3];
        size_t len = cs2d_sv_build_pinglist(sv, list, sizeof(list));
        for (int i = 1; i <= CS2D_SV_MAX_PLAYERS && len > 0; i++) {
            cs2d_sv_conn_t *c = conn_of_player(sv, i);
            if (!c || !sv->players[i].joined) continue;
            cs2d_pkt_t *p = sv_packet(sv, c, false, false);
            for (size_t k = 0; k < len; k++) cs2d_pkt_w_byte(p, list[k]);
            sv_flush(sv, c);
        }
    }
}

/* ------------------------------------------------------------------------------------------------ */
/* Start / frame / stop                                                                             */
/* ------------------------------------------------------------------------------------------------ */

static void sv_load_serverinfo(cs2d_server_t *sv)
{
    FILE *f = fopen("sys/serverinfo.txt", "rb");
    if (!f) return;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\r' || line[n - 1] == '\n')) line[--n] = '\0';
        char (*grown)[256] = realloc(sv->info_lines, (size_t)(sv->info_count + 1) * sizeof(*grown));
        if (!grown) break;
        sv->info_lines = grown;
        snprintf(sv->info_lines[sv->info_count++], 256, "%.255s", line);
    }
    fclose(f);
}

static int map_cmp(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }

static void sv_load_maplist(cs2d_server_t *sv)
{
    sv->map_count = 0;
    sv->mapcycle_count = 0;
    /* PTR_DAT_1405a1538: the map cycle (sys/mapcycle.cfg) sorted by name - the 14 maps of the shipped cycle
     * arrive in alphabetical order in proxy_ph1.log (the sort itself is not located in the binary yet) */
    FILE *f = fopen("sys/mapcycle.cfg", "rb");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof(line), f) && sv->map_count < CS2D_SV_MAX_MAPS) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if ((p[0] == '/' && p[1] == '/') || *p == '\r' || *p == '\n' || !*p) continue;
        int k = 0;
        char m[64];
        while (*p && *p != ' ' && *p != '\t' && *p != '{' && *p != '\r' && *p != '\n' && k < 63) m[k++] = *p++;
        m[k] = '\0';
        if (k) {
            snprintf(sv->mapcycle[sv->mapcycle_count++], 64, "%s", m);
            snprintf(sv->maps[sv->map_count++], 64, "%s", m);
        }
    }
    fclose(f);
    qsort(sv->maps, (size_t)sv->map_count, sizeof(sv->maps[0]), map_cmp);
}

int cs2d_sv_building_price(const cs2d_server_t *sv, int typ)
{
    if (sv && typ >= 0 && typ < CS2D_DYNOBJ_TYPES && (sv->building_price_mask & (1ULL << typ))) {
        return sv->building_price[typ];
    }
    const cs2d_dynobj_def_t *d = cs2d_dynobj_def(typ);
    return d ? d->price : 0;
}

int cs2d_sv_building_limit(const cs2d_server_t *sv, int typ)
{
    if (sv && typ >= 0 && typ < CS2D_DYNOBJ_TYPES && (sv->building_limit_mask & (1ULL << typ))) {
        return sv->building_limit[typ];
    }
    const cs2d_dynobj_def_t *d = cs2d_dynobj_def(typ);
    return d ? d->limit : 0;
}

int cs2d_sv_building_health(const cs2d_server_t *sv, int typ)
{
    if (sv && typ >= 0 && typ < CS2D_DYNOBJ_TYPES && (sv->building_health_mask & (1ULL << typ))) {
        return sv->building_health[typ];
    }
    const cs2d_dynobj_def_t *d = cs2d_dynobj_def(typ);
    return d ? d->max_health : 100;
}

int cs2d_sv_resolve_building_type(const char *name)
{
    if (!name || !name[0]) return -1;
    char *end = NULL;
    long val = strtol(name, &end, 10);
    if (end && *end == '\0' && val > 0 && val < CS2D_DYNOBJ_TYPES) return (int)val;

    for (int i = 1; i < CS2D_DYNOBJ_TYPES; i++) {
        const cs2d_dynobj_def_t *d = cs2d_dynobj_def(i);
        if (!d || !d->name[0]) continue;
        if (_stricmp(d->name, name) == 0) return i;
        char s1[64], s2[64];
        int p1 = 0, p2 = 0;
        for (int k = 0; d->name[k] && p1 < 63; k++) {
            if (d->name[k] != ' ' && d->name[k] != '_') s1[p1++] = (char)tolower((unsigned char)d->name[k]);
        }
        s1[p1] = '\0';
        for (int k = 0; name[k] && p2 < 63; k++) {
            if (name[k] != ' ' && name[k] != '_') s2[p2++] = (char)tolower((unsigned char)name[k]);
        }
        s2[p2] = '\0';
        if (strcmp(s1, s2) == 0) return i;
    }
    return -1;
}

bool cs2d_server_start(cs2d_server_t *sv, const char *map, int port, bool offline)
{
    if (!cs2d_platform_net_init()) return false;
    sv->offline = offline;
    if (map && map[0]) {
        cs2d_sv_sanitize_map_name(sv->set.sv_map, sizeof(sv->set.sv_map), map);
    }
    if (port <= 0) port = sv->set.sv_hostport;

    sv->sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sv->sock == INVALID_SOCKET) return false;
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons((u_short)port);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sv->sock, (struct sockaddr *)&a, sizeof(a)) != 0) {
        cs2d_server_print("ERROR: failed to initialize UDP socket on port %d", port);
        return false;
    }
    cs2d_platform_set_nonblocking(sv->sock);
    cs2d_server_print("UDP socket initialized using port %d", port);

    cs2d_server_print("load map '%s'", sv->set.sv_map);
    char path[256];
    snprintf(path, sizeof(path), "maps/%s.map", sv->set.sv_map);
    sv->map_loaded = cs2d_map_load_file(&sv->map, path);
    if (!sv->map_loaded) {
        cs2d_server_print("ERROR: failed to load map '%s'", sv->set.sv_map);
        return false;
    }
    cs2d_net_sha256_file(path, sv->map_sha);
    memset(sv->entity_reuse_ms, 0, sizeof(sv->entity_reuse_ms));

    /* The join secret: 10 letters (proxy: "TuorxHPAAT"); the generator is not decompiled yet */
    srand(GetTickCount());
    for (int i = 0; i < 10; i++) {
        int k = rand() % 52;
        sv->secret[i] = (char)(k < 26 ? 'A' + k : 'a' + k - 26);
    }
    sv->secret[10] = '\0';
    sv->seed = rand() & 0x7fffffff;
    sv->start_ms = sv->round_start_ms = sv->second_ms = sv->map_start_ms = sv_now();

    sv->next_item_id = 1;
    memset(sv->items, 0, sizeof(sv->items));
    memset(sv->projectiles, 0, sizeof(sv->projectiles));
    sv->next_proj_id = 1;
    sv->last_proj_ms = sv_now();

    for (int i = 0; i < CS2D_DYNOBJ_TYPES; i++) {
        sv->building_price[i] = -1;
        sv->building_limit[i] = -1;
        sv->building_health[i] = -1;
    }

    cs2d_sv_init_hostages(sv);
    cs2d_sv_init_env_items(sv);
    sv_load_serverinfo(sv);
    sv_load_maplist(sv);
    cs2d_dynobj_init(&sv->dynobj, &sv->map);
    cs2d_dynobj_spawn_map_objects(&sv->dynobj);
    cs2d_sv_fix_building_health(sv);
    s_active_sv = sv;
    cs2d_sv_lua_init(sv);
    cs2d_server_print("----- Server started -----");
    return true;
}

/* Main loop timing of cs2d_dedicated.exe (0x14019d3a0..0x14019d94a, server only). Each loop adds the time since
 * the previous loop (ms) to a 180-entry ring (0x1405acdd0, newest first); the number of newest entries needed for
 * their sum to pass 999 ms (at most 180) goes into a second ring (0x1405acdc8); DAT_1407dfe28, the "ticks" byte of
 * [247], is the minimum of that ring. Both rings start at 0 (assumed: BlitzMax int arrays; not read at runtime). */
static void sv_frame_stats(cs2d_server_t *sv, uint32_t now)
{
    if (sv->last_frame_ms > 0) {
        memmove(sv->frame_dt + 1, sv->frame_dt, sizeof(sv->frame_dt) - sizeof(sv->frame_dt[0]));
        sv->frame_dt[0] = now - sv->last_frame_ms;
        uint32_t sum = 0;
        int cnt = 0;
        while (cnt < 180) {
            sum += sv->frame_dt[cnt++];
            if (sum > 999) break;
        }
        memmove(sv->fps_hist + 1, sv->fps_hist, sizeof(sv->fps_hist) - sizeof(sv->fps_hist[0]));
        sv->fps_hist[0] = (uint32_t)cnt;
        uint32_t mn = sv->fps_hist[0];
        for (int i = 1; i < 180; i++) if (sv->fps_hist[i] < mn) mn = sv->fps_hist[i];
        sv->fps = (int)mn;
    }
    sv->last_frame_ms = now;
}

void cs2d_server_frame(cs2d_server_t *sv)
{
    s_active_sv = sv;
    uint8_t buf[4096];
    struct sockaddr_in from;
    for (;;) {
        cs2d_socklen_t fl = sizeof(from);
        int n = recvfrom(sv->sock, (char *)buf, sizeof(buf), 0, (struct sockaddr *)&from, &fl);
        if (n < 0) break;
        sv->bytes_in_acc += (uint32_t)n; /* Tudp counts every recvfrom, also the info queries (0x1407e06d8) */
        cs2d_sv_handle_datagram(sv, &from, buf, n);
    }

    uint32_t now = sv_now();
    for (int i = 0; i < (int)(sizeof(sv->conns) / sizeof(sv->conns[0])); i++) {
        cs2d_sv_conn_t *c = &sv->conns[i];
        if (!c->active) continue;
        sv_flush(sv, c);
        for (int k = 0; k < CS2D_SV_MAX_PENDING; k++) {
            cs2d_sv_pending_t *pd = &c->pending[k];
            if (pd->len && now - pd->last_send >= SV_RESEND_MS) {
                pd->last_send = now;
                sv_sendto(sv, c, pd->data, pd->len);
            }
        }
        if (now - c->last_recv > SV_TIMEOUT_MS) {
            if (c->player) sv_remove_player(sv, c->player, 1, "");
            c->active = false;
        }
    }

    cs2d_sv_ai_frame(sv);
    cs2d_sv_relay_movement(sv);
    cs2d_sv_step_bomb(sv);
    cs2d_sv_step_hostages(sv);
    cs2d_sv_step_items(sv);
    cs2d_sv_step_trigger_delays(sv);
    cs2d_sv_step_reloads(sv);
    cs2d_sv_step_respawns(sv);
    cs2d_sv_step_buildings(sv);
    cs2d_sv_step_projectiles(sv);
    /* Ttween.update (0x14027fc50): the dedicated keeps the tween state, so object()/imageparam()
     * return the animated x/y/rot/... while Lua scripts (the C4 projectile system) poll them. */
    cs2d_dynobj_update_tweens(&sv->dynobj, now);
    cs2d_sv_lua_frame(sv);
    cs2d_sv_step_round(sv);     /* after the Lua timers: a restart queued by a script starts after its first `second` */

    sv->frames++;
    sv_frame_stats(sv, now);
    if (now - sv->second_ms >= 1000) {
        sv->second_ms = now;
        sv_second(sv);
    }
}

void cs2d_server_stop(cs2d_server_t *sv)
{
    if (s_active_sv == sv) s_active_sv = NULL;
    if (sv->lua) cs2d_sv_lua_hook_shutdown(sv);
    cs2d_sv_lua_close(sv);
    if (sv->sock != INVALID_SOCKET) closesocket(sv->sock);
    sv->sock = INVALID_SOCKET;
    for (int i = 0; i < (int)(sizeof(sv->conns) / sizeof(sv->conns[0])); i++) {
        free(sv->conns[i].pending);
        sv->conns[i].pending = NULL;
    }
    free(sv->info_lines);
    sv->info_lines = NULL;
    cs2d_map_free(&sv->map);
}
