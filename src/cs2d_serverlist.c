#include "cs2d_serverlist.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Server browser model and queries. Packet layouts are verified with a proxy capture of the
 * original client against cs2d_dedicated.exe -offline (see KNOWLEDGE.md "Server browser"):
 *   C>S [01 00][03][tick u16][FB][mode][version u16 = 1018]   info request (mode 1 internet, 2 LAN)
 *   C>S [01 00][FA][millisecs i32]                             ping
 *   S>C [01 00][FA][millisecs i32]                             ping echo
 *   S>C [01 00][FB][mode][flags][name][map][players][max]([gamemode])[bots][flags2]
 */

uint32_t cs2d_sv_ip_from_octets(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | (uint32_t)d;
}

void cs2d_sv_ip_to_str(uint32_t ip, char out[16])
{
    snprintf(out, 16, "%u.%u.%u.%u", (ip >> 24) & 255u, (ip >> 16) & 255u, (ip >> 8) & 255u, ip & 255u);
}

bool cs2d_sv_parse_address(const char *text, uint32_t *out_ip, uint16_t *out_port)
{
    unsigned a, b, c, d, port = CS2D_SV_LAN_PORT;
    if (!text) return false;
    while (*text == ' ' || *text == '\t') text++;
    int n = sscanf(text, "%u.%u.%u.%u:%u", &a, &b, &c, &d, &port);
    if (n < 4 || a > 255 || b > 255 || c > 255 || d > 255 || port == 0 || port > 65535) return false;
    if (out_ip)   *out_ip   = cs2d_sv_ip_from_octets((uint8_t)a, (uint8_t)b, (uint8_t)c, (uint8_t)d);
    if (out_port) *out_port = (uint16_t)port;
    return true;
}

/* Tsv.get (0x14003d6d0) */
cs2d_sv_t *cs2d_sv_list_find(cs2d_sv_list_t *list, uint32_t ip, uint16_t port)
{
    if (!list) return NULL;
    for (int i = 0; i < list->count; i++) {
        if (list->entries[i].ip == ip && list->entries[i].port == port) return &list->entries[i];
    }
    return NULL;
}

/*
 * Tsv.addNoCheck (0x14002c310) behind a Tsv.get check: the first query of entry n is due
 * n * 30 ms after the list was filled (pingt = now - 3000 + n * DAT_140a14ca8).
 */
cs2d_sv_t *cs2d_sv_list_add(cs2d_sv_list_t *list, uint32_t ip, uint16_t port, uint32_t now_ms)
{
    if (!list) return NULL;
    cs2d_sv_t *sv = cs2d_sv_list_find(list, ip, port);
    if (sv) return sv;
    if (list->count >= CS2D_SV_MAX) return NULL;
    sv = &list->entries[list->count];
    memset(sv, 0, sizeof(*sv));
    sv->ip        = ip;
    sv->port      = port;
    sv->ping_time = now_ms - CS2D_SV_RETRY_MS + (uint32_t)(list->count * CS2D_SV_STAGGER_MS);
    list->count++;
    return sv;
}

/* Tsv.refresh (0x14002f900): new info round, a single extra ping (pingc = 2) */
void cs2d_sv_refresh(cs2d_sv_t *sv, uint32_t now_ms)
{
    if (!sv) return;
    sv->active     = false;
    sv->info_done  = false;
    sv->info_reqs  = 0;
    sv->ping_count = 2;
    sv->ping_time  = now_ms - CS2D_SV_RETRY_MS;
}

/*
 * offlineserverlist_load (0x1400f25d0): skip the comment line, then ReadInt ip + ReadShort port.
 * BlitzMax ReadInt is little-endian and its IPs keep the first octet in the MSB, so the bytes
 * of an entry are d, c, b, a (svl.cache "ae 03 e6 17 65 90" = 23.230.3.174:36965).
 */
