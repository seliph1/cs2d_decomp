#ifndef CS2D_ATLAS_H
#define CS2D_ATLAS_H

#include <stdbool.h>
#include <stdint.h>
#include "cs2d_render.h"
#include "cs2d_map.h"

#define CS2D_ATLAS_PAGE_SIZE 2048
#define CS2D_ATLAS_PAD       4

/*
 * One item to be packed into an atlas page.
 * Pure GL-free descriptor for shelf packing and placement math.
 */
typedef struct {
    char      path[128];
    int       mask_mode;    /* 1 = normal / magenta, 3 = black mask / additive */
    int       w, h;         /* Logical sprite dimensions (tex->width, tex->height) */
    int       page_index;   /* 0-based page index */
    int       x, y;         /* Allocated cell origin on page (including extruded border) */
    int       src_x, src_y; /* Logical sprite content origin on page (x + pad, y + pad) */
    bool      placed;       /* True if placed on a page */
    uint32_t *rgba;         /* Decoded & processed RGBA pixels (w * h), NULL in GL-free tests */
} cs2d_atlas_item_t;

/*
 * Shelf First-Fit Decreasing (FFD) packer.
 * Packs `count` items into pages of `page_w` x `page_h` with `pad` extruded pixels around each item.
 * Pure GL-free algorithm.
 * Returns the number of pages used (>= 1 if any item placed), or 0 if count == 0.
 */
int cs2d_atlas_pack_items(
    cs2d_atlas_item_t *items,
    int count,
    int page_w,
    int page_h,
    int pad
);

/*
 * Builds atlas pages for the entity sprite set of a map and populates the bank's sprite cache.
 * Uses separate pages for mask_mode == 1 (normal) vs mask_mode == 3 (black_mask / additive).
 * Sprites maintain their logical unpadded size in tex->width/height, while tex->id points to the
 * atlas page texture and tex->src_x/src_y hold the sprite's offset in the atlas page.
 */
bool cs2d_gfx_atlas_load_map(cs2d_gfx_bank_t *bank, const cs2d_map_t *map);

/*
 * Frees atlas pages and cached sprites in the bank.
 */
void cs2d_gfx_bank_free_sprites(cs2d_gfx_bank_t *bank);

#endif /* CS2D_ATLAS_H */

