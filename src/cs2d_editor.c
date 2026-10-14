#include "cs2d_editor.h"
#include "cs2d_map.h"
#include "cs2d_weapons.h"
#include "cs2d_dynobj.h"

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

/* --- Entity Properties (FUN_1402aa220) --- */

#define LBL(t, x, y)            { CS2D_EDF_LABEL, 0, x, y, 0, 0, t }
#define INT(x, y, w, s, f)      { CS2D_EDF_INT, f, x, y, w, s, NULL }
#define STR(x, y, w, s, f)      { CS2D_EDF_STR, f, x, y, w, s, NULL }
#define STRI(x, y, w, s)        { CS2D_EDF_STR_INT, 0, x, y, w, s, NULL }
#define RAD(x, y, s, f, items)  { CS2D_EDF_RADIO, f, x, y, 0, s, items }
#define CHK(x, y, s, f, t)      { CS2D_EDF_CHECK, f, x, y, 0, s, t }
#define CHKS(x, y, s, f, t)     { CS2D_EDF_CHECK_STR, f, x, y, 0, s, t }
#define CMB(x, y, w, s, f, its) { CS2D_EDF_COMBO, f, x, y, w, s, its }
#define COL(x, y, w, s)         { CS2D_EDF_COLOR, 0, x, y, w, s, NULL }

/* Label column 216 (0xd8), widgets at 361 (0x169) 100 wide; rows 25 px apart from 32 */
#define ROW(n) (32 + 25 * (n))
#define XY_TILES(fl) LBL("X Tiles / Area Width:", 216, ROW(0)), INT(361, ROW(0), 100, 0, fl), \
                     LBL("Y Tiles / Area Height:", 216, ROW(1)), INT(361, ROW(1), 100, 1, fl)
#define TRIGGERED_BY "Everyone|Ts only|CTs only"

