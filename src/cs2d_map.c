#include "cs2d_map.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Binary stream reader matching BlitzMax TStream (ReadByte +0xd8, ReadShort +0xe8, ReadInt +0x100, ReadLine +0x140) */
typedef struct {
    const uint8_t *data;
    size_t         size;
    size_t         pos;
} cs2d_stream_t;

static bool stream_eof(const cs2d_stream_t *s)
{
    return s->pos >= s->size;
}

static uint8_t stream_read_byte(cs2d_stream_t *s)
{
    if (s->pos >= s->size) return 0;
    return s->data[s->pos++];
}

static uint16_t stream_read_short(cs2d_stream_t *s)
{
    if (s->pos + 2 > s->size) {
        s->pos = s->size;
        return 0;
    }
    uint16_t v = (uint16_t)s->data[s->pos] | ((uint16_t)s->data[s->pos + 1] << 8);
    s->pos += 2;
    return v;
}

static int32_t stream_read_int(cs2d_stream_t *s)
{
    if (s->pos + 4 > s->size) {
        s->pos = s->size;
        return 0;
    }
    uint32_t v = (uint32_t)s->data[s->pos]
               | ((uint32_t)s->data[s->pos + 1] << 8)
               | ((uint32_t)s->data[s->pos + 2] << 16)
               | ((uint32_t)s->data[s->pos + 3] << 24);
    s->pos += 4;
    return (int32_t)v;
}

/* Matches BlitzMax TStream.ReadLine (+0x140) and CS2D spec: 8-bit string ended with CR+LF (0x0D 0x0A) */
static void stream_read_line(cs2d_stream_t *s, char *out_buf, size_t max_len)
{
    size_t out_idx = 0;
    while (s->pos < s->size) {
        uint8_t c = s->data[s->pos++];
        if (c == '\r') {
            if (s->pos < s->size && s->data[s->pos] == '\n') {
                s->pos++;
            }
            break;
        }
        if (c == '\n') {
            break;
        }
        if (out_buf && max_len > 0 && out_idx < max_len - 1) {
            out_buf[out_idx++] = (char)c;
        }
    }
    if (out_buf && max_len > 0) {
        out_buf[out_idx] = '\0';
    }
}

uint8_t cs2d_map_mode_collision(uint8_t mode)
{
    if (mode == CS2D_TILE_WALL || mode == CS2D_TILE_WALL_NOSHADOW || mode == CS2D_TILE_WALL_FLOORLEVEL) {
        return 1; /* Solid wall (blocks movement & bullets) */
    }
    if (mode == CS2D_TILE_OBSTACLE || mode == CS2D_TILE_OBSTACLE_NOSHADOW) {
        return 2; /* Obstacle (blocks movement, allows bullets) */
    }
    if (mode >= CS2D_TILE_DEADLY_NORMAL && mode <= CS2D_TILE_DEADLY_ABYSS) {
        return 3; /* Deadly tile */
    }
    return 0;     /* Walkable floor */
}

/* g_prop_class (REA): the class of a tile mode, 0 wall, 1 floor, 2 obstacle, -1 deadly. Everything the
 * wall / obstacle / deadly lists do not name is a floor, including the unknown modes. */
int cs2d_map_prop_class(uint8_t mode)
{
    if (mode == CS2D_TILE_WALL || mode == CS2D_TILE_WALL_NOSHADOW || mode == CS2D_TILE_WALL_FLOORLEVEL) return 0;
    if (mode == CS2D_TILE_OBSTACLE || mode == CS2D_TILE_OBSTACLE_NOSHADOW) return 2;
    if (mode >= CS2D_TILE_DEADLY_NORMAL && mode <= CS2D_TILE_DEADLY_ABYSS) return -1;
    return 1;
}

