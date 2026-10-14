#include "cs2d_editor.h"
#include "cs2d_map.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- Brush (tile list selection) --- */

void cs2d_ed_brush_rotate(cs2d_ed_brush_t *brush)
{
    brush->mod = (uint8_t)((brush->mod & 0xfc) + (((brush->mod & 3) + 1) & 3));
}

void cs2d_ed_brush_brightness(cs2d_ed_brush_t *brush)
{
    int level = ((brush->mod >> 2) & 0xf) + 1;
    if (level > 9) level = 0;
    brush->mod = (uint8_t)((brush->mod & 0xc3) + level * 4);
}

int cs2d_ed_brightness_percent(uint8_t mod)
{
    int level = (mod >> 2) & 0xf;
    return level ? level * 10 : 100;
}

void cs2d_ed_brush_set_color(cs2d_ed_brush_t *brush, uint8_t r, uint8_t g, uint8_t b)
{
    brush->r = r;
    brush->g = g;
    brush->b = b;
    if (r == 0xff && g == 0xff && b == 0xff) {
        brush->mod &= 0x7f;
    } else {
        brush->mod = (uint8_t)((brush->mod & 0x3f) + 0x80);
    }
}

/* --- Painting --- */

static bool in_map(const cs2d_map_t *map, int x, int y)
{
    return map->tiles && x >= 0 && y >= 0 && (uint32_t)x < map->width && (uint32_t)y < map->height;
}

bool cs2d_ed_set_tile(cs2d_map_t *map, int x, int y, const cs2d_ed_brush_t *brush)
{
    if (!in_map(map, x, y)) return false;
    size_t idx = (size_t)y * map->width + (size_t)x;
    uint8_t mod = brush->mod;
    /* A white color is no color (FUN_1402bd6d0 drops the 0x80 flag) */
    if ((mod & 0xc0) == 0x80 && brush->r == 0xff && brush->g == 0xff && brush->b == 0xff) mod &= 0x3f;
    uint8_t *d = map->tile_data ? &map->tile_data[idx * 4] : NULL;
    bool same_data = !d || mod < 0x40 || (d[0] == brush->r && d[1] == brush->g && d[2] == brush->b);
    if (map->tiles[idx] == brush->tile && map->modifiers[idx] == mod && same_data) return false;

    map->tiles[idx]     = brush->tile;
    map->modifiers[idx] = mod;
    if (d && mod >= 0x40) {
        d[0] = brush->r;
        d[1] = brush->g;
        d[2] = brush->b;
        d[3] = 0;
    }
    if (mod) map->use_modifiers = 1;
    return true;
}

int cs2d_ed_fill(cs2d_map_t *map, int x, int y, const cs2d_ed_brush_t *brush)
{
    if (!in_map(map, x, y)) return 0;
    size_t idx = (size_t)y * map->width + (size_t)x;
    uint8_t tile = map->tiles[idx];
    uint8_t mod  = map->modifiers[idx];
    uint8_t new_mod = brush->mod;
    if ((new_mod & 0xc0) == 0x80 && brush->r == 0xff && brush->g == 0xff && brush->b == 0xff) new_mod &= 0x3f;
    if (tile == brush->tile && mod == new_mod) return 0; /* FUN_1402bdcc0: nothing would change */

    /* Ted_fill list as a stack of tile indices; a tile is painted when it is queued, so it is never queued twice */
    size_t total = (size_t)map->width * map->height;
    uint32_t *stack = malloc(total * sizeof(uint32_t));
    if (!stack) return 0;
    int painted = 0;
    size_t top = 0;
    cs2d_ed_set_tile(map, x, y, brush);
    painted++;
    stack[top++] = (uint32_t)idx;
    while (top > 0) {
        uint32_t i = stack[--top];
        int cx = (int)(i % map->width), cy = (int)(i / map->width);
        static const int dx[4] = { 0, 0, -1, 1 }, dy[4] = { -1, 1, 0, 0 };
        for (int k = 0; k < 4; k++) {
            int nx = cx + dx[k], ny = cy + dy[k];
            if (!in_map(map, nx, ny)) continue;
            size_t n = (size_t)ny * map->width + (size_t)nx;
            if (map->tiles[n] != tile || map->modifiers[n] != mod) continue;
            cs2d_ed_set_tile(map, nx, ny, brush);
            painted++;
            stack[top++] = (uint32_t)n;
        }
    }
    free(stack);
    return painted;
}

