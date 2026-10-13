#ifndef CS2D_RENDER_H
#define CS2D_RENDER_H

#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include "cs2d_types.h"

typedef struct SDL_Window SDL_Window;

#define CS2D_MAX_SPRITE_CACHE 64

typedef struct {
    uint32_t id;       /* OpenGL Texture ID */
    int      width;    /* Logical unpadded image width */
    int      height;   /* Logical unpadded image height */
    int      gpu_w;    /* Actual GPU texture width (with per-cell padding) */
    int      gpu_h;    /* Actual GPU texture height (with per-cell padding) */
    int      cell_w;   /* Cell width when built as a padded cell atlas (0 = single frame) */
    int      cell_h;   /* Cell height when built as a padded cell atlas (0 = single frame) */
    int      pad;      /* Per-side pixel border extruded around each cell */
} cs2d_texture_t;

typedef struct {
    char           path[128];
    int            mask_mode;
    cs2d_texture_t tex;
} cs2d_sprite_cache_entry_t;

/*
 * Complete CS2D Graphics Asset Bank (reconstructed from Tgfx descriptors
 * at 0x140309500..0x140310500 and weapon loader at 0x1401d9210 in CS2D.exe)
 */
typedef struct {
    bool                      initialized;

    /* Player & Actor Sprite Sheets (maskMode = 2, 32x32 cells) */
    cs2d_texture_t            tex_t[4];          /* gfx/player/t1..t4.bmp (64x96 -> 6 frames of 32x32) */
    cs2d_texture_t            tex_ct[4];         /* gfx/player/ct1..ct4.bmp (64x96 -> 6 frames of 32x32) */
    cs2d_texture_t            tex_vip;           /* gfx/player/vip.bmp (64x96 -> 6 frames of 32x32) */
    cs2d_texture_t            tex_zombie;        /* gfx/player/zombie.bmp (64x96 -> 6 frames of 32x32) */
    cs2d_texture_t            tex_legs;          /* gfx/player/legs.bmp (128x64 -> 8 frames of 32x32) */
    cs2d_texture_t            tex_hostages;      /* gfx/player/hostages.bmp (128x64 -> 8 frames of 32x32) */
    cs2d_texture_t            tex_equipment;     /* gfx/equipment.bmp (96x32 -> 3 frames of 32x32) */

    /* Weapon Sprites (0x1409f3ce8 held, 0x1409f3d28 dropped, 0x1409f3d20 killfeed) */
    cs2d_texture_t            tex_weapons[CS2D_MAX_WEAPONS];
    cs2d_texture_t            tex_weapons_d[CS2D_MAX_WEAPONS];
    cs2d_texture_t            tex_weapons_k[CS2D_MAX_WEAPONS];
    cs2d_texture_t            tex_reload;        /* gfx/weapons/reload.bmp */
    cs2d_texture_t            tex_silencer;      /* gfx/weapons/silencer.bmp */
    cs2d_texture_t            tex_m4a1s;         /* gfx/weapons/m4a1s.bmp */
    cs2d_texture_t            tex_redflag;       /* gfx/weapons/redflag.bmp */
    cs2d_texture_t            tex_blueflag;      /* gfx/weapons/blueflag.bmp */
    cs2d_texture_t            tex_noflag;        /* gfx/weapons/noflag.bmp */

    /* Projectile Sprites (gfx/weapons) */
    cs2d_texture_t            tex_proj_rocket;
    cs2d_texture_t            tex_proj_rpg;
    cs2d_texture_t            tex_proj_grenade;
    cs2d_texture_t            tex_proj_molotov;
    cs2d_texture_t            tex_proj_flare;
    cs2d_texture_t            tex_proj_gutbomb;
    cs2d_texture_t            tex_proj_airstrike;
    cs2d_texture_t            tex_proj_flashbang;
    cs2d_texture_t            tex_proj_smokegrenade;
    cs2d_texture_t            tex_proj_gasgrenade;
    cs2d_texture_t            tex_proj_snowball;

    /* Map & Entity Sprites */
    cs2d_texture_t            tex_shadowmap;     /* gfx/shadowmap.bmp (64x288 -> 18 frames of 32x32, maskMode=3) */
    cs2d_texture_t            tex_decals;        /* gfx/decals.bmp (576x128 -> 72 frames of 32x32, maskMode=3) */
    cs2d_texture_t            tex_buildings;     /* gfx/buildings.png (128x128 -> 16 frames of 32x32) */
    cs2d_texture_t            tex_turret[3];     /* gfx/weapons/turret.bmp, turret2.bmp, turret3.bmp (0x140a0ddb0 / da8 / da0) */
    cs2d_texture_t            tex_buildhelper;   /* gfx/hud_buildhelper.png (82x82, maskMode 1, 0x140a0dd38) */
    cs2d_texture_t            tex_palms;         /* incbindata/palmleave.png (30x70, handle (15, 2), g_img_palmleave) */
    cs2d_texture_t            tex_tree;          /* incbindata/treeleaves.png (40x40, midhandle, g_img_treeleaves) */
    cs2d_texture_t            tex_leaf;          /* gfx/sprites/leaf.bmp */
    cs2d_texture_t            tex_shadow;        /* incbindata/shadow.bmp (32x32, maskMode=3, 0x140a0e038) */
    cs2d_texture_t            tex_objects;       /* incbindata/objects.bmp (10 frames of 32x32, midhandle, g_img_objects) */
    cs2d_texture_t            tex_buttons;       /* incbindata/buttons.bmp (10 frames of 32x32, midhandle, mask 255,0,255; g_img_buttons) */
    cs2d_texture_t            tex_lamp2;         /* gfx/sprites/lamp2.bmp */
    cs2d_texture_t            tex_flare1;        /* gfx/sprites/flare1.bmp (maskMode=3) */
    cs2d_texture_t            tex_flare3;        /* gfx/sprites/flare3.bmp (maskMode=3) */
    cs2d_texture_t            tex_fragments;     /* gfx/fragments.bmp (128x128 -> 16 frames of 32x32, maskMode=3) */
    cs2d_texture_t            tex_explosion;     /* gfx/explosion.png (320x320 -> 25 frames of 64x64, maskMode=1) */
    cs2d_texture_t            tex_flare_x;       /* incbindata/flare_x.png muzzle flash (32x32) */
    cs2d_texture_t            tex_glowdot;       /* incbindata/glowdot.bmp (3x3) bullet path / sparks */
    cs2d_texture_t            tex_shells;        /* gfx/shells.bmp|png (32x8 -> 4 frames of 8x8, maskMode=2) */
    cs2d_texture_t            tex_knifeslash;    /* gfx/knifeslash.bmp (32x32, maskMode=3, 0x140a0dcd8) */
    cs2d_texture_t            tex_bspot_a;       /* gfx/sprites/bspot_a.png */
    cs2d_texture_t            tex_bspot_b;       /* gfx/sprites/bspot_b.png */
    cs2d_texture_t            tex_npc[5];        /* gfx/npc/zombie, headcrab, snark, vortigaunt, soldier */

    /* HUD & Crosshair Assets */
    cs2d_texture_t            tex_hud_nums;       /* gfx/hud_nums.bmp (592x66 -> 12 frames of 48x66, maskMode=1) */
    cs2d_texture_t            tex_hud_symbols;    /* gfx/hud_symbols.bmp (832x64 -> 13 frames of 64x64, maskMode=1) */
    cs2d_texture_t            tex_hud_radar;      /* gfx/hud_radar.bmp (100x100, maskMode=1) */
    cs2d_texture_t            tex_hud_radar_adv;  /* gfx/hud_radar_advanced.png (128x128, maskMode=2) */
    cs2d_texture_t            tex_hud_radar_ring; /* gfx/hud_radar_ring.png (128x128, maskMode=2) */
    cs2d_texture_t            tex_hud_radar_icons;/* gfx/hud_radar_icons.png (32x32 -> 24 frames, maskMode=2) */
    cs2d_texture_t            tex_hud_slot;       /* gfx/hud_slot.bmp (150x40, maskMode 0, 0x140a0dd68) */
    cs2d_texture_t            tex_hud_slotheads;  /* gfx/hud_slotheads.bmp (90x10 -> 9 frames of 10x10, maskMode 1, 0x140a0dd60) */
    cs2d_texture_t            tex_pointer;        /* gfx/pointer.bmp (92x92 -> 4 frames of 46x46, maskMode=2) */

    /* Cached circular minimap texture for the active map */
    cs2d_texture_t            tex_minimap;
    uint32_t                  minimap_w;
    uint32_t                  minimap_h;

    /* Dynamic Cache for Env_Sprite (22) and Env_Image (28) */
    cs2d_sprite_cache_entry_t sprite_cache[CS2D_MAX_SPRITE_CACHE];
    int                       sprite_cache_count;
} cs2d_gfx_bank_t;

