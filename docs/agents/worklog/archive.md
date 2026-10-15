# Work log archive (older entries, newest first). Do not read in full; grep it.

## 2026-10-15 — Cline — the [252,6] spread seed (TRandom.SeedTable) + opcode 40
- New `src/cs2d_random.c` + `include/cs2d_random.h` reproduce BlitzMax `TRandom`: `GenerateSeed` (0x14000fe90) fills a
  **1000-entry** table (`SeedTable`; the loop ends at `data + 0xfa0`) with values < **Range = 123**
  (`mov dword [0x140dde720],0x7b` at 0x14030720e; `B`/`aX` at 0x140dde728 / 0x140dde724, pointer 0x140dde730). The
  seed is the i32 in the join settings `[252,6]` (0x1402d0560: `Tudp_p.rint` + `GenerateSeed`), now read into
  `client->net_seed`; `Tpl.shoot` spreads with `SeedTable[Tpl.random] % (2*acc) - acc` per pellet, so the shotgun
  spreads like the server instead of `rand()` (`pl->shot_random` mirrors `Tpl.random`, `(random+1)%200` per pellet).
  Details: `knowledge/random.md`.
- Opcode 40 "kill dropped weapon" decoded: `[40, id u16]`, **reliable** (`Tudp_con.Createpacket(1,0)`; p1 == 1 takes
  the reliable id) from `Titem.fadeout` (0x14004cd20: `wbyte(0x28)` at 0x14004cec2 + `wshort(id)`). Its trigger is
  `Titem.update` (0x140147dc0): a *dropped* item (`Titem.dropped`, +0x34) ages +1 per drawn frame and fades at
  `0x140a14d94` (= 15). New `cs2d_client_send_kill_dropped_weapon` + a per-frame timer in `cs2d_client_update`; the
  `[24]` handler marks the item `dropped` (flags and the bomb stay 0, 1401f0000.c:1004).
  *(Corrected 2026-10-15: the timer counts seconds gated by DAT_140ddf540 and only runs while this instance is the
  server, DAT_140ddf400 - see "a weapon dropped on a public server vanished" in `WORKLOG.md`.)*
- Verified: `test_random_and_dropwep` locks four seeds' tables (`cs2d_random_table` / `cs2d_random_get`) and the three
  packet bytes, suite **61/61**; `build.bat` warning-free; a live offline join (`-offline`, de_dust2, `CS2D_NET_LOG=1`)
  logs `[net seed 33081]` and `[net seed_table_1 113]`, which the algorithm reproduces; `git status sys/` clean.
- Left (OPEN_ISSUES): the 15-frame fade threshold and the original's gates need a capture (the accuracy question
  is answered by the follow-up below).
- Follow-up (same session): extracted the **whole** accuracy table 0x1409f5d18 with `tools/agent/weapontables.py`
  after teaching it the `lea reg,[base+id*4]` form this build uses (init_weapons writes `[base + id*4]`): the 17
  values in `cs2d_weapon_accuracy` were already right, the 1..6 / 31 / 33 / 34 / 35 zeros are what init_weapons
  writes, and the id 92 (2) is an item we do not model. `Tpl.attack` (0x14021a6xx) calls `Tpl.shoot` - the only
  consumer - for typ 10 / 11 alone and sends every other weapon through `Tpl.shoot_recoil` (0x140209a90), so a
  player shot of any other weapon has no accuracy spread: our shotgun-only gate is the faithful behaviour, now
  stated in the comments and locked by the full table in `test_weapon_modes` (suite **61/61**). Also noted the
  muzzle candidates the re-extraction produced (shield 22, bomb 1, items 16, an id 92) as unverified.


## 2026-10-15 — Claude — pixel diff: our core-profile frame vs the original's (GL 3.3 phase gate)
- New `tools/agent/pixel_diff.py` (Pillow): differing pixels (%), mean/max channel delta, PSNR, per-band
  breakdown and the worst 32x32 cells, with `--window-shot` (crops a `drive_orig.ps1 shot:` to the client
  area: 3 px border, 26 px title), `--crop`, `--mask` and `--diff <heat map>`.
- New `tools/agent/lua/pixdiff_probe.lua`: player 1 on tile (44,25) of `prison_funst3r` (pixel 1424,816),
  armed on the `join` *and* `team` hooks. Two traps found on the way: the *original* only spawns after it
  picks a team (`drive_orig.ps1 tap:2/tap:1`), our client only via its `team ct` console command (its
  Select Team window could not be driven with `!mouse`, OPEN_ISSUES "Verification debt"), and
  `drive_orig.ps1` splits steps on `:` so its `shot:` path must be absolute.
- Measured (same scene, same camera, light engine on, 850x480): whole frame **18.52 %** of pixels differ,
  mean |delta| 4.93, PSNR 23.30 dB; **playfield only** (HUD + radar masked) **14.13 % / 2.32 / 28.19 dB**;
  our own run-to-run noise floor 3.9-4.7 % / 0.35-0.83 (the varying band is the player sprite, y 180..240).
  The scene needs >= 260 frames after the team selection to settle. Residual: the HUD strips and a region
  where ours is darker - screen x 240..300, y 190..300 = tiles (38..41, 23..28), cell mean 32 (original) vs
  20 (ours). Numbers, recipe and the earlier `--screenshot`/tour notes: `knowledge/rendering-notes.md`
  "Pixel comparison".
- Verified: the tool on identical images (0.00 %) and on two different ones (per-band + worst cells);
  `git status sys/` clean, `server.cfg` / `recentlyjoined.lst` restored after every run.
- Left (OPEN_ISSUES): the diff on a daylight map (de_dust2) and a `light 0..5` / `mapshadows 0..4` sweep;
  the darker tile region; the `!mouse` click question.

## 2026-10-15 — Claude — join handshake: `[252,0]` / `[252,1]` now match the original byte for byte
- `[252,0]` (cs2d_network.c) was 3 payload bytes: opcode, sub-opcode and an empty string nobody reads.
  The original sends **2** (`fc 00`) and lets the server append the secret in its reply - proxy
  `proxy_orig1.log` 2026-10-11: `C>S 7 02 00 03 32 00 fc 00`.
- `[252,1]` was built from guesswork ("auth_type", "security_hash", "os_info", a spray field stamped with
  `\xFA0opengl`). It is rebuilt in the order `FUN_1402ce7c0` writes it: name (sliced to 25), password
  (`j01nk3y`), version (`funkymonky`), u16 1018, **usgn id** (`DAT_140ddf1f0`) + name, **os id**
  (`DAT_140ddf5d8`: 1 Windows / 2 MacOS / 3 Linux - Tgui_hudmen draws the `Windows`/`MacOS`/`Linux` line
  from it), steam name + id, **spray logo file name** (`0x140a14f90` = the `spraylogo` value of
  sys/config.cfg, not the menu label), transformed secret, nickname scramble, client id, map mode,
  screen w/h, **language name + code** (the two halves of the `language` setting, `EN-English`), and the
  three trailing bytes 0/1/1 (4:3 `DAT_140ddf1c8`, voice available, `DAT_140ddf6a8`).
- New `join_core_id` (`cs2d_client.c`) reproduces the client id of field 13: `"\xfa0"` + the 32-char id
  `FUN_140060a10` reads at `sys/core/core.edc` +0x523 **+ the number the original appends, which is the
  size in bytes of its executable** (`bbStringFromLong(FUN_1404b7860(AppArgs[0]))`; `PTR_bbEmptyArray_
  140b13060` is AppArgs, and the writer of core.edc stores the same value at +0x543). CS2D.exe is
  14789120 bytes = the capture's number, so field 13 is byte-identical now; without the original next to
  us we fall back to core.edc's stored value.
- Verified: `test_network_protocol` compares the whole 138-byte `[252,1]` payload against a literal taken
  from the capture (`join1_want`), asserts `[252,0]` == 2 bytes and runs `cs2d_client_join_core_id` against
  the real `sys/core/core.edc` + `CS2D.exe` (42 bytes, tail `14789120`) - it also proves `cs2d_net_secret`
  returns the capture's `x!Ob448bb15R56` for `6X4XGs3PRs`; suite **60/60**. Live: `cs2d_dedicated.exe
  -offline -map de_dust2 -port 36970` + `cs2d_client.exe --capture:"connect …;inv"` gives
  `out/cli_join2.log`, whose `[net out 138] 04 00 fc 01 …` parses to **0 differing fields** against
  `proxy_orig1.log` (only the transformed secret differs, it is random per join;
  `out/join_compare.py` does the field-by-field diff). The dedicated answers `real is using IP …,
  no U.S.G.N. ID and no Steam`. `git status sys/` clean, server.cfg / recentlyjoined.lst restored.
- Left: the field 13 number; the `[252,1]` "nickname scramble" (`FUN_1402c97f0`) is empty in the capture
  but the dump computes a mangled name there - unresolved, we follow the capture.

## 2026-10-15 — Claude — `--screenshot` writes where it is told (OPEN_ISSUES verification debt)
- The tour wrote its 7 frames to a hardcoded absolute path inside one agent's scratch directory
  (`C:/Users/Gabriel/.gemini/antigravity/brain/<uuid>/scratch/`), so every other agent's run either failed
  silently or littered that folder. It takes a prefix now: `--screenshot[:<prefix>]` (a space works too, when
  WinMain hands over the whole line), default `screens/client_screenshot`.
- New platform helpers (both backends, no Win32 call of their own): `cs2d_platform_screenshot_path` builds
  `<prefix><suffix>.bmp` and keeps a `.bmp` the prefix already has (`screens/dust2.bmp` + `_console` =
  `screens/dust2_console.bmp`); `cs2d_platform_mkdir_parent` creates every missing level of the directory
  part. `main_client.c` prints `[screenshot] <path>` per frame, and a frame that cannot be written is
  reported on stderr **and** makes the process exit 1 (`screenshot_writes_failed`) instead of looking saved.
- Verified: `test_screenshot_paths` (extension handling, truncation, mkdir levels, refusals), suite **60/60**;
  the real tour on the default prefix and on `--screenshot:out/shots/de_dust2` (7 x 1,225,014 B BMPs, magic
  "BM", nested directory created), and a bad prefix (`screens/capture.bmp/x`) → 7 stderr lines, exit 1.
  `git status sys/` clean.
- Left: the tour itself is unchanged (de_dust2 → `prison_funst3r`, frames 45..75); the screenshot comparisons
  it exists for stay open in `OPEN_ISSUES.md` (GL 3.3 core vs the original, `light 0..5`, loading screen,
  and the rest of the "Verification debt" list).

## 2026-10-15 — Claude — bug: a de_dust2 spawn override teleported the local player
- Removed a leftover hardcoded placement in `game_load_map` (`cs2d_scene_game.c`): after `cs2d_game_start_round`
  had already spawned everyone on a spawn of their team, it moved the local player to tile (31, 44.5) - the CT
  side. A Terrorist holding the C4 was therefore yanked from the T spawn (tiles x 79..82) to the CT spawn area on
  every map load: the reported "teleporting with the bomb in hand". It came from commit f6d6bad (2026-10-10),
  not from the 2.7 work.
- The player now stays where `cs2d_game_start_round` put them. The client's only other position writes are the two
  tile-entry teleports (a `Func_Teleport`, and portals / teleporters) and de_dust2 has neither: 1403 entities, no
  type 70 and no Env_Building (checked with a throwaway dump tool, not committed).
- Verified: new assertion in the game-flow test (every player stands on an Info_T / Info_CT tile of their own team,
  so nothing may move them afterwards), suite **59/59**; `--screenshot` de_dust2 exit 0, `git status sys/` clean.

## 2026-10-15 — Claude — the NPC attack spin decays per draw (OPEN_ISSUES 2.7, 5th pass)
- `update_npc` no longer decays `rotvar`; `render_npc` does it (`rotvar -= 5`) after drawing, exactly where
  `Tdo.draw_ground` case 0x1e does it. Consequences, both faithful: a **culled** NPC keeps its spin (the early
  cull return happens before the decay) and the spin follows the drawn frames, not our fixed 60 Hz accumulator.
- `cs2d_render_dynobjs` lost its `const` (`cs2d_render.h`, the 3 calls in `cs2d_scene_game.c` pass a non-const
  world), since the draw now writes one field, like the original.
- Verified: the existing NPC assertions plus a new one (`rotvar` stays 20 across 30 simulated frames), suite **59/59**;
  `--screenshot` de_dust2 exit 0.

## 2026-10-15 — Claude — Tdo.shoot: the shot spread (+ the turret reach) (OPEN_ISSUES 2.7, 4th pass)
- `cs2d_fx_tdo_shot_angle` (cs2d_fx.c): `Tdo.shoot` 0x140209b50 hands `Tshoot.Shoot` the angle **rot + Rnd(-3, 3)**
  (the double constants 0x140bd6c18 / 0x140bd6bb8 read from CS2D.exe), the same function that sets c_dist 400
  (0x140ddea0c) and c_offset 15 (0x140ddea08). Applied in the NPC shot (cs2d_client.c kind 5, soldier tracer and
  Vortigaunt beam) and in the turret burst.
- The turret tracer reach becomes the `Tdo.shoot` c_dist **400** (and c_offset 15) instead of the calibrated 300:
  `FUN_14028c630` fires through the method at +0x120 with one call per barrel (0 / +-6 / 0,+-8 px) and passes no
  distance, so Tdo.shoot's 400 applies.
- Verified: `test_tdo_shot_spread` (200 draws stay in [rot - 3, rot + 3] and vary), suite **59/59**.


## 2026-10-15 — Claude — NPC death sounds + the `particles` config (OPEN_ISSUES 2.7, 2nd pass)
- `cs2d_dynobj_kill` (kind 6) + `npc_die_sound`: the die sound of `Tdo.kill` case 0x1e, all five NPC types
  (zombie `zm_die`, headcrab / snark / vortigaunt `*_die`, soldier = a random `sfx/player/die[1..3]`), played by
  `cs2d_client.c` at the NPC. Resolved from the sound loader 0x14030cc00: in each block the name `lea` belongs to
  the sound stored in the *next* global, and the literals are static BBStrings in `.data` (UTF-16 at +0x10), read
  straight from the file (KNOWLEDGE sound.md "How to resolve a global to a file").
- `particles` (`DAT_140ddf182`) and `gore` (`DAT_140ddf192`) are parsed from sys/config.cfg; `cs2d_config_particles()`
  feeds the building-hit fragment count and the wall-ricochet gate (was a fixed 4).
- Verified: `test_images_and_npcs` (die sound of every NPC type, the silent invalid type, the soldier's die1..3),
  the particles / gore config defaults in `test_item_and_player_shadows`, suite **57/57**; the zombie sheet
  (64 x 96 = 2 x 3 cells of 32 x 32) checked against the BMP header, no code change was needed.
- Left (OPEN_ISSUES "Construction"): the gore-gated ground blood decals and the snark's particle fountain, the
  remaining `3 < particles` gates, fragments stopping at walls, the vortigaunt beam / soldier tracer.


## 2026-10-15 — Claude — Tp 12 debris stops at walls (OPEN_ISSUES 2.7, 3rd pass)
- `cs2d_fx_update` takes the map (`&client->game.map`; NULL in the tests) and, after moving a `CS2D_FX_FRAGMENT`
  (Tp 12), looks the tile up in `cs2d_map_prop_class(cs2d_map_get_tile_mode())`: a wall (class 0) zeroes the speed
  (the fragment stops where it is) and a deadly tile (class -1) halves the alpha - Tp.draw 0x140263cc0 case 0xc.
- Verified: `test_fragment_walls` (a fragment that lands in the wall column stops with its x unchanged, the same
  fragment in the deadly column keeps its speed at half the alpha, no map = no check), suite **58/58**.

## 2026-10-15 — Claude — construction mode: g_prop_class + hostage hover (OPEN_ISSUES 2.7)
- `cs2d_map_prop_class(mode)` (`cs2d_map.c/.h`): one `g_prop_class` helper (0 wall, 1 floor, 2 obstacle, -1 deadly).
  `cs2d_gen.c` (its own duplicate) and `cs2d_dynobj.c` (can-build `FUN_14014ef60`, portal tile) call it; the
  "collision == 0 stands for the floor class" approximation is gone and the class is no longer re-derived in 3 places.
- Hostage hover text: `mp_hovertext` bit 4 (`cs2d_hostages_hovered` + `render_hostage_hover`), wired between the
  player hover (which skips it) and the building hover (which it suppresses). New `Thostage.fog` (+0x5c) field.
- Verified: dump / REA (`FUN_1402a12d0` check order and text lines, Thostage field layout, the class uses in
  0x1402829ed / 0x140170000 / 0x140180000, `g_prop_class` = `[rip] # 0x140a0fd90`); `test_prop_class` and the hover
  cases in `test_hostages`, suite **57/57**.
