/*
 * GDI+ vs stb_truetype text comparison (Windows only: it needs both backends of src/cs2d_font.c).
 *
 * Corpus: every string of the given language files (the "NNNN=" prefix and "|" breaks removed) plus each
 * printable ASCII character alone. For every font id it reports how many strings measure to the same
 * ceil(width) (what the layout uses, cs2d_render_text_width_ex), the worst width error, the height, and
 * the mean / max absolute coverage difference of the rasterized texture.
 *
 *   tools/web/font_compare.sh [lang files...]     (run from the repo root; writes PGM samples to out/font_compare)
 */
#include "cs2d_font.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAX_STRINGS 8192

static char *s_strings[MAX_STRINGS];
static int   s_nstrings;

static void add_string(const char *s, size_t n)
{
    while (n > 0 && (s[n - 1] == '\r' || s[n - 1] == '\n')) n--;
    if (n == 0 || n > 180 || s_nstrings >= MAX_STRINGS) return;
    char *c = (char *)malloc(n + 1);
    memcpy(c, s, n);
    c[n] = '\0';
    s_strings[s_nstrings++] = c;
}

static void load_lang(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return; }
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (line[0] == '/' || !eq) continue;
        for (char *p = eq + 1;;) {
            char *bar = strchr(p, '|');
            add_string(p, bar ? (size_t)(bar - p) : strlen(p));
            if (!bar) break;
            p = bar + 1;
        }
    }
    fclose(f);
}

static void write_pgm(const char *path, const uint8_t *a, const uint8_t *b, int w, int h)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P5\n%d %d\n255\n", w, h * 2 + 1);
    fwrite(a, 1, (size_t)w * h, f);
    for (int x = 0; x < w; x++) fputc(128, f);
    fwrite(b, 1, (size_t)w * h, f);
    fclose(f);
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) load_lang(argv[i]);
    if (argc < 2) load_lang("sys/language/EN-English.txt");
    for (char c = 32; c < 127; c++) add_string(&c, 1);
    add_string("Liberation Sans  trailing  ", 27);

    if (!cs2d_font_backend_ready(CS2D_FONT_BACKEND_GDIPLUS) || !cs2d_font_backend_ready(CS2D_FONT_BACKEND_STB)) {
        fprintf(stderr, "a backend failed to start\n");
        return 1;
    }

    static const struct { cs2d_font_id_t id; const char *name; } fonts[] = {
        { CS2D_FONT_REGULAR, "regular13" }, { CS2D_FONT_SMALL, "small10" }, { CS2D_FONT_BOLD, "bold13" },
        { CS2D_FONT_TITLE, "title18" }, { CS2D_FONT_MENU, "menu15" },
        { CS2D_FONT_SIZED(20), "sized20" }, { CS2D_FONT_SIZED(40), "sized40" },
    };
    static uint8_t ca[1024 * 128], cb[1024 * 128];
    int samples = 0;

    printf("%-10s %6s %8s %8s %9s %9s %8s %8s\n", "font", "n", "ceilEq", "maxErr", "hGdi", "hStb", "covMean", "covMax");
    for (size_t fi = 0; fi < sizeof(fonts) / sizeof(fonts[0]); fi++) {
        int n = 0, eq = 0, cov_max = 0;
        double max_err = 0.0, cov_sum = 0.0, cov_px = 0.0;
        float hg = 0.0f, hs = 0.0f;
        const char *worst = "";
        for (int i = 0; i < s_nstrings; i++) {
            float wg, ws;
            if (!cs2d_font_measure_with(CS2D_FONT_BACKEND_GDIPLUS, fonts[fi].id, s_strings[i], -1, &wg, &hg)) continue;
            cs2d_font_measure_with(CS2D_FONT_BACKEND_STB, fonts[fi].id, s_strings[i], -1, &ws, &hs);
            n++;
            if (ceilf(wg) == ceilf(ws)) eq++;
            double err = fabs((double)wg - (double)ws);
            if (err > max_err) { max_err = err; worst = s_strings[i]; }

            int tw = (int)ceilf(wg) + 2, th = (int)ceilf(hg) + 2;
            if (tw > 1024) tw = 1024;
            if (th > 128) th = 128;
            memset(ca, 0, (size_t)tw * th);
            memset(cb, 0, (size_t)tw * th);
            cs2d_font_raster_with(CS2D_FONT_BACKEND_GDIPLUS, fonts[fi].id, s_strings[i], ca, tw, th);
            cs2d_font_raster_with(CS2D_FONT_BACKEND_STB, fonts[fi].id, s_strings[i], cb, tw, th);
            for (int p = 0; p < tw * th; p++) {
                int d = abs((int)ca[p] - (int)cb[p]);
                cov_sum += d;
                if (d > cov_max) cov_max = d;
            }
            cov_px += tw * th;
            if (i < 6 || (i % 97) == 0) {
                char path[256];
                snprintf(path, sizeof(path), "out/font_compare/%s_%04d.pgm", fonts[fi].name, i);
                write_pgm(path, ca, cb, tw, th);
                samples++;
            }
        }
        printf("%-10s %6d %7.2f%% %8.3f %9.3f %9.3f %8.3f %8d   worst: \"%s\"\n", fonts[fi].name, n,
               n ? 100.0 * eq / n : 0.0, max_err, hg, hs, cov_px ? cov_sum / cov_px : 0.0, cov_max, worst);
    }
    printf("%d sample images (top: GDI+, bottom: stb) in out/font_compare/\n", samples);
    return 0;
}
