#ifndef CS2D_NETWORK_H
#define CS2D_NETWORK_H

#include "cs2d_types.h"

/* CS2D Protocol Opcodes (verified from cs2d_dedicated.exe via REA & cs2d_netcode_codes.txt) */
typedef enum {
    CS2D_NET_FIRE                  = 7,
    CS2D_NET_ATTACK2               = 8,
    CS2D_NET_SELECT_WEAPON         = 9,
    CS2D_NET_POS_RUN               = 10,
    CS2D_NET_POS_WALK              = 11,
    CS2D_NET_ROT                   = 12,
    CS2D_NET_POSROT                = 13,
    CS2D_NET_POSROT2               = 14,
    CS2D_NET_SETPOS                = 15,
    CS2D_NET_RELOAD                = 16,
    CS2D_NET_DAMAGE                = 17,
    CS2D_NET_KILL                  = 19,
    CS2D_NET_TEAM                  = 20,
    CS2D_NET_SPAWN                 = 21,
    CS2D_NET_ROUNDSTART            = 22,
    CS2D_NET_BUY                   = 23,
    CS2D_NET_DROP                  = 24,
    CS2D_NET_COLLECT               = 25,
    CS2D_NET_USE                   = 26,
    CS2D_NET_PROJECTILE            = 27,
    CS2D_NET_SPRAY                 = 28,
    CS2D_NET_HOSTAGE               = 29,
    CS2D_NET_PROCESS               = 30,
    CS2D_NET_ENTITYEVENT           = 31,
    CS2D_NET_SPECPOS               = 32,
    CS2D_NET_SPAWNITEM             = 33,
    CS2D_NET_BUILD                 = 34,
    CS2D_NET_DYNOBJ_REMOVE         = 35,
    CS2D_NET_DYNOBJ_MODE           = 36,
    CS2D_NET_DYNOBJ_HEALTH         = 37,
    CS2D_NET_DYNOBJ_DATA           = 38,
    CS2D_NET_RESPAWN_REQ           = 39,
    CS2D_NET_KILL_DROPPED_WEP      = 40,
    CS2D_NET_KILL_PROJECTILE       = 41,
    CS2D_NET_PLAYERDATA            = 42,
    CS2D_NET_VOTE                  = 43,
    CS2D_NET_MOVE_R                = 44,
    CS2D_NET_MOVE_W                = 45,
    CS2D_NET_MOVE_R_DIR            = 46,
    CS2D_NET_FX                    = 48,
    CS2D_NET_DO_SPAWN              = 49,
    CS2D_NET_PORTALFIRE            = 50,
    CS2D_NET_DYNAMICIMAGE          = 51,
    CS2D_NET_SPAWN_GND_PROJ        = 52,
    CS2D_NET_CLIENTDATA            = 53,
    CS2D_NET_TWEEN                 = 54,
    CS2D_NET_WEAPON_IMPULSE        = 56,
    CS2D_NET_FLASHLIGHT            = 57,
    CS2D_NET_IMAGEHITZONE          = 59,
    CS2D_NET_SPECFLASH             = 60,
    CS2D_NET_SETAMMO               = 61,
    CS2D_NET_SETTILE               = 62,
    CS2D_NET_MOVE_PROJECTILE       = 63,
    CS2D_NET_SETTEAMSCORES         = 64,
    CS2D_NET_POSITIONALSOUND       = 65,
    CS2D_NET_SETBACKGROUND         = 66,
    CS2D_NET_CLIENTSPEC            = 67,
    CS2D_NET_FIREDIRECTION         = 68,
    CS2D_NET_WEAPON_DATA           = 69,
    CS2D_NET_ROT_CLIENT            = 70, /* Client -> Server rotation: [70, angle u16, aim distance / 2 u8] */
    CS2D_NET_PHYS_PLAYERS_UPDATE   = 80,
    CS2D_NET_PHYS_ITEMS_UPDATE     = 81,
    CS2D_NET_PHYS_PROJ_UPDATE      = 82,
    CS2D_NET_PHYS_PLATFORMS_UPDATE = 83,
    CS2D_NET_PHYS_CONTROLLER_UPDATE= 84,
    CS2D_NET_PHYS_PARSE            = 85,
    CS2D_NET_PHYS_OBJECTS_UPDATE   = 86,
    CS2D_NET_NOTIFICATION          = 97,
    CS2D_NET_CONNECT               = 98,
    CS2D_NET_JOIN_LEGACY           = 100,
    CS2D_NET_PING                  = 101,
    CS2D_NET_PING_RESULT           = 102,
    CS2D_NET_LOBBY_DATA            = 110,
    CS2D_NET_SCOREBOARD_ENTRY      = 112,
    CS2D_NET_SERVERSTATE           = 115,
    CS2D_NET_LUA_BIND_DATA         = 224,
    CS2D_NET_VOICE_CHAT            = 225,
    CS2D_NET_MSG                   = 226,
    CS2D_NET_LUABIND_PRESS         = 227,
    CS2D_NET_LUABIND_RELEASE       = 228,
    CS2D_NET_CLOCK_PING            = 229,
    CS2D_NET_LEADERBOARD           = 230,
    CS2D_NET_CLIENTSETTINGS        = 232,
    CS2D_NET_HUDTXT                = 234,
    CS2D_NET_LUAMENU               = 235,
    CS2D_NET_RCONCHECK             = 236,
    CS2D_NET_CHANGESETTINGS        = 237,
    CS2D_NET_CHANGENAME            = 238,
    CS2D_NET_CHANGEMAP             = 239,
    CS2D_NET_MSG2                  = 240,
    CS2D_NET_RCON                  = 241,
    CS2D_NET_PUBLIC_RCON           = 242,
    CS2D_NET_PINGLIST              = 247,
    CS2D_NET_PLAYERJOINS           = 248,
    CS2D_NET_PING_INGAME           = 249,
    CS2D_NET_PING_SERVERLIST       = 250,
    CS2D_NET_INFO                  = 251,
    CS2D_NET_JOIN                  = 252,
    CS2D_NET_LEAVE                 = 253
} cs2d_net_opcode_t;