static const cs2d_edf_t f_hostage[] = {
    LBL("Look:", 216, ROW(0)),
    RAD(291, ROW(0), 1, 0, "White Scientist|Guy, Orange Suit|Black Scientist|Native|Black Guy, Orange Suit|"
                           "Brown Hair Scientist|Gray Hair Scientist|Bald Scientist")
};
static const cs2d_edf_t f_bombspot[] = { LBL("Explosion Range:", 216, ROW(0)), INT(361, ROW(0), 100, 0, CS2D_EDF_MIN0) };
static const cs2d_edf_t f_animation[] = {
    LBL("Start Tile/Frame #:", 216, ROW(0)), INT(361, ROW(0), 100, 0, 0),
    LBL("End Tile/Frame #:", 216, ROW(1)), INT(361, ROW(1), 100, 1, 0),
    LBL("Anim. Speed (ms):", 216, ROW(2)), INT(361, ROW(2), 100, 2, CS2D_EDF_MIN0)
};
static const cs2d_edf_t f_storm[] = { LBL("Storm Speed:", 216, ROW(0)), INT(361, ROW(0), 100, 1, 0) };
static const cs2d_edf_t f_tilefx[] = {
    LBL("Tile/Frame #:", 216, ROW(0)), INT(361, ROW(0), 100, 0, 0),
    LBL("Effect:", 216, ROW(1)),
    RAD(361, ROW(1), 1, 0, "Scrolling|Distortion 1|Distortion 2|Distortion 3|Waves 1|Waves 2|Waves 3"),
    LBL("Scroll X:", 216, 282), INT(361, 282, 100, 2, 0),
    LBL("Scroll Y:", 216, 307), INT(361, 307, 100, 3, 0)
};
static const cs2d_edf_t f_noweapons[] = { LBL("Remove:", 16, 227), CHK(161, 227, 0, CS2D_EDF_INV, "Remove old weapons") };
static const cs2d_edf_t f_quake[] = { LBL("Quake Power:", 216, ROW(0)), INT(361, ROW(0), 100, 0, 0) };
static const cs2d_edf_t f_ctf_flag[] = { LBL("Flag Type:", 216, ROW(0)), RAD(291, ROW(0), 0, 0, "Red Flag (T)|Blue Flag (CT)") };
static const cs2d_edf_t f_dom_point[] = {
    LBL("Ini. State:", 216, ROW(0)), RAD(291, ROW(0), 0, 0, "Not dominated|Dominated by Ts|Dominated by CTs"),
    LBL("Min Pl.:", 216, 112), RAD(291, 112, 1, 0, "1 Player|2 Players|3 Players")
};
static const cs2d_edf_t f_nobuildings[] = {
    XY_TILES(CS2D_EDF_MIN1), LBL("Mines/Portals:", 216, 92), CHK(361, 92, 2, 0, "Allow")
};
static const cs2d_edf_t f_teamgate[] = {
    XY_TILES(CS2D_EDF_MIN1),
    LBL("Let Pass:", 216, 90), RAD(291, 92, 2, 0, "Nobody|Terrorists|Counter-Terrorists"),
    LBL("Don't Kill:", 216, 160),
    CHK(291, 160, 3, 0, "Flag Carrier"), CHK(291, 180, 4, 0, "Bomb Carrier"), CHK(291, 200, 5, 0, "Defuse Kit Carrier"),
    CHK(291, 220, 6, 0, "VIP"), CHK(291, 240, 7, 0, "Gas Mask Wearer"), CHK(291, 260, 8, 0, "Night Vision Wearer")
};
static const cs2d_edf_t f_item[] = {
    LBL("Item ID:", 216, ROW(0)), { CS2D_EDF_COMBO_ITEM, 0, 281, ROW(0), 180, 0, NULL },
    LBL("Spawn:", 216, 77), RAD(281, 77, 1, 0, "One Time (1)|Instant/Infinite|Every X Sec.|On Trigger only"),
    LBL("X (Sec.):", 216, 182), INT(361, 182, 100, 2, CS2D_EDF_MIN0)
};
static const cs2d_edf_t f_sprite[] = {
    LBL("Image File:", 216, ROW(0)), STR(361, ROW(0), 100, 0, 0),
    LBL("X Size:", 216, ROW(1)), INT(361, ROW(1), 100, 0, 0),
    LBL("Y Size:", 216, ROW(2)), INT(361, ROW(2), 100, 1, 0),
    LBL("X Offset:", 216, ROW(3)), INT(361, ROW(3), 100, 2, 0),
    LBL("Y Offset:", 216, ROW(4)), INT(361, ROW(4), 100, 3, 0),
    LBL("Transp. (0.0-1.0):", 216, ROW(5)), STR(361, ROW(5), 100, 1, 0),
    LBL("Color:", 216, ROW(6)), COL(361, ROW(6), 100, 5),
    LBL("Blend Mode:", 216, ROW(7)), RAD(361, ROW(7), 9, CS2D_EDF_OFF1, "Solid|Masked|Light|Shade|Auto|Grayscale"),
    LBL("Rot. Speed:", 216, 332), STR(361, 332, 100, 3, 0),
    LBL("(FX Mode:)", 216, 357), INT(361, 357, 100, 8, 0),
    LBL("(Mask (0,2,4):)", 216, 382), STRI(361, 382, 100, 2),
    LBL("3D Height (pixels):", 216, 407), STRI(361, 407, 100, 6),
    CHKS(16, 232, 4, CS2D_EDF_INV, "Sprite covers pl. (2D only)"),
    CHKS(16, 257, 5, 0, "Hide if nightvision is on"),
    CHKS(16, 282, 7, 0, "Affect lighting"),
    CHKS(16, 307, 8, 0, "Is decoration")
};
static const cs2d_edf_t f_sound[] = {
    LBL("Sound File:", 216, ROW(0)), STR(361, ROW(0), 100, 0, 0),
    LBL("Volume (0.0-1.0):", 216, ROW(1)), STR(361, ROW(1), 100, 1, 0),
    LBL("Play:", 216, ROW(2)), CHK(296, ROW(2), 0, 0, "Only when triggered"),
    LBL("Loop:", 216, ROW(3)), CHK(296, ROW(3), 1, 0, "Do NOT loop"),
    LBL("Location:", 216, ROW(4)), CHK(296, ROW(4), 2, 0, "Play everywhere")
};
/* The decal preview (Tgui_img 0x140a0e850 at (377, 123)) is not reconstructed */
static const cs2d_edf_t f_decal[] = {
    LBL("Decal Frame:", 216, ROW(0)), INT(361, ROW(0), 100, 0, 0),
    LBL("Color:", 216, ROW(1)), COL(361, ROW(1), 100, 1),
    LBL("Transp. (0.0-1.0):", 216, ROW(2)), STR(361, ROW(2), 100, 0, 0)
};
static const cs2d_edf_t f_breakable[] = {
    LBL("Tile/Frame #:", 216, ROW(0)), INT(361, ROW(0), 100, 0, 0),
    LBL("Health:", 216, ROW(1)), INT(361, ROW(1), 100, 1, 0),
    LBL("Material:", 216, 92), RAD(361, 92, 2, 0, "Wood|Stone|Metal|Flesh|Glass|Nothing"),
    LBL("Explode:", 216, 227), CHK(361, 227, 3, 0, "Explode"),
    LBL("Explosion Range:", 216, 252), INT(361, 252, 100, 4, 0),
    LBL("Explosion Power:", 216, 277), INT(361, 277, 100, 5, 0),
    LBL("Shadow Type:", 216, 312), RAD(361, 312, 6, CS2D_EDF_OFF1, "Wall|None|Obstacle"),
    LBL("Transp. (0.0-1.0):", 216, 387), STR(361, 387, 100, 0, CS2D_EDF_PLUS1),
    CHK(361, 407, 7, 0, "Ignore FOW")
};
static const cs2d_edf_t f_explode[] = {
    LBL("Explosion Range:", 216, ROW(0)), INT(361, ROW(0), 100, 0, 0),
    LBL("Explosion Power:", 216, ROW(1)), INT(361, ROW(1), 100, 1, 0),
    LBL("Movement:", 216, ROW(2)), CHK(361, ROW(2), 2, 0, "don't explode")
};
static const cs2d_edf_t f_hurt[] = {
    LBL("Damage per Sec.:", 216, ROW(0)), INT(361, ROW(0), 100, 0, 0),
    LBL("Damage on:", 216, ROW(1)), RAD(361, ROW(1), 1, 0, "Time|Movement|Both"),
    LBL("X Tiles / Area Width:", 216, 132), INT(361, 132, 100, 2, CS2D_EDF_MIN1),
    LBL("Y Tiles / Area Height:", 216, 157), INT(361, 157, 100, 3, CS2D_EDF_MIN1)
};
static const cs2d_edf_t f_image[] = {
    LBL("Image File:", 216, ROW(0)), STR(361, ROW(0), 100, 0, 0),
    LBL("X Offset:", 216, ROW(1)), INT(361, ROW(1), 100, 0, 0),
    LBL("Y Offset:", 216, ROW(2)), INT(361, ROW(2), 100, 1, 0),
    LBL("3D Height (pixels):", 216, ROW(3)), INT(361, ROW(3), 100, 3, 0),
    CHK(216, ROW(4), 2, 0, "Image covers pl. (2D only)"),
    CHK(216, ROW(5), 4, 0, "Is decoration")
};
static const cs2d_edf_t f_object[] = {
    LBL("Object:", 216, ROW(0)),
    CMB(281, ROW(0), 180, 0, 0, "Palm|Tree|Alarm|Sink|Toilet|Street Light|Street Light (flicker)"),
    LBL("Size (0.1-2.0):", 216, ROW(1)), STR(361, ROW(1), 100, 0, CS2D_EDF_SIZE),
    LBL("Color:", 216, ROW(2)), COL(361, ROW(2), 100, 1),
    LBL("Transp. (0.0-1.0):", 216, ROW(3)), STR(361, ROW(3), 100, 1, 0)
};
static const cs2d_edf_t f_building[] = {
    LBL("Building:", 216, ROW(0)), { CS2D_EDF_COMBO_BLD, 0, 281, ROW(0), 180, 0, NULL },
    LBL("Team:", 216, 77), RAD(281, 77, 1, 0, "Neutral / Yellow|T / Red|CT / Blue")
};
static const cs2d_edf_t f_npc[] = {
    LBL("Type:", 216, ROW(0)),
    CMB(281, ROW(0), 180, 1, CS2D_EDF_OFF1, "1 - Zombie|2 - Headcrab|3 - Snark|4 - Vortigaunt|5 - Soldier"),
    LBL("Health:", 216, ROW(1)), INT(281, ROW(1), 100, 2, 0),
    LBL("Spawn:", 216, ROW(2)), CHK(281, ROW(2), 3, CS2D_EDF_INV, "Spawn on start")
};
static const cs2d_edf_t f_room[] = {
    XY_TILES(0),
    LBL("Shadow Height (Z):", 216, ROW(2)), INT(361, ROW(2), 100, 2, 0),
    LBL("Shadow Layer:", 216, ROW(3)), INT(361, ROW(3), 100, 9, 0),
    LBL("Alpha (0.0-1.0):", 216, ROW(4)), STR(361, ROW(4), 105, 2, CS2D_EDF_ONEMINUS),
    LBL("Color:", 216, ROW(5)), COL(361, ROW(5), 100, 3),
    CHK(216, 192, 6, CS2D_EDF_INV, "Sharp room edges"),
    CHK(216, 217, 7, 0, "Display room shadows"),
    CHK(216, 242, 8, CS2D_EDF_INV, "Daylight influences color"),
    LBL("Offset X (pixels):", 216, 282), STRI(361, 282, 100, 0),
    LBL("Offset Y (pixels):", 216, 307), STRI(361, 307, 100, 1)
};
static const cs2d_edf_t f_light[] = {
    LBL("Light Range:", 216, ROW(0)), INT(356, ROW(0), 105, 0, CS2D_EDF_MAX250),
    LBL("Color:", 216, ROW(1)), COL(356, ROW(1), 105, 1),
    LBL("Angle from to:", 216, ROW(2)), INT(356, ROW(2), 50, 4, CS2D_EDF_MAX359), INT(411, ROW(2), 50, 5, CS2D_EDF_MAX359),
    LBL("Rotation Speed:", 216, ROW(3)), INT(356, ROW(3), 105, 6, 0),
    LBL("Additive (0.0-1.0):", 216, ROW(4)), STR(356, ROW(4), 105, 0, CS2D_EDF_PAD0),
    LBL("X Offset:", 216, ROW(5)), STRI(356, ROW(5), 105, 1),
    LBL("Y Offset:", 216, ROW(6)), STRI(356, ROW(6), 105, 2),
    LBL("Rotation Offset:", 216, ROW(7)), STRI(356, ROW(7), 105, 3)
};
static const cs2d_edf_t f_lightstripe[] = {
    XY_TILES(0),
    LBL("Direction:", 216, ROW(2)), RAD(361, ROW(2), 2, 0, "top|right|bottom|left"),
    LBL("Color:", 216, 162), COL(361, 162, 100, 3),
    LBL("Additive (0.0-1.0):", 216, 187), STR(361, 187, 100, 0, CS2D_EDF_PAD0),
    CHK(216, 212, 6, CS2D_EDF_INV, "Daylight influences color"),
    LBL("X Offset:", 216, 237), INT(361, 237, 100, 7, 0),
    LBL("Y Offset:", 216, 262), INT(361, 262, 100, 8, 0),
    LBL("Shadow Height (Z):", 216, 287), INT(361, 287, 100, 9, 0)
};
static const cs2d_edf_t f_cube3d[] = {
    LBL("Tile/Frame #:", 216, ROW(0)), INT(361, ROW(0), 100, 0, 0),
    LBL("X Offset (Pixels):", 216, ROW(1)), INT(361, ROW(1), 100, 1, 0),
    LBL("Y Offset (Pixels):", 216, ROW(2)), INT(361, ROW(2), 100, 2, 0),
    LBL("Z Offset (Pixels):", 216, ROW(3)), INT(361, ROW(3), 100, 3, 0)
};
static const cs2d_edf_t f_particles[] = {
    LBL("Lifetime Min.:", 216, ROW(0)), INT(361, ROW(0), 100, 0, 0),
    LBL("Lifetime Max.:", 216, ROW(1)), INT(361, ROW(1), 100, 1, 0),
    LBL("X Speed Min.:", 216, ROW(2)), INT(361, ROW(2), 100, 2, 0),
    LBL("X Speed Max.:", 216, ROW(3)), INT(361, ROW(3), 100, 3, 0),
    LBL("Y Speed Min.:", 216, ROW(4)), INT(361, ROW(4), 100, 4, 0),
    LBL("Y Speed Max.:", 216, ROW(5)), INT(361, ROW(5), 100, 5, 0),
    LBL("Color:", 216, ROW(6)), { CS2D_EDF_COLOR_STR, 0, 361, ROW(6), 100, 0, NULL }
};
static const cs2d_edf_t f_sprites[] = {
    LBL("Sprite Frame:", 216, ROW(0)), INT(361, ROW(0), 100, 0, 0),
    LBL("X Speed Min.:", 216, ROW(1)), INT(361, ROW(1), 100, 4, 0),
    LBL("X Speed Max.:", 216, ROW(2)), INT(361, ROW(2), 100, 5, 0),
    LBL("Y Speed Min.:", 216, ROW(3)), INT(361, ROW(3), 100, 6, 0),
    LBL("Y Speed Max.:", 216, ROW(4)), INT(361, ROW(4), 100, 7, 0),
    LBL("Color:", 216, ROW(5)), COL(361, ROW(5), 100, 1),
    LBL("Sprite Type:", 216, ROW(6)), INT(361, ROW(6), 100, 8, 0),
    LBL("Transp. (0.0-1.0):", 216, ROW(7)), STR(361, ROW(7), 100, 0, 0)
};
static const cs2d_edf_t f_weather[] = {
    LBL("Weather Type:", 216, ROW(0)), RAD(361, ROW(0), 0, 0, "Rain|Snow|Fog|Dust|Smog|Thunder"),
    LBL("Particles (1-30):", 216, 232), INT(361, 232, 100, 1, 0)
};
static const cs2d_edf_t f_fx[] = {
    LBL("Effect Type:", 216, ROW(0)), RAD(361, ROW(0), 0, 0, "Waves|Smoke|Fire|Steam"),
    LBL("X Range Min.:", 216, 232), INT(361, 232, 100, 1, 0),
    LBL("X Range Max.:", 216, 257), INT(361, 257, 100, 2, 0),
    LBL("Y Range Min.:", 216, 282), INT(361, 282, 100, 3, 0),
    LBL("Y Range Max.:", 216, 307), INT(361, 307, 100, 4, 0),
    LBL("Spawn Rate (ms):", 216, 332), INT(361, 332, 100, 5, 0)
};
static const cs2d_edf_t f_teleport[] = {
    LBL("Target X Tile:", 216, ROW(0)), INT(361, ROW(0), 100, 0, 0),
    LBL("Target Y Tile:", 216, ROW(1)), INT(361, ROW(1), 100, 1, 0)
};
static const cs2d_edf_t f_dynwall[] = {
    LBL("Tile/Frame #:", 216, ROW(0)), INT(361, ROW(0), 100, 0, 0),
    LBL("Behavior:", 216, 72),
    RAD(316, 75, 1, 0, "Wall|Obstacle|Wall w/o Shdw.|Obstacle w/o Shdw.|Floor+Tile Behavior"),
    LBL("Transp. (0.0-1.0):", 216, 187), STR(361, 187, 100, 0, CS2D_EDF_PLUS1),
    CHK(216, 227, 2, 0, "Close only if not blocked")
};
static const cs2d_edf_t f_message[] = {
    LBL("Msg:", 216, ROW(0)), STR(281, ROW(0), 180, 0, 0),
    LBL("Display X Sec.:", 216, ROW(1)), INT(361, ROW(1), 100, 0, 0),
    LBL("Position:", 216, ROW(2)), RAD(361, ROW(2), 1, 0, "Center|Left (Chat)"),
    LBL("Color:", 216, 157), COL(361, 157, 100, 2)
};
static const cs2d_edf_t f_gameaction[] = {
    LBL("Action:", 216, ROW(0)),
    CMB(281, ROW(0), 180, 0, 0, "0 - Round Draw|1 - T Win|2 - CT Win|3 - Round Restart in X Sec|4 - Kill all Ts|"
                                "5 - Kill all CTs|6 - Kill everyone|7 - Load Map X"),
    LBL("X:", 216, ROW(1)), STR(281, ROW(1), 180, 0, 0)
};
static const cs2d_edf_t f_noweather[] = { XY_TILES(CS2D_EDF_MIN1) };
/* Icon names: pointer table 0x1409cd280 */
static const cs2d_edf_t f_radaricon[] = {
    LBL("Radar Icon:", 216, ROW(0)),
    CMB(341, ROW(0), 120, 0, 0, "Dot|Bomb|Flag|Person|X|$|!|?|A|B|C|D|E|F|G|H|up|down|left|right|"
                                "up-left|down-right|up-right|down-left"),
    LBL("Color:", 216, ROW(1)), COL(341, ROW(1), 120, 1)
};
static const cs2d_edf_t f_trigger_move[] = { LBL("Triggered by:", 216, ROW(0)), RAD(316, 35, 0, 0, TRIGGERED_BY) };
/* Button names: pointer table 0x1409cd1e0 */
static const cs2d_edf_t f_trigger_use[] = {
    LBL("Button:", 216, ROW(0)),
    CMB(281, ROW(0), 180, 0, 0, "None/Invisible|Gray+Broad|Gray+Small|Black|Knob|Red+Small|Pipe+Valve|Glass Covered|"
                                "Valve|Lever A|Lever B|Lighted Red|Lighted Green|Lighted Blue|Lighted Yellow|"
                                "Lighted White|Red Lighted Alarm|Green Lighted Alarm"),
    LBL("Alignment:", 216, 72), RAD(316, 75, 1, 0, "Top|Bottom|Left|Right"),
    LBL("Triggered by:", 216, 172), RAD(316, 175, 2, 0, TRIGGERED_BY),
    LBL("Re-Use Delay (ms):", 216, 242), INT(361, 242, 100, 3, CS2D_EDF_ABS)
};
static const cs2d_edf_t f_trigger_delay[] = {
    LBL("Delay (sec., float):", 216, ROW(0)), { CS2D_EDF_DELAY, 0, 361, ROW(0), 100, 0, NULL }
};
static const cs2d_edf_t f_trigger_if[] = {
    CMB(16, 115, 450, 0, 0, "If Lua condition is true|"
                            "If at least A player(s) in area B,C,D,E (x,y,width,height in tiles)|"
                            "If max A player(s) in area B,C,D,E (x,y,width,height in tiles)|"
                            "If entity state at A,B (pos. in tiles) is 0 (visible/enabled)|"
                            "If entity state at A,B (pos. in tiles) is 1 (hidden/disabled)|"
                            "If at least A terrorists are alive|If at least A counter-terrorists are alive|"
                            "If max A terrorists are alive|If max A counter-terrorists are alive|"
                            "If remaining round time in seconds is greater/equal A|"
                            "If remaining round time in seconds is less/equal A|"
                            "If at least A item(s) are at B,C (pos. in tiles)|If max A item(s) are at B,C (pos. in tiles)|"
                            "If building is at A,B (pos. in tiles)|If no building is at A,B (pos. in tiles)"),
    LBL("Condition (boolean Lua expression without if):", 16, 150),
    LBL("Sample 1: entity(2,5,'state')==false", 16, 170),
    LBL("Sample 2: player(1,'health')>50", 16, 190),
    STR(16, 210, 450, 0, 0),
    LBL("A:", 16, 250), INT(46, 250, 420, 1, 0),
    LBL("B:", 16, 270), INT(46, 270, 420, 2, 0),
    LBL("C:", 16, 290), INT(46, 290, 420, 3, 0),
    LBL("D:", 16, 310), INT(46, 310, 420, 4, 0),
    LBL("E:", 16, 330), INT(46, 330, 420, 5, 0)
};

