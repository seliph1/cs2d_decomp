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
#include "cs2d_weapons.h"
#include "cs2d_sv_lua.h"

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
#define conn_of_player cs2d_sv_conn_of_player

/* ------------------------------------------------------------------------------------------------ */
/* Console                                                                                          */
/* ------------------------------------------------------------------------------------------------ */

/* FUN_1400ce590 prints a line on the console; a leading "©RRRGGGBBB" colour tag is not part of the
 * text (the dedicated's log shows the lines without it, ded_ph1.log) */
void cs2d_server_print(const char *fmt, ...)
{
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    const char *t = line;
    if ((uint8_t)t[0] == 0xa9 && strlen(t) >= 10) t += 10;
    else if ((uint8_t)t[0] == 0xc2 && (uint8_t)t[1] == 0xa9 && strlen(t) >= 11) t += 11;
    printf("%s\n", t);
    fflush(stdout);
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
    s->sv_antiaccel       = 1;   /* the capture pings with [229] */
    s->sv_daylighttime    = -1;
    s->sv_maxclientsperip = 5;   /* DAT_1405a6514 initial value */
    s->mp_startmoney      = 800; /* NOT verified (CS default); the shipped server.cfg sets 16000 */
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

static void cfg_set(cs2d_sv_settings_t *s, const char *key, const char *val)
{
    if (_stricmp(key, "sv_rcon") == 0) {
        /* 0x14014908d: sv_rcon is truncated to 50 characters */
        snprintf(s->sv_rcon, sizeof(s->sv_rcon), "%.50s", val);
        return;
    }
#define SV_INT(name) if (_stricmp(key, #name) == 0) { s->name = atoi(val); return; }
#define SV_STR(name) if (_stricmp(key, #name) == 0) { snprintf(s->name, sizeof(s->name), "%s", val); return; }
    SV_STR(sv_name) SV_STR(sv_map) SV_STR(sv_password) SV_STR(mp_supply_items) SV_STR(sv_rconusers)
    SV_INT(sv_hostport) SV_INT(sv_maxplayers) SV_INT(sv_fow) SV_INT(sv_specmode) SV_INT(sv_gamemode)
    SV_INT(sv_usgnonly) SV_INT(sv_friendlyfire) SV_INT(sv_forcelight) SV_INT(sv_daylighttime)
    SV_INT(sv_voicechat) SV_INT(sv_offscreendamage) SV_INT(sv_maxclientsperip) SV_INT(sv_antiaccel)
    SV_INT(mp_roundtime) SV_INT(mp_freezetime) SV_INT(mp_c4timer) SV_INT(mp_respawndelay) SV_INT(mp_deadchat)
    SV_INT(mp_infammo) SV_INT(mp_flashlight) SV_INT(mp_hovertext) SV_INT(mp_recoil) SV_INT(mp_hudscale)
    SV_INT(sv_spraytransfer) SV_INT(mp_startmoney) SV_INT(mp_postspawn) SV_INT(mp_autoteambalance)
    SV_INT(lagcompensation) SV_INT(ticks) SV_INT(leaderboard) SV_INT(physics)
#undef SV_INT
#undef SV_STR
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

static void sv_flush(cs2d_server_t *sv, cs2d_sv_conn_t *c)
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

static void sv_conn_reset(cs2d_sv_conn_t *c)
{
    cs2d_sv_pending_t *keep = c->pending;
    memset(c, 0, sizeof(*c));
    c->pending = keep;
    if (c->pending) memset(c->pending, 0, sizeof(cs2d_sv_pending_t) * CS2D_SV_MAX_PENDING);
    c->next_seq = 2;
}

static cs2d_sv_conn_t *sv_find_conn(cs2d_server_t *sv, const struct sockaddr_in *a, bool create)
{
    int free_slot = -1;
    for (int i = 0; i < (int)(sizeof(sv->conns) / sizeof(sv->conns[0])); i++) {
        cs2d_sv_conn_t *c = &sv->conns[i];
        if (c->active) {
            if (c->addr.sin_addr.s_addr == a->sin_addr.s_addr && c->addr.sin_port == a->sin_port) return c;
        } else if (free_slot < 0) {
            free_slot = i;
        }
    }
    if (!create || free_slot < 0) return NULL;
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

/* FUN_140098050(1, -1): the lowest free player id (estimated: allocation order not verified) */
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
                                        int32_t seed);

void cs2d_sv_build_join_settings(cs2d_pkt_t *p, const cs2d_sv_settings_t *s, const cs2d_map_t *m, int score_t,
                                 int score_ct, int32_t round_ms, int32_t seed)
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
    sv_write_join_settings_tail(p, s, m, seed);
}

/* The map part of [252,6]: background image, its scroll, the header int, then the rest (FUN_1401dc530) */
static void sv_write_join_settings_tail(cs2d_pkt_t *p, const cs2d_sv_settings_t *s, const cs2d_map_t *m,
                                        int32_t seed)
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
    cs2d_pkt_w_byte(p, 0);                              /* 0 < DAT_1407dfcac: Lua scripts loaded */
    cs2d_pkt_w_byte(p, (uint8_t)s->ticks);              /* DAT_1405a64c8 */
    cs2d_pkt_w_byte(p, 0);                              /* Lua bind count (DAT_1407df810), then n x u16 */
    cs2d_pkt_w_int(p, seed);                            /* DAT_1407df3d4 */
    cs2d_pkt_w_byte(p, (uint8_t)s->sv_offscreendamage);
    cs2d_pkt_w_byte(p, (uint8_t)(s->leaderboard != 0)); /* && DAT_1405a63e8 */
    cs2d_pkt_w_byte(p, (uint8_t)s->physics);
    /* FUN_1401dba60: 3 zero bytes with the default config (not decompiled yet) */
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
    cs2d_pkt_w_byte(p, 0);                              /* +0x3dc votekick */
    cs2d_pkt_w_short(p, 0xffff);                        /* +0x3e0 votemap: -1 = no vote (proxy) */
}