/* Reconstructed directly from FUN_1400493d0 & FUN_140113d50 in cs2d_dedicated.exe */
bool cs2d_map_load_file(cs2d_map_t *map, const char *filepath)
{
    if (!map || !filepath) return false;
    memset(map, 0, sizeof(*map));

    FILE *f = fopen(filepath, "rb");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize < 32) {
        fclose(f);
        return false;
    }

    uint8_t *buffer = (uint8_t *)malloc((size_t)fsize);
    if (!buffer) {
        fclose(f);
        return false;
    }

    if (fread(buffer, 1, (size_t)fsize, f) != (size_t)fsize) {
        free(buffer);
        fclose(f);
        return false;
    }
    fclose(f);

    cs2d_stream_t s = { buffer, (size_t)fsize, 0 };

    /* (1) HEADER: FUN_140113d50 lines 118-126 */
    stream_read_line(&s, map->header, sizeof(map->header));
    if (strstr(map->header, "Unreal Software's Counter-Strike 2D Map File") == NULL &&
        strstr(map->header, "Unreal Software's CS2D Map File") == NULL) {
        free(buffer);
        return false;
    }

    /* 10 bytes for map settings / info: FUN_140113d50 lines 132-137 */
    uint8_t b_settings[10];
    for (int i = 0; i < 10; i++) {
        b_settings[i] = stream_read_byte(&s);
    }
    map->scroll_map        = b_settings[0]; /* DAT_1407dfa80 */
    map->use_modifiers     = b_settings[1];
    map->save_tile_heights = b_settings[2];
    map->use_64x64_tiles   = b_settings[3]; /* DAT_1407dfa84 */
    memcpy(map->header_unused_bytes, &b_settings[4], 6);

    /* 10 ints for map settings / info: FUN_140113d50 lines 139-165 */
    int32_t i_settings[10];
    for (int i = 0; i < 10; i++) {
        i_settings[i] = stream_read_int(&s);
    }
    map->sys_uptime         = i_settings[0]; /* _DAT_1407dfa50 */
    map->author_usgn_offset = i_settings[1] - 0x33; /* _DAT_1407dfad0 = puVar15[1] - 51 */
    map->daylight_time      = 0xe1; /* 225 default */
    if (i_settings[2] > 999) {
        map->daylight_time = (i_settings[2] - 1000) % 360;
    }
    memcpy(map->header_unused_ints, &i_settings[3], 7 * sizeof(int32_t));

    /* 10 strings for map settings / info: FUN_140113d50 lines 146-151, 161 */
    stream_read_line(&s, map->author, sizeof(map->author)); /* PTR_PTR_1405a18c8 */
    stream_read_line(&s, map->save_program, sizeof(map->save_program));
    for (int i = 2; i < 10; i++) {
        stream_read_line(&s, NULL, 0);
    }

    /* More map settings: FUN_140113d50 lines 167-201 */
    stream_read_line(&s, map->check_string, sizeof(map->check_string)); /* PTR_PTR_1405a18d8 */
    stream_read_line(&s, map->tileset, sizeof(map->tileset));           /* PTR_PTR_1405a1590 */
    map->tile_count = stream_read_byte(&s);                             /* DAT_1407dfa4c */
    int32_t map_xsize = stream_read_int(&s);                            /* DAT_1407dfaa0 */
    int32_t map_ysize = stream_read_int(&s);                            /* DAT_1407dfa9c */
    stream_read_line(&s, map->bg_image, sizeof(map->bg_image));         /* PTR_PTR_1405a1880 */
    map->bg_scroll_x = stream_read_int(&s);                             /* DAT_1407dfa8c */
    map->bg_scroll_y = stream_read_int(&s);                             /* _DAT_1407dfa88 */
    map->bg_r = stream_read_byte(&s);                                   /* PTR_DAT_1405a1870[0] */
    map->bg_g = stream_read_byte(&s);                                   /* PTR_DAT_1405a1870[1] */
    map->bg_b = stream_read_byte(&s);                                   /* PTR_DAT_1405a1870[2] */

    if (map_xsize < 0) map_xsize = 0;
    if (map_ysize < 0) map_ysize = 0;
    if (map_xsize > 1000) map_xsize = 1000;
    if (map_ysize > 1000) map_ysize = 1000;

    map->width  = (uint32_t)(map_xsize + 1);
    map->height = (uint32_t)(map_ysize + 1);

    /* Header Test: FUN_140113d50 lines 239-247 */
    char header_test[64];
    stream_read_line(&s, header_test, sizeof(header_test));
    if (strcmp(header_test, "ed.erawtfoslaernu") != 0) {
        free(buffer);
        return false;
    }

    /* (2) Tile Modes (tile_count + 1 bytes): FUN_140113d50 lines 258-272 */
    int tile_max = (int)map->tile_count;
    for (int i = 0; i <= tile_max; i++) {
        map->tile_modes[i] = stream_read_byte(&s);
    }

    /* (3) Tile Heights: FUN_140113d50 lines 273-341 */
    if (map->save_tile_heights == 1) {
        for (int i = 0; i <= tile_max; i++) {
            map->tile_heights[i] = (uint16_t)stream_read_int(&s);
            map->tile_3d_mods[i] = 0;
        }
    } else if (map->save_tile_heights == 2) {
        for (int i = 0; i <= tile_max; i++) {
            map->tile_heights[i] = stream_read_short(&s);
            map->tile_3d_mods[i] = stream_read_byte(&s);
        }
    } else {
        for (int i = 0; i <= tile_max; i++) {
            uint8_t mode = map->tile_modes[i];
            if (mode == CS2D_TILE_WALL || mode == CS2D_TILE_WALL_NOSHADOW || mode == CS2D_TILE_WALL_FLOORLEVEL) {
                map->tile_heights[i] = 32; /* 0x20 */
            } else if (mode == CS2D_TILE_OBSTACLE || mode == CS2D_TILE_OBSTACLE_NOSHADOW) {
                map->tile_heights[i] = 16; /* 0x10 */
            } else {
                map->tile_heights[i] = 0;
            }
            map->tile_3d_mods[i] = 0;
        }
    }

    /* (4) Map Grid: FUN_140113d50 lines 342-370 */
    size_t total_tiles = (size_t)map->width * (size_t)map->height;
    map->tiles     = (uint8_t *)calloc(total_tiles, sizeof(uint8_t));
    map->modifiers = (uint8_t *)calloc(total_tiles, sizeof(uint8_t));
    map->collision = (uint8_t *)calloc(total_tiles, sizeof(uint8_t));
    map->tile_data = (uint8_t *)calloc(total_tiles * 4, sizeof(uint8_t));

    if (!map->tiles || !map->modifiers || !map->collision || !map->tile_data) {
        cs2d_map_free(map);
        free(buffer);
        return false;
    }

    /* Nested loop: for x = 0 to map_xsize; for y = 0 to map_ysize */
    for (uint32_t x = 0; x < map->width; x++) {
        for (uint32_t y = 0; y < map->height; y++) {
            uint8_t t = stream_read_byte(&s);
            size_t idx = (size_t)y * (size_t)map->width + (size_t)x;
            map->tiles[idx] = t;

            map->collision[idx] = cs2d_map_mode_collision(map->tile_modes[t]);
        }
    }

    /* Optional Tile Modifiers: FUN_140113d50 lines 372-462 */
    if (map->use_modifiers == 1) {
        for (uint32_t x = 0; x < map->width; x++) {
            for (uint32_t y = 0; y < map->height; y++) {
                uint8_t mod = stream_read_byte(&s);
                size_t idx = (size_t)y * (size_t)map->width + (size_t)x;
                map->modifiers[idx] = mod;
                if (mod > 0x3f) {
                    uint8_t flags = mod & 0xc0;
                    uint8_t *d = &map->tile_data[idx * 4];
                    if (flags == 0xc0) {
                        stream_read_line(&s, NULL, 0);
                    } else if (flags == 0x40) {
                        d[3] = stream_read_byte(&s);         /* Blend frame */
                    } else {
                        /* flags == 0x80: R, G, B, overlay frame */
                        d[0] = stream_read_byte(&s);
                        d[1] = stream_read_byte(&s);
                        d[2] = stream_read_byte(&s);
                        d[3] = stream_read_byte(&s);
                    }
                }
            }
        }
    }

    /* (5) Entities: FUN_140113d50 lines 463-508 */
    int32_t ent_count = stream_read_int(&s);
    if (ent_count < 0 || ent_count >= 100001) { /* 0x186a1 check in FUN_140113d50 */
        cs2d_map_free(map);
        free(buffer);
        return false;
    }

    map->entity_count = 0;
    for (int32_t i = 0; i < ent_count; i++) {
        if (stream_eof(&s)) {
            break;
        }
        cs2d_entity_t temp_ent;
        memset(&temp_ent, 0, sizeof(temp_ent));

        stream_read_line(&s, temp_ent.name, sizeof(temp_ent.name));
        temp_ent.type = (cs2d_entity_type_t)stream_read_byte(&s);
        temp_ent.x    = stream_read_int(&s);
        temp_ent.y    = stream_read_int(&s);
        stream_read_line(&s, temp_ent.trigger, sizeof(temp_ent.trigger));

        for (int j = 0; j < 10; j++) {
            temp_ent.int_settings[j] = stream_read_int(&s);
            if (j == 0) {
                stream_read_line(&s, temp_ent.str0, sizeof(temp_ent.str0));
                snprintf(temp_ent.str_settings[0], sizeof(temp_ent.str_settings[0]), "%s", temp_ent.str0);
            } else {
                stream_read_line(&s, temp_ent.str_settings[j], sizeof(temp_ent.str_settings[j]));
            }
        }
        temp_ent.param1 = temp_ent.int_settings[0];
        temp_ent.param2 = temp_ent.int_settings[1];

        if (map->entity_count < CS2D_MAX_ENTITIES) {
            map->entities[map->entity_count++] = temp_ent;
        }
    }

    /* The entity table was replaced: invalidate the renderer's entity index (cs2d_render.c) */
    map->entity_table_version++;

    free(buffer);
    return true;
}

