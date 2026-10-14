#ifndef CS2D_TILEFX_H
#define CS2D_TILEFX_H

#include <stdint.h>

/*
 * Info_TileFX (10): Ttilefx.CreateTileFX (0x1400aa640) builds animation frames from one tileset frame
 * (ints[0]) at map load (FUN_1401d7c40, "Generating Tile Effects", together with Info_Animation; the first
 * entity on a frame wins, canCreateNewTileFXFromEntity 0x1400f4930). Ttilefx.update (0x1400f4b60) then shows
 * frame (f_current - f_start), advancing one frame every `speed` ms and wrapping.
 *
 * ints[1] = effect:
 *   0     scroll: frame k is the tile shifted by k * (dx, dy) px with wrap-around; dx / dy = ints[2] / ints[3]
 *         clamped to +-size/2 and mapped by cs2d_tilefx_scroll_step; up to `size` frames, 60 ms
 *   1..3  noise: 16 frames, each a copy with 51 / 101 / 501 pixels moved by Rand(-q, q) (q = size / 32;
 *         2x2 blocks for tiles >= 64 px), 50 ms
 *   4..6  wave: 30 frames, pixel (x, y) reads (x + sin((y*11/q + ph) deg) * a, y + sin((x*11/q + ph) deg) * a),
 *         a = (ints[1] - 3) * q, ph = 12 per frame, 80 ms
 */
#define CS2D_TILEFX_MAX_FRAMES 64

/* The pixel step for an ints[2] / ints[3] value (the CreateTileFX switch; values not listed stay as they are) */
int cs2d_tilefx_scroll_step(int v, int size);

/*
 * Builds the frames of an effect from the size x size tile `src` (ARGB) into `out` (frames one after the
 * other, size * size each, room for CS2D_TILEFX_MAX_FRAMES). Returns the frame count (0 = no effect) and the
 * frame time in ms. `seed` drives the noise effect (the original uses Rand).
 */
int cs2d_tilefx_build(const uint32_t *src, int size, int effect, int ints2, int ints3, uint32_t seed,
                      uint32_t *out, int *speed_ms);

#endif