static void sv_send_join_snapshot(cs2d_server_t *sv, cs2d_sv_conn_t *c)
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

    /* Sections 4..6 terminators (entity states, dynamic objects, ground projectiles) */
    for (int sec = 4; sec <= 6; sec++) {
        p = sv_packet(sv, c, true, true);
        cs2d_pkt_w_byte(p, 252); cs2d_pkt_w_byte(p, 7); cs2d_pkt_w_byte(p, (uint8_t)sec); cs2d_pkt_w_byte(p, 0);
    }

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

    /* Sections 7, 8, 9: image data, tweens, custom tiles (empty until Lua images / settile are ported) */
    for (int sec = 7; sec <= 9; sec++) {
        p = sv_packet(sv, c, true, true);
        cs2d_pkt_w_byte(p, 252); cs2d_pkt_w_byte(p, 7); cs2d_pkt_w_byte(p, (uint8_t)sec); cs2d_pkt_w_byte(p, 0);
    }

    /* Section 11: weapon settings, wbank of PTR_DAT_14059d3a8 = [u16 size][u32 raw size][zlib]. The raw
     * table (170 bytes) is the one the shipped sys/weapons.cfg produced in proxy_ph1.log; building it from
     * the weapon tables is not ported yet */
    {
        static const uint8_t raw[] = {
            0x01,0x01,0x7c,0x03,0x09,0x02,0x01,0x7c,0x03,0x09,0x03,0x01,0x7c,0x06,0x0d,0x04,0x01,0x7c,0x04,0x09,
            0x05,0x01,0x7c,0x04,0x07,0x06,0x01,0x7c,0x04,0x05,0x0a,0x04,0x7c,0x0f,0x19,0x0b,0x04,0x7c,0x0a,0x07,
            0x14,0x01,0x7c,0x00,0x03,0x15,0x02,0x7c,0x00,0x02,0x16,0x00,0x7c,0x00,0x02,0x17,0x01,0x7c,0x00,0x02,
            0x18,0x00,0x7c,0x00,0x04,0x1e,0x00,0x7c,0x01,0x03,0x1f,0x00,0x7c,0x01,0x04,0x20,0x00,0x7c,0x01,0x03,
            0x21,0x00,0x7c,0x01,0x04,0x22,0x00,0x7c,0x01,0x19,0x23,0x00,0x7c,0x01,0x41,0x24,0x00,0x7c,0x01,0x06,
            0x25,0x00,0x7c,0x01,0x06,0x26,0x00,0x7c,0x01,0x03,0x27,0x00,0x7c,0x01,0x03,0x28,0x03,0x7c,0x01,0x02,
            0x29,0x00,0x7c,0x00,0x02,0x2d,0x00,0x7c,0x01,0x1e,0x2e,0x00,0x7c,0x01,0x03,0x2f,0x00,0x7c,0x01,0x32,
            0x30,0x00,0x7c,0x01,0x14,0x31,0x00,0x7c,0x01,0x14,0x58,0x00,0x7c,0x00,0x05,0x5a,0x04,0x7c,0x01,0x02,
            0x5b,0x04,0x7c,0x01,0x03,0x5c,0x00,0x00,0x00,0x02
        };
        uint8_t z[512];
        uLongf zn = sizeof(z);
        compress2(z, &zn, raw, sizeof(raw), Z_DEFAULT_COMPRESSION);
        p = sv_packet(sv, c, true, true);
        cs2d_pkt_w_byte(p, 252); cs2d_pkt_w_byte(p, 7); cs2d_pkt_w_byte(p, 11);
        cs2d_pkt_w_short(p, (uint16_t)(zn + 4));
        cs2d_pkt_w_int(p, (int32_t)sizeof(raw));
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

    /* sv_spraytransfer: [28,0,1] appended, then FUN_14002e9e0 per other connected player (their sprays,
     * not ported yet) */
    if (sv->set.sv_spraytransfer == 1) {
        p = sv_packet(sv, c, true, false);
        cs2d_pkt_w_byte(p, 28); cs2d_pkt_w_byte(p, 0); cs2d_pkt_w_byte(p, 1);
    }

    /* Server info: [240,0,50,"1"], one [240,0,51,line] per line of sys/serverinfo.txt, [240,0,50,"2"].
     * FUN_1400ce360 writes a u16 count + UTF-16LE; a line of 256+ characters is cut */
    if (sv->info_count > 0) {
        p = sv_packet(sv, c, true, true);
        cs2d_pkt_w_byte(p, 240); cs2d_pkt_w_byte(p, 0); cs2d_pkt_w_byte(p, 50);
        cs2d_pkt_w_short(p, 1); cs2d_pkt_w_short(p, '1');
        for (int i = 0; i < sv->info_count; i++) {
            const uint8_t *t = (const uint8_t *)sv->info_lines[i];
            int len = (int)strlen((const char *)t);
            if (len > 255) len = 255;
            p = sv_packet(sv, c, true, c->out.pos >= 1000 - len);
            cs2d_pkt_w_byte(p, 240); cs2d_pkt_w_byte(p, 0); cs2d_pkt_w_byte(p, 51);
            cs2d_pkt_w_short(p, (uint16_t)len);
            for (int j = 0; j < len; j++) cs2d_pkt_w_short(p, t[j]); /* Latin-1 -> UTF-16 */
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
        } else if (sv_player_count(sv) >= sv->set.sv_maxplayers) {
            sv_join_fail_log(c, name, "can't join (server is full)");
            status = 3;
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
            char ip_str[32];
            snprintf(ip_str, sizeof(ip_str), "%s", inet_ntoa(c->addr.sin_addr));
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
    c->player = id;
    c->join_step = 2;

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
        /* The map transfer ([252,4,13] + [252,50]) is not ported yet: the client has to have the map */
        sv_join_fail_log(c, pl ? pl->name : "?", sha[0] ? "can't join (map differs)" : "can't join (map missing)");
        cs2d_pkt_w_byte(&c->out, (uint8_t)(sha[0] ? 11 : 12));
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
    pl->joined = true;   /* +0xf4 */
    cs2d_pkt_t *p = sv_packet(sv, c, true, true);
    int32_t round_ms = (int32_t)(sv_now() - sv->round_start_ms);
    cs2d_sv_build_join_settings(p, &sv->set, &sv->map, sv->score_t, sv->score_ct, round_ms, sv->seed);
    sv_send_join_snapshot(sv, c);
    c->join_step = 4;
    cs2d_sv_lua_hook_join(sv, c->player);
}

static void sv_recv_join(cs2d_server_t *sv, cs2d_sv_conn_t *c, sv_rd_t *r)
{
    if (rd_left(r) < 1) return;
    int sub = rd_byte(r);
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
    case 10:
        /* Client data (spray): stored with the player, relayed once sprays are ported. The skipped part:
         * [28,2,size u16,raw u32,zlib] */
        r->p = r->n;
        break;
    default:
        r->p = r->n;
        break;
    }
}

static void sv_remove_player(cs2d_server_t *sv, int id, int reason)
{
    if (id < 1 || id > CS2D_SV_MAX_PLAYERS || !sv->players[id].used) return;
    cs2d_server_print("%s has left the game", sv->players[id].name);
    cs2d_sv_lua_hook_leave(sv, id, "");
    sv->players[id].used = false;
    /* [253,pid,reason] to the others */
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        cs2d_sv_conn_t *oc = conn_of_player(sv, i);
        if (!oc || !sv->players[i].joined) continue;
        cs2d_pkt_t *p = sv_packet(sv, oc, true, false);
        cs2d_pkt_w_byte(p, 253);
        cs2d_pkt_w_byte(p, (uint8_t)id);
        cs2d_pkt_w_byte(p, (uint8_t)reason);
    }
}