- Left (OPEN_ISSUES "Construction"): the team-game / respawn tables stay assumed, the hover screenshots are owed,
  Gate Field team blocking, kill rewards, the teleporter destination menu.

## 2026-10-15 — Claude — web port: phase 0 + browser client
- New `cs2d_platform.h/.c` (the Win32 include site; POSIX shims, dir listing, mkdir, net init), `cs2d_font.c`
  (text engine out of `cs2d_render.c`: GDI+ backend unchanged + stb_truetype backend), `client_frame()` loop,
  WebGL2 GLSL header, `tools/web/*` (emenv, build_tests, build_client, serve, shell, font_compare).
- Verified: desktop 56/56 + `--capture` de_dust2 unchanged; Wasm suite under Node 56/56; stb metrics = GDI+ on
  2365 strings x 7 fonts (`test_font_metrics`); main menu + de_dust2 in headless Chromium (screenshots).
- Left (OPEN_ISSUES "Web port"): network gateway (decision), IDBFS, asset trimming, sound gesture, CI.

## 2026-10-15 — Claude — settile
- New `cs2d_customtile.c` (Tcustomtile addcheck / add / endbatch): `[62]` long and short forms, join section
  `[252,7,9]` (was skipped), local `settile` command (FUN_140195d00 blend byte), collision / light / shadow-chunk
  update of the bounds + 1 (`cs2d_render_tiles_changed`, cs2d_update_mapdata).
- Verified: `settile_probe.lua` proxy capture of the dedicated (bytes + `tile()` values), `test_settile` (55/55),
  local screenshot `out/settile_ours.png`. No CS2D.exe screenshot comparison.

## 2026-10-15 — Claude — light engine: the rest (all but sv_fow)
- Tshoot lights for laser, turrets, NPC soldier / Vortigaunt (NPC shots now drawn), C4 beep (sound, flare, red light),
  `explosion_count` cap; Tp 2 flare (`cs2d_fx_flare`, replaces our fake 320 px muzzle glow); light sprites of the
  particles, Gen weather / smoke / fog, buildings (teleporters, gate field, portals) and Lua images (mode 3;
  ObstacleShadow2 for mode 1 / > 132); day-buffer silhouettes of Lua image shadows and flying grenades
  (`cs2d_light_add_day_shadow`).
- Verified: emulator traces unchanged (0 mismatches), tests 54/54 (`test_fx_light_particles`), `explosion_probe.lua`
  screenshots of laser / HE vs the original (`out/light_compare2.png`). Style 6 lightning / p8 92 have no client caller.

## 2026-10-14 — Claude — reload sounds
- `cs2d_weapon_reload` plays w_clipout, `cs2d_weapon_finish_reload` w_clipin (FUN_140214780 modes 1 / 2, positional vol 1.0).
- Verified in the asm/dump only; build ok, tests 51/52 (light engine failure predates this).
- Shotguns (M3 / XM1014, flag 0x1409efcf8) reload one shell per 450 ms stage (reload x 5 ms) with w_roundin; online the
  shell comes from [16,pid,4] (fallback [16,101]); a shot interrupts; reloadani -> torso frame 3. `test_shell_reload`.
- Left: mode 4 not proxy-captured; clip reload x 10 ms vs the original's x 5 ms stage; mod item reload sounds.

## 2026-10-14 — Claude — light engine: failing test, flashlights, muzzle / explosion lights
- `test_light_engine` failed because cbd56be replaced `maps/fun_roleplay.map`; fixtures regenerated with the emulator
  (ours = original, 0 mismatches). `light_check.sh` needed `-Ithird_party`.
- New: `Tp_lightpool` (`pool_get` / `pool_return`), light particles Tp 0x1f (`cs2d_light_flash`: shots, explosions,
  flashbang), per-player flashlights (`cs2d_light_flashlight`), `[57]` send / receive, mp_flashlight (`[252,6]` h+1,
  `[237,14]`), special byte 32, flare1 on the player.
- Verified: emulator on scenes with a flashlight and a muzzle light (new `*_dyn_*` fixtures, tests 53/53);
  `flashlight_probe.lua` on de_dust at night: same `[57,1]` (reliable) / `[57,1,1]` and matching screenshots.

## 2026-10-14 — Claude — every Lua image mode
- `<tile:N>`, `<flag:cc>`, `<avatar_N>` (placeholder), `<light>` (new dynamic point lights `cs2d_light_point`),
  follow-image recoil, mp_hudscale on HUD images (drawn first in the HUD), integer camera; modes 10..12 proven
  never drawn (parentId never set). NPC shadow now needs mapshadows > 1.
- Verified: `test_images_and_npcs` (path kinds), screenshots with the extended `image_probe.lua`.

## 2026-10-14 — Claude — Lua images and NPCs (Tdo 40 / 30)
- `[51]` 0..9, `[59]`, image `[54]` tweens, snapshot sections 7 / 8 / 10 (Trestable "?" paths), NPC `[36,7]` / `[36,9]`,
  NPC motion / attack fx, image passes (background hook in `cs2d_render_map_floor_ex`, floor, top, overwall, HUD).
- Fix: map cycle is snapshot section 12 (9 = custom tiles, now skipped); the proxy shows 7..13 in order.
- Verified: `test_images_and_npcs`, proxy + screenshots with `image_probe.lua` on the offline dedicated. Light engine test still fails (pre-existing).

## 2026-10-14 — Claude — Portals and teleporters
- `cs2d_dynobj_place_portal` / `_portal_tile` / `_teleport` (FUN_1401fc9f0, Tpl.tilepos 0xd/0x16/0x17): offline Portal
  Gun placement on floor tiles, portal ↔ portal and Teleporter Entrance → Exit on tile entry; portal close flash.
- Verified: `test_dynobjs` cases (dump-derived rules); not compared with a capture. Light engine test fails before and after.

## 2026-10-14 — Claude — item scale, [33] ammo offset, held weapon size
- Ground items and held weapon drawn at native size with the original uniform scale (were squashed to 32x32 / per-axis
  halves); `[33]` clip / reserve read as u16 - 1 (FUN_14011c0b0) → no "(0)" on supply items. Source: dump.
- light engine test (light_fun_roleplay line 458) already failed before this session.
## 2026-10-14 — Claude — special weapons, consumable items, item tooltips
- Laser beam / flames / RPG backblast (`cs2d_fx_laser/flame`), launchers as projectiles without `[7]` (GL now too),
  Portal Gun `[50]` send/receive + portal Tdo drawing; table fixes (45..49, 88, 89 = Satchel, new 64..68).
- Consumables on pickup (`cs2d_player_consume_item`); `[33]` ammo normalized (`cs2d_weapon_item_ammo`) → no
  "(empty)" on supply items; 64..68 now named items (plain tooltip).
- Verified: dump/REA (knowledge/special-weapons.md), `test_special_items`, offline screenshots (laser, flame, RPG).

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

## 2026-10-14 — Claude — Lua menu height, serveraction, F-key binds

- Lua menu (menu 120) window is 440 tall, centred vertically (`Tgui_hudmen.update` 0x1b8); 330 clipped item 9 over Cancel.
- `serveraction1..3` (F2..F4) send `[240, 4, 1, "N"]` (0x1401b1446 → FUN_1401de400); F1 `serverinfo`, F6 `console` via binds.
  Removed the hardcoded F1 console / F2 New Game / F3 Options keys. F-keys use `CS2D_KEY_F1` (VK_F* collide with 'p'..'{').
- Verified: build, tests (light engine test `light_fun_roleplay_15_20_floor` fails, unrelated). Not proxied against original yet.

## 2026-10-14 — Antigravity — Lua server menu (menu(), opcode 235 CS2D_NET_LUAMENU)