#define FIELDS(a) *count = (int)(sizeof(a) / sizeof(a[0])); return a

const cs2d_edf_t *cs2d_ed_prop_fields(int type, int *count)
{
    switch (type) {
        case CS2D_ENT_INFO_HOSTAGE:     FIELDS(f_hostage);
        case CS2D_ENT_INFO_BOMBSPOT:    FIELDS(f_bombspot);
        case CS2D_ENT_INFO_ANIMATION:   FIELDS(f_animation);
        case CS2D_ENT_INFO_STORM:       FIELDS(f_storm);
        case CS2D_ENT_INFO_TILEFX:      FIELDS(f_tilefx);
        case CS2D_ENT_INFO_NOWEAPONS:   FIELDS(f_noweapons);
        case CS2D_ENT_INFO_QUAKE:       FIELDS(f_quake);
        case CS2D_ENT_INFO_CTF_FLAG:    FIELDS(f_ctf_flag);
        case CS2D_ENT_INFO_DOM_POINT:   FIELDS(f_dom_point);
        case CS2D_ENT_INFO_NOBUILDINGS: FIELDS(f_nobuildings);
        case CS2D_ENT_INFO_TEAMGATE:    FIELDS(f_teamgate);
        case CS2D_ENT_ENV_ITEM:         FIELDS(f_item);
        case CS2D_ENT_ENV_SPRITE:       FIELDS(f_sprite);
        case CS2D_ENT_ENV_SOUND:        FIELDS(f_sound);
        case CS2D_ENT_ENV_DECAL:        FIELDS(f_decal);
        case CS2D_ENT_ENV_BREAKABLE:    FIELDS(f_breakable);
        case CS2D_ENT_ENV_EXPLODE:      FIELDS(f_explode);
        case CS2D_ENT_ENV_HURT:         FIELDS(f_hurt);
        case CS2D_ENT_ENV_IMAGE:        FIELDS(f_image);
        case CS2D_ENT_ENV_OBJECT:       FIELDS(f_object);
        case CS2D_ENT_ENV_BUILDING:     FIELDS(f_building);
        case CS2D_ENT_ENV_NPC:          FIELDS(f_npc);
        case CS2D_ENT_ENV_ROOM:         FIELDS(f_room);
        case CS2D_ENT_ENV_LIGHT:        FIELDS(f_light);
        case CS2D_ENT_ENV_LIGHTSTRIPE:  FIELDS(f_lightstripe);
        case CS2D_ENT_ENV_CUBE3D:       FIELDS(f_cube3d);
        case CS2D_ENT_GEN_PARTICLES:    FIELDS(f_particles);
        case CS2D_ENT_GEN_SPRITES:      FIELDS(f_sprites);
        case CS2D_ENT_GEN_WEATHER:      FIELDS(f_weather);
        case CS2D_ENT_GEN_FX:           FIELDS(f_fx);
        case CS2D_ENT_FUNC_TELEPORT:    FIELDS(f_teleport);
        case CS2D_ENT_FUNC_DYNWALL:     FIELDS(f_dynwall);
        case CS2D_ENT_FUNC_MESSAGE:     FIELDS(f_message);
        case CS2D_ENT_FUNC_GAMEACTION:  FIELDS(f_gameaction);
        case CS2D_ENT_INFO_NOWEATHER:   FIELDS(f_noweather);
        case CS2D_ENT_INFO_RADARICON:   FIELDS(f_radaricon);
        case CS2D_ENT_TRIGGER_MOVE:
        case CS2D_ENT_TRIGGER_HIT:      FIELDS(f_trigger_move);
        case CS2D_ENT_TRIGGER_USE:      FIELDS(f_trigger_use);
        case CS2D_ENT_TRIGGER_DELAY:    FIELDS(f_trigger_delay);
        case CS2D_ENT_TRIGGER_IF:       FIELDS(f_trigger_if);
        default: *count = 0; return NULL;
    }
}

