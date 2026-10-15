# Work log (append-only, newest first)

Add one entry per session: `## YYYY-MM-DD — <agent> — <topic>`, followed by a few bullets covering:

- what changed;
- how it was verified;
- what is left (move it to `OPEN_ISSUES.md`).

Keep entries short; the details go in `KNOWLEDGE.md` or in code comments.

Only the 5 newest entries live here; when adding one, move the oldest to the top of `worklog/archive.md` (do not read the archive in full; grep it).

## 2026-10-15 — Claude — settile
- New `cs2d_customtile.c` (Tcustomtile addcheck / add / endbatch): `[62]` long and short forms, join section
  `[252,7,9]` (was skipped), local `settile` command (FUN_140195d00 blend byte), collision / light / shadow-chunk update
  of the bounds + 1 (`cs2d_render_tiles_changed`, cs2d_update_mapdata).
- Verified: `settile_probe.lua` proxy capture of the dedicated (bytes + `tile()` values), `test_settile` (55/55), local
  screenshot `out/settile_ours.png`. No CS2D.exe screenshot comparison.

## 2026-10-15 — Claude — light engine: the rest (all but sv_fow)
- Tshoot lights for laser, turrets, NPC soldier / Vortigaunt (NPC shots now drawn), C4 beep (sound, flare, red light),
  `explosion_count` cap; Tp 2 flare (`cs2d_fx_flare`, replaces our fake 320 px muzzle glow); light sprites of the
  particles, Gen weather / smoke / fog, buildings (teleporters, gate field, portals) and Lua images (mode 3; ObstacleShadow2
  for mode 1 / > 132); day-buffer silhouettes of Lua image shadows and flying grenades (`cs2d_light_add_day_shadow`).
- Verified: emulator traces unchanged (0 mismatches), tests 54/54 (`test_fx_light_particles`), `explosion_probe.lua`
  screenshots of laser / HE vs the original (`out/light_compare2.png`). Style 6 lightning / p8 92 have no client caller.

## 2026-10-14 — Claude — light engine: failing test, flashlights, muzzle / explosion lights
- `test_light_engine` failed because cbd56be replaced `maps/fun_roleplay.map`; fixtures regenerated with the emulator
  (ours = original, 0 mismatches). `light_check.sh` needed `-Ithird_party`.
- New: `Tp_lightpool` (`pool_get` / `pool_return`), light particles Tp 0x1f (`cs2d_light_flash`: shots, explosions,
  flashbang), per-player flashlights (`cs2d_light_flashlight`), `[57]` send / receive, mp_flashlight (`[252,6]` h+1,
  `[237,14]`), special byte 32, flare1 on the player.
- Verified: emulator on scenes with a flashlight and a muzzle light (new `*_dyn_*` fixtures, tests 53/53); proxy
  `flashlight_probe.lua` on de_dust at night: same `[57,1]` (reliable) / `[57,1,1]` and matching screenshots.

## 2026-10-14 — Claude — reload sounds

- `cs2d_weapon_reload` plays w_clipout, `cs2d_weapon_finish_reload` w_clipin (FUN_140214780 modes 1 / 2, positional vol 1.0).
- Verified in the asm/dump only; build ok, tests 51/52 (light engine failure predates this).
- Shotguns (M3 / XM1014, flag 0x1409efcf8) reload one shell per 450 ms stage (reload x 5 ms) with w_roundin; online the
  shell comes from [16,pid,4] (fallback [16,101]); a shot interrupts; reloadani -> torso frame 3. `test_shell_reload`.
- Left: mode 4 not proxy-captured; clip reload x 10 ms vs the original's x 5 ms stage; mod item reload sounds.

## 2026-10-14 — Claude — every Lua image mode
- `<tile:N>`, `<flag:cc>`, `<avatar_N>` (placeholder), `<light>` (new dynamic point lights `cs2d_light_point`),
  follow-image recoil, mp_hudscale on HUD images (drawn first in the HUD), integer camera; modes 10..12 proven
  never drawn (parentId never set). NPC shadow now needs mapshadows > 1.
- Verified: `test_images_and_npcs` (path kinds), screenshots with the extended `image_probe.lua`.
