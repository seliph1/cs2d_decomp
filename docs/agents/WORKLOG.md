# Work log (append-only, newest first)

Add one entry per session: `## YYYY-MM-DD — <agent> — <topic>`, followed by a few bullets covering:

- what changed;
- how it was verified;
- what is left (move it to `OPEN_ISSUES.md`).

Keep entries short; the details go in `KNOWLEDGE.md` or in code comments.

Only the 5 newest entries live here; when adding one, move the oldest to the top of `worklog/archive.md` (do not read the archive in full; grep it).

## 2026-10-14 — Claude — Game sounds (cs2d_sound)

- New `src/cs2d_sound.c`: SDL3 audio stream mixer, WAV (SDL_LoadWAV) + OGG (vendored `third_party/stb_vorbis.c`),
  port of `cs2d_sound_play_at` 0x1401191c0 / FUN_140109d30 (gain, pan, 20 per frame), TPosSound tracking.
- Hooks: weapon fire (+ silenced), wall ricochet, material hit / break, building hit, explosions, hit / die, item
  pickup / drop, build, [65], Env_Sound, thunder, flashbang, hostage, HUD slot keys. Config `mapsounds`, `soundmode`.
- Verified: `test_sound` (formula, table); client starts with audio. Not compared by ear with the original.
- Left: see OPEN_ISSUES "Audio".

## 2026-10-14 — Claude — spawnobject buildings missing ([65] positional sound)
- Changed: client skips `[65]` positional sound (`FUN_1402de5f0`); before, the unknown opcode cut the datagram and dropped the
  `[49]` that follows it. New console `objlist`; `udp_proxy.py` survives WinError 10054; probes `spawnobj_probe.lua`,
  `buildattempt_probe.lua` (buildattempt -> sv_soundpos + spawnobject).
- Verified: proxy on 157.90.170.75:10003 (`[65]` + `[49]` in one datagram); local buildattempt probe -> `objlist` shows it.
- Left: no audio in the client, so `[65]` plays nothing.

## 2026-10-14 — Claude — Hostages, bomb plant / defuse, use key targets
- New `cs2d_hostage.c` (Thostage list: round start / snapshot [252,7,2], Thostage.use, [29], client movement, drawn from the
  list); E probes hostage / planted bomb / Trigger_Use per distance like FUN_14014f820; handlers for the [26] echo, [29],
  [30]; plant ([30,1]/[30,2]) and defuse (mode 3, [30,4] on release) online; [22] special byte (bomb / kit / flag);
  item 63 renamed Planted Bomb.
- Verified on the original ded: `hostage_probe.lua` (cs_office: [26,1] echo, [29] targets, hostage stops at 172,176 like
  the server) and `bomb_probe.lua` (de_dust2: our [30,1] → bombplant 28 22, CT E → use 3 / bombdefuse after 10 s).
  Test `test_hostages`. Left: OPEN_ISSUES "Hostages / bomb".

## 2026-10-14 — Claude — Use key [26] always sent (Lua "use" hook)
- E now sends `[26][0][0 i32][0 u16][0 u16]` when no Trigger_Use is in reach, like FUN_14014f820 (the original sends
  the packet whatever it finds); the server runs the Lua `use` hook with event 0. Trigger_Use still sends mode 100.
- Verified: `tools/agent/capture_ours.sh use_probe.lua` (new probe) logs `US 1 0 0 0 0` per press on the original ded.
- Left: hostage (modes 1/2), bomb defuse (3) and own dispenser/teleporter menu (Tdo 9/15) targets (OPEN_ISSUES).

## 2026-10-14 — Claude — Ingame menu (Esc): briefing, team, leaderboard, vote, options, server info

- Esc in game opens `Tgui_hudmen` menu 2 (was: disconnect). Buttons and the server info box from `Tgui_hudmen.draw`;
  `[252,6]` tail parsed (FF, voice, recoil, off-screen, physics, Lua, OS, lag comp, leaderboard flag).
- Map Briefing (`maps/<map>.txt`, Server Info window generalized), leaderboard `[230]`, vote screen + `[43]`, map cycle
  `[252,7,9]`, votekick/votemap from snapshot. Facts in `knowledge/game-menu.md`.
- Verified: build, `test_game_menu`; capture against the offline dedicated (screens: menu, vote, briefing, leaderboard;
  `[230,0]` answered, `[43,1,1]` self-vote refused with 0xfff4). Harness step `!esc` added.
- Light engine test still fails (unrelated, pre-existing).