int cs2d_ed_rect(cs2d_map_t *map, int x0, int y0, int x1, int y1, const cs2d_ed_brush_t *brush)
{
    if (x1 < x0) { int t = x0; x0 = x1; x1 = t; }
    if (y1 < y0) { int t = y0; y0 = y1; y1 = t; }
    int painted = 0;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            if (cs2d_ed_set_tile(map, x, y, brush)) painted++;
        }
    }
    return painted;
}

/* --- Tile properties --- */

const char *const cs2d_ed_prop_labels[CS2D_ED_PROP_OPTIONS] = {
    "Wall", "Obstacle", "Wall - w/o Shadow", "Obstacle - w/o Shadow",
    "Floor - Soundless", "Floor - Dirt", "Floor - Snow", "Floor - Step",
    "Floor - Tile", "Floor - Water", "Floor - Metal", "Floor - Wood",
    "Deadly - Normal", "Deadly - Explosion", "Deadly - Toxic", "Deadly - Abyss"
};

/* FUN_1402b85f0: option index -> tile mode ("Deadly - Explosion" is 51 and "Toxic" 52 there) */
const uint8_t cs2d_ed_prop_modes[CS2D_ED_PROP_OPTIONS] = {
    1, 2, 3, 4, 0, 10, 11, 12, 13, 14, 15, 16, 50, 51, 52, 53
};

int cs2d_ed_prop_option(uint8_t mode)
{
    for (int i = 0; i < CS2D_ED_PROP_OPTIONS; i++) {
        if (cs2d_ed_prop_modes[i] == mode) return i;
    }
    return 4;
}

const char *cs2d_ed_tile_mode_name(uint8_t mode)
{
    switch (mode) {
        case 5:  return "Floor Wall";
        case 6:  return "Obstacle Wall";
        default: break;
    }
    for (int i = 0; i < CS2D_ED_PROP_OPTIONS; i++) {
        if (cs2d_ed_prop_modes[i] == mode) return cs2d_ed_prop_labels[i];
    }
    return "Unknown Behavior";
}

uint8_t cs2d_ed_tile_max(int image_w, int image_h)
{
    int n = (image_w / CS2D_TILE_SIZE) * (image_h / CS2D_TILE_SIZE) - 1;
    if (n < 0) n = 0;
    if (n > 255) n = 255;
    return (uint8_t)n;
}

void cs2d_ed_inf_path(const char *tileset, char *out, size_t out_size)
{
    char base[96];
    snprintf(base, sizeof(base), "%s", tileset ? tileset : "");
    char *dot = strrchr(base, '.');
    if (dot) *dot = '\0';
    snprintf(out, out_size, "gfx/tiles/%s.inf", base);
}

/* Default heights when a format has none: walls 32, obstacles 16 (FUN_14019c550 via g_prop_layer) */
static uint16_t default_height(uint8_t mode)
{
    if (mode == 1 || mode == 3 || mode == 5) return 32;
    if (mode == 2 || mode == 4 || mode == 6) return 16;
    return 0;
}

static bool known_mode(int mode)
{
    return strcmp(cs2d_ed_tile_mode_name((uint8_t)mode), "Unknown Behavior") != 0;
}

bool cs2d_ed_inf_load(cs2d_map_t *map, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    char header[96] = {0};
    if (!fgets(header, sizeof(header), f)) {
        fclose(f);
        return false;
    }
    header[strcspn(header, "\r\n")] = '\0';
    int version;
    if (strcmp(header, "Unreal Software's Counter-Strike 2D Tileset Informations") == 0) {
        version = 1;
    } else if (strcmp(header, "Unreal Software's Counter-Strike 2D Tileset Informations V 2.0") == 0) {
        version = 2;
    } else if (strcmp(header, "Unreal Software's Counter-Strike 2D Tileset Informations V 3.0") == 0 ||
               strcmp(header, "Unreal Software's CS2D Tileset Informations V 3.0") == 0) {
        version = 3;
    } else if (strcmp(header, "Unreal Software's CS2D Tileset Informations V 4.0") == 0) {
        version = 4;
    } else {
        fclose(f);
        return false;
    }

    for (int i = 0; ; i++) {
        int mode, height = -1, mods = 0;
        if (version == 1) {
            char line[32];
            if (!fgets(line, sizeof(line), f)) break;
            mode = atoi(line);
        } else {
            mode = fgetc(f);
            if (mode == EOF) break;
            if (version == 3) {
                int32_t h = 0;
                if (fread(&h, 4, 1, f) != 1) break;
                height = h;
            } else if (version == 4) {
                uint16_t h = 0;
                if (fread(&h, 2, 1, f) != 1) break;
                height = h;
                mods = fgetc(f);
                if (mods == EOF) break;
            }
        }
        if (i > map->tile_count || !known_mode(mode)) continue;
        map->tile_modes[i]   = (uint8_t)mode;
        map->tile_heights[i] = (uint16_t)(height >= 0 ? height : default_height((uint8_t)mode));
        map->tile_3d_mods[i] = (uint8_t)mods;
    }
    fclose(f);
    return true;
}

