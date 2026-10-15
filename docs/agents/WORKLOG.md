# Work log (append-only, newest first)

Add one entry per session: `## YYYY-MM-DD — <agent> — <topic>`, followed by a few bullets covering:

- what changed;
- how it was verified;
- what is left (move it to `OPEN_ISSUES.md`).

Keep entries short; the details go in `KNOWLEDGE.md` or in code comments.

Only the 5 newest entries live here; when adding one, move the oldest to the top of `worklog/archive.md` (do not read the archive in full; grep it).

## 2026-10-15 — Claude — dedicated server phase 2 (part 1): team, spawn, movement relay
- Receive dispatch found (`FUN_1401e3cf0`, jump table `0x1406cdb14`, handler per opcode in
  `knowledge/dedicated-server.md`); new `src/cs2d_sv_game.c` ports team `FUN_1401624c0` (main path), spawn
  `FUN_140160740` (spawn point, mode item tables, best weapon, money, `[21]`), the per-frame relay
  `FUN_1401a68e0`; `[10]/[11]/[70]` input, `[248]` join notice. `tools/agent/reasave.py` saves REA pseudo-C with
  strings and Tudp calls annotated (`docs/dump/dedicated/`).
- Verified: original CS2D.exe picks CT on our server and gets `[20]` + `[21]` byte-equal to the original server's;
  with our client as a second player the original sees its `[248]`, `[20]`, `[21]`, `[10]` relay and `[253]`.
  `test_server_join` gains the spawn case: 62 / 62.
- Left: OPEN_ISSUES "Dedicated server" phase 2 list (rounds, shooting, damage, buy...).

## 2026-10-15 — Claude — dedicated server phase 1: transport + join (cs2d_server.exe)
- New `src/cs2d_server.c` / `include/cs2d_server.h` / `src/main_dedicated.c` (third gcc line in `build.bat`):
  Tudp (reliable ids, ACK triples, resend), connections, recv 252 `FUN_1401dc530` ([252,0] secret, [252,1] checks
  and status codes, [252,3] secret / sha check, [252,5] -> [252,6] + snapshot 1..13 + 200, `[28,0,1]`, [240]
  serverinfo), `[20]` team echo, the per-second [229] / [247] block (0x1401e505d), name sanitizer `FUN_14010d970`.
- Ghidra pseudo-C of `FUN_1401dc530` saved in `docs/dump/dedicated/` (git-ignored); new `tools/agent/bbfind.py`
  (text -> string object), `tools/agent/capture_server.sh` (original CS2D.exe against our server).
- Verified: original CS2D.exe joins `cs2d_server.exe -offline -map de_dust2` and picks a team; every server
  message of the join equals `cs2d_dedicated.exe`'s (proxy diff) except the random secret / round time and the
  missing `[21]` spawn. `test_server_join` locks [252,6] (105 bytes), the [252,7,1] record and the sanitizer: 62 / 62.
- Left: OPEN_ISSUES "Dedicated server" (phase 2 = spawn / movement / rounds, then console, Lua, systems).

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
