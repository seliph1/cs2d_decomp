#include "cs2d_network.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

uint8_t cs2d_net_get_opcode(const uint8_t *packet, size_t len)
{
    if (!packet || len == 0) return 0;
    return packet[0];
}

/*
 * Opcode 250 (CS2D_NET_PING_SERVERLIST) - Reconstructed from FUN_1401da1e0:
 *   WriteByte(stream, 250);
 *   WriteInt(stream, timestamp);
 */
size_t cs2d_net_build_ping_serverlist(int32_t timestamp, uint8_t *out_buf, size_t max_len)
{
    if (!out_buf || max_len < 5) return 0;
    out_buf[0] = (uint8_t)CS2D_NET_PING_SERVERLIST;
    out_buf[1] = (uint8_t)(timestamp & 0xFF);
    out_buf[2] = (uint8_t)((timestamp >> 8) & 0xFF);
    out_buf[3] = (uint8_t)((timestamp >> 16) & 0xFF);
    out_buf[4] = (uint8_t)((timestamp >> 24) & 0xFF);
    return 5;
}

bool cs2d_net_parse_ping_serverlist(const uint8_t *packet, size_t len, int32_t *out_timestamp)
{
    if (!packet || len < 5 || packet[0] != CS2D_NET_PING_SERVERLIST) return false;
    if (out_timestamp) {
        *out_timestamp = (int32_t)(
            ((uint32_t)packet[1]) |
            ((uint32_t)packet[2] << 8) |
            ((uint32_t)packet[3] << 16) |
            ((uint32_t)packet[4] << 24)
        );
    }
    return true;
}

/*
 * Opcode 251 (CS2D_NET_INFO) - ded FUN_1401da400, client parser 0x1402d2d80, proxy capture:
 *   Client request: [251, mode (1 internet, 2 LAN, 5 player list), uint16_t version (0x03fa = 1018)]
 *   Server response (mode 1 or 2):
 *     [251][mode][flags][name: u8 len + text][map: u8 len + text][players][max]
 *     ([gamemode] when flags bit 5)[bots][flags2]
 *   e.g. fb 01 10 0b "CS2D Server" 0e "prison_funst3r" 00 0c 00 02
 *   (the full datagram decoder for the browser is cs2d_sv_parse_info in cs2d_serverlist.c)
 */
size_t cs2d_net_build_info_request(uint8_t mode, uint8_t *out_buf, size_t max_len)
{
    if (!out_buf || max_len < 4) return 0;
    out_buf[0] = (uint8_t)CS2D_NET_INFO;
    out_buf[1] = mode;
    /* Version 1018 (0x03fa) from FUN_1401dac80 line 1254 */
    out_buf[2] = 0xFA;
    out_buf[3] = 0x03;
    return 4;
}

static void write_bstr(uint8_t *buf, size_t *offset, const char *str, size_t max_chars)
{
    size_t len = str ? strlen(str) : 0;
    if (len > max_chars) len = max_chars;
    buf[(*offset)++] = (uint8_t)len;
    if (len > 0) {
        memcpy(&buf[*offset], str, len);
        *offset += len;
    }
}

static bool read_bstr(const uint8_t *buf, size_t len, size_t *offset, char *out_str, size_t max_out)
{
    if (*offset >= len) return false;
    size_t n = buf[(*offset)++];
    if (*offset + n > len) return false;
    size_t copy = (n < max_out - 1) ? n : max_out - 1;
    memcpy(out_str, &buf[*offset], copy);
    out_str[copy] = '\0';
    *offset += n;
    return true;
}

size_t cs2d_net_build_info_packet(
    const cs2d_net_server_info_t *info,
    uint8_t                      *out_buf,
    size_t                        max_len
)
{
    if (!info || !out_buf || max_len < 128) return 0;

    size_t offset = 0;
    out_buf[offset++] = (uint8_t)CS2D_NET_INFO;
    out_buf[offset++] = (info->mode != 0) ? info->mode : 1;

    uint8_t flags = info->flags;
    if (info->password_protected) flags |= 0x01;
    if (info->gamemode != 0)      flags |= 0x20;
    out_buf[offset++] = flags;

    write_bstr(out_buf, &offset, info->server_name, 60);
    write_bstr(out_buf, &offset, info->map_name, 30);

    out_buf[offset++] = info->players_current;
    out_buf[offset++] = info->players_max;
    if (flags & 0x20) {
        out_buf[offset++] = info->gamemode;
    }
    out_buf[offset++] = info->bots;
    out_buf[offset++] = info->flags2;

    return offset;
}

