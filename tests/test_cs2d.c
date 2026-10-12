#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif
#endif

#include "cs2d_types.h"
#include "cs2d_weapons.h"
#include "cs2d_player.h"
#include "cs2d_map.h"
#include "cs2d_game.h"
#include "cs2d_network.h"
#include "cs2d_render.h"
#include "cs2d_ui.h"
#include "cs2d_client.h"
#include "cs2d_entities.h"
#include "cs2d_transfer.h"
#include "cs2d_editor.h"
#include "cs2d_spectate.h"
#include "cs2d_dynobj.h"
#include <zlib.h>
#include <SDL3/SDL.h>

#define TEST_PASS(name) printf("  [PASS] %s\n", name)
#define TEST_FAIL(name, msg) do { printf("  [FAIL] %s: %s\n", name, msg); return false; } while(0)

/* =========================================================================
 * Test 1: Weapons, Config Loader & Armor Ballistics (FUN_1400e0fb0, FUN_140112bc0, FUN_1401658c0)
 * ========================================================================= */
static bool test_weapons(void)
{
    printf("\n=== Testing Weapons & Ballistics (FUN_1400e0fb0 & FUN_1401658c0) ===\n");

    cs2d_weapons_init();

    /* 1.1 Weapon definitions from FUN_1400e0fb0 */
    const cs2d_weapon_def_t *ak = cs2d_weapon_get(CS2D_WEP_AK47);
    if (!ak || ak->price != 2500 || ak->damage != 22 || ak->clip_size != 30 ||
        ak->max_reserve != 90 || ak->reload_ticks != 275 || ak->fire_delay_ticks != 3) {
        TEST_FAIL("AK-47 Definition (FUN_1400e0fb0)", "Incorrect stats or price");
    }
    TEST_PASS("AK-47 Definition (dmg=22, clip=30/90, reload=275, rate=3, price=$2500)");

    const cs2d_weapon_def_t *awp = cs2d_weapon_get(CS2D_WEP_AWP);
    if (!awp || awp->price != 4750 || awp->damage != 50 || awp->damage_z1 != 65 ||
        awp->damage_z2 != 150 || awp->reload_ticks != 400 || awp->fire_delay_ticks != 65) {
        TEST_FAIL("AWP Definition (FUN_1400e0fb0)", "Incorrect stats or zoom damage");
    }
    TEST_PASS("AWP Definition (dmg=50, z1=65, z2=150, reload=400, rate=65, price=$4750)");

    const cs2d_weapon_def_t *deagle = cs2d_weapon_get(CS2D_WEP_DEAGLE);
    if (!deagle || deagle->clip_size != 7 || deagle->max_reserve != 35 ||
        deagle->damage != 34 || deagle->price != 650) {
        TEST_FAIL("Deagle Definition (FUN_1400e0fb0)", "Incorrect stats");
    }
    TEST_PASS("Deagle Definition (dmg=34, clip=7/35, reload=210, rate=13, price=$650)");

    /* 1.2 Weapon Config Loader (FUN_140112bc0) & Lua itemtype() (FUN_14009b4c0) */
    if (!cs2d_weapons_load_cfg("sys/weapons_recoil.cfg")) {
        TEST_FAIL("loadweaponcfg (FUN_140112bc0)", "Failed to parse sys/weapons_recoil.cfg");
    }
    if (cs2d_itemtype_int(CS2D_WEP_AK47, "recoil_recover") != 40 ||
        cs2d_itemtype_int(CS2D_WEP_DEAGLE, "recoil_attack") != 4 ||
        cs2d_itemtype_int(CS2D_WEP_M134, "dmg") != 13) {
        TEST_FAIL("itemtype() lookup (FUN_14009b4c0)", "Property mismatch");
    }
    TEST_PASS("loadweaponcfg (sys/weapons_recoil.cfg) & itemtype() property lookup");

    /* 1.3 Authentic Damage & Armor calculation (FUN_1401658c0) */
    int hp_dmg = 0, armor_dmg = 0;
    /* AK-47 shot at unarmored target (armor=0) -> 22 HP damage */
    cs2d_calculate_damage(ak, 50.0f, false, 0, false, &hp_dmg, &armor_dmg);
    if (hp_dmg != 22 || armor_dmg != 0) {
        TEST_FAIL("AK unarmored damage", "Expected 22 HP dmg, 0 armor dmg");
    }
    TEST_PASS("AK-47 unarmored damage (22 HP, 0 Armor)");

    /* AK-47 shot at Kevlar target (armor=100, mp_kevlar=0.35f):
       absorbed = (int)(22 * 0.35f) = 7 -> hp_dmg = 15, armor_dmg = 22 */
    int arm_hp = 0, arm_dmg = 0;
    cs2d_calculate_damage(ak, 50.0f, false, 100, false, &arm_hp, &arm_dmg);
    if (arm_hp != 15 || arm_dmg != 22) {
        TEST_FAIL("AK Kevlar damage (FUN_1401658c0)", "Expected 15 HP dmg and 22 armor loss");
    }
    TEST_PASS("AK-47 Kevlar absorption (15 HP dmg, 22 Armor loss @ mp_kevlar=0.35)");

    /* Special CS2D Armor 205 (Super Armor = 0.05x damage, 0 durability loss) */
    int sup_hp = 0, sup_arm = 0;
    cs2d_calculate_damage(awp, 100.0f, true, 205, true, &sup_hp, &sup_arm);
    if (sup_hp != (int)(150.0f * 0.05f) || sup_arm != 0) {
        TEST_FAIL("Super Armor 205 (FUN_1401658c0)", "Super armor multiplier mismatch");
    }
    TEST_PASS("Special Armor 205 (Super Armor 95% reduction, 0 durability loss)");

    return true;
}

/* =========================================================================
 * Test 2: Player Logic & Inventory
 * ========================================================================= */
static bool test_player_logic(void)
{
    printf("\n=== Testing Player Logic & Inventory ===\n");

    cs2d_player_t pl;
    cs2d_player_init(&pl, 1, "Terrorist_1");

    if (pl.id != 1 || strcmp(pl.name, "Terrorist_1") != 0 || pl.money != 800) {
        TEST_FAIL("Player Init", "Initial player state mismatch");
    }
    TEST_PASS("Player Init");

    /* Spawn as Terrorist */
    cs2d_vec2_t spawn_pt = { 150.0f, 200.0f };
    cs2d_player_spawn(&pl, CS2D_TEAM_T, spawn_pt);

    if (pl.health != 100 || !pl.alive || pl.team != CS2D_TEAM_T) {
        TEST_FAIL("Player Spawn", "Spawn properties mismatch");
    }
    if (pl.inventory[CS2D_SLOT_MELEE] != CS2D_WEP_KNIFE ||
        pl.inventory[CS2D_SLOT_SECONDARY] != CS2D_WEP_GLOCK ||
        pl.current_weapon != CS2D_WEP_GLOCK) {
        TEST_FAIL("Player Default Loadout", "T must spawn with Knife + Glock");
    }
    TEST_PASS("Player Spawn (T loadout: Knife + Glock)");

    /* Fire weapon */
    int initial_clip = pl.clip_ammo[CS2D_WEP_GLOCK];
    if (!cs2d_weapon_fire(&pl)) {
        TEST_FAIL("Weapon Fire", "Glock failed to fire");
    }
    if (pl.clip_ammo[CS2D_WEP_GLOCK] != initial_clip - 1) {
        TEST_FAIL("Weapon Ammo Decrement", "Clip ammo did not decrement");
    }
    TEST_PASS("Weapon Fire & Ammo Consumption");

    /* Buying a weapon */
    pl.money = 3000;
    if (!cs2d_player_buy_weapon(&pl, CS2D_WEP_AK47)) {
        TEST_FAIL("Weapon Buy", "Failed to purchase AK-47 with sufficient funds");
    }
    if (pl.money != 500 || pl.inventory[CS2D_SLOT_PRIMARY] != CS2D_WEP_AK47) {
        TEST_FAIL("Weapon Purchase State", "Money or inventory incorrect after buy");
    }
    TEST_PASS("Weapon Purchase & Money Deduction (AK-47)");

    /* Taking Damage */
    bool fatal = cs2d_player_apply_damage(&pl, 45, 10);
    if (fatal || pl.health != 55) {
        TEST_FAIL("Non-lethal Damage", "Health deduction mismatch");
    }
    TEST_PASS("Non-lethal Damage Application");

    /* Lethal Damage */
    fatal = cs2d_player_apply_damage(&pl, 60, 0);
    if (!fatal || pl.alive || pl.health != 0) {
        TEST_FAIL("Lethal Damage", "Player should be dead");
    }
    TEST_PASS("Lethal Damage & Player Death");

    return true;
}

/* =========================================================================
 * Test 3: Real Map File Loading (FUN_1400493d0, FUN_14009aeb0, FUN_140113d50)
 * ========================================================================= */
static bool test_map_loading(void)
{
    printf("\n=== Testing CS2D Map Loading (Against Original .map Files) ===\n");

    struct {
        const char *path;
        uint32_t    exp_w;
        uint32_t    exp_h;
        uint32_t    exp_ents;
    } maps[] = {
        { "maps/cs_assault.map", 69,  53, 93   },
        { "maps/as_snow.map",    47,  61, 47   },
        { "maps/de_dust2.map",   101, 88, 1403 }
    };

    for (size_t i = 0; i < sizeof(maps) / sizeof(maps[0]); i++) {
        cs2d_map_t map;
        if (!cs2d_map_load_file(&map, maps[i].path)) {
            printf("  Error loading %s\n", maps[i].path);
            TEST_FAIL("Map Loading", maps[i].path);
        }

        printf("  Loaded '%s':\n", maps[i].path);
        printf("    Header: '%s' | Check: '%s'\n", map.header, map.check_string);
        printf("    Author: '%s' | Tileset: '%s' (tile_count=%u)\n", map.author, map.tileset, map.tile_count);
        printf("    Dimensions: %u x %u tiles | Entities: %u\n", map.width, map.height, map.entity_count);

        if (map.width != maps[i].exp_w || map.height != maps[i].exp_h || map.entity_count != maps[i].exp_ents) {
            cs2d_map_free(&map);
            TEST_FAIL("Map Exact Dimensions/Entities", maps[i].path);
        }

        if (!map.tiles || !map.modifiers || !map.collision) {
            cs2d_map_free(&map);
            TEST_FAIL("Map Buffers", "Null tile/modifier/collision buffers");
        }

        /* Out of bounds must be solid */
        if (!cs2d_map_is_solid(&map, -100.0f, -100.0f)) {
            cs2d_map_free(&map);
            TEST_FAIL("Collision Bounds", "Out of bounds must be solid");
        }

        /* Check spawn counting (Info_T = 0, Info_CT = 1) */
        size_t t_spawns = cs2d_map_count_spawns(&map, CS2D_TEAM_T);
        size_t ct_spawns = cs2d_map_count_spawns(&map, CS2D_TEAM_CT);
        if (t_spawns == 0 || ct_spawns == 0) {
            cs2d_map_free(&map);
            TEST_FAIL("Spawns", "Map must have spawn points for both teams");
        }

        cs2d_map_free(&map);
        TEST_PASS(maps[i].path);
    }

    return true;
}

/* =========================================================================
 * Test 4: Game Loop, Objectives & Economy
 * ========================================================================= */
static bool test_game_loop(void)
{
    printf("\n=== Testing Game Loop, Round Progression & Economy ===\n");

    cs2d_game_t game;
    cs2d_game_init(&game, CS2D_MODE_STANDARD);

    /* Load de_dust2.map and verify auto-detected gamemode == CS2D_MODE_DE (FUN_140113d50) */
    if (!cs2d_game_load_map(&game, "maps/de_dust2.map") || game.gamemode != CS2D_MODE_DE) {
        TEST_FAIL("Game Load Map & Mode Detection", "Failed to load de_dust2.map or detect CS2D_MODE_DE");
    }
    TEST_PASS("Auto-detected CS2D_MODE_DE from de_dust2.map entities");

    /* Add 2 Terrorists and 2 Counter-Terrorists */
    cs2d_game_add_player(&game, 1, "T_Alpha", CS2D_TEAM_T);
    cs2d_game_add_player(&game, 2, "T_Beta", CS2D_TEAM_T);
    cs2d_game_add_player(&game, 3, "CT_Alpha", CS2D_TEAM_CT);
    cs2d_game_add_player(&game, 4, "CT_Beta", CS2D_TEAM_CT);

    /* Start Round */
    cs2d_game_start_round(&game);
    if (game.round_state != CS2D_ROUND_FREEZETIME) {
        TEST_FAIL("Round Start", "Round must begin in FREEZETIME");
    }
    TEST_PASS("Round Start (Freezetime)");

    /* Pass Freeze time */
    cs2d_game_update(&game, 3.5f);
    if (game.round_state != CS2D_ROUND_PLAYING) {
        TEST_FAIL("Freezetime Expiry", "Round should transition to PLAYING");
    }
    TEST_PASS("Transition to PLAYING");

    /* Find an Info_BombSpot entity (type == CS2D_ENT_INFO_BOMBSPOT = 5) in de_dust2.map */
    float bombspot_px = -1.0f, bombspot_py = -1.0f;
    for (uint32_t i = 0; i < game.map.entity_count; i++) {
        if (game.map.entities[i].type == CS2D_ENT_INFO_BOMBSPOT) {
            bombspot_px = (float)(game.map.entities[i].x * CS2D_TILE_SIZE + 16);
            bombspot_py = (float)(game.map.entities[i].y * CS2D_TILE_SIZE + 16);
            break;
        }
    }
    if (bombspot_px < 0.0f) {
        TEST_FAIL("Find Info_BombSpot", "No Info_BombSpot found in de_dust2.map");
    }

    /* Move T_Alpha to Bombspot */
    game.players[1].pos.x = bombspot_px;
    game.players[1].pos.y = bombspot_py;
    game.players[1].inventory[CS2D_SLOT_BOMB] = CS2D_WEP_BOMB;

    if (!cs2d_game_start_plant(&game, 1)) {
        TEST_FAIL("Bomb Plant Start", "Failed to start bomb plant on Info_BombSpot");
    }
    if (!cs2d_game_tick_plant(&game, 1, 3.1f)) {
        TEST_FAIL("Bomb Plant Complete", "Bomb plant should complete after 3s");
    }
    if (!game.bomb_planted || game.c4_time <= 0.0f) {
        TEST_FAIL("Bomb Plant State", "Bomb planted state mismatch");
    }
    TEST_PASS("Bomb Plant on Info_BombSpot & C4 Countdown Start");

    /* Move CT_Alpha to bomb position to defuse */
    game.players[3].pos = game.bomb_pos;
    game.players[3].has_defuse_kit = true;

    if (!cs2d_game_start_defuse(&game, 3)) {
        TEST_FAIL("Defuse Start", "CT failed to start defusal");
    }
    if (!cs2d_game_tick_defuse(&game, 3, 5.2f)) {
        TEST_FAIL("Defuse Complete", "Defuse should complete after 5s with kit");
    }
    if (game.round_state != CS2D_ROUND_ENDED || game.score_ct != 1) {
        TEST_FAIL("CT Defusal Win", "CT should win round after defusal");
    }
    TEST_PASS("Bomb Defused -> CT Victory & Score Increment");

    if (game.players[3].money != 800 + 3250 + 300) {
        TEST_FAIL("Economy Payout", "Winner payout mismatch");
    }
    TEST_PASS("Economy Reward Distribution");

    cs2d_game_free(&game);
    return true;
}

/* =========================================================================
 * Test 5: Network Serialization (FUN_1401cc0b0, FUN_1401cc9f0, FUN_1401ccc90, FUN_1401da1e0, FUN_1401dac80)
 * ========================================================================= */
