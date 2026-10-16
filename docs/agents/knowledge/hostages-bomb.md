Part of the shared knowledge; index and source conventions in `docs/agents/KNOWLEDGE.md`.

## Hostages and bomb (use key, plant, defuse)

Code: `src/cs2d_hostage.c` (Thostage list), `game_use_online` in `src/cs2d_scene_game.c`, handlers in `src/cs2d_client.c`.
Probes: `tools/agent/lua/hostage_probe.lua` (cs_office), `bomb_probe.lua` (de_dust2), `use_probe.lua`.

- **Use key (REA `FUN_14014f820`, param_6 = 1):** tiles 0 / 16 / 32 px along the aim, in turn; on each tile: CT hostage
  (health > 0, tile = `(int)x >> 5`) → mode 2 if it follows me else 1, data = hostage id; CT planted bomb item 63 on the tile
  (bomb timer running, proc 0, reload < 1) → mode 3, data = `Tpl.defuse` (kit), x/y = tile, defuse starts locally
  (proc 2 no kit 10 s / 3 kit 5 s); Trigger_Use (mode 100); own Tdo 9/15 opens the build menu (no packet). Nothing:
  mode 0, zeros — still sent, the server runs the Lua `use` hook (verified). Server floodguard 200 ms (`FUN_1402d8f00`).
- **Echo `[26,pid,mode,data i32,x u16,y u16]`** (11 bytes, all clients, proxy): mode 1/2 → client sets hostage `used` = x
  then `Thostage.use` (0x1401193f0: CT only; first use +$150 capped 16000; follow = pid, or 0 on release; random
  sfx/hostage/hos1..4); mode 3 → that player's proc 2/3 + proctimer reset. Verified: `[26,1,1,1,0,0]` after our mode 1.
- **`[29,id,kind,a u16,b u16]`** (`FUN_1402d93f0`): 0 target tile (a,b), follow = 1 if 0; 2 rescued by b (+$1000);
  3 health = a, b −$150; 4 killed by b (−$1500, Tp 0x18). Proxy: `[29,1,0,4,5]`, `[29,1,0,5,5]` while the follower walks.
- **Client movement (Thostage.draw 0x140147090):** only while follow > 0: rot = atan2 to the follower (follow = 0 if he is
  dead / team < 1); x/y step 4 px per drawn frame towards the target tile centre until within 4 px (Lua on the ded:
  172,176 for target tile 5,5 — same as ours). `lastmove` is never written on the client, so the legs branch should never
  draw (we still draw legs, see OPEN_ISSUES).
- **Round start (FUN_1402520b0):** Thostage.freeall, then if `0x140a14e48[1] == 0` (taken as sv_gamemode 0) one hostage per
  Info_Hostage in entity order, ids 1.., tile centre, rot = ints[0], look = min(ints[1], 7). Hostages only exist after a
  round start (`sv_restartround` in probes).
- **Join snapshot section 2:** `[252,7,2,n] + n×{id,health,look,follow,x u16,y u16}`; target = current tile, rot Rand(1,360).
- **Hover text** (FUN_1402a12d0, `mp_hovertext` bit 4, `cs2d_hostages_hovered`): tried after the players and before the
  buildings, and a hovered hostage suppresses the building text (bit 8). The first hostage with health > 0, `fog`
  (+0x5c) 0 and |world cursor - (x, y)| < 20 px on both axes: "Hostage" (language 0776) in (255,255,0) at cursor y - 25
  and the health "n%" at y + 25 in ((100 - hp) * 5.1, hp * 5.1, 0). No per-player FOW here yet, so `fog` stays 0.
- **`[22]` record byte 11 = `Tpl.setspecialbyte` (0x1401ffdc0):** 1 bomb (T: Titem.give 55, not in the inventory trailer) /
  defuse kit (CT), 2 night vision, 4 VIP (team 3, armor 200), 8 flag, 16 gas mask, 32 flashlight.
- **Plant (FUN_1401cb510):** attack with item 55 inside an Info_BombSpot zone, proc 0 → proc 1, `[30,1]`; attack released →
  `[30,2]`. The server also starts it from `[7]` when its weapon is 55 (Lua `setweapon` is not sent to the client).
  Done after 3000 ms on the server: `[30,pid,3,x u16,y u16,item id u16,c4 timer u8]` (proxy `1e 01 03 1c 00 16 00 01 00 23`):
  item 63 at the tile, bomb removed from the planter, "Bomb has been planted!" (255,220,0).
- **Defuse:** server completes after 10000 / 5000 ms; no `[30,5]` reached the clients in the capture (round end `[22]`
  follows). Client aborts with `[30,4]` when use is released or the bomb timer is 0 (0x14021c19f). `[30,6]` = explosion
  (removes item 63, big particle burst FUN_1401f1130, screenshake 50).
- Lua: `equip 55` is refused ("this would lead to problems"); `spawnplayer` resets the inventory (use `setpos`).
- Item 63 is "Planted Bomb" (`gfx/weapons/plantedbomb_d.bmp`), not Gold (68).
- **Planted C4 is never a pickup (fixed 2026-10-16):** `cs2d_sv_step_items`' generic "does the player
  already own it" fallback also matched item 63 (its `CS2D_SLOT_ITEM` matches no branch), so the
  *planter*, who keeps standing on the tile he planted, collected the C4 on the next tick. The server
  broadcast `[25]` and every client removed the ground item (and the use key of
  `cs2d_scene_game` looks up that item to start a defuse), so the bomb was invisible for both teams
  and could not be defused - the round only ended with the explosion. The original only ever touches
  the planted item with the defuse use (`[26, 3]`). Regression: `test_server_phase5_bomb_hostages_bots`
  (the plant must survive a `cs2d_sv_step_items` with the planter on the tile, no `[25]`).
- **Planted C4 beep** (`FUN_1402553d0`, per frame): `sfx/weapons/c4.wav` at the bomb, red Tp 2 flare (fade 0.1, sx 0.4),
  light particle (255, 0, 0 -> 35, 80, 20); next beep by seconds left r: r <= 3 100 ms, < 8 200, < 12 400, < 16 750,
  < 20 1000, else 1500. Radar blink (`Thud_radar_blink`, T / spectators) not drawn. Code: `bomb_beep` (scene_game).