bool cs2d_net_parse_info_packet(
    const uint8_t          *packet,
    size_t                  len,
    cs2d_net_server_info_t *out_info
)
{
    if (!packet || len < 7 || !out_info) return false;
    if (packet[0] != CS2D_NET_INFO) return false;

    memset(out_info, 0, sizeof(*out_info));
    size_t offset = 1;
    out_info->mode = packet[offset++];
    out_info->flags = packet[offset++];
    out_info->password_protected = (out_info->flags & 0x01) != 0;

    if (!read_bstr(packet, len, &offset, out_info->server_name, sizeof(out_info->server_name))) return false;
    if (!read_bstr(packet, len, &offset, out_info->map_name, sizeof(out_info->map_name))) return false;

    if (offset + 2 > len) return false;
    out_info->players_current = packet[offset++];
    out_info->players_max = packet[offset++];

    if ((out_info->flags & 0x20) && offset < len) {
        out_info->gamemode = packet[offset++];
    }
    if (offset < len) out_info->bots   = packet[offset++];
    if (offset < len) out_info->flags2 = packet[offset++];
    return true;
}

/*
 * Opcode 10 (pos - run) / Opcode 11 (pos - walk) - Reconstructed from FUN_1401cb570:
 *   Client -> Server: [opcode, uint16_t x, uint16_t y] (5 bytes)
 *   Server -> Client: [opcode, uint8_t player_id, uint16_t x, uint16_t y] (6 bytes)
 */
size_t cs2d_net_build_pos_packet(
    bool     walk,
    uint8_t  player_id,
    float    x,
    float    y,
    uint8_t *out_buf,
    size_t   max_len
)
{
    size_t needed = (player_id > 0) ? 6 : 5;
    if (!out_buf || max_len < needed) return 0;

    size_t idx = 0;
    out_buf[idx++] = walk ? (uint8_t)CS2D_NET_POS_WALK : (uint8_t)CS2D_NET_POS_RUN;
    if (player_id > 0) {
        out_buf[idx++] = player_id;
    }

    uint16_t ix = (uint16_t)((int)x & 0xFFFF);
    uint16_t iy = (uint16_t)((int)y & 0xFFFF);
    out_buf[idx++] = (uint8_t)(ix & 0xFF);
    out_buf[idx++] = (uint8_t)((ix >> 8) & 0xFF);
    out_buf[idx++] = (uint8_t)(iy & 0xFF);
    out_buf[idx++] = (uint8_t)((iy >> 8) & 0xFF);
    return idx;
}

/*
 * Opcode 12 (rot) - Reconstructed from FUN_1401cbce0:
 *   fVar5 = (float)uVar2 * 0.005493164f;
 *   Client -> Server: [12, uint16_t rot] (3 bytes)
 *   Server -> Client: [12, uint8_t player_id, uint16_t rot] (4 bytes)
 */
size_t cs2d_net_build_rot_packet(
    uint8_t  player_id,
    float    rot,
    uint8_t *out_buf,
    size_t   max_len
)
{
    size_t needed = (player_id > 0) ? 4 : 3;
    if (!out_buf || max_len < needed) return 0;

    while (rot < 0.0f) rot += 360.0f;
    while (rot >= 360.0f) rot -= 360.0f;
    uint16_t u16_rot = (uint16_t)(rot / CS2D_NET_ROT_SCALE);

    size_t idx = 0;
    out_buf[idx++] = (uint8_t)CS2D_NET_ROT;
    if (player_id > 0) {
        out_buf[idx++] = player_id;
    }
    out_buf[idx++] = (uint8_t)(u16_rot & 0xFF);
    out_buf[idx++] = (uint8_t)((u16_rot >> 8) & 0xFF);
    return idx;
}

/*
 * Opcode 13 (posrot) - Reconstructed from FUN_1401cc0b0:
 *   Client -> Server: [13, uint16_t x, uint16_t y, uint16_t rot] (7 bytes)
 *   Server -> Client: [13, uint8_t player_id, uint16_t x, uint16_t y, uint16_t rot] (8 bytes)
 */