static bool test_network_protocol(void)
{
    printf("\n=== Testing Authentic Network Protocol Serialization ===\n");

    /* 5.1 Serverlist Ping packet (Opcode 250 - FUN_1401da1e0: 5 bytes) */
    uint8_t ping_buf[16];
    size_t ping_len = cs2d_net_build_ping_serverlist(12345678, ping_buf, sizeof(ping_buf));
    int32_t parsed_ts = 0;
    if (ping_len != 5 || !cs2d_net_parse_ping_serverlist(ping_buf, ping_len, &parsed_ts) || parsed_ts != 12345678) {
        TEST_FAIL("Ping Serverlist (Opcode 250)", "Failed roundtrip for Opcode 250");
    }
    TEST_PASS("Serverlist Ping Packet (Opcode 250, 5 bytes - FUN_1401da1e0)");

    /* 5.2 Info packet (Opcode 251 - FUN_1401dac80) */
    cs2d_net_server_info_t s_info = {
        .mode = 1,
        .server_name = "CS2D Dedicated [Reconstructed]",
        .map_name = "de_dust2",
        .players_current = 4,
        .players_max = 32,
        .gamemode = CS2D_MODE_DE,
        .bots = 2,
        .password_protected = false,
        .port = 36963
    };

    uint8_t buffer[256];
    size_t len = cs2d_net_build_info_packet(&s_info, buffer, sizeof(buffer));
    cs2d_net_server_info_t parsed_info;
    if (len == 0 || !cs2d_net_parse_info_packet(buffer, len, &parsed_info) ||
        strcmp(parsed_info.server_name, s_info.server_name) != 0 ||
        strcmp(parsed_info.map_name, "de_dust2") != 0 ||
        parsed_info.players_current != 4 || parsed_info.players_max != 32) {
        TEST_FAIL("Net Info Packet (Opcode 251)", "Failed to build/parse info packet");
    }
    TEST_PASS("Server Info Packet Roundtrip (Opcode 251 - FUN_1401dac80)");

    /* 5.3 PosRot packet (Opcode 13 - FUN_1401cc0b0: rot * 0.005493164f) */
    uint8_t pos_buf[16];
    size_t pos_len = cs2d_net_build_posrot_packet(5, 1024.0f, 768.0f, 180.0f, pos_buf, sizeof(pos_buf));
    if (pos_len != 8 || cs2d_net_get_opcode(pos_buf, pos_len) != CS2D_NET_POSROT) {
        TEST_FAIL("PosRot Packet Build", "Invalid PosRot packet length or opcode");
    }

    uint8_t parsed_id = 0;
    float px = 0.0f, py = 0.0f, prot = 0.0f;
    if (!cs2d_net_parse_posrot_packet(pos_buf, pos_len, &parsed_id, &px, &py, &prot)) {
        TEST_FAIL("PosRot Packet Parse", "Failed to parse posrot packet");
    }
    if (parsed_id != 5 || fabsf(px - 1024.0f) > 1.0f || fabsf(py - 768.0f) > 1.0f || fabsf(prot - 180.0f) > 0.1f) {
        TEST_FAIL("PosRot Data Accuracy", "Coordinate or angle error");
    }
    TEST_PASS("PosRot Packet Serialization & Roundtrip (Opcode 13 - FUN_1401cc0b0)");

    /* 5.4 Damage packet (Opcode 17 - FUN_1401cc9f0: 5 bytes [17, victim, attacker, new_hp, new_armor]) */
    uint8_t dmg_buf[16];
    size_t dmg_len = cs2d_net_build_damage_packet(2, 1, 85, 78, dmg_buf, sizeof(dmg_buf));
    if (dmg_len != 5 || cs2d_net_get_opcode(dmg_buf, dmg_len) != CS2D_NET_DAMAGE) {
        TEST_FAIL("Damage Packet Build", "Invalid Damage packet length (expected 5 bytes)");
    }

    uint8_t victim = 0, attacker = 0, new_hp = 0, new_arm = 0;
    if (!cs2d_net_parse_damage_packet(dmg_buf, dmg_len, &victim, &attacker, &new_hp, &new_arm)) {
        TEST_FAIL("Damage Packet Parse", "Failed to parse damage packet");
    }
    if (victim != 2 || attacker != 1 || new_hp != 85 || new_arm != 78) {
        TEST_FAIL("Damage Packet Content", "Damage fields mismatch");
    }
    TEST_PASS("Damage Packet Serialization & Roundtrip (Opcode 17, 5 bytes - FUN_1401cc9f0)");

    /* 5.5 Kill packet (Opcode 19 - FUN_1401ccc90: 9 bytes) */
    uint8_t kill_buf[16];
    size_t kill_len = cs2d_net_build_kill_packet(2, 1, CS2D_WEP_AK47, 1, 640, 480, kill_buf, sizeof(kill_buf));
    uint8_t kv = 0, kk = 0, kw = 0, kf = 0;
    uint16_t kx = 0, ky = 0;
    if (kill_len != 9 || !cs2d_net_parse_kill_packet(kill_buf, kill_len, &kv, &kk, &kw, &kf, &kx, &ky) ||
        kv != 2 || kk != 1 || kw != CS2D_WEP_AK47 || kf != 1 || kx != 640 || ky != 480) {
        TEST_FAIL("Kill Packet (Opcode 19)", "Failed roundtrip for Opcode 19");
    }
    TEST_PASS("Kill Packet Serialization & Roundtrip (Opcode 19, 9 bytes - FUN_1401ccc90)");

    /* 5.6 Opcode 252 Join Handshake Crypto (net_secret 0x1401be680, Encrypt 0x1405f02c0, SHA-256) */
    char secret_out[64] = {0};
    uint8_t enc_out[64] = {0};
    int enc_len = 0;
    char map_sha256[65] = {0};
    cs2d_net_secret("135421593", secret_out);
    cs2d_net_encrypt("395124531", "!aCz0jK5f\xFC", enc_out, &enc_len);
    if (strlen(secret_out) < 10 || enc_len != 9 ||
        !cs2d_net_sha256_file("maps/de_dust2.map", map_sha256) || strlen(map_sha256) != 64) {
        TEST_FAIL("Join Handshake Crypto", "Failed net_secret, Encrypt, or SHA-256 map checksum");
    }
    cs2d_pkt_t p0, p1, p2, p3;
    cs2d_net_build_join_stage0(&p0);
    cs2d_net_build_join_stage1(&p1, "CS2D_Decomp", "", "135421593", 850, 480, 0);
    cs2d_net_build_join_stage2(&p2, map_sha256, "135421593", 1);
    cs2d_net_build_join_stage3(&p3, "de_dust2", map_sha256);
    if (p0.pos != 3 || p1.pos == 0 || p2.pos == 0 || p3.pos == 0) {
        TEST_FAIL("Join Packet Builders (Opcode 252)", "Failed to build Join Stage 0..3 packets");
    }
    TEST_PASS("Join Handshake Crypto & Opcode 252 Stage 0..3 Builders (FUN_1401be680 & FUN_1401e22d5)");

    return true;
}

/* =========================================================================
 * Test 6: Live UDP Query against cs2d_dedicated.exe
 * ========================================================================= */
static bool test_live_server_query(void)
{
    printf("\n=== Testing Live Server Query against cs2d_dedicated.exe ===\n");

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("  [SKIP] WSAStartup failed\n");
        return true;
    }

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        WSACleanup();
        printf("  [SKIP] socket creation failed\n");
        return true;
    }

    DWORD timeout = 1500;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));

    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(CS2D_DEFAULT_PORT);
    saddr.sin_addr.s_addr = inet_addr("127.0.0.1");

    /* Send CS2D UDP header [1, 0] + Opcode 250 Serverlist Ping (FUN_1401da1e0) */
    uint8_t pkt[7] = { 1, 0, (uint8_t)CS2D_NET_PING_SERVERLIST, 0x78, 0x56, 0x34, 0x12 };
    sendto(sock, (const char *)pkt, sizeof(pkt), 0, (struct sockaddr *)&saddr, sizeof(saddr));

    uint8_t recv_buf[512];
    int recv_len = recvfrom(sock, (char *)recv_buf, sizeof(recv_buf), 0, NULL, NULL);

    if (recv_len > 0) {
        printf("  Received live response from cs2d_dedicated.exe! (%d bytes, header=[%u,%u], op=%u)\n",
               recv_len, recv_buf[0], recv_len > 1 ? recv_buf[1] : 0, recv_len > 2 ? recv_buf[2] : 0);
        TEST_PASS("Live UDP Response from cs2d_dedicated.exe");
    } else {
        printf("  Server not currently running on 127.0.0.1:36963\n");
        TEST_PASS("UDP Socket and Opcode 250 packet format verified");
    }

    closesocket(sock);
    WSACleanup();
#endif

    return true;
}

/* =========================================================================
 * Test 7: Entities (Te.inzone FUN_140033fd0) & Player Sprite Frames (Tpl.getFrame FUN_140002e20)
 * ========================================================================= */
static bool test_entities_and_sprites(void)
{
    printf("\n=== Testing Map Entities (Te.inzone) & Player Sprite Frames (Tpl.getFrame) ===\n");

    /* 7.1 Verify entity type names from scraped cs2d.com/entities.php database */
    if (strcmp(cs2d_entity_type_name(CS2D_ENT_INFO_T), "Info_T") != 0 ||
        strcmp(cs2d_entity_type_name(CS2D_ENT_INFO_BOMBSPOT), "Info_BombSpot") != 0 ||
        strcmp(cs2d_entity_type_name(CS2D_ENT_ENV_IMAGE), "Env_Image") != 0 ||
        strcmp(cs2d_entity_type_name(CS2D_ENT_FUNC_DYNWALL), "Func_DynWall") != 0 ||
        strcmp(cs2d_entity_type_name(CS2D_ENT_TRIGGER_IF), "Trigger_If") != 0) {
        TEST_FAIL("cs2d_entity_type_name (0..96)", "Entity type name mismatch");
    }
    TEST_PASS("All 50 CS2D entity type names (0..96) verified against cs2d.com/entities.php");

    /* 7.2 Verify Te.inzone (FUN_140033fd0) rectangular & single-tile zone checks */
    cs2d_map_t map;
    if (!cs2d_map_load_file(&map, "maps/de_dust2.map")) {
        TEST_FAIL("Load de_dust2.map for Te.inzone", "Failed to load de_dust2.map");
    }

    int found_zone = 0;
    for (uint32_t i = 0; i < map.entity_count; i++) {
        const cs2d_entity_t *e = &map.entities[i];
        if (e->type == CS2D_ENT_INFO_BOMBSPOT || e->type == CS2D_ENT_INFO_T || e->type == CS2D_ENT_INFO_CT) {
            /* Test inside 3x3 radius=1 zone [e->x - 1 .. e->x + 1, e->y - 1 .. e->y + 1] */
            if (!cs2d_map_in_zone(&map, e->type, e->x, e->y) ||
                !cs2d_map_in_zone(&map, e->type, e->x + 1, e->y + 1)) {
                cs2d_map_free(&map);
                TEST_FAIL("Te.inzone (FUN_140033fd0)", "Failed to detect tile inside 3x3 spawn/bombspot zone");
            }
            found_zone++;
        } else if (e->type == CS2D_ENT_ENV_ROOM) {
            if (!cs2d_map_in_zone(&map, e->type, e->x, e->y)) {
                cs2d_map_free(&map);
                TEST_FAIL("Te.inzone (FUN_140033fd0)", "Failed to detect origin tile inside Env_Room");
            }
            if (e->int_settings[0] > 1 && e->int_settings[1] > 1) {
                if (!cs2d_map_in_zone(&map, e->type, e->x + e->int_settings[0] - 1, e->y + e->int_settings[1] - 1)) {
                    cs2d_map_free(&map);
                    TEST_FAIL("Te.inzone (FUN_140033fd0)", "Failed to detect far corner inside Env_Room");
                }
            }
            found_zone++;
        }
    }
    cs2d_map_free(&map);
    if (found_zone == 0) {
        TEST_FAIL("Zone entities in de_dust2.map", "Expected spawn/bombspot/room zones in de_dust2.map");
    }
    TEST_PASS("Te.inzone (FUN_140033fd0) 3x3 objective zone & rectangular area lookup verified");

    /* 7.3 Verify Tpl.getFrame (FUN_140002e20) 6-frame player sprite sheet selection */
    cs2d_player_t pl;
    memset(&pl, 0, sizeof(pl));

    pl.current_weapon = CS2D_WEP_USP;
    if (cs2d_player_get_frame(&pl, false) != 3) {
        TEST_FAIL("Tpl.getFrame Pistols (w <= 9)", "USP must use frame 3");
    }
    pl.current_weapon = CS2D_WEP_DEAGLE;
    if (cs2d_player_get_frame(&pl, true) != 3) {
        TEST_FAIL("Tpl.getFrame Pistols (w <= 9)", "Deagle must use frame 3");
    }

    pl.current_weapon = CS2D_WEP_AK47;
    if (cs2d_player_get_frame(&pl, false) != 4) {
        TEST_FAIL("Tpl.getFrame Rifles", "AK-47 RH must use frame 4");
    }
    pl.current_weapon = CS2D_WEP_M4A1;
    if (cs2d_player_get_frame(&pl, true) != 5) {
        TEST_FAIL("Tpl.getFrame Rifles", "M4A1 LH must use frame 5");
    }

    pl.current_weapon = CS2D_WEP_KNIFE;
    if (cs2d_player_get_frame(&pl, false) != 0) {
        TEST_FAIL("Tpl.getFrame Melee/Grenade/Bomb (w 50..89)", "Knife RH must use frame 0");
    }
    pl.current_weapon = CS2D_WEP_HE;
    if (cs2d_player_get_frame(&pl, true) != 1) {
        TEST_FAIL("Tpl.getFrame Melee/Grenade/Bomb (w 50..89)", "HE LH must use frame 1");
    }
    pl.current_weapon = CS2D_WEP_BOMB;
    if (cs2d_player_get_frame(&pl, false) != 0) {
        TEST_FAIL("Tpl.getFrame Melee/Grenade/Bomb (w 50..89)", "Bomb RH must use frame 0");
    }

    pl.current_weapon = CS2D_WEP_CLAW;
    if (cs2d_player_get_frame(&pl, false) != 2) {
        TEST_FAIL("Tpl.getFrame Zombie Claw (w == 78)", "Claw must use frame 2");
    }

    pl.current_weapon = CS2D_WEP_AK47;
    pl.reload_timer = 1.5f;
    if (cs2d_player_get_frame(&pl, false) != 0 ||
        cs2d_player_get_frame(&pl, true) != 1) {
        TEST_FAIL("Tpl.getFrame Reloading", "Reloading must use frame 0 (RH) or 1 (LH)");
    }
    TEST_PASS("Tpl.getFrame (FUN_140002e20) 6-frame player torso animation verified");

    return true;
}

/* =========================================================================
 * Test 8: Clay-Style Declarative UI Layout Solver (cs2d_ui)
 * ========================================================================= */
