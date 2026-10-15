Part of the shared knowledge; index and source conventions in `docs/agents/KNOWLEDGE.md`.

## Buy menu

Source: dump `Tgui_hudmen` (`update 0x14002eb80`, `button 0x140129620`, `fbutton 0x14012a2c0`, `box 0x14012aaf0`,
`moneyinfo 0x140108010`, `drawBuySectionPreview 0x140111b10`, `weaponinfo 0x14013bfd0`, `buyWpnButton 0x140202670`,
`draw 0x14021f6d0` menus 0x14..0x1a), command handlers at `0x1401b546d` (buy) / `0x1401b5530` (buyammo1) /
`0x1401b55c7` (buyequip), `FUN_1401fff70` (buy request), `init_weapons` tables. Code: `src/cs2d_hudmen.c`.

- **g_lang strings:** `g_lang[N].scales + data_start + k` is `sys/language` entry `(N * 36 + k + 28) / 8` (Price = 0242,
  Close = 0031, Back = 0032 check out).
- **Commands** (`sys/controls.cfg`: `B` buy, `C` buyequip, `,` buyammo1, `.` buyammo2), only while `DAT_140ddf418` is
  1..2 (this is the game state, not the team: 0 menu, 1 / 2 in a game, 4 map editor, set at 0x1401d0af5): `buy` sets menu 20, `buyequip` menu 26 (`FUN_14014ee60` opens the window), both skipped when the
  setting `0x140a14e20[1]` starts with "-"; `buyammo1/2` buy item 61 / 62 at once.
- **Buying** (`FUN_1401fff70(pid, item, 0, 1, 0)`): sends `[23, item, 0]`; the menu stays open (the button only flushes
  the keys). Autobuy / rebuy go through the same function with local checks (`DAT_140ddf2e0`, not reconstructed).
- **Window:** 600 x 440 centered. Buttons 240 x 25 at x 16, key = number key (`DAT_140ddf340`):
  - menu 20 "Buy" (0308): 1 Handgun (0309) y 30, 2 Shotgun 60, 3 Sub Machine Gun 90, 4 Rifle 120, 5 Machine Gun 150, 6
    Primary Ammo 210, 7 Secondary Ammo 240, 8 Equipment (0316) 270, 0 Close (0031) h-46; no key: Auto-Buy (0318) and
    Re-Buy Previous (0317) at x w-256, y h-76 / h-46. The section buttons show the `_k.bmp` of Glock / USP, M3, MP5,
    AK-47 / M4A1, M249, Kevlar+Helm (T / CT by `Tpl.team == 1`).
  - 21..26 "Buy - <section>" (`" - "`): items every 30 px from y 30, Back (0032, no key) h-76, 0 Close h-46.
    Handgun 1..6; Shotgun 10, 11; SMG key 1 Mac 10 (T) / MP5, then TMP, P90, UMP45; Rifle Galil / Famas, AK-47 / M4A1,
    SG552 / Aug at 30..90, then Scout, AWP, G3SG1, SG550 at 150..240; MG M249; Equipment Kevlar, Kevlar+Helm,
    Flashbang, HE, Smoke, Defuse Kit (enabled when the map has Info_BombSpot, `DAT_140ddf078`, and team >= 2), Night
    Vision, Tactical Shield (team > 1), Flare.
- **Button:** inside black 0.3; frame 200 hovered / 150 / 50 disabled; gui_shade additive 0.3 / 0.1; text 255 / 200 /
  100 at y + 12 - h / 2; key at +5 (alpha 0.4), label at +20 (+5 without key); an image is scaled to fit 46 x 22 (max 1),
  centered on x + 42, additive 0.75, and moves the label to +70 (+55); right text at w - 5 (alpha 0.4). Items:
  name, `_k.bmp`, `"<price> $"`.
- **Images** (`0x140220000` loader, name without spaces / dashes, lower case, maskMode 2): `_k.bmp` 0x1409f3d18
  (top-left), `_m.bmp` 0x1409f3d20 (int midhandle), `_d.bmp` 0x1409f3d28.
- **Hovering a section** (menu 20): its `_m` images circle window + (430, 180), shadow +2 black 0.3, step 360 / n (int),
  turning 3 degrees per frame (`rot += g_dt * 3`). Radii: handgun 35 (1..6), shotgun 30, SMG 40 (T 23,20,22,24; CT
  20,21,22,24), rifle 55 (T 38,30,31,34..37; CT 39,32,33,34..37), MG / ammo at the center, equipment 55 (57,58,52,51,56,
  53,59,41,54).
- **Hovering an item** (`weaponinfo`): `_m` at (430, 180) and 3x at (430, 110) (shadow +6, +4 when taller than 35 or
  wider than 100, +3 when wider than 150 or taller than 55), `_d` when there is no `_m`; `_k` at (285, 35) additive (100);
  box (275, 215) 305 x 190; lines in (150) at x 280, values right aligned at 575, y 220 + 20 n: Name (0199), Price (0242,
  label and value red when money < price), then for HUD slots 1 / 2: Damage, Ammo `clip|reserve`, Rate of Fire
  `60000 / (delay * 40)` rounds/min, Range `<0248..0251> (range)` (< 21, < 101, < 301), Weight (0 Normal, > 0 Light,
  < 0 Heavy 0876), Accuracy (0 Perfect, 1 Good, < 4 Average, < 6 Bad, else Very Bad, `$1` = value), a y 380 note
  (Silencer 1 / 32, Burst-Mode 2 / 39, "5 bullets" damage x 5 for 10 / 11, 1x Zoom 31 / 33, 2x Zoom 34..37). Equipment
  has its own notes (0266..0278); Kevlar 65 / Kevlar+Helm 100 AP.
- **Client stats** (`init_weapons`, `tools/agent/weapontables.py`): damage 0x1409f62a0, clip 0x140a017a8, reserve
  0x140a017b0, fire delay 0x1409f2600, range 0x1409f62a8, accuracy 0x1409f5d18, weight 0x1409efd18 (float), zoom damage
  0x1409efd10 / 0x1409efd08. Range, accuracy and weight differ from the dedicated's table (M3 range 50, AWP weight -0.5).
- **Fire and reload timers** (`Tpl.firetimer` / `Tpl.reloadtimer`, `Ttimer.reset(ms)`: time = ms + `g_time_ms`):
  `Tpl.attack` (`FUN_1402164d0`, imul 0x27 at 0x140216b8f) re-arms the fire timer with firedelay x **39 ms** (AK-47 3
  -> 117 ms); the client also reads `firedelay` from `sys/weapons.cfg` into 0x1409f2600 (`FUN_140175f40`, clamped
  2..255). Knife / Claw right click (`Tpl.attack2`, 0x1401fdde8 / 0x1401fde66) waits 1200 ms. Reload: `FUN_140214780` re-arms the
  timer with reload x 5 ms per stage; clip weapons 1 -> 2 (ours keeps reload x 10 ms, unresolved), shell weapons
  (flag 0x1409efcf8: M3, XM1014) 1 -> 4 per shell, see knowledge/sound.md.
- **moneyinfo:** box (514, 30) 70 x 25, `"<money> $"` right aligned at 579, y 33, in (200).