size_t cs2d_net_build_posrot_packet(
    uint8_t  player_id,
    float    x,
    float    y,
    float    rot,
    uint8_t *out_buf,
    size_t   max_len
)
{
    size_t needed = (player_id > 0) ? 8 : 7;
    if (!out_buf || max_len < needed) return 0;

    while (rot < 0.0f) rot += 360.0f;
    while (rot >= 360.0f) rot -= 360.0f;

    uint16_t ix = (uint16_t)((int)x & 0xFFFF);
    uint16_t iy = (uint16_t)((int)y & 0xFFFF);
    uint16_t irot = (uint16_t)(rot / CS2D_NET_ROT_SCALE);

    size_t idx = 0;
    out_buf[idx++] = (uint8_t)CS2D_NET_POSROT;
    if (player_id > 0) {
        out_buf[idx++] = player_id;
    }
    out_buf[idx++] = (uint8_t)(ix & 0xFF);
    out_buf[idx++] = (uint8_t)((ix >> 8) & 0xFF);
    out_buf[idx++] = (uint8_t)(iy & 0xFF);
    out_buf[idx++] = (uint8_t)((iy >> 8) & 0xFF);
    out_buf[idx++] = (uint8_t)(irot & 0xFF);
    out_buf[idx++] = (uint8_t)((irot >> 8) & 0xFF);
    return idx;
}

bool cs2d_net_parse_posrot_packet(
    const uint8_t *packet,
    size_t         len,
    uint8_t       *out_player_id,
    float         *out_x,
    float         *out_y,
    float         *out_rot
)
{
    if (!packet || len < 7) return false;
    if (packet[0] != CS2D_NET_POSROT && packet[0] != CS2D_NET_POSROT2) return false;

    size_t idx = 1;
    if (len >= 8) {
        if (out_player_id) *out_player_id = packet[idx];
        idx++;
    } else {
        if (out_player_id) *out_player_id = 0;
    }

    uint16_t ix   = (uint16_t)packet[idx] | ((uint16_t)packet[idx + 1] << 8);
    uint16_t iy   = (uint16_t)packet[idx + 2] | ((uint16_t)packet[idx + 3] << 8);
    uint16_t irot = (uint16_t)packet[idx + 4] | ((uint16_t)packet[idx + 5] << 8);

    if (out_x)   *out_x   = (float)ix;
    if (out_y)   *out_y   = (float)iy;
    if (out_rot) *out_rot = (float)irot * CS2D_NET_ROT_SCALE;

    return true;
}

/*
 * Opcode 17 (damage) - Reconstructed from FUN_1401cc9f0 & FUN_1401658c0:
 *   WriteByte(17);
 *   WriteByte(victim_id);
 *   WriteByte(attacker_id);
 *   WriteByte(new_health);
 *   WriteByte(new_armor);
 */
size_t cs2d_net_build_damage_packet(
    uint8_t  victim_id,
    uint8_t  attacker_id,
    uint8_t  new_health,
    uint8_t  new_armor,
    uint8_t *out_buf,
    size_t   max_len
)
{
    if (!out_buf || max_len < 5) return 0;

    out_buf[0] = (uint8_t)CS2D_NET_DAMAGE;
    out_buf[1] = victim_id;
    out_buf[2] = attacker_id;
    out_buf[3] = new_health;
    out_buf[4] = new_armor;

    return 5;
}

bool cs2d_net_parse_damage_packet(
    const uint8_t *packet,
    size_t         len,
    uint8_t       *out_victim_id,
    uint8_t       *out_attacker_id,
    uint8_t       *out_new_health,
    uint8_t       *out_new_armor
)
{
    if (!packet || len < 5) return false;
    if (packet[0] != CS2D_NET_DAMAGE) return false;

    if (out_victim_id)   *out_victim_id   = packet[1];
    if (out_attacker_id) *out_attacker_id = packet[2];
    if (out_new_health)  *out_new_health  = packet[3];
    if (out_new_armor)   *out_new_armor   = packet[4];

    return true;
}

/*
 * Opcode 19 (kill) - Reconstructed from FUN_1401ccc90:
 *   WriteByte(19);
 *   WriteByte(victim_id);
 *   WriteByte(killer_id);
 *   WriteByte(weapon_id);
 *   WriteByte(flag);
 *   WriteShort(victim_x);
 *   WriteShort(victim_y);
 */
