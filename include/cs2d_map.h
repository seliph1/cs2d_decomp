#ifndef CS2D_MAP_H
#define CS2D_MAP_H

#include "cs2d_types.h"

/* Load CS2D .map file into structure (reconstructed from FUN_1400493d0 & FUN_140113d50) */
bool cs2d_map_load_file(cs2d_map_t *map, const char *filepath);

/* Save CS2D .map file from structure (reconstructed from FUN_1401f09b0) */
bool cs2d_map_save_file(const cs2d_map_t *map, const char *filepath);

/* Free allocated map memory (tiles, modifiers, collision) */
void cs2d_map_free(cs2d_map_t *map);

/* Get tile index at tile coordinates (tx, ty) */
uint8_t cs2d_map_get_tile(const cs2d_map_t *map, uint32_t tx, uint32_t ty);

/* Get tile mode (0=floor, 1=wall, 2=obstacle, etc.) at tile coordinates (tx, ty) */
uint8_t cs2d_map_get_tile_mode(const cs2d_map_t *map, uint32_t tx, uint32_t ty);

/* Collision value of a tile mode: 0 floor, 1 wall, 2 obstacle, 3 deadly (the map loader's mapping) */
uint8_t cs2d_map_mode_collision(uint8_t mode);

/* Check if pixel position (px, py) is solid (wall or obstacle) */
bool cs2d_map_is_solid(const cs2d_map_t *map, float px, float py);

/* Check if pixel position (px, py) is a bullet-blocking wall (modes 1, 3, 5) */
bool cs2d_map_is_wall(const cs2d_map_t *map, float px, float py);

/* Get spawn position in pixels for team (index 0..count-1) */
bool cs2d_map_get_spawn(const cs2d_map_t *map, cs2d_team_t team, size_t index, cs2d_vec2_t *out_pos);

/* Count spawns for a team */
size_t cs2d_map_count_spawns(const cs2d_map_t *map, cs2d_team_t team);

/* Check if pixel position is within a bomb spot zone (Info_BombSpot, type 5) */
bool cs2d_map_is_in_bombspot(const cs2d_map_t *map, float px, float py);

/* Reconstructed from Te.inzone (FUN_140033fd0 in CS2D.exe): check if tile (tx,ty) is inside an entity zone */
bool cs2d_map_in_zone(const cs2d_map_t *map, cs2d_entity_type_t type, int tx, int ty);

/* Return authentic CS2D entity type name (0=Info_T .. 96=Info_Portal) */
const char *cs2d_entity_type_name(cs2d_entity_type_t type);

#endif /* CS2D_MAP_H */