bool cs2d_ed_inf_save(const cs2d_map_t *map, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    fputs("Unreal Software's CS2D Tileset Informations V 4.0\r\n", f);
    for (int i = 0; i <= map->tile_count; i++) {
        fputc(map->tile_modes[i], f);
        fwrite(&map->tile_heights[i], 2, 1, f);
        fputc(map->tile_3d_mods[i], f);
    }
    fclose(f);
    return true;
}

/* --- Entities --- */

const char *const cs2d_ed_entity_filters[CS2D_ED_ENT_FILTERS] = {
    "All", "Info", "Environment", "Generate", "Function", "Trigger"
};

/* Name prefix of each filter and the type ranges that use it (Info also holds 80 and 81) */
static const char *const s_filter_prefix[CS2D_ED_ENT_FILTERS] = { "", "Info_", "Env_", "Gen_", "Func_", "Trigger_" };

static bool valid_type(int type)
{
    return (type >= 0 && type <= 6) || (type >= 8 && type <= 35) || (type >= 50 && type <= 53) ||
           (type >= 70 && type <= 73) || type == 80 || type == 81 || (type >= 90 && type <= 96);
}

int cs2d_ed_entity_list(int filter, int *out_types, int max)
{
    int n = 0;
    for (int f = 1; f < CS2D_ED_ENT_FILTERS; f++) {
        if (filter != 0 && filter != f) continue;
        for (int t = 0; t < 100 && n < max; t++) {
            if (!valid_type(t)) continue;
            const char *name = cs2d_entity_type_name((cs2d_entity_type_t)t);
            if (strncmp(name, s_filter_prefix[f], strlen(s_filter_prefix[f])) == 0) out_types[n++] = t;
        }
    }
    return n;
}

/* Short labels: the init code fills 0x1409e9450 */
const char *cs2d_ed_entity_label(int type)
{
    static const struct { int type; const char *label; } labels[] = {
        { 0, "T" }, { 1, "CT" }, { 2, "VIP" }, { 3, "H" }, { 4, "RPoint" }, { 5, "BSpot" }, { 6, "EPoint" },
        { 8, "Anim" }, { 9, "Storm" }, { 10, "TileFX" }, { 11, "NoBuying" }, { 12, "NoWpns" }, { 13, "NoFOW" },
        { 14, "Quake" }, { 15, "CTF" }, { 16, "OldRender" }, { 17, "DOM" }, { 18, "NoBuild" }, { 19, "BNode" },
        { 20, "TGate" }, { 21, "Item" }, { 22, "Spr" }, { 23, "Snd" }, { 24, "Dcl" }, { 25, "Brkbl" },
        { 26, "Expl" }, { 27, "Hurt" }, { 28, "Img" }, { 29, "Obj" }, { 30, "Build" }, { 31, "NPC" },
        { 32, "Room" }, { 33, "Light" }, { 34, "LStripe" }, { 35, "C3D" }, { 50, "Particles" },
        { 51, "Sprites" }, { 52, "Weather" }, { 53, "FX" }, { 70, "Tel" }, { 71, "DWall" }, { 72, "MSG" },
        { 73, "GAction" }, { 80, "NoWeather" }, { 81, "RadIcn" }, { 90, "Start" }, { 91, "Move" },
        { 92, "Hit" }, { 93, "Use" }, { 94, "Delay" }, { 95, "Once" }, { 96, "If" }
    };
    for (size_t i = 0; i < sizeof(labels) / sizeof(labels[0]); i++) {
        if (labels[i].type == type) return labels[i].label;
    }
    return "";
}

/* Color table 0x1409e9db0, filled by a loop in the init code (0x140318ee7) */
void cs2d_ed_entity_color(int type, uint8_t rgb[3])
{
    uint8_t r = 255, g = 255, b = 0;                                /* Default: yellow */
    if (type == 0)                       { r = 255; g = 0;   b = 0;   }
    else if (type == 1)                  { r = 0;   g = 0;   b = 255; }
    else if (type >= 21 && type <= 35)   { r = 128; g = 255; b = 0;   }
    else if (type >= 50 && type <= 53)   { r = 255; g = 200; b = 0;   }
    else if (type >= 70 && type <= 73)   { r = 255; g = 128; b = 255; }
    else if (type >= 90 && type <= 96)   { r = 100; g = 200; b = 255; }
    rgb[0] = r;
    rgb[1] = g;
    rgb[2] = b;
}

