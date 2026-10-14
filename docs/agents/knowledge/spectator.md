Part of the shared knowledge; index and source conventions in `docs/agents/KNOWLEDGE.md`.

## Spectator

Source: REA `FUN_14025aa60` (camera), `FUN_1401698a0` (target picker), `FUN_1401682f0` (`[67]`), `FUN_14025a270`
(`[32]`), `FUN_14029ac10` (HUD), `FUN_140298bf0` (HUD button), `FUN_140299470` (name box), command handlers at
`0x1401b58c6` / `0x1401b5980`; proxy and screenshots of the original on prison_funst3r with two bots and on
140.238.183.194:36969. Code: `src/cs2d_spectate.c`, `src/cs2d_scene_game.c`.

- **sv_specmode** (`0x140a14ee8[1]`): 0 Nothing (camera at -100000, box "Server does not allow Spectating"),
  1 Everything, 2 Own Team only (a team member follows only its side, CT and VIP together; a Spectator anybody).
  Live changes: `[237, setting, value]`; settings 1, 11 and 12 carry a string, 2..10 and 13..24 an i32; 3 is
  sv_specmode.
- **Mode** (`DAT_140ddf2f8`): 0 nothing, 1 Free Look, 2 Follow Player; target `DAT_140ddf2f4`. Mode 0 becomes 2
  under sv_specmode 1 or 2. Bindings (`sys/controls.cfg`): `leftctrl` = `specmode`, `space` = `specplayer`.
  - `specmode` (only sv_specmode 1): free -> follow sends `[67,target]`, follow -> free sends `[67,0]`.
  - `specplayer` (no HUD window open; the Server Info window blocks it): in follow mode it picks the next player
    (which sends `[67,n]`), then sends `[67,n]` again; the proxy shows `43 02 43 02`.
- **Picker:** living players in `Tpl.list` order. dir 0 keeps a valid target, else takes the first one; next
  wraps to the first; previous from the second entry (index <= 1) wraps to the last (quirk). Nobody alive:
  target 0 and no `[67]`.
- **Free look:** 10 px per 60 Hz frame per held movement key (`0x140bd6d88`), x 0.5 with walk; the integer
  camera is truncated after each axis step.
- **Follow** (`Tmap_scroll.toplayer_smoothly`): within 32 px (`maxDelta`) on both axes the camera moves
  0.3 x the distance per frame (`smoothScrollFactor`); farther, it jumps.
- **Showcase** (`FUN_140060b10`; flag `DAT_140ddf328` set at join, cleared by any team menu button): every 5 s of
  the 1 s tick, with no T or CT player and a HUD window open, the camera jumps to a random Info_T, then an Info_CT
  (alternating; the first one is random).
- **HUD (850x480):**
  - Black bands at alpha 0.55, y 0..40 and h-40..h. "Spectating" (150) centered at y = 40 - text height - 3.
  - A dead team member also gets "You are dead!" at y 0, pulsing red (`|sin(ms/10 deg)| x 155 + 100`); respawn
    modes show "You are dead! - Click to respawn!" in (255, `|sin(ms/5 deg)| x 255`, 0).
  - Buttons 25 high at y h-33: Menu x 5 w 46, Map x 56 w 37, Fog x 98 w 37 (enabled with sv_fow and
    sv_specmode 1), name box x 140 w w-280, mode button x w-135 w 130 ("Spectate Nothing" / "Free Look" /
    "Follow Player"; enabled in free look, and in follow under sv_specmode 1).
  - Button: inside (50) at alpha 0.6, frame 200 hovered / 150 / 100 disabled, additive `gui_shade` 0.3 / 0.1;
    text white hovered / 200 / 150, or the team color (T 255,25,0; CT 50,150,255) at alpha 1 / 0.8 / 0.5.
  - Name box: "-" (disabled) without a target or in free look. Otherwise an avatar square ("?" without one),
    the name in the team color (white in Deathmatch), `hud_symbols` frame 0 + health and frame 1 + armor
    (additive (255,255,32); value color ((100-v) x 5.1, v x 5.1, 0); armors 201..206 by name). Under
    sv_specmode 2 an enemy shows only its name. Left click = next player, right click = previous.
- **Time and money** (FUN_14029ac10 0x14029c5xx; the alive HUD FUN_140295190 is not drawn while dead): sized by
  TextHeight("0") / TextWidth("ITEM:0000"), `hud_symbols` additive. mp_hud 4: (155,155,155) frame 2 (6 with the bomb
  planted) at (w - TW, 2), TH + 4 square; mp_roundtime 100 without bomb: frame 11 at w - 17, else "M:SS" right
  aligned at w - 2, y 4. mp_hud 16: (150,150,150) frame 4 (cart, not the $ 7 of the alive HUD) at
  (w - TW + 2, 40 - TH - 2), TH + 2 square, money (0x140a18378 +0x278) right aligned at w - 2, y 40 - TH - 1.
  mp_hud bits: 1 health, 2 armor, 4 time, 8 ammo, 16 money, 32 icons, 64 kill messages.
- **Minimap** (`FUN_140299d70`, called with y = 40 while `DAT_140a15478` is 1; the Map button toggles it; it is 1 in
  .data; `DAT_140ddf195` selects `TSmartMinimap` instead): the editor pixmap scaled s = 5/4/3/2/1 for max(xmax, ymax)
  < 21/31/61/121/else at x = w - (xmax - 1) * s, alpha 0.75, white frame; living players as s x s dots (T (255,25,0),
  CT (50,150,255), green without teams; hostages yellow, items 70/71 and Info_CTF_Flag / Info_Dom_Point dots too);
  the 27x15 camera rect additive yellow |sin(ms / 10 deg)| * 0.2. With sv_specmode 1 a held left button on it sets
  free look (no `[67]`) and camx = ((mx - x) / s - 13) * 32 - 16, camy = ((my - 40) / s - 6) * 32 - 16.