- **Changed:**
  - Implemented `cs2d_luamenu_t` parsing, rendering, and input in `src/cs2d_hudmen.c` matching CS2D.exe (`FUN_14015c320`, `Tgui_hudmen.update` at `0x14002ef23`, `Tgui_hudmen.draw` at `0x14022680e`).
  - Supports comma-separated `"Title[@b|@i],Btn1|Right1,(Btn2|Right2),..."`:
    - Mode 0 (default): `272x330` window at `(20, 20)` with `240` px buttons.
    - Mode 1 (`@b` big centered): `600x330` window centered at `y = 20` with `568` px buttons.
    - Mode 2 (`@i` invisible): positioned off-screen at `x = 10000` for script input capture.
    - Parentheses `(option)` mark items as disabled.
    - Escaping for `\,`, `\|`, `\(`, `\)`.
    - Key 0 ("Cancel", `sys/language 0027`) at `y = wh - 46` and top-right close `X` button.
  - Added opcode 235 (`CS2D_NET_LUAMENU`) dispatcher in `src/cs2d_client.c` for receiving server menu strings, and client -> server reply `cs2d_client_send_luamenu` (`[235, pid, slen, title_utf16, button]`).
  - Integrated into `src/cs2d_scene_game.c` for rendering, number key selection (`0..9`), and mouse clicks.
- **Verified:** test `test_lua_menu` in `tests/test_cs2d.c` testing string parsing, modes `@b`/`@i`, disabled options, keys and mouse clicks (45/46 tests passed). Full build clean.

## 2026-10-14 — Antigravity — mp_hudscale (modes 0..3, zoom scaling, aspect letterboxing, font sharpness)

- **Changed:**
  - Implemented `cs2d_hudscale_compute` in `src/cs2d_hudtxt.c` from CS2D.exe (`FUN_140291080`, `FUN_14024f5a0`, `1402b0000.c:6760-6785`):
    - Mode 0: 1:1 screen pixels (`screenw x screenh`).
    - Mode 1: 850x480 virtual resolution, scaled by `g_zoom = min(screen_w / 850, screen_h / 480)` and letterbox offsets.
    - Mode 2: 640x480 virtual resolution stretched across viewport (`screen_w / 640`, `screen_h / 480`).
    - Mode 3: 640x480 virtual resolution centered unscaled (`off_x = (screen_w - 640) / 2`, `off_y = (screen_h - 480) / 2`).
    - Font sharpness (`FUN_14024f5a0:8645`): when `mp_hudscale` in 1..2 and `screen_h > 589`, selects raster font size `size * g_zoom` (clamped 10..100) and scale `1.0 / g_zoom` for pixel-crisp 1:1 rasterized text on high-res displays.
  - Added `cs2d_render_text_transform_ex` to `src/cs2d_render.c` supporting non-uniform 2D font scaling.
  - Updated `cs2d_hudtxt_render` to transform coordinates, line height, image sizes, and alignment across all 4 modes.
  - Added `net_hudscale` to `cs2d_client_t`: parsed from `[252, 6]` byte at offset `b + 16` (`0x140a14de8[1]`), `[237, 22, val]` (`changesettings` case 0x16 in `FUN_1401e3f10`), and console command `mp_hudscale [0..3]` (`0x1401bbc89`).
- **Verified:** regression test `test_hudscale` in `tests/test_cs2d.c` (44 / 45 passed, only known pre-existing `test_light_engine` formatting diff remains). Build succeeds cleanly.

## 2026-10-14 — Claude — Lua HUD texts (hudtxt, hudtxt2, hudtxtclear, move / alpha / colour fades)

- New `src/cs2d_hudtxt.c`: [234] (FUN_1400a3530), [54] Ttween for hud texts, update and draw (FUN_14024f5a0 /
  FUN_14024cc00, inline `©` colours and `®` images); render: sized fonts `CS2D_FONT_SIZED(px)` 10..100.
- Verified: proxy capture + screenshots vs CS2D.exe with `tools/agent/lua/hudtxt_probe.lua`; `test_hudtxt`.
  Build, 43 / 44 tests (`test_light_engine` fails on fun_roleplay, map file modified in the working tree).
- Left: mp_hudscale, Tdo (Lua image) tweens, `®` images not screenshot-compared (OPEN_ISSUES).

## 2026-10-14 — Claude — join password prompt

- `[252,2,1]` now opens the "Password required" window (Tgui_joinmen menu 1) instead of aborting; Connect restarts the join with the typed password, Cancel/Esc sends `[253,9]`. Layout and strings from the dump (net-join.md).
- Verified: build + 43/43 tests. Not yet tested live against a dedicated with `sv_password` (Esc = Cancel is an assumption).
- Refused joins (`[252,2]` / `[252,4]` status != 0) now open the original error window instead of aborting; status 11 offers temp / overwrite / cancel and rejoins with that map mode. Not tested live yet.

## 2026-10-14 — Claude Code — Ground item hover tooltip
- **Changed:** `render_item_hover` (FUN_14029ffb0, mp_hovertext bit 16); `net_infammo` read from `[252,6]`. Not yet compared with a screenshot of the original.

## 2026-10-14 — Claude Code — CTF flags: carried flag, drop / take / return messages, draw over walls
- **Changed:** `Tpl.flag` is `cs2d_player_t.has_flag` (Titem.give 70 / 71 sets it, it is not an inventory item): `[25]`
  of a flag sets it with the 0707 / 0708 / 0709 messages, `[24]` clears it with 0704 / 0705, `[31]` of a CTF base back
  to state 0 shows "... Flag returned". Radar, scoreboard and the carried-flag sprite read it. The waving base flag
  (`cs2d_render_map_flags`) and the carried flag are drawn after the wall tiles.
- **Verified:** proxy capture with `tools/agent/lua/ctf_probe.lua` on ctf_cs2dfortress (screenshots: flag on the back,
  dropped flag on the ground, message); build, 43 / 43 tests.

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
- 2026-10-14 (Claude): HUD chat + center messages honour inline ©RRRGGGBBB codes mid-line (render_text_colored in cs2d_scene_game.c); multi-colour server msgs.

## 2026-10-14 — Claude — Editor entity helpers
- **Changed:** zones, Env_Light radius, LightStripe, Teleport line and trigger links are drawn only on hover / Space
  (Te.drawineditor + Ted_tr.drawat).

- 2026-10-14: weather (rain/snow) drawn after light buffer; Env_Room suppresses weather spawn like Info_NoWeather (user request, not REA-verified).
- 2026-10-14 (Claude): smoke grenade cloud ported from Tp 0x1b (sgsmoke.bmp x4, dark grey) instead of white disc + puffs.
- 2026-10-14 (Claude): Gen_Weather fog spawned 60x too often; gated on the 1 s tick (DAT_140ddf540). Test added.
- 2026-10-14 (Claude): walls drawn after players (cs2d_render_map_floor / cs2d_render_map_walls); smoke/gas fields after players, before walls.

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

## 2026-10-13 — Claude — Gen_Particles / Gen_Sprites / Gen_Weather / Gen_FX + Info_NoWeather

- **Changed:** new `src/cs2d_gen.c` (+ `include/cs2d_gen.h`): spawn rules of `Te.draw` pass 2 and the `Tp` types
  1/3/12/14/17/19/20/21 in fixed 60 Hz steps; `cs2d_render_gen` / `cs2d_render_gen_flash` (render.c) with
  wave/snowflake/raindrop/smokepuff textures; hooked in `game_load_map`, the game update and `game_render`.
- **Verified:** `test_gen_entities` (39 / 39); our client screenshots of de_aztec_classic (rain + fog) and de_inferno
  (chimney smoke). Not compared to original screenshots yet.
- **Left:** see OPEN_ISSUES "Gen_* entities".

## 2026-10-13 — Claude — Map loading screen

- **Changed:** `cs2d_render_loadscreen` (render.c) draws+presents a "Loading Map" window with step text and progress
  bar (port of `LoadScreen::DrawLoadingScreen` 0x1401d1610 state 3); `game_load_map` calls it at each step.
- **Verified:** build + 38 / 38; not screenshot-compared.
- **Left:** see OPEN_ISSUES "Loading screen".

## 2026-10-13 — Claude — Env_Decal (TDecal) + entity checklist

- **Changed:** Env_Decal moved out of the ground pass into its own `CS2D_EPASS_DECAL`, drawn right after the tiles
  (`render_decals`): alpha = `strs[0]` as float with no invented default ("" = invisible), frame clamped, one decal
  per tile (first wins), entity_state ignored. Source: `Te.setup` 0x1401ad930 case 0x18, `TDecal::Add` 0x140005c70,
  `FUN_140110c60`. Added the implementation checklist to `knowledge/entities.md`.
- **Verified:** `test_env_decal` + updated `test_entity_culling`; 38 / 38. No screenshot comparison yet.
- **Left:** decals on wall/obstacle tiles are drawn in the original's wall/obstacle tile loops (layer 4 / 5);
  ours draws all after the single tile pass.

## 2026-10-13 — Antigravity — Fix player shadow order and remove BombSpot decals on floor

- **Changed:**
  - Removed floor rendering of `bspot_a.png` / `bspot_b.png` for `CS2D_ENT_INFO_BOMBSPOT` in `src/cs2d_render.c`:
    the original CS2D renders no floor decals for BombSpots on the game map; bombspot icons appear only on the radar / minimap.
  - Reordered the game scene render loop in `src/cs2d_scene_game.c`:
    - Moved the light buffer (`cs2d_light_draw_buffer()`) and world floor shadows (`cs2d_render_map_shadows()`) to render *before* the player drawing loop instead of after it.
    - Previously, `cs2d_light_draw_buffer()` was multiplied (blend mode 5) over the scene *after* players, multiplying the black player day shadow silhouette directly over the player's body and darkening it by ~50%.
    - With the new pass order, world shadows, day shadows and the player's floor shadow silhouette are rendered onto the floor first, and player bodies are rendered on top with their legs, torso and weapons clean and unshaded. Overhead passes (rooftop sprites and tree canopies) continue to render above players.
  - Separated CPU floor shadow chunk pass (`cs2d_render_map_shadows`) from `cs2d_render_map_overhead`, calling it before players in `src/cs2d_scene_game.c` and before overhead entities in `src/cs2d_scene_editor.c`.
- **Verified:**
  - `build.bat` compiles `test_cs2d.exe` and `cs2d_client.exe` without warnings or errors.
  - `test_cs2d.exe` regression suite: **37 / 37** tests passing.

## 2026-10-13 — Antigravity — Dynamic map list in New Game (Create Server) from maps/ directory

- **Changed:**
  - Replaced the hardcoded 10-map list in Create Server ("New Game") with a dynamic directory scanner
    (`cs2d_maplist_load` in `src/cs2d_client.c`, declared in `include/cs2d_client.h`).
  - Faithfully ported the original BlitzMax map scanner logic (`Tgui_ml::Load` 0x1400af1d0 and `FUN_1401a4610`):
    - Reads map prefix categories from `sys/maptypes.lst` (e.g. `as_`, `cs_`, `de_`, `dm_`, `ctf_`, etc.).
    - Scans `maps/*.map`, filters out `temp.map` and `autosave.map`, lowercases and strips extension.
    - Groups maps under their respective section header in `maptypes.lst` order, sorted alphabetically per group.
    - Remaining maps without matching prefixes are grouped under "Other Maps" (`sys/language` entry 0182).
  - Integrated `cs2d_ui_scrollbar` with fixed 300px list container height and mouse wheel scrolling support.
  - Selected map is stored by name (`srv_selected_map`), synced with Map Cycle view, and used for console `map <name>` execution.
  - Fixed duplicate `work_ms` declaration in `src/main_client.c`.
  - Added test `test_new_game_maplist` to `tests/test_cs2d.c` validating grouping, sorting, exclusions, and headers against `sys/maptypes.lst`.
- **Verified:**
  - `build.bat` compiles `test_cs2d.exe` and `cs2d_client.exe` cleanly.
  - `test_cs2d.exe` regression suite: all **37 / 37** tests passing.
- **Left:**
  - Map cycle editing (add/remove from cycle).

- 2026-10-13: chat input starting with '/' runs the rest as a console command (cs2d_client_handle_console_key); not yet checked against the original.

## 2026-10-13 — Antigravity — Sprite atlasing for entity sprite set & lifted 64-sprite cache ceiling

