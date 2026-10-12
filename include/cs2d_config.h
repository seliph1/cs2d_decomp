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
    bool hud_fastswitch; /* "hud_fastswitch": a slot key with one item holds it at once (DAT_140ddf19e) */
    int  drawradar;      /* "drawradar" 0..4 (DAT_140ddf19b) */
    int  radarsize;      /* "radarsize" (DAT_140ddf19a) */
} cs2d_config_t;

/* Load config from sys/config.cfg */
bool cs2d_config_load(cs2d_config_t *cfg, const char *filepath);

#endif /* CS2D_CONFIG_H */