/* Types that show the "Rotation" label (0x140a0ea88) and dial (0x140a0e8a0), and the int they edit */
int cs2d_ed_prop_rot_slot(int type)
{
    switch (type) {
        case CS2D_ENT_INFO_T: case CS2D_ENT_INFO_CT: case CS2D_ENT_INFO_VIP:
        case CS2D_ENT_INFO_HOSTAGE: case CS2D_ENT_INFO_STORM: case CS2D_ENT_ENV_NPC:
            return 0;
        case CS2D_ENT_ENV_BUILDING:
            return 2;
        case CS2D_ENT_ENV_SPRITE: case CS2D_ENT_ENV_DECAL: case CS2D_ENT_ENV_OBJECT: case CS2D_ENT_ENV_CUBE3D:
            return 4;
        default:
            return -1;
    }
}

/* Env_Item: item name table 0x1409fff98 minus ids 50, 55, 63, 70, 71 (mask 0xffcfdfde from 50); Env_Building:
 * types of category 1 or 2 (0x1409f7c40) */
int cs2d_ed_prop_combo_ids(int kind, int *ids, char names[][48], int max)
{
    int n = 0;
    for (int id = 1; id < 256 && n < max; id++) {
        const char *name = NULL;
        if (kind == CS2D_EDF_COMBO_ITEM) {
            if (id == 50 || id == 55 || id == 63 || id == 70 || id == 71) continue;
            const cs2d_weapon_def_t *w = cs2d_weapon_get((uint8_t)id);
            if (w && w->name[0]) name = w->name;
        } else {
            const cs2d_dynobj_def_t *d = cs2d_dynobj_def(id);
            if (d && (d->category == 1 || d->category == 2)) name = d->name;
        }
        if (!name) continue;
        ids[n] = id;
        snprintf(names[n], 48, "%d - %s", id, name);
        n++;
    }
    return n;
}