- **Changed:**
  - Implemented `include/cs2d_atlas.h` and `src/cs2d_atlas.c`: shelf packing (First-Fit Decreasing) of map
    entity sprites (`Env_Sprite` 22, `Env_Image` 28) into 2048x2048 atlas pages at map load (`game_load_map`,
    `editor_load_map`).
  - Added 4 px extruded margin padding (`CS2D_ATLAS_PAD = 4`) around every cell to eliminate neighbour bleed with `GL_LINEAR`.
  - Maintained separate atlas pages for `mask_mode` 1 (normal / magenta key) and `mask_mode` 3 (black mask / additive).
  - Extended `cs2d_texture_t` with `src_x`, `src_y` page offsets while strictly preserving logical unpadded dimensions in `width` and `height` so camera culling and entity learn size logic remain intact.
  - Replaced the fixed 64-entry `CS2D_MAX_SPRITE_CACHE` ceiling with a dynamically sized sprite cache in `cs2d_gfx_bank_t`.
  - Updated sprite draw UV calculations across `cs2d_render.c` and `cs2d_light.c` to offset UVs by `(src_x, src_y)`.
- **Verified:**
  - `build.bat` clean; `test_cs2d.exe` passed 35 / 36 (added `test_sprite_atlas_packer` covering >64 sprites, no overlaps, UV round-trips, and map atlas loading without truncation).
  - Screenshots at `tp 44 25` (`screens/atlas_before.bmp` vs `screens/atlas_after.bmp`) are 100% byte-identical (1,225,014 / 1,225,014 bytes equal).
  - First-frame freeze eliminated: map load builds atlas in **172.35 ms** for 156 sprites across 2 pages; first frame render max time dropped from **1447 ms** to **7.7 ms** (187x improvement).
  - GPU time per frame dropped from **1.912 ms** to **1.750 ms** (-8.5%) with light, and from **1.433 ms** to **1.317 ms** (-8.1%) with `light=0,mapshadows=0`.
  - Draw calls and texture switches dropped from 672.7 / 296.2 to **664.7** / **288.3** in steady-state, and down to **472.8** / **104.1** at `tp 44 25`.
- **Left:**
  - Light engine internal texture atlas (273 draw calls, 256 texture switches with light on).
  - Unconditional 128x128 radar minimap upload on every frame.
  - VSync OFF frame cap.

## 2026-10-13 — Cline — Why our client uses ~20 % GPU where the original uses 0.6 %

- **Question (user):** Task Manager shows ~20 % GPU on `fun_roleplay` for our client, 0.6 % for the
  original. Reproduced first: while our client ran, the whole desktop's `engtype_3D` total was
  **17.85 %**, of which **17.76 %** was our pid — the attribution is not a Task-Manager artifact.
- **Method:** the counter Task Manager itself uses, `\GPU Engine(*)\Utilization Percentage`
  (`engtype_3D`, per pid), sampled once a second. Two new tools: `tools/agent/perf_gpu_matrix.ps1`
  (steady state, rewrites `sys/config.cfg` per case and restores it) and
  `tools/agent/perf_gpu_frames.ps1` + `tools/agent/perf_frames.sh` (a `--capture:"map …;!wait 600"`
  run = 636 frames, so GPU busy time / frames gives ms of GPU per frame). New `CS2D_MSAA` env gate in
  `src/main_client.c` (default unchanged: 4x) is the axis one of the runs compares.
- **Measured** (850x480, `fun_roleplay`, shipped `light 5 mapshadows 4 shadows 50`): **22.7 % GPU
  with VSync on** (13.8 % with every light setting off), **42-46 % with VSync off** where the CPU
  goes past one core; per frame **0.24 ms** (main menu) against **2.0-2.5 ms** (drawn map; MSAA 4x vs
  `CS2D_MSAA=0` is inside the run-to-run spread, MSAA does cost ~1.1 ms of CPU). The original's 0.6 %
  is ~0.1 ms of GPU per frame, so our world frame is 20-40x heavier in GPU terms.
- **Conclusion:** the cost is in the world pass (the menu proves the present path is cheap), ~40 % of
  it is the light engine + map shadows, and VSync OFF additionally removes any frame cap (the
  original locks 65 FPS). Numbers: `KNOWLEDGE.md` "Performance".
- **Then attributed the draw calls** (`CS2D_DRAWCALLS=1`): `fun_roleplay` = **699 calls/frame for
  32,557 vertices** (avg batch 47 verts), of which **320 are texture switches** (one `gfx/...` file per
  `Env_Sprite`/`Env_Image`) and 273 calls + 256 switches belong to the light engine; the tileset grid
  already goes out in **one** call (max batch 6,210 verts, 0 overflows). The remaining ~360 calls are
  blend toggles and target flushes. Same conclusion as LÖVE's `SpriteBatch` documentation: batching
  needs a shared image, so the next win is atlasing the sprite set — grouping by state is not an
  option because the sprite order is the original's painter's order. Tool: `perf_gpu_frames.ps1
  -DrawCalls [-Config 'key=val,…']`.
- **Left (moved to `OPEN_ISSUES.md`):** decide the frame cap, and re-measure before optimising the
  batcher/radar.

## 2026-10-12 — Cline — Entity index & view culling (5 sweeps per frame → one typed list per pass)

- **Measured first** (`.map` parse off-line, then the new test): `fun_roleplay` is **360x512 tiles with 9901
  entities**, and only **22 of them are inside a 27x15 tile view on average** (0.2 % — over all 166,332 camera
  positions, min 0, max 212). The five map passes (`src/cs2d_render.c`: tile-animation remap, Breakable /
  DynWall, ground actors, rooftop sprites, rooftop objects) swept all 9901 entities each, so **49,505
  dispatches per frame for ~20 draws**; `Env_Sound` (769), `Trigger_Delay` (509) and `Trigger_If` (387) were
  swept five times per frame without drawing anything.
- **Changed:** `src/cs2d_render.c` "Entity index & view culling": one cached list per pass, **ascending entity
  index** so the draw order of the original sweep is unchanged, rebuilt only when the map or its entity table
  changes (`map->entity_table_version`, new in `include/cs2d_types.h`, bumped by the loader in `cs2d_map.c`
  and by `cs2d_ed_entity_add` / `cs2d_ed_entity_remove`). Entities whose draw box (tile centre ±
  `cs2d_render_entity_margin` tiles) cannot touch the view are skipped before the string parse, the sprite
  lookup and the draw. `strs[4]` of `Env_Sprite` is parsed once into the index (was `atoi` per sprite per
  pass, 3x per frame) and `render_env_sprite_entity` takes the map + entity index now. A texture sized
  sprite/image reports the size it just drew (`cs2d_render_entity_learn_size`), so it is culled from the
  second frame on.