/* Initialize OpenGL rendering on an SDL3 window (with 4x MSAA) */
bool cs2d_render_init(SDL_Window *window, int width, int height);

/* Shutdown OpenGL rendering context */
void cs2d_render_shutdown(SDL_Window *window);

/* Resize viewport */
void cs2d_render_resize(int width, int height);

/* Frame control */
void cs2d_render_begin_frame(void);
void cs2d_render_end_frame(SDL_Window *window);

/* Dynamic object layers, in the order of the world pass FUN_14028f890 */
typedef enum {
    CS2D_DO_LAYER_GROUND   = 0, /* Tdo.draw_ground: after the floor, before items and players */
    CS2D_DO_LAYER_OBSTACLE = 1, /* Tdo.draw_obstacle: after the items (Barricade) */
    CS2D_DO_LAYER_WALL     = 2  /* Tdo.draw_wall: after the players (walls, Dispenser, turrets, supplies) */
} cs2d_dynobj_layer_t;

struct cs2d_dynobj_world_s;
/* team_game: the game mode colors buildings by team (0x140a015d0[sv_gamemode]); local_id for own buildings */
void cs2d_render_dynobjs(const struct cs2d_dynobj_world_s *w, cs2d_dynobj_layer_t layer, float cam_x, float cam_y,
                         int screen_w, int screen_h, bool team_game, int local_id);