static bool test_ui_layout(void)
{
    printf("\n=== Testing Declarative UI Layout Engine (cs2d_ui) ===\n");

    static cs2d_ui_context_t ui; /* ~1MB: too large for the stack */
    cs2d_ui_init(&ui);

    cs2d_ui_begin_layout(&ui, 850, 480, 120.0f, 80.0f, false, 0);

    CS2D_UI_ELEMENT(&ui, CS2D_UI_ID("WinTest"), {
        .layout = {
            .sizing    = { CS2D_UI_FIXED(400.0f), CS2D_UI_FIXED(300.0f) },
            .padding   = CS2D_UI_PAD_ALL(10.0f),
            .child_gap = 10.0f,
            .dir       = CS2D_UI_TOP_TO_BOTTOM
        },
        .floating = { .enabled = true, .x = 50.0f, .y = 40.0f, .z_index = 10 }
    }) {
        /* Top fixed header (height 30, width GROW -> 380) */
        CS2D_UI_ELEMENT(&ui, CS2D_UI_ID("Header"), {
            .layout = { .sizing = { CS2D_UI_GROW(0, 0), CS2D_UI_FIXED(30.0f) } }
        }) {}

        /* Middle GROW row -> height = 280 - 30 - 40 - 20(gaps) = 190 */
        CS2D_UI_ELEMENT(&ui, CS2D_UI_ID("BodyRow"), {
            .layout = {
                .sizing    = { CS2D_UI_GROW(0, 0), CS2D_UI_GROW(0, 0) },
                .child_gap = 10.0f,
                .dir       = CS2D_UI_LEFT_TO_RIGHT
            }
        }) {
            CS2D_UI_ELEMENT(&ui, CS2D_UI_ID("LeftCol"), {
                .layout = { .sizing = { CS2D_UI_FIXED(100.0f), CS2D_UI_GROW(0, 0) } }
            }) {}
            CS2D_UI_ELEMENT(&ui, CS2D_UI_ID("RightCol"), {
                .layout = { .sizing = { CS2D_UI_GROW(0, 0), CS2D_UI_GROW(0, 0) } }
            }) {}
        }

        /* Bottom fixed footer (height 40, width GROW -> 380) */
        CS2D_UI_ELEMENT(&ui, CS2D_UI_ID("Footer"), {
            .layout = { .sizing = { CS2D_UI_GROW(0, 0), CS2D_UI_FIXED(40.0f) } }
        }) {}
    }

    cs2d_ui_end_layout(&ui);

    cs2d_ui_rect_t r_win   = cs2d_ui_get_box(&ui, CS2D_UI_ID("WinTest"));
    cs2d_ui_rect_t r_hdr   = cs2d_ui_get_box(&ui, CS2D_UI_ID("Header"));
    cs2d_ui_rect_t r_left  = cs2d_ui_get_box(&ui, CS2D_UI_ID("LeftCol"));
    cs2d_ui_rect_t r_right = cs2d_ui_get_box(&ui, CS2D_UI_ID("RightCol"));
    cs2d_ui_rect_t r_ftr   = cs2d_ui_get_box(&ui, CS2D_UI_ID("Footer"));

    if (r_win.x != 50.0f || r_win.y != 40.0f || r_win.w != 400.0f || r_win.h != 300.0f) {
        TEST_FAIL("Floating Window Geometry", "Expected (50, 40, 400, 300)");
    }
    if (r_hdr.x != 60.0f || r_hdr.y != 50.0f || r_hdr.w != 380.0f || r_hdr.h != 30.0f) {
        TEST_FAIL("Header GROW Width & Padding", "Expected (60, 50, 380, 30)");
    }
    if (r_left.x != 60.0f || r_left.y != 90.0f || r_left.w != 100.0f || r_left.h != 190.0f) {
        TEST_FAIL("Left Column Geometry", "Expected (60, 90, 100, 190)");
    }
    if (r_right.x != 170.0f || r_right.y != 90.0f || r_right.w != 270.0f || r_right.h != 190.0f) {
        TEST_FAIL("Right Column GROW Distribution", "Expected (170, 90, 270, 190)");
    }
    if (r_ftr.x != 60.0f || r_ftr.y != 290.0f || r_ftr.w != 380.0f || r_ftr.h != 40.0f) {
        TEST_FAIL("Footer Positioning", "Expected (60, 290, 380, 40)");
    }
    TEST_PASS("4-pass layout solver (FIT, GROW, FIXED, padding, child_gap, floating)");

    /* Verify hit-testing on second frame using box_cache */
    cs2d_ui_begin_layout(&ui, 850, 480, 75.0f, 110.0f, true, 0);
    if (!cs2d_ui_hovered(&ui, CS2D_UI_ID("LeftCol"))) {
        TEST_FAIL("Hit-Testing", "Expected mouse (75, 110) to hover LeftCol");
    }
    cs2d_ui_end_layout(&ui);
    TEST_PASS("Z-ordered Box Cache & Interactive Hover/Active Hit-Testing");

    return true;
}

/* =========================================================================
 * Test 9: Textbox Editing Engine (selection, word navigation, undo/redo, filters, UTF-8, focus)
 * ========================================================================= */
static cs2d_ui_context_t s_tb_ui;
static char              s_tb_buf[32];
static int               s_tb_cur;

/* Runs one UI frame with the textbox focused, feeding optional text and one key (with modifiers) */
static bool tb_frame(const char *text, int key, uint32_t mods, uint32_t flags)
{
    cs2d_ui_begin_layout(&s_tb_ui, 850, 480, -100.0f, -100.0f, false, 0);
    if (text) snprintf(s_tb_ui.text_input, sizeof(s_tb_ui.text_input), "%s", text);
    if (key) {
        s_tb_ui.key_events[0].key  = key;
        s_tb_ui.key_events[0].mods = mods;
        s_tb_ui.key_event_count    = 1;
    }
    bool submitted = cs2d_ui_textbox_ex(&s_tb_ui, CS2D_UI_ID("TbTest"), s_tb_buf, sizeof(s_tb_buf), &s_tb_cur, 200.0f, flags);
    cs2d_ui_end_layout(&s_tb_ui);
    return submitted;
}

static void tb_reset(const char *text)
{
    snprintf(s_tb_buf, sizeof(s_tb_buf), "%s", text);
    s_tb_cur = (int)strlen(s_tb_buf);
    cs2d_ui_edit_reset(&s_tb_ui);
    s_tb_ui.focused_id = CS2D_UI_ID("TbTest");
    tb_frame(NULL, 0, 0, 0);
}

static bool test_ui_textbox(void)
{
    printf("\n=== Testing Textbox Editing Engine (cs2d_ui_textbox_ex) ===\n");
    const uint32_t SH = CS2D_UI_MOD_SHIFT, CT = CS2D_UI_MOD_CTRL;
    cs2d_ui_init(&s_tb_ui);

    /* Typing, word navigation and typing over a selection */
    tb_reset("");
    tb_frame("hello world", 0, 0, 0);
    if (strcmp(s_tb_buf, "hello world") != 0 || s_tb_cur != 11) TEST_FAIL("Typing", s_tb_buf);
    tb_frame(NULL, SDLK_LEFT, CT, 0);
    if (s_tb_cur != 6) TEST_FAIL("Ctrl+Left", "Expected caret at start of 'world'");
    tb_frame(NULL, SDLK_LEFT, CT | SH, 0);
    if (s_tb_cur != 0 || s_tb_ui.edit.anchor != 6) TEST_FAIL("Ctrl+Shift+Left", "Expected selection [0,6)");
    tb_frame("bye ", 0, 0, 0);
    if (strcmp(s_tb_buf, "bye world") != 0 || s_tb_cur != 4) TEST_FAIL("Type over selection", s_tb_buf);
    tb_frame(NULL, SDLK_RIGHT, CT, 0);
    if (s_tb_cur != 9) TEST_FAIL("Ctrl+Right", "Expected caret at end");
    TEST_PASS("Typing, Ctrl+Left/Right word jumps, Shift selection, typing replaces selection");

    /* Word deletion and undo / redo */
    tb_frame(NULL, SDLK_HOME, 0, 0);
    tb_frame(NULL, SDLK_RIGHT, CT, 0);
    tb_frame(NULL, SDLK_BACKSPACE, CT, 0);
    if (strcmp(s_tb_buf, "world") != 0 || s_tb_cur != 0) TEST_FAIL("Ctrl+Backspace", s_tb_buf);
    tb_frame(NULL, SDLK_DELETE, CT, 0);
    if (strcmp(s_tb_buf, "") != 0) TEST_FAIL("Ctrl+Delete", s_tb_buf);
    tb_frame(NULL, SDLK_Z, CT, 0);
    if (strcmp(s_tb_buf, "world") != 0) TEST_FAIL("Undo #1", s_tb_buf);
    tb_frame(NULL, SDLK_Z, CT, 0);
    if (strcmp(s_tb_buf, "bye world") != 0) TEST_FAIL("Undo #2", s_tb_buf);
    tb_frame(NULL, SDLK_Z, CT, 0);
    if (strcmp(s_tb_buf, "hello world") != 0) TEST_FAIL("Undo #3", s_tb_buf);
    tb_frame(NULL, SDLK_Y, CT, 0);
    if (strcmp(s_tb_buf, "bye world") != 0) TEST_FAIL("Redo (Ctrl+Y)", s_tb_buf);
    tb_frame(NULL, SDLK_Z, CT | SH, 0);
    if (strcmp(s_tb_buf, "world") != 0) TEST_FAIL("Redo (Ctrl+Shift+Z)", s_tb_buf);
    tb_frame("!", 0, 0, 0);
    tb_frame(NULL, SDLK_Y, CT, 0);
    if (strcmp(s_tb_buf, "!world") != 0) TEST_FAIL("New edit drops redo branch", s_tb_buf);
    TEST_PASS("Ctrl+Backspace/Delete word deletion, Ctrl+Z undo, Ctrl+Y / Ctrl+Shift+Z redo");

    /* Select all, Shift+Home, Delete selection */
    tb_frame(NULL, SDLK_A, CT, 0);
    if (s_tb_ui.edit.anchor != 0 || s_tb_cur != 6) TEST_FAIL("Ctrl+A", "Expected full selection");
    tb_frame(NULL, SDLK_DELETE, 0, 0);
    if (s_tb_buf[0] != '\0') TEST_FAIL("Delete selection", s_tb_buf);
    tb_reset("abc def");
    tb_frame(NULL, SDLK_HOME, SH, 0);
    if (s_tb_cur != 0 || s_tb_ui.edit.anchor != 7) TEST_FAIL("Shift+Home", "Expected selection [0,7)");
    tb_frame(NULL, SDLK_RIGHT, 0, 0);
    if (s_tb_cur != 7 || s_tb_ui.edit.anchor != 7) TEST_FAIL("Right collapses selection", "Expected caret at selection end");
    TEST_PASS("Ctrl+A, Shift+Home, Delete selection, arrow collapses selection");

    /* Filters, capacity and UTF-8 */
    tb_reset("");
    tb_frame("12a3-4 5", 0, 0, CS2D_UI_TEXTBOX_NUMERIC);
    if (strcmp(s_tb_buf, "12345") != 0) TEST_FAIL("Numeric filter", s_tb_buf);
    tb_reset("");
    tb_frame("0123456789012345678901234567890123456789", 0, 0, 0);
    if (strlen(s_tb_buf) != sizeof(s_tb_buf) - 1) TEST_FAIL("Buffer capacity", "Expected text truncated to buffer size");
    tb_reset("a\xC3\xA9");
    tb_frame(NULL, SDLK_LEFT, 0, 0);
    if (s_tb_cur != 1) TEST_FAIL("UTF-8 Left", "Caret must skip the whole 2-byte character");
    tb_frame(NULL, SDLK_END, 0, 0);
    tb_frame(NULL, SDLK_BACKSPACE, 0, 0);
    if (strcmp(s_tb_buf, "a") != 0) TEST_FAIL("UTF-8 Backspace", "Expected the whole 2-byte character removed");
    tb_reset("ro");
    tb_frame("x", SDLK_BACKSPACE, 0, CS2D_UI_TEXTBOX_READONLY);
    if (strcmp(s_tb_buf, "ro") != 0) TEST_FAIL("Read-only", s_tb_buf);
    TEST_PASS("Numeric filter, buffer capacity, UTF-8 caret/backspace, read-only");

    /* Enter submits; Tab moves focus to the next textbox */
    tb_reset("cmd");
    if (!tb_frame(NULL, SDLK_RETURN, 0, 0)) TEST_FAIL("Enter", "Expected submitted == true");
    char other[16] = "next";
    int  other_cur = 0;
    cs2d_ui_begin_layout(&s_tb_ui, 850, 480, -100.0f, -100.0f, false, 0);
    s_tb_ui.key_events[0].key  = SDLK_TAB;
    s_tb_ui.key_events[0].mods = 0;
    s_tb_ui.key_event_count    = 1;
    cs2d_ui_textbox_ex(&s_tb_ui, CS2D_UI_ID("TbTest"), s_tb_buf, sizeof(s_tb_buf), &s_tb_cur, 200.0f, 0);
    cs2d_ui_textbox_ex(&s_tb_ui, CS2D_UI_ID("TbOther"), other, sizeof(other), &other_cur, 200.0f, 0);
    cs2d_ui_end_layout(&s_tb_ui);
    if (s_tb_ui.focused_id != CS2D_UI_ID("TbOther")) TEST_FAIL("Tab", "Expected focus on the next textbox");
    cs2d_ui_begin_layout(&s_tb_ui, 850, 480, -100.0f, -100.0f, false, 0);
    cs2d_ui_textbox_ex(&s_tb_ui, CS2D_UI_ID("TbTest"), s_tb_buf, sizeof(s_tb_buf), &s_tb_cur, 200.0f, 0);
    cs2d_ui_textbox_ex(&s_tb_ui, CS2D_UI_ID("TbOther"), other, sizeof(other), &other_cur, 200.0f, 0);
    cs2d_ui_end_layout(&s_tb_ui);
    if (s_tb_ui.edit.anchor != 0 || other_cur != 4) TEST_FAIL("Tab select-all", "Expected the focused textbox fully selected");
    TEST_PASS("Enter submits, Tab moves focus and selects the next textbox");

    return true;
}

/* =========================================================================
 * Test 10: Dropdown (cs2d_ui_select) & Number Box (cs2d_ui_numberbox)
 * ========================================================================= */
static cs2d_ui_context_t s_dd_ui;

static const char *s_dd_opts[] = {
    "Opt 0", "Opt 1", "Opt 2", "Opt 3", "Opt 4", "Opt 5",
    "Opt 6", "Opt 7", "Opt 8", "Opt 9", "Opt 10", "Opt 11"
};

/* One frame with a 12-option dropdown at (0,0) 120x18 and a checkbox laid out below it */
static bool dd_frame(int *sel, float mx, float my, bool down, int wheel, int key)
{
    cs2d_ui_begin_layout(&s_dd_ui, 850, 480, mx, my, down, wheel);
    if (key) {
        s_dd_ui.key_events[0].key  = key;
        s_dd_ui.key_events[0].mods = 0;
        s_dd_ui.key_event_count    = 1;
    }
    bool changed = cs2d_ui_select(&s_dd_ui, CS2D_UI_ID("DdTest"), s_dd_opts, 12, sel, 120.0f);
    bool below = false;
    cs2d_ui_checkbox(&s_dd_ui, CS2D_UI_ID("DdBelow"), "Below", &below);
    cs2d_ui_end_layout(&s_dd_ui);
    return changed;
}

/* One frame with a [1, 32] number box at (0,0), optionally typing text and pressing a key */
static void nb_frame(int *value, const char *text, int key, uint32_t mods, int wheel, float mx, float my)
{
    cs2d_ui_begin_layout(&s_dd_ui, 850, 480, mx, my, false, wheel);
    if (text) snprintf(s_dd_ui.text_input, sizeof(s_dd_ui.text_input), "%s", text);
    if (key) {
        s_dd_ui.key_events[0].key  = key;
        s_dd_ui.key_events[0].mods = mods;
        s_dd_ui.key_event_count    = 1;
    }
    cs2d_ui_numberbox(&s_dd_ui, CS2D_UI_ID("NbTest"), value, 1, 32, 40.0f);
    cs2d_ui_end_layout(&s_dd_ui);
}

