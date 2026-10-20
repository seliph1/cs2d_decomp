#ifndef CS2D_BIND_H
#define CS2D_BIND_H

#include <stdbool.h>

/*
 * Key bindings of sys/controls.cfg (`bind "<key>" "<command>"`, Tbind in CS2D.exe). Keys use the controls.cfg
 * names: "1", "W", "space", "leftctrl", "mouse1", "mwheelup", ...
 */
#define CS2D_BIND_MAX      128
#define CS2D_BIND_KEY_LEN  24
#define CS2D_BIND_CMD_LEN  64

typedef struct {
    char key[CS2D_BIND_KEY_LEN];
    char cmd[CS2D_BIND_CMD_LEN];
} cs2d_bind_t;

typedef struct {
    cs2d_bind_t binds[CS2D_BIND_MAX];
    int         count;
} cs2d_binds_t;

/* Weapon selection defaults of the shipped sys/controls.cfg: "1".."9" slot1..slot9, "0" slot10,
 * mwheelup invprev, mwheeldown invnext */
void cs2d_binds_defaults(cs2d_binds_t *b);
/* Adds the `bind` lines of a controls file over the current table; false if the file is missing */
bool cs2d_binds_load(cs2d_binds_t *b, const char *filepath);
/* Binds key (case-insensitive) to cmd; an empty cmd removes the binding */
void cs2d_binds_set(cs2d_binds_t *b, const char *key, const char *cmd);
/* Command bound to key, or NULL */
const char *cs2d_binds_find(const cs2d_binds_t *b, const char *key);
/* controls.cfg name of one of our key codes (cs2d_client_handle_key), or NULL */
/* Key codes F1..F12 (CS2D_KEY_F1 + n): outside the ASCII / VK range, where VK_F1.. would collide with 'p'..'{' */
#define CS2D_KEY_F1 0x1000
const char *cs2d_bind_key_name(int key);
/* Canonical name of a Win32 VK code from the Lua bind list (f1 = 0x70, kp_5 = 0x65, ...) */
const char *cs2d_bind_vk_name(int key);
/* VK code of a Lua addbind() key name ("Q", "f1", "leftshift", "mouse1", "kp_5", ...);
 * 0 when the name is not a valid Lua bind key. Captured from cs2d_dedicated.exe [252,6]. */
int cs2d_bind_key_code(const char *name);
/* controls.cfg name of a mouse button (1 left, 2 right, 3 middle): "mouse1".."mouse3", or NULL */
const char *cs2d_bind_mouse_name(int button);

#endif /* CS2D_BIND_H */
