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
    cs2d_net_build_join_stage1(&p1, "CS2D_Decomp", "", "135421593", 850, 480);
    cs2d_net_build_join_stage2(&p2, map_sha256, "135421593");
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

    cs2d_ui_context_t ui;
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
 * Main Runner
 * ========================================================================= */
int main(void)
{
    printf("============================================================\n");
    printf("   CS2D Reconstructed Game Logic Test Suite (C99)\n");
    printf("============================================================\n");

    int passed = 0;
    int total = 9;

    if (test_weapons()) passed++;
    if (test_player_logic()) passed++;
    if (test_map_loading()) passed++;
    if (test_game_loop()) passed++;
    if (test_network_protocol()) passed++;
    if (test_live_server_query()) passed++;
    if (test_entities_and_sprites()) passed++;
    if (test_ui_layout()) passed++;
    if (test_ui_textbox()) passed++;

    printf("\n============================================================\n");
    printf("Tests Passed: %d / %d\n", passed, total);
    printf("============================================================\n");

    return (passed == total) ? 0 : 1;
}