/* Authentic rotation conversion factor from FUN_1401cbce0 & FUN_1401cc0b0 (360.0f / 65536.0f) */
#define CS2D_NET_ROT_SCALE 0.005493164f

/* Server Info Response (FUN_1401dac80 - recv 251 info) */
typedef struct {
    uint8_t     mode;
    uint8_t     flags;
    char        server_name[64];
    char        map_name[32];
    uint8_t     players_current;
    uint8_t     players_max;
    uint8_t     gamemode;
    uint8_t     bots;
    uint8_t     flags2;      /* 0 recoil, 1 off-screen damage, 2 server transfers */
    bool        password_protected;
    uint16_t    port;
} cs2d_net_server_info_t;

/* Parse an incoming packet opcode */
uint8_t cs2d_net_get_opcode(const uint8_t *packet, size_t len);

/* Build Opcode 250 Serverlist Ping packet (FUN_1401da1e0: [250, int32_t timestamp]) */
size_t cs2d_net_build_ping_serverlist(int32_t timestamp, uint8_t *out_buf, size_t max_len);
bool cs2d_net_parse_ping_serverlist(const uint8_t *packet, size_t len, int32_t *out_timestamp);

/* Build Opcode 251 Server Info Request / Response (FUN_1401dac80) */
size_t cs2d_net_build_info_request(uint8_t mode, uint8_t *out_buf, size_t max_len);
size_t cs2d_net_build_info_packet(
    const cs2d_net_server_info_t *info,
    uint8_t                      *out_buf,
    size_t                        max_len
);
bool cs2d_net_parse_info_packet(
    const uint8_t          *packet,
    size_t                  len,
    cs2d_net_server_info_t *out_info
);

/* Opcode 10 (pos run) / Opcode 11 (pos walk) - FUN_1401cb570 */
size_t cs2d_net_build_pos_packet(
    bool     walk,
    uint8_t  player_id, /* 0 = client->server (5 bytes), >0 = server->client (6 bytes) */
    float    x,
    float    y,
    uint8_t *out_buf,
    size_t   max_len
);

/* Opcode 12 (rot) - FUN_1401cbce0 */
size_t cs2d_net_build_rot_packet(
    uint8_t  player_id, /* 0 = client->server (3 bytes), >0 = server->client (4 bytes) */
    float    rot,
    uint8_t *out_buf,
    size_t   max_len
);

/* Opcode 13 (posrot) - FUN_1401cc0b0 */
size_t cs2d_net_build_posrot_packet(
    uint8_t  player_id, /* 0 = client->server (7 bytes), >0 = server->client (8 bytes) */
    float    x,
    float    y,
    float    rot,
    uint8_t *out_buf,
    size_t   max_len
);
bool cs2d_net_parse_posrot_packet(
    const uint8_t *packet,
    size_t         len,
    uint8_t       *out_player_id,
    float         *out_x,
    float         *out_y,
    float         *out_rot
);