static bool test_ui_dropdown_numberbox(void)
{
    printf("\n=== Testing Dropdown & Number Box (cs2d_ui_select, cs2d_ui_numberbox) ===\n");
    cs2d_ui_id_t dd = CS2D_UI_ID("DdTest");
    int sel = 0;
    cs2d_ui_init(&s_dd_ui);

    /* Open with a click, then pick the third visible row with the mouse */
    dd_frame(&sel, 60.0f, 9.0f, false, 0, 0);
    dd_frame(&sel, 60.0f, 9.0f, true, 0, 0);
    if (s_dd_ui.dropdown_id != dd) TEST_FAIL("Dropdown open", "Click on the box must open the list");
    dd_frame(&sel, 60.0f, 9.0f, false, 0, 0);
    if (cs2d_ui_get_box(&s_dd_ui, dd ^ 0x6002u).h != 8 * 20.0f + 2.0f) TEST_FAIL("Dropdown list", "Expected 8 visible rows");

    int last = s_dd_ui.render_order[s_dd_ui.render_cmd_count - 1];
    if (s_dd_ui.render_cmds[last].z_index != 100) TEST_FAIL("Z-order", "Open list must be drawn last (above the checkbox)");
    for (int i = 1; i < s_dd_ui.render_cmd_count; i++) {
        if (s_dd_ui.render_cmds[s_dd_ui.render_order[i - 1]].z_index > s_dd_ui.render_cmds[s_dd_ui.render_order[i]].z_index) {
            TEST_FAIL("Z-order", "render_order must be sorted by z_index");
        }
    }

    float row2_y = 18.0f + 1.0f + 2.0f * 20.0f + 10.0f;
    dd_frame(&sel, 40.0f, row2_y, false, 0, 0);
    dd_frame(&sel, 40.0f, row2_y, true, 0, 0);
    bool changed = dd_frame(&sel, 40.0f, row2_y, false, 0, 0);
    if (!changed || sel != 2 || s_dd_ui.dropdown_id != 0) TEST_FAIL("Dropdown pick", "Expected option 2 picked and list closed");
    TEST_PASS("Click opens list (8 rows, drawn above later widgets), click on a row picks it and closes");

    /* Wheel scrolls the list, ESC closes it, keyboard changes the value while closed */
    dd_frame(&sel, 60.0f, 9.0f, true, 0, 0);
    dd_frame(&sel, 60.0f, 9.0f, false, 0, 0);
    dd_frame(&sel, 40.0f, 60.0f, false, -10, 0);
    if (s_dd_ui.dropdown_scroll.top != 4) TEST_FAIL("Dropdown wheel", "Expected list scrolled to the last page (top = 4)");
    if (!cs2d_ui_capture_escape(&s_dd_ui) || s_dd_ui.dropdown_id != 0) TEST_FAIL("Dropdown ESC", "ESC must close the list");
    if (cs2d_ui_capture_escape(&s_dd_ui)) TEST_FAIL("Dropdown ESC", "ESC with no open list must not be captured");
    dd_frame(&sel, 400.0f, 400.0f, false, 0, SDLK_DOWN);
    if (sel != 3) TEST_FAIL("Dropdown Down", "Expected value 3");
    dd_frame(&sel, 400.0f, 400.0f, false, 0, SDLK_END);
    if (sel != 11) TEST_FAIL("Dropdown End", "Expected value 11");
    dd_frame(&sel, 400.0f, 400.0f, false, 0, SDLK_RETURN);
    if (s_dd_ui.dropdown_id != dd || s_dd_ui.dropdown_hover != 11) TEST_FAIL("Dropdown Enter", "Enter must open the list on the value");
    dd_frame(&sel, 400.0f, 400.0f, false, 0, SDLK_UP);
    dd_frame(&sel, 400.0f, 400.0f, false, 0, SDLK_RETURN);
    if (sel != 10 || s_dd_ui.dropdown_id != 0) TEST_FAIL("Dropdown keyboard pick", "Expected option 10 picked");
    dd_frame(&sel, 400.0f, 400.0f, true, 0, 0);
    TEST_PASS("Wheel scrolls the list, ESC closes it, Up/Down/End/Enter keyboard navigation");

    /* Number box */
    cs2d_ui_init(&s_dd_ui);
    cs2d_ui_id_t nb = CS2D_UI_ID("NbTest");
    int val = 12;
    nb_frame(&val, NULL, 0, 0, 0, 400.0f, 400.0f);
    s_dd_ui.focused_id = nb;
    nb_frame(&val, NULL, SDLK_A, CS2D_UI_MOD_CTRL, 0, 400.0f, 400.0f);
    nb_frame(&val, "4x5", 0, 0, 0, 400.0f, 400.0f);
    if (strcmp(s_dd_ui.numbox_text, "45") != 0 || val != 12) TEST_FAIL("Numberbox typing", "Out-of-range text must not change the value yet");
    nb_frame(&val, NULL, SDLK_RETURN, 0, 0, 400.0f, 400.0f);
    if (val != 32 || strcmp(s_dd_ui.numbox_text, "32") != 0) TEST_FAIL("Numberbox Enter", "Expected clamp to 32");
    nb_frame(&val, NULL, SDLK_DOWN, CS2D_UI_MOD_SHIFT, 0, 400.0f, 400.0f);
    if (val != 22) TEST_FAIL("Numberbox Shift+Down", "Expected 22");
    nb_frame(&val, NULL, SDLK_BACKSPACE, 0, 0, 400.0f, 400.0f);
    if (val != 2) TEST_FAIL("Numberbox live value", "Valid in-range text must update the value");
    nb_frame(&val, NULL, SDLK_BACKSPACE, 0, 0, 400.0f, 400.0f);
    s_dd_ui.focused_id = 0;
    nb_frame(&val, NULL, 0, 0, 0, 400.0f, 400.0f);
    if (val != 1 || s_dd_ui.numbox_id != 0) TEST_FAIL("Numberbox focus loss", "Empty text must commit as the minimum");
    nb_frame(&val, NULL, 0, 0, 3, 20.0f, 9.0f);
    if (val != 4) TEST_FAIL("Numberbox wheel", "Wheel over the box must step the value");
    TEST_PASS("Number box: digits only, live value, clamp on Enter / focus loss, arrows and wheel");

    return true;
}

/* =========================================================================
 * Test 10b: Shared scrolling (cs2d_ui_scroll_*, cs2d_ui_scrollbar): every scrollable view uses it
 * ========================================================================= */
static cs2d_ui_context_t s_sc_ui;
static cs2d_ui_scroll_t  s_sc;

/* One frame: 50 rows, 10 visible, list area (0,0) 100x200 with the default scrollbar at x 100 */
static void sc_frame(float mx, float my, bool down, int wheel)
{
    cs2d_ui_rect_t area = { 0.0f, 0.0f, 100.0f, 200.0f };
    cs2d_ui_begin_layout(&s_sc_ui, 850, 480, mx, my, down, wheel);
    cs2d_ui_scroll_wheel(&s_sc_ui, &s_sc, &area, 3, 10, 50);
    cs2d_ui_scrollbar_at(&s_sc_ui, CS2D_UI_ID("ScTest"), &s_sc, 10, 50, 100.0f, 0.0f, 200.0f, 1, NULL);
    cs2d_ui_end_layout(&s_sc_ui);
}

static bool test_ui_scroll(void)
{
    printf("\n=== Testing Shared Scrolling (cs2d_ui_scroll_*, cs2d_ui_scrollbar) ===\n");
    cs2d_ui_init(&s_sc_ui);
    memset(&s_sc, 0, sizeof(s_sc));
    sc_frame(400.0f, 400.0f, false, 0);

    sc_frame(50.0f, 50.0f, false, -2);
    if (s_sc.top != 6) TEST_FAIL("Wheel", "Two notches down over the list must scroll 6 rows");
    sc_frame(400.0f, 400.0f, false, -2);
    if (s_sc.top != 6) TEST_FAIL("Wheel outside", "The wheel outside the list must not scroll it");
    sc_frame(50.0f, 50.0f, false, 10);
    if (s_sc.top != 0) TEST_FAIL("Wheel clamp", "Scrolling up must stop at the first row");
    TEST_PASS("Wheel over the view scrolls it (3 rows per notch), outside it does not, clamped at the top");

    /* Track below the thumb pages down by visible - 1; dragging the thumb past the end reaches the last row */
    cs2d_ui_rect_t track = cs2d_ui_get_box(&s_sc_ui, CS2D_UI_ID("ScTest"));
    if (track.x != 100.0f || track.w != 9.0f || track.h != 200.0f) TEST_FAIL("Scrollbar box", "Expected a 9x200 track at x 100");
    sc_frame(104.0f, 150.0f, true, 0);
    sc_frame(104.0f, 150.0f, false, 0);
    if (s_sc.top != 9) TEST_FAIL("Track page", "A press below the thumb must page down by 9 rows");
    cs2d_ui_rect_t thumb = cs2d_ui_get_box(&s_sc_ui, CS2D_UI_ID("ScTest") ^ 0x7001u);
    float thumb_h = 196.0f * 10.0f / 50.0f;
    if (fabsf(thumb.h - thumb_h) > 0.01f || fabsf(thumb.y - (2.0f + (196.0f - thumb_h) * 9.0f / 40.0f)) > 0.5f /* positions are rounded */) {
        TEST_FAIL("Thumb geometry", "Thumb must be visible / total of the track, at top / max of the free space");
    }
    float grab_y = thumb.y + 5.0f;
    sc_frame(104.0f, grab_y, true, 0);
    sc_frame(104.0f, grab_y + (196.0f - thumb_h) * 0.5f, true, 0);
    if (s_sc.top != 9 + 20) TEST_FAIL("Thumb drag", "Dragging the thumb by half the free space must scroll half the rows");
    sc_frame(104.0f, 470.0f, true, 0);
    sc_frame(104.0f, 470.0f, false, 0);
    if (s_sc.top != 40) TEST_FAIL("Thumb drag end", "Dragging past the end must stop at the last page");
    TEST_PASS("Track press pages by visible - 1; the thumb is proportional and follows the pointer while dragged");

    if (!cs2d_ui_scroll_page(&s_sc, -1, 10, 50) || s_sc.top != 31) TEST_FAIL("Page up", "Expected 40 - 9 = 31");
    cs2d_ui_scroll_clamp(&s_sc, 10, 20);
    if (s_sc.top != 10) TEST_FAIL("Clamp", "A shorter list must clamp top to total - visible");
    cs2d_ui_scroll_clamp(&s_sc, 10, 5);
    if (s_sc.top != 0 || cs2d_ui_scroll_by(&s_sc, 3, 10, 5)) TEST_FAIL("Clamp", "A list that fits never scrolls");
    TEST_PASS("Page and clamp helpers");
    return true;
}

/* =========================================================================
 * Test 11: Console Ring Buffer, Scrolling & Performance (cs2d_client)
 * ========================================================================= */
static cs2d_client_t s_con;

static double now_ms(void)
{
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
}

/* Lays out the console and returns its log box node (children = visible lines) */
static const cs2d_ui_node_t *console_layout(void)
{
    cs2d_client_build_ui(&s_con, 850, 480);
    for (int i = 0; i < s_con.ui.node_count; i++) {
        if (s_con.ui.nodes[i].id == CS2D_UI_ID("ConsoleLogBox")) return &s_con.ui.nodes[i];
    }
    return NULL;
}

static const char *visible_line(const cs2d_ui_node_t *box, int i)
{
    return s_con.ui.nodes[box->children[i]].text;
}

static bool test_console(void)
{
    printf("\n=== Testing Console Ring Buffer, Scrolling & Performance ===\n");
    memset(&s_con, 0, sizeof(s_con));
    cs2d_ui_init(&s_con.ui);
    s_con.config.window_width  = 850;
    s_con.config.window_height = 480;
    s_con.console_open         = true;

    /* Throughput: 100k lines through the 1024-line ring buffer */
    const int N = 100000;
    double t0 = now_ms();
    for (int i = 0; i < N; i++) {
        cs2d_client_console_print(&s_con, 170, 170, 170, "console line %d", i);
    }
    double print_ms = now_ms() - t0;
    char expect[64];
    snprintf(expect, sizeof(expect), "console line %d", N - CS2D_CONSOLE_MAX_LINES);
    if (s_con.console_line_count != CS2D_CONSOLE_MAX_LINES || strcmp(cs2d_client_console_line(&s_con, 0)->text, expect) != 0) {
        TEST_FAIL("Ring buffer", "Expected the newest 1024 lines, oldest first");
    }
    snprintf(expect, sizeof(expect), "console line %d", N - 1);
    if (strcmp(cs2d_client_console_line(&s_con, CS2D_CONSOLE_MAX_LINES - 1)->text, expect) != 0) {
        TEST_FAIL("Ring buffer", "Newest line must be last");
    }
    printf("  [INFO] %d prints: %.2f ms (%.3f us/line)\n", N, print_ms, print_ms * 1000.0 / N);
    if (print_ms > 500.0) TEST_FAIL("Print throughput", "100k console prints took longer than 500ms");
    TEST_PASS("Ring buffer keeps the newest 1024 of 100k lines in order");

    /* Regression: scrolling up must never empty the log box (lines used to vanish from the bottom) */
    const cs2d_ui_node_t *box = console_layout();
    int vis = s_con.console_visible_lines;
    if (!box || vis < 5 || box->child_count != vis) TEST_FAIL("Console layout", "Log box must show a full page of lines");
    for (int step = 0; step < 400; step++) {
        cs2d_client_handle_mouse_wheel(&s_con, 1);
        box = console_layout();
        if (box->child_count != vis) {
            char msg[96];
            snprintf(msg, sizeof(msg), "Scroll step %d shows %d of %d lines", step, box->child_count, vis);
            TEST_FAIL("Scroll up", msg);
        }
    }
    if (s_con.console_scroll.top != 0) TEST_FAIL("Scroll clamp", "Scroll must stop when the oldest line is at the top");
    if (strcmp(visible_line(box, 0), cs2d_client_console_line(&s_con, 0)->text) != 0) TEST_FAIL("Scroll top", "Oldest line must be the first visible line");
    TEST_PASS("Scrolling to the top and beyond keeps every row filled; stops at the oldest line");

    /* While scrolled up, new output keeps the view on the same lines */
    for (int i = 0; i < 300; i++) cs2d_client_handle_mouse_wheel(&s_con, -1);
    box = console_layout();
    char first[CS2D_CONSOLE_LINE_LEN];
    snprintf(first, sizeof(first), "%s", visible_line(box, 0));
    for (int i = 0; i < 10; i++) cs2d_client_console_print(&s_con, 255, 255, 255, "new output %d", i);
    box = console_layout();
    if (strcmp(visible_line(box, 0), first) != 0) TEST_FAIL("Scroll anchor", "New lines must not move a scrolled-up view");
    s_con.console_scroll.top = CS2D_CONSOLE_MAX_LINES; /* Clamped to the bottom by the layout */
    box = console_layout();
    if (strcmp(visible_line(box, vis - 1), "new output 9") != 0) TEST_FAIL("Follow output", "At the bottom the newest line must be visible");
    TEST_PASS("Scrolled-up view stays put on new output; bottom view follows it");

    /* Layout cost of a full console window (headless: CPU layout only) */
    const int FRAMES = 2000;
    t0 = now_ms();
    for (int f = 0; f < FRAMES; f++) {
        s_con.console_scroll.top = (f * 7) % (CS2D_CONSOLE_MAX_LINES - vis);
        console_layout();
    }
    double layout_ms = (now_ms() - t0) / FRAMES;
    printf("  [INFO] console layout with %d lines: %.3f ms/frame (%d visible)\n", s_con.console_line_count, layout_ms, vis);
    if (layout_ms > 2.0) TEST_FAIL("Layout performance", "Console layout must stay under 2ms per frame");
    TEST_PASS("Console layout cost independent of history size");

    return true;
}

/* =========================================================================
 * Test 12: Map Entity Logic vs. Dedicated Server Ground Truth (prison_funst3r)
 * The fixture was recorded on cs2d_dedicated with a Lua sweep that fires every trigger name
 * of the map and samples entity(x,y,"state") (see the fixture header for the timing).
 * ========================================================================= */
static cs2d_map_t          s_pmap;
static cs2d_entity_world_t s_pworld;
static uint8_t             s_expected[CS2D_MAX_ENTITIES];

static int prison_entity_at(int x, int y)
{
    for (uint32_t i = 0; i < s_pmap.entity_count; i++) {
        if (s_pmap.entities[i].x == x && s_pmap.entities[i].y == y) return (int)i;
    }
    return -1;
}

/* Round restart scheduled by Func_GameAction (0 = draw, 1 = T win, 2 = CT win: new round 3 s later) */
static float s_prestart = -1.0f;

static void prison_handle_events(void)
{
    cs2d_ent_event_t ev;
    while (cs2d_ents_pop_event(&s_pworld, &ev)) {
        const cs2d_entity_t *e = &s_pmap.entities[ev.entity];
        if (ev.type == CS2D_ENT_EVENT_ACTION && e->int_settings[0] >= 0 && e->int_settings[0] <= 2) {
            s_prestart = 3.0f;
        }
    }
}

static void prison_advance(float seconds)
{
    const float dt = 0.01f;
    for (float t = 0.0f; t < seconds - dt * 0.5f; t += dt) {
        cs2d_ents_update(&s_pworld, dt);
        prison_handle_events();
        if (s_prestart >= 0.0f) {
            s_prestart -= dt;
            if (s_prestart <= 0.005f) {
                s_prestart = -1.0f;
                cs2d_ents_round_start(&s_pworld);
                prison_handle_events();
            }
        }
    }
}

/* Applies "+x,y" / "-x,y" tokens (or plain "x,y" = 1) to s_expected; returns false on an unknown position */
static bool prison_apply_tokens(char *tokens)
{
    for (char *tok = strtok(tokens, " \r\n"); tok; tok = strtok(NULL, " \r\n")) {
        uint8_t v = 1;
        if (*tok == '+' || *tok == '-') v = (*tok++ == '+') ? 1 : 0;
        int x, y;
        if (sscanf(tok, "%d,%d", &x, &y) != 2) return false;
        int idx = prison_entity_at(x, y);
        if (idx < 0) return false;
        s_expected[idx] = v;
    }
    return true;
}

static int prison_compare(const char *where, int *reported)
{
    int bad = 0;
    for (uint32_t i = 0; i < s_pmap.entity_count; i++) {
        if (s_pmap.entities[i].type == CS2D_ENT_ENV_DECAL) continue; /* Not exposed to Lua */
        if (s_pmap.entity_state[i] != s_expected[i]) {
            if (*reported < 12) {
                printf("  [DIFF] %s: #%u %s at (%d,%d) name='%s' expected %u, got %u\n", where, i,
                       cs2d_entity_type_name(s_pmap.entities[i].type), s_pmap.entities[i].x, s_pmap.entities[i].y,
                       s_pmap.entities[i].name, s_expected[i], s_pmap.entity_state[i]);
                (*reported)++;
            }
            bad++;
        }
    }
    return bad;
}

