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
    int  shadows;        /* "shadows" 0..100 shadow opacity (DAT_140a14f7b) */
    bool playershadows;  /* "playershadows" 0|1 draw player shadows (DAT_140ddf184) */
    bool itemshadows;    /* "itemshadows" 0|1 draw ground item shadows (DAT_140ddf183) */
    int  mapshadows;     /* "mapshadows" 0..4 (DAT_140ddf190) */
    int  light;          /* "light" 0..5 light engine quality, 0 = off (DAT_140ddf185) */
    bool deathnoticeimg;       /* "deathnoticeimg" 0|1 kill messages with weapon images (DAT_140ddf191) */
    int  hud_deathnotice_time; /* "hud_deathnotice_time" s a kill message stays, default 8 in .data (DAT_140a14c80) */
    bool vsync;          /* "vsync" 0|1 OpenGL swap interval, default 1 (DAT_140a12960) */
} cs2d_config_t;

/* Load config from sys/config.cfg */
bool cs2d_config_load(cs2d_config_t *cfg, const char *filepath);

#endif /* CS2D_CONFIG_H */

