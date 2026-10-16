#ifndef CS2D_SV_LUA_H
#define CS2D_SV_LUA_H

#include "cs2d_server.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#define CS2D_SV_MAX_HOOKS       128
#define CS2D_SV_MAX_TIMERS      256
#define CS2D_SV_MAX_IMAGES      2048

/* Hook listener entry (ordered by priority ascending) */
typedef struct cs2d_hook_node {
    char func[64];
    int priority;
    struct cs2d_hook_node *next;
} cs2d_hook_node_t;

/* Hook definition */
typedef struct {
    char name[32];
    cs2d_hook_node_t *head;
} cs2d_hook_t;

/* Timer */
typedef struct {
    bool     active;
    uint32_t start_ms;
    int      interval_ms;
    int      count;          /* 0 = infinite, >0 = remaining triggers */
    char     func[64];
    char     param[128];
} cs2d_sv_timer_t;

/* Lua image (Tdo 40) */
typedef struct {
    bool     active;
    uint16_t id;
    char     path[128];
    float    x, y, rot;
    uint8_t  mode;
    uint8_t  player;
    float    alpha;
    uint8_t  blend;
    uint8_t  r, g, b;
    float    sx, sy;
    uint8_t  frame;
    uint8_t  shadow;
} cs2d_sv_image_t;

/* Lua subsystem state */
typedef struct cs2d_sv_lua {
    lua_State        *L;
    cs2d_server_t    *sv;
    cs2d_hook_t       hooks[CS2D_SV_MAX_HOOKS];
    int               hook_count;
    cs2d_sv_timer_t   timers[CS2D_SV_MAX_TIMERS];
    cs2d_sv_image_t   images[CS2D_SV_MAX_IMAGES];
    int               next_image_id;
    uint32_t          last_ms100;
    uint32_t          last_second;
    uint32_t          last_minute;
} cs2d_sv_lua_t;

/* Lifetime */
bool cs2d_sv_lua_init(cs2d_server_t *sv);
void cs2d_sv_lua_close(cs2d_server_t *sv);
void cs2d_sv_lua_frame(cs2d_server_t *sv);
void cs2d_sv_lua_step_timers(cs2d_server_t *sv);

/* Script runners */
bool cs2d_sv_lua_dofile(cs2d_server_t *sv, const char *path);
bool cs2d_sv_lua_dostring(cs2d_server_t *sv, const char *code);
void cs2d_sv_lua_load_autorun(cs2d_server_t *sv);
void cs2d_sv_lua_load_map(cs2d_server_t *sv, const char *mapname);

/* Hook dispatcher */
int  cs2d_sv_lua_addhook(cs2d_server_t *sv, const char *hook_name, const char *func_name, int priority);
int  cs2d_sv_lua_freehook(cs2d_server_t *sv, const char *hook_name, const char *func_name);