/* FUN_1402aa0d0: adds dir * 1.0 to a decimal string, keeping its digits after the point (one when it has none) */
static void decimal_add_one(const char *in, int dir, char *out, size_t out_size)
{
    const char *dot = strchr(in, '.');
    int decimals = dot ? (int)strlen(dot + 1) : 1;
    if (decimals > 9) decimals = 9;
    snprintf(out, out_size, "%.*f", decimals, atof(in) + (double)dir);
}

/* Shortest float text with a point ("0.5", "1.0") */
static void float_text(double v, char *out, size_t out_size)
{
    snprintf(out, out_size, "%g", v);
    if (!strchr(out, '.') && !strchr(out, 'e')) strncat(out, ".0", out_size - strlen(out) - 1);
}

static int field_index(const cs2d_edf_t *f, int value)
{
    if (f->flags & CS2D_EDF_OFF1) value -= 1;
    return (f->flags & CS2D_EDF_INV) ? 1 - value : value;
}

void cs2d_ed_prop_load(cs2d_ed_prop_t *p, const cs2d_entity_t *e)
{
    memset(p, 0, sizeof(*p));
    snprintf(p->name, sizeof(p->name), "%s", e->name);
    snprintf(p->trigger, sizeof(p->trigger), "%s", e->trigger);
    int rs = cs2d_ed_prop_rot_slot((int)e->type);
    if (rs >= 0) p->rot = e->int_settings[rs];

    int count;
    const cs2d_edf_t *fields = cs2d_ed_prop_fields((int)e->type, &count);
    for (int i = 0; i < count && i < CS2D_ED_PROP_FIELDS; i++) {
        const cs2d_edf_t *f = &fields[i];
        char *t = p->text[i];
        const int *ints = e->int_settings;
        const char *str = e->str_settings[f->slot];
        switch (f->kind) {
            case CS2D_EDF_INT:     snprintf(t, 64, "%d", ints[f->slot]); break;
            case CS2D_EDF_STR_INT: snprintf(t, 64, "%d", atoi(str)); break;
            case CS2D_EDF_STR:
                if (f->flags & CS2D_EDF_PLUS1)         decimal_add_one(str, 1, t, 64);
                else if (f->flags & CS2D_EDF_ONEMINUS) float_text(1.0 - atof(str), t, 64);
                else snprintf(t, 64, "%s", (str[0] || !(f->flags & CS2D_EDF_PAD0)) ? str : "0");
                break;
            case CS2D_EDF_RADIO:
            case CS2D_EDF_CHECK:
            case CS2D_EDF_COMBO:
                p->value[i] = field_index(f, ints[f->slot]);
                break;
            case CS2D_EDF_CHECK_STR:
                p->value[i] = (f->flags & CS2D_EDF_INV) ? (str[0] == '\0') : (str[0] != '\0');
                break;
            case CS2D_EDF_COMBO_ITEM:
            case CS2D_EDF_COMBO_BLD:
                p->value[i] = ints[f->slot];
                break;
            case CS2D_EDF_COLOR:
                for (int c = 0; c < 3; c++) p->rgb[c] = ints[f->slot + c];
                break;
            case CS2D_EDF_COLOR_STR: {
                int v = atoi(str);
                p->rgb[0] = (v >> 16) & 0xff;
                p->rgb[1] = (v >> 8) & 0xff;
                p->rgb[2] = v & 0xff;
                break;
            }
            case CS2D_EDF_DELAY:
                /* Milliseconds as seconds, cut 3 digits after the point (the original slices up to '.' + 4) */
                if (ints[1] > 0) {
                    snprintf(t, 64, "%.3f", (double)ints[1] * 0.001);
                    char *z = t + strlen(t) - 1;
                    while (*z == '0' && z[-1] != '.') *z-- = '\0';
                } else {
                    snprintf(t, 64, "%d", ints[0]);
                }
                break;
            default: break;
        }
    }
}