int cs2d_sv_parse_cache(const uint8_t *data, size_t len, cs2d_sv_list_t *list, uint32_t now_ms)
{
    if (!data || !list) return 0;
    size_t pos = 0;
    while (pos < len && data[pos] != '\n') pos++;
    if (pos >= len) return 0;
    pos++;

    int added = 0;
    for (; pos + 6 <= len; pos += 6) {
        uint32_t ip = (uint32_t)data[pos] | ((uint32_t)data[pos + 1] << 8) |
                      ((uint32_t)data[pos + 2] << 16) | ((uint32_t)data[pos + 3] << 24);
        uint16_t port = (uint16_t)(data[pos + 4] | (data[pos + 5] << 8));
        if (ip == 0 || port == 0) continue;
        int before = list->count;
        if (cs2d_sv_list_add(list, ip, port, now_ms) && list->count > before) added++;
    }
    return added;
}

int cs2d_sv_load_cache(const char *path, cs2d_sv_list_t *list, uint32_t now_ms)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    uint8_t buf[CS2D_SV_MAX * 6 + 256];
    size_t len = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    return cs2d_sv_parse_cache(buf, len, list, now_ms);
}

/* loadfavs (0x140083730) / loadrecent (0x140085090): "ip:port" lines */
int cs2d_sv_load_address_file(const char *path, cs2d_sv_list_t *list, uint32_t now_ms)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[128];
    int added = 0;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '/' && line[1] == '/') continue;
        uint32_t ip;
        uint16_t port;
        if (!cs2d_sv_parse_address(line, &ip, &port)) continue;
        int before = list->count;
        if (cs2d_sv_list_add(list, ip, port, now_ms) && list->count > before) added++;
    }
    fclose(f);
    return added;
}

/*
 * Info request (pingServers 0x1403012d0, LAN scan 0x14009da30):
 * [01 00] unreliable header, [03][tick u16] control triple (tick = 100 ms units since start),
 * [FB][mode][version u16]. The dedicated sets flags bit 4 when the version is 1018.
 */
size_t cs2d_sv_build_info_request(uint8_t mode, uint16_t tick, uint8_t *out, size_t max_len)
{
    if (!out || max_len < 9) return 0;
    out[0] = 0x01;
    out[1] = 0x00;
    out[2] = 0x03;
    out[3] = (uint8_t)(tick & 0xFF);
    out[4] = (uint8_t)(tick >> 8);
    out[5] = 0xFB;
    out[6] = mode;
    out[7] = (uint8_t)(CS2D_SV_VERSION & 0xFF);
    out[8] = (uint8_t)(CS2D_SV_VERSION >> 8);
    return 9;
}

/* Ping (pingServers): [01 00][FA][millisecs i32]; the server echoes the 7 bytes */
size_t cs2d_sv_build_ping(int32_t millisecs, uint8_t *out, size_t max_len)
{
    if (!out || max_len < 7) return 0;
    uint32_t v = (uint32_t)millisecs;
    out[0] = 0x01;
    out[1] = 0x00;
    out[2] = 0xFA;
    out[3] = (uint8_t)(v & 0xFF);
    out[4] = (uint8_t)((v >> 8) & 0xFF);
    out[5] = (uint8_t)((v >> 16) & 0xFF);
    out[6] = (uint8_t)((v >> 24) & 0xFF);
    return 7;
}

static bool read_bstr(const uint8_t *msg, size_t len, size_t *pos, char *out, size_t out_size)
{
    if (*pos >= len) return false;
    size_t n = msg[(*pos)++];
    if (*pos + n > len) return false;
    size_t copy = (n < out_size - 1) ? n : out_size - 1;
    memcpy(out, msg + *pos, copy);
    out[copy] = '\0';
    *pos += n;
    return true;
}