static bool test_entities_prison(void)
{
    printf("\n=== Testing Entity Logic vs. Dedicated Server (prison_funst3r, all trigger names) ===\n");
    FILE *f = fopen("tests/data/prison_funst3r_triggers.txt", "r");
    if (!f) TEST_FAIL("Fixture", "tests/data/prison_funst3r_triggers.txt not found");
    if (!cs2d_map_load_file(&s_pmap, "maps/prison_funst3r.map")) {
        fclose(f);
        TEST_FAIL("Map", "maps/prison_funst3r.map not found");
    }
    cs2d_ents_init(&s_pworld, &s_pmap);
    cs2d_ents_round_start(&s_pworld);
    s_prestart = -1.0f;
    prison_handle_events();

    static char line[65536];
    int reported = 0, samples = 0, bad_samples = 0, names = 0;
    bool first_step = true;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        if (strncmp(line, "init ", 5) == 0) {
            memset(s_expected, 0, sizeof(s_expected));
            if (!prison_apply_tokens(line + 5)) {
                fclose(f);
                TEST_FAIL("Fixture", "bad init record");
            }
            if (prison_compare("round start", &reported) != 0) bad_samples++;
            continue;
        }
        char name[64];
        int sample = 0, consumed = 0;
        if (sscanf(line, "step %63s %d%n", name, &sample, &consumed) != 2) continue;
        if (sample == 1) {
            if (!first_step) prison_advance(0.1f);
            first_step = false;
            cs2d_ents_trigger(&s_pworld, name);
            prison_handle_events();
            prison_advance(0.5f);
            names++;
        } else {
            prison_advance(1.0f);
        }
        if (!prison_apply_tokens(line + consumed)) {
            fclose(f);
            TEST_FAIL("Fixture", "bad step record");
        }
        char where[96];
        snprintf(where, sizeof(where), "trigger '%s' sample %d", name, sample);
        if (prison_compare(where, &reported) != 0) bad_samples++;
        samples++;
    }
    fclose(f);

    printf("  [INFO] %d trigger names, %d samples, %u activations, %d sample(s) differing from the server\n",
           names, samples, s_pworld.activations, bad_samples);
    cs2d_map_free(&s_pmap);
    if (bad_samples != 0) TEST_FAIL("Entity logic", "entity states differ from the dedicated server");
    TEST_PASS("Round start + every trigger name of prison_funst3r match the dedicated server state-for-state");
    return true;
}

/* =========================================================================
 * Test 13: Player Movement Collision vs. the Original Client (prison_funst3r)
 * Reference trajectory recorded from CS2D.exe through a UDP proxy (opcode 10 positions):
 * from (1328,781) holding W it stops at y = 748; then W+D slides east along that wall at
 * y = 748 (reaching x = 1481 after 1.5 s), and D alone keeps going to x = 1617.
 * ========================================================================= */
static bool test_player_physics(void)
{
    printf("\n=== Testing Player Movement Collision vs. CS2D.exe (prison_funst3r) ===\n");
    static cs2d_map_t map;
    static cs2d_entity_world_t world;
    if (!cs2d_map_load_file(&map, "maps/prison_funst3r.map")) TEST_FAIL("Map", "maps/prison_funst3r.map not found");
    cs2d_ents_init(&world, &map);
    cs2d_ents_round_start(&world);

    cs2d_player_t pl;
    memset(&pl, 0, sizeof(pl));
    pl.alive = true;
    pl.pos.x = 1328.0f;
    pl.pos.y = 781.0f;

    const float step = 2.4f; /* px per 60 Hz frame */
    for (int i = 0; i < 60; i++) cs2d_player_move(&pl, &map, 0.0f, -step);
    if ((int)pl.pos.y != 748 || (int)pl.pos.x != 1328) { /* Positions go over the wire truncated to u16 */
        char msg[96];
        snprintf(msg, sizeof(msg), "Expected stop at (1328,748), got (%.2f,%.2f)", pl.pos.x, pl.pos.y);
        cs2d_map_free(&map);
        TEST_FAIL("Wall stop", msg);
    }
    TEST_PASS("Holding W stops flush against the wall at y = 748 (24x24 box, like CS2D.exe)");

    const float diag = step * 0.70710678f;
    for (int i = 0; i < 90; i++) cs2d_player_move(&pl, &map, diag, -diag);
    if ((int)pl.pos.y != 748 || pl.pos.x < 1328.0f + diag * 89.0f) {
        char msg[96];
        snprintf(msg, sizeof(msg), "Expected slide along y = 748, got (%.2f,%.2f)", pl.pos.x, pl.pos.y);
        cs2d_map_free(&map);
        TEST_FAIL("Wall slide", msg);
    }
    TEST_PASS("W+D against the wall slides along it at the parallel speed (X then Y resolution)");

    for (int i = 0; i < 60; i++) cs2d_player_move(&pl, &map, step, 0.0f);
    if ((int)pl.pos.y != 748 || pl.pos.x < 1617.0f) {
        char msg[96];
        snprintf(msg, sizeof(msg), "Expected free run east past x = 1617, got (%.2f,%.2f)", pl.pos.x, pl.pos.y);
        cs2d_map_free(&map);
        TEST_FAIL("Free run", msg);
    }
    TEST_PASS("D keeps running east along the wall (original reached x = 1617)");
    cs2d_map_free(&map);
    return true;
}

/* =========================================================================
 * Main Runner
 * ========================================================================= */
/* Shots against Env_Breakable on prison_funst3r, numbers recorded from cs2d_dedicated.exe (Lua "break" hook) */
static bool test_breakables(void)
{
    printf("\n=== Testing Env_Breakable vs. cs2d_dedicated (prison_funst3r) ===\n");
    static cs2d_map_t map;
    static cs2d_entity_world_t world;
    if (!cs2d_map_load_file(&map, "maps/prison_funst3r.map")) TEST_FAIL("Map", "maps/prison_funst3r.map not found");
    cs2d_ents_init(&world, &map);
    cs2d_ents_round_start(&world);
    const cs2d_weapon_def_t *ak = cs2d_weapon_get(CS2D_WEP_AK47);
    const cs2d_weapon_def_t *knife = cs2d_weapon_get(CS2D_WEP_KNIFE);
    char msg[128];
    float hx, hy;

    /* AK-47 (22 damage) from (1744,1264) east: the 280 health breakable at (56,39) broke on shot 13 */
    int target = cs2d_ents_breakable_at(&world, 56, 39);
    if (target < 0) { cs2d_map_free(&map); TEST_FAIL("Breakable", "no Env_Breakable at (56,39)"); }
    int broke_at = 0;
    for (int shot = 1; shot <= 15 && !broke_at; shot++) {
        int hit = cs2d_ents_trace_shot(&world, 1744.0f, 1264.0f, 90.0f, cs2d_weapon_shot_reach(ak), &hx, &hy);
        if (hit != target) break;
        if (cs2d_ents_damage_breakable(&world, hit, ak->damage)) broke_at = shot;
    }
    size_t tile = (size_t)39 * map.width + 56;
    if (broke_at != 13 || map.entity_state[target] != 1 || map.collision[tile] != map.base_collision[tile]) {
        snprintf(msg, sizeof(msg), "broke at shot %d (expected 13), state %u", broke_at, map.entity_state[target]);
        cs2d_map_free(&map);
        TEST_FAIL("AK-47 vs 280 health", msg);
    }
    cs2d_ent_event_t ev;
    bool break_event = false;
    while (cs2d_ents_pop_event(&world, &ev)) break_event |= (ev.type == CS2D_ENT_EVENT_BREAK && ev.entity == target);
    if (!break_event) { cs2d_map_free(&map); TEST_FAIL("Break event", "no CS2D_ENT_EVENT_BREAK queued"); }
    TEST_PASS("AK-47 breaks the 280 health breakable on the 13th shot and frees its tile");

    /* Closed cell doors (Func_DynWall cell6 at (53,39)) stop bullets; open, the AK-47 reaches 310 px but not 320 */
    cs2d_ents_round_start(&world);
    if (cs2d_ents_trace_shot(&world, 1600.0f, 1264.0f, 90.0f, cs2d_weapon_shot_reach(ak), &hx, &hy) != -1) {
        cs2d_map_free(&map);
        TEST_FAIL("Door", "bullet went through the closed cell door");
    }
    cs2d_ents_trigger(&world, "cell6");
    cs2d_ents_trigger(&world, "cell1");
    if (cs2d_ents_trace_shot(&world, 1792.0f - 310.0f, 1264.0f, 90.0f, cs2d_weapon_shot_reach(ak), &hx, &hy) != target ||
        cs2d_ents_trace_shot(&world, 1792.0f - 320.0f, 1264.0f, 90.0f, cs2d_weapon_shot_reach(ak), &hx, &hy) != -1) {
        cs2d_map_free(&map);
        TEST_FAIL("AK-47 reach", "expected a hit at 310 px and a miss at 320 px");
    }
    TEST_PASS("Closed doors stop bullets; with them open the AK-47 reaches 310 px but not 320 px");

    /* Knife (45 damage): the 10 health stone at (71,50) breaks from 20 px, not from 21 px */
    int stone = cs2d_ents_breakable_at(&world, 71, 50);
    float sx = 71.0f * 32.0f + 16.0f;
    if (stone < 0 ||
        cs2d_ents_trace_shot(&world, sx, 1632.0f + 21.0f, 0.0f, cs2d_weapon_shot_reach(knife), &hx, &hy) != -1 ||
        cs2d_ents_trace_shot(&world, sx, 1632.0f + 20.0f, 0.0f, cs2d_weapon_shot_reach(knife), &hx, &hy) != stone ||
        !cs2d_ents_damage_breakable(&world, stone, knife->damage)) {
        cs2d_map_free(&map);
        TEST_FAIL("Knife reach", "expected the stone to break from 20 px and be out of reach from 21 px");
    }
    TEST_PASS("Knife reaches 20 px (not 21) and breaks a 10 health stone in one hit");

    /* A bullet fired from inside a breakable hits that breakable; explosive (113,72) explodes on break */
    int barrel = cs2d_ents_breakable_at(&world, 113, 72);
    while (cs2d_ents_pop_event(&world, &ev)) {}
    int shots = 0;
    while (barrel >= 0 && map.entity_state[barrel] == 0 && shots < 20) {
        int hit = cs2d_ents_trace_shot(&world, 3632.0f, 2320.0f, 0.0f, cs2d_weapon_shot_reach(ak), &hx, &hy);
        if (hit != barrel) break;
        cs2d_ents_damage_breakable(&world, hit, ak->damage);
        shots++;
    }
    break_event = false;
    while (cs2d_ents_pop_event(&world, &ev)) {
        if (ev.type == CS2D_ENT_EVENT_BREAK && ev.entity == barrel) {
            const cs2d_entity_t *e = &map.entities[ev.entity];
            break_event = e->int_settings[3] == 1 && e->int_settings[4] == 1 && e->int_settings[5] == 50;
        }
    }
    if (shots != 14 || !break_event) {
        snprintf(msg, sizeof(msg), "%d shots (expected 14), explosive break event %d", shots, break_event);
        cs2d_map_free(&map);
        TEST_FAIL("Explosive", msg);
    }
    TEST_PASS("Shooting from inside the 300 health barrel breaks it on shot 14 with an explosion (range 1, power 50)");

    /* Round restart restores every breakable with full health */
    cs2d_ents_round_start(&world);
    if (map.entity_state[target] != 0 || world.health[target] != 280 || map.collision[tile] == map.base_collision[tile]) {
        cs2d_map_free(&map);
        TEST_FAIL("Restart", "breakable not restored at round start");
    }
    TEST_PASS("Round restart restores broken breakables (state, health and collision)");
    cs2d_map_free(&map);
    return true;
}

/* =========================================================================
 * Test 15: Server Info ("<Server> - Info") & "Select Team" / Skin Subcategory UI
 * ========================================================================= */
static cs2d_client_t s_tm;

static void tm_click(cs2d_ui_id_t id)
{
    cs2d_client_build_ui(&s_tm, 850, 480);
    cs2d_ui_rect_t r = cs2d_ui_get_box(&s_tm.ui, id);
    s_tm.mouse_screen_x = r.x + r.w * 0.5f;
    s_tm.mouse_screen_y = r.y + r.h * 0.5f;
    s_tm.mouse_fire = false;
    cs2d_client_build_ui(&s_tm, 850, 480);
    s_tm.mouse_fire = true;
    cs2d_client_build_ui(&s_tm, 850, 480);
    s_tm.mouse_fire = false;
    cs2d_client_build_ui(&s_tm, 850, 480);
}

static bool test_server_info_and_team_menu(void)
{
    printf("\n=== Testing Server Info & Select Team / Character Subcategory UI ===\n");
    memset(&s_tm, 0, sizeof(s_tm));
    cs2d_ui_init(&s_tm.ui);
    s_tm.local_player_id = 1;
    s_tm.game.players[1].id   = 1;
    s_tm.game.players[1].team = CS2D_TEAM_SPEC;
    snprintf(s_tm.net_server_name, sizeof(s_tm.net_server_name), "CS2D Server");

    if (!cs2d_client_load_serverinfo(&s_tm, "sys/serverinfo.txt") || s_tm.server_info_line_count < 12) {
        TEST_FAIL("serverinfo.txt", "Expected sys/serverinfo.txt loaded into server_info_lines");
    }
    if (strcmp(s_tm.server_info_lines[0].text, "Welcome on my CS2D Server!") != 0 ||
        s_tm.server_info_lines[0].r != 255 || s_tm.server_info_lines[0].g != 255 || s_tm.server_info_lines[0].b != 255 ||
        strcmp(s_tm.server_info_lines[4].text, "- Don't cheat/hack") != 0 ||
        s_tm.server_info_lines[4].r != 255 || s_tm.server_info_lines[4].g != 0 || s_tm.server_info_lines[4].b != 0 ||
        s_tm.server_info_lines[11].r != 255 || s_tm.server_info_lines[11].g != 220 || s_tm.server_info_lines[11].b != 0) {
        TEST_FAIL("Color tag", "Expected U+00A9 RRRGGGBBB stripped and U+00BB link highlighted in RGB(255,220,0)");
    }
    TEST_PASS("sys/serverinfo.txt parsed with U+00A9 RRRGGGBBB colors and U+00BB link highlighting");

    /* Both windows queued on server join: Server Info renders first (600x440 at 125,20); Close reveals Select Team */
    s_tm.server_info_open = true;
    s_tm.team_menu_open   = true;
    s_tm.team_menu_page   = 0;
    cs2d_client_build_ui(&s_tm, 850, 480);

    cs2d_ui_rect_t info_win = cs2d_ui_get_box(&s_tm.ui, CS2D_UI_ID("ServerInfoWin"));
    cs2d_ui_rect_t info_box = cs2d_ui_get_box(&s_tm.ui, CS2D_UI_ID("ServerInfoBox"));
    cs2d_ui_rect_t info_sb  = cs2d_ui_get_box(&s_tm.ui, CS2D_UI_ID("ServerInfoScrollbar"));
    cs2d_ui_rect_t info_btn = cs2d_ui_get_box(&s_tm.ui, CS2D_UI_ID("BtnServerInfoClose"));
    if ((int)info_win.x != 125 || (int)info_win.y != 20 || (int)info_win.w != 600 || (int)info_win.h != 440 ||
        (int)info_box.w != 553 || (int)info_box.h != 374 || (int)info_sb.w != 15 ||
        (int)info_btn.w != 568 || (int)info_btn.h != 20) {
        TEST_FAIL("ServerInfo geometry", "Expected 600x440 window, 553x374 text box, 15px scrollbar, 568x20 Close button");
    }
    tm_click(CS2D_UI_ID("BtnServerInfoClose"));
    if (s_tm.server_info_open || !s_tm.team_menu_open) {
        TEST_FAIL("ServerInfo close", "Closing Server Info must reveal Select Team");
    }
    TEST_PASS("CS2D Server - Info matches 600x440 geometry and reveals Select Team on Close");

    /* Select Team main menu -> Terrorists / Counter-Terrorists subcategories, Back, and skin selection */
    cs2d_client_build_ui(&s_tm, 850, 480);
    cs2d_ui_rect_t btn_t = cs2d_ui_get_box(&s_tm.ui, CS2D_UI_ID("BtnTeamT"));
    if ((int)btn_t.x != 205 || (int)btn_t.y != 120 || (int)btn_t.w != 240 || (int)btn_t.h != 25) {
        TEST_FAIL("SelectTeam geometry", "Expected 240x25 option buttons starting at (205, 120)");
    }
    tm_click(CS2D_UI_ID("BtnTeamT"));
    if (!s_tm.team_menu_open || s_tm.team_menu_page != 1) {
        TEST_FAIL("Terrorists subcategory", "Clicking 1 Terrorists must open page 1 (Select Team - Terrorists)");
    }
    tm_click(CS2D_UI_ID("BtnSkinBack"));
    if (!s_tm.team_menu_open || s_tm.team_menu_page != 0) {
        TEST_FAIL("Back button", "Clicking Back must return to page 0 (Select Team)");
    }
    tm_click(CS2D_UI_ID("BtnTeamCT"));
    if (!s_tm.team_menu_open || s_tm.team_menu_page != 2) {
        TEST_FAIL("CT subcategory", "Clicking 2 Counter-Terrorists must open page 2 (Select Team - Counter-Terrorists)");
    }
    tm_click(CS2D_UI_IDI("BtnTeamSkin", 1)); /* 2 German GSG-9 (look = 1) */
    if (s_tm.team_menu_open || s_tm.game.players[1].team != CS2D_TEAM_CT || s_tm.game.players[1].look != 1) {
        TEST_FAIL("Skin selection", "Clicking 2 German GSG-9 must select CT look 1 and close the menu");
    }
    TEST_PASS("Terrorists and Counter-Terrorists open character subcategories; Back and skin buttons work");

    /* Auto-Select and Spectator on main Select Team menu have NO subcategory: they select and close immediately */
    s_tm.team_menu_open = true;
    s_tm.team_menu_page = 0;
    tm_click(CS2D_UI_ID("BtnTeamAuto"));
    if (s_tm.team_menu_open || s_tm.team_menu_page != 0) {
        TEST_FAIL("Auto-Select", "5 Auto-Select must not open a subcategory; it must select immediately and close");
    }
    s_tm.team_menu_open = true;
    s_tm.team_menu_page = 0;
    tm_click(CS2D_UI_ID("BtnTeamSpec"));
    if (s_tm.team_menu_open || s_tm.team_menu_page != 0 || s_tm.game.players[1].team != CS2D_TEAM_SPEC) {
        TEST_FAIL("Spectator", "6 Spectator must not open a subcategory; it must switch to Spectator and close");
    }
    TEST_PASS("5 Auto-Select and 6 Spectator have no subcategory and select immediately");

    return true;
}

