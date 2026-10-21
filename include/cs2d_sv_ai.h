#ifndef CS2D_SV_AI_H
#define CS2D_SV_AI_H

#include "cs2d_server.h"

/*
 * Dedicated-server AI (bots and NPCs).
 *
 * Bot AI is not native in CS2D: the engine runs the Lua script that ships in `bots/Standard AI.lua`
 * (the client loads it at startup, CS2D.exe 0x140190000.c:2263 checks `FileType(bots/Standard AI.lua)`;
 * `bots/includes` are its helpers). The engine calls, per bot:
 *   - ai_onspawn(id)          after each spawn                       (Standard AI.lua:58)
 *   - ai_update_living(id)    every frame while the bot is alive     (Standard AI.lua:80)
 *   - ai_update_dead(id)      every second while the bot is dead     (Standard AI.lua:219)
 *   - ai_hear_radio(source, radio) for each radio message            (Standard AI.lua:230)
 *   - ai_hear_chat(source, msg, teamonly) for each chat message      (Standard AI.lua:238)
 * The `ai_*` API functions below are the engine side of that script; their semantics follow
 * docs/cs2d_help_db_complete.json (lua_commands) and the client dump
 * (cs2dlua_ai_* in docs/dump/cs2d/_global/140070000.c .. 1402a0000.c).
 */

/* Loads bots/Standard AI.lua into the server Lua state (silently succeeds when the file is absent). */
bool cs2d_sv_ai_load(cs2d_server_t *sv);

/* Per-frame engine tick: runs ai_update_living for every living bot, turns bots towards ai_aim,
 * keeps bot_count/bot_autofill filled and steps the NPC AI. Call before cs2d_sv_relay_movement. */
void cs2d_sv_ai_frame(cs2d_server_t *sv);

/* Once-per-second tick: ai_update_dead, ai_flash countdown (Tpl.c:4202). */
void cs2d_sv_ai_second(cs2d_server_t *sv);

/* ai_onspawn(id), called by cs2d_sv_spawn_at for bots. */
void cs2d_sv_ai_on_spawn(cs2d_server_t *sv, int id);

/* ai_hear_radio / ai_hear_chat for every bot (Standard AI.lua). radio = message id (1..N). */
void cs2d_sv_ai_hear_radio(cs2d_server_t *sv, int source, int radio);
void cs2d_sv_ai_hear_chat(cs2d_server_t *sv, int source, const char *msg, int teamonly);

/* Flashbang: ai_flash = Rand(4, 6) seconds for bots within 640 x 480 with a free line
 * (CS2D.exe Tpro.c:3227..3237). Called from the flashbang detonation. */
void cs2d_sv_ai_flashbang(cs2d_server_t *sv, float x, float y);

/* Returns true when the bot may move/shoot (false while bot_freeze is 1). */
bool cs2d_sv_ai_bot_active(const cs2d_server_t *sv);

/* --- engine side of the ai_* Lua API (id is always a bot id) ------------------------------- */
int  cs2d_sv_ai_move(cs2d_server_t *sv, int id, float angle, int walk);
int  cs2d_sv_ai_goto(cs2d_server_t *sv, int id, int tx, int ty, int walk);
void cs2d_sv_ai_aim(cs2d_server_t *sv, int id, float x, float y);
int  cs2d_sv_ai_findtarget(cs2d_server_t *sv, int id);
bool cs2d_sv_ai_freeline(cs2d_server_t *sv, int id, float x, float y);
void cs2d_sv_ai_rotate(cs2d_server_t *sv, int id, float rot);
void cs2d_sv_ai_attack(cs2d_server_t *sv, int id, int secondary);
void cs2d_sv_ai_iattack(cs2d_server_t *sv, int id);
void cs2d_sv_ai_reload(cs2d_server_t *sv, int id);
void cs2d_sv_ai_selectweapon(cs2d_server_t *sv, int id, int type);
void cs2d_sv_ai_buy(cs2d_server_t *sv, int id, int type);
void cs2d_sv_ai_use(cs2d_server_t *sv, int id);
void cs2d_sv_ai_respawn(cs2d_server_t *sv, int id);
void cs2d_sv_ai_drop(cs2d_server_t *sv, int id);
void cs2d_sv_ai_radio(cs2d_server_t *sv, int id, int msg);
void cs2d_sv_ai_say(cs2d_server_t *sv, int id, const char *text, int team);
void cs2d_sv_ai_spray(cs2d_server_t *sv, int id);
void cs2d_sv_ai_build(cs2d_server_t *sv, int id, int typ, int tx, int ty);
void cs2d_sv_ai_debug(cs2d_server_t *sv, int id, const char *text);

/* Server-side NPC AI (Tdo 30, Env_NPC). Exposed for the tests. */
void cs2d_sv_ai_npc_frame(cs2d_server_t *sv);

#endif /* CS2D_SV_AI_H */