/* Reconstructed from FUN_1401f09b0 (savemap) */
static void write_line_crlf(FILE *f, const char *str)
{
    if (str && *str) {
        fwrite(str, 1, strlen(str), f);
    }
    fwrite("\r\n", 1, 2, f);
}

bool cs2d_map_save_file(const cs2d_map_t *map, const char *filepath)
{
    if (!map || !filepath || !map->tiles || map->width == 0 || map->height == 0) return false;

    FILE *f = fopen(filepath, "wb");
    if (!f) return false;

    write_line_crlf(f, "Unreal Software's CS2D Map File (max)");

    uint8_t b_settings[10] = {0};
    b_settings[0] = map->scroll_map;
    b_settings[1] = map->use_modifiers;
    b_settings[2] = 2; /* save_tile_heights = 2 since 1.0.0.3 */
    b_settings[3] = map->use_64x64_tiles;
    fwrite(b_settings, 1, 10, f);

    int32_t i_settings[10] = {0};
    i_settings[0] = map->sys_uptime;
    i_settings[1] = (map->author_usgn_offset > 0) ? (map->author_usgn_offset + 51) : 0;
    i_settings[2] = map->daylight_time + 1000;
    fwrite(i_settings, sizeof(int32_t), 10, f);

    write_line_crlf(f, map->author);
    write_line_crlf(f, map->save_program[0] ? map->save_program : "CS2D Client 1.0.0.3");
    for (int i = 2; i < 10; i++) {
        write_line_crlf(f, "");
    }

    write_line_crlf(f, map->check_string);
    write_line_crlf(f, map->tileset);
    fwrite(&map->tile_count, 1, 1, f);

    int32_t map_xsize = (int32_t)map->width - 1;
    int32_t map_ysize = (int32_t)map->height - 1;
    fwrite(&map_xsize, sizeof(int32_t), 1, f);
    fwrite(&map_ysize, sizeof(int32_t), 1, f);
    write_line_crlf(f, map->bg_image);
    fwrite(&map->bg_scroll_x, sizeof(int32_t), 1, f);
    fwrite(&map->bg_scroll_y, sizeof(int32_t), 1, f);
    fwrite(&map->bg_r, 1, 1, f);
    fwrite(&map->bg_g, 1, 1, f);
    fwrite(&map->bg_b, 1, 1, f);

    write_line_crlf(f, "ed.erawtfoslaernu");

    int tile_max = (int)map->tile_count;
    fwrite(map->tile_modes, 1, (size_t)(tile_max + 1), f);

    for (int i = 0; i <= tile_max; i++) {
        fwrite(&map->tile_heights[i], sizeof(uint16_t), 1, f);
        fwrite(&map->tile_3d_mods[i], 1, 1, f);
    }

    for (uint32_t x = 0; x < map->width; x++) {
        for (uint32_t y = 0; y < map->height; y++) {
            uint8_t t = map->tiles[y * map->width + x];
            fwrite(&t, 1, 1, f);
        }
    }

    if (map->use_modifiers == 1 && map->modifiers) {
        for (uint32_t x = 0; x < map->width; x++) {
            for (uint32_t y = 0; y < map->height; y++) {
                size_t idx = (size_t)y * map->width + x;
                uint8_t m = map->modifiers[idx];
                if ((m & 0xc0) == 0xc0) m &= 0x3f; /* The string form is unused */
                fwrite(&m, 1, 1, f);
                static const uint8_t no_data[4] = { 0, 0, 0, 0 };
                const uint8_t *d = map->tile_data ? &map->tile_data[idx * 4] : no_data;
                if ((m & 0xc0) == 0x40) {
                    fwrite(&d[3], 1, 1, f);
                } else if ((m & 0xc0) == 0x80) {
                    fwrite(d, 1, 4, f);
                }
            }
        }
    }

    int32_t ent_count = (int32_t)map->entity_count;
    fwrite(&ent_count, sizeof(int32_t), 1, f);
    for (uint32_t i = 0; i < map->entity_count; i++) {
        const cs2d_entity_t *e = &map->entities[i];
        write_line_crlf(f, e->name);
        uint8_t t = (uint8_t)e->type;
        fwrite(&t, 1, 1, f);
        fwrite(&e->x, sizeof(int32_t), 1, f);
        fwrite(&e->y, sizeof(int32_t), 1, f);
        write_line_crlf(f, e->trigger);
        for (int j = 0; j < 10; j++) {
            fwrite(&e->int_settings[j], sizeof(int32_t), 1, f);
            if (j == 0) {
                /* str0 holds the full string; if the editor changed str_settings[0], prefer it. */
                const char *s0 = (e->str0[0] && strncmp(e->str0, e->str_settings[0], 63) == 0)
                                 ? e->str0 : e->str_settings[0];
                write_line_crlf(f, s0);
            } else {
                write_line_crlf(f, e->str_settings[j]);
            }
        }
    }

    fclose(f);
    return true;
}

