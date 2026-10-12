# Open issues / next steps

Keep this file current: remove items when they are done (and log them in `WORKLOG.md`), and add what
you discover. Sign items that are being worked on (`[in progress: <agent>]`) so two agents do not take
the same one.

## Known gaps
- **Audio:** there is no audio system (material sounds, shots, `Env_Sound`).
- **Shots:** offline shots have no spread, and the reach of weapons other than the knife and the AK-47
  is not measured.
- **Muzzle offsets:** the per-weapon muzzle offset table (`0x1409f5d20` in `CS2D.exe`) is now recovered
  (KNOWLEDGE "Buy menu") but not used yet; `cs2d_fx` still uses the calibrated 30 / 22 px.
- **Fire rate, not measured:** the 39 ms firedelay tick comes from REA only; compare the `[7]` interval of the
  original on a proxy capture. Unknown whether semi-automatic weapons need a new click (we fire while held), and the
  global timer 0x140a1b008 that knife right click also sets to 1200 ms is not reconstructed.
- **Explosion damage fall-off:** assumed; only distance 0 was measured.
- **Fragments:** which fragment frames and tints belong to each material is inferred from captures.
- **Scoreboard, not reconstructed** (KNOWLEDGE "Scoreboard"):
  - Left click on a row to follow that player, the right-click context menu (`Thud_contextmenu`), the "Ticks" tooltip.
  - Voice / chat mute icons, country flags, Steam / U.S.G.N. avatars (always `incbindata/avatar.png`), the competitive line.
  - Small font: `PTR_bbNullObject_140a1ccb8` is assumed to be our 10 px font; not compared with a screenshot of the original.
  - Zombies: a zombie killer scores `DAT_140a14d7c` (we add 1). The client's local +300 / -3300 kill money is not applied.
  - Our list order is the player id; the original's `Tpl.list` is the join order (matters only for equal sortval).
- **Kill feed:** `[19]` updates scores and deaths, but the kill messages (`Thud_msg.add_kill`) are not drawn.
- **Opcode 40:** "kill dropped weapon"; its format is unknown.
- **Round start and ground items:** whether dropped weapons are removed at round start is unknown.
- **Spray:** our client sends a blank spray logo.
- **Dedicated broadcast quirk:** after some round restarts the dedicated stops broadcasting entity
  changes; the original client shows the same, so it is server behaviour.
- **Weapon selection HUD, not reconstructed** (KNOWLEDGE "Weapon selection HUD"):
  - Our inventory holds one weapon per `cs2d_weapon_slot_t`, so slot 4 lists one grenade (the original lists HE,
    Flashbang and Smoke with their counts, e.g. "2 x").
  - "clip | INF" under infinite ammo (`0x140a14e38[1]`, language 0762) and "0 x - Remote" (0763, slot 6).
  - The HUD sounds (`0x140a0d628` / `d630` / `d640`), `lastinv`, and the gamepad path (`FUN_140239bd0`).
  - Bindings: only the weapon selection commands go through `sys/controls.cfg`; movement, reload, use, buy, drop and
    the spectator keys are still hardcoded in `game_handle_key`. `cs2d_bind_key_name` knows only digits, letters,
    space, leftctrl and leftshift. The HUD font scale is fixed at 1 (13 px).
