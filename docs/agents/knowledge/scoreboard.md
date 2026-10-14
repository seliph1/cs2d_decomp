Part of the shared knowledge; index and source conventions in `docs/agents/KNOWLEDGE.md`.

## Scoreboard

Source: dump `Thud_scoreboard` (`draw 0x140251220`, `drawBase 0x140250170`, `drawTeamHeader 0x140124cd0`,
`drawTeamColumns 0x140124ff0`, `drawPlayer 0x14012e4b0`, `drawWithTeams 0x140130450`, `drawWithoutTeams 0x1401309d0`,
spectators `FUN_1401066d0`), scope `0x1409df4e0`, kill `FUN_1401eaf60`, round start `FUN_1402520b0`; screenshots of our
client offline and against the dedicated. Code: `src/cs2d_scoreboard.c`.

- **Key:** `bind "tab" "showscores"` sets `DAT_140ddf480` while held; the HUD draws `Thud_scoreboard.draw(1)` after the chat.
- **Settings (shipped):** columns Latency, Deaths, Assists, Score + avatars, flags, MVPs on (`0x140a14fd0..0x140a14fe8` = 1);
  money, K / D, ids, ranks, shortcuts off (.bss). Size setting `0x140ddf1e0` = 0.
- **Window:** resolution table (widths `0x140bd7860`, heights `0x140bd78a0`: 640x480, 850x480, 800x600, 1060x600, ...);
  a screen width at index i > 1 uses entry 1 (i = 2, 3), `0x140bc8a40[i - 4]` (4..11) or i, minus 20, centered when it
  fits; otherwise the screen minus 10 px on every side (850x480: 830x460 at (10, 10)). Black 0.55, `gui_c.bmp` 16 px corners.
- **Sizes** (HUD font 13 < 15): 15 px font, small 12, scale 1.1538461; value column 48 (+2 gap), team header 26, gap 2,
  row 21, icon 17, column (rw - 32) / 2 - 4 = 395; rows per column ceil(...) = 17.
- **Top line** (y = ry + 5): server name (255,220,0) at rx + 16, ticks (small, red < 20, yellow < 30, else 180) after it,
  map name (150,255,0) right-aligned at rx + rw - 16. Then y += 15 + scale * 8.
- **Team game:** CT (50,150,255) left "Counter-Terrorist Forces" (0353), T (255,25,0) right "Terrorist Forces" (0352);
  Zombies: "Survivors" (0880) / "Zombies" (0879) in green. Header: 0.2 bar, white name (squeezed to 75 %), team score on
  the right, "n Players" (0201, "1 Player") small at 0.7. Without teams: one green "Players" header over both columns.
- **Rows:** sortval = score * 500000 + assists * 5000 + mvp * 500 - deaths, descending. Row bar 0.1 / 0.03 alternating,
  the local player +50 brighter at 0.3. Dead: name gray 110 at 0.8, `hud_symbols` frame 12 fading yellow -> 200 over
  130 frames, deaths white -> row color. Score / assists flash for 97.5 frames after a change (green, yellow without teams).
  Latency colored like the server browser; offline the local player "Server" (0281), bots "Bot" (0229).
- **Team scores:** `[252,6]` u16 T, u16 CT after the server name; `[64]`; client-side `+1` at `[22]` by reason:
  1 << reason in 0x1004010040101000 T, 0x804 / 21 / 22 / 0x2008020080000000 CT, 4 and 5 reset all scores.
- **Kill scoring (client):** victim deaths + 1 unless weapon 0xfc; T vs CT/VIP killer + 1; otherwise (not self, weapon not
  0xff / 72 / 73 / 76 / 77 / 87) team game - 1, no teams + 1.
- **Spectators:** "Spectators (n): " (0791) + names with ", " (80), alternating 110 / 150, anchored to the window bottom.