void cs2d_map_free(cs2d_map_t *map)
{
    if (!map) return;
    if (map->tiles) {
        free(map->tiles);
        map->tiles = NULL;
    }
    if (map->modifiers) {
        free(map->modifiers);
        map->modifiers = NULL;
    }
    if (map->collision) {
        free(map->collision);
        map->collision = NULL;
    }
    free(map->tile_data);
    map->tile_data = NULL;
    if (map->base_collision) {
        free(map->base_collision);
        map->base_collision = NULL;
    }
    free(map->object_collision);
    map->object_collision = NULL;
    free(map->object_shadow);
    map->object_shadow = NULL;
    free(map->custom_tiles);
    map->custom_tiles = NULL;
    map->custom_tile_count = map->custom_tile_cap = 0;
    map->width = 0;
    map->height = 0;
    map->entity_count = 0;
    map->entity_table_version++;   /* cs2d_render.c entity index */
}

uint8_t cs2d_map_get_tile(const cs2d_map_t *map, uint32_t tx, uint32_t ty)
{
    if (!map || !map->tiles || tx >= map->width || ty >= map->height) return 0;
    return map->tiles[ty * map->width + tx];
}

uint8_t cs2d_map_get_tile_mode(const cs2d_map_t *map, uint32_t tx, uint32_t ty)
{
    if (!map || !map->tiles || tx >= map->width || ty >= map->height) return CS2D_TILE_WALL;
    uint8_t tile_idx = map->tiles[ty * map->width + tx];
    return map->tile_modes[tile_idx];
}