/*
 * Info reply, parser at 0x1402d2d80 (msg starts at the 0xFB opcode):
 *   [FB][mode 1|2][flags u8][name u8 len + text][map u8 len + text][players u8][max u8]
 *   ([gamemode u8] when flags bit 5)[bots u8][flags2 u8]
 * flags: 0 password, 1 U.S.G.N. only, 2 fog of war, 3 friendly fire, 4 same version,
 *        5 game mode present, 6 Lua scripts, 7 force light.
 * flags2: 0 recoil influences accuracy, 1 off-screen damage, 2 server transfers.
 */
bool cs2d_sv_parse_info(cs2d_sv_t *sv, const uint8_t *msg, size_t len)
{
    if (!sv || !msg || len < 3 || msg[0] != 0xFB || (msg[1] != 1 && msg[1] != 2)) return false;
    size_t pos = 2;
    uint8_t flags = msg[pos++];
    sv->password      = (flags & 0x01) != 0;
    sv->usgn_only     = (flags & 0x02) != 0;
    sv->fog           = (flags & 0x04) != 0;
    sv->friendly_fire = (flags & 0x08) != 0;
    sv->same_version  = (flags & 0x10) != 0;
    sv->lua           = (flags & 0x40) != 0;
    sv->light         = (flags & 0x80) != 0;
    sv->gamemode      = 0;
    sv->dyn_recoil    = false;
    sv->offscreen_damage = false;
    sv->transfers     = false;

    if (!read_bstr(msg, len, &pos, sv->name, sizeof(sv->name))) return false;
    if (pos < len && !read_bstr(msg, len, &pos, sv->map, sizeof(sv->map))) return false;
    if (pos + 2 <= len) {
        sv->players     = msg[pos++];
        sv->max_players = msg[pos++];
        if ((flags & 0x20) && pos < len) sv->gamemode = msg[pos++];
        if (pos < len) {
            sv->bots = msg[pos++];
            if (sv->bots > sv->max_players) sv->bots = sv->max_players;
        }
        if (pos < len) {
            uint8_t flags2 = msg[pos++];
            sv->dyn_recoil       = (flags2 & 0x01) != 0;
            sv->offscreen_damage = (flags2 & 0x02) != 0;
            sv->transfers        = (flags2 & 0x04) != 0;
        }
    }

    sv->info_done = true;
    sv->active    = true;
    if (sv->max_players > 32) {   /* 0x1402d2fa8: oversized lists are clamped and hidden */
        sv->max_players = 32;
        sv->active      = false;
    }
    if (sv->players > sv->max_players) sv->players = sv->max_players;
    if (!sv->got_ping) sv->ping_count = 0;
    return true;
}

/* Ping echo (case 0xFA at 0x1402d24f0): average of the last two non-zero samples */
void cs2d_sv_apply_ping(cs2d_sv_t *sv, int32_t sent_ms, uint32_t now_ms)
{
    if (!sv) return;
    sv->pings[1] = sv->pings[0];
    sv->pings[0] = (int)(now_ms - (uint32_t)sent_ms);
    int n = 0, sum = 0;
    for (int i = 0; i < 2; i++) {
        if (sv->pings[i] != 0) {
            sum += sv->pings[i];
            n++;
        }
    }
    sv->ping       = (n > 0) ? sum / n : 0;
    if (sv->ping < 0) sv->ping = 0;
    sv->ping_blend = 255.0f;
    sv->got_ping   = true;
}

/* pingServers (0x1403012d0), one entry: every 3 s up to 3 pings and up to 5 info requests */
void cs2d_sv_schedule(cs2d_sv_t *sv, uint32_t now_ms, bool *send_ping, bool *send_info)
{
    *send_ping = false;
    *send_info = false;
    if (!sv || (int32_t)(now_ms - sv->ping_time) <= CS2D_SV_RETRY_MS) return;
    sv->ping_time = now_ms;
    if (sv->ping_count < CS2D_SV_PINGS) {
        sv->ping_count++;
        *send_ping = true;
    }
    /* After 3 unanswered requests the original also asks U.S.G.N. to poke the server
     * (FUN_140301220: [0x12][ip][port]); there is no U.S.G.N. connection here. */
    if (!sv->info_done && sv->info_reqs < CS2D_SV_INFO_TRIES) {
        sv->info_reqs++;
        *send_info = true;
    }
}

