#include <stdio.h>
#include <stdlib.h>
#include "cs2d_map.h"
static cs2d_map_t m;
int main(int argc, char **argv) {
    if (!cs2d_map_load_file(&m, argv[1])) { printf("load failed\n"); return 1; }
    printf("map %ux%u entities=%u\n", m.width, m.height, m.entity_count);
    for (uint32_t i = 0; i < m.entity_count; i++) {
        cs2d_entity_t *e = &m.entities[i];
        printf("#%u %-18s (%3d,%3d) name='%s' trig='%s' ints=[", i, cs2d_entity_type_name(e->type), e->x, e->y, e->name, e->trigger);
        for (int k = 0; k < 10; k++) printf("%d%s", e->int_settings[k], k < 9 ? "," : "");
        printf("] strs=[");
        for (int k = 0; k < 10; k++) if (e->str_settings[k][0]) printf("%d:'%s' ", k, e->str_settings[k]);
        printf("]\n");
    }
    return 0;
}