/* FUN_14014f1f0: build helper on the player's tile (yellow) and on the target tile (green ok / red refused) */
void cs2d_render_build_helper(int ptx, int pty, int tx, int ty, bool ok, float cam_x, float cam_y);

/* Load and manage global/client CS2D graphics bank */
bool cs2d_gfx_bank_init(cs2d_gfx_bank_t *bank);
void cs2d_gfx_bank_free(cs2d_gfx_bank_t *bank);
cs2d_gfx_bank_t *cs2d_render_get_gfx_bank(void);
const cs2d_texture_t *cs2d_gfx_get_sprite(cs2d_gfx_bank_t *bank, const char *rel_path, bool black_mask);
const cs2d_texture_t *cs2d_render_get_projectile_texture(uint8_t type);

/* Texture loading (supports CS2D maskMode 0=opaque, 1=black key, 2=magenta/alpha, 3=luminance-to-alpha, plus padded cell atlases) */
bool cs2d_texture_load_grid(cs2d_texture_t *tex, const char *filepath, int mask_mode, int cell_w, int cell_h);
bool cs2d_texture_load_ex(cs2d_texture_t *tex, const char *filepath, int mask_mode);
bool cs2d_texture_load_bmp(cs2d_texture_t *tex, const char *filepath, bool color_key_black);
bool cs2d_texture_load_tileset(cs2d_texture_t *tex, const char *tileset_name);

/* Tile blending (Ttileblend, FUN_14005e2e0): the neighbour tile in direction 0..7 (up, then clockwise; 0 outside
 * the map), the per-channel mix with a blendmap value, and the red of gfx/blendmap.bmp frame 0..39 (-1 if missing) */
