/*
 * Prints our light engine's draw calls (cs2d_light_set_trace) for one map and camera, and writes the scene file that
 * tools/agent/emu_light.py runs through the original. Compare both with tools/agent/light_diff.py.
 *
 * build: gcc -Iinclude tools/agent/light_trace.c <every src/*.c but main_client.c> -lSDL3 -lopengl32 -lgdi32 -lws2_32
 *        -l:libz.a -lm -o tools/agent/out/light_trace.exe   (tools/agent/light_check.sh does it)
 * usage: light_trace.exe <map file> <cam x> <cam y> <main|floor|color> <scene out> [light mapshadows shadows]
 *        DAYLIGHT=<angle> overrides the map's sun angle
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cs2d_light.h"
#include "cs2d_map.h"

static void print_line(const char *line, void *ud)
{
    (void)ud;
    printf("%s\n", line);
}

int main(int argc, char **argv)
{
    if (argc < 6) {
        fprintf(stderr, "usage: %s <map> <cam x> <cam y> <main|floor|color> <scene out> [light mapshadows shadows]\n", argv[0]);
        return 2;
    }
    static cs2d_map_t map;
    if (!cs2d_map_load_file(&map, argv[1])) {
        fprintf(stderr, "cannot load %s\n", argv[1]);
        return 1;
    }
    int cx = atoi(argv[2]), cy = atoi(argv[3]);
    const char *day = getenv("DAYLIGHT");   /* sun angle override (map daylight time) */
    if (day && day[0]) map.daylight_time = atoi(day) % 360;
    cs2d_light_config_t cfg = {5, 4, 50, true};
    if (argc > 8) {
        cfg.light = atoi(argv[6]);
        cfg.mapshadows = atoi(argv[7]);
        cfg.shadows = atoi(argv[8]);
    }
    cs2d_light_configure(&cfg);
    cs2d_light_load_map(&map);
    if (strcmp(argv[4], "main") == 0) cs2d_light_set_trace(print_line, NULL);
    cs2d_light_frame(&map, cx, cy, 1.0f);
    if (strcmp(argv[4], "floor") == 0) {
        cs2d_light_set_trace(print_line, NULL);
        cs2d_light_draw_floor();
    } else if (strcmp(argv[4], "color") == 0) {
        cs2d_light_set_trace(print_line, NULL);
        cs2d_light_draw_color();
    }
    cs2d_light_set_trace(NULL, NULL);
    FILE *f = fopen(argv[5], "w");
    if (f) {
        cs2d_light_dump_scene(f, cx, cy);
        fclose(f);
    }
    return 0;
}