size_t cs2d_net_build_kill_packet(
    uint8_t  victim_id,
    uint8_t  killer_id,
    uint8_t  weapon_id,
    uint8_t  flag,
    uint16_t victim_x,
    uint16_t victim_y,
    uint8_t *out_buf,
    size_t   max_len
)
{
    if (!out_buf || max_len < 9) return 0;

    out_buf[0] = (uint8_t)CS2D_NET_KILL;
    out_buf[1] = victim_id;
    out_buf[2] = killer_id;
    out_buf[3] = weapon_id;
    out_buf[4] = flag;
    out_buf[5] = (uint8_t)(victim_x & 0xFF);
    out_buf[6] = (uint8_t)((victim_x >> 8) & 0xFF);
    out_buf[7] = (uint8_t)(victim_y & 0xFF);
    out_buf[8] = (uint8_t)((victim_y >> 8) & 0xFF);

    return 9;
}

bool cs2d_net_parse_kill_packet(
    const uint8_t *packet,
    size_t         len,
    uint8_t       *out_victim_id,
    uint8_t       *out_killer_id,
    uint8_t       *out_weapon_id,
    uint8_t       *out_flag,
    uint16_t      *out_victim_x,
    uint16_t      *out_victim_y
)
{
    if (!packet || len < 9) return false;
    if (packet[0] != CS2D_NET_KILL) return false;

    if (out_victim_id) *out_victim_id = packet[1];
    if (out_killer_id) *out_killer_id = packet[2];
    if (out_weapon_id) *out_weapon_id = packet[3];
    if (out_flag)      *out_flag      = packet[4];
    if (out_victim_x)  *out_victim_x  = (uint16_t)packet[5] | ((uint16_t)packet[6] << 8);
    if (out_victim_y)  *out_victim_y  = (uint16_t)packet[7] | ((uint16_t)packet[8] << 8);

    return true;
}

/*
 * Opcode 20 (team) - Reconstructed from FUN_1401cd140
 */
size_t cs2d_net_build_team_packet(
    uint8_t  player_id,
    uint8_t  team,
    uint8_t  look,
    uint8_t *out_buf,
    size_t   max_len
)
{
    size_t needed = (player_id > 0) ? 4 : 3;
    if (!out_buf || max_len < needed) return 0;

    size_t idx = 0;
    out_buf[idx++] = (uint8_t)CS2D_NET_TEAM;
    if (player_id > 0) {
        out_buf[idx++] = player_id;
    }
    out_buf[idx++] = team;
    out_buf[idx++] = look;
    return idx;
}

/*
 * Opcode 21 (spawn) - Reconstructed from FUN_1401cd470 & FUN_140160740
 */
size_t cs2d_net_build_spawn_packet(
    uint8_t        player_id,
    uint16_t       x,
    uint16_t       y,
    uint8_t        weapon_id,
    uint8_t        armor,
    uint16_t       money,
    const uint8_t *items,
    uint8_t        item_count,
    uint8_t       *out_buf,
    size_t         max_len
)
{
    size_t needed = 11 + (size_t)item_count;
    if (!out_buf || max_len < needed) return 0;

    size_t idx = 0;
    out_buf[idx++] = (uint8_t)CS2D_NET_SPAWN;
    out_buf[idx++] = player_id;
    out_buf[idx++] = (uint8_t)(x & 0xFF);
    out_buf[idx++] = (uint8_t)((x >> 8) & 0xFF);
    out_buf[idx++] = (uint8_t)(y & 0xFF);
    out_buf[idx++] = (uint8_t)((y >> 8) & 0xFF);
    out_buf[idx++] = weapon_id;
    out_buf[idx++] = armor;
    out_buf[idx++] = (uint8_t)(money & 0xFF);
    out_buf[idx++] = (uint8_t)((money >> 8) & 0xFF);
    out_buf[idx++] = item_count;
    if (item_count > 0 && items) {
        memcpy(&out_buf[idx], items, item_count);
        idx += item_count;
    }
    return idx;
}