int cs2d_sv_humans(const cs2d_sv_t *sv)
{
    int h = sv->players - sv->bots;
    return (h > 0) ? h : 0;
}

/* Default order of the original list ("Players" column): human players, descending; stable */
void cs2d_sv_sort_by_humans(cs2d_sv_list_t *list)
{
    if (!list) return;
    for (int i = 1; i < list->count; i++) {
        cs2d_sv_t tmp = list->entries[i];
        int key = cs2d_sv_humans(&tmp);
        int j = i - 1;
        while (j >= 0 && cs2d_sv_humans(&list->entries[j]) < key) {
            list->entries[j + 1] = list->entries[j];
            j--;
        }
        list->entries[j + 1] = tmp;
    }
}

/* ---------------------------------------------------------------------------------------------
 * Browser socket
 * ------------------------------------------------------------------------------------------- */

static void browser_send(cs2d_sv_browser_t *b, uint32_t ip, uint16_t port, const uint8_t *data, size_t len)
{
    if (!b->open || len == 0) return;
    struct sockaddr_in to;
    memset(&to, 0, sizeof(to));
    to.sin_family      = AF_INET;
    to.sin_port        = htons(port);
    to.sin_addr.s_addr = htonl(ip);
    sendto(b->sock, (const char *)data, (int)len, 0, (struct sockaddr *)&to, sizeof(to));
}

static uint16_t browser_tick(const cs2d_sv_browser_t *b)
{
    return (uint16_t)(((uint32_t)GetTickCount() - b->epoch_ms) / 100u);
}

void cs2d_sv_browser_open(cs2d_sv_browser_t *b, uint32_t now_ms)
{
    if (!b || b->open) return;
    if (!cs2d_platform_net_init()) return;
    b->sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (b->sock == INVALID_SOCKET) return;
    cs2d_platform_set_nonblocking(b->sock);
    int broadcast = 1;
    setsockopt(b->sock, SOL_SOCKET, SO_BROADCAST, (const char *)&broadcast, sizeof(broadcast));
    b->open        = true;
    b->open_ms     = now_ms;
    b->lan_scan_ms = now_ms - CS2D_SV_LAN_SCAN_MS;
    if (b->epoch_ms == 0) b->epoch_ms = (uint32_t)GetTickCount();
}

void cs2d_sv_browser_close(cs2d_sv_browser_t *b)
{
    if (!b || !b->open) return;
    closesocket(b->sock);
    b->sock = INVALID_SOCKET;
    b->open = false;
}

void cs2d_sv_browser_refresh(cs2d_sv_browser_t *b, int tab, uint32_t now_ms)
{
    if (!b || tab < 0 || tab >= CS2D_SV_TAB_COUNT) return;
    cs2d_sv_list_t *list = &b->lists[tab];
    for (int i = 0; i < list->count; i++) cs2d_sv_refresh(&list->entries[i], now_ms);
    if (tab == CS2D_SV_TAB_LAN) b->lan_scan_ms = now_ms - CS2D_SV_LAN_SCAN_MS;
    b->open_ms = now_ms;
}

static void check_existing_map(cs2d_sv_t *sv)
{
    char path[128];
    snprintf(path, sizeof(path), "maps/%s.map", sv->map);
    FILE *f = (sv->map[0] && !strpbrk(sv->map, "/\\:")) ? fopen(path, "rb") : NULL;
    sv->existing_map = (f != NULL);
    if (f) fclose(f);
}

