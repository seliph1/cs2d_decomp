Part of the shared knowledge; index and source conventions in `docs/agents/KNOWLEDGE.md`.

## Sound effects (client, REA / dump)

- **Positional** `cs2d_sound_play_at(sound, x, y, vol, flags)` 0x1401191c0: listener `(g_camx + 425, g_camy + 240)`
  (constants, not the window size); `gain = vol - (|dx| + |dy| + flags) * 0.001`, skipped below 0.0001 (≈ 1000 px
  Manhattan); `SetPan(dx * 0.002)` only if stereo (`DAT_140ddf17a`, config `soundmode`); `SetVolume(volume * 0.01 *
  gain)` (`DAT_140ddf17d`, config `volume`, shipped 10). Copy at FUN_14011b6a0.
- **2D** FUN_140109d30(sound, vol, force): plays if vol > 0.01, `volume * 0.01 * vol`.
- **Per-frame cap:** `DAT_140ddf3a8` counts sounds, the 21st of a frame is dropped (2D with force ignores it); reset
  at 0x140230000+ (two sites).
- **TPosSound** (0x1401e3640, update 0x140278730): positional channel re-attenuated every frame while playing; used by
  [65] (`"sfx/" + path`, split at `|`, FUN_1401e3890). Env_Sound (Te.update case 0x17, `mapsounds` `DAT_140ddf17b`):
  `Tres.add(strs[0], 2)`, vol = strs[1], ints[2] = 1 → 2D; loop (ints[1] = 0) plays while state == ints[0], restarted
  when it ends.
- **Loader 0x14030cc00** (one global per file; arrays allocated by 0x1408daba0 and stored before the next scalar).
  **How to resolve a global to a file (verified 2026-10-15):** each sound block ends with
  `lea reg,[rip+..] # <literal>` + `mov [rax+0x8],reg` (the name of the sound it just created) and the next block
  starts with `mov [rip+..],rax # <global>` (that same object). So **the `lea` belongs to the global of the *next*
  block**, not of its own. The literals are static BBString objects in `.data` with UTF-16 text at +0x10, so they
  can be read straight from the file (VA - section start + section file offset). Resolved this way:
  `d8d8` = `sfx/player/zm_die.wav`, `d608` = `sfx/npc/headcrab_die.wav`, `d618` = `sfx/npc/snark_die.wav`,
  `d5f8` = `sfx/npc/vortigaunt_die.wav`, `d5f0` = `vortigaunt_charge`, `d5e8` = `vortigaunt_ranged`,
  `d600` = `vortigaunt_attack`, `d610` = `headcrab_attack`, `d620` = `snark_attack`, `d968` = `die_abyss.ogg`,
  `d960` = `die_toxic.ogg`, `d958` = the `sfx/player/die[1..3]` array, `d930` = `dominate`, `d938` = `build`.

  | Global | File |
  |---|---|
  | d968 / d960 | player/die_abyss.ogg / die_toxic.ogg |
  | d958 / d950 | player/die[1..3] / hit[1..3] arrays |
  | d948..d918 | sprayer, spawn, build, dominate, vote, repair, flashlight |
  | d910..d8e0 | pl_dirt, pl_metal, pl_snow, pl_step, pl_tile, pl_wade, pl_wood [1..4] arrays |
  | d8d8..d8b0 | zm_die, zm_radio, zm_hit, zm_spray, zm_attack[], env/mystery |
  | d8a8..d888 | materials flesh, glass, metal, stone, wood [1..3] arrays |
  | d880 / d878 | materials/bustglass / burstwood (Tp.killbreakable: glass / wood) |
  | d868..d7c0 | weapons ak47, aug, awp, deagle, elite, fiveseven, g3sg1, glock18, m3, m4a1, m249, mac10, mp5, p90, p228, scout, sg550, sg552, tmp, ump45, usp, xm1014 |
  | d7b8..d668 | laser, flamethrower, galil, famas, rocket, chainsaw, portalgun, m134, fnf2000, airstrike, teslacoil(+loop/start/end), c4, c4_disarm, c4_explode, g_bounce, g_flash, g_smoke, knife_hit/hitwall/slash, machete_*, w_clipin, w_clipout, w_roundin, w_empty, nvg, shell, usp_silenced, m4a1_silenced, silencer_off/on, zoom, click, glaunch, melee, barbedwire, mine_charge, mine_activate |
  | d660 / d658 / d650 | ric[1..5].ogg / ricmetal[1..2] / explode[1..6] arrays |
  | d648..d628 | wpn_denyselect, wpn_hudon, wpn_hudoff, wpn_moveselect, wpn_select |
  | d620..d5e8 | npc snark / headcrab / vortigaunt |
  | d5e0..d5c8 | items ammo, drop, kevlar, pickup |
  | d5c0 | hostage/hos[1..5] array |

- **Fire sound** FUN_14018c6b0: array `0x1409f4a20[weapon type]` (filled in 0x140230330: knife / wrench / claw →
  knife_slash, RPG + rocket launcher → rocket, 92 → teslacoil); silenced USP / M4A1 → *_silenced at flags 100.
- **Triggers:** ricochet 1 in 3 on wall impact with `particles` > 3 (Tshoot.WallImpactFx 0x14011aeb0); material by
  breakable ints[2] (Tp.breakable 0x1401197c0); explosion Rand(0,5) (Tp.explosion 0x14011a500); die Rand(0,2) at the
  kill position, zm_die for zombies in gamemode 4 (FUN_14019a530); hit in FUN_1401ec180; item sounds in Titem.give
  0x1401feeb0 (60..62 ammo, armour kevlar, else pickup); drop at the tile centre; thunder 2D at counter 1;
  HUD slot keys in FUN_1401cc180 (select / moveselect / hudon).
- **Reload** FUN_140214780(player, mode, local): mode 1 start → w_clipout (0x140a0d6e0), mode 2 finish → w_clipin
  (0x140a0d6e8), mode 4 shotgun shell → w_roundin (0x140a0d6d8); all at the player, vol 1.0, flags 0; a non-null
  items[typ] + 0x20 (mod sound) replaces them. Shell weapons: mode 1 is silent (only reload = 1 + timer); each
  timer expiry (host) or [16,pid,4] (client) is mode 4: timer re-armed, reloadani 8, roundin, +1 shell, ends full /
  reserve 0. reloadani (+0x330, 15 for clips) forces torso frame 3 (Tpl.getFrame). A shot clears reload
  (Tpl.attack 0x140216690).
- **Ours** (`src/cs2d_sound.c`): F32 stereo 44.1 kHz mixer on an SDL3 stream, 64 voices. Pan law estimated
  (`l = min(1, 1 - pan)`, `r = min(1, 1 + pan)`), BlitzMax driver not checked.
