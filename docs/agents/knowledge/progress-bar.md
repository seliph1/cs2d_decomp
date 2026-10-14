Part of the shared knowledge; index and source conventions in `docs/agents/KNOWLEDGE.md`.

## In-game Action Progress Bar (Reloading, Planting, Defusing)

Source: CS2D.exe dump `FUN_140295190` (`0x1402958f0..0x140295b70`), `Tpl.update` (`0x14021e35a`), `Tpl.reload` (`+0x320`),
`Tpl.proc` (`+0x340`), `Tpl.proctimer` (`+0x348`), language entries 0759..0761. Code: `src/cs2d_render.c`.

- **Trigger conditions:**
  - `Reloading` (`sys/language 0759`): `player->reload == 1` / `player->reload_timer > 0` on firearms (`wep->clip_size > 0` and `wep->slot != CS2D_SLOT_MELEE`). Progress: `1.0f - (player->reload_timer / wep->reload_time)`.
  - `Planting Bomb` (`sys/language 0760`): `player->proc == 1` (`player->is_planting`). Total time 3000 ms (`0.00033333333`). Progress: `player->plant_progress / 3.0f`.
  - `Defusing Bomb` (`sys/language 0761`): `player->proc == 2` (no kit, 10000 ms, `0.0001`) or `player->proc == 3` (with kit, 5000 ms, `0.0002`). Progress: `player->defuse_progress / (has_kit ? 5.0f : 10.0f)`.
- **Dimensions & Geometry (0x140295980):**
  - Font scale: `scale = font_h / 13.0f = 1.0f` (`DAT_140a1ccc8 * 0.07692308`).
  - Bar dimensions: `w = (int)(scale * 102.0f) = 102 px`, `h = (int)(scale * 20.0f) = 20 px`.
  - Border: `border = max(1, (int)scale) = 1 px`.
  - Position: `x = screen_w / 2 - w / 2`, `y = screen_h / 2 + (int)(scale * 80.0f)`.
  - Inner fill: `inner_w = w - border * 4 = 98 px`, `inner_h = h - border * 4 = 16 px`, `inner_x = x + border * 2`, `inner_y = y + border * 2`.
- **Styling & Colors:**
  - Background: `incbindata/shadow.bmp` (`bank->tex_shadow`) midhandled at `(screen_w / 2, y + h / 2)`, scaled `(scale * 11.0f, scale * 5.0f)` in black at alpha 0.40.
  - Text: Centered at `screen_w / 2`, `y = bar_y - text_h - (int)(scale * 4.0f)`. Color: warm amber/gold `(255, 200, 0)` (`DAT_140a14c6c * 1.64`), alpha 0.75.
  - Border & Inner fill: Same `(255, 200, 0)` color, alpha 0.75. Fill width: `inner_w * progress`.