bool cs2d_map_is_solid(const cs2d_map_t *map, float px, float py)
{
    if (!map || !map->collision) return false;

    int tx = (int)(px / (float)CS2D_TILE_SIZE);
    int ty = (int)(py / (float)CS2D_TILE_SIZE);

    if (tx < 0 || ty < 0 || (uint32_t)tx >= map->width || (uint32_t)ty >= map->height) {
        return true; /* Out of map bounds is solid wall */
    }

    uint8_t col = map->collision[(uint32_t)ty * map->width + (uint32_t)tx];
    return (col == 1 || col == 2); /* Wall or obstacle blocks movement */
}

bool cs2d_map_is_wall(const cs2d_map_t *map, float px, float py)
{
    if (!map || !map->collision) return false;

    int tx = (int)(px / (float)CS2D_TILE_SIZE);
    int ty = (int)(py / (float)CS2D_TILE_SIZE);

    if (tx < 0 || ty < 0 || (uint32_t)tx >= map->width || (uint32_t)ty >= map->height) {
        return true;
    }

    uint8_t col = map->collision[(uint32_t)ty * map->width + (uint32_t)tx];
    return (col == 1); /* Only walls block bullets/vision */
}

bool cs2d_map_get_spawn(const cs2d_map_t *map, cs2d_team_t team, size_t index, cs2d_vec2_t *out_pos)
{
    if (!map || !out_pos) return false;

    cs2d_entity_type_t target_type = (team == CS2D_TEAM_T) ? CS2D_ENT_INFO_T :
                                     (team == CS2D_TEAM_VIP) ? CS2D_ENT_INFO_VIP : CS2D_ENT_INFO_CT;
    size_t count = 0;

    for (uint32_t i = 0; i < map->entity_count; i++) {
        if (map->entities[i].type == target_type) {
            if (count == index) {
                /* Convert tile coordinates to pixel center of tile: tx * 32 + 16, ty * 32 + 16 */
                out_pos->x = (float)(map->entities[i].x * CS2D_TILE_SIZE + (CS2D_TILE_SIZE / 2));
                out_pos->y = (float)(map->entities[i].y * CS2D_TILE_SIZE + (CS2D_TILE_SIZE / 2));
                return true;
            }
            count++;
        }
    }
    return false;
}

