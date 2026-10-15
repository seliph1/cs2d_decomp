# Work log (append-only, newest first)

Add one entry per session: `## YYYY-MM-DD — <agent> — <topic>`, followed by a few bullets covering:

- what changed;
- how it was verified;
- what is left (move it to `OPEN_ISSUES.md`).

Keep entries short; the details go in `KNOWLEDGE.md` or in code comments.

Only the 5 newest entries live here; when adding one, move the oldest to the top of `worklog/archive.md` (do not read the archive in full; grep it).

## 2026-10-15 — Claude — join transfer: HTTP downloads, transfer cache read-back
- `Thttp` ported into `cs2d_transfer.c` (addjob / get / reset, size limits `DAT_140bc89e0`, `[252,50,2]` 0xffff
  terminator, `[252,51,10]` when done, HTTP window layout); `cs2d_platform_tcp_connect` added. `[252,50,3]` now fills
  a global path → SHA cache (upper-case hex, `FUN_1400a1750`) that sprite and sound loading resolve (`Tres.add`).
  `[252,4,11]` dialog was already implemented (OPEN_ISSUES entry was stale).
- Verified: `build.bat` clean, `test_cs2d.exe` 61 / 61 (transfer test: HTTP jobs, terminator, limits, cache).
  No live HTTP capture yet.
- Left (OPEN_ISSUES "Join file transfer"): live HTTP check, assumed string tails, cache clear point.

## 2026-10-15 — Claude — every game key / button goes through sys/controls.cfg
- Movement, speed, reload, drop, use, specplayer / specmode, say / sayteam and mouse1 / mouse2 (attack / attack2)
  were hardcoded in `game_handle_key` / `game_handle_mouse`; now `game_bound_cmd` dispatches the bound command
  (held commands follow press and release). Key names `uparrow`.., `leftalt`, `enter`, `mouse1..3` from the
  CS2D.exe strings; the middle button reaches the game; `cs2d_binds_defaults` = the whole shipped controls.cfg.
- Verified: `build.bat` clean, `test_cs2d.exe` 61 / 61; `test_weapon_select` gains unbind / rebind / mouse3 cases.
- Left (OPEN_ISSUES "Weapon selection HUD"): kp_* / right-side modifier names, unimplemented bound commands.

## 2026-10-15 — Cline — bug: a weapon dropped on a public server vanished at once (client-side fade)
- Symptom (user, playing on 140.238.183.194:36963): dropping a weapon made it invisible immediately.
- Cause: the earlier session took the drop timer from `Titem.update` but missed **two gates** of the same block in
  `Titem.draw` (the tail of both 0x140147b80 and 0x1401483a0): `DAT_140ddf540`, the main loop's 1000 ms timer (1 for
  the single frame that closes a second, 140230000.c:6800) and `DAT_140ddf400`, which is 1 only while this instance
  runs the server (create server 1401e0000.c:1499, cleared on join 1401d0000.c:128 / 1402c0000.c:6747). The timer
  therefore counts **seconds** (0x140a14d94 = 15 -> 15 s), not frames, and a pure network client never runs it.
  We aged one step per *frame* at up to 167 FPS (no frame cap, OPEN_ISSUES), so the item faded in ~90 ms and our
  `[40]` then told the server to delete the weapon for everyone.
- Changed: new `cs2d_ground_item_step` (`cs2d_client.c`, exported for the test) ports the original condition;
  `cs2d_client_update` drives it from a 1 s accumulator (`cs2d_client_t.item_tick` = DAT_140ddf540) with
  server_instance = false (we are always a network client). New incoming `[40, id u16]` branch removes the item when
  the server's own fade reports it; before, that unknown opcode cut the rest of the datagram
  (`knowledge/net-messages.md`, `cs2d_types.h`).
- Verified: `build.bat` clean, `test_cs2d.exe` **61 / 61**; `test_random_and_dropwep` locks the rule (600 client
  ticks do not age an item, off-tick frames do not, the 15th second expires it, spawned items / the Lua veto 10000 /
  dead slots stay).
- Left (OPEN_ISSUES): a proxy capture on a public dedicated to confirm the server broadcasts `[40]` ~15 s after a
  drop (and that our client then removes the item); the third gate `0x140a14e48[..] & 0xfb` stays unidentified.


## 2026-10-15 — Cline — OPEN_ISSUES curation: removed the entries that were already done
- Re-read the whole list against the modules each bullet names and deleted the elements that were 100% resolved,
  keeping every still-open item: the "Join messages (fixed 2026-10-15)" bullet (the handshake lives in KNOWLEDGE
  "Network: join" and in WORKLOG), the Phase 1 image-decoding bullet, the Phase 2 "original plan", and the
  phase-3 / pixel-comparison completion narration. The section is now "GL 1.x / GDI+ removal (done ...)" plus its
  open decisions.
- Trimmed the resolved clauses that sat inside still-open bullets (dated "done"/"fixed"/"now" sentences):
  hostages-bomb "done: use ray ...", the Gen_* light sprites, the weapon-select grenade counts, the env-object
  `test_file_transfer` note, dynobj team-menu x / turret tracer / NPC beams / debris-in-wall / hostage hover /
  `g_prop_class`, the light-engine "Done 2026-10-15:" list, and the settile cut `[252,7,9]` abort (only its
  remaining socket difference stays listed).
- Docs only, no behaviour change: no build/test run needed. Verified the file reads top to bottom and every
  remaining bullet names an open gap; also fixed two stale notes (`favorites.lst` writer exists via the menu
  Favorite row, and the Node suite count).
- Left (OPEN_ISSUES): the rest of the list is genuinely open - network/special weapons vs a proxy capture, the
  audio gaps, the radars/scoreboard/buy-menu details, the map editor, the dynamic objects and the verification
  debt (daylight pixel diff, `light`/`mapshadows` sweep, the `!mouse` UI click, the two-client session).

## 2026-10-15 — Cline — muzzle offsets + the weapon-select ammo line
- `cs2d_weapon_def_t.muzzle` (`cs2d_types.h`) is filled in `cs2d_weapons_init` (`cs2d_weapons.c`) from the per-weapon
  table `0x1409f5d20` of `init_weapons` (FUN_140230330): 38 weapons, e.g. AK-47 26, AWP 33, pistols 19, M3 / XM1014
  28, melee 25, M134 32, FN F2000 23 (items, grenades and the Tactical Shield stay 0). The shot in
  `cs2d_scene_game.c` now starts the tracer, muzzle flash and its light at `pos + muzzle * (sin, -cos)` instead of
  the calibrated 30 / 22 px per slot.
- New `cs2d_render_weapon_select_ammo` (`cs2d_render.c/.h`): the weapon-select row's second line, now including the
  original's slot-6 fallback `"0 x - Remote"` (literal "0 x - " + sys/language 0763) when the item has no count;
  `"clip | reserve"` / `"clip | INF"` (0762, mp_infammo) and `"n x"` were already drawn.
- Verified: `test_weapon_select` asserts 13 muzzle values and the seven ammo-line cases (including the empty melee /
  shield rows), suite **60/60**; an offline capture firing the AK (`map prison_funst3r`, `equip 30`, `!mouse` down/up)
  exits 0; `build.bat` clean; the doc values match `knowledge/shot-effects.md`.
- Left (OPEN_ISSUES): the "Muzzle offsets" and weapon-HUD-string items are gone; the rest of the weapon selection HUD
  (grenade counts vs a screenshot, the Medikit clip 5, the HUD sound, `lastinv`, the gamepad path and the bindings)
  stays open.

