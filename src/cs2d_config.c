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
    cfg->mapsounds = true;
    cfg->soundmode = true;
    strncpy(cfg->language, "English", sizeof(cfg->language));
    strncpy(cfg->spray_logo, "unrealsoftware.bmp", sizeof(cfg->spray_logo));
    cfg->recoil_enabled = true;
    strncpy(cfg->server_ip, "127.0.0.1", sizeof(cfg->server_ip));
    cfg->server_port = 36963;
    cfg->download = 1;
    /* Shipped sys/config.cfg values */
    cfg->hud_fastswitch = true;
    cfg->deathnoticeimg = true;
    cfg->hud_deathnotice_time = 8;
    cfg->drawradar = 4;
    cfg->radarsize = 0;
    cfg->shadows = 50;
    cfg->playershadows = true;
    cfg->itemshadows = true;
    cfg->mapshadows = 4;
    cfg->light = 5;
    cfg->vsync = true;   /* VSync is enabled by default (Changelog 1.0.1.0) */

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
            } else if (strcmp(key, "mapsounds") == 0) {
                cfg->mapsounds = (atoi(val) != 0);
            } else if (strcmp(key, "soundmode") == 0) {
                cfg->soundmode = (atoi(val) != 0);
            } else if (strcmp(key, "language") == 0) {
                strncpy(cfg->language, val, sizeof(cfg->language) - 1);
            } else if (strcmp(key, "recoil") == 0) {
                cfg->recoil_enabled = (atoi(val) != 0);
            } else if (strcmp(key, "download") == 0) {
                cfg->download = atoi(val);
            } else if (strcmp(key, "hud_fastswitch") == 0) {
                cfg->hud_fastswitch = (atoi(val) != 0);
            } else if (strcmp(key, "deathnoticeimg") == 0) {
                cfg->deathnoticeimg = (atoi(val) != 0);
            } else if (strcmp(key, "hud_deathnotice_time") == 0) {
                cfg->hud_deathnotice_time = atoi(val);
            } else if (strcmp(key, "drawradar") == 0) {
                cfg->drawradar = atoi(val);
            } else if (strcmp(key, "radarsize") == 0) {
                cfg->radarsize = atoi(val);
            } else if (strcmp(key, "shadows") == 0) {
                cfg->shadows = atoi(val);
            } else if (strcmp(key, "playershadows") == 0) {
                cfg->playershadows = (atoi(val) != 0);
            } else if (strcmp(key, "itemshadows") == 0) {
                cfg->itemshadows = (atoi(val) != 0);
            } else if (strcmp(key, "mapshadows") == 0) {
                cfg->mapshadows = atoi(val);
            } else if (strcmp(key, "light") == 0) {
                cfg->light = atoi(val);
            } else if (strcmp(key, "vsync") == 0) {
                cfg->vsync = (atoi(val) != 0);
            }
        }
    }

    fclose(f);
    return true;
}