bool cs2d_net_parse_spawn_packet(
    const uint8_t *packet,
    size_t         len,
    uint8_t       *out_player_id,
    uint16_t      *out_x,
    uint16_t      *out_y,
    uint8_t       *out_weapon_id,
    uint8_t       *out_armor,
    uint16_t      *out_money,
    uint8_t       *out_items,
    uint8_t       *out_item_count,
    size_t        *out_bytes_read
)
{
    if (!packet || len < 11 || packet[0] != CS2D_NET_SPAWN) return false;
    uint8_t item_cnt = packet[10];
    if (len < 11 + (size_t)item_cnt) return false;

    if (out_player_id)  *out_player_id  = packet[1];
    if (out_x)          *out_x          = (uint16_t)packet[2] | ((uint16_t)packet[3] << 8);
    if (out_y)          *out_y          = (uint16_t)packet[4] | ((uint16_t)packet[5] << 8);
    if (out_weapon_id)  *out_weapon_id  = packet[6];
    if (out_armor)      *out_armor      = packet[7];
    if (out_money)      *out_money      = (uint16_t)packet[8] | ((uint16_t)packet[9] << 8);
    if (out_item_count) *out_item_count = item_cnt;
    if (out_items && item_cnt > 0) {
        memcpy(out_items, &packet[11], item_cnt);
    }
    if (out_bytes_read) *out_bytes_read = 11 + (size_t)item_cnt;
    return true;
}

/* --- Authentic Tudp Stream Helpers (0x1402b8700..0x1402ba820) --- */
void cs2d_pkt_w_byte(cs2d_pkt_t *p, uint8_t b)
{
    if (p && p->pos < (int)sizeof(p->buf)) p->buf[p->pos++] = b;
}

void cs2d_pkt_w_short(cs2d_pkt_t *p, uint16_t s)
{
    if (p && p->pos + 2 <= (int)sizeof(p->buf)) {
        memcpy(p->buf + p->pos, &s, 2);
        p->pos += 2;
    }
}

void cs2d_pkt_w_int(cs2d_pkt_t *p, int32_t v)
{
    if (p && p->pos + 4 <= (int)sizeof(p->buf)) {
        memcpy(p->buf + p->pos, &v, 4);
        p->pos += 4;
    }
}

void cs2d_pkt_w_long(cs2d_pkt_t *p, uint64_t v)
{
    if (p && p->pos + 8 <= (int)sizeof(p->buf)) {
        memcpy(p->buf + p->pos, &v, 8);
        p->pos += 8;
    }
}

void cs2d_pkt_w_str(cs2d_pkt_t *p, const char *s)
{
    if (!p) return;
    size_t slen = s ? strlen(s) : 0;
    if (slen > 255) slen = 255;
    if (p->pos + 1 + (int)slen <= (int)sizeof(p->buf)) {
        p->buf[p->pos++] = (uint8_t)slen;
        if (slen > 0) {
            memcpy(p->buf + p->pos, s, slen);
            p->pos += (int)slen;
        }
    }
}

void cs2d_pkt_w_raw_str(cs2d_pkt_t *p, const uint8_t *b, int len)
{
    if (!p) return;
    if (len < 0) len = 0;
    if (len > 255) len = 255;
    if (p->pos + 1 + len <= (int)sizeof(p->buf)) {
        p->buf[p->pos++] = (uint8_t)len;
        if (len > 0 && b) {
            memcpy(p->buf + p->pos, b, (size_t)len);
            p->pos += len;
        }
    }
}

void cs2d_pkt_w_lstr(cs2d_pkt_t *p, const char *s)
{
    if (!p) return;
    size_t slen = s ? strlen(s) : 0;
    if (slen > 65535) slen = 65535;
    uint16_t ulen = (uint16_t)slen;
    if (p->pos + 2 + (int)ulen <= (int)sizeof(p->buf)) {
        memcpy(p->buf + p->pos, &ulen, 2);
        p->pos += 2;
        if (ulen > 0) {
            memcpy(p->buf + p->pos, s, ulen);
            p->pos += (int)ulen;
        }
    }
}

int cs2d_pkt_r_str(const uint8_t *buf, int len, int *pos, char *out, int max_out)
{
    if (!buf || !pos || *pos >= len) {
        if (out && max_out > 0) out[0] = '\0';
        return 0;
    }
    uint8_t slen = buf[(*pos)++];
    int i = 0;
    for (int k = 0; k < (int)slen && *pos < len; k++) {
        uint8_t c = buf[(*pos)++];
        if (out && i < max_out - 1) out[i++] = (char)c;
    }
    if (out && max_out > 0) out[i] = '\0';
    return i;
}

