# Work log (append-only, newest first)

Add one entry per session: `## YYYY-MM-DD — <agent> — <topic>`, followed by a few bullets covering:

- what changed;
- how it was verified;
- what is left (move it to `OPEN_ISSUES.md`).

Keep entries short; the details go in `KNOWLEDGE.md` or in code comments.

Only the 5 newest entries live here; when adding one, move the oldest to the top of `worklog/archive.md` (do not read the archive in full; grep it).

## 2026-10-14 — Claude Code — World draw order (light buffer last) and Env_Sprite layer
- **Changed:** `game_render` follows `FUN_140281fd0` again: CPU shadow chunks before the players, `DrawLightsColorList`
  after them, the light buffer (day shadows of walls / buildings / DynWalls) last, over players and Tp 5/7/6. The
  player day-shadow silhouette moved out of the light buffer into `cs2d_render_player` (depth-mask pass, 4 px along
  ShadowRotation - 180, alpha 0.5; our deviation) so the player is not darkened by its own shadow.
  Env_Sprite layer: `Len(strs[4]) > 0` read per draw (the `sprite_layer` cache went stale after inspector edits).
- **Verified:** build, 43 / 43 tests. No screenshot comparison yet.

## 2026-10-14 — Claude — Radar icons (FUN_140291bd0), minimap upload
- **Changed:** new `cs2d_radar.c` (icon queue: Dom_Point, CTF bases, RadarIcon, players with the original colours, bomb,
  dropped flags); `cs2d_render_hud` draws the queue like cs2d_draw_line_3; the minimap is uploaded once per map and drawn
  as a UV disc (no per-move glTexImage2D). `cs2d_ground_item_t` moved to cs2d_types.h.
- **Verified:** test_radar_icons (43 / 43); `--capture` on ctf_fast and de_dust. Not compared with the original client.
- **Left:** OPEN_ISSUES "Radar".

## 2026-10-14 — Claude — Kill feed (Thud_msg.add_kill / draw_kill)
- **Changed:** `cs2d_client_add_kill` (texts, team colors, specials 0xf8..0xff, 5 rows) fed by `[19]` (skips weapon
  0xfa / 0xfc, assist only T vs CT, as FUN_1401eaf60); `render_kill_msgs` in cs2d_scene_game.c (icon and text modes,
  hud_shade_c bar, fade / collapse); config `deathnoticeimg`, `hud_deathnotice_time`; debug command `killmsg`.
- **Verified:** test_kill_messages (42 / 42); `--capture` screenshot on de_dust with five `killmsg` rows. Not compared
  with the original client yet.
- **Spectator time / money:** `render_spectator_time_money` (FUN_14029ac10): clock + "M:SS" and cart + money in the
  top band (the original never draws the alive HUD while dead). Screenshot checked (`team spec` on de_dust).
- **Grenade stack:** `owned[CS2D_MAX_WEAPONS]` + `cs2d_player_remove_item`; slot 4 lists HE / Flashbang / Smoke
  (test_weapon_select extended; screenshot after `equip 51/52/53`, `!press 4`).
- **Grenade counts:** Flashbang / Snowball / Mines clip from 0x140a017a8 (2 / 10 / 3); `cs2d_player_add_bought`
  ([23] +1 when owned) and `cs2d_player_add_collected` ([25] up to the max). Verified online (proxy fb: 2 Flashbangs
  bought -> 2 x), test_weapon_select, offline screenshot (2 x / 10 x).
- **Partial pickup:** `cs2d_player_add_collected` returns whether the stack was taken whole; `[25]` keeps the ground
  item otherwise (FUN_14020ce60 client path; slot 4 only). test_weapon_select; not run against the dedicated.

## 2026-10-14 — Claude — Batched outlines, UI child windows
- **Changed:** `gl3_push` (cs2d_gl.c) flushes before LINE_LOOP / STRIP / FAN pushes (two outlines in one draw were
  joined into a single loop: Color window sliders linked to the preview box). UI: `cs2d_ui_window_begin_child`
  (parent id, persistent window registry) closes a child when its parent closes; the editor Color window opened from
  Entity Properties uses it.
- **Verified:** build, test_ui_layout (41 / 41). Not run in the client.

## 2026-10-14 — Claude — Editor entity helpers
- **Changed:** zones, Env_Light radius, LightStripe, Teleport line and trigger links are drawn only on hover / Space
  (Te.drawineditor + Ted_tr.drawat).

- 2026-10-14: weather (rain/snow) drawn after light buffer; Env_Room suppresses weather spawn like Info_NoWeather (user request, not REA-verified).
- 2026-10-14 (Claude): smoke grenade cloud ported from Tp 0x1b (sgsmoke.bmp x4, dark grey) instead of white disc + puffs.
- 2026-10-14 (Claude): Gen_Weather fog spawned 60x too often; gated on the 1 s tick (DAT_140ddf540). Test added.
