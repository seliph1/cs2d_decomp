# Work log (append-only, newest first)

Add one entry per session: `## YYYY-MM-DD — <agent> — <topic>`, followed by a few bullets covering:

- what changed;
- how it was verified;
- what is left (move it to `OPEN_ISSUES.md`).

Keep entries short; the details go in `KNOWLEDGE.md` or in code comments.

Only the 5 newest entries live here; when adding one, move the oldest to the top of `worklog/archive.md` (do not read the archive in full; grep it).

## 2026-10-15 — Antigravity — dedicated server phase 2 (part 3): damage [17], kill [19], buy [23]
- Implemented damage calculation and absorption (`cs2d_sv_damage`, `FUN_1401658c0`) deducting kevlar (~35%) and broadcasting reliable `[17, victim, attacker, hp, arm]`.
- Implemented kill logic (`cs2d_sv_kill`, `FUN_1401647d0`): sets `alive = false`, updates victim deaths, killer scores (+1, +$300 on enemy kill, friendly fire / suicide penalties), assist tracking, and broadcasts `[19, victim, killer, assist, weapon, x, y]`.
- Implemented buy handler (`cs2d_sv_buy`, `FUN_1401cd710`): price and money verification, money deduction, inventory addition (`sv_give`), special items / equipment handling (defuse kit, kevlar, helmet, nightvision, gas mask), and response `[23, id, type, money u16, 0]`. Broadcasts `[9]` weapon switch on weapon purchases.
- Integrated opcode handlers for `17` (hit report), `19` (suicide / kill), and `23` (buy request) in `sv_recv_game` in `src/cs2d_server.c`.
- Verified: `build.bat` compiled all 3 binaries, `test_cs2d.exe` **62 / 62** passing all damage, kill, score and buy assertions.
- Left: next Phase 2 items: drop / collect `[24]`/`[25]`, reload `[16]`, chat `[240]`, spectate `[67]`/`[32]`.

## 2026-10-15 — Antigravity — dedicated server phase 2 (part 2): rounds, leave, weapon selection [9], fire [7]/[8]
- Implemented round end (`cs2d_sv_round_end`, `FUN_14015c840`), round start (`cs2d_sv_round_start`, `FUN_1402520b0` / `FUN_140123530`), and per-frame round progression (`cs2d_sv_step_round`, `FUN_14015ce30`).
- Added "Game commencing" trigger in `cs2d_sv_team` (`FUN_1401624c0` LAB_14015cff0: mode 4, 3000 ms delay).
- Broadcasts opcode 226 mode 1 (`[226, 1, winner, mvp, reason_lstr, mode_byte]`) on round end, and opcode 22 (`[22, reason, count, ...]` + inventory) on round start.
- Handled opcode 253 leave reason and optional reason string (`FUN_1401e3790`).
- Implemented weapon selection `[9]` (`cs2d_sv_select_weapon`, `FUN_140158060`), primary fire relay `[7]` (`cs2d_sv_fire`, `FUN_140191a70`), and secondary attack relay `[8]` (`cs2d_sv_attack2`, `FUN_1401fd590`).
- Verified: `build.bat` clean, `test_cs2d.exe` **62 / 62** (passed dedicated server round end / start tests and combat relay test).
- Left: next Phase 2 items: damage / kill `[17]`/`[19]`, buy `[23]`, drop / collect `[24]`/`[25]`, reload `[16]`, chat `[240]`.

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