uint8_t  cs2d_render_tileblend_neighbour(const cs2d_map_t *map, int tx, int ty, int dir);
uint32_t cs2d_render_tileblend_mix(uint32_t a, uint32_t b, uint8_t mask);
int      cs2d_render_tileblend_mask(int frame, int x, int y);
void cs2d_texture_free(cs2d_texture_t *tex);
/* RGBA pixels of an image file (mask mode 0), (a << 24) | (b << 16) | (g << 8) | r per pixel; free() the result */
uint32_t *cs2d_image_load_rgba(const char *path, int *out_w, int *out_h);

/* 2D Primitives */
void cs2d_render_rect(float x, float y, float w, float h, float r, float g, float b, float a, bool fill);
/* One tileset frame at (x, y) scaled to size px, with its modifier (rotation, brightness), color (or NULL)
 * and a gray tint (Tgui_tilelist: 0.78 = 200/255, 1 when hovered) */
void cs2d_render_tile_frame(const cs2d_texture_t *tileset_tex, uint8_t frame, uint8_t mod, const uint8_t *rgb,
                            float x, float y, float size, float tint, float alpha);
void cs2d_render_line(float x1, float y1, float x2, float y2, float r, float g, float b, float a);
void cs2d_render_circle(float cx, float cy, float radius, float r, float g, float b, float a, bool fill);

/* Sprite rendering (with rotation around center) */
void cs2d_render_sprite(
    const cs2d_texture_t *tex,
    float x, float y,
    float w, float h,
    float angle_deg,
    float r, float g, float b, float a
);

/* Multi-frame Sprite Sheet Cell Rendering (with CS2D blend modes: 0=alpha, 3/4=light/additive, 5/6=shade) */
void cs2d_render_sprite_frame(
    const cs2d_texture_t *tex,
    int cell_w, int cell_h, int frame,
    float x, float y,
    float draw_w, float draw_h,
    float angle_deg,
    float r, float g, float b, float a,
    int blend_mode
);

/* Reconstructed from Tpl.getFrame (FUN_140002e20 in CS2D.exe): returns player torso frame 0..5 */
int cs2d_player_get_frame(const cs2d_player_t *player, bool lefthanded);

/* Top-down player rendering (legs.bmp, 32x32 torso frame from ct/t/vip.bmp, armor, equipment.bmp, weapon in hands) */
/* The player's silhouette in the light engine's day shadows (FUN_140112340); call before cs2d_light_frame */
void cs2d_render_player_light_shadow(const cs2d_player_t *player, const cs2d_texture_t *skin_tex, float cam_x, float cam_y);
void cs2d_render_player(
    const cs2d_player_t  *player,
    const cs2d_texture_t *skin_tex,
    const cs2d_texture_t *wep_tex,
    float cam_x, float cam_y
);

/* Map rendering (Pass 0 & 1: tiles, modifiers, shadows, decals, breakables, dynwalls, ground items, hostages, buildings, NPCs) */
void cs2d_render_map_ex(
    const cs2d_map_t     *map,
    const cs2d_texture_t *tileset_tex,
    float cam_x, float cam_y,
    int screen_w, int screen_h
);

void cs2d_render_map(
    const cs2d_map_t *map,
    float cam_x, float cam_y,
    int screen_w, int screen_h
);

/* Map overhead pass (Pass 2: Env_Sprite above players, Env_Object trees/palms/lamps, Env_Light coronas) */
void cs2d_render_map_overhead(
    const cs2d_map_t *map,
    float cam_x, float cam_y,
    int screen_w, int screen_h
);

/* Breakable effects (tile quarters, fragments, explosions) of cs2d_fx */
struct cs2d_fx_s;
void cs2d_render_fx(const struct cs2d_fx_s *fx, const cs2d_map_t *map, const cs2d_texture_t *tileset_tex,
                    float cam_x, float cam_y);

/* HUD number drawing (reconstructed from FUN_140113000 in CS2D.exe using gfx/hud_nums.bmp) */
void cs2d_render_hud_number(
    const cs2d_texture_t *hud_nums_tex,
    int value,
    float x, float y,
    float r, float g, float b, float a
);