static int clamp_int(const cs2d_edf_t *f, int v)
{
    if ((f->flags & (CS2D_EDF_MIN0 | CS2D_EDF_MAX250 | CS2D_EDF_MAX359)) && v < 0) v = 0;
    if ((f->flags & CS2D_EDF_MIN1) && v < 1) v = 1;
    if ((f->flags & CS2D_EDF_MAX250) && v > 250) v = 250;
    if ((f->flags & CS2D_EDF_MAX359) && v > 359) v = 359;
    if (f->flags & CS2D_EDF_ABS) v = abs(v);
    return v;
}

void cs2d_ed_prop_apply(const cs2d_ed_prop_t *p, cs2d_entity_t *e)
{
    snprintf(e->name, sizeof(e->name), "%s", p->name);
    snprintf(e->trigger, sizeof(e->trigger), "%s", p->trigger);
    int rs = cs2d_ed_prop_rot_slot((int)e->type);
    if (rs >= 0) e->int_settings[rs] = p->rot;

    int count;
    const cs2d_edf_t *fields = cs2d_ed_prop_fields((int)e->type, &count);
    for (int i = 0; i < count && i < CS2D_ED_PROP_FIELDS; i++) {
        const cs2d_edf_t *f = &fields[i];
        const char *t = p->text[i];
        int *ints = e->int_settings;
        char *str = e->str_settings[f->slot];
        switch (f->kind) {
            case CS2D_EDF_INT:
                ints[f->slot] = clamp_int(f, atoi(t));
                if (f->flags & CS2D_EDF_MAX359) ints[f->slot + 3] = ints[f->slot]; /* Env_Light ints[7] / ints[8] */
                break;
            case CS2D_EDF_STR_INT: snprintf(str, 64, "%d", atoi(t)); break;
            case CS2D_EDF_STR:
                if (f->flags & CS2D_EDF_PLUS1) {
                    decimal_add_one(t, -1, str, 64);
                } else if (f->flags & CS2D_EDF_ONEMINUS) {
                    float_text(1.0 - atof(t), str, 64);
                } else if (f->flags & CS2D_EDF_SIZE) {
                    /* Below 0.1 becomes "0.1", above 2.0 "2.0", else the text as typed */
                    double v = atof(t);
                    snprintf(str, 64, "%s", v < 0.1 ? "0.1" : (v > 2.0 ? "2.0" : t));
                } else {
                    snprintf(str, 64, "%s", t);
                }
                break;
            case CS2D_EDF_RADIO:
            case CS2D_EDF_CHECK:
            case CS2D_EDF_COMBO: {
                int v = (f->flags & CS2D_EDF_INV) ? 1 - p->value[i] : p->value[i];
                ints[f->slot] = (f->flags & CS2D_EDF_OFF1) ? v + 1 : v;
                break;
            }
            case CS2D_EDF_CHECK_STR:
                snprintf(str, 64, "%s", ((p->value[i] != 0) != ((f->flags & CS2D_EDF_INV) != 0)) ? "1" : "");
                break;
            case CS2D_EDF_COMBO_ITEM:
            case CS2D_EDF_COMBO_BLD:
                ints[f->slot] = p->value[i];
                break;
            case CS2D_EDF_COLOR:
                for (int c = 0; c < 3; c++) ints[f->slot + c] = p->rgb[c];
                break;
            case CS2D_EDF_COLOR_STR:
                snprintf(str, 64, "%d", (p->rgb[0] * 256 + p->rgb[1]) * 256 + p->rgb[2]);
                break;
            case CS2D_EDF_DELAY:
                if (strchr(t, '.')) {
                    ints[0] = 0;
                    ints[1] = abs((int)(atof(t) * 1000.0));
                } else {
                    ints[0] = abs(atoi(t));
                    ints[1] = 0;
                }
                break;
            default: break;
        }
    }
    e->param1 = e->int_settings[0];
    e->param2 = e->int_settings[1];
}

