# Work log (append-only, newest first)

Add one entry per session: `## YYYY-MM-DD — <agent> — <topic>`, followed by a few bullets covering:

- what changed;
- how it was verified;
- what is left (move it to `OPEN_ISSUES.md`).

Keep entries short; the details go in `KNOWLEDGE.md` or in code comments.

Only the 5 newest entries live here; when adding one, move the oldest to the top of `worklog/archive.md` (do not read the archive in full; grep it).

## 2026-10-14 — Claude — Batched outlines, UI child windows
- **Changed:** `gl3_push` (cs2d_gl.c) flushes before LINE_LOOP / STRIP / FAN pushes (two outlines in one draw were
  joined into a single loop: Color window sliders linked to the preview box). UI: `cs2d_ui_window_begin_child`
  (parent id, persistent window registry) closes a child when its parent closes; the editor Color window opened from
  Entity Properties uses it.
- **Verified:** build, test_ui_layout (41 / 41). Not run in the client.

## 2026-10-14 — Claude — Editor entity helpers
- **Changed:** zones, Env_Light radius, LightStripe, Teleport line and trigger links are drawn only on hover / Space
  (Te.drawineditor + Ted_tr.drawat).

## 2026-10-14 — Claude — Info_Storm wind and Info_Quake screen shake
- **Changed:** `cs2d_gen.c`: wind from Te.setup case 9 added to Tp 3 / 14; `gen.shake` (Info_Quake, cs2d_draw_world
  0x140290310) + `cs2d_gen_shake_offset`, applied to the camera for the world draw in `game_render`.
- **Verified:** dump only (Te.c, Tp.c, 140290000.c); tests `test_gen_entities` (Storm, Quake). Not screenshot-compared.
- **Left:** shake from explosions / Lua (OPEN_ISSUES Gen_* entry).
- **Culling:** Env_Object's uncited `size*70+32` margin replaced by the drawn size (`env_object_learn` in the palm /
  tree / alarm / street light / sink draws); Env_Image already learned its size. test_entity_culling: 40/40.
- **Info_TileFX:** new `cs2d_tilefx.c` (CreateTileFX scroll / noise / wave), atlas + draw in `cs2d_render_map_ex`;
  `test_tilefx` (41/41); de_dust (75, 65) wave tile animates in `--capture`. Gaps in OPEN_ISSUES.

## 2026-10-14 — Claude — Editor Entity Properties, editor and spectator minimap
- **Changed:** `cs2d_ed_prop_*` (editor.c: per-type widget table of `FUN_1402aa220`, load / apply with its clamps
  and conversions), `cs2d_ed_minimap_build` (`FUN_14019d7c0`); editor scene: Entity Properties window (right click
  on an entity), rotation dial, color box -> Color window, minimap with click / drag navigation; spectator minimap
  (`FUN_140299d70`) toggled by the Map button, click moves the free-look camera. Render: `cs2d_texture_upload_rgba`,
  `cs2d_render_rect_add`.
- **Verified:** build, `test_editor_entity_props` (40 / 40); `--capture` of the editor on de_dust2 (minimap click
  moved the camera, Env_Object window, dial drag). The spectator minimap was not run against a server.
- **Left:** OPEN_ISSUES "Map editor" / "Spectator".

## 2026-10-13 — Claude — Editor Display Settings window, console over editor UI
- Client windows (console, ...) now build after the editor UI, so they draw on top of it.
- Display Settings window (`build_disp_window`), `sys/editor.cfg` load/save (`cs2d_ed_disp_*`), grid / shadows / entities gated; G toggles the grid.
- Verified: build, tests (`test_editor` cfg byte round trip). The `--capture` `!mouse` clicks did not reach the editor sidebar (Load Map did not open either), so no screenshot yet.