/* Zone table 0x1409f75f8 */
int cs2d_ed_entity_zone(int type)
{
    switch (type) {
        case 0: case 1: case 5: return 2;
        case 4:                 return 1;
        default:                return 0;
    }
}

int cs2d_ed_entity_at(const cs2d_map_t *map, int x, int y)
{
    for (uint32_t i = 0; i < map->entity_count; i++) {
        if (map->entities[i].x == x && map->entities[i].y == y) return (int)i;
    }
    return -1;
}

int cs2d_ed_entity_add(cs2d_map_t *map, int type, int x, int y)
{
    if (!in_map(map, x, y) || map->entity_count >= CS2D_MAX_ENTITIES || !valid_type(type)) return -1;
    cs2d_entity_t *e = &map->entities[map->entity_count];
    memset(e, 0, sizeof(*e));
    e->type = (cs2d_entity_type_t)type;
    e->x    = x;
    e->y    = y;
    map->entity_state[map->entity_count] = 0;
    map->entity_table_version++;   /* cs2d_render.c entity index */
    return (int)map->entity_count++;
}

void cs2d_ed_entity_remove(cs2d_map_t *map, int index)
{
    if (index < 0 || (uint32_t)index >= map->entity_count) return;
    memmove(&map->entities[index], &map->entities[index + 1],
            (map->entity_count - (uint32_t)index - 1) * sizeof(cs2d_entity_t));
    map->entity_count--;
    map->entity_table_version++;   /* cs2d_render.c entity index */
}

bool cs2d_ed_new_map(cs2d_map_t *map, uint32_t w, uint32_t h, const char *tileset)
{
    memset(map, 0, sizeof(*map));
    snprintf(map->header, sizeof(map->header), "Unreal Software's CS2D Map File (max)");
    snprintf(map->tileset, sizeof(map->tileset), "%s", tileset);
    map->width             = w;
    map->height            = h;
    map->save_tile_heights = 2;
    size_t total = (size_t)w * (size_t)h;
    map->tiles     = calloc(total, 1);
    map->modifiers = calloc(total, 1);
    map->collision = calloc(total, 1);
    map->tile_data = calloc(total * 4, 1);
    if (!map->tiles || !map->modifiers || !map->collision || !map->tile_data) {
        cs2d_map_free(map);
        return false;
    }
    return true;
}

/* --- Display Settings (sys/editor.cfg) --- */

const char *const cs2d_ed_disp_keys[CS2D_ED_DISP_COUNT] = {
    "editor_minimap", "editor_grid", "editor_shadows", "editor_entities", "editor_entitygraphics", "editor_gameplayview"
};
/* sys/language 0443..0447, 0898 */
const char *const cs2d_ed_disp_labels[CS2D_ED_DISP_COUNT] = {
    "Minimap", "Grid", "Shadows", "Entities", "Entity Graphics/FX", "In-game scale/view"
};

bool cs2d_ed_disp_load(bool flags[CS2D_ED_DISP_COUNT], const char *path)
{
    static const bool defaults[CS2D_ED_DISP_COUNT] = { true, true, true, false, true, true };
    memcpy(flags, defaults, sizeof(defaults));
    FILE *f = fopen(path, "r");
    if (!f) return false;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char key[64];
        int value;
        if (sscanf(line, "%63s %d", key, &value) != 2) continue;
        for (int i = 0; i < CS2D_ED_DISP_COUNT; i++) {
            if (strcmp(key, cs2d_ed_disp_keys[i]) == 0) flags[i] = value != 0;
        }
    }
    fclose(f);
    return true;
}

/* FUN_1402b7570: a comment line, then "<key> <0|1>" per flag (WriteLine: CRLF) */
bool cs2d_ed_disp_save(const bool flags[CS2D_ED_DISP_COUNT], const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    fputs("// CS2D Editor Configuration\r\n", f);
    for (int i = 0; i < CS2D_ED_DISP_COUNT; i++) fprintf(f, "%s %d\r\n",cs2d_ed_disp_keys[i], flags[i] ? 1 : 0);
    fclose(f);
    return true;
}