/* --- MD5 Implementation (0x1400c9ed0) --- */
static const uint32_t MD5_K[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
};
static const uint32_t MD5_S[64] = {
    7,12,17,22, 7,12,17,22, 7,12,17,22, 7,12,17,22,
    5, 9,14,20, 5, 9,14,20, 5, 9,14,20, 5, 9,14,20,
    4,11,16,23, 4,11,16,23, 4,11,16,23, 4,11,16,23,
    6,10,15,21, 6,10,15,21, 6,10,15,21, 6,10,15,21
};
#define CS2D_ROL32(x, n) (((x) << (n)) | ((x) >> (32 - (n))))
#define CS2D_ROR32(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

void cs2d_net_md5_hex(const char *str, char out_hex[33])
{
    size_t len = str ? strlen(str) : 0;
    uint8_t msg[128];
    memset(msg, 0, sizeof(msg));
    if (len > 0 && len < 56) memcpy(msg, str, len);
    msg[len] = 0x80;
    uint64_t bit_len = (uint64_t)len * 8;
    memcpy(msg + 56, &bit_len, 8);

    uint32_t M[16];
    memcpy(M, msg, 64);

    uint32_t a0 = 0x67452301, b0 = 0xefcdab89, c0 = 0x98badcfe, d0 = 0x10325476;
    uint32_t A = a0, B = b0, C = c0, D = d0;
    for (uint32_t i = 0; i < 64; i++) {
        uint32_t F, g;
        if (i < 16)      { F = (B & C) | ((~B) & D); g = i; }
        else if (i < 32) { F = (D & B) | ((~D) & C); g = (5 * i + 1) % 16; }
        else if (i < 48) { F = B ^ C ^ D;            g = (3 * i + 5) % 16; }
        else             { F = C ^ (B | (~D));       g = (7 * i) % 16; }
        F = F + A + MD5_K[i] + M[g];
        A = D; D = C; C = B;
        B = B + CS2D_ROL32(F, MD5_S[i]);
    }
    a0 += A; b0 += B; c0 += C; d0 += D;
    uint8_t digest[16];
    memcpy(digest +  0, &a0, 4);
    memcpy(digest +  4, &b0, 4);
    memcpy(digest +  8, &c0, 4);
    memcpy(digest + 12, &d0, 4);
    for (int i = 0; i < 16; i++) sprintf(out_hex + i * 2, "%02x", digest[i]);
    out_hex[32] = '\0';
}

/* --- SHA-256 Implementation (0x140116560 / 0x140116020 + 0x14008bdc0) --- */
static const uint32_t SHA256_K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

bool cs2d_net_sha256_file(const char *filepath, char out_hex[65])
{
    FILE *f = fopen(filepath, "rb");
    if (!f) { out_hex[0] = '\0'; return false; }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = (uint8_t *)malloc((size_t)fsize + 1);
    if (!data) { fclose(f); out_hex[0] = '\0'; return false; }
    if (fsize > 0) fread(data, 1, (size_t)fsize, f);
    fclose(f);

    uint32_t n_blocks = (((uint32_t)fsize + 8) >> 6) + 1;
    uint32_t total_words = n_blocks * 16;
    uint32_t *words = (uint32_t *)calloc(total_words, sizeof(uint32_t));
    for (long i = 0; i < fsize; i++) {
        words[i >> 2] |= ((uint32_t)data[i]) << (24 - (i & 3) * 8);
    }
    words[fsize >> 2] |= 0x80U << (24 - (fsize & 3) * 8);
    words[total_words - 1] = (uint32_t)(fsize * 8);

    uint32_t H[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };
    uint32_t W[64];
    for (uint32_t b = 0; b < n_blocks; b++) {
        for (int i = 0; i < 16; i++) W[i] = words[b * 16 + i];
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = CS2D_ROR32(W[i-15], 7) ^ CS2D_ROR32(W[i-15], 18) ^ (W[i-15] >> 3);
            uint32_t s1 = CS2D_ROR32(W[i-2], 17) ^ CS2D_ROR32(W[i-2], 19) ^ (W[i-2] >> 10);
            W[i] = W[i-16] + s0 + W[i-7] + s1;
        }
        uint32_t a=H[0], b1=H[1], c=H[2], d=H[3], e=H[4], f1=H[5], g=H[6], h=H[7];
        for (int i = 0; i < 64; i++) {
            uint32_t S1 = CS2D_ROR32(e, 6) ^ CS2D_ROR32(e, 11) ^ CS2D_ROR32(e, 25);
            uint32_t ch = (e & f1) ^ ((~e) & g);
            uint32_t temp1 = h + S1 + ch + SHA256_K[i] + W[i];
            uint32_t S0 = CS2D_ROR32(a, 2) ^ CS2D_ROR32(a, 13) ^ CS2D_ROR32(a, 22);
            uint32_t maj = (a & b1) ^ (a & c) ^ (b1 & c);
            uint32_t temp2 = S0 + maj;
            h = g; g = f1; f1 = e; e = d + temp1;
            d = c; c = b1; b1 = a; a = temp1 + temp2;
        }
        H[0]+=a; H[1]+=b1; H[2]+=c; H[3]+=d; H[4]+=e; H[5]+=f1; H[6]+=g; H[7]+=h;
    }
    free(words);
    free(data);
    for (int i = 0; i < 8; i++) sprintf(out_hex + i * 8, "%08x", H[i]);
    out_hex[64] = '\0';
    return true;
}

