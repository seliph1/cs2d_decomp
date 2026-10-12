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

typedef struct {
    int  light;          /* "light" 0..5 (DAT_140ddf185); 0 = engine off; SetQuality(light - 1) */
    int  mapshadows;     /* "mapshadows" 0..4 (DAT_140ddf190); day shadows need > 1, high walls > 3 */
    int  shadows;        /* "shadows" 0..100 (DAT_140a14f7b): day shadow brightness 255 - shadows * 2.55 */
    bool playershadows;  /* "playershadows" (DAT_140ddf184) */
} cs2d_light_config_t;

/* Settings; takes effect at once (FUN_1401733b0) */
void cs2d_light_configure(const cs2d_light_config_t *cfg);
/* The engine draws on this map (light != 0 and a map is loaded) */
bool cs2d_light_active(void);
/* TSmartLighting.InitMap + the entity lights of FUN_1401d6790 (Env_Room, Env_Light, Env_LightStripe) */
void cs2d_light_load_map(const cs2d_map_t *map);
/* Light state for this frame: tile changes (SetTile from cs2d_update_mapdata), entity states and Env_Light rotation
 * (Te.update), sun angle (FUN_140262ed0), then TSmartLighting.Update (light buffers). cam = integer camera. */
void cs2d_light_frame(const cs2d_map_t *map, int cam_x, int cam_y, float dt);

/* World passes, in the order of FUN_140281fd0 */
void cs2d_light_draw_floor(void);   /* DrawLayer1: floor occlusion, after the items */
void cs2d_light_draw_color(void);   /* DrawLightsColorList: additive light color, after the players */
void cs2d_light_draw_buffer(void);  /* DrawLayer2: the light buffer, shade blend, over the world */

/* Prints every Max2D call (and skips OpenGL) while set; NULL restores drawing */
void cs2d_light_set_trace(void (*fn)(const char *line, void *ud), void *ud);
/* Scene file for tools/agent/emu_light.py (the engine state after cs2d_light_frame) */
void cs2d_light_dump_scene(FILE *f, int cam_x, int cam_y);

#endif /* CS2D_LIGHT_H */
