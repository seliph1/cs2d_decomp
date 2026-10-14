Part of the shared knowledge; index and source conventions in `docs/agents/KNOWLEDGE.md`.

## Ingame menu (Esc), vote, leaderboard, map briefing

Source: REA/dump `Tgui_hudmen.draw` 0x14021f6d0 (menu 2 / 100), window setup 0x1401a6800.., `TVote` (docs/dump/cs2d/TVote.c),
`FUN_1402e4220` ([230]), `Tgui_leadlist.draw` 0x1402e3150, reference screenshots of CS2D.exe. Code: `src/cs2d_hudmen.c`
(`cs2d_gamemenu_*`, `cs2d_votemenu_*`), `src/cs2d_scene_game.c` (`game_menu_*`), `src/cs2d_client.c` (leaderboard, briefing).

- **g_lang index:** `g_lang[N].scales + data_start + k` is EN line `(N * 36 + k + 28) / 8` (checked on 15+ strings).
- **Menu 2 (online client):** hudmen window 600x440. Buttons x 16 w 240: `1 Show Briefing` y63, `2 Change Team` y93 (menu 10),
  `3 Leaderboard` y123 (enabled when `[252,6]` leaderboard byte 0x140a14dd8 != 0), `4 Vote` y153 (menu 100), `5 Options` y183,
  `9 Disconnect / Quit` y h-106, `0 Close` y h-46; `Server Info` (275, h-46) w 305 key -1. Keys 7 "Players & Bans" / 8 "Bots"
  only when DAT_140ddf0d4 == 1. Info box `box(275,63,305,326)`; label x 280, values right-aligned on x 575; rows y 63 (Server,
  normal font, 255), 83 Address, 100 OS, 117 Ticks, 142 Map (150,255,0), 159 Players, 176 Game Mode, 201 FF, 218 Force Lighting,
  235 Voice Chat, 252 Fog of War (lang 0883 parts), 269 Recoil (0900), 286 Off-Screen Damage, 303 Physics, 320 Lua, 337 Lag
  Compensation = `lagcomp*100 "ms (" int(1000/ticks) " ticks)"`; On (0,255,0) / Off (255,0,0). Favorite row y 362.
- **`[252,6]` tail (0x1402d0560), after `b` = after the background string:** b+12 forcelight, b+13 daylight u16, b+15 recoil,
  b+16 hudscale, b+17 sv_voicechat, b+18 OS, b+19 lag compensation (DAT_140ddf140), b+20 Lua, b+21 ticks (DAT_140ddf13c),
  b+22 n, n x u16 Lua binds, i32 random seed, off-screen damage, leaderboard, physics, then "ACK". `h+4` = friendly fire; `s` = maxplayers.
- **Briefing:** `maps/<map>.txt` in a Tgui_tb (16,25) 568x374 + Close (16,404) 568, window "Map Briefing" (0727); no lines →
  red center "there is no briefing for this map" (0233). Server Info button with no lines: "there is no additional server information".
- **Vote (menu 100):** two Tgui_slist (16,50) / (305,50) 279x330; titles at (16,30) "Vote to kick a Player", (305,30) "Vote a Map".
  Rows: count at +5 (getVoteBrightness 0→125, 1→200, 2→220, 3→240, else 255), name +30, yellow `<` for self / current map;
  sorted by "99-votes"+name. Players: team > 0, not bots. Click: "vote"/"votemap" → `[43, 1|2, value u16]` and the menu closes.
- **`[43]` S→C:** `[43, voter, type, value u16]`; value >= 0xff9b = refusal: kick 0xfff5 (0720 disabled), 0xfff4 (0911 self, proxy-
  confirmed), 0xfffe (0722 wait 15 s); map 0xffeb (0721), 0xfffe. Valid → chat "votes to kick X" (0723) / "votes the map X" (0725).
- **Map cycle:** `[252,7,9, n]`: n = 0xff → u16 size; n = 0 → done; else n x {u16 index, str}. The offline dedicated sent `[252,7,9,0]`.
- **Snapshot section 1:** player bytes r[23] votekick, r[24..25] votemap u16.
- **Leaderboard `[230]` (proxy-verified on the local dedicated):** C→S `[230,0]` → S→C `[230,0, ts i32]`; C→S `[230,1, rank u16]`
  (one queued request per 7 frames, ranks visible in the list) → `[230,1, rank u16, ts i32, name (u16 len), id type u8 (0 usgn,
  1 steam), id i64, score, kills, deaths, assists, mvp, time i32]`. A new ts drops the rows. Window "Server Leaderboard", list
  (16,26) 568x340, header row, 20 px rows, columns +5 / +55 (name, max 120 px) / +210 / +270 / +330 / +390 / +460, Close (16,404).
  Footer: "Last updated YYYY/MM/DD HH:MM" (UTC, 155) or red "Leaderboard is unavailable" (0962). Time: "N s", "M m S s", "H h M m", "D d H h".