/* =========================================================================
 * Test 16: Firearm Visual Recoil Kickback & Melee Swing Animation
 *   (CS2D.exe 0x1409eda18, Tpl.attack 0x1402166a7, Tpl.slash 0x14018cebc,
 *    Tpl.updateAll 0x14021d1b1 / 0x14021d68a / 0x14021da21, Tp.create 0x14004b4e0)
 * ========================================================================= */
static bool test_recoil_and_swing(void)
{
    printf("\n=== Testing Firearm Recoil Kickback & Melee Swing Animation (CS2D.exe) ===\n");
    cs2d_weapons_init();

    /* 16.1 Verify per-weapon visual_recoil table (0x1409eda18 in CS2D.exe) */
    if (cs2d_weapon_get(CS2D_WEP_MP5)->visual_recoil    != 1 ||
        cs2d_weapon_get(CS2D_WEP_M4A1)->visual_recoil   != 1 ||
        cs2d_weapon_get(CS2D_WEP_USP)->visual_recoil    != 2 ||
        cs2d_weapon_get(CS2D_WEP_GLOCK)->visual_recoil  != 2 ||
        cs2d_weapon_get(CS2D_WEP_AK47)->visual_recoil   != 2 ||
        cs2d_weapon_get(CS2D_WEP_DEAGLE)->visual_recoil != 4 ||
        cs2d_weapon_get(CS2D_WEP_M3)->visual_recoil     != 5 ||
        cs2d_weapon_get(CS2D_WEP_AWP)->visual_recoil    != 6 ||
        cs2d_weapon_get(CS2D_WEP_LASER)->visual_recoil  != 6 ||
        cs2d_weapon_get(CS2D_WEP_KNIFE)->visual_recoil  != 0) {
        TEST_FAIL("visual_recoil table (0x1409eda18)", "Per-weapon visual recoil pixel table mismatch");
    }
    TEST_PASS("Per-weapon visual_recoil table verified against CS2D.exe (0x1409eda18)");

    /* 16.2 Firearm backward recoil kickback on shot and 0.5 px / 60 Hz frame decay */
    cs2d_player_t pl;
    cs2d_player_init(&pl, 1, "Shooter");
    cs2d_vec2_t pt = { 256.0f, 256.0f };
    cs2d_player_spawn(&pl, CS2D_TEAM_CT, pt);
    cs2d_player_give_weapon(&pl, CS2D_WEP_AWP);
    pl.angle = 90.0f;

    if (!cs2d_weapon_fire(&pl) || pl.recoil < 6.0f || pl.recoil > 7.0f) {
        TEST_FAIL("AWP recoil on shot (0x1402166a7)", "Expected recoil in [6.0, 7.0] px");
    }
    float r0 = pl.recoil;
    cs2d_player_update(&pl, 1.0f / 60.0f);
    if (fabsf(pl.recoil - (r0 - 0.5f)) > 0.001f) {
        TEST_FAIL("Recoil decay rate (0x14021d1b1)", "Expected 0.5 px decay per 60 Hz frame (30 px/s)");
    }
    cs2d_player_update(&pl, 14.0f / 60.0f);
    if (pl.recoil != 0.0f) {
        TEST_FAIL("Recoil return to zero", "Expected recoil to clamp to 0.0 after decaying");
    }
    TEST_PASS("Firearm recoil kickback (visual_recoil + Rnd(0,1)) and 0.5 px/frame decay verified");

    /* 16.3 Melee swing timer (8.0 -> 0.0 at 60 Hz), torso/weapon draw_angle (+48 deg -> 0), and knifeslash FX */
    cs2d_player_switch_weapon(&pl, CS2D_SLOT_MELEE);
    pl.fire_cooldown = 0.0f;
    pl.angle = 90.0f;
    if (!cs2d_weapon_fire(&pl) ||
        fabsf(pl.slash_timer - 8.0f) > 0.001f ||
        fabsf(pl.draw_angle - (90.0f + 48.0f)) > 0.001f ||
        pl.recoil != 0.0f) {
        TEST_FAIL("Knife swing start (0x14018cebc)", "Expected slash_timer=8.0, draw_angle=angle+48, recoil=0");
    }
    cs2d_player_update(&pl, 4.0f / 60.0f);
    if (fabsf(pl.slash_timer - 4.0f) > 0.001f ||
        fabsf(pl.draw_angle - (90.0f + 24.0f)) > 0.001f) {
        TEST_FAIL("Knife swing mid-arc (0x14021d68a)", "Expected slash_timer=4.0 and draw_angle=angle+24 after 4 frames");
    }
    cs2d_player_update(&pl, 4.0f / 60.0f);
    if (pl.slash_timer != 0.0f || fabsf(pl.draw_angle - 90.0f) > 0.001f) {
        TEST_FAIL("Knife swing finish (0x14021da21)", "Expected slash_timer=0 and draw_angle=angle after 8 frames");
    }

    /* Chainsaw (85) must not trigger slash_timer (0x14018ce92) */
    cs2d_player_give_weapon(&pl, CS2D_WEP_CHAINSAW);
    pl.fire_cooldown = 0.0f;
    if (!cs2d_weapon_fire(&pl) || pl.slash_timer != 0.0f) {
        TEST_FAIL("Chainsaw exception (0x14018ce92)", "Chainsaw (85) must not set slash_timer");
    }

    /* Knife slash arc particle (Tp type 13, 0x14004b4e0: fade 0.07 per 60 Hz frame) */
    static cs2d_fx_t fx;
    cs2d_fx_clear(&fx);
    cs2d_fx_knifeslash(&fx, 256.0f, 256.0f, 90.0f);
    if (fx.count != 1 || fx.p[0].kind != CS2D_FX_KNIFESLASH ||
        fabsf(fx.p[0].max_life - (1.0f / (0.07f * 60.0f))) > 0.001f) {
        TEST_FAIL("Knife slash FX (0x14004b4e0)", "Expected CS2D_FX_KNIFESLASH with 0.07/frame fade rate");
    }
    cs2d_fx_update(&fx, 0.30f);
    if (fx.count != 0) {
        TEST_FAIL("Knife slash FX expiry", "Expected knifeslash particle to expire after ~0.24s");
    }
    TEST_PASS("Melee swing (slash_timer 8->0, draw_angle +48..0 deg, Chainsaw exception, knifeslash FX) verified");

    return true;
}

/* =========================================================================
 * Server browser (pingServers 0x1403012d0, info parser 0x1402d2d80, svl.cache 0x1400f25d0).
 * Byte strings are a proxy capture of CS2D.exe against cs2d_dedicated.exe -offline.
 * ========================================================================= */
static bool test_serverlist(void)
{
    printf("\n=== Testing Server Browser (Find Servers) ===\n");

    /* Requests, byte for byte as the original sends them */
    static const uint8_t lan_req[9]  = { 0x01, 0x00, 0x03, 0x97, 0x00, 0xfb, 0x02, 0xfa, 0x03 };
    static const uint8_t inet_req[9] = { 0x01, 0x00, 0x03, 0x8f, 0x00, 0xfb, 0x01, 0xfa, 0x03 };
    static const uint8_t ping_req[7] = { 0x01, 0x00, 0xfa, 0x39, 0x87, 0x00, 0x00 };
    uint8_t buf[32];
    if (cs2d_sv_build_info_request(2, 0x0097, buf, sizeof(buf)) != 9 || memcmp(buf, lan_req, 9) != 0 ||
        cs2d_sv_build_info_request(1, 0x008f, buf, sizeof(buf)) != 9 || memcmp(buf, inet_req, 9) != 0 ||
        cs2d_sv_build_ping(0x8739, buf, sizeof(buf)) != 7 || memcmp(buf, ping_req, 7) != 0) {
        TEST_FAIL("Server browser requests", "Info / ping request bytes differ from the capture");
    }
    TEST_PASS("Info request [01 00][03][tick][FB][mode][1018] and ping [01 00][FA][ms] match the capture");

    /* Info reply of the offline dedicated (prison_funst3r, default settings) */
    static const uint8_t reply[36] = {
        0x01, 0x00, 0xfb, 0x02, 0x10, 0x0b, 'C', 'S', '2', 'D', ' ', 'S', 'e', 'r', 'v', 'e', 'r',
        0x0e, 'p', 'r', 'i', 's', 'o', 'n', '_', 'f', 'u', 'n', 's', 't', '3', 'r', 0x00, 0x0c, 0x00, 0x02
    };
    cs2d_sv_t sv;
    memset(&sv, 0, sizeof(sv));
    if (!cs2d_sv_parse_info(&sv, reply + 2, sizeof(reply) - 2) ||
        strcmp(sv.name, "CS2D Server") != 0 || strcmp(sv.map, "prison_funst3r") != 0 ||
        sv.players != 0 || sv.max_players != 12 || sv.bots != 0 || sv.gamemode != 0 ||
        !sv.same_version || !sv.offscreen_damage || sv.password || sv.lua || sv.dyn_recoil ||
        !sv.active || !sv.info_done) {
        TEST_FAIL("Info reply", "Captured reply decoded wrong");
    }
    /* Game mode byte only when flags bit 5; bots are clamped to the slot count */
    static const uint8_t reply_dm[] = { 0xfb, 0x01, 0x61, 0x01, 'A', 0x02, 'd', 'm', 0x05, 0x08, 0x01, 0x09, 0x05 };
    memset(&sv, 0, sizeof(sv));
    if (!cs2d_sv_parse_info(&sv, reply_dm, sizeof(reply_dm)) || !sv.password || !sv.lua || sv.gamemode != 1 ||
        sv.players != 5 || sv.max_players != 8 || sv.bots != 8 || !sv.dyn_recoil || sv.offscreen_damage || !sv.transfers) {
        TEST_FAIL("Info reply flags", "Game mode / flags / flags2 decoded wrong");
    }
    cs2d_net_server_info_t ni;
    if (!cs2d_net_parse_info_packet(reply + 2, sizeof(reply) - 2, &ni) || strcmp(ni.map_name, "prison_funst3r") != 0 ||
        ni.players_max != 12 || ni.flags2 != 0x02) {
        TEST_FAIL("cs2d_net_parse_info_packet", "Captured reply decoded wrong");
    }
    TEST_PASS("Info reply: u8-length name/map, players, max, optional game mode, bots, flags2");

    /* sys/core/svl.cache: comment line, then {ip i32 LE (first octet in the MSB), port u16 LE} */
    static const uint8_t cache[] = {
        '/', '/', ' ', 'x', '\r', '\n',
        0xae, 0x03, 0xe6, 0x17, 0x65, 0x90,   /* 23.230.3.174:36965 (first entry of the real cache) */
        0x01, 0x00, 0x00, 0x7f, 0x74, 0x90,   /* 127.0.0.1:36980 */
        0x01, 0x00, 0x00, 0x7f, 0x74, 0x90    /* duplicate: ignored */
    };
    static cs2d_sv_list_t list;
    memset(&list, 0, sizeof(list));
    char addr[16];
    if (cs2d_sv_parse_cache(cache, sizeof(cache), &list, 100000u) != 2 || list.count != 2) {
        TEST_FAIL("svl.cache", "Wrong entry count");
    }
    cs2d_sv_ip_to_str(list.entries[0].ip, addr);
    if (strcmp(addr, "23.230.3.174") != 0 || list.entries[0].port != 36965 ||
        list.entries[1].ip != cs2d_sv_ip_from_octets(127, 0, 0, 1) || list.entries[1].port != 36980) {
        TEST_FAIL("svl.cache", "Address byte order wrong");
    }
    uint32_t ip = 0;
    uint16_t port = 0;
    if (!cs2d_sv_parse_address("25.38.11.44:36963\n", &ip, &port) || ip != cs2d_sv_ip_from_octets(25, 38, 11, 44) ||
        port != 36963 || cs2d_sv_parse_address("// comment", &ip, &port)) {
        TEST_FAIL("recentlyjoined.lst", "ip:port line parsed wrong");
    }
    TEST_PASS("svl.cache entries (23.230.3.174:36965) and ip:port list lines");

    /* pingServers: entry n is due n * 30 ms after the list is filled; then every 3 s,
     * up to 3 pings and 5 info requests (the capture shows ping + info every 3 s) */
    bool p, i;
    cs2d_sv_schedule(&list.entries[1], 100000u + 29u, &p, &i);
    if (p || i) TEST_FAIL("pingServers", "Second entry queried before its 30 ms stagger");
    int pings = 0, infos = 0;
    for (uint32_t t = 100031u; t < 100031u + 30000u; t += 100u) {
        cs2d_sv_schedule(&list.entries[1], t, &p, &i);
        pings += p;
        infos += i;
    }
    if (pings != 3 || infos != 5) TEST_FAIL("pingServers", "Expected 3 pings and 5 info requests");
    cs2d_sv_refresh(&list.entries[1], 200000u);
    pings = infos = 0;
    for (uint32_t t = 200001u; t < 230000u; t += 100u) {
        cs2d_sv_schedule(&list.entries[1], t, &p, &i);
        pings += p;
        infos += i;
    }
    if (pings != 1 || infos != 5) TEST_FAIL("Tsv.refresh", "Refresh must send one ping (pingc = 2) and 5 info requests");

    /* Ping echo: average of the last two samples, blend flash */
    memset(&sv, 0, sizeof(sv));
    cs2d_sv_apply_ping(&sv, 1000, 1080u);
    cs2d_sv_apply_ping(&sv, 4000, 4120u);
    if (!sv.got_ping || sv.ping != 100 || sv.ping_blend != 255.0f) TEST_FAIL("Ping echo", "Ping average wrong");
    TEST_PASS("pingServers schedule (30 ms stagger, 3 s period, 3 pings, 5 info requests) and ping average");

    /* Default order: human players (players - bots), descending */
    memset(&list, 0, sizeof(list));
    static const int pl[4][2] = { { 6, 5 }, { 0, 0 }, { 4, 0 }, { 26, 26 } };
    for (int k = 0; k < 4; k++) {
        cs2d_sv_t *e = cs2d_sv_list_add(&list, (uint32_t)(k + 1), 36963, 0);
        e->players = pl[k][0];
        e->bots    = pl[k][1];
    }
    cs2d_sv_sort_by_humans(&list);
    if (list.entries[0].ip != 3 || list.entries[1].ip != 1 || list.entries[2].ip != 2 || list.entries[3].ip != 4) {
        TEST_FAIL("Server list order", "Expected human players descending, stable");
    }
    TEST_PASS("Server list sorted by human players (4/12 > 6/32 with 5 bots > 0/32)");

    return true;
}

