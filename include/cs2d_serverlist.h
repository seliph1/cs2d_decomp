#ifndef CS2D_SERVERLIST_H
#define CS2D_SERVERLIST_H

#include "cs2d_platform.h"
#include "cs2d_types.h"

/*
 * Server browser ("Find Servers"), reconstructed from CS2D.exe:
 *   class Tsv (debug scope 0x1409f04a0): one server entry
 *   pingServers (0x1403012d0): per-server ping / info request scheduler
 *   offlineserverlist_load (0x1400f25d0): sys/core/svl.cache reader
 *   out-of-band dispatcher 0x1402d22c0 (jump table 0x140bc8010): 0xFA ping echo, 0xFB info reply
 */

#define CS2D_SV_MAX           512
#define CS2D_SV_LAN_PORT      36963  /* DAT_140a14ca4 */
#define CS2D_SV_VERSION       1018   /* 0x3fa, sent with every info request */
#define CS2D_SV_STAGGER_MS    30     /* DAT_140a14ca8: first query of entry n waits n * 30 ms */
#define CS2D_SV_RETRY_MS      3000   /* pingServers: one ping / info per server every 3 s */
#define CS2D_SV_PINGS         3      /* pingc < 3 */
#define CS2D_SV_INFO_TRIES    5      /* inforeqs < 5 */
#define CS2D_SV_LAN_SCAN_MS   5000   /* Proxy capture: [FB][2] broadcast every ~5 s */

typedef enum {
    CS2D_SV_TAB_INTERNET  = 0,
    CS2D_SV_TAB_LAN       = 1,
    CS2D_SV_TAB_FAVORITES = 2,
    CS2D_SV_TAB_RECENT    = 3,
    CS2D_SV_TAB_COUNT     = 4
} cs2d_sv_tab_t;

/* Tsv fields (offsets in the original object in comments) */
typedef struct {
    uint32_t ip;               /* +0x08, BlitzMax order: first octet in the most significant byte */
    uint16_t port;             /* +0x0c */
    bool     active;           /* +0x10: an info reply arrived */
    bool     info_done;        /* +0x14 inforeqstate */
    int      info_reqs;        /* +0x18 inforeqs */
    bool     password;         /* +0x1c flags bit 0 */
    bool     usgn_only;        /* +0x20 flags bit 1 */
    bool     fog;              /* +0x24 flags bit 2 */
    bool     light;            /* +0x28 flags bit 7 (force light) */
    bool     dyn_recoil;       /* +0x2c flags2 bit 0 */
    bool     offscreen_damage; /* +0x30 flags2 bit 1 */
    bool     transfers;        /* +0x34 flags2 bit 2 */
    char     name[64];         /* +0x38 */
    char     map[64];          /* +0x40 */
    int      players;          /* +0x48 */
    int      max_players;      /* +0x4c */
    bool     existing_map;     /* +0x50: maps/<map>.map exists locally */
    bool     friendly_fire;    /* +0x54 flags bit 3 */
    bool     same_version;     /* +0x58 flags bit 4: the request carried version 1018 */
    int      gamemode;         /* +0x5c: 0 Standard, 1 Deathmatch, 2 Team DM, 3 Construction, 4 Zombies */
    bool     lua;              /* +0x60 flags bit 6 */
    int      bots;             /* +0x64 */
    int      ping;             /* +0x68 */
    int      pings[2];         /* +0x70: last two samples, 0 = none */
    uint32_t ping_time;        /* +0x78 pingt: last query time (ms) */
    int      ping_count;       /* +0x7c pingc */
    bool     got_ping;         /* +0x80 */
    float    ping_blend;       /* +0x84: 255 on a fresh echo, -25 per 60 Hz frame */
    bool     lan;              /* +0x88 */
} cs2d_sv_t;

typedef struct {
    cs2d_sv_t entries[CS2D_SV_MAX];
    int       count;
} cs2d_sv_list_t;

typedef struct {
    cs2d_sv_list_t lists[CS2D_SV_TAB_COUNT];
    SOCKET         sock;
    bool           open;
    bool           using_cache;   /* Internet list comes from sys/core/svl.cache (no U.S.G.N.) */
    uint32_t       open_ms;       /* When the browser was opened (Loading Serverlist timeout) */
    uint32_t       lan_scan_ms;
    uint32_t       epoch_ms;      /* DAT_140ddf6c0: base of the 100 ms tick in the request header */
} cs2d_sv_browser_t;

/* Addresses */
uint32_t cs2d_sv_ip_from_octets(uint8_t a, uint8_t b, uint8_t c, uint8_t d);
void     cs2d_sv_ip_to_str(uint32_t ip, char out[16]);
bool     cs2d_sv_parse_address(const char *text, uint32_t *out_ip, uint16_t *out_port);

/* Lists (Tsv.get / Tsv.addNoCheck / Tsv.refresh) */
cs2d_sv_t *cs2d_sv_list_find(cs2d_sv_list_t *list, uint32_t ip, uint16_t port);
cs2d_sv_t *cs2d_sv_list_add(cs2d_sv_list_t *list, uint32_t ip, uint16_t port, uint32_t now_ms);
void       cs2d_sv_refresh(cs2d_sv_t *sv, uint32_t now_ms);

/* sys/core/svl.cache: "// Serverlist cached on ...\r\n" + n * {ip i32 LE, port u16 LE} */
int  cs2d_sv_parse_cache(const uint8_t *data, size_t len, cs2d_sv_list_t *list, uint32_t now_ms);
int  cs2d_sv_load_cache(const char *path, cs2d_sv_list_t *list, uint32_t now_ms);
/* sys/favorites.lst, sys/recentlyjoined.lst: one "ip:port" per line, "//" comments */
int  cs2d_sv_load_address_file(const char *path, cs2d_sv_list_t *list, uint32_t now_ms);

/* Out-of-band packets (whole datagrams, header included) */
size_t cs2d_sv_build_info_request(uint8_t mode, uint16_t tick, uint8_t *out, size_t max_len);
size_t cs2d_sv_build_ping(int32_t millisecs, uint8_t *out, size_t max_len);
bool   cs2d_sv_parse_info(cs2d_sv_t *sv, const uint8_t *msg, size_t len);
void   cs2d_sv_apply_ping(cs2d_sv_t *sv, int32_t sent_ms, uint32_t now_ms);

/* pingServers decision for one entry: what to send now (updates the counters) */
void cs2d_sv_schedule(cs2d_sv_t *sv, uint32_t now_ms, bool *send_ping, bool *send_info);

/* Display helpers */
int  cs2d_sv_humans(const cs2d_sv_t *sv);
void cs2d_sv_sort_by_humans(cs2d_sv_list_t *list);

/* Browser: socket, LAN scan, queries for the visible tab, replies */
void cs2d_sv_browser_open(cs2d_sv_browser_t *b, uint32_t now_ms);
void cs2d_sv_browser_close(cs2d_sv_browser_t *b);
void cs2d_sv_browser_refresh(cs2d_sv_browser_t *b, int tab, uint32_t now_ms);
void cs2d_sv_browser_update(cs2d_sv_browser_t *b, int tab, uint32_t now_ms, float frames);

#endif /* CS2D_SERVERLIST_H */