/* --- Minimap pixmap (FUN_14019d7c0) --- */

void cs2d_ed_minimap_build(const cs2d_map_t *map, bool entities, uint32_t *out)
{
    const uint32_t black = 0xff000000u;
    for (uint32_t y = 0; y < map->height; y++) {
        for (uint32_t x = 0; x < map->width; x++) {
            size_t idx = (size_t)y * map->width + x;
            uint8_t tile = map->tiles[idx], mode = map->tile_modes[tile];
            uint32_t c;
            if (tile == 0) c = black;
            else if (mode == CS2D_TILE_WALL || mode == CS2D_TILE_WALL_NOSHADOW || mode == CS2D_TILE_WALL_FLOORLEVEL) c = 0xffffffffu;
            else if (mode == CS2D_TILE_OBSTACLE || mode == CS2D_TILE_OBSTACLE_NOSHADOW) c = 0xff7d7d7du;
            else if (mode >= CS2D_TILE_DEADLY_NORMAL && mode <= CS2D_TILE_DEADLY_ABYSS) c = 0xff0000ffu;
            else if (mode == CS2D_TILE_FLOOR_WADE) c = 0xff966400u; /* (0, 100, 150) */
            else c = 0xff323232u;
            out[idx] = c;
        }
    }
    if (!entities) return;
    for (uint32_t i = 0; i < map->entity_count; i++) {
        const cs2d_entity_t *e = &map->entities[i];
        if (e->x < 0 || e->y < 0 || (uint32_t)e->x >= map->width || (uint32_t)e->y >= map->height) continue;
        uint8_t rgb[3];
        cs2d_ed_entity_color((int)e->type, rgb);
        out[(size_t)e->y * map->width + (size_t)e->x] = black | ((uint32_t)rgb[2] << 16) | ((uint32_t)rgb[1] << 8) | rgb[0];
    }
}