size_t cs2d_map_count_spawns(const cs2d_map_t *map, cs2d_team_t team)
{
    if (!map) return 0;
    cs2d_entity_type_t target_type = (team == CS2D_TEAM_T) ? CS2D_ENT_INFO_T :
                                     (team == CS2D_TEAM_VIP) ? CS2D_ENT_INFO_VIP : CS2D_ENT_INFO_CT;
    size_t count = 0;
    for (uint32_t i = 0; i < map->entity_count; i++) {
        if (map->entities[i].type == target_type) count++;
    }
    return count;
}

bool cs2d_map_is_in_bombspot(const cs2d_map_t *map, float px, float py)
{
    if (!map) return false;
    int tx = (int)(px / (float)CS2D_TILE_SIZE);
    int ty = (int)(py / (float)CS2D_TILE_SIZE);
    return cs2d_map_in_zone(map, CS2D_ENT_INFO_BOMBSPOT, tx, ty);
}

/*
 * Reconstructed from Te.inzone (FUN_140033fd0 in CS2D.exe):
 * Checks if tile coordinate (tx, ty) is inside an entity of the given type.
 * - Spawn & objective zones (Info_T=0, Info_CT=1, Info_VIP=2, Info_RescuePoint=4,
 *   Info_BombSpot=5, Info_EscapePoint=6, Info_Target=7, Info_NoFOW=13,
 *   Info_CTF_Flag=15, Info_Dom_Point=17):
 *   radius = 1 tile -> abs(e->x - tx) <= 1 && abs(e->y - ty) <= 1 (3x3 tile area)
 * - Rectangular area entities (Info_NoBuildings=18, Info_TeamGate=20, Env_Room=32, Info_NoWeather=80):
 *   tx in [e->x .. e->x + max(0, int_settings[0] - 1)], ty in [e->y .. e->y + max(0, int_settings[1] - 1)]
 * - Env_Hurt (27):
 *   tx in [e->x .. e->x + max(0, int_settings[2] - 1)], ty in [e->y .. e->y + max(0, int_settings[3] - 1)]
 * - Point entities:
 *   tx == e->x && ty == e->y
 */