/* FUN_1401502a0: kick a player with optional reason ([253, id, 2] or [253, id, 16, reason]) */
void cs2d_sv_kick(cs2d_server_t *sv, int id, const char *reason)
{
    if (id < 1 || id > CS2D_SV_MAX_PLAYERS || !sv->players[id].used) return;
    cs2d_sv_player_t *pl = &sv->players[id];
    bool has_reason = (reason && reason[0] != '\0');
    if (has_reason) {
        cs2d_server_print("%s has been kicked (%s)", pl->name, reason);
    } else {
        cs2d_server_print("%s has been kicked", pl->name);
    }

    cs2d_sv_conn_t *kicked_conn = conn_of_player(sv, id);
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        cs2d_sv_conn_t *oc = conn_of_player(sv, i);
        if (!oc || !sv->players[i].joined) continue;
        cs2d_pkt_t *p = sv_packet(sv, oc, true, false);
        cs2d_pkt_w_byte(p, 253);
        cs2d_pkt_w_byte(p, (uint8_t)id);
        cs2d_pkt_w_byte(p, (uint8_t)(has_reason ? 16 : 2));
        if (has_reason) cs2d_pkt_w_str(p, reason);
    }
    if (kicked_conn) {
        sv_flush(sv, kicked_conn);
        kicked_conn->active = false;
    }
    pl->used = false;
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

    char pw[256];
    cs2d_net_decrypt(enc_pw, enc_pw_len, "mysecretremotecontrolpw", pw, sizeof(pw));

    bool ok = sv_is_rcon_user(sv, pl->usgn) ||
              (sv->set.sv_rcon[0] != '\0' && strcmp(pw, sv->set.sv_rcon) == 0);
    if (ok) pl->rcon_auth = true;

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
    cs2d_server_print("Parse RCon (%s:%d): %s", pl->ip, pl->port, cmd);
    cs2d_sv_exec(sv, cmd);
}