/* --- CS2D Shift Cipher (0x140055400) --- */
void cs2d_net_encrypt(const char *txt, const char *key, uint8_t *out, int *out_len)
{
    int n = txt ? (int)strlen(txt) : 0;
    int period = key ? ((int)strlen(key) - 1) : 0;
    int idx = 0;
    for (int i = 0; i < n; i++) {
        int k = 0;
        if (period > 0) {
            if (idx < period) {
                k = (uint8_t)key[idx];
                idx++;
            } else {
                k = (uint8_t)key[0];
                idx = 1;
            }
        }
        int v = (uint8_t)txt[i] + k;
        if (v > 255) v -= 255;
        out[i] = (uint8_t)v;
    }
    if (out_len) *out_len = n;
}

/* --- CS2D net_secret Transform (0x1402c9660 / 0x1401be680) --- */
void cs2d_net_secret(const char *raw_secret, char *out)
{
    int n = raw_secret ? (int)strlen(raw_secret) : 0;
    if (n < 9) { out[0] = '\0'; return; }
    char rev[128];
    for (int i = 0; i < n; i++) rev[i] = raw_secret[n - 1 - i];
    rev[n] = '\0';

    /* bbStringSlice(rev, 3, 8) -> 5 chars at index 3..7 */
    char sub[6];
    memcpy(sub, rev + 3, 5);
    sub[5] = '\0';

    char md5_str[33];
    cs2d_net_md5_hex(sub, md5_str);

    uint16_t c4 = (uint8_t)rev[4];
    uint16_t c7 = (uint8_t)rev[7];
    uint16_t c8 = (uint8_t)rev[8];

    out[0] = '\0';
    strcat(out, (c8 & 1) ? "z" : "x");
    if ((uint16_t)(c4 * 0xcccd) <= 0x3333) strcat(out, "!O");
    if ((uint16_t)(c7 * 0x8e39 + 0x71c7) <= 0x1c71) strcat(out, "l");

    /* bbStringSlice(md5_str, 2, 10) -> 8 chars at index 2..9 */
    char mid_b[9];
    memcpy(mid_b, md5_str + 2, 8);
    mid_b[8] = '\0';

    /* bbStringSlice(rev, 1, 2) -> 1 char at index 1 */
    char mid_a[2];
    mid_a[0] = rev[1];
    mid_a[1] = '\0';

    /* String(Asc(String(c8))) */
    char num_buf[16], asc_buf[16];
    sprintf(num_buf, "%u", (unsigned)c8);
    sprintf(asc_buf, "%d", (int)(uint8_t)num_buf[0]);

    strcat(out, mid_b);
    strcat(out, mid_a);
    strcat(out, asc_buf);
}

/* --- Opcode 252 (CS2D_NET_JOIN) 4-Stage Handshake Builders --- */
void cs2d_net_build_join_stage0(cs2d_pkt_t *p)
{
    if (!p) return;
    p->pos = 0;
    cs2d_pkt_w_byte(p, 252);
    cs2d_pkt_w_byte(p, 0);
    cs2d_pkt_w_str(p, "");
}