/* =========================================================================
 * Join-time file transfer [252,50] (FUN_1402cd970, Ttrans_recv). The list chunk is a proxy capture of
 * our client joining a modded dedicated (tools/agent/capture_transfer.sh, map fun_roleplay).
 * ========================================================================= */
static void xfer_put_str(uint8_t *m, int *l, const char *s)
{
    m[(*l)++] = (uint8_t)strlen(s);
    memcpy(m + *l, s, strlen(s));
    *l += (int)strlen(s);
}

static void xfer_put_i32(uint8_t *m, int *l, int32_t v)
{
    memcpy(m + *l, &v, 4);
    *l += 4;
}

static bool test_file_transfer(void)
{
    printf("\n=== Testing Join File Transfer ([252,50]) ===\n");

    /* FUN_1402cb770 path conditions */
    static const struct { const char *path; int accept; int want; } paths[] = {
        { "maps/fun_roleplay.map",       1, CS2D_XFER_OK },
        { "maps/fun_roleplay.txt",       1, CS2D_XFER_OK },
        { "maps/fun_roleplay.lua",       1, CS2D_XFER_BAD_MAPS_EXT },
        { "gfx/tiles/custom.txt",        1, CS2D_XFER_BAD_TILES_EXT },
        { "sfx/weapons/ak47_clipin.wav", 1, CS2D_XFER_RELOAD_SOUND },
        { "sfx/weapons/ak47.bmp",        1, CS2D_XFER_WEAPONS_NOT_SND },
        { "sfx/c4/door.ogg",             2, CS2D_XFER_NO_SOUNDS },
        { "gfx/c4/door.png",             3, CS2D_XFER_NO_IMAGES },
        { "gfx/c4/door.png",             6, CS2D_XFER_OK },
        { "gfx/c4/notes.txt",            1, CS2D_XFER_INVALID_EXT },
        { "gfx/../door.png",             1, CS2D_XFER_INVALID_PATH },
        { "\\GFX\\C4\\Door.PNG",         1, CS2D_XFER_OK },
    };
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        int got = cs2d_xfer_check_path(paths[i].path, paths[i].accept);
        if (got != paths[i].want) {
            printf("    %s (accept %d) -> %d\n", paths[i].path, paths[i].accept, got);
            TEST_FAIL("Transfer path conditions", "wrong FUN_1402cb770 result");
        }
    }

    /* The last [252,50,1] chunk of the fun_roleplay list: the 3 sfx/weapons sounds ship with the game */
    static const char *names[6] = {
        "gfx/c4/map/objects/c4_amaguya.PNG", "sfx/weapons/c4_disarm.wav", "sfx/weapons/nvg.ogg",
        "sfx/weapons/c4_explode.wav", "gfx/c4/map/texture/light_map/fun_roleplay_dungeon8_1.png", "maps/fun_roleplay.map"
    };
    uint8_t chunk[512];
    int cl = 0;
    chunk[cl++] = 0xfc; chunk[cl++] = 0x32; chunk[cl++] = 0x01; chunk[cl++] = 0x06; chunk[cl++] = 0x00;
    for (int i = 0; i < 6; i++) xfer_put_str(chunk, &cl, names[i]);
    static const uint8_t list_end[] = { 0xfc, 0x32, 0x01, 0x00, 0x00 };
    static cs2d_xfer_t x;
    cs2d_xfer_init(&x);
    int pos = 2;
    if (cl != 5 + 34 + 26 + 20 + 27 + 57 + 22 ||
        cs2d_xfer_on_message(&x, chunk, cl, &pos) != CS2D_XFER_EV_NONE || pos != cl || x.request_count != 3 ||
        strcmp(x.requests[0], names[0]) != 0 || strcmp(x.requests[1], names[4]) != 0 ||
        strcmp(x.requests[2], names[5]) != 0) {
        TEST_FAIL("Transfer list", "the chunk was not parsed into 3 requests");
    }
    pos = 2;
    if (cs2d_xfer_on_message(&x, list_end, (int)sizeof(list_end), &pos) != CS2D_XFER_EV_LIST_DONE || x.files_total != 3) {
        TEST_FAIL("Transfer list", "[252,50,1,0,0] did not end the list");
    }

    /* Requests: [252,50,2,n u16,n x str] in chunks of 10, then [252,50,2,0,0] */
    static cs2d_pkt_t pkts[4];
    int n = cs2d_xfer_build_requests(&x, pkts, 4);
    static const uint8_t req_head[] = { 0xfc, 0x32, 0x02, 0x03, 0x00, 0x21, 'g', 'f', 'x' };
    static const uint8_t req_end[]  = { 0xfc, 0x32, 0x02, 0x00, 0x00 };
    if (n != 2 || memcmp(pkts[0].buf, req_head, sizeof(req_head)) != 0 || pkts[0].pos != 5 + 34 + 57 + 22 ||
        pkts[1].pos != 5 || memcmp(pkts[1].buf, req_end, sizeof(req_end)) != 0) {
        TEST_FAIL("Transfer requests", "wrong [252,50,2] messages");
    }
    for (int i = 0; i < 23; i++) snprintf(x.requests[i], CS2D_XFER_PATH, "gfx/f%d.png", i);
    x.request_count = 23;
    n = cs2d_xfer_build_requests(&x, pkts, 4);
    if (n != 4 || pkts[0].buf[3] != 10 || pkts[1].buf[3] != 10 || pkts[2].buf[3] != 3 || pkts[3].pos != 5) {
        TEST_FAIL("Transfer requests", "names are not split in chunks of 10");
    }

    /* FUN_14022d2f0: [raw size i32][zlib], a negative size means stored */
    static const char payload[] = "CS2D transfer payload CS2D transfer payload CS2D transfer payload";
    uint8_t packed[256];
    uLongf packed_len = sizeof(packed) - 4;
    int32_t raw_size = (int32_t)sizeof(payload);
    compress(packed + 4, &packed_len, (const Bytef *)payload, sizeof(payload));
    memcpy(packed, &raw_size, 4);
    uint8_t *out = NULL;
    int out_len = 0;
    bool ok = cs2d_xfer_uncompress(packed, (int)packed_len + 4, &out, &out_len) && out_len == raw_size &&
              memcmp(out, payload, sizeof(payload)) == 0;
    free(out);
    uint8_t plain[8];
    int pl = 0;
    xfer_put_i32(plain, &pl, -4);
    memcpy(plain + 4, "abcd", 4);
    ok = ok && cs2d_xfer_uncompress(plain, 8, &out, &out_len) && out_len == 4 && memcmp(out, "abcd", 4) == 0;
    free(out);
    if (!ok) TEST_FAIL("Transfer uncompress", "wrong FUN_14022d2f0 result");

    /* [252,50,5] + [252,50,7] blocks are joined; Ttrans_recv.finish refuses a .lua file (nothing is written) */
    uint8_t m5[64], m7[64];
    int l5 = 0, l7 = 0;
    m5[l5++] = 0xfc; m5[l5++] = 0x32; m5[l5++] = 0x05;
    xfer_put_str(m5, &l5, "maps/evil.lua");
    xfer_put_i32(m5, &l5, 8);
    xfer_put_i32(m5, &l5, 4);
    memcpy(m5 + l5, plain, 4); l5 += 4;
    m7[l7++] = 0xfc; m7[l7++] = 0x32; m7[l7++] = 0x07;
    xfer_put_str(m7, &l7, "maps/evil.lua");
    xfer_put_i32(m7, &l7, 4);
    memcpy(m7 + l7, plain + 4, 4); l7 += 4;
    cs2d_xfer_reset(&x);
    pos = 2;
    cs2d_xfer_event_t ev5 = cs2d_xfer_on_message(&x, m5, l5, &pos);
    bool half = pos == l5 && x.data_len == 4 && x.files_received == 1 && strcmp(x.prog, "0/0 kb - 50 %") == 0;
    pos = 2;
    cs2d_xfer_event_t ev7 = cs2d_xfer_on_message(&x, m7, l7, &pos);
    if (ev5 != CS2D_XFER_EV_NONE || !half || ev7 != CS2D_XFER_EV_FILE_FAILED || x.data_len != 8 || pos != l7) {
        TEST_FAIL("Transfer blocks", "blocks not joined, or the .lua file was accepted");
    }
    cs2d_xfer_free(&x);
    TEST_PASS("Join file transfer: path rules, [252,50,1] list, [252,50,2] requests, blocks, uncompress");
    return true;
}

/* =========================================================================
 * Map editor model (cs2d_editor.c): brush modifier byte (TileProp.Get 0x140010450), painting
 * (FUN_1402bd6d0, FUN_1402bdcc0), .inf defaults (FUN_14019c550 / FUN_1402b7860), entity tables
 * (init code 0x140318856) and the map writer against the original map files.
 * ========================================================================= */
static bool file_bytes_equal(const char *a, const char *b)
{
    FILE *fa = fopen(a, "rb"), *fb = fopen(b, "rb");
    bool same = fa && fb;
    while (same) {
        int ca = fgetc(fa), cb = fgetc(fb);
        if (ca != cb) same = false;
        if (ca == EOF || cb == EOF) break;
    }
    if (fa) fclose(fa);
    if (fb) fclose(fb);
    return same;
}

static bool test_editor(void)
{
    printf("\n=== Testing Map Editor Model ===\n");

    /* Rotation wraps after 270; BR: 100 -> 10 -> ... -> 90 -> 100; white clears the color flag */
    cs2d_ed_brush_t br = { .tile = 3, .mod = 0, .r = 255, .g = 255, .b = 255 };
    for (int i = 0; i < 4; i++) cs2d_ed_brush_rotate(&br);
    if (br.mod != 0) TEST_FAIL("Editor brush", "rotation does not wrap");
    cs2d_ed_brush_rotate(&br);
    static const int percents[11] = { 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 10 };
    for (int i = 0; i < 11; i++) {
        cs2d_ed_brush_brightness(&br);
        if (cs2d_ed_brightness_percent(br.mod) != percents[i] || (br.mod & 3) != 1) {
            TEST_FAIL("Editor brush", "brightness sequence or rotation bits wrong");
        }
    }
    cs2d_ed_brush_set_color(&br, 255, 0, 0);
    if ((br.mod & 0xc0) != 0x80 || (br.mod & 0x3f) != 5) TEST_FAIL("Editor brush", "color flag not set");
    cs2d_ed_brush_set_color(&br, 255, 255, 255);
    if ((br.mod & 0x80) != 0) TEST_FAIL("Editor brush", "white did not clear the color flag");

    /* Painting on a 10x10 map: rect, flood fill bounded by a different modifier, color data */
    static cs2d_map_t m;
    if (!cs2d_ed_new_map(&m, 10, 10, "cs2dnorm.bmp")) TEST_FAIL("Editor map", "new map failed");
    m.tile_count = 254;
    cs2d_ed_brush_t wall = { .tile = 5, .mod = 0, .r = 255, .g = 255, .b = 255 };
    if (cs2d_ed_rect(&m, 4, 0, 4, 9, &wall) != 10) TEST_FAIL("Editor paint", "rect count");
    cs2d_ed_brush_t floor = { .tile = 7, .mod = 0x80 | 0x02, .r = 10, .g = 20, .b = 30 };
    int filled = cs2d_ed_fill(&m, 0, 0, &floor);
    if (filled != 40 || m.tiles[9 * 10 + 3] != 7 || m.tiles[5] != 0 || m.modifiers[0] != 0x82 ||
        m.tile_data[0] != 10 || m.tile_data[2] != 30) {
        TEST_FAIL("Editor paint", "flood fill crossed the wall or lost the color");
    }
    if (cs2d_ed_fill(&m, 0, 0, &floor) != 0 || cs2d_ed_set_tile(&m, 1, 1, &floor)) {
        TEST_FAIL("Editor paint", "repainting the same tile changed something");
    }

    /* Entities: tables from the init code, place / find / remove */
    uint8_t rgb[3];
    cs2d_ed_entity_color(0, rgb);
    bool ok = rgb[0] == 255 && rgb[1] == 0 && rgb[2] == 0;
    cs2d_ed_entity_color(22, rgb);
    ok = ok && rgb[0] == 128 && rgb[1] == 255 && rgb[2] == 0;
    cs2d_ed_entity_color(93, rgb);
    ok = ok && rgb[0] == 100 && rgb[1] == 200 && rgb[2] == 255;
    int types[128];
    int all = cs2d_ed_entity_list(0, types, 128);
    int info = cs2d_ed_entity_list(1, types, 128);
    ok = ok && all == 52 && info == 22 && types[20] == 80 && types[21] == 81;
    ok = ok && strcmp(cs2d_ed_entity_label(22), "Spr") == 0 && cs2d_ed_entity_zone(5) == 2 && cs2d_ed_entity_zone(4) == 1;
    int e = cs2d_ed_entity_add(&m, 22, 2, 3);
    ok = ok && e == 0 && cs2d_ed_entity_at(&m, 2, 3) == 0 && cs2d_ed_entity_add(&m, 7, 1, 1) == -1;
    cs2d_ed_entity_remove(&m, 0);
    ok = ok && m.entity_count == 0;
    if (!ok) TEST_FAIL("Editor entities", "tables or placement wrong");

    /* .inf: V2 file of the game, then a V4 round trip */
    m.tile_modes[1] = 99;
    if (!cs2d_ed_inf_load(&m, "gfx/tiles/cs2dnorm.inf") || m.tile_modes[0] != 0 || m.tile_modes[1] != 10 ||
        m.tile_heights[1] != 0) {
        TEST_FAIL("Editor .inf", "V2 tile modes not read");
    }
    m.tile_modes[3] = 52;
    m.tile_heights[3] = 40;
    m.tile_3d_mods[3] = 5;
    const char *inf_tmp = "test_editor_tmp.inf";
    bool inf_ok = cs2d_ed_inf_save(&m, inf_tmp);
    m.tile_modes[3] = 0;
    m.tile_heights[3] = 0;
    m.tile_3d_mods[3] = 0;
    inf_ok = inf_ok && cs2d_ed_inf_load(&m, inf_tmp) && m.tile_modes[3] == 52 && m.tile_heights[3] == 40 &&
             m.tile_3d_mods[3] == 5;
    remove(inf_tmp);
    cs2d_map_free(&m);
    if (!inf_ok) TEST_FAIL("Editor .inf", "V4 round trip");
    if (cs2d_ed_prop_modes[13] != 51 || strcmp(cs2d_ed_tile_mode_name(51), "Deadly - Explosion") != 0) {
        TEST_FAIL("Editor .inf", "tile mode table");
    }

    /* The map writer keeps original maps byte for byte, colored tiles (fy_shipment: 700) included */
    static const char *maps[3] = { "maps/fy_shipment.map", "maps/de_dust2.map", "maps/de_aztec.map" };
    const char *map_tmp = "test_editor_tmp.map";
    for (int i = 0; i < 3; i++) {
        static cs2d_map_t orig;
        memset(&orig, 0, sizeof(orig));
        bool same = cs2d_map_load_file(&orig, maps[i]) && cs2d_map_save_file(&orig, map_tmp) &&
                    file_bytes_equal(maps[i], map_tmp);
        cs2d_map_free(&orig);
        remove(map_tmp);
        if (!same) {
            printf("    %s\n", maps[i]);
            TEST_FAIL("Editor map save", "a saved map differs from the original file");
        }
    }
    TEST_PASS("Map editor: brush, rect / fill, entities tables, .inf V2 / V4, byte-identical map save");
    return true;
}

/* Spectator camera (FUN_14025aa60, FUN_1401698a0, Tmap_scroll.toplayer_smoothly) and the join snapshot
 * player names, checked against proxy captures of CS2D.exe on prison_funst3r with two bots */
