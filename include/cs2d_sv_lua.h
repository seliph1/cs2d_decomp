#ifndef CS2D_SV_LUA_H
#define CS2D_SV_LUA_H

#include "cs2d_server.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#define CS2D_SV_MAX_HOOKS       128
#define CS2D_SV_MAX_TIMERS      256
#define CS2D_SV_MAX_HTTP        8

/* Hook listener entry (ordered by priority ascending) */
typedef struct cs2d_hook_node {
    char func[64];
    int priority;
    struct cs2d_hook_node *next;
} cs2d_hook_node_t;

/* Hook definition */
typedef struct {
    char name[32];
    bool enabled;
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

/* Pending reqhttp() request (HTTP GET, non-blocking reads in cs2d_sv_lua_frame) */
typedef struct {
    bool     active;
    bool     failed;
    bool     headers_done;
    int      request_id;
    SOCKET   sock;
    int      mode;          /* 0 = string, 1 = byte table */
    uint32_t start_ms;
    char    *buf;
    int      len, cap;
    int      body_off;      /* offset after "\r\n\r\n" */
} cs2d_sv_http_t;

/* Lua subsystem state */
typedef struct cs2d_sv_lua {
    lua_State        *L;
    cs2d_server_t    *sv;
    cs2d_hook_t       hooks[CS2D_SV_MAX_HOOKS];
    int               hook_count;
    cs2d_sv_timer_t   timers[CS2D_SV_MAX_TIMERS];
    uint32_t          last_ms100;
    uint32_t          last_second;
    uint32_t          last_minute;
    char              bind_keys[64][32];
    int               bind_key_count;
    cs2d_sv_http_t    http[CS2D_SV_MAX_HTTP];
    int               next_http_id;
} cs2d_sv_lua_t;

/* Lifetime */
bool cs2d_sv_lua_init(cs2d_server_t *sv);
void cs2d_sv_lua_close(cs2d_server_t *sv);
void cs2d_sv_lua_frame(cs2d_server_t *sv);
void cs2d_sv_lua_step_timers(cs2d_server_t *sv);
void cs2d_sv_lua_log(cs2d_server_t *sv, const char *msg);

/* Script runners */
bool cs2d_sv_lua_dofile(cs2d_server_t *sv, const char *path);
bool cs2d_sv_lua_dostring(cs2d_server_t *sv, const char *code);
/* Trigger_If condition type 0: runs "_cs2d_trigger_if=" + code and returns the result. */
bool cs2d_sv_lua_trigger_if(cs2d_server_t *sv, const char *code, bool *result);
void cs2d_sv_lua_load_server(cs2d_server_t *sv);
void cs2d_sv_lua_load_autorun(cs2d_server_t *sv);
void cs2d_sv_lua_load_map(cs2d_server_t *sv, const char *mapname);

/* Hook dispatcher */
int  cs2d_sv_lua_addhook(cs2d_server_t *sv, const char *hook_name, const char *func_name, int priority);
int  cs2d_sv_lua_freehook(cs2d_server_t *sv, const char *hook_name, const char *func_name);
int  cs2d_sv_lua_sethookstate(cs2d_server_t *sv, const char *hook_name, int state);

/* Specific hook triggers */
void cs2d_sv_lua_hook_join(cs2d_server_t *sv, int id);
void cs2d_sv_lua_hook_leave(cs2d_server_t *sv, int id, int reason); /* reason id, not text (help.php; proxy 2026-10-24) */
int  cs2d_sv_lua_hook_team(cs2d_server_t *sv, int id, int team, int look);
int  cs2d_sv_lua_hook_spawn(cs2d_server_t *sv, int id, char *out_items, size_t max_items);
void cs2d_sv_lua_hook_die(cs2d_server_t *sv, int victim, int killer, int weapon, int x, int y, int killerobject);
void cs2d_sv_lua_hook_kill(cs2d_server_t *sv, int killer, int victim, int weapon, int x, int y, int killerobject,
                           int assistant);
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
int  cs2d_sv_lua_hook_drop(cs2d_server_t *sv, int id, int itemid, int type, int ammoin, int ammo,
                           int mode, int xpos, int ypos);
int  cs2d_sv_lua_hook_collect(cs2d_server_t *sv, int id, int itemid, int type, int ammoin, int ammo,
                              int mode);
int  cs2d_sv_lua_hook_buy(cs2d_server_t *sv, int id, int type);
int  cs2d_sv_lua_hook_hit(cs2d_server_t *sv, int victim, int source, int weapon, int hpdmg, int apdmg, int rawdmg,
                          int object);
int  cs2d_sv_lua_hook_rcon(cs2d_server_t *sv, int id, const char *ip, int port, const char *password, const char *cmd);
int  cs2d_sv_lua_hook_name(cs2d_server_t *sv, int id, const char *oldname, const char *newname, bool forced);
int  cs2d_sv_lua_hook_parse(cs2d_server_t *sv, const char *cmd);
void cs2d_sv_lua_hook_key(cs2d_server_t *sv, int id, const char *key, int state);
void cs2d_sv_lua_hook_bombplant(cs2d_server_t *sv, int id, int x, int y);
void cs2d_sv_lua_hook_bombdefuse(cs2d_server_t *sv, int id);
void cs2d_sv_lua_hook_bombexplode(cs2d_server_t *sv, int id, int x, int y);
void cs2d_sv_lua_hook_use(cs2d_server_t *sv, int id, int event, int data, int x, int y);
void cs2d_sv_lua_hook_hostageuse(cs2d_server_t *sv, int id, int hostage_id, int mode);
void cs2d_sv_lua_hook_hostagedamage(cs2d_server_t *sv, int id, int hostage_id, int damage);
void cs2d_sv_lua_hook_hostagekill(cs2d_server_t *sv, int id, int hostage_id, int damage);
void cs2d_sv_lua_hook_hostagerescue(cs2d_server_t *sv, int id, int x, int y);
void cs2d_sv_lua_hook_flashlight(cs2d_server_t *sv, int id, int state);
void cs2d_sv_lua_hook_projectile(cs2d_server_t *sv, int id, int type, int x, int y, int projectileid);
void cs2d_sv_lua_hook_projectile_impact(cs2d_server_t *sv, int id, int type, int x, int y, int mode, int projectileid);
void cs2d_sv_lua_hook_break(cs2d_server_t *sv, int x, int y, int player);
void cs2d_sv_lua_hook_mapchange(cs2d_server_t *sv, const char *newmap);
void cs2d_sv_lua_hook_spray(cs2d_server_t *sv, int id);
void cs2d_sv_lua_hook_radio(cs2d_server_t *sv, int id, int msg);
void cs2d_sv_lua_hook_startround_prespawn(cs2d_server_t *sv, int mode);
int cs2d_sv_lua_hook_assist(cs2d_server_t *sv, int assist, int victim, int killer);
void cs2d_sv_lua_hook_suicide(cs2d_server_t *sv, int id);
void cs2d_sv_lua_hook_specswitch(cs2d_server_t *sv, int id, int target);
void cs2d_sv_lua_hook_voice(cs2d_server_t *sv, int id);
void cs2d_sv_lua_hook_vote(cs2d_server_t *sv, int id, int mode, const char *param);
int cs2d_sv_lua_hook_walkover(cs2d_server_t *sv, int id, int iid, int type, int ain, int a, int mode);
void cs2d_sv_lua_hook_usebutton(cs2d_server_t *sv, int id, int x, int y);
int cs2d_sv_lua_hook_trigger(cs2d_server_t *sv, const char *trigger, int source);
int cs2d_sv_lua_hook_triggerentity(cs2d_server_t *sv, int x, int y);
void cs2d_sv_lua_hook_vipescape(cs2d_server_t *sv, int id, int x, int y);
void cs2d_sv_lua_hook_dominate(cs2d_server_t *sv, int id, int team, int x, int y);
void cs2d_sv_lua_hook_flagtake(cs2d_server_t *sv, int id, int team, int x, int y);
void cs2d_sv_lua_hook_flagcapture(cs2d_server_t *sv, int id, int team, int x, int y);
/* "build": the object was placed. Returns 1 when a Lua hook cancelled it (the original reads an int result). */
int  cs2d_sv_lua_hook_build(cs2d_server_t *sv, int id, int type, int x, int y, int mode, int objectid);
/* "buildattempt": the build request passed the checks. 1 cancels it. */
int  cs2d_sv_lua_hook_buildattempt(cs2d_server_t *sv, int id, int type, int x, int y, int mode);
/* "objectdamage": an object is about to take damage. 1 cancels it (Tdo.damage 0x140035207 reads the int). */
int  cs2d_sv_lua_hook_objectdamage(cs2d_server_t *sv, int id, int damage, int player);
void cs2d_sv_lua_hook_objectkill(cs2d_server_t *sv, int id, int player);
int  cs2d_sv_lua_hook_objectupgrade(cs2d_server_t *sv, int id, int player, int progress, int total);
void cs2d_sv_lua_hook_move(cs2d_server_t *sv, int id, int x, int y, int walk);
void cs2d_sv_lua_hook_movetile(cs2d_server_t *sv, int id, int x, int y);
void cs2d_sv_lua_hook_clientdata(cs2d_server_t *sv, int id, int mode, int data1, int data2, const char *sdata);
void cs2d_sv_lua_hook_clientsetting(cs2d_server_t *sv, int id, int setting, int val1, int val2);
void cs2d_sv_lua_hook_itemfadeout(cs2d_server_t *sv, int iid, int type, int x, int y);
void cs2d_sv_lua_hook_shutdown(cs2d_server_t *sv);
/* connect(id): help.php lists a single player id (the old ip/port/usgn/name signature was wrong).
 * It runs after connect_initplayer and before join. */
int  cs2d_sv_lua_hook_connect(cs2d_server_t *sv, int id);
/*
 * connect_attempt(name, ip, port, usgnID, usgnName, steamID, steamName), the 7 arguments of
 * help.php. steamID is a string there and the original pushes it as one (cs2d_dedicated.exe
 * 0x1401dea4a, bbStringFromLong).
 *
 * Returns 1 and fills out_reason with the value the script returned when that value is not
 * empty: the caller then cancels the join with status 4 (ban) and the string as ban reason.
 * Returns 0 when no script or every script returned "".
 */
int  cs2d_sv_lua_hook_connect_attempt(cs2d_server_t *sv, const char *name, const char *ip, int port,
                                      int usgn, const char *usgn_name, int64_t steam_id,
                                      const char *steam_name, char *out_reason, size_t max_reason);
void cs2d_sv_lua_hook_connect_initplayer(cs2d_server_t *sv, int id);
/* disconnect(id, reason id): runs after the player is fully removed (player(id) no longer works).
 * reason is the numeric reason of the [253] message: 0 = normal, 1 = timeout, 2/16 = kick/ban. */
void cs2d_sv_lua_hook_disconnect(cs2d_server_t *sv, int id, int reason);
/* "hitzone" (imagehitzone / [59]): (image id, player id, object id, weapon, impact x, impact y, damage).
 * Returns 0 (keep the damage), > 0 (new damage) or < 0 (set the damage to 0). */
int  cs2d_sv_lua_hook_hitzone(cs2d_server_t *sv, int image_id, int player, int object, int weapon,
                              float x, float y, int damage);
void cs2d_sv_lua_hook_httpdata(cs2d_server_t *sv, int request_id, int state, const char *data, size_t len);
int  cs2d_sv_lua_hook_sayutf8(cs2d_server_t *sv, int id, const char *text);
int  cs2d_sv_lua_hook_sayteamutf8(cs2d_server_t *sv, int id, const char *text);
void cs2d_sv_lua_hook_shieldhit(cs2d_server_t *sv, int id, int source, int weapon, int direction, int object_id,
                                int damage);
/* "turretscan" (FUN_14028c630 slot +0x318): a turret is about to look for a target. The 4 arguments are
 * its object id, its team, x + 16 and y + 16; a returned player id (> 0) becomes the target directly. */
int  cs2d_sv_lua_hook_turretscan(cs2d_server_t *sv, int object_id, int team, int x, int y);

/* Menu helper */
void cs2d_sv_menu_send(cs2d_server_t *sv, int id, const char *menu_str);

/* Bot AI callbacks (bots/Standard AI.lua). The AI module calls these; they are no-ops without Lua.
 * `a` is the bot id for ai_onspawn/ai_update_*, the radio/chat source for ai_hear_*. */
bool cs2d_sv_lua_call_ai(cs2d_server_t *sv, const char *func, int a);
bool cs2d_sv_lua_call_ai_radio(cs2d_server_t *sv, const char *func, int a, int b);
bool cs2d_sv_lua_call_ai_chat(cs2d_server_t *sv, const char *func, int a, const char *text, int c);

/* HUD text packet helpers */
void cs2d_sv_hudtxt(cs2d_server_t *sv, int target_pid, int id, const char *text, int x, int y, int align, int valign, int size);
void cs2d_sv_hudtxt_clear(cs2d_server_t *sv, int target_pid, int id);
void cs2d_sv_hudtxt_alphafade(cs2d_server_t *sv, int target_pid, int id, int duration, float alpha);
void cs2d_sv_hudtxt_colorfade(cs2d_server_t *sv, int target_pid, int id, int duration, int r, int g, int b);
void cs2d_sv_hudtxt_move(cs2d_server_t *sv, int target_pid, int id, int duration, int x, int y);
bool cs2d_sv_lua_parse_hudtxt(cs2d_server_t *sv, const char *cmd);

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
void cs2d_sv_image_physicsparent(cs2d_server_t *sv, int id, int parent_type, int parent_id, int mode);
void cs2d_sv_image_hitzone(cs2d_server_t *sv, int id, int mode, int xo, int yo, int w, int h);
void cs2d_sv_tween_alpha(cs2d_server_t *sv, int id, int duration, float alpha);
void cs2d_sv_tween_color(cs2d_server_t *sv, int id, int duration, int r, int g, int b);
void cs2d_sv_tween_move(cs2d_server_t *sv, int id, int duration, float x, float y, float rot);
void cs2d_sv_tween_rotate(cs2d_server_t *sv, int id, int duration, float rot);
void cs2d_sv_tween_rotateconstantly(cs2d_server_t *sv, int id, float speed);
void cs2d_sv_tween_scale(cs2d_server_t *sv, int id, int duration, float sx, float sy);
void cs2d_sv_tween_frame(cs2d_server_t *sv, int id, int duration, int frame);
void cs2d_sv_tween_animate(cs2d_server_t *sv, int id, int speed, int mode);

#endif /* CS2D_SV_LUA_H */