bool cs2d_map_in_zone(const cs2d_map_t *map, cs2d_entity_type_t type, int tx, int ty)
{
    if (!map) return false;

    int radius = 0;
    if (type == CS2D_ENT_INFO_T || type == CS2D_ENT_INFO_CT || type == CS2D_ENT_INFO_VIP ||
        type == CS2D_ENT_INFO_RESCUEPOINT || type == CS2D_ENT_INFO_BOMBSPOT ||
        type == CS2D_ENT_INFO_ESCAPEPOINT || type == CS2D_ENT_INFO_TARGET ||
        type == CS2D_ENT_INFO_NOFOW || type == CS2D_ENT_INFO_CTF_FLAG ||
        type == CS2D_ENT_INFO_DOM_POINT) {
        radius = 1;
    }

    for (uint32_t i = 0; i < map->entity_count; i++) {
        const cs2d_entity_t *e = &map->entities[i];
        if (e->type != type) continue;

        if (radius > 0) {
            int dx = e->x - tx;
            int dy = e->y - ty;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            if (dx <= radius && dy <= radius) return true;
        } else if (type == CS2D_ENT_INFO_NOBUILDINGS || type == CS2D_ENT_INFO_TEAMGATE ||
                   type == CS2D_ENT_ENV_ROOM || type == CS2D_ENT_INFO_NOWEATHER) {
            int w = e->int_settings[0] - 1;
            int h = e->int_settings[1] - 1;
            if (w < 0) w = 0;
            if (h < 0) h = 0;
            if (tx >= e->x && ty >= e->y && tx <= e->x + w && ty <= e->y + h) return true;
        } else if (type == CS2D_ENT_ENV_HURT) {
            int w = e->int_settings[2] - 1;
            int h = e->int_settings[3] - 1;
            if (w < 0) w = 0;
            if (h < 0) h = 0;
            if (tx >= e->x && ty >= e->y && tx <= e->x + w && ty <= e->y + h) return true;
        } else {
            if (tx == e->x && ty == e->y) return true;
        }
    }
    return false;
}

