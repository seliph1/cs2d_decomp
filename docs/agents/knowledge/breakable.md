Part of the shared knowledge; index and source conventions in `docs/agents/KNOWLEDGE.md`.

## Env_Breakable and shots

Source: Lua, proxy and measured, on prison_funst3r.

- **Damage:** a bullet does the weapon damage. The AK-47 (22) breaks 280 health on shot 13. The knife (45)
  breaks the 10 health stone in one hit. The server sends nothing on hits that do not break.
- **Reach:** the knife (range 15) reaches 20 px but not 21; the AK-47 (range 300) reaches 310 px but not 320.
  The code uses range + 6 and range + 15 with 1 px ray steps.
- **What stops a bullet:** walls (collision 1) and closed DynWalls; obstacles do not. A shot fired from
  inside a breakable hits that breakable.
- **Explosion:** `ints[3]` = 1, range `ints[4]` in px, power `ints[5]`. A player at distance 0 took 50
  (range 1, power 50). The fall-off `power × (1 − d/range)` is assumed.
- **Break visuals (frame captures):**
  - the tile splits into 4 quarters that drift, grow slightly and fade, keeping their orientation;
  - material fragments (`gfx/fragments.bmp`) fly back towards the shooter;
  - glass with `strs[0]` = −0.4 is drawn at alpha 0.6 (the sign is ignored).
