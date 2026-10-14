#include "cs2d_tilefx.h"
#include <math.h>
#include <string.h>

/* BlitzMax Rand(lo, hi), both inclusive */
static int rand_i(uint32_t *s, int lo, int hi)
{
    if (hi < lo) { int t = lo; lo = hi; hi = t; }
    *s = *s * 1664525u + 1013904223u;
    return lo + (int)((*s >> 8) % (uint32_t)(hi - lo + 1));
}

static int wrap(int v, int n)
{
    v %= n;
    return v < 0 ? v + n : v;
}

int cs2d_tilefx_scroll_step(int v, int size)
{
    int q = size / 32;
    int a = v < 0 ? -v : v, sign = v < 0 ? -1 : 1;
    if (a == 3) return sign * 2 * q;
    if (a == 5) return sign * 4 * q;
    if (a == 7 || (a >= 9 && a <= 12)) return sign * 8 * q;
    if (a >= 13 && a <= 15) return sign * 16 * q;
    return v;
}

/* Effect 0 (0x1400aaf00..): frame k = the tile moved by k * (dx, dy), wrapping */
static int build_scroll(const uint32_t *src, int s, int ints2, int ints3, uint32_t *out)
{
    int half = s / 2;
    int dx = ints2 > half ? half : (ints2 < -half ? -half : ints2);
    int dy = ints3 > half ? half : (ints3 < -half ? -half : ints3);
    dx = cs2d_tilefx_scroll_step(dx, s);
    dy = cs2d_tilefx_scroll_step(dy, s);
    if (dx == 0 && dy == 0) return 0; /* No Ttilefx is added */
    int n1 = dx ? s / (dx < 0 ? -dx : dx) - 1 : 0;
    int n2 = dy ? s / (dy < 0 ? -dy : dy) - 1 : 0;
    int last = n1 > n2 ? n1 : n2;
    if (last >= s) last = s - 1;
    if (last >= CS2D_TILEFX_MAX_FRAMES) last = CS2D_TILEFX_MAX_FRAMES - 1;
    if (last < 0) last = 0;
    int ox = 0, oy = 0;
    for (int k = 0; k <= last; k++) {
        uint32_t *dst = out + (size_t)k * s * s;
        for (int y = 0; y < s; y++) {
            for (int x = 0; x < s; x++) dst[wrap(oy + y, s) * s + wrap(ox + x, s)] = src[y * s + x];
        }
        ox = wrap(ox + dx, s);
        oy = wrap(oy + dy, s);
    }
    return last + 1;
}

/* Effects 1..3: 16 frames, each the tile with N + 1 random pixels copied Rand(-q, q) away */
static int build_noise(const uint32_t *src, int s, int effect, uint32_t seed, uint32_t *out)
{
    int q = s / 32;
    int n = effect == 2 ? 100 : (effect == 3 ? 500 : 50);
    uint32_t r = seed ? seed : 0x7113fa;
    for (int k = 0; k < 16; k++) {
        uint32_t *dst = out + (size_t)k * s * s;
        memcpy(dst, src, (size_t)s * s * 4);
        for (int i = 0; i <= n; i++) {
            int x = rand_i(&r, 0, s - 1), y = rand_i(&r, 0, s - 1);
            uint32_t c = src[y * s + x];
            int nx = x + rand_i(&r, -q, q), ny = y + rand_i(&r, -q, q);
            if (s < 64) {
                if (nx < 0) nx = s - 1; else if (nx >= s) nx = 0;
                if (ny < 0) ny = s - 1; else if (ny >= s) ny = 0;
                dst[ny * s + nx] = c;
            } else {
                /* Tiles of 64 px and more: a 2x2 block */
                if (nx < 0) nx = s - 2;
                if (ny < 0) ny = s - 2;
                int nx2 = nx + 1, ny2 = ny + 1;
                if (nx >= s - 1) { nx = 0; nx2 = 1; }
                if (ny >= s - 1) { ny = 0; ny2 = 1; }
                dst[ny * s + nx] = c;
                dst[ny * s + nx2] = c;
                dst[ny2 * s + nx2] = c;
                dst[ny2 * s + nx] = c;
            }
        }
    }
    return 16;
}

/* Effects 4..6: 30 frames of a sine displacement, phase + 12 degrees per frame */
static int build_wave(const uint32_t *src, int s, int effect, uint32_t *out)
{
    int q = s / 32;
    if (q < 1) q = 1;
    double amp = (double)((effect - 3) * q);
    for (int k = 0; k < 30; k++) {
        int ph = k * 12;
        uint32_t *dst = out + (size_t)k * s * s;
        for (int x = 0; x < s; x++) {
            double sx_ = sin((double)((x * 11) / q + ph) * 0.017453292519943295);
            for (int y = 0; y < s; y++) {
                int ry = (int)((double)y + sx_ * amp);
                int rx = (int)(sin((double)((y * 11) / q + ph) * 0.017453292519943295) * amp + (double)x);
                dst[y * s + x] = src[wrap(ry, s) * s + wrap(rx, s)];
            }
        }
    }
    return 30;
}

int cs2d_tilefx_build(const uint32_t *src, int size, int effect, int ints2, int ints3, uint32_t seed,
                      uint32_t *out, int *speed_ms)
{
    if (!src || !out || size < 32) return 0;
    if (effect == 0) {
        if (speed_ms) *speed_ms = 60;
        return build_scroll(src, size, ints2, ints3, out);
    }
    if (effect >= 1 && effect <= 3) {
        if (speed_ms) *speed_ms = 50;
        return build_noise(src, size, effect, seed, out);
    }
    if (effect >= 4 && effect <= 6) {
        if (speed_ms) *speed_ms = 80;
        return build_wave(src, size, effect, out);
    }
    return 0;
}
