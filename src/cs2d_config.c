#include "cs2d_config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

bool cs2d_config_load(cs2d_config_t *cfg, const char *filepath)
{
    if (!cfg) return false;

    /* Defaults matching CS2D standard settings */
    strncpy(cfg->name, "Player", sizeof(cfg->name));
    cfg->window_width = 850;
    cfg->window_height = 480;
    cfg->windowed = true;
    cfg->sound_volume = 10;
    strncpy(cfg->language, "English", sizeof(cfg->language));
    strncpy(cfg->spray_logo, "unrealsoftware.bmp", sizeof(cfg->spray_logo));
    cfg->recoil_enabled = true;
    strncpy(cfg->server_ip, "127.0.0.1", sizeof(cfg->server_ip));
    cfg->server_port = 36963;

    FILE *f = fopen(filepath, "r");
    if (!f) return false;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        /* Strip whitespace / newline */
        char *nl = strchr(line, '\r');
        if (nl) *nl = '\0';
        nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        if (line[0] == '\0' || line[0] == '/' || line[0] == '#') continue;

        char key[64] = {0};
        char val[128] = {0};
        if (sscanf(line, "%63s %127[^\n]", key, val) == 2) {
            if (strcmp(key, "name") == 0) {
                strncpy(cfg->name, val, sizeof(cfg->name) - 1);
            } else if (strcmp(key, "res") == 0) {
                int w = 0, h = 0;
                if (sscanf(val, "%dx%d", &w, &h) == 2 && w >= 640 && h >= 480) {
                    cfg->window_width = w;
                    cfg->window_height = h;
                }
            } else if (strcmp(key, "win") == 0) {
                cfg->windowed = (atoi(val) != 0);
            } else if (strcmp(key, "volume") == 0) {
                cfg->sound_volume = atoi(val);
            } else if (strcmp(key, "language") == 0) {
                strncpy(cfg->language, val, sizeof(cfg->language) - 1);
            } else if (strcmp(key, "recoil") == 0) {
                cfg->recoil_enabled = (atoi(val) != 0);
            }
        }
    }

    fclose(f);
    return true;
}

