#ifndef CS2D_TRANSFER_H
#define CS2D_TRANSFER_H

#include "cs2d_types.h"
#include "cs2d_network.h"

/*
 * Join-time file transfer (server -> client), reconstructed from CS2D.exe:
 *   [252,50,sub] handler   FUN_1402cd970 (called from the join dispatcher FUN_1402ce7c0)
 *   path conditions        FUN_1402cb770 (normal files), FUN_1402cb540 (maps/), FUN_1402cbcd0 (mods/)
 *   receiving              Ttrans_recv.ini 0x1400f43a0, .add 0x1400f4460, .finish 0x1402cc1a0
 *   uncompress             FUN_14022d2f0: [raw size i32][zlib]; a negative size means stored
 *   download log           FUN_1402cbff0: sys/core/dls.cache
 *   progress window        Tgui_joinmen.mapdownload 0x140004080, .draw 0x140301640 (menu 10)
 *
 * Messages (proxy capture against a modded dedicated, tools/agent/capture_transfer.sh):
 *   S>C [252,4,13]                          files are needed: open the "Downloading" window
 *   S>C [252,50,1, n u16, n x str]          file list, chunks of 10; n = 0 ends the list
 *   S>C [252,50,3, n u16, n x {str, 8 x i32}] files the server keeps in sys/cache/transfer/<sha256>
 *   C>S [252,50,2, n u16, n x str]          requested files, chunks of 10; then [252,50,2, 0 | 0xffff]
 *                                           (0xffff = HTTP downloads queued too)
 *   S>C [252,50,5, str file, i32 total, i32 len, len bytes]   first block (Ttrans_recv.ini)
 *   S>C [252,50,6, str file, i32 len, len bytes]              more data
 *   S>C [252,50,7, str file, i32 len, len bytes]              last block: uncompress and save
 * Strings are u8 length + bytes.
 */

#define CS2D_XFER_PATH        256
#define CS2D_XFER_MAX_FILES   2048
#define CS2D_XFER_CHUNK_FILES 10      /* FUN_1402cd970: a new [252,50,2] message every 10 names */
#define CS2D_XFER_MAX_PKTS    (CS2D_XFER_MAX_FILES / CS2D_XFER_CHUNK_FILES + 2)

/* FUN_1402cb770 results; each one has its console reason (FUN_1402cbe70) */
#define CS2D_XFER_OK                1
#define CS2D_XFER_UNKNOWN           0
#define CS2D_XFER_BAD_MAPS_EXT     -1
#define CS2D_XFER_BAD_TILES_EXT    -2
#define CS2D_XFER_RELOAD_SOUND     -3
#define CS2D_XFER_WEAPONS_NOT_SND  -4
#define CS2D_XFER_NO_SOUNDS        -5
#define CS2D_XFER_NO_IMAGES        -6
#define CS2D_XFER_INVALID_PATH     -7
#define CS2D_XFER_INVALID_EXT      -8

/* Result of cs2d_xfer_on_message */
typedef enum {
    CS2D_XFER_EV_NONE = 0,
    CS2D_XFER_EV_LIST_DONE,     /* The list ended: send cs2d_xfer_build_requests() */
    CS2D_XFER_EV_FILE_SAVED,
    CS2D_XFER_EV_FILE_FAILED,
    CS2D_XFER_EV_MAP_FAILED     /* "Map file is crucial for playing" (join error 0x67) */
} cs2d_xfer_event_t;

typedef void (*cs2d_xfer_log_fn)(void *user, uint8_t r, uint8_t g, uint8_t b, const char *text);

typedef struct {
    char path[CS2D_XFER_PATH];
    char sha[65];
} cs2d_xfer_cached_t;

typedef struct {
    int    accept;            /* "download" setting (DAT_140a14f4c): 0 none, 2 no sounds, 3 no images, else all */
    int    map_mode;          /* DAT_140ddf3dc: 0 normal, 1 save the map as maps/temp.map, 2 overwrite */

    char   (*requests)[CS2D_XFER_PATH];   /* Files to request (list 0x140a18340) */
    int    request_count;
    cs2d_xfer_cached_t *cached;          /* lua_filetransfer.cache: [252,50,3] */
    int    cached_count;

    /* File being received (Ttrans_recv) */
    char     file[CS2D_XFER_PATH];
    int      tsize;
    uint8_t *data;
    int      data_len;
    int      data_cap;

    /* Progress shown by the window (Tgui_joinmen +0x88 dlfile, +0x90 dlprog, +0x98 dlperc, +0x9c / +0xa0) */
    int    files_received;    /* dlfnum */
    int    files_total;       /* dlftotal */
    float  perc;
    char   prog[64];
    char   saved_map[CS2D_XFER_PATH];   /* Last map file written (maps/<name>.map or maps/temp.map) */

    cs2d_xfer_log_fn log;
    void            *log_user;
} cs2d_xfer_t;

void cs2d_xfer_init(cs2d_xfer_t *x);
void cs2d_xfer_free(cs2d_xfer_t *x);
/* Forget the request list and the file being received (a new join) */
void cs2d_xfer_reset(cs2d_xfer_t *x);

/* FUN_1402cb770: may this path be requested? CS2D_XFER_OK or a negative reason */
int         cs2d_xfer_check_path(const char *path, int accept);
const char *cs2d_xfer_reason(int code);

/* Parses one [252,50,sub,...] message; *pos points at sub and moves past the message */
cs2d_xfer_event_t cs2d_xfer_on_message(cs2d_xfer_t *x, const uint8_t *buf, int len, int *pos);

/* [252,50,2] request messages for the current list, terminator included; returns the count */
int cs2d_xfer_build_requests(const cs2d_xfer_t *x, cs2d_pkt_t *pkts, int max_pkts);

/* FUN_14022d2f0: [raw size i32][zlib data]; a negative size means the bytes follow uncompressed */
bool cs2d_xfer_uncompress(const uint8_t *in, int in_len, uint8_t **out, int *out_len);

#endif