- **Buy menu, not reconstructed** (KNOWLEDGE "Buy menu" has what is done):
  - `autobuy` / `rebuy` only print a message (the original's `FUN_1401fff70` path with `DAT_140ddf2e0` / `2e4`).
  - The custom buy menu (`custombuy`, when the setting `0x140a14e20[1]` is longer than 2 characters) and the "menu
    disabled" case (it starts with "-"): the setting is not received yet, so the default menu always opens.
  - The radial menu variant (`DAT_140dde9ac`, `TRadialMenu`), the voxel weapon preview (`DAT_140a14ca0`), the button sound.
  - Offline, buying ignores buy zones and buy time (online the server decides and answers `[23, pid, 250]`).
  - Not compared with a screenshot of the original: the window frame / title reuse the "Construct" menu's colors.
- **Server browser, not reconstructed:**
  - U.S.G.N. serverlist download: the Internet tab always uses `sys/core/svl.cache`.
  - Filters dialog: the button only shows the count from `sys/filters.cfg`, and filters are not applied.
  - Right-click to add a favorite, and the "Add to Favorites" button in the middle of the footer.
  - Writing `favorites.lst` / `recentlyjoined.lst`: joined servers go to the Recently Joined tab in memory only.
  - Sorting by the other columns, and the player list request `[FB][5]`.
- **Server browser, not measured:**
  - The meaning of the "M" header icon (frame 5 of `gui_server.bmp`).
  - The "Connect to IP" dialog layout.
  - The hover / selection highlight alpha (calibrated by eye).
  - Long names: the original scales them down to fit (`SetScale(225 / width)`), ours clips them.
    `cs2d_render_text_scaled_ex` now does this (used by the download window).
- **Join file transfer, not reconstructed** (see KNOWLEDGE "join-time file transfer"):
  - HTTP downloads (`url > path` entries, `Thttp`): well-formed ones are skipped and we always end the request with
    `0`, never `0xffff`.
  - `[252,4,11]` (the local map differs from the server's): the original asks "save as temp.map / overwrite /
    cancel" and joins again with map mode 1 or 2. We only print the status; the mode stays 0.
  - Files cached under `sys/cache/transfer/<sha>` (`[252,50,3]`) are saved there, but nothing reads them back yet
    (the original resolves Lua image paths through that cache). The checksum text format of `FUN_1400a1750` is assumed
    to be 8 × `%08x`.
  - The spinner icon (`gfx/gui_load.bmp`) next to the "Downloading" title.
  - Our Tudp has no retransmission of our own reliable packets; the request burst (26 messages) only works because
    nothing is lost on a LAN.
- **Join messages that differ from the original (proxy, not fixed):** the original sends `[252,0]` (2 bytes, ours
  adds an empty string), and in `[252,1]` it sends `01` after the U.S.G.N. fields and the spray file name
  (`unrealsoftware.bmp`) where ours sends empty strings. The dedicated accepts both.
- **Spectator, not reconstructed** (KNOWLEDGE "Spectator" has what is done):
  - The Menu / Map / Fog buttons are drawn but open nothing (game menu, big map `DAT_140a15478`, fog toggle).
  - `mp_spectatemouse`: the followed player's mouse cursor (`Tpl.mx/my`, `net_mspec`) is not drawn, and its
    network message is unknown. The kill cam (`Thud_killCam`) is not done.
  - Respawn game modes: "Click to respawn" sends nothing (`FUN_140298b30`), and the respawn countdown text
    (0767) is missing.
  - Avatars: the name box always shows the "?" placeholder (Steam / U.S.G.N. avatars, `DAT_140a14fe8`).
  - While spectating our HUD (timer, money, radar) is hidden; the original keeps its timer, money and minimap.
  - Clicking a player on the scoreboard or the minimap to follow it (`FUN_14029fc70`, `0x140299e7a`).
  - Snapshot section 1: the steam / usgn name strings are assumed to be u16 count + UTF-16 (always empty so far).
- **Map editor, not reconstructed yet** (KNOWLEDGE "Map editor" has the layouts):
  - Map Properties (Map Settings window), Display Settings, and the Tools / More Options mode (Distance, Path
    Distance, Pathfinder, Tile Blender, Decal Painter, minimap sizes, export, move map).
  - Entity Properties window (clicking a placed entity), the tile context menu (right click), undo
    (`Teditor_actionlist`), tile blending (`0x40`), the unsaved-map and "save as default" confirmations.
  - New Map makes a 50x50 `cs2dnorm.bmp` map (size not measured); the 600 px/s scroll speed is a guess; the selection
    pulse speed (`_DAT_140ddeff8`) is calibrated by eye.
  - Entities are deleted with Delete over them (the original uses its context menu).
  - The overlay frame of colored tiles (`Tmap_d.overlay`) is loaded and saved but not drawn.
  - With the console open in the editor, the sidebar buttons ("Tile Properties", "BR", z 11) and the tile tooltip
    are drawn over the console window (z 10).
- **Scrolling, not measured:** wheel steps (3 lines for the console / Server Info / server list / map files,
  1 row for the dropdown and the editor lists) and the thumb hover color are our choices, not taken from `CS2D.exe`.
- **Map saver:** a map without daylight (0) is saved as 225 + 1000; maps saved with "heights 0 / 1" are re-saved with 2
  (like the 1.0.1.8 editor, not checked).

- **Construction / dynamic objects, not reconstructed or not measured** (KNOWLEDGE "Construction mode"):
  - No screenshot of the original's buildings / "Construct" menu yet: the menu window colors reuse our Tgui window,
    the "X" button (`Tgui_hudmen.fbutton`) is drawn like a small button, and the center message shade bar and fade
    (1 s) are calibrated. The team menu buttons sit at window + 80 in our UI while every `Tgui_hudmen.button` call
    passes x = 16: one of the two is wrong (check on a screenshot).
  - Not drawn: portals (22 / 23), NPCs (30), Lua images (40, `Tdo` image fields and `[252,7,7]` image data),
    laser mine beams, Construction Site smoke particles, dynamic lights; turret tracers use a calibrated 300 px reach.
  - Building hit debris (`cs2d_fx_building_hit`): no hit sound (array 0x140a0d890 not identified), fragments do not
    stop at walls, the `particles` setting is fixed at 4, and the look is not compared with a screenshot yet.
  - Building hover text (KNOWLEDGE "Construction mode"): not compared with a screenshot of the original; the text
    height uses our 15 px line height for Max2D `TextHeight`. Hostage (bit 4) hover text and the ground item hover
    (`FUN_14029ffb0`, `THudItemHover`) are not done.
  - Player hover text: no smoke cover check (no smoke grenades) and no `Tpl.fog` (no fog of war); the click on the
    hovered player to spectate it (`FUN_14029fc70`) is not done. `team_game()` stands for both `0x140a015d0` tests
    (buildings `== 1`, players `!= 0`): the table values are not read yet.
  - Not handled: teleporter use
    ("Select Teleporter Destination" 0528), Supply (`FUN_14028dba0` item menu, menu 61/62), Gate Field team blocking
    for movement, kill rewards and `mp_building_limit` / `mp_building_price` overrides.
  - `g_prop_class[cellprop] == 1` is approximated by our collision 0; the team-game table `0x140a015d0` is assumed
    (every mode but Deathmatch) and the respawn-mode table `0x1409f57b8` as modes 1..4.
  - `[252,7,3]` items and `[252,7,6]` ground projectiles are read, but `[252,7,7]` images and `[252,7,8]` tweens are not.
- **CTF:** captures, returns and scores (`[64]` setteamscores, `[226]` messages) are not shown on the HUD; the flag
  drawing follows the code but was not compared with a screenshot of the original.

## Verification debt
- **Original + our client, one session:** not re-run after the latest join/leave fix (the session ended
  in a BSOD). Run `twoplayer_probe.lua` with the original firing next to our client and check the remote
  shot effects on screen.