/* FUN_1401be1c0 -> FUN_1401bd520: connectionless [242, pw (rstr), cmd (rlstr)] */
void cs2d_sv_public_rcon(cs2d_server_t *sv, const struct sockaddr_in *from,
                         const char *pw, const char *cmd)
{
    const char *err_code = NULL;
    if (sv->set.sv_rcon[0] == '\0') {
        err_code = "2"; /* 0x1401bdc50 */
    } else if (!pw || strcmp(pw, sv->set.sv_rcon) != 0) {
        err_code = "4"; /* 0x1401bdd16 */
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
        cs2d_server_print("Parse RCon ext. (%s:%d): %s",
                          inet_ntoa(from->sin_addr), ntohs(from->sin_port), cmd ? cmd : "");
    }
    if (cmd) cs2d_sv_exec(sv, cmd);
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
static int sv_resolve_weapon(const char *tok)
{
    if (!tok || !tok[0]) return 0;
    int id = atoi(tok);
    if (id > 0 && id < CS2D_MAX_WEAPONS) return id;
    for (int i = 1; i < CS2D_MAX_WEAPONS; i++) {
        const cs2d_weapon_def_t *w = cs2d_weapon_get((uint8_t)i);
        if (w && w->name && w->name[0] && _stricmp(w->name, tok) == 0) return i;
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
bool cs2d_sv_exec(cs2d_server_t *sv, const char *cmd_line)
{
    if (!sv || !cmd_line) return false;
    const char *cur = cmd_line;
    char cmd[64];
    if (!sv_next_arg(&cur, cmd, sizeof(cmd))) return false;

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
    if (_stricmp(cmd, "sv_msg") == 0) { /* 0x140144a92 */
        sv_rest_arg(cur, a1, sizeof(a1));
        cs2d_sv_msg(sv, a1, 0);
        return true;
    }
    if (_stricmp(cmd, "sv_msg2") == 0) { /* 0x140144b99 */
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
    if (_stricmp(cmd, "sv_restart") == 0 || _stricmp(cmd, "sv_restartround") == 0) {
        /* 0x140145180 -> 0x14015d2a0 */
        int delay = sv_next_arg(&cur, a1, sizeof(a1)) ? atoi(a1) : 0;
        if (delay < 0) delay = 0;
        if (delay > 10) delay = 10;
        cs2d_sv_round_end(sv, 5, delay * 1000);
        if (delay == 0) cs2d_sv_round_start(sv, 5);
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
        int look = sv_next_arg(&cur, a2, sizeof(a2)) ? atoi(a2) : 0;
        sv_rest_arg(cur, a3, sizeof(a3));
        cs2d_sv_bot_add(sv, team, look, a3);
        return true;
    }
    if (_stricmp(cmd, "bot_add_t") == 0) {
        int look = sv_next_arg(&cur, a1, sizeof(a1)) ? atoi(a1) : 0;
        sv_rest_arg(cur, a2, sizeof(a2));
        cs2d_sv_bot_add(sv, 1, look, a2);
        return true;
    }
    if (_stricmp(cmd, "bot_add_ct") == 0) {
        int look = sv_next_arg(&cur, a1, sizeof(a1)) ? atoi(a1) : 0;
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
    if (_stricmp(cmd, "listbans") == 0) {
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

    /* Fall back to server settings cvars (sv_*, mp_*, etc.) */
    sv_rest_arg(cur, a1, sizeof(a1));
    if (a1[0] != '\0') {
        cfg_set(&sv->set, cmd, a1);
        return true;
    }
    return false;
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
        if (pl) sv_remove_player(sv, c->player, reason);
        c->active = false;
        return false;
    }
    case 20: { /* team: [20,team,look] (FUN_1401cd140 -> FUN_1401624c0) */
        if (rd_left(r) < 2) return false;
        int team = rd_byte(r), look = rd_byte(r);
        if (pl) cs2d_sv_team(sv, c->player, team, look);
        return true;
    }
    case 229: rd_int(r); rd_int(r); return true;       /* clock ping reply */
    case 249: rd_int(r); return true;                  /* ping reply */
    case 10: case 11: { /* position [10 run / 11 walk, x, y] (FUN_1401cb570 / FUN_1401cb930) */
        if (rd_left(r) < 4) return false;
        int x = rd_short(r), y = rd_short(r);
        if (pl && pl->alive) {
            cs2d_sv_validate_move(sv, c->player, (float)x, (float)y, op);
        }
        return true;
    }
    case 71: { /* move + aim: [71, x u16, y u16, aim] (FUN_1401d3fb0) */
        if (rd_left(r) < 5) return false;
        int x = rd_short(r), y = rd_short(r), aim = rd_byte(r);
        if (pl && pl->alive) {
            cs2d_sv_validate_move(sv, c->player, (float)x, (float)y, 10);
            pl->aim = (float)aim * 2.0f;
        }
        return true;
    }
    case 72: { /* move + rot + aim: [72, x u16, y u16, rot u16, aim] (FUN_1401d4430) */
        if (rd_left(r) < 7) return false;
        int x = rd_short(r), y = rd_short(r), rot = rd_short(r), aim = rd_byte(r);
        if (pl && pl->alive) {
            cs2d_sv_validate_move(sv, c->player, (float)x, (float)y, 10);
            pl->rot = (float)rot / 182.04445f;
            pl->aim = (float)aim * 2.0f;
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
    case 41: { /* kill projectile: [41, proj_id u16] (FUN_1401cfd00) */
        if (rd_left(r) < 2) return false;
        int proj_id = rd_short(r);
        if (pl) cs2d_sv_kill_projectile(sv, c->player, proj_id);
        return true;
    }
    case 57: { /* flashlight: [57, state u8] (FUN_1401d28b0) */
        if (rd_left(r) < 1) return false;
        int state = rd_byte(r);
        if (pl) cs2d_sv_flashlight(sv, c->player, state);
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
    default:
        return false;
    }
}

static void sv_handle_datagram(cs2d_server_t *sv, const struct sockaddr_in *from, const uint8_t *buf, int n)
{
    if (n < 2) return;
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
        hello = (q + 1 < n && buf[q] == 252 && buf[q + 1] == 0);
    }
    cs2d_sv_conn_t *c = sv_find_conn(sv, from, hello);
    if (!c) return;
    c->last_recv = sv_now();
    c->bytes_in += (uint32_t)n;
    sv->bytes_in_acc += (uint32_t)n;

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
    while (r.p < n) {
        int op = rd_byte(&r);
        if (op == 252) {
            sv_recv_join(sv, c, &r);
        } else if (!sv_recv_game(sv, c, &r, op)) {
            break;
        }
    }
}

/* ------------------------------------------------------------------------------------------------ */
/* Periodic messages (0x1401e505d, once per second: DAT_1407dff88)                                  */
/* ------------------------------------------------------------------------------------------------ */

static void sv_second(cs2d_server_t *sv)
{
    sv->fps = sv->frames;
    sv->frames = 0;
    sv->bytes_in_sec = sv->bytes_in_acc * 8;
    sv->bytes_out_sec = sv->bytes_out_acc * 8;
    sv->bytes_in_acc = sv->bytes_out_acc = 0;

    /* Every 2nd second: [229, ms since start] (sv_antiaccel > 0) or [249, ...] to every connection */
    sv->ping_ctr++;
    sv->pinglist_ctr++;
    if (sv->ping_ctr > 1) {
        sv->ping_ctr = 0;
        for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
            cs2d_sv_conn_t *c = conn_of_player(sv, i);
            if (!c) continue;
            cs2d_pkt_t *p = sv_packet(sv, c, false, true);
            cs2d_pkt_w_byte(p, sv->set.sv_antiaccel > 0 ? 229 : 249);
            cs2d_pkt_w_int(p, (int32_t)(sv_now() - sv->start_ms));
            sv_flush(sv, c);
        }
    }
    /* Every 3rd second: [247, n, fps, in/8, out/8, n x {id, ping}] to the joined players */
    if (sv->pinglist_ctr > 2) {
        sv->pinglist_ctr = 0;
        int cnt = 0;
        for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) if (sv->players[i].used && sv->players[i].team > 0) cnt++;
        for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
            cs2d_sv_conn_t *c = conn_of_player(sv, i);
            if (!c || !sv->players[i].joined) continue;
            cs2d_pkt_t *p = sv_packet(sv, c, false, false);
            cs2d_pkt_w_byte(p, 247);
            cs2d_pkt_w_byte(p, (uint8_t)cnt);
            cs2d_pkt_w_byte(p, (uint8_t)sv->fps);                       /* DAT_1407dfe28 */
            cs2d_pkt_w_short(p, (uint16_t)(sv->bytes_in_sec / 8));      /* DAT_1407dfe34 / 8 */
            cs2d_pkt_w_short(p, (uint16_t)(sv->bytes_out_sec / 8));     /* DAT_1407dfe30 / 8 */
            for (int j = 1; j <= CS2D_SV_MAX_PLAYERS; j++) {
                const cs2d_sv_player_t *o = &sv->players[j];
                if (!o->used || o->team <= 0) continue;
                cs2d_pkt_w_byte(p, (uint8_t)j);
                cs2d_pkt_w_short(p, o->bot ? 0xfffe : (uint16_t)(o->ping > 30000 ? 30000 : o->ping));
            }
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
        if (k) snprintf(sv->maps[sv->map_count++], 64, "%s", m);
    }
    fclose(f);
    qsort(sv->maps, (size_t)sv->map_count, sizeof(sv->maps[0]), map_cmp);
}

bool cs2d_server_start(cs2d_server_t *sv, const char *map, int port, bool offline)
{
    if (!cs2d_platform_net_init()) return false;
    sv->offline = offline;
    if (map && map[0]) snprintf(sv->set.sv_map, sizeof(sv->set.sv_map), "%s", map);
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

    /* The join secret: 10 letters (proxy: "TuorxHPAAT"); the generator is not decompiled yet */
    srand(GetTickCount());
    for (int i = 0; i < 10; i++) {
        int k = rand() % 52;
        sv->secret[i] = (char)(k < 26 ? 'A' + k : 'a' + k - 26);
    }
    sv->secret[10] = '\0';
    sv->seed = rand() & 0x7fffffff;
    sv->start_ms = sv->round_start_ms = sv->second_ms = sv_now();

    sv->next_item_id = 1;
    memset(sv->items, 0, sizeof(sv->items));

    cs2d_sv_init_hostages(sv);
    sv_load_serverinfo(sv);
    sv_load_maplist(sv);
    cs2d_sv_lua_init(sv);
    cs2d_server_print("----- Server started -----");
    return true;
}

void cs2d_server_frame(cs2d_server_t *sv)
{
    uint8_t buf[4096];
    struct sockaddr_in from;
    for (;;) {
        cs2d_socklen_t fl = sizeof(from);
        int n = recvfrom(sv->sock, (char *)buf, sizeof(buf), 0, (struct sockaddr *)&from, &fl);
        if (n < 0) break;
        sv_handle_datagram(sv, &from, buf, n);
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
            if (c->player) sv_remove_player(sv, c->player, 1);
            c->active = false;
        }
    }

    cs2d_sv_relay_movement(sv);
    cs2d_sv_step_round(sv);
    cs2d_sv_step_bomb(sv);
    cs2d_sv_step_hostages(sv);
    cs2d_sv_step_items(sv);
    cs2d_sv_step_respawns(sv);
    cs2d_sv_lua_frame(sv);

    sv->frames++;
    if (now - sv->second_ms >= 1000) {
        sv->second_ms += 1000;
        sv_second(sv);
    }
}

void cs2d_server_stop(cs2d_server_t *sv)
{
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
