#ifndef CS2D_LIGHT_H
#define CS2D_LIGHT_H

/*
 * Light engine (TSmartLighting, CS2D.exe; docs/agents/KNOWLEDGE.md "Light engine").
 *
 * Every frame the engine renders a light buffer (854 x 480, "MainBuffer"): white, multiplied by the day shadows of
 * walls, obstacles and room roofs, then the rooms (Env_Room) in their color, the day / night toning, and every light
 * (Env_Light) added from its own 512 x 512 buffer with the wall shadows cut out. The buffer is multiplied over the
 * whole world after the overhead pass. Two passes go into the world itself: the floor occlusion next to walls
 * (after the items) and the additive light color (after the players).
 *
 * The drawing goes through a small Max2D layer (color, alpha, blend 3 alpha / 4 light / 5 shade, transform, rect,
 * image, poly, textured poly, buffers) so cs2d_light_set_trace can print every call in the format of
 * tools/agent/emu_light.py, which runs the original functions in an emulator.
 */

#include <stdbool.h>
#include <stdio.h>
#include "cs2d_types.h"
#include "cs2d_render.h"

typedef struct {
    int  light;          /* "light" 0..5 (DAT_140ddf185); 0 = engine off; SetQuality(light - 1) */
    int  mapshadows;     /* "mapshadows" 0..4 (DAT_140ddf190); day shadows need > 1, high walls > 3 */
    int  shadows;        /* "shadows" 0..100 (DAT_140a14f7b): day shadow brightness 255 - shadows * 2.55 */
    bool playershadows;  /* "playershadows" (DAT_140ddf184) */
} cs2d_light_config_t;

/* Settings; takes effect at once (FUN_1401733b0) */
void cs2d_light_configure(const cs2d_light_config_t *cfg);
/* sv_daylighttime: the sun angle the server imposes, -1 = the map's own (FUN_140262ed0) */
void cs2d_light_set_daylight(int angle);
/* The engine draws on this map (light != 0 and a map is loaded) */
bool cs2d_light_active(void);
/* TSmartLighting.InitMap + the entity lights of FUN_1401d6790 (Env_Room, Env_Light, Env_LightStripe) */
void cs2d_light_load_map(const cs2d_map_t *map);
/* Dynamic point lights (Tp_lightpool.GetLight): name every wanted light by a non-zero key between begin and end
 * each frame; lights not named again are switched off and pooled */
void cs2d_light_points_begin(void);
void cs2d_light_point(int key, int x, int y, float size, int r, int g, int b, float alpha, int max_size);
void cs2d_light_points_end(void);
/* Tp.CreateLight (0x14009ba30): a light particle (Tp 0x1f) of radius size at world (x, y) that shrinks by shrink per
 * 60 Hz frame and goes back to the pool at 16 (muzzle flashes, explosions). Tp.CreateLight raises every channel
 * below 35 to 35 first; Tp.Create(0x1f) does not: the caller passes the final color */
void cs2d_light_flash(int x, int y, int r, int g, int b, float size, float shrink, float alpha);
/* Tpl.update (0x14021d080): the flashlight of player pid (LightType 1, radius 162, gfx_flashlight.png): on at world
 * (x, y) turned to rot (0 = north), or off */
void cs2d_light_flashlight(int pid, bool on, float x, float y, float rot);
/* Light state for this frame: tile changes (SetTile from cs2d_update_mapdata), entity states and Env_Light rotation
 * (Te.update), sun angle (FUN_140262ed0), then TSmartLighting.Update (light buffers). cam = integer camera. */
void cs2d_light_frame(const cs2d_map_t *map, int cam_x, int cam_y, float dt);

/* World passes, in the order of FUN_140281fd0 */
void cs2d_light_draw_floor(void);   /* DrawLayer1: floor occlusion, after the items */
void cs2d_light_draw_color(void);   /* DrawLightsColorList: additive light color, after the players */
void cs2d_light_draw_buffer(void);  /* DrawLayer2: the light buffer, shade blend, over the world */

/*
 * What the scene adds to the next light buffer:
 *  - a light sprite (TLightingSprite.DrawSprite 0x140013ac0, added after the toning) or a floor light sprite
 *    (TLightingSpriteFloor, added over the day shadows), at world (x, y) in color x alpha;
 *  - an image whose area the day shadows leave lit (ObstacleShadow2.Create 0x14002e8e0), at screen (x, y).
 * (hx, hy) is the image handle. Both do nothing while the engine is off.
 */
void cs2d_light_add_sprite(bool floor, const cs2d_texture_t *tex, float hx, float hy, float x, float y, float sx, float sy,
                           int r, int g, int b, float rot, float alpha);
/* A light sprite of one cell_w x cell_h frame of a sheet (0 = the whole texture), midhandled, at world (x, y): the
 * particles of Tp.draw / TSparkle.draw (explosion frames, glow dots, flares, smoke puffs) */
void cs2d_light_add_sprite_frame(const cs2d_texture_t *tex, int cell_w, int cell_h, int frame, float x, float y,
                                 float sx, float sy, int r, int g, int b, float rot, float alpha);
/* ObstacleShadow2 of one frame of a sheet (Lua images, FUN_14025eb80), midhandled, at screen (x, y) */
void cs2d_light_add_obstacle_frame(const cs2d_texture_t *tex, int cell_w, int cell_h, int frame, float x, float y,
                                   float sx, float sy, float rot);
/*
 * A black silhouette in the day shadows at world (x, y), midhandled; submit before cs2d_light_frame. projectile false:
 * a Lua image's shadow (FUN_14025f0e0: x, y already moved by the shadow offset; transform rot, sx, sy); true: a flying
 * projectile (Tpro.draw_flying_shadow: 3 px lower right, scaled by the buffer, half again for images of 64 px or more)
 */
void cs2d_light_add_day_shadow(bool projectile, const cs2d_texture_t *tex, int cell_w, int cell_h, int frame, float x,
                               float y, float rot, float sx, float sy);
void cs2d_light_add_obstacle(const cs2d_texture_t *tex, float hx, float hy, float x, float y, float sx, float sy, float rot);
/* One part of a player's silhouette in the day shadows (FUN_140112340: armor, held weapon, bomb / kit, body; midhandled,
 * black, 4 px away from the sun), at screen (x, y) for this frame's camera; submit before cs2d_light_frame */
void cs2d_light_add_player_part(const cs2d_texture_t *tex, int cell_w, int cell_h, int frame, float x, float y,
                                float rot, float scale);
/* TSmartLighting.ShadowRotation: the sun angle the shadows use (the map's daylight time, smoothed) */
float cs2d_light_shadow_rotation(void);
/* mapshadows > 1: day shadows, floor sprites and occlusion are on */
bool cs2d_light_day_shadows(void);
/* The "mapshadows" setting (DAT_140ddf190) */
int  cs2d_light_mapshadows(void);

/* Prints every Max2D call (and skips OpenGL) while set; NULL restores drawing */
void cs2d_light_set_trace(void (*fn)(const char *line, void *ud), void *ud);
/* Scene file for tools/agent/emu_light.py (the engine state after cs2d_light_frame) */
void cs2d_light_dump_scene(FILE *f, int cam_x, int cam_y);

#endif /* CS2D_LIGHT_H */