/* Specific hook triggers */
void cs2d_sv_lua_hook_join(cs2d_server_t *sv, int id);
void cs2d_sv_lua_hook_leave(cs2d_server_t *sv, int id, const char *reason);
int  cs2d_sv_lua_hook_team(cs2d_server_t *sv, int id, int team, int look);
int  cs2d_sv_lua_hook_spawn(cs2d_server_t *sv, int id, char *out_items, size_t max_items);
void cs2d_sv_lua_hook_die(cs2d_server_t *sv, int victim, int killer, int weapon, int x, int y, int killerweapon);
void cs2d_sv_lua_hook_kill(cs2d_server_t *sv, int killer, int victim, int weapon, int x, int y, int killerweapon);
void cs2d_sv_lua_hook_menu(cs2d_server_t *sv, int id, const char *title, int button);
void cs2d_sv_lua_hook_serveraction(cs2d_server_t *sv, int id, int action);
int  cs2d_sv_lua_hook_say(cs2d_server_t *sv, int id, const char *text);
int  cs2d_sv_lua_hook_sayteam(cs2d_server_t *sv, int id, const char *text);
void cs2d_sv_lua_hook_startround(cs2d_server_t *sv, int mode);
void cs2d_sv_lua_hook_endround(cs2d_server_t *sv, int mode);
void cs2d_sv_lua_hook_attack(cs2d_server_t *sv, int id);
void cs2d_sv_lua_hook_attack2(cs2d_server_t *sv, int id, int mode);
void cs2d_sv_lua_hook_select(cs2d_server_t *sv, int id, int type, int mode);
void cs2d_sv_lua_hook_reload(cs2d_server_t *sv, int id, int mode);
int  cs2d_sv_lua_hook_drop(cs2d_server_t *sv, int id, int itemid, int type, int ammo, int ammoin);
int  cs2d_sv_lua_hook_collect(cs2d_server_t *sv, int id, int itemid, int type, int ammo, int ammoin);
int  cs2d_sv_lua_hook_buy(cs2d_server_t *sv, int id, int type);
int  cs2d_sv_lua_hook_hit(cs2d_server_t *sv, int victim, int source, int weapon, int hpdmg, int apdmg, int rawdmg);
int  cs2d_sv_lua_hook_rcon(cs2d_server_t *sv, int id, const char *ip, int port, const char *password, const char *cmd);
int  cs2d_sv_lua_hook_name(cs2d_server_t *sv, int id, const char *oldname, const char *newname, bool forced);
int  cs2d_sv_lua_hook_parse(cs2d_server_t *sv, const char *cmd);

/* Menu helper */
void cs2d_sv_menu_send(cs2d_server_t *sv, int id, const char *menu_str);

/* HUD text packet helpers */
void cs2d_sv_hudtxt(cs2d_server_t *sv, int target_pid, int id, const char *text, int x, int y, int align, int valign, int size);
void cs2d_sv_hudtxt_clear(cs2d_server_t *sv, int target_pid, int id);
void cs2d_sv_hudtxt_alphafade(cs2d_server_t *sv, int target_pid, int id, int duration, float alpha);
void cs2d_sv_hudtxt_colorfade(cs2d_server_t *sv, int target_pid, int id, int duration, int r, int g, int b);
void cs2d_sv_hudtxt_move(cs2d_server_t *sv, int target_pid, int id, int duration, int x, int y);

/* Image helper functions */
int  cs2d_sv_image_create(cs2d_server_t *sv, const char *path, float x, float y, int mode, int player);
void cs2d_sv_image_free(cs2d_server_t *sv, int id);
void cs2d_sv_image_pos(cs2d_server_t *sv, int id, float x, float y, float rot);
void cs2d_sv_image_alpha(cs2d_server_t *sv, int id, float alpha);
void cs2d_sv_image_color(cs2d_server_t *sv, int id, int r, int g, int b);
void cs2d_sv_image_scale(cs2d_server_t *sv, int id, float sx, float sy);
void cs2d_sv_image_blend(cs2d_server_t *sv, int id, int blend);
void cs2d_sv_image_frame(cs2d_server_t *sv, int id, int frame);
void cs2d_sv_image_shadow(cs2d_server_t *sv, int id, int shadow);
void cs2d_sv_image_hitzone(cs2d_server_t *sv, int id, int mode, int xo, int yo, int w, int h);
void cs2d_sv_tween_alpha(cs2d_server_t *sv, int id, int duration, float alpha);
void cs2d_sv_tween_color(cs2d_server_t *sv, int id, int duration, int r, int g, int b);
void cs2d_sv_tween_move(cs2d_server_t *sv, int id, int duration, float x, float y);
void cs2d_sv_tween_rotate(cs2d_server_t *sv, int id, int duration, float rot);
void cs2d_sv_tween_rotateconstantly(cs2d_server_t *sv, int id, float speed);
void cs2d_sv_tween_scale(cs2d_server_t *sv, int id, int duration, float sx, float sy);
void cs2d_sv_tween_frame(cs2d_server_t *sv, int id, int duration, int frame);
void cs2d_sv_tween_animate(cs2d_server_t *sv, int id, int duration, int frame, int mode);

#endif /* CS2D_SV_LUA_H */