static void handle_datagram(cs2d_sv_browser_t *b, uint32_t ip, uint16_t port,
                            const uint8_t *data, int len, uint32_t now_ms)
{
    if (len < 3) return;
    const uint8_t *msg = data + 2;   /* Skip the [seq u16] header */
    size_t mlen = (size_t)len - 2;

    if (msg[0] == 0xFA && mlen >= 5) {
        int32_t sent = (int32_t)((uint32_t)msg[1] | ((uint32_t)msg[2] << 8) |
                                 ((uint32_t)msg[3] << 16) | ((uint32_t)msg[4] << 24));
        for (int t = 0; t < CS2D_SV_TAB_COUNT; t++) {
            cs2d_sv_t *sv = cs2d_sv_list_find(&b->lists[t], ip, port);
            if (sv) cs2d_sv_apply_ping(sv, sent, now_ms - b->epoch_ms);
        }
        return;
    }
    if (msg[0] != 0xFB || mlen < 3 || (msg[1] != 1 && msg[1] != 2)) return;

    /* Mode 2 answers the LAN broadcast: the server joins the LAN list (+0x88 lan = 1) */
    if (msg[1] == 2) {
        cs2d_sv_t *lan = cs2d_sv_list_add(&b->lists[CS2D_SV_TAB_LAN], ip, port, now_ms);
        if (lan) lan->lan = true;
    }
    /* The reply is copied to the same address in every list (0x1402d30b8 loop) */
    for (int t = 0; t < CS2D_SV_TAB_COUNT; t++) {
        cs2d_sv_t *sv = cs2d_sv_list_find(&b->lists[t], ip, port);
        if (sv && cs2d_sv_parse_info(sv, msg, mlen)) check_existing_map(sv);
    }
}

void cs2d_sv_browser_update(cs2d_sv_browser_t *b, int tab, uint32_t now_ms, float frames)
{
    if (!b || !b->open) return;

    uint8_t buf[1024];
    for (int guard = 0; guard < 256; guard++) {
        struct sockaddr_in from;
        cs2d_socklen_t from_len = sizeof(from);
        int n = recvfrom(b->sock, (char *)buf, sizeof(buf), 0, (struct sockaddr *)&from, &from_len);
        if (n <= 0) break;
        handle_datagram(b, ntohl(from.sin_addr.s_addr), ntohs(from.sin_port), buf, n, now_ms);
    }

    uint8_t pkt[16];
    /* LAN scan (0x14009da30): broadcast [FB][2] to the LAN port */
    if ((int32_t)(now_ms - b->lan_scan_ms) >= CS2D_SV_LAN_SCAN_MS) {
        b->lan_scan_ms = now_ms;
        size_t len = cs2d_sv_build_info_request(2, browser_tick(b), pkt, sizeof(pkt));
        browser_send(b, 0xFFFFFFFFu, CS2D_SV_LAN_PORT, pkt, len);
    }

    if (tab < 0 || tab >= CS2D_SV_TAB_COUNT) return;
    cs2d_sv_list_t *list = &b->lists[tab];
    for (int i = 0; i < list->count; i++) {
        cs2d_sv_t *sv = &list->entries[i];
        bool send_ping, send_info;
        cs2d_sv_schedule(sv, now_ms, &send_ping, &send_info);
        if (send_ping) {
            /* MilliSecs() of the original: time since the client started */
            size_t len = cs2d_sv_build_ping((int32_t)(now_ms - b->epoch_ms), pkt, sizeof(pkt));
            browser_send(b, sv->ip, sv->port, pkt, len);
        }
        if (send_info) {
            size_t len = cs2d_sv_build_info_request(1, browser_tick(b), pkt, sizeof(pkt));
            browser_send(b, sv->ip, sv->port, pkt, len);
        }
        /* pingblend fades by 25 per 60 Hz frame */
        if (sv->ping_blend > 0.0f) {
            sv->ping_blend -= 25.0f * frames;
            if (sv->ping_blend < 0.0f) sv->ping_blend = 0.0f;
        }
    }
}