/* Opcode 17 (damage) - FUN_1401cc9f0 & FUN_1401658c0:
   Server -> Client: [17, victim_id, attacker_id, new_health, new_armor] (5 bytes) */
size_t cs2d_net_build_damage_packet(
    uint8_t  victim_id,
    uint8_t  attacker_id,
    uint8_t  new_health,
    uint8_t  new_armor,
    uint8_t *out_buf,
    size_t   max_len
);
bool cs2d_net_parse_damage_packet(
    const uint8_t *packet,
    size_t         len,
    uint8_t       *out_victim_id,
    uint8_t       *out_attacker_id,
    uint8_t       *out_new_health,
    uint8_t       *out_new_armor
);

/* Opcode 19 (kill) - FUN_1401ccc90:
   Server -> Client: [19, victim_id, killer_id, weapon_id, flag, uint16_t x, uint16_t y] (9 bytes) */
size_t cs2d_net_build_kill_packet(
    uint8_t  victim_id,
    uint8_t  killer_id,
    uint8_t  weapon_id,
    uint8_t  flag,
    uint16_t victim_x,
    uint16_t victim_y,
    uint8_t *out_buf,
    size_t   max_len
);
bool cs2d_net_parse_kill_packet(
    const uint8_t *packet,
    size_t         len,
    uint8_t       *out_victim_id,
    uint8_t       *out_killer_id,
    uint8_t       *out_weapon_id,
    uint8_t       *out_flag,
    uint16_t      *out_victim_x,
    uint16_t      *out_victim_y
);

/* Opcode 20 (team) - FUN_1401cd140:
   Client -> Server: [20, team, look] (3 bytes)
   Server -> Client: [20, player_id, team, look] (4 bytes) */
size_t cs2d_net_build_team_packet(
    uint8_t  player_id,
    uint8_t  team,
    uint8_t  look,
    uint8_t *out_buf,
    size_t   max_len
);

/* Opcode 21 (spawn) - FUN_1401cd470 & FUN_140160740 */
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
);
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
);

/* --- Authentic CS2D Tudp Stream & Crypto/Handshake Helpers --- */
typedef struct {
    uint8_t buf[2048];
    int     pos;
} cs2d_pkt_t;

void    cs2d_pkt_w_byte(cs2d_pkt_t *p, uint8_t b);
void    cs2d_pkt_w_short(cs2d_pkt_t *p, uint16_t s);
void    cs2d_pkt_w_int(cs2d_pkt_t *p, int32_t v);
void    cs2d_pkt_w_long(cs2d_pkt_t *p, uint64_t v);
void    cs2d_pkt_w_str(cs2d_pkt_t *p, const char *s);
void    cs2d_pkt_w_raw_str(cs2d_pkt_t *p, const uint8_t *b, int len);
void    cs2d_pkt_w_lstr(cs2d_pkt_t *p, const char *s);
int     cs2d_pkt_r_str(const uint8_t *buf, int len, int *pos, char *out, int max_out);

/* Authentic CS2D Crypto & Hash Functions (0x140055400, 0x1401be680, 0x1401bef60) */
void    cs2d_net_md5_hex(const char *str, char out_hex[33]);
bool    cs2d_net_sha256_file(const char *filepath, char out_hex[65]);
void    cs2d_net_encrypt(const char *txt, const char *key, uint8_t *out, int *out_len);
void    cs2d_net_secret(const char *raw_secret, char *out);

/* Opcode 252 (CS2D_NET_JOIN) 4-Stage Handshake Builders (0x1402ce900 & 0x1401dc530) */
void    cs2d_net_build_join_stage0(cs2d_pkt_t *p);
void    cs2d_net_build_join_stage1(
    cs2d_pkt_t *p,
    const char *player_name,
    const char *password,
    const char *raw_secret,
    int         screen_w,
    int         screen_h,
    int         map_mode      /* DAT_140ddf3dc: 0 normal, 1 temp.map, 2 overwrite */
);
void    cs2d_net_build_join_stage2(
    cs2d_pkt_t *p,
    const char *map_checksum,
    const char *raw_secret,
    int         download      /* "download" setting DAT_140a14f4c */
);
void    cs2d_net_build_join_stage3(
    cs2d_pkt_t *p,
    const char *map_name,
    const char *map_checksum
);

#endif /* CS2D_NETWORK_H */
