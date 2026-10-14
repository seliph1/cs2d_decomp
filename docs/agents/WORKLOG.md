# Work log (append-only, newest first)

Add one entry per session: `## YYYY-MM-DD — <agent> — <topic>`, followed by a few bullets covering:

- what changed;
- how it was verified;
- what is left (move it to `OPEN_ISSUES.md`).

Keep entries short; the details go in `KNOWLEDGE.md` or in code comments.

Only the 5 newest entries live here; when adding one, move the oldest to the top of `worklog/archive.md` (do not read the archive in full; grep it).

## 2026-10-14 — Claude — attack2 (silencer, burst, scope, strong slash) and shotgun pellets
- `cs2d_weapon_attack2_type/accuracy/mode`, per-item `weapon_mode[]` (replaces unused `zoom_mode`), attack2 handler +
  `[8]` send/receive, burst queue, 5 shotgun pellets, `cs2d_fx_tracer`, `cs2d_render_scope`. Knife attack2 now sends `[8,0]`.
- Verified: dump/REA (see knowledge/shot-effects.md), `test_weapon_modes`, offline screenshots (M3 fan, AWP zoom 2).
- Not verified against a proxy capture of the original. Light engine test fails on HEAD too (not from this change).

## 2026-10-14 — Claude — Supply menu (Tgui_hudmen 61 / 62)

- Use key on an own Supply / Super Supply opens "Supply Selection"; picking a slot sends `[26,150,slot,x,y]`.
  `mp_supply_items` parsed from `[252,6]` / `[237,11]` (`net_supply_items`). Code: `src/cs2d_scene_game.c`.
- Verified: layout and packet from the dump (`Tgui_hudmen::draw` case 0x3d, `FUN_14014f820`), string encoding from
  `proxy_ba2.log`. Not yet run against the dedicated; no screenshot comparison.

## 2026-10-14 — Claude — Auto switch on pickup

- [25] collect: the local player now holds the collected weapon and sends [9, type, 0] when opt_auto_switch is on (FUN_14020ce60 -> FUN_1401caca0).
- Verified: build + tests (48/49, the light-engine failure predates this). Not yet compared on a proxy capture.

## 2026-10-14 — Claude — HUD: infinite rounds and mp_infammo
- [252,6] roundtime byte now sets `cfg_roundtime`; main HUD hides the clock at mp_roundtime 100 (0x1402955b8).
- Clock also hidden in DM / TDM / Construction (table 0x1409f77e8 = 1,0,0,0,1).
- mp_infammo: main HUD shows clip | infinity (hud_symbols 11), weapon select "clip | INF".
- Verified: build + tests (48/49, Light engine failure pre-existing). Not compared on screenshots yet.

## 2026-10-14 — Claude — Game sounds (cs2d_sound)

- New `src/cs2d_sound.c`: SDL3 audio stream mixer, WAV (SDL_LoadWAV) + OGG (vendored `third_party/stb_vorbis.c`),
  port of `cs2d_sound_play_at` 0x1401191c0 / FUN_140109d30 (gain, pan, 20 per frame), TPosSound tracking.
- Hooks: weapon fire (+ silenced), wall ricochet, material hit / break, building hit, explosions, hit / die, item
  pickup / drop, build, [65], Env_Sound, thunder, flashbang, hostage, HUD slot keys. Config `mapsounds`, `soundmode`.
- Verified: `test_sound` (formula, table); client starts with audio. Not compared by ear with the original.
- Left: see OPEN_ISSUES "Audio".