void cs2d_net_build_join_stage1(
    cs2d_pkt_t *p,
    const char *player_name,
    const char *password,
    const char *raw_secret,
    int         screen_w,
    int         screen_h,
    int         map_mode
)
{
    if (!p) return;
    p->pos = 0;

    char t_secret[128];
    cs2d_net_secret(raw_secret, t_secret);

    uint8_t enc_ver[32];
    int enc_ver_len = 0;
    cs2d_net_encrypt("1.0.1.8", "funkymonky", enc_ver, &enc_ver_len);

    uint8_t enc_pass[128];
    int enc_pass_len = 0;
    if (password && password[0] != '\0') {
        /* Key bbs_j01nk3y (0x140a3bf20), applied in the [252,1] builder of FUN_1402ce7c0 */
        cs2d_net_encrypt(password, "j01nk3y", enc_pass, &enc_pass_len);
    }

    cs2d_pkt_w_byte(p, 252);
    cs2d_pkt_w_byte(p, 1);
    cs2d_pkt_w_str(p, (player_name && player_name[0]) ? player_name : "CS2D_Decomp"); /* 1. name */
    cs2d_pkt_w_raw_str(p, enc_pass, enc_pass_len);                                    /* 2. enc_password */
    cs2d_pkt_w_raw_str(p, enc_ver, enc_ver_len);                                      /* 3. enc_version_str */
    cs2d_pkt_w_short(p, 1018);                                                        /* 4. version_short (0x03fa) */
    cs2d_pkt_w_int(p, 0);                                                             /* 5. usgn_id */
    cs2d_pkt_w_lstr(p, "");                                                           /* 6. usgn_name */
    cs2d_pkt_w_byte(p, 0);                                                            /* 7. auth_type */
    cs2d_pkt_w_lstr(p, "");                                                           /* 8. auth_token */
    cs2d_pkt_w_long(p, 0ULL);                                                         /* 9. steam_id */
    cs2d_pkt_w_str(p, "");                                                            /* 10. steam_name */
    cs2d_pkt_w_str(p, t_secret);                                                      /* 11. transformed_secret */
    cs2d_pkt_w_str(p, "");                                                            /* 12. security_hash */
    cs2d_pkt_w_str(p, "\xFA" "0opengl");                                              /* 13. os_info */
    cs2d_pkt_w_byte(p, (uint8_t)map_mode);                                            /* 14. maptransfer_mode */
    cs2d_pkt_w_int(p, screen_w > 0 ? screen_w : 850);                                 /* 15. screen_w */
    cs2d_pkt_w_int(p, screen_h > 0 ? screen_h : 480);                                 /* 16. screen_h */
    cs2d_pkt_w_lstr(p, "");                                                           /* 17. spraylogo */
    cs2d_pkt_w_lstr(p, "en");                                                         /* 18. language */
    cs2d_pkt_w_byte(p, 0);                                                            /* 19. flag1 */
    cs2d_pkt_w_byte(p, 1);                                                            /* 20. flag2 */
    cs2d_pkt_w_byte(p, 0);                                                            /* 21. flag3 */
}

void cs2d_net_build_join_stage2(
    cs2d_pkt_t *p,
    const char *map_checksum,
    const char *raw_secret,
    int         download
)
{
    if (!p) return;
    p->pos = 0;

    int rlen = raw_secret ? (int)strlen(raw_secret) : 0;
    char rev_sec[128];
    for (int i = 0; i < rlen; i++) rev_sec[i] = raw_secret[rlen - 1 - i];
    rev_sec[rlen] = '\0';

    uint8_t enc_sec[128];
    int enc_sec_len = 0;
    cs2d_net_encrypt(rev_sec, "!aCz0jK5f\xFC", enc_sec, &enc_sec_len);

    cs2d_pkt_w_byte(p, 252);
    cs2d_pkt_w_byte(p, 3);
    cs2d_pkt_w_str(p, map_checksum ? map_checksum : "");
    cs2d_pkt_w_raw_str(p, enc_sec, enc_sec_len);
    cs2d_pkt_w_byte(p, (uint8_t)download); /* DAT_140a14f4c, last byte of [252,3] in FUN_1402ce7c0 */
}

void cs2d_net_build_join_stage3(
    cs2d_pkt_t *p,
    const char *map_name,
    const char *map_checksum
)
{
    if (!p) return;
    p->pos = 0;
    cs2d_pkt_w_byte(p, 252);
    cs2d_pkt_w_byte(p, 5);
    cs2d_pkt_w_str(p, map_name ? map_name : "");
    cs2d_pkt_w_str(p, map_checksum ? map_checksum : "");
}