const char *cs2d_entity_type_name(cs2d_entity_type_t type)
{
    switch (type) {
        case CS2D_ENT_INFO_T:            return "Info_T";
        case CS2D_ENT_INFO_CT:           return "Info_CT";
        case CS2D_ENT_INFO_VIP:          return "Info_VIP";
        case CS2D_ENT_INFO_HOSTAGE:      return "Info_Hostage";
        case CS2D_ENT_INFO_RESCUEPOINT:  return "Info_RescuePoint";
        case CS2D_ENT_INFO_BOMBSPOT:     return "Info_BombSpot";
        case CS2D_ENT_INFO_ESCAPEPOINT:  return "Info_EscapePoint";
        case CS2D_ENT_INFO_TARGET:       return "Info_Target";
        case CS2D_ENT_INFO_ANIMATION:    return "Info_Animation";
        case CS2D_ENT_INFO_STORM:        return "Info_Storm";
        case CS2D_ENT_INFO_TILEFX:       return "Info_TileFX";
        case CS2D_ENT_INFO_NOBUYING:     return "Info_NoBuying";
        case CS2D_ENT_INFO_NOWEAPONS:    return "Info_NoWeapons";
        case CS2D_ENT_INFO_NOFOW:        return "Info_NoFOW";
        case CS2D_ENT_INFO_QUAKE:        return "Info_Quake";
        case CS2D_ENT_INFO_CTF_FLAG:     return "Info_CTF_Flag";
        case CS2D_ENT_INFO_OLDRENDER:    return "Info_OldRender";
        case CS2D_ENT_INFO_DOM_POINT:    return "Info_Dom_Point";
        case CS2D_ENT_INFO_NOBUILDINGS:  return "Info_NoBuildings";
        case CS2D_ENT_INFO_BOTNODE:      return "Info_BotNode";
        case CS2D_ENT_INFO_TEAMGATE:     return "Info_TeamGate";
        case CS2D_ENT_ENV_ITEM:          return "Env_Item";
        case CS2D_ENT_ENV_SPRITE:        return "Env_Sprite";
        case CS2D_ENT_ENV_SOUND:         return "Env_Sound";
        case CS2D_ENT_ENV_DECAL:         return "Env_Decal";
        case CS2D_ENT_ENV_BREAKABLE:     return "Env_Breakable";
        case CS2D_ENT_ENV_EXPLODE:       return "Env_Explode";
        case CS2D_ENT_ENV_HURT:          return "Env_Hurt";
        case CS2D_ENT_ENV_IMAGE:         return "Env_Image";
        case CS2D_ENT_ENV_OBJECT:        return "Env_Object";
        case CS2D_ENT_ENV_BUILDING:      return "Env_Building";
        case CS2D_ENT_ENV_NPC:           return "Env_NPC";
        case CS2D_ENT_ENV_ROOM:          return "Env_Room";
        case CS2D_ENT_ENV_LIGHT:         return "Env_Light";
        case CS2D_ENT_ENV_LIGHTSTRIPE:   return "Env_LightStripe";
        case CS2D_ENT_ENV_CUBE3D:        return "Env_Cube3D";
        case CS2D_ENT_GEN_PARTICLES:     return "Gen_Particles";
        case CS2D_ENT_GEN_SPRITES:       return "Gen_Sprites";
        case CS2D_ENT_GEN_WEATHER:       return "Gen_Weather";
        case CS2D_ENT_GEN_FX:            return "Gen_FX";
        case CS2D_ENT_FUNC_TELEPORT:     return "Func_Teleport";
        case CS2D_ENT_FUNC_DYNWALL:      return "Func_DynWall";
        case CS2D_ENT_FUNC_MESSAGE:      return "Func_Message";
        case CS2D_ENT_FUNC_GAMEACTION:   return "Func_GameAction";
        case CS2D_ENT_INFO_NOWEATHER:    return "Info_NoWeather";
        case CS2D_ENT_INFO_RADARICON:    return "Info_RadarIcon";
        case CS2D_ENT_TRIGGER_START:     return "Trigger_Start";
        case CS2D_ENT_TRIGGER_MOVE:      return "Trigger_Move";
        case CS2D_ENT_TRIGGER_HIT:       return "Trigger_Hit";
        case CS2D_ENT_TRIGGER_USE:       return "Trigger_Use";
        case CS2D_ENT_TRIGGER_DELAY:     return "Trigger_Delay";
        case CS2D_ENT_TRIGGER_ONCE:      return "Trigger_Once";
        case CS2D_ENT_TRIGGER_IF:        return "Trigger_If";
        default:                         return "Unknown_Entity";
    }
}