/* In-game HUD (gfx/hud_symbols.bmp, gfx/hud_nums.bmp, gfx/hud_radar.bmp, weapon icon) */
void cs2d_render_hud(
    const cs2d_player_t *player,
    const cs2d_game_t   *game,
    const cs2d_texture_t *tileset_tex,
    int screen_w, int screen_h
);

/* In-game action progress bar layout (reconstructed from FUN_140295190 0x1402958f0..0x140295f70 in CS2D.exe) */
typedef struct {
    float bar_x, bar_y;
    float bar_w, bar_h;
    float border;
    float inner_x, inner_y;
    float inner_w, inner_h;
    float fill_w;
    float text_x, text_y;
    float progress;
    const char *title;
} cs2d_hud_progress_layout_t;

bool cs2d_hud_progress_layout(
    const cs2d_player_t *player,
    int screen_w, int screen_h,
    cs2d_hud_progress_layout_t *out_layout
);

/* Draws the in-game action progress bar (Reloading / Planting Bomb / Defusing Bomb) */
void cs2d_render_hud_progress_bar(
    const cs2d_player_t *player,
    int screen_w, int screen_h
);

/* Weapon selection HUD (invnext / invprev / slotN) at the top of the screen, from x0 (FUN_140295190) */
struct cs2d_invsel_s;
void cs2d_render_weapon_select(const cs2d_player_t *player, const struct cs2d_invsel_s *sel, float x0);

/* Crosshair at mouse pointer (gfx/pointer.bmp cell 0) */
void cs2d_render_crosshair(float mx, float my);

/* UI Font Identifiers (gfx/fonts/liberationsans.ttf & gfx/fonts/sourcesans.ttf) */
typedef enum {
    CS2D_FONT_REGULAR = 0, /* Liberation Sans 13px (Console, dialogs, inputs, buttons) */
    CS2D_FONT_SMALL   = 1, /* Liberation Sans 10px (Top-left FPS / GL / Network telemetry) */
    CS2D_FONT_BOLD    = 2, /* Liberation Sans 13px Bold (Active tabs, section headers) */
    CS2D_FONT_TITLE   = 3, /* Source Sans Pro Black 18px (Main menu CS2D title) */
    CS2D_FONT_MENU    = 4, /* Liberation Sans 15px (Main menu entries: New Game, Find Servers, ...) */
    CS2D_FONT_COUNT   = 5
} cs2d_font_id_t;

/* High-Quality TrueType UI Font Rendering & Measurement */
void  cs2d_render_text_ex(cs2d_font_id_t font_id, float x, float y, const char *text, float r, float g, float b, float a);
/* Text stretched horizontally by scale_x (BlitzMax SetScale(sx, 1) before DrawText) */
void  cs2d_render_text_scaled_ex(cs2d_font_id_t font_id, float x, float y, const char *text, float scale_x,
                                 float r, float g, float b, float a);
void  cs2d_render_text_measure(cs2d_font_id_t font_id, const char *text, float *out_w, float *out_h);
float cs2d_render_text_width_ex(cs2d_font_id_t font_id, const char *text);
/* Width of the first len bytes of text, measured without rasterizing or touching the string texture cache */
float cs2d_render_text_prefix_width(cs2d_font_id_t font_id, const char *text, int len);
float cs2d_render_font_line_height(cs2d_font_id_t font_id);

void  cs2d_render_text(float x, float y, const char *text, float r, float g, float b, float a);
void  cs2d_render_text_small(float x, float y, const char *text, float r, float g, float b, float a);
float cs2d_render_text_width(const char *text);

/* UI Primitives: Vertical Gradient Rectangle & Hardware Scissor Clipping */
void  cs2d_render_gradient_rect(
    float x, float y, float w, float h,
    float r_top, float g_top, float b_top, float a_top,
    float r_bot, float g_bot, float b_bot, float a_bot
);
void  cs2d_render_scissor_begin(int x, int y, int w, int h);
void  cs2d_render_scissor_end(void);