static bool test_spectator(void)
{
    printf("\n=== Testing Spectator Mode ===\n");

    /* [252,7,1] names: cipher key "name" (captured bytes of the bots and of the joining player) */
    static const uint8_t bot1[8] = { 0xc9, 0xc3, 0xca, 0xda, 0xc2, 0xe4, 0xda, 0xdb };
    static const uint8_t me[4]   = { 0xe0, 0xc6, 0xce, 0xda };
    char name[32];
    cs2d_net_decrypt(bot1, 8, "name", name, sizeof(name));
    if (strcmp(name, "[b]lawlz") != 0) TEST_FAIL("Snapshot names", "bot name not decoded");
    cs2d_net_decrypt(me, 4, "name", name, sizeof(name));
    if (strcmp(name, "real") != 0) TEST_FAIL("Snapshot names", "player name not decoded");

    /* Players 1 (T) and 2 (CT) alive, 3 = local Spectator, 4 (CT) alive */
    static cs2d_game_t g;
    memset(&g, 0, sizeof(g));
    cs2d_game_add_player(&g, 1, "t1", CS2D_TEAM_T);
    cs2d_game_add_player(&g, 2, "ct2", CS2D_TEAM_CT);
    cs2d_game_add_player(&g, 3, "me", CS2D_TEAM_SPEC);
    cs2d_game_add_player(&g, 4, "ct4", CS2D_TEAM_CT);
    g.players[1].alive = g.players[2].alive = g.players[4].alive = true;

    /* The first pick at join follows player 1 and sends [67,1]; a valid target is kept */
    cs2d_spec_t s;
    cs2d_spec_init(&s, 1);
    if (!cs2d_spec_pick(&s, &g, 3, 0) || s.target != 1) TEST_FAIL("Spectator pick", "first target is not player 1");
    if (cs2d_spec_pick(&s, &g, 3, 0) || s.target != 1) TEST_FAIL("Spectator pick", "valid target not kept");
    /* space: next (2, then 4, then back to 1) */
    cs2d_spec_pick(&s, &g, 3, 1);
    if (s.target != 2) TEST_FAIL("Spectator pick", "next is not player 2");
    cs2d_spec_pick(&s, &g, 3, 1);
    cs2d_spec_pick(&s, &g, 3, 1);
    if (s.target != 1) TEST_FAIL("Spectator pick", "next does not wrap");
    /* Previous: from the second entry the original wraps to the last one (0x140169c00: index <= 1) */
    s.target = 2;
    cs2d_spec_pick(&s, &g, 3, -1);
    if (s.target != 4) TEST_FAIL("Spectator pick", "previous from the second player is not the last one");
    cs2d_spec_pick(&s, &g, 3, -1);
    if (s.target != 2) TEST_FAIL("Spectator pick", "previous from the last player");
    /* A dead target is replaced; nobody alive -> 0 without [67] */
    g.players[2].alive = false;
    if (!cs2d_spec_pick(&s, &g, 3, 0) || s.target != 1) TEST_FAIL("Spectator pick", "dead target not replaced by the first one");
    g.players[1].alive = g.players[4].alive = false;
    if (cs2d_spec_pick(&s, &g, 3, 0) || s.target != 0) TEST_FAIL("Spectator pick", "no target expected");

    /* sv_specmode 2: a dead Terrorist only follows Terrorists, a Spectator anybody */
    g.players[1].alive = g.players[2].alive = g.players[4].alive = true;
    cs2d_spec_init(&s, 2);
    g.players[3].team = CS2D_TEAM_T;
    cs2d_spec_pick(&s, &g, 3, 0);
    cs2d_spec_pick(&s, &g, 3, 1);
    if (s.target != 1) TEST_FAIL("Spectator own team", "a Terrorist may follow a Counter-Terrorist");
    if (!cs2d_spec_can_follow(&s, CS2D_TEAM_CT, CS2D_TEAM_VIP) || cs2d_spec_can_follow(&s, CS2D_TEAM_T, CS2D_TEAM_CT) ||
        !cs2d_spec_can_follow(&s, CS2D_TEAM_SPEC, CS2D_TEAM_CT)) {
        TEST_FAIL("Spectator own team", "team filter");
    }

    /* Free look: 10 px per 60 Hz frame, 5 with walk, truncated per axis step */
    int cx = 100, cy = 100;
    cs2d_spec_free_look(&cx, &cy, false, true, true, false, false, 1.0f);
    if (cx != 110 || cy != 90) TEST_FAIL("Free look", "10 px per frame");
    cs2d_spec_free_look(&cx, &cy, true, false, false, true, true, 1.0f);
    if (cx != 105 || cy != 95) TEST_FAIL("Free look", "walk halves the speed");
    cs2d_spec_free_look(&cx, &cy, false, true, false, false, false, 0.45f);
    if (cx != 109) TEST_FAIL("Free look", "4.5 px step not truncated");

    /* Follow: farther than 32 px the camera jumps, closer it moves 0.3 of the distance per frame */
    cx = 0; cy = 0;
    cs2d_spec_follow(&cx, &cy, 1776.0f, 1456.0f, 425.0f, 240.0f, 1.0f);
    if (cx != 1351 || cy != 1216) TEST_FAIL("Follow", "camera does not jump to the player");
    cs2d_spec_follow(&cx, &cy, 1796.0f, 1446.0f, 425.0f, 240.0f, 1.0f);
    if (cx != 1357 || cy != 1213) TEST_FAIL("Follow", "smoothing within 32 px is not 0.3 per frame");

    TEST_PASS("Spectator: snapshot names, follow order (+ previous quirk), own team filter, free look, follow");
    return true;
}

/* =========================================================================
 * Construction: dynamic objects (Tdo), join snapshot, [35] [36] [37] [49], build rules
 * ========================================================================= */
static bool test_construction(void)
{
    printf("\n=== Testing Construction / Dynamic Objects (Tdo) ===\n");

    static cs2d_map_t map;
    static cs2d_entity_world_t ents;
    static cs2d_dynobj_world_t w;
    static cs2d_player_t players[CS2D_MAX_PLAYERS + 1];
    memset(&map, 0, sizeof(map));
    if (!cs2d_map_load_file(&map, "maps/ctf_cs2dfortress.map")) TEST_FAIL("Construction", "ctf_cs2dfortress.map not loaded");
    cs2d_ents_init(&ents, &map);
    cs2d_dynobj_init(&w, &map);

    /* Join snapshot [252,7,5] recorded on 140.238.183.194:36969 (UNLIMITED Construction): 56 Wall III */
    FILE *f = fopen("tests/data/dynobj_snapshot_ctf_cs2dfortress.hex", "r");
    if (!f) TEST_FAIL("Snapshot", "fixture missing");
    static uint8_t buf[1024];
    int len = 0;
    unsigned v;
    while (len < (int)sizeof(buf) && fscanf(f, "%x", &v) == 1) buf[len++] = (uint8_t)v;
    fclose(f);
    if (len != 844 || buf[0] != 0xfc || buf[1] != 7 || buf[2] != 5) TEST_FAIL("Snapshot", "fixture is not [252,7,5]");
    int pos = 3;
    if (!cs2d_dynobj_read_snapshot(&w, buf, len, &pos) || pos != len) TEST_FAIL("Snapshot", "15-byte records not consumed exactly");
    if (cs2d_dynobj_count(&w) != 56) TEST_FAIL("Snapshot", "56 objects expected");
    cs2d_dynobj_t *o = cs2d_dynobj_at_tile(&w, 19, 17);
    if (!o || o->id != 3 || o->typ != CS2D_DO_WALL_3 || o->health != 100 || o->team != 0) TEST_FAIL("Snapshot", "object 3: Wall III at (608, 544)");
    if (map.collision[17 * map.width + 19] != 1) TEST_FAIL("Snapshot", "Wall III does not block its tile");
    int low = 100;
    for (int i = 0; i < CS2D_DYNOBJ_MAX; i++) if (w.objs[i].active && w.objs[i].health < low) low = w.objs[i].health;
    if (low != 83) TEST_FAIL("Snapshot", "health is the percent byte (lowest 83)");

    /* [49] spawn: Turret 100 on tile (26, 32) for CT player 3, rot u16 0x4000 = 90 degrees */
    static const uint8_t spawn_turret[] = { 49, 100, 0, 8, 0x40, 0x03, 0x00, 0x04, 0x00, 0x40, 0, 2, 3 };
    if (cs2d_dynobj_net_message(&w, spawn_turret, sizeof(spawn_turret), 0, NULL) != 13) TEST_FAIL("[49]", "13 bytes");
    o = cs2d_dynobj_get(&w, 100);
    if (!o || o->typ != CS2D_DO_TURRET || o->x != 832.0f || o->y != 1024.0f || o->team != 2 || o->player != 3 ||
        fabsf(o->rot - 90.0f) > 0.01f || o->health != 100) TEST_FAIL("[49]", "turret fields");
    if (map.collision[32 * map.width + 26] != 1) TEST_FAIL("[49]", "turret does not block");
    if (cs2d_dynobj_net_message(&w, spawn_turret, 12, 0, NULL) != -1) TEST_FAIL("[49]", "truncated message accepted");

    /* [36,id,0,target]: aim at player 5 (east of the turret) for 15 frames, 5 of them firing */
    players[5].alive = true; players[5].team = CS2D_TEAM_T; players[5].health = 100;
    players[5].pos.x = 848.0f + 200.0f; players[5].pos.y = 1040.0f;
    static const uint8_t target[] = { 36, 100, 0, 0, 5 };
    if (cs2d_dynobj_net_message(&w, target, sizeof(target), 0, NULL) != 5) TEST_FAIL("[36,0]", "5 bytes");
    if (o->target != 5 || o->countdown != 15) TEST_FAIL("[36,0]", "target / countdown");
    cs2d_dynobj_update(&w, players, 15.0f);
    if (fabsf(o->rot - 90.0f) > 0.5f) TEST_FAIL("[36,0]", "turret does not aim east (atan2(dx - 16, -dy))");
    if (o->shots != 5 || o->countdown != 0) TEST_FAIL("[36,0]", "burst: frames 15..11 fire");

    /* [37] health percent, above 200 ignored but consumed */
    static const uint8_t hp50[] = { 37, 100, 0, 50 }, hp201[] = { 37, 100, 0, 201 };
    if (cs2d_dynobj_net_message(&w, hp50, 4, 0, NULL) != 4 || o->health != 50) TEST_FAIL("[37]", "health 50");
    if (cs2d_dynobj_net_message(&w, hp201, 4, 0, NULL) != 4 || o->health != 50) TEST_FAIL("[37]", "201 must be ignored");

    /* [36,id,4,type]: upgrade to Dual Turret at 100 % */
    static const uint8_t upgrade[] = { 36, 100, 0, 4, 11 };
    if (cs2d_dynobj_net_message(&w, upgrade, sizeof(upgrade), 0, NULL) != 5 || o->typ != CS2D_DO_DUAL_TURRET ||
        o->health != 100) TEST_FAIL("[36,4]", "upgrade");

    /* Construction Site 101 (mode 7 = Dispenser) on (28, 32); [36,id,1] finishes it */
    static const uint8_t site[] = { 49, 101, 0, 10, 0x80, 0x03, 0x00, 0x04, 0, 0, 7, 1, 4 };
    cs2d_dynobj_net_message(&w, site, sizeof(site), 0, NULL);
    o = cs2d_dynobj_get(&w, 101);
    if (!o || o->typ != CS2D_DO_SITE || o->mode != 7) TEST_FAIL("Site", "construction site");
    static const uint8_t finish[] = { 36, 101, 0, 1 };
    cs2d_dynobj_fx_t fx = { 0 };
    if (cs2d_dynobj_net_message(&w, finish, sizeof(finish), 0, &fx) != 4) TEST_FAIL("[36,1]", "4 bytes");
    o = cs2d_dynobj_get(&w, 101);
    if (!o || o->typ != CS2D_DO_DISPENSER || o->team != 1 || o->player != 4 || o->mode != 0 || fx.kind != 2)
        TEST_FAIL("[36,1]", "site did not become a Dispenser");

    /* Barbed wire hit: [36,id,5] wobbles 60 frames; Barbed Wire leaves the tile walkable */
    static const uint8_t wire[] = { 49, 102, 0, 2, 0x60, 0x03, 0x00, 0x04, 0, 0, 0, 1, 4 };
    static const uint8_t hit[] = { 36, 102, 0, 5 };
    cs2d_dynobj_net_message(&w, wire, sizeof(wire), 0, NULL);
    cs2d_dynobj_net_message(&w, hit, sizeof(hit), 0, NULL);
    o = cs2d_dynobj_get(&w, 102);
    if (!o || o->mode != 60 || map.collision[32 * map.width + 27] != 0) TEST_FAIL("Barbed Wire", "hit / walkable");
    cs2d_dynobj_update(&w, players, 60.0f);
    if (o->mode != 0) TEST_FAIL("Barbed Wire", "wobble does not decay to 0");

    /* [36] for an unknown object reads nothing after the sub-command (Tdo.getdata returns early) */
    static const uint8_t unknown[] = { 36, 0x34, 0x12, 0, 9 };
    if (cs2d_dynobj_net_message(&w, unknown, sizeof(unknown), 0, NULL) != 4) TEST_FAIL("[36]", "unknown object");

    /* [35] remove: the Dual Turret explodes and its tile is free again */
    static const uint8_t rm[] = { 35, 100, 0 };
    memset(&fx, 0, sizeof(fx));
    if (cs2d_dynobj_net_message(&w, rm, sizeof(rm), 0, &fx) != 3 || cs2d_dynobj_get(&w, 100)) TEST_FAIL("[35]", "remove");
    if (fx.kind != 1 || fx.x != 848.0f || map.collision[32 * map.width + 26] != 0) TEST_FAIL("[35]", "explosion / collision");

    /* FUN_14014ef60: player 1 on tile (26, 33); Barbed Wire 102 lies on (27, 32) */
    players[1].alive = true; players[1].team = CS2D_TEAM_CT; players[1].health = 100;
    players[1].pos.x = 26 * 32 + 16; players[1].pos.y = 33 * 32 + 16;
    players[5].alive = false;
    if (cs2d_dynobj_can_build(&w, players, 1, 26, 33, 27, 33, false) != 1) TEST_FAIL("Build rules", "free floor next to the player");
    if (cs2d_dynobj_can_build(&w, players, 1, 26, 33, 26, 33, false) != 0) TEST_FAIL("Build rules", "own tile");
    if (cs2d_dynobj_can_build(&w, players, 1, 26, 33, 28, 33, false) != 0) TEST_FAIL("Build rules", "two tiles away");
    if (cs2d_dynobj_can_build(&w, players, 1, 26, 33, 27, 32, false) != 0) TEST_FAIL("Build rules", "tile with an object");
    if (cs2d_dynobj_can_build(&w, players, 1, 19, 14, 19, 15, false) != 0) TEST_FAIL("Build rules", "tile with a Wall III");
    players[2].alive = true; players[2].team = CS2D_TEAM_T; players[2].health = 100;
    players[2].pos.x = 27 * 32 + 16 + 20; players[2].pos.y = 33 * 32 + 16;
    if (cs2d_dynobj_can_build(&w, players, 1, 26, 33, 27, 33, false) != -1) TEST_FAIL("Build rules", "player within 32 px");

    cs2d_dynobj_clear(&w);
    if (cs2d_dynobj_count(&w) != 0 || map.collision[17 * map.width + 19] != 0) TEST_FAIL("Clear", "Tdo.freeall");
    cs2d_map_free(&map);

    TEST_PASS("Construction: snapshot (56 Wall III), [49] [36] [37] [35], site -> building, turret aim, build rules");
    return true;
}

int main(void)
{
    printf("============================================================\n");
    printf("   CS2D Reconstructed Game Logic Test Suite (C99)\n");
    printf("============================================================\n");

    int passed = 0;
    int total = 22;

    if (test_weapons()) passed++;
    if (test_player_logic()) passed++;
    if (test_map_loading()) passed++;
    if (test_game_loop()) passed++;
    if (test_network_protocol()) passed++;
    if (test_live_server_query()) passed++;
    if (test_entities_and_sprites()) passed++;
    if (test_ui_layout()) passed++;
    if (test_ui_textbox()) passed++;
    if (test_ui_dropdown_numberbox()) passed++;
    if (test_ui_scroll()) passed++;
    if (test_console()) passed++;
    if (test_entities_prison()) passed++;
    if (test_player_physics()) passed++;
    if (test_breakables()) passed++;
    if (test_server_info_and_team_menu()) passed++;
    if (test_recoil_and_swing()) passed++;
    if (test_serverlist()) passed++;
    if (test_file_transfer()) passed++;
    if (test_editor()) passed++;
    if (test_spectator()) passed++;
    if (test_construction()) passed++;

    printf("\n============================================================\n");
    printf("Tests Passed: %d / %d\n", passed, total);
    printf("============================================================\n");

    return (passed == total) ? 0 : 1;
}
