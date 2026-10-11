#ifndef CS2D_CONFIG_H
#define CS2D_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    char name[32];
    int  window_width;
    int  window_height;
    bool windowed;
    int  sound_volume;
    char language[32];
    char spray_logo[64];
    bool recoil_enabled;
    char server_ip[64];
    int  server_port;
    int  download;      /* "download": which files a server may send (DAT_140a14f4c) */
} cs2d_config_t;

/* Load config from sys/config.cfg */
bool cs2d_config_load(cs2d_config_t *cfg, const char *filepath);

#endif /* CS2D_CONFIG_H */