/* Shadow/Lighting map control for interactive console commands & regression testing */
void  cs2d_render_invalidate_shadows(void);
void  cs2d_render_set_shadows_enabled(bool enabled);
bool  cs2d_render_get_shadows_enabled(void);
void  cs2d_render_set_shadow_intensity(int intensity);
int   cs2d_render_get_shadow_intensity(void);
void  cs2d_render_set_player_shadows(bool enabled);
bool  cs2d_render_get_player_shadows(void);
void  cs2d_render_set_item_shadows(bool enabled);
bool  cs2d_render_get_item_shadows(void);

/* Vertical sync (OpenGL swap interval): sys/config.cfg "vsync" (DAT_140a12960) */
void  cs2d_render_set_vsync(bool enabled);
bool  cs2d_render_get_vsync(void);
void  cs2d_render_ensure_shadow_chunks(const cs2d_map_t *map);
void  cs2d_render_get_shadow_chunk_info(int *out_chunks_x, int *out_chunks_y, int *out_dirty_count);
void  cs2d_render_clear_shadow_dirty_test(void);

/*
 * Entity index + view culling (performance, see KNOWLEDGE.md "Performance").
 * The map passes used to sweep every map entity per pass (5 sweeps per frame); on fun_roleplay
 * (360x512 tiles, 9901 entities) that is 49k dispatches for ~20 entities actually on screen.
 * cs2d_render keeps a cached index (one list per pass, ascending entity index so the draw order is
 * unchanged) and drops entities whose draw box cannot touch the view rectangle.
 */
typedef enum {
    CS2D_EPASS_ANIM = 0,    /* Info_Animation (8): tile animation remap table */
    CS2D_EPASS_SOLID,       /* Env_Breakable (25) / Func_DynWall (71) tiles (Te.draw 0x140272808) */
    CS2D_EPASS_GROUND,      /* Te.draw(0)/(1): bomb spots, decals, ground images/objects, items, flags,
                             * hostages, NPCs, trigger buttons, ground Env_Sprite */
    CS2D_EPASS_ROOF_SPRITE, /* Te.draw(2): non-additive rooftop Env_Sprite / Env_Image */
    CS2D_EPASS_ROOF_OBJECT, /* Te.draw(2): Env_Object palms / trees / lamps and layer 3+4 sprites */
    CS2D_EPASS_COUNT
} cs2d_entity_pass_t;

/* Builds (or reuses) the index for this map; called by the map passes themselves, exposed for tests */
void cs2d_render_entity_index_prepare(const cs2d_map_t *map);
/* Entities the pass would consider at this camera (before the per-type state filters) */
int  cs2d_render_entity_pass_count(const cs2d_map_t *map, cs2d_entity_pass_t pass,
                                   float cam_x, float cam_y, int screen_w, int screen_h);
/* Total entities the pass holds (index size, camera independent) */
int  cs2d_render_entity_pass_total(const cs2d_map_t *map, cs2d_entity_pass_t pass);
/* The pass list (ascending entity indices; NULL when there is none) and its length in *out_n */
const int *cs2d_render_entity_pass_list(const cs2d_map_t *map, cs2d_entity_pass_t pass, int *out_n);
/* Culling decision for one entity at one camera: entities whose box cannot touch the view are dropped */
bool cs2d_render_entity_visible(const cs2d_map_t *map, int index, float cam_x, float cam_y,
                                int screen_w, int screen_h);
/* Half extent of the entity's draw in tiles (0 = its size is unknown here, so it is never culled) */
int  cs2d_render_entity_margin(const cs2d_map_t *map, int index);
/* The draw path reports the size it just used for an image whose size was unknown (Env_Sprite / Env_Image,
 * whose draw size is their texture): the margin can only grow, so it never cuts something already drawn */
void cs2d_render_entity_learn_size(const cs2d_map_t *map, int index, int w, int h, int offx, int offy);

#endif /* CS2D_RENDER_H */