- **Verified:** new `test_entity_culling` in `tests/test_cs2d.c` — pass membership and ascending order,
  rebuild on `entity_table_version`, the culling invariant ("no entity whose draw box touches the view is
  dropped") over 17x22 cameras, the learned sprite size, and three real maps. Measured there: `fun_roleplay`
  9901 entities → 10,357 list entries and **998 considered per frame (49.6x fewer than the 49,505)**, 902 after
  the sprite sizes are learned; `de_dust2` 1403 → 2,907 entries, 218 considered (vs 7,015);
  `prison_funst3r` 1608 → 1,651 entries, 311 considered (vs 8,040). Suite 34 / 35 (the only failure is the
  known `test_file_transfer` "Transfer list" gap).
- **Left:** the freeze numbers of `OPEN_ISSUES.md` were **not** re-timed. An `Env_Sprite` with an explicit
  `ints[0]` / `ints[1]` size is now skipped *and not loaded* when it is off screen, even on the first frame,
  so expect the first frame to be cheaper on maps that size their sprites (which is what the 998 → 902
  difference after learning shows: those had no explicit size). A texture sized sprite / image is still drawn
  once before it can be culled, so the preload stays owed. `Env_Object` and texture sized `Env_Image` frames
  are never culled yet (see `OPEN_ISSUES.md`).



- **Question (user):** the game ran with less performance than expected with VSync disabled.
- **Method:** temporary per-section timers in the `main_client.c` loop (events / update / render /
  `glFinish` / `end_frame` / `SDL_Delay`) plus a native-pacing mode (`CS2D_PERF=<frames>`,
  `CS2D_PERF_VSYNC=0|1`, `CS2D_PERF_CMD="<console cmd>"`); all of it removed again before the session
  ended, so the tree only keeps the findings.
- **Measured (this machine, 850x480, `sys/config.cfg` at its shipped maximum, light 5 / mapshadows 4):**
  - **VSync OFF:** prison_funst3r 167-176 FPS (5.7-6.1 ms/frame), de_dust2 163-176 FPS,
    fun_roleplay 72-98 FPS (10.2-14 ms/frame).
  - **VSync ON:** 59.6 FPS (locked to the 60 Hz panel). The `render` number jumps 4.4 -> 15.2 ms
    there, but that is the deferred vblank wait landing in the next GL call — `end_frame` still
    returns in 0.04 ms and there is no stall.
  - Split at steady state (prison_funst3r, VSync off): events 0.008 | update 0.02 | render 4.4 |
    `SDL_Delay(1)` 1.49 ms.
  - **The original locks its own frame at ~65 FPS** (`15.384615 ms` = 1000/65,
    `docs/dump/cs2d/_global/1402f0000.c:2284`), so our uncapped 167 FPS is *faster* than the original.
  - `SDL_Delay(1)` costs 1.8 ms and a swap with interval 0 presents in 0.49 ms (2029 FPS) here, so
    neither the loop delay nor the swap is a cap on this machine.
- **Real defect found:** the first frame after a map is set up freezes for **~0.1 ms per map entity**
  (133 ms prison_funst3r / 140 ms de_dust2 / **1447 ms fun_roleplay**, 9902 entities). The cost is
  entirely inside `cs2d_client_render` and survives `light 0`, `mapshadows 0` and `drawradar 0`, so it
  is the lazy per-entity sprite load, not the light engine, the map shadows or the radar/minimap.
- **Method trap for the next session:** the `--capture` harness paces itself with `SDL_Delay(16)` per
  frame, so its "avg ms/frame" is CPU+GPU work only and its FPS is the harness's, never the game's — two
  runs of different length gave a false "36.8 FPS" before this was noticed. It also resets the stats at
  every step, which is why the first-frame freeze never shows up in its output.
- **Reverted:** a speculative streaming-VBO change in `cs2d_gl.c` (fresh write offset per batch +
  orphan on wrap). It left the vsync-on frame time unchanged, so the "BufferSubData stall" hypothesis
  was wrong; kept out rather than keeping an unproven rewrite.
- **Verified:** `Build Successful!`, `test_cs2d.exe` 33 / 34 (the known `test_file_transfer`
  "Transfer list" gap). `git status sys/` clean.


- **Changed:** the OpenGL swap interval is now a real setting instead of the hard-coded
  `SDL_GL_SetSwapInterval(1)`. Added `cs2d_render_set_vsync` / `cs2d_render_get_vsync`
  (`src/cs2d_render.c`, `include/cs2d_render.h`), a `vsync` field in `cs2d_config_t` (`sys/config.cfg`
  "vsync", default 1, `DAT_140a12960`) and the `vsync [0|1]` console command (a bare `vsync` toggles)
  in `src/cs2d_client.c`, with a `help` line. `cs2d_client_init` applies the config value at startup.
- **Verified:** `test_vsync_command` in `tests/test_cs2d.c` (config parse, getters/setters, the command
  and the bare toggle). Full suite 33 / 34; the only failure is the known `test_file_transfer`
  "Transfer list" gap (see `OPEN_ISSUES.md`).
- **Left:** the options menu still does not expose VSync (only the console and `sys/config.cfg` do).

## 2026-10-12 — Cline — GL 1.x removal, phase 3: the GL 3.3 core + shader backend

- **Decision (user):** the fixed-function renderer is replaced by **GL 3.3 core + shaders**, not
  `SDL_Renderer` (the alternative my plan recommended). One SDL3 dependency is kept; the whole GL 3.3
  entry-point set is still loaded from `SDL_GL_GetProcAddress` (opengl32.dll only exports GL 1.1).
- **New `src/cs2d_gl.c` + `include/cs2d_gl.h`:** loads the 3.3 pointers, builds one shader
  (`frag = texture(u_tex, uv) * vertex colour`, with an alpha-test `discard` that stands in for
  `glAlphaFunc`) and one streaming VAO/VBO. The public API is a batcher (`cs2d_gl_quad`,
  `cs2d_gl_quad_rot`, `cs2d_gl_quad_corners`, `cs2d_gl_rect`, `cs2d_gl_rect_grad`, `cs2d_gl_line`,
  `cs2d_gl_circle`, `cs2d_gl_push_*`), sticky state (`set_blend` 1 alpha / 4 add / 5 mult / none,
  `set_texture`, `set_alpha_ref`, `scissor`) and the light engine's render targets
  (`bind_target`/`unbind_target`). A 1x1 white texture makes a textured draw with `tex = 0` a solid
  colour, so one shader covers everything.
- **`cs2d_render.c`:** `init` calls `cs2d_gl_load()` and fails cleanly when the context is not 3.3;
  `resize`/`begin_frame`/`end_frame` drive the backend; the 8 `glBegin` sites (rect, line, circle,
  sprite, tile frame, text, gradient, rotated FX quad) and the depth + `glAlphaFunc` player-shadow
  silhouette were rewritten on the batcher (the silhouette is now `cs2d_gl_shadow_mark_begin` /
  `_mark_end` / `_fill`); the map loop and `cs2d_render_tile_frame` pass the texture per quad instead
  of binding it, so the tilemap batches into one draw call.
- **`cs2d_light.c`:** the Max2D layer's `m2d_*` calls push into `cs2d_gl`; the FBO loader lost its
  `EXT` fallback (FBO/blendFuncSeparate are core now) and no longer carries `glBlendFuncSeparate`
  (the backend owns blending). **The `s_trace` path is untouched, so the emulator comparison still
  runs GL-free.**
- **`main_client.c`:** `SDL_GL_CONTEXT_PROFILE_CORE` 3.3 (keeps 4x MSAA + depth 24); the screenshot
  reads `GL_RGB` and swaps to BGR (no `GL_BGR_EXT`). `build.bat` compiles `src/cs2d_gl.c` in both gcc
  lines.
- **Verified:** `Build Successful!`; `test_cs2d.exe` prints `Tests Passed: 32 / 33` — the only failure
  is the known `test_file_transfer` "Transfer list" (OPEN_ISSUES). The light engine test still passes
  its 4 scenes against the original (`_global` trace is GL-free). Screenshots via `--capture`
  render real content: `map prison_funst3r` -> 850x480, 68002 distinct colours, mean (27.2, 32.6, 50.5);
  `map fun_roleplay;light 5` -> 18010 colours, mean (34.5, 30.7, 24.3) (frame time rises to ~43 ms with
  the light engine on, as expected). No `cs2d_gl` missing-entry-point or shader-compile errors on stdout.
- **Left:** a screenshot diff of the new core output against the *original* client on the same scene is
  still owed (OPEN_ISSUES); the FX `glBlendFunc` dead code in weapon-select was removed, and the
  `windows.h` in `include/cs2d_render.h` still waits for phase 2 (the GDI+ text engine).

## 2026-10-12 — Cline — Removing the GDI+ dependency, phase 1: image decoding
- **Changed:** new `src/cs2d_image.c` + `include/cs2d_image.h` decode every asset (BMP / PNG / ICO) with the
  vendored public-domain `third_party/stb_image.h`; `build.bat` compiles it, adds `-Ithird_party` and no longer
  links `-lgdi32`. `cs2d_render.c` lost the `GdipCreateBitmapFromFile` / `GdipCreateBitmapFromStream` /
  `GdipGetImageWidth` / `GdipGetImageHeight` / `GdipBitmapGetPixel` / `GdipDisposeImage` bindings and the
  `shlwapi` `SHCreateMemStream` trick used for `.ico` (`upload_gdiplus_bitmap` → `upload_decoded_image`,
  `load_image_via_gdiplus` → `load_image_portable`, `load_ico_largest_frame` rewritten on `cs2d_image`). The
  hand-rolled 24/32-bit BMP fast path, the mask modes and the transparent-RGB dilation are untouched, so no
  uploaded pixel changes. GDI+ now only starts for the TrueType text engine.
- **Verified:** `Build Successful`; `test_cs2d.exe` prints `Tests Passed: 32 / 33`, the single failure being the
  already known `test_file_transfer` "Transfer list" (OPEN_ISSUES). New `test_image_decode` decodes 9 assets
  covering 8-bit RGBA / 8-bit palette / 4-bit palette PNG, PNG-data-inside-`.bmp`, 24-bit, 32-bit and 8-bit
  palette BMP and the `cs2d.ico` container, and matches an independent decode byte for byte (FNV-1a clip over
  R,G,B,A). `objdump -p test_cs2d.exe` now lists only KERNEL32, msvcrt, OPENGL32, libwinpthread-1, SDL3 and
  WS2_32 — no gdi32.
- **Regression found and fixed in the same session:** the first build of this change died on launch with
  *"libwinpthread-1.dll não foi encontrado"* (`0xC0000135`). Cause: the vendored `stb_image.h` keeps some
  settings in `__thread` (`STBI_THREAD_LOCAL`), and GCC implements `__thread` on MinGW through emutls, whose
  runtime is built on the winpthreads API — `objdump -p` showed the exe importing `pthread_once` /
  `pthread_mutex_*` / `pthread_key_*` / `pthread_getspecific` from `libwinpthread-1.dll`. A scan of `src/` +
  `include/` + `tests/` found no `__thread` / `thread_local` anywhere else, so the import was new. Fix:
  `#define STBI_NO_THREAD_LOCALS` before the `stb_image.h` include (single-threaded client, never calls
  `stbi_set_*_thread`). After rebuilding, both executables import only KERNEL32 / msvcrt / OPENGL32 / SDL3 /
  WS2_32, and `test_cs2d.exe` runs correctly with `PATH` reduced to `%SystemRoot%`.
- **Left:** the text engine is the last GDI+ user and the GL 1.x side is untouched; both are planned in
  OPEN_ISSUES "GL 1.x / GDI+ removal". `third_party/` is new and must be committed with the sources.

## 2026-10-12 — Cline — Web/Emscripten port feasibility (second opinion, docs only)
- **Changed:** these three shared-memory docs only (`WORKLOG.md`, `KNOWLEDGE.md`, `OPEN_ISSUES.md`). No source, build
  or asset file was touched.
- **Question:** is an Emscripten/WebAssembly port viable? This is the *second* opinion the DeepSeek entry below
  (2026-10-12, "Web/Emscripten feasibility report") asked for; an independent session re-measured the tree instead of
  re-labelling the first report. Same overall verdict, now with numbers.
- **Verified (measured, not estimated, `src/` + `include/`):** the platform surface is small and concentrated —
  **no threading** (`SDL_CreateThread`/`pthread`/`CreateThread`/`Sleep` = 0), **no audio at all** (no
  `SDL_Audio`/`Mix_`/`PlaySound`; only `cfg->sound_volume` at `cs2d_config.c:15,59` and the wav/ogg transfer filters at
  `cs2d_transfer.c:131`), stdio-only file I/O (`fopen` 21, `fwrite` 29, `fclose` 31, `_mkdir` 1 in 13 files), directory
  listing only for `maps\*.map` (`cs2d_client.c:2121`, `cs2d_scene_editor.c:140`), UDP-only sockets in 3 files,
  `GetTickCount` 31 uses, `VK_*` 13 codes, ~118 SDL references over 25.5k lines, and **0 shader calls** — the renderer is
  100% OpenGL 1.x fixed-function. Per-file lists, asset weights and the toolchain citations are in `KNOWLEDGE.md`
  "Web/Wasm portability".
- **Verdict:** viable for the C99 client; the deciding risk is the **render/font backend**, not the game logic.
  Emscripten documents exactly the mode we need (`-sLEGACY_GL_EMULATION` + `-sGL_FFP_ONLY`, "your code will not use the
  programmable pipeline/shaders at all"), and names gl4es / Regal as fallbacks. Online play needs a **server-side UDP
  gateway**: "Direct UDP communication is not available in browsers" and the documented POSIX-socket proxy is itself
  "slow" and aimed at test infrastructure. The original `CS2D.exe` is not a port target (Emscripten compiles source,
  not PE).
- **Left:** the phases and the open decisions are in `OPEN_ISSUES.md` "Web port (Emscripten)". Phase 0 (emcc build of
  the Win32-free core + `test_cs2d.exe` under Node) is the next step and runs in a separate context. Any structural work
  goes on `feat/emscripten-poc`, never `master` (parallel session, merge risk).

## 2026-10-12 — Claude Code — Light engine (TSmartLighting)
- **Changed:** new `src/cs2d_light.c` ports the original light engine: day shadows of walls, obstacles, high walls and
  room roofs along the sun, player silhouettes, Env_Room / Env_Light / Env_LightStripe, toning, per-light buffers with
  wall shadows and penumbra, floor occlusion, light sprites (Env_Sprite, alarms, Trigger_Use glow, leaves) and
  `ObstacleShadow2`. Settings `light` / `mapshadows`, server `sv_forcelight` / `sv_daylighttime` (`[252,6]`, `[237,19/20]`),
  console `light`, `mapshadows`, `daylight`, `lightdump`. Trigger_Use buttons are drawn. `CS2D_MAX_ENTITIES` 16384
  (fun_roleplay has 9901 entities).
- **Verified:** the original's functions run in Unicorn (`tools/agent/emu_light.py`); our traces are identical on
  fun_roleplay, de_dust2, de_inferno, as_mars, de_vantage and cs_assault, light 1..5, mapshadows 2..4, sun 30..300.
  `test_light_engine` locks 4 traces. Screenshots against the original on fun_roleplay: 91..94 % of pixels within 8 by
  day, 94 % at night (the rest is items, HUD and timing). About 1.5 ms per frame.
- **Left:** flashlights, particle / building / projectile light sprites and shadows, editor lighting (OPEN_ISSUES).

## 2026-10-12 — Cline — Advanced radar (minimap) fidelity pass
- **Changed:** the top-left HUD radar in `cs2d_render.c` now renders the real tileset instead of hardcoded
  brown/gray tile colours. `ensure_minimap_texture` builds a cached full-map minimap (2 px/tile, box-averaged via
  `tileblend_find`) and crops it around the player's exact pixel position, so it slides smoothly instead of
  snapping per tile (16 world px per radar px, matching `TSmartRadar`). Added player blips: the local player red,
  teammates in team colour (T light red 0xff9696, CT light blue 0x9696ff), enemies hidden outside Deathmatch.
  `cs2d_render_hud` gained a `tileset_tex` parameter (wired through `cs2d_scene_game.c`).
- **Verified:** `gcc -fsyntax-only` clean; the regression suite still passes 30/31 (the one failure is the
  pre-existing `test_file_transfer` "Transfer list" gap, unrelated). Full `build.bat` step [1/2] (test suite)
  linked; step [2/2] was cut short by the terminal, but it compiles the same sources plus `main_client.c`.
- **Left:** the full-screen `TSmartMinimap` overlay (big map) is still not reconstructed; the radar minimap does not
  apply tile rotation/brightness (negligible at 2 px/tile). Recorded in `KNOWLEDGE.md`.

## 2026-10-12 — Antigravity — Scoreboard ping display & Opcode 229 clock ping reply
- **Changed:** implemented client reply to `CS2D_NET_CLOCK_PING` (opcode 229 / 0xE5) in `src/cs2d_client.c`.
  The original dedicated server sends opcode 229 when `sv_antiaccel > 0` (default) every 2 ticks (~2 s) with
  `[229, srv_ts i32]`. The client now responds with `[229, srv_ts i32, cli_ts i32]` (9 bytes, unreliable datagram,
  per `CS2D.exe` `0x1401d7690` and `0x1402e2ed0`) where `cli_ts = GetTickCount()`. With this reply, the server
  calculates RTT latency (`now - srv_ts`) and transmits live player pings via `CS2D_NET_PINGLIST` (opcode 247).
- **Verified:**
  - Dedicated server capture `tools/agent/capture_ours.sh` (`proxy_pingtest.log`) confirmed the exchange: server sent
    `e5 af 17 00 00`, our client replied `e5 af 17 00 00 19 46 12 00`, and the dedicated server sent `f7 03 ... 03 10 00`
    broadcasting our player ID 3 with latency 16 ms (0x0010).
  - Regression tests in `tests/test_cs2d.c` for `[247]` ping assignment and `[229]` 9-byte reply roundtrip.
  - Test suite passes (30/31, same as baseline).
- **Left:** None for this item.

## 2026-10-12 — DeepSeek — Web/Emscripten feasibility report (read-only, no code changed)
- **Changed:** nothing. Read-only feasibility review at the user's request, deliberately without editing any source
  or doc file to avoid conflicting with the parallel session (Claude Code). Report only.
  Note on authorship: this entry is by the same agent (DeepSeek) as the "Lua language-server config" entry below;
  the agent names in this log are session/tool labels, not separate identities.
- **Question:** is an Emscripten/WebAssembly port of this reimplementation viable?
- **Evidence scanned:** `build.bat`, `main_client.c`, `cs2d_render.c`, `cs2d_network.c`, `cs2d_transfer.c`,
  `include/cs2d_client.h`, `include/cs2d_serverlist.h`, `include/cs2d_render.h`, `.github/workflows/build.yml`,
  `src/` + `include/` size (46 files, ~22.8k lines), targeted grep for Win32 / GDI+ / OpenGL-fixed-function symbols.
- **Findings (facts):**
  - **Render:** 100% legacy fixed-function OpenGL 1.x throughout `cs2d_render.c` / `cs2d_scene_*` (`glBegin/glEnd`,
    `glOrtho`, `glPushMatrix/glRotatef/glTranslatef/glScalef`, `glColor4f`, `glAlphaFunc`, `glReadPixels(GL_BGR_EXT)`,
    manual mipmaps, `glEnable(GL_MULTISAMPLE)`). WebGL has no fixed-function pipeline -> either a fixed-function->WebGL
        shim or a shader/VBO rewrite is required. Highest-effort item.
  - **GDI+ is the only image decoder AND the only text/font engine** (`GdipCreateBitmapFromFile`,
    `GdipCreateBitmapFromStream` for `.ico`, `GdipCreateBitmapFromScan0`, `GdipNewPrivateFontCollection` +
    `GdipPrivateAddFontFile` for `gfx/fonts/liberationsans.ttf` / `sourcesans.ttf`, `GdipDrawString` /
    `GdipMeasureString` feeding a 512-entry GPU string-texture cache). No non-Windows fallback; the system-font
    fallback (`Segoe UI` / `Arial`) is Windows-only. Second-highest effort.
  - **Winsock / windows.h are in public headers:** `cs2d_client.h` (`#ifdef _WIN32`, `SOCKET`, `sockaddr_in`) and
    `cs2d_serverlist.h` (`#include <winsock2.h>` **without a guard**). Runtime uses `WSAStartup`, `ioctlsocket`,
    `sendto`, `recvfrom`, `closesocket`, `SO_BROADCAST`. CS2D is UDP-only; browsers have no raw UDP, so a
    WebTransport/WebRTC-to-UDP gateway on the **server** is required for online play. Major architectural cost.
  - **Win32 I/O and system:** `FindFirstFileA` (map listing in `cs2d_editor.c`, `cs2d_client.c`),
    `CreateDirectoryA` (`main_client.c`), `GetTickCount()` (UI, serverlist, render), `MultiByteToWideChar`,
    `LoadLibrary` / `GetProcAddress`.
- **Findings (portable):** the C99 core (`cs2d_types`, map, entities, player, weapons, UI) and the whole
  protocol/crypto layer (`cs2d_network.c`: MD5, SHA-256, shift cipher, Tudp builders) are Win32-free; zlib
  (transfer) works in wasm; SDL3 (window/input) has first-class Emscripten support.
- **Assessment:** technically viable but NOT a recompile. It needs a platform-abstraction layer (HAL) that does not
  exist today, plus a render/font backend and, for online, a server-side UDP gateway. Suggested phases: (A) HAL for
  time/dir/file (low/med), (B) render + text/image backend (high, project-deciding), (C) socket abstraction + UDP
  gateway (high, client+server, required only for online), (D) asset preload via `--preload-file`. Lowest-risk first
  step: an Emscripten build of only the Win32-free logic (network/map/entities/player/weapons) to measure portability
  without touching render/network.
- **Risks:** visual/byte fidelity is a project invariant (the `--capture` harness compares frames/screenshots and the
  proxy compares packets byte-for-byte); a render/network rewrite risks violating it, so keep the desktop path intact
  and add a web backend beside it. Also: any structural refactor has high merge-conflict risk with the parallel agent,
  so do it on an isolated branch (`feat/emscripten-poc`), not on `master`.
- **Left (requested):** the user asked for a second opinion, but it must come from a *distinct* session/tool
  (e.g. the parallel Claude Code session, or an independent review), not from this agent re-labelling itself as a
  third party. Open items to size: exact per-file inventory of Win32/GDI+ symbols; asset weights (`gfx/`, `sfx/`,
  `maps/`); whether the GL extensions used (`GL_BGR_EXT`, `GL_MULTISAMPLE`) survive on WebGL; fixed-function
  emulation fidelity against the screenshot tests.

## 2026-10-12 — Antigravity — In-game Action Progress Bar (Reloading, Planting, Defusing)
- **Changed:** reconstructed the in-game action progress bar from CS2D.exe `FUN_140295190` (`0x1402958f0..0x140295b70`).
  Displays centered at `y = screen_h / 2 + 80px` with width 102px and height 20px, 1px outer frame, 2px padding,
  98x16 inner filled rectangle, warm gold/amber `(255, 200, 0)` at alpha 0.75, soft black shadow underneath
  (`incbindata/shadow.bmp` scaled 11x5 at alpha 0.40). Supports "Reloading" (`sys/language 0759`), "Planting Bomb"
  (`0760`), and "Defusing Bomb" (`0761`). Synchronized `is_reloading` in `cs2d_weapons.c`, `cs2d_player.c`, and
  `cs2d_client.c`.
- **Verified:** `test_hud_action_progress_bar` added to `tests/test_cs2d.c` (passes 31/31 suites, verifying layout,
  exact 102x20 geometry, centering, inner dimensions, progress calculations, and weapon filters). Visually matched against
  the user screenshot.
- **Left:** nothing; integrated directly into `cs2d_render_hud`.

## 2026-10-12 — DeepSeek — Lua language-server config
- **Changed:** added `.luarc.json` (Lua 5.1, `workspace.library = meta`, the CS2D command/hook/global list in
  `diagnostics.globals`, and a few noisier diagnostics disabled) and `meta/cs2d.lua`, EmmyLua stubs for the CS2D
  Lua API derived from `sys/lua/info.txt`, `docs/cs2d_help_db.json` and the shipped scripts. Editor-only; not built
  or executed. The shipped scripts under `sys/lua/`, `maps/` and `bots/` no longer report undefined globals and get
  autocomplete for `addhook` / `player` / `entity` / `parse` / `ai_*`.
- **Verified:** `.luarc.json` parses; the stubs cover every command in the scrape plus the `wrapper` / `sample` globals.
- **Left:** nothing; the config lives at the workspace root and covers the whole tree.

## 2026-10-12 — Claude Code — Env_Object and tile blending
- **Changed:** Env_Object is now a port of `Te.draw`: Sink / Toilet on the ground pass (frames 0 / 1, we used type - 2),
  Palm / Tree with the original sway and leaf shadows, Alarm (flare1 pulse), Street Lights (flare3 + lamp frame 2); size,
  alpha and color are used as stored (we forced 0 to 1 / white, so fun_roleplay's street lights showed a flare the
  original does not draw). Tile blending (`Ttileblend`): blended images built from the tileset pixels and
  `gfx/blendmap.bmp` into a per-tileset atlas the first time they are drawn. The editor now draws the overhead pass too
  (the original editor loop calls the same map draw). `tex_lamp1` renamed `tex_objects`.
- **Verified:** `test_tile_blending` (direction table, blendmap orientation, mix, de_aztec frames); screenshots of
  fun_roleplay at 2|24 and 119|150 next to the user's screenshots of the original, prison_funst3r sinks / toilets /
  palms / trees. 29 / 30 tests: `Transfer list` also fails without these changes (the untracked `maps/fun_roleplay.map`
  and `gfx/c4/` from a download make the list request different files).
- **Left:** see OPEN_ISSUES "Env_Object and tile blending".

## 2026-10-12 — Claude Code — projected player shadow
- **Changed:** at the user's request, `cs2d_render_player` casts the player's silhouette (legs, torso, weapon in black)
  along the map sun instead of the `shadow.bmp` blob of the original `Tpl.draw`. Second round: offset reduced (h 6 px,
  ~3.6 px per axis) and two-pass depth mask + one quad, which fixed the legs' semi-transparent outline showing over
  the torso shadow. Hostages keep the blob.
- **Verified:** build, 29 / 29 tests, screenshots on de_dust2 (standing and walking).

## 2026-10-12 — Claude Code — Scoreboard (Tab)
- **Changed:** `src/cs2d_scoreboard.c` (port of `Thud_scoreboard`), shown while `showscores` (Tab) is held. Network:
  `[19]` kill scoring (byte 3 is the assist, byte 4 the weapon), `[22]` round end reason -> team scores, `[64]`, `[247]`
  ping list (was unhandled and dropped the rest of the datagram), `[42]` fields 1 / 4 / 9 / 10, `[252,6]` team scores,
  `[252,7,1]` assists / MVPs. `cs2d_game_init` leaves player slots empty (id 0); offline bots have `bot` set.
  Harness step `!hold <key> down|up`.
- **Verified:** `test_scoreboard` (layout, order, kill and round reason scoring, timers); screenshots offline and against
  the dedicated with two bots (`[247]` = `f7 02 25 0000 0000 01 fffe 02 fffe`).
- **Left:** see OPEN_ISSUES "Scoreboard"; not compared with a screenshot of the original.

## 2026-10-12 — Antigravity — Player and ground item shadows
- **Changed:** implemented authentic player, hostage, ground item, and ground projectile drop shadows:
  - Added shadow cvars and config parameters: `shadows` (0..100), `playershadows` (0|1), `itemshadows` (0|1), `mapshadows` (0..4) (`include/cs2d_config.h`, `src/cs2d_config.c`).
  - Added shadow control and querying API: `cs2d_render_set/get_shadow_intensity`, `cs2d_render_set/get_player_shadows`, `cs2d_render_set/get_item_shadows` (`include/cs2d_render.h`, `src/cs2d_render.c`).
  - Added `tex_shadow` texture loading (`incbindata/shadow.bmp`, maskMode 3) to `cs2d_gfx_bank_t`.
  - Player shadows (`Tpl.draw 0x140121160 / 0x140122298`): rendered centered at player `(sx, sy)` including recoil backward kickback, scale 1.5 (48x48 px), color black `(0,0,0)`, alpha `player_alpha * (shadows * 0.01f)` (0.50 default, 0.10 stealth suit).
  - Hostage shadows (`Thostage.draw 0x140147090`): scale 1.5 (48x48 px) centered at hostage position.
  - Ground item shadows (`Titem.draw 0x1401483a0`, `Te.draw 0x140260000`): 32x32 circular soft shadow from `shadow.bmp` at `(x, y)` plus 1px offset silhouette drop shadow from the weapon sprite at `(x + 1.0f, y + 1.0f)` in black with alpha `shadows * 0.01f`.
  - Ground projectile shadows (`Tpro.draw 0x140288702`): 32x32 circular shadow at `(x, y)` with rotation `p->rot` plus 1px offset silhouette drop shadow at `(x + 1.0f, y + 1.0f)`.
  - Interactive console commands: `shadows [0..100]`, `playershadows [0|1]`, `itemshadows [0|1]` in `cs2d_client.c`.
- **Verified:** dump disassembly of `Tpl.draw`, `Titem.draw`, `Te.draw`, `Tpro.draw`, `Thostage.draw`; added `test_item_and_player_shadows` regression test in `tests/test_cs2d.c` (28 / 28 passed).

## 2026-10-12 — Antigravity — Projectiles and Persistent Area Fields (Tpro, Tfield)
- **Changed:** implemented full projectiles and fields system (`include/cs2d_projectile.h`, `src/cs2d_projectile.c`):
  - Projectile classification (throwables vs. rockets vs. impact weapons).
  - Throwable consumption with 2 charges for Flashbang (1 clip, 1 reserve) and 1 for others (item stripped on throw).
  - Flight simulation with wall ricochet (bouncing grenades reflect velocity, reduce flydist and speed, reverse spin).
  - Impact detonation on first obstacle/wall/player for Rockets, RPG, Molotov, Snowball, Gut Bomb.
  - Cosmetic effects and persistent area fields (`CS2D_FIELD_FIRE`, `CS2D_FIELD_GAS`, `CS2D_FIELD_SMOKE`) with smoke cover detection (`cs2d_projectile_smoke_cover`).
  - Added particle FX: smoke puffs, fire puffs, sparkles, blood splats, snow puffs (`cs2d_fx.c`, `cs2d_render.c`).
  - Loaded projectile textures (rocket, rpg, grenade, molotov, flare, gutbomb, airstrike, flashbang, smokegrenade, gasgrenade, snowball) with fallback to ground weapon sprites.
  - Client networking & dispatch: Client->Server opcode 27 (`CS2D_NET_PROJECTILE`), Server->Client opcodes 27, 41, 52, 55, 58, 63, and snapshot chunk 6 (`[252, 7, 6]`).
  - Integrated into game loop, firing logic, rendering passes (ground and flying) and state resets.
- **Verified:** dump inspection of `Tpro.c` and opcode handlers in `1400a0000.c`, `1402d0000.c`, `1402e0000.c`; added `test_projectiles_and_fields` to test suite (`test_cs2d.exe` 27 / 27 passing).
- **Left:** Satchel charge intentionally deferred per user instruction.

## 2026-10-12 — Claude Code — player hover text
- **Changed:** hovering another player shows its name ("Friend: " / "Enemy: " in a team or deathmatch) and "hp%"
  (`render_player_hover` in `src/cs2d_scene_game.c`, `cs2d_player_hover_kind` in `src/cs2d_player.c`), from the player
  branch of `FUN_1402a12d0`; it takes precedence over the building hover.
- **Verified:** dump; regression in `test_construction` (friend / enemy / neutral, enemy health rules); our client on
  the offline dedicated with the reworked `tools/agent/lua/hover_probe.lua` shows "Enemy: [b]LUL" in red, no health.
- **Left:** smoke cover, fog, click-to-spectate, table 0x140a015d0 (OPEN_ISSUES).

## 2026-10-12 — Claude Code — building hover health
- **Changed:** hovering a building shows its name / "$1 by $2", "n%" health and "Upgrade n%" (`render_building_hover`
  and `hover_text` in `src/cs2d_scene_game.c`, `cs2d_dynobj_hovered` / `cs2d_dynobj_hover_health`), ported from
  `FUN_1402a12d0` / `FUN_14024fb10`. `mp_hovertext` is read from `[252,6]` and `[237,16]` (`net_hovertext`).
- **Verified:** dump + objdump (strings "%", " ", ": " at 0x140a6dd00 / 0x140a71020 / 0x140a66160); proxy log of
  `[252,6]` (hovertext 31); regression in `test_construction`; our client on the offline dedicated with
  `tools/agent/lua/hover_probe.lua` shows "Turret" in CT blue and "100%" in green.
- **Left:** screenshot of the original; player / hostage / item hover texts (OPEN_ISSUES).

## 2026-10-12 — Claude Code — fire rate twice too fast
- **Changed:** `fire_delay` converts firedelay ticks at 39 ms (`Tpl.attack` `firetimer.reset(firedelay * 0x27)`,
  0x140216b8f) instead of 20 ms, so every weapon fired ~1.95x faster than the original. Knife / Claw right click
  waits 1200 ms (0x1401fdde8) instead of 0.42 s.
- **Verified:** dump + objdump (`Ttimer.reset`, imul 0x27); regression in `test_weapons` (AK-47 117 ms, AWP 2535 ms).
  Not yet measured on a proxy capture (interval between `[7]` of the original).
- **Left:** whether semi-automatic weapons need a new click in the original (we fire while the button is held).

## 2026-10-12 — Claude Code — building hit debris
- **Changed:** shots (local and remote) that reach a building throw gray fragments both ways
  (`cs2d_fx_building_hit`, `cs2d_dynobj_hit_debris`, `building_hit_debris` in `src/cs2d_scene_game.c`), ported
  from `Tshoot.Shoot` 0x1402073ed -> `Tp.breakable` material 1 -> `Tp.matp`. Particles got a per-particle `drag`.
- **Verified:** REA / dump / objdump only (type mask 0x9b80, matp constants Rnd(0.2, 3), Tp type 12 update);
  regression in `test_construction`. Not yet compared with a screenshot of the original.
- **Left:** see OPEN_ISSUES (hit sounds, wall stop of fragments, screenshot check).

## 2026-10-12 — Claude Code — chat send/receive verified on the remote server
- **Changed:** no code change. Live end-to-end check of the `[240]` (`CS2D_NET_MSG2`) chat path against
  `140.238.183.194:36969` (user-authorized), driven by `tools/agent/udp_proxy.py` + `cs2d_client.exe`
  with `CS2D_NET_LOG=1` and the console `say` / `sayteam` commands (the `!press Y` harness uppercases the
  key and does not open the prompt, so it is not usable for this).
- **Verified:** the two chat datagrams are byte-correct and round-trip:
  - send `[240, mode 1, len 0x17, UTF-16LE]` and `[240, mode 2, len 0x0c, UTF-16LE]`;
  - server echo `[240, 01 (sender_id), mode, len, ...]` decoded to `*DEAD* real: ...` and
    `*DEAD*(TEAM) real: ...` in the chat log.
  `sys/recentlyjoined.lst` was backed up and restored (rule 5). The user reported the original
  "missing messages" were the server's anti-flood during parallel-client testing, not a client bug.
- **Left:** confirm/set aside the pre-existing `sys/core/svl.cache` modification (not from this session);
  the two latent dispatcher risks (`break` on an unknown opcode discarding the rest of the datagram, and
  the `[226]` trailing-byte length assumption) stay unverified in `OPEN_ISSUES.md`.

## 2026-10-12 — Antigravity — In-game chat, server messages (@C) & Opcode 240
- **Changed:**
  - Added in-game chat log layout matching CS2D original (`Thud_msg.draw_chat` 0x14024d8a0): left aligned at `x = 3.0f`
    with black drop shadow at `x = 4.0f, y + 1.0f` at alpha `fade`, baseline starting at `screen_h - 85 - 2*line_h` stacking
    upwards with 10 s duration and fade-out decay. Top ceiling at `y = 180.0f` prevents overlap with top radar.
  - Interactive chat input prompt (`FUN_14029f7b0`) on 'Y' (`Say : `) and 'U' (`Say (Team) : `) or `say`/`sayteam` commands.
    Draws alpha 0.4 black background box, blinking vertical cursor, freezes movement/attack input while open, and sends on Enter.
  - Fixed `sdl_key_to_cs2d_key` in `src/main_client.c`: previously only W, S, A, D, R, E, B, G, M, C were mapped. Added
    generic letter mapping (`SDLK_A..SDLK_Z` -> `'A'..'Z'`), keypad digits, Tab (`VK_TAB`), Backspace (`VK_BACK`), Enter, Escape.
  - Added trigger key suppression (`chat_just_opened`): consumes the initial 'y' or 'u' key event from text input so the chat prompt starts empty.
  - Server message routing (`FUN_1401de400`): messages ending with `@C` or `@c` (or mode == 2) strip the `@C` tag and route
    to `cs2d_client_center_msg` (screen center at `h / 2 - 10*n - 50` with black drop shadow at `+1, +1`). Other server messages
    route to HUD chat log. All messages echo to client console.
  - Color tag parser: `\xA9RRRGGGBBB` (single-byte 0xA9 or UTF-8 `\xC2\xA9` + 9 ASCII digits) parses RGB and strips prefix.
  - Team colors (`tintTeamColor` 0x140083a40): Spec/Server `(255, 220, 0)`, T `(255, 25, 0)`, CT `(50, 150, 255)`. Dead
    player formatting with `*DEAD* ` and `*DEAD*(TEAM) `.
  - Opcode 240 (`CS2D_NET_MSG2`): client send `[240, mode, len_u16, len*2 bytes UTF-16LE]`, server recv/broadcast handler.
- **Verified:** `test_chat_and_server_messages` in `tests/test_cs2d.c` (26 / 26 passed), verifying color code parsing,
  `@C` tag detection & stripping, server routing to center vs. chat, player team formatting, opcode 240 serialization, and
  interactive chat prompt operation (Y/U trigger, clean text input consumption, backspace, enter submit, escape close).

## 2026-10-12 — Antigravity — Chunk-based shadow map & incremental invalidation

- **Changed:** `src/cs2d_render.c`: Replaced full-map monolithic shadow texture generation with a 16x16-tile chunk grid
  (256x256 texels per chunk). Replaced dynamic `calloc`/`free` of 34+ MB per frame with static scratch buffers and
  cached `eff_modes` / `eff_heights` tables. Fast delta tracking checks which `Func_DynWall`, `Env_Breakable` or
  `object_shadow` tile changed and marks only localized chunks dirty (typically 1..4 chunks of 42+). Lazy texture
  generation and viewport frustum culling ensures only visible dirty chunks generate OpenGL textures with `glTexSubImage2D`.
- **Verified:** `test_shadow_chunks` added to `tests/test_cs2d.c` (25 / 25 passed), confirming 101x88 de_dust2 grid
  geometry (7x6 chunks) and that a solid tile modification marks only localized chunks (1..4) dirty instead of 42.

## 2026-10-12 — Claude Code — Buy menu
- **Buy menu** (`src/cs2d_hudmen.c`): `Tgui_hudmen` menus 20..26 from the dump: sections, T / CT items, disabled
  Defuse Kit / shield, `_k` images on buttons, rotating `_m` section previews, `weaponinfo` description with the client's
  `init_weapons` stats, money box. `buy` / `buyequip` / `buyammo1` / `buyammo2` go through `sys/controls.cfg`
  (`B`, `C`, `,`, `.`); the hardcoded `B` shortcut is gone. The "Construct" menu now uses the shared `Tgui_hudmen` button.
- **Tools:** `tools/agent/weapontables.py` recovers the per-weapon `init_weapons` tables (also the muzzle offsets).
- **Verification:** `test_buy_menu` (layout, previews, info lines, keys and Back click); screenshots of our client on
  de_dust2 offline. Not compared with the original client yet, and not run against the dedicated.

## 2026-10-12 — Claude Code — weapon selection HUD and bindings
- **Changed:** invnext / invprev / slotN logic and the selection HUD ported from `CS2D.exe` (KNOWLEDGE "Weapon selection
  HUD"); attack / attack2 with the HUD open holds the item. New `src/cs2d_bind.c` reads `sys/controls.cfg` (defaults
  mwheelup invprev, mwheeldown invnext, 0..9 slots); console `bind`, `invnext`, `invprev`, `slotN`. The mouse wheel now
  always reaches the client (in game it runs the bound command). Config reads `hud_fastswitch`, `drawradar`, `radarsize`.
- **Verified:** screenshots of the original (`wsel_probe.lua`, new `wheel:n` step in `drive_orig.ps1`) against ours:
  same positions, colors and texts; the proxy shows `[9, type, 0]` on the confirming click and nothing on the wheel.
  `test_weapon_select` (23 / 23).
- **Left:** see OPEN_ISSUES (one item per inventory slot, INF / Remote texts, sounds, other bindings).

## 2026-10-12 — Claude Code — building shadows, build helper alpha, additive explosion
- **Changed (user requests, not checked against the original):** solid buildings (walls, Dispenser, turrets, supplies;
  `cs2d_dynobj_casts_shadow`) mark `map->object_shadow` and cast a 32 px wall shadow in the shadow map; the build helper
  alpha is floored at 0.35 (`0.35 + 0.65 x sin`); the `gfx/explosion.png` effect draws additive (LIGHTBLEND).
- **Verified:** `test_construction` checks the shadow overlay for the turret and Wall III (22 / 22).

## 2026-10-12 — Claude Code — Construction mode, dynamic objects, CTF flags
- **Changed:** new `src/cs2d_dynobj.c` (Tdo list, type table, `[35]` `[36]` `[37]` `[49]`, snapshot sections 3 and 5,
  collision overlay `map->object_collision`, `FUN_14014ef60` build rules); client `[34]` build result + red center
  messages, `[19]` kill, `[22]` frees objects; render of every building layer, build helper, "Construct" menu (Wrench
  right click twice), respawn click `[39]` + countdown, waving CTF flag and carried flag; Env_Building is now spawned as
  dynamic objects offline. New `tools/agent/bbscope.py`, `lua/construct_probe.lua`, harness `rdown`/`rup`.
- **Verified:** `test_construction` (22 / 22) with the 56-object snapshot recorded on 140.238.183.194:36969; our client
  against the offline dedicated (`sv_gamemode 3`, ctf_cs2dfortress): respawn by click, objects from `[49]` drawn, build
  request byte-identical in format, server built Barricade -> Wall I -> Wall II with Wrench hits (screenshots).
- **Left:** see OPEN_ISSUES "Construction".

## 2026-10-11 — Claude Code — spectator mode
- **Changed:** spectator camera and HUD (`src/cs2d_spectate.c`, `cs2d_scene_game.c`): free look and follow, the
  target picker, `[67]` and `[32]`, `space` / `leftctrl` (`specplayer` / `specmode` commands), the join
  showcase, sv_specmode from `[252,6]` and `[237]`. The join snapshot section 1 (players already in game, names
  deciphered) is now read, so bots and earlier players exist. The fixed `[67,1]` after the client data is gone.
- **Verified:** proxy of the original vs ours on prison_funst3r with two bots (`spec_probe.lua`): `[67,1]` and
  `[32] 20 f0 86 90 85` byte-identical at join, `specplayer` `[67,2][67,2]`, `specmode` `[67,0]` / `[67,2]`,
  HUD screenshots match. Both clients on 140.238.183.194:36969 (empty server): no `[67]`, identical first
  `[32]`, showcase every 5 s. `test_spectator` added (21 / 21).
- **Left:** see OPEN_ISSUES "Spectator".

## 2026-10-11 — Claude Code — shared scrolling in the UI library
- **Changed:**
  - `cs2d_ui`: `cs2d_ui_scroll_t` (first visible row + thumb grab), `cs2d_ui_scrollbar_style_t`, and
    `cs2d_ui_scroll_{max,clamp,by,page,wheel}`, `cs2d_ui_scrollbar` (in the layout) / `cs2d_ui_scrollbar_at`
    (floating, over custom-drawn lists). Every view now drags the thumb, pages on a track press (visible - 1),
    takes the wheel while the pointer is over it, and clamps the same way.
  - Moved to it: console (now counts from the top; at the bottom it follows new output), Server Info, server list
    (it had no drag / page), dropdown, and the editor's tile / entity / map file lists (they only had the wheel).
    Each keeps its old look through a style. The global wheel dispatch in `cs2d_client_handle_mouse_wheel` and
    `cs2d_scene_editor_wheel` are gone: the views read the wheel from the UI context.
- **Verified:** `test_ui_scroll` (wheel inside / outside, track page, thumb geometry and drag, clamp), the console
  and dropdown tests (20 / 20); screenshots of the editor tile list being dragged, the console and the server list.
- **Left:** the editor sidebar buttons (z 11) and its tooltip are drawn over the console window (it was like that before).

## 2026-10-11 — Claude Code — map editor (first part)
- **Changed:**
  - New `cs2d_editor.c/.h` (model): brush (rotation, BR, color), set tile / rect / flood fill, tile properties,
    `.inf` V1..V4 read and V4 write, entity tables, place / remove.
  - `cs2d_scene_editor.c` rewritten: sidebar with the 12 icon buttons, tile list, entity list with filter, rotation /
    BR / color, Tile Properties, Color and Load / Save windows, grid, hovered-tile frame, entity icons, status line,
    tooltips. The editor builds the UI every frame (`cs2d_scene_editor_build_ui`, `_render_overlay`, `_wheel`).
  - Map: per-tile color data (`tile_data`) loaded and saved; the render follows `TileProp.Get` (brightness b * 10 %,
    colored tiles) instead of the old guesses. New `cs2d_render_tile_frame` and `cs2d_render_text_scaled_ex`.
  - Tools: `initarrays.py`, `bbstr.py --array`.
- **Verified:** our editor's screenshots next to the user's screenshots of the original (sidebar, Tile Properties,
  Color); load -> save byte-identical on 7 original maps; `test_editor` (19 / 19); de_dust renders as before apart
  from the corrected brightness.
- **Left:** Map Properties, Display Settings, Tools mode, Entity Properties, context menu, undo (OPEN_ISSUES).

## 2026-10-11 — Claude Code — join file transfer (server downloads)
- **Changed:**
  - New `cs2d_transfer.c/.h`: `[252,50]` list checks (`FUN_1402cb770` rules), `[252,50,2]` requests, file blocks,
    zlib uncompress (`-lz` in `build.bat`), saving with the `Ttrans_recv.finish` rules, `sys/core/dls.cache`.
  - Client: "Downloading" window (Tgui_joinmen layout, Cancel = `[253,9]`); the map now loads at `[252,4,0]`;
    reliable packets are handled once (sequence bitmap); `connect <ip> <port> [password]` (key `j01nk3y`, the old
    `mypassword` key was wrong); `download` read from `sys/config.cfg` and sent in `[252,3]`.
  - `cs2d_render_text_scaled_ex`; tools `capture_transfer.sh` (dedicated in another folder, client in a copy,
    `CLIENT=orig`, `KEEP=1`) and `bbstr.py` (decodes `bbs__` literals).
- **Verified** against the modded C4 dedicated (`C:\C4`, fun_roleplay, password 132):
  - our client downloads 231 files, every one byte-identical to the server's copy, then joins;
  - our 26 `[252,50,2]` request messages are byte-identical to the original client's;
  - the handshake after the transfer (`[252,2]` again → `[252,3]` with the new checksum) matches the original;
  - a second join with everything present gets no list (original too);
  - a normal join on the stock dedicated (prison_funst3r) still works; `test_file_transfer` added (18 / 18).
- **Left:** HTTP downloads, the `[252,4,11]` map-differs dialog, the transfer cache lookups (see `OPEN_ISSUES.md`).
- **Follow-up:** zlib is linked statically (the exe needed `zlib1.dll`). Joining from Find Servers no longer loads
  `de_dust2` first: the handshake and download run over the menu and the game scene starts at `[252,4,0]` with the
  server's map (checked on prison_funst3r and on C4 with a download).

## 2026-10-11 — Claude Code — Ghidra dump of CS2D.exe
- **Changed:** documented `docs/dump/` (full Ghidra decompilation of the client, git-ignored) in `TOOLS.md`
  and `AGENTS.md`; added `tools/agent/dumpgrep.sh` (function by address/name, methods of a class, body regex).
- **Verified:** dump addresses match REA on `CS2D.exe` (`Tpl::move` `0x1401f7600`, `Tgui_sl::draw`,
  `Tgui_sl::pingServers`, `NativeFlare::Draw`).

## 2026-10-11 — Claude Code — server browser (Find Servers)
- **Changed:**
  - New `cs2d_serverlist.c/.h`:
    - `Tsv` model;
    - `svl.cache`, `favorites.lst` and `recentlyjoined.lst` readers;
    - info / ping packets;
    - `pingServers` scheduler;
    - LAN broadcast;
    - its own UDP socket.
  - "Servers" window in `cs2d_client.c`: tabs, flag icons, mode letters, players and bots, latency bars,
    scrollbar, stats, tooltip, the 15 s cache reload, joining, and "Connect to IP". It opens from the lobby's
    "Find Servers".
  - Fixed `cs2d_net_parse_info_packet`: strings are u8-length, and bots + flags2 are always present.
- **Verified:**
  - Proxy captures of the original (`tools/agent/browser_proxy.py` + a test `svl.cache`), and our client
    against the same dedicated: identical request and ping bytes, the same 15 s reload, and joining from the
    list reaches "Connected".
  - Screenshots compared with the original.
  - `test_serverlist` (17/17).
- **Left:** see `OPEN_ISSUES.md` (U.S.G.N. list, filters, favorites, writing the lists).

## 2026-10-11 — Antigravity — firearm visual recoil kickback, melee swing animation, and GitHub Actions release workflow
- **Firearm visual recoil (`Tpl +0x308`):** extracted the per-weapon visual recoil table (`0x1409eda18` in
  `CS2D.exe`, `1..6 px`) from `init_weapons` (`0x1402305db..0x1402340a9`). Firing sets
  `recoil = visual_recoil + Rnd(0, 1)` (`0x1402166a7`), which displaces the player backward along `-angle`
  in `cs2d_render_player` (`0x14012131c..0x1401213cc`) and decays by `0.5 px` per 60 Hz frame (`30 px/s`, `0x14021d1b1`).
- **Melee swing (`Tpl +0x31c`, `+0x24c`, `Tp` type 13):** melee attacks (except Chainsaw `85`) set
  `slash_timer = 8.0f` (`0x14018cebc`) and `draw_angle = angle + 6.0f * slash_timer` (`+48° -> 0°` over 8
  frames at 60 Hz, `0x14021d68a` & `0x14021da21`), rotating both the torso and held weapon (`13.5 px` offset)
  and spawning the additive `gfx/knifeslash.bmp` arc (`0.07/frame` fade, `0x14004b4e0` & `0x140265220`).
- **GitHub Actions CI & Releases:** added `.github/workflows/build.yml` (MSYS2 MinGW-w64 + SDL3) to compile
  `cs2d_client.exe` and `test_cs2d.exe`, run the regression suite, upload workflow artifacts, and publish
  `cs2d-binaries-win64.zip` + `SDL3.dll` to GitHub Releases.
- **Verification:** added `test_recoil_and_swing` in `tests/test_cs2d.c` (`16 / 16` passing).

## 2026-10-11 — Antigravity — Server Info ("<Server> - Info") and "Select Team" / skin subcategory UI
- **Protocol & parsing:** parsed `server_name` from `[252,6]`, `opcode 240` (`CS2D_NET_MSG2`) `mode 50`
  (`"1"` clear, `"2"` open) and `mode 51` (UTF-16LE `serverinfo.txt` lines with `U+00A9` `RRRGGGBBB`
  color tags and `U+00BB` gold link highlighting), and `opcode 20` (`CS2D_NET_TEAM`) send/receive.
- **Clay UI windows (`cs2d_ui`):** implemented the `600×440` `"<Server Name> - Info"` window (`553×374`
  scrollable box + `15 px` scrollbar + `568×20` `Close` button) and the `600×440` `"Select Team"` menu
  (`M` key) with `240×25` numbered buttons, T/CT skin subcategories (`1..4` skins, `5 Auto-Select`,
  `Back`, `0 Close`), and immediate selection (no subcategory) for `5 Auto-Select` and `6 Spectator`.
- **Verification:** added `test_server_info_and_team_menu` in `tests/test_cs2d.c` (`15 / 15` passing) and
  captured live screenshots against `cs2d_dedicated.exe -offline` verifying the full join → Server Info →
  Select Team → CT skin selection → live spawn flow.

## 2026-10-11 — Claude Code — agent memory structure
- Added `AGENTS.md` and its pointers (`CLAUDE.md`, `GEMINI.md`, `.agent/rules/`), plus `docs/agents/*`,
  `tools/agent/*` (capture scripts and Lua probes) and `.mcp.json` (REA).

## 2026-10-11 — Claude Code — weapon sync, shot effects, join/leave
- **Weapon sync:** the client follows the server's inventory: opcodes 9, 16, 21, 22 (fixed record size),
  23, 24, 25, 33, 42 and 61, plus ground items. Validated with the `inv` command against the Lua dump:
  identical.
- **Shot effects:** tracer, glowdots, muzzle flash with light, wall sparks and shells (`cs2d_fx_shot`).
  Remote shots come from `[7,pid,seed]`.
- **Join/leave:** the missing client data `[252,10]` + `[67,1]` meant the server never ran `join`; the
  client now sends both. Two-client test: join 1, join 2, leave 2, leave 1; player count 0→1→2→1→0.
  The client also handles `[248]`, `[253]` and `[28]`.

## 2026-10-11 — Claude Code — Env_Breakable
- **Behaviour:** health, damage, hitscan trace, explosion and break effects (`cs2d_fx.c`). The fire
  packet is now `[7,seed]`.
- **Verification:** `test_breakables` uses numbers measured on the dedicated.

## Earlier (Claude Code / Antigravity)
- **Scenes and UI:** scene split (lobby, game, editor); UI library (textbox, dropdown, numberbox, console
  ring buffer).
- **Shadows:** fixed shadow halos and removed the invented roof parapet.
- **Entities:** entity logic validated state for state against the dedicated (fixture of 331 samples).
- **Movement and protocol:** movement is an exact port of `FUN_1401f7600`; the protocol matches the
  original client (positions, rotation, use, setpos).
- **Restart:** the offline `restart` command restarts the round (states, Trigger_Start, freeze time, random spawns).
