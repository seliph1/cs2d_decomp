# Open issues / next steps

Keep this file current: remove items when they are done (and log them in `WORKLOG.md`), and add what
you discover. Sign items that are being worked on (`[in progress: <agent>]`) so two agents do not take
the same one.

## Dedicated server — residuals

- **Dedicated server: `mp_anticlock` clock tracking (2026-10-20):** the cvar now selects
  `[229]`/`[249]` and the `[247]` ping list is locked by tests, but the anti-speedhack part of the
  `[229]` reply (`clock_diff` / `clock_ratio`, 0x1402e2aa0, kick "Speedhack Detected") is not
  implemented; only the RTT (`Tpl +0xd0`) is used.

- **Dedicated server: bot and NPC AI, residuals (2026-10-20, `knowledge/dedicated-server.md`):** bot secondary attack only relays `[8]`
  (no bot grenade projectile); shotgun bots apply one pellet hit; the per-skill turn/range tables are runtime arrays
  (ours calibrated); a lost/reordered reliable `[36,7]` can leave a temporary client offset until the next waypoint;
  the damage-assist tracker (and the `assist` hook) is not implemented; the Tactical Shield (and its `shieldhit` /
  attacker-object-id hook) is not modelled in the damage path.

- **Client: local player [253] (2026-10-20):** the dedicated already sends `[253,id,16,reason]` to the kicked client too
  (the original shows "Disconnect Reason" in `FUN_1401c7500`; test `test_dedicated_kick_message`), but `cs2d_client.c`
  only removes the player when `id != local_player_id`; our client still lacks the kick/disconnect UI.

- **Dedicated server: WQ2D roleplay trigger cascades (2026-10-20, `knowledge/dedicated-server.md`):** after 16 s our server ends with
  1618 vs 1574 entities in state 1 and 587 vs 565 `trigger` calls (late Trigger_If/Delay cascades: `d4entrance`,
  `sc*_light`, `d4mp*`); objects and items match (27/57 turrets, 120 items). Investigate the exact forwarding
  semantics of `Trigger_Delay` and the `Trigger_If` conditions > 0. Regression: `test_dedicated_trigger_graph`.

- **Round restart / win conditions, offline client (2026-10-16, `knowledge/standard-gamemode.md`):** the dedicated is fixed
  (event-driven `cs2d_sv_check_win`); the offline client (`src/cs2d_game.c`) still checks every frame and keeps the
  "simplified" win conditions; a 2-client elimination round was not captured side by side.
- **Standard game mode, offline client (2026-10-17, `knowledge/standard-gamemode.md`):** the dedicated is a 1:1 port; the offline
  model (`src/cs2d_game.c`) still has the simplified win conditions without the mission types, and its
  `cs2d_game_start_plant` / `cs2d_game_tick_plant` are dead code: offline (`cs2d_client.exe` without a server) a plant never
  completes and no planted-bomb ground item is created, so the C4 stays invisible and cannot be defused there (the online
  path is fixed - see `knowledge/hostages-bomb.md`).
- **Construction mode (2026-10-17, `docs/agents/knowledge/dynobj.md` "Construction mode, server side"):**
  the dedicated builds (`[34]` + `FUN_140175550` rules), finishes Construction Sites, runs the turret scan /
  burst and the Wrench with 2px-step raycast probe (`Tdo.getatpos`), damages buildings with bullets, melee
  and explosions (C4, mines, projectiles), honours the `turretscan` hook, teleports players onto charged
  entrances, applies the barbed wire / gate field / mine and Laser Mine beam detonation (`0x14000aa30` `Tdo.inlasermine`),
  handles dynamic `mp_building_price` / `mp_building_limit` / `mp_building_health` console commands (`0x140118060`)
  with health clamping (`0x1401182a6`), and runs Dispenser / Supply effects (1 Hz) and respawn spawn money.
  Locked by `test_construction_laser_mine_and_cvars`. Still open:
  - **Not verified against the original**: no proxy capture of the original building next to ours (the bytes
    are only checked against the client's own parsers in the test suite); the "X" buttons
    (`tx == ty == 0xffff`) do nothing, matching the dedicated's jump to its epilogue; the turret shot
    throttle (100 ms), the burst damage (5), the Dispenser / Supply 1 Hz cadence (10 HP/s, $500/s, ammo
    at 5s/10s, supply at 7s), the C4 blast radius (350 / 500), the gate field 20/100 and the barbed wire
    20/60 are read from the code, not measured on a capture.
  - **`sv_gamemode 3` mode rules**: verified inert (`cs2d_sv_check_win` skips `respawn != 0` and
    `cs2d_sv_step_round` only ticks the objectives for round modes - locked by
    `test_construction_rounds_and_explosion`).
- **Special weapons / items (2026-10-17, `docs/agents/knowledge/special-weapons.md`):**
  - Consumables & stackables pickup faithfully reconstructed from `FUN_14020ce60` / `FUN_14020ddc0` (jump table `0x140bc6104` / `0x140bc6194`) and `0x14020e3c0`: Primary/Secondary Ammo reserve refills, Medikit/Bandage health thresholds, Coins/Money/Gold $16000 clamp, Kevlar 65/100 armor thresholds, and partial stack clamping (Grenades, Snowballs, Mines, Laser Mines, Satchel Charge) where residual clip remains active on the ground. Locked by `test_consumables_and_stackables_pickup` (85/85 tests).
  - Still open: money indicator `Thud_msg.add_money` on Coins/Money/Gold, laser / flame lights and TSparkle details, Tp 23 / Tp 0x1d sprites (approximated with glowdot), Satchel Charge attack / attack2 (FUN_1401f2670), offline teleporter / portal messages (g_lang 0xb3), mp_teleportreload offline (taken as 0), Portal Gun not verified on a capture.
- **attack2 / shotgun (2026-10-14):** the spread now uses `TRandom.SeedTable`, seeded from the settings `[252,6]`
  (`knowledge/random.md`). The accuracy table `0x1409f5d18` is fully extracted (27 ids) and locked by
  `test_weapon_modes`; only `Tpl.shoot` reads it and `Tpl.attack` calls that for the M3 / XM1014 alone, so the
  other weapons correctly fire without spread - nothing left here. `[8]` not checked against a proxy capture;
  whether attack2 repeats while held (we do, 500 ms) is untested on the original; the scope hint text
  (`DAT_140ddf19c`, lang 0x36 group) and the Portal Gun (88/89) attack2 are missing; offline knife attack2 hurts
  breakables with dmg, not dmg_z1.
- **Muzzle table 0x1409f5d20, candidates (2026-10-15):** re-extracting it with `tools/agent/weapontables.py`
  (after teaching the tool the `lea reg,[base+id*4]` form) reproduces all 39 ids `s_muzzle` already has, plus
  ids we leave 0 and cannot confirm yet: 41 Tactical Shield 22, 55 Bomb 1, 56 Defuse Kit 2, 57 Kevlar 3,
  58 Kevlar+Helmet 3, 72 Gas 16, 73 Molotov 16, 75 Snowball 16, 76 Airstrike 16, 77 Mine 16, 86 Gut Bomb 14 and
  an id 92 (muzzle 27, accuracy 2) we do not model. Two entries of that run are clearly mis-paired
  (14:3000 and 85:60 against our verified Chainsaw 30), so the extras need a second look before they go in.
- **Hostages / bomb (2026-10-14, `knowledge/hostages-bomb.md`):** missing: c4 plant / defuse
  sounds, own Dispenser / Teleporter menu on E (Tdo 9 / 15), the green "following me" marker
  (Tp 100 at the hostage, mp_hovertext-like), [29] kind 4 blood (Tp 0x18) and the bomb blast particles (FUN_1401f1130,
  screenshake 50; ours uses cs2d_fx_explosion), hostage money HUD messages (Thud_msg.add_money), hostage legs (the original
  never sets `lastmove` on the client, so it likely draws no legs: compare a screenshot), hostage step is 4 px per drawn
  frame and we assume 60 fps. `0x140a14e48[1]` (hostage spawn gate) taken as sv_gamemode 0, unverified.
- **Ingame menu (2026-10-14, `knowledge/game-menu.md`):** not compared pixel by pixel with CS2D.exe. Guessed: game mode
  and voice chat value names (runtime arrays 0x140a1ccf8 / 0x140a14ed0), Players colours, vote row height (16 px) and the
  vote lists have no scrolling; vote player names are drawn gray (original screenshot shows team colours). Leaderboard:
  no avatars (`drawAvatarRect`), MVP star drawn as text, the "no answer yet" placeholder text (`bbs____`) not drawn.
  Briefing lines are not word-wrapped (Tgui_tb). The original keeps the menu behind sub-windows; ours closes it.
  Disconnect packet **verified** (2026-10-16): the original sends `[253, reason]` (`FUN_1401c7500`, `0x1401c7970`)
  and "Disconnect / Quit" passes reason 0, i.e. `fd 00` - byte-identical to ours (`knowledge/net-messages.md`).
  Still open: the reason codes the *server* sends in `[253,pid,reason]` to the other clients are not compared.
- **Lua HUD texts (2026-10-14, `src/cs2d_hudtxt.c`):** `mp_hudscale` implemented (modes 0..3, zoom, aspect letterbox, and sharpness scaling when screen_h > 589); [54] tweens of Tdo targets (Lua images) are parsed and skipped; `®path` inline images not
  compared with the original; texts are cleared on map load (original reset point not checked).
- **Gen_* entities (2026-10-13, `src/cs2d_gen.c`):** not screenshot-compared with CS2D.exe. Missing: wind on Tp 22 / 0x20 (not spawned by us), screen shake
  from explosions / Lua (only Info_Quake feeds `gen.shake`),
  the FOW hiding (`FUN_140146fd0`).
  Settings gating rain / snow / effects (DAT_140a1b068, 140a1b05c, 140ddf540) are assumed on, "particles" = 4.
  Thunder decoded as a 10..30 *frame* countdown (assembly 0x14027525b), which seems very frequent: verify on the
  original. smokepuff masks (mode 3) are our choice. Spawns follow 60 Hz, the original spawns per rendered frame.
- **Loading screen (2026-10-13):** `cs2d_render_loadscreen` lacks the menu background and DrawWindow corner
  images/spinner; progress uses our 6 steps, not the original `PROCESS_MAPS`. Not compared to a screenshot yet.
- **Info_TileFX (2026-10-14, `cs2d_tilefx.c`):** not screenshot-compared with CS2D.exe. Not applied to tile frames
  drawn outside the map pass (Env_Breakable / Func_DynWall, tile blending sources); the frame clock is the global
  tick instead of a per-effect timer; built on the first draw, not at map load; ignores DAT_140ddf17f (as tile blending).
- **No frame cap with VSync off (measured 2026-10-13):** the original locks its frame at ~65 FPS
  (`15.384615 ms`, `docs/dump/cs2d/_global/1402f0000.c:2284`) whether VSync is on or off; our loop only
  does `SDL_Delay(1)` (~1.5 ms), so it runs at 167 FPS and pegs a CPU core. Measured on `fun_roleplay`:
  VSync ON 22.7 % GPU / 36 % CPU, VSync OFF **42-46 % GPU / 81-114 % CPU** (the CPU past one core).
  Decide whether to reproduce the original's cap; it is the biggest lever for the GPU number the user
  sees whenever VSync is off. Numbers: `KNOWLEDGE.md` "Performance" (2026-10-13).
- **GPU per frame, not yet optimised (measured 2026-10-13):** a drawn `fun_roleplay` frame costs
  **1.75 ms of GPU with light** (down from 1.91-2.48 ms before entity atlasing; 1.32 ms with `light 0 mapshadows 0`):
  ~40 % of the steady-state load is the light engine + map shadows (`light 5 mapshadows 4`); MSAA 4x is
  within the run-to-run spread but costs ~1.1 ms of CPU per frame (`CS2D_MSAA=0` in `src/main_client.c`
  turns it off). Still unattributed or pending optimization:
  - **light engine draw calls & texture switches:** 273 calls and 256 texture switches disappear
    with `light 0`; packing the light engine's internal texture set (room map, roof layers, occlusion,
    blur, stripes) remains the next atlasing target. Entity sprites (`Env_Sprite` / `Env_Image`) are
    already atlased (2026-10-13).
  - **per-entity sprite draws** (up to ~1000 considered per frame on `fun_roleplay` after the entity
    index) and the per-entity margin decisions in `cs2d_render.c`.
- **Audio (2026-10-14, `knowledge/sound.md`):** engine + shots, impacts, explosions, hit / die, items, Env_Sound,
  [65], thunder, flashbang, hostage, HUD slot sounds. Not compared by ear with CS2D.exe; pan law and the hit trigger
  are estimates. Missing: footsteps (`pl_*` arrays 0x140a0d910..8e0), reload sounds by mod items (items[typ] + 0x20);
  shell reload `[16,pid,4]` not proxy-captured, clip reload kept at reload x 10 ms vs the x 5 ms stage of
  FUN_140214780; c4 beeps / plant / defuse, grenade bounce, radio, kill-streak `sfx/fun`, menu music,
  GUI clicks, sv_sound (`Tserversound.playServer`), Env_Sound one-shots triggered online, `volume` options slider.
- **Shots:** offline shots have no spread, and the reach of weapons other than the knife and the AK-47
  is not measured.
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
- **Env_Object and tile blending, not reconstructed** (KNOWLEDGE "Env_Object and tile blending"):
  - `Te.age` is per entity and only advances on screen; we use one clock for every object.
  - Sink / Toilet are drawn on our ground pass, under the ground items (the original draws `Te.draw(1)` after items).
  - The 3D mode (`Tile3D`).
  - The setting `DAT_140ddf17f` that turns tile blending off is not read; we always blend. In the editor, painting does
    not re-run `updatetile` (we compute the neighbour every frame, so a cleared 0x40 flag never stays cleared).
- **Radar (2026-10-14, `cs2d_radar.c`):** icon queue ported from FUN_140291bd0; minimap texture now uploaded once per
  map. Missing: hostages (no client hostage objects), death icons (`DeathIcon`, frame 4), beeps (`Thud_radar_blink`),
  `Tpl.radarblink` red pulse, `Mode 2` pulsing bomb / rescue / escape tiles, and the full map (`TSmartMinimap`).
  Assumed: DAT_140ddf4b4 = 0, DAT_140ddf078 / 074 = Info_BombSpot / Info_CTF_Flag counts, item 63 = planted C4.
  Our radar is 128 px vs the original BufferSize 100: icon size (16 px) and edge circle (51) not compared with a screenshot.
- **Kill feed (2026-10-14):** not compared with a screenshot of the original. Missing: the small font under 700 px
  (DAT_140a14f88), the mp_hud kill flag (0x40), Lua `customkill` images (`[226]`-style case 4 in FUN_1401de400, wpnimg
  -1) and offline kills (our offline damage has no killer). Specials 0xfd / 0xfe / 0xff assume 0x1409f79f8 is indexed
  by building type (Turret / Gate Field / Barbed Wire).
- **Opcode 40 (kill dropped weapon):** `[40, id u16]`, reliable, sent by `Titem.fadeout` (0x14004cd20) to every
  connected player. Its trigger is the tail block of `Titem.draw` (0x140147b80 / 0x1401483a0): a *dropped* item's
  timer steps **once per 1 s tick** (`DAT_140ddf540`) and only while this instance runs the server
  (`DAT_140ddf400`), so the threshold `0x140a14d94` = 15 is **15 s**, and a network client never fades an item
  itself (`cs2d_ground_item_step`; `knowledge/net-messages.md` "Ground item lifetime"). Still to verify on a
  capture: that the dedicated broadcasts `[40]` ~15 s after a drop (and that our client removes the item then),
  and the third gate `0x140a14e48[..] & 0xfb`, an unidentified setting we assume on.
- **Round start and ground items:** whether dropped weapons are removed at round start is unknown.
- **Spray:** our client sends a blank spray logo.
- **Dedicated broadcast quirk:** after some round restarts the dedicated stops broadcasting entity
  changes; the original client shows the same, so it is server behaviour.
- **Weapon selection HUD, not reconstructed** (KNOWLEDGE "Weapon selection HUD"):
  - Grenade counts: not checked on a screenshot of the original. The Medikit (89) keeps clip 0
    in our table although 0x140a017a8 gives 5 (using it would then consume a count: not verified). Partial pickup
    (2026-10-14) follows FUN_14020ce60 but was not reproduced against the dedicated (does it send [25] for a stack
    that only partly fits?).
  - The HUD sound on closing / confirming (`wpn_hudoff` d638), `lastinv`, and the gamepad path (`FUN_140239bd0`).
  - Bindings (2026-10-15): every game key and mouse button goes through `sys/controls.cfg`. Not done: the other
    key names (kp_*, rightshift / rightctrl / rightalt are folded into the left ones, keypad digits act as the digit
    row, ins / del / home / end / pgup), `mouse4/5`, and the commands radio1..3, voicesay, chooseteam, votemenu, bot,
    lastinv, nightvision, spray, snapshot (bound but run nothing). The HUD font scale is fixed at 1 (13 px).
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
  - Writing `recentlyjoined.lst`: joined servers go to the Recently Joined tab in memory only (the browser loads
    `sys/favorites.lst` / `sys/recentlyjoined.lst`, but only the in-game menu's Favorite row writes favorites).
  - Sorting by the other columns, and the player list request `[FB][5]`.
- **Server browser, not measured:**
  - The meaning of the "M" header icon (frame 5 of `gui_server.bmp`).
  - The "Connect to IP" dialog layout.
  - The hover / selection highlight alpha (calibrated by eye).
  - Long names: the original scales them down to fit (`SetScale(225 / width)`), ours clips them
    (`cs2d_render_text_scaled_ex` exists but the server list does not use it).
- **Join file transfer, not reconstructed** (see KNOWLEDGE "join-time file transfer"):
  - HTTP downloads (`Thttp`) are ported but not checked live against the original (needs a web server on port 80 and
    `url > path` lines in the dedicated's `sys/servertransfer.lst`). Unverified: the closing literals of
    "HTTP download '…' to '…'", "Failed To write '…'", "Unexpected reply (…"; the `[252,51]` byte (Tgui_joinmen +0x68,
    taken as `menu` = 10); what the server does after `[252,51]`.
  - When the original clears `lua_filetransfer.cache` (CacheClear callers unclear); ours keeps it until exit.
  - The spinner icon (`gfx/gui_load.bmp`) next to the "Downloading" title.
  - Our Tudp has no retransmission of our own reliable packets; the request burst (26 messages) only works because
    nothing is lost on a LAN.
- **Spectator, not reconstructed** (KNOWLEDGE "Spectator" has what is done):
  - The Menu / Fog buttons open nothing (game menu, fog toggle). Map toggles the minimap; its hostage / flag / bomb /
    Dom_Point dots, the per-player fog test under sv_specmode 2 and the `TSmartMinimap` variant are missing.
  - `mp_spectatemouse`: the followed player's mouse cursor (`Tpl.mx/my`, `net_mspec`) is not drawn, and its
    network message is unknown. The kill cam (`Thud_killCam`) is not done.
  - Respawn game modes: "Click to respawn" sends nothing (`FUN_140298b30`), and the respawn countdown text
    (0767) is missing.
  - Avatars: the name box always shows the "?" placeholder (Steam / U.S.G.N. avatars, `DAT_140a14fe8`).
  - Spectator time / money (2026-10-14): mp_hud bits, DAT_140ddf1cc and the mode table 0x1409f77e8 are assumed on;
    the text uses alpha blending instead of the original's additive blend; not compared with a screenshot.
  - Clicking a player on the scoreboard or the minimap to follow it (`FUN_14029fc70`, `0x140299e7a`).
  - Snapshot section 1: the steam / usgn name strings are assumed to be u16 count + UTF-16 (always empty so far).
- **Map editor, not reconstructed yet** (KNOWLEDGE "Map editor" has the layouts):
  - Map Properties (Map Settings window), Display Settings, and the Tools / More Options mode (Distance, Path
    Distance, Pathfinder, Tile Blender, Decal Painter, minimap sizes, export, move map).
  - Entity Properties: opened by a right click on the entity (the original's context menu is missing); no decal
    preview (`Tgui_img` 0x140a0e850); Okay does not re-run `Te.setup` / `cs2d_update_mapdata` (nothing in our editor
    caches them). Not compared to screenshots of every type, only Env_Hurt's layout.
  - The tile context menu (right click), undo
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
    (1 s) are calibrated. The "Select Team" button x is the screenshot's value (KNOWLEDGE dynobj: window +
    (80, 100) for the team menu, window + (16, y) for "Construct").
  - Not drawn: laser mine beams, Construction Site smoke particles, dynamic lights.
  - Lua images / NPCs (2026-10-14, KNOWLEDGE dynobj "Lua images"): not compared with a screenshot of the original.
    Images: player avatars (no avatar download; the avatar_load placeholder always spins), physics parents
    (`[51,8]`, no physics objects), image obstacle shadows (ObstacleShadow2 / TLightingSprite for shadow 0), 3D tiles
    (Tile3D) and fog of war on follow images are not done; solid blend draws as alpha 1; `<tile:N>` and the flag
    images are assumed midhandled. The vortigaunt beam's colour mode 0 is still assumed. Missing: the gore-gated
    ground blood decals and the snark's 9-particle fountain, and the health table 0x1409ed7f0 (a runtime array).
  - Building hit debris (`cs2d_fx_building_hit`): the look is not compared with a screenshot. `particles` (`DAT_140ddf182`) is read
    from sys/config.cfg instead of the fixed 4; the `3 < particles` gates of the water splash / flashbang /
    explosion lights still assume the shipped value.
  - Building hover text (KNOWLEDGE "Construction mode"): not compared with a screenshot of the original; the text
    height uses our 15 px line height for Max2D `TextHeight`.
  - Player hover text: no smoke cover check (no smoke grenades) and no `Tpl.fog` (no fog of war); the click on the
    hovered player to spectate it (`FUN_14029fc70`) is not done. `team_game()` stands for both `0x140a015d0` tests
    (buildings `== 1`, players `!= 0`): the table values are not read yet.
  - Not handled: teleporter destination menu
    ("Select Teleporter Destination" 0528), Gate Field team blocking
    for movement, kill rewards and `mp_building_limit` / `mp_building_price` overrides.
  - The respawn-mode table `0x1409f57b8` is assumed as modes 1..4; like `0x140a015d0` it is a BlitzMax array filled
    at runtime (`PTR_bbEmptyArray_...`), so it was not read.
  - `[252,7,11]` weapon settings are not read.
- **CTF:** capture messages (0589 / 0590), scores (`[64]`)
  and the return money bonus are not. Dropped ground items have no random rotation (Titem.add Rnd(360)). The draw order
  of the base / carried flag over the walls is from a screenshot, not from the code.
- **Light engine, not reconstructed** (KNOWLEDGE "Light engine"; what exists matches the original call for call):
  - Approximations: our fx particles are not a Tp port, so their light sprites mirror our sizes; the Vortigaunt beam color (mode 0)
    and NPC shot spread are assumed; images on physics objects keep no shadow; the C4 radar blink is not drawn.
    `FUN_14025f0e0` / `draw_flying_shadow` are not covered by the emulator (stubbed), only by eye.
  - Flashlights: `Tpl.fog` (FOW-hidden players) does not switch them off; the flare1 on the player skips the per-frame
    mp_flashlight / Zombies check (the flag is only set when they pass); the `ChangeRotation` argument (xmm9) is
    assumed to be Tpl.rot read at 0x14021d185; `FUN_140146fd0` (FOW) does not hide muzzle lights.
  - The player silhouette in the day shadows leaves out the carried bomb / defuse kit; `ObstacleShadow` (player parts as
    obstacles) is not filled.
  - `DrawStripesLightColor`, `render3d` (`DAT_140ddf084`), and the map editor (the original lights the editor too; ours
    turns the engine off there).
  - Trigger_Use buttons are drawn on our ground pass, not on `Te.draw(1)`.
  - The `light 0` path (old CPU shadow map) was never compared with the original's `mapshadows 1`.
  - The sprite / obstacle queues (Env_Sprite, palm / tree leaves, alarms) are not covered by the emulator test (the scene
    file does not carry them); they were checked on screenshots only.

## Dedicated server (cs2d_server.exe) — started 2026-10-15
Goal (user): 1:1 port of `cs2d_dedicated.exe`, tested with the original CS2D.exe; every hook, console command,
Lua function and script must work; final test = the server scripts in `C:\C4`. Decisions: **Lua = PUC Lua 5.1.4
vendored** (user, 2026-10-15; LuaJIT may become a build option later); **bots: AI ported 2026-10-20** (was "present,
no AI"). Knowledge: `knowledge/dedicated-server.md`; test: `tools/agent/capture_server.sh`. Phases 1-8 and the
2026-10-19 gap audit are done (history in `worklog/archive.md`, "Moved from OPEN_ISSUES"); what is left:
- **Join snapshot:** section 11 is still the 170-byte capture literal (build it from the weapon tables), section 13 is
  always `""`; `FUN_1401dba60` (3 bytes at the end of `[252,6]`) is hardcoded zeros; player id allocation order
  (`FUN_140098050`); `[247]` counters are ours (fps / bytes); `[253]` leave text.
- **Estimated, not measured:** resend interval / timeout (500 ms / 30 s).
- **Secret generator:** still a random 10 letters; `PTR_PTR_1405a6800`'s generator is not decompiled.
- **CVars still inert:** `mp_autogamemode`, `mp_shotweakening` (needs a server-side shot ray; our damage arrives from
  `[17]`), `mp_lagcompensationdivisor`, `mp_trace`, `mp_natholepunching`, `mp_localrconoutput`,
  `mp_radar`/`mp_hud`/`mp_smokeblock`/`mp_buymenu` (client display, stored/broadcast through `[237]` only).
- **Missing commands:** `flashplayer` and `flashposition` (client needs the `[48]` flash effect 4/5 and the screen
  overlay; `[48]` screen shake type 2 is not drawn yet).
- **Lua stubs:** `steamstats` returns an empty table (no Steam integration in this build).
- **Lower priority:** the `[252,10]` spray path is byte-tested but not re-checked with the original client;
  U.S.G.N. login / serverlist intentionally offline.

## GL 1.x / GDI+ removal (done 2026-10-12..2026-10-15, independent of the Wasm port)
Goal: drop the two Windows-only backends the renderer still depends on. The UDP network layer is explicitly out
of scope, and this is *not* the Wasm port — the phases just happen to be the same work the port needs.
All three phases are done (details in KNOWLEDGE "Image decoding is portable", "Text engine", "Renderer" and
`knowledge/web-port.md`); only the decisions and the owed verification below stay open.
- **Phase 2 — text: DONE off Windows (2026-10-15).** `src/cs2d_font.c` has a GDI+ backend (still the Windows
  default) and an stb_truetype backend with identical metrics (KNOWLEDGE "Text engine").
  Left: decide whether the desktop switches to stb too (pixels differ by 6..12 / 255 on average); the metrics
  corpus and the raster conventions it had to match are in KNOWLEDGE "Text engine".
- **Phase 3 — OpenGL 1.x: DONE (2026-10-12) via GL 3.3 core + shaders.** `src/cs2d_gl.c` + `include/cs2d_gl.h`
  own the drawing path (entry points from `SDL_GL_GetProcAddress`, one `texture * vertex colour` shader with an
  alpha-test `discard` in place of `glAlphaFunc`, one streaming VAO/VBO batcher); no fixed-function call is left.
  Entry-point inventory: KNOWLEDGE "Renderer".
  - **Pixel comparison (2026-10-15):** our frame vs the original's on the same scene (prison_funst3r, tile 44,25,
    850x480): whole frame **18.52 %** differing pixels, mean |delta| 4.93, PSNR 23.30 dB; playfield only (HUD +
    radar masked) **14.13 % / 2.32 / 28.19 dB**, against a 3.9-4.7 % / 0.35-0.83 run-to-run noise floor. **Still
    owed:** the residual (HUD strips + a darker region, tiles (38..41, 23..28)), the same diff on a daylight map
    (de_dust2) and a `light 0..5` / `mapshadows 0..4` sweep. Recipe and numbers: `knowledge/rendering-notes.md`
    "Pixel comparison".
  - **Light engine on core:** `glBlendFuncSeparate` + render-to-texture are core in 3.3 and the `emu_light.py`
    trace test still passes; the on-screen compositing (`m2d_draw_buffer`, `cs2d_gl_bind_target`) is now
    covered *with the light engine on* by the pixel comparison above, but not by a `light 0..5` /
    `mapshadows 0..4` sweep.
  - **Fallback removed:** the compatibility context is gone; a machine without a 3.3 core context fails to
    start (clear stderr message). Decide later whether to keep a legacy path.

## Web port (Emscripten) — started 2026-10-15
- **Done:** phase 0 (the suite under Node via `tools/web/build_tests.sh`) and an offline client in WebGL2
  (`tools/web/build_client.sh` + `serve.sh`, `out/web/cs2d.html?de_dust2`): main menu and de_dust2 render like
  the desktop capture (headless Chromium screenshots; eyeballed, no pixel diff). Facts in KNOWLEDGE
  "Web/Wasm portability".
- **Next: network (the user's decision).** The browser turns our UDP socket into a WebSocket to
  `ws://<server ip>:<port>/`. A gateway must relay WebSocket binary frames <-> UDP datagrams byte for byte (one
  UDP socket per browser, so the dedicated sees one client per source port). Open: where it runs (next to the
  dedicated / the player's machine), whether `cs2d_client.c` talks to it explicitly (a configurable
  `ws://gateway/?to=ip:port` instead of the implicit per-ip URL), `[229]` latency, the LAN scan (impossible).
- **Other gaps:** sound starts only after a user gesture (SDL3 uses the deprecated ScriptProcessorNode);
  persistence of `sys/config.cfg` / maps / downloads (IDBFS) not set up; `gfx/ sys/ maps/ sfx/` are preloaded
  whole (43.5 MB `.data`; CJK fonts and `gfx/backgrounds` excluded): trim to what is used; screenshot
  (`glReadPixels` GL_RGB) untested on WebGL; the `--capture` harness is not wired for the browser; no CI job.
- **Fonts:** CJK / Thai text has no fallback face in either backend (unchanged).

## Verification debt
- **Flaky Lua timer test (2026-10-16):** `test_server_phase4_lua` ("timer 1st step" / "hook log verification")
  failed once in ~7 runs while the machine was busy right after a build, then passed 5 runs in a row. The test
  drives a Lua `timer()` with wall-clock time, so it is load sensitive - not a behaviour difference. Worth
  giving it a fake clock if it keeps biting.
- **Harness `!mouse` clicks do not reach UI windows (2026-10-15):** with the Select Team window open
  (`team_menu_open`, set on the join ACK) six `!mouse <x> <y> down/up` steps on the documented button
  rectangles (`knowledge/team-menu.md`: 240x25 at (205, 120) with 5 px gaps) changed nothing - the
  dedicated never ran its `team` hook and kept answering `spawnplayer - player #1 is a spectator!`.
  The same client joins a team instantly with the console command `team ct` (or `team t|auto|spec`), so
  the *client* path is fine. Not investigated: whether the UI needs a mouse *motion* event before the
  press (`!mouse` only calls `cs2d_client_handle_mouse`, while the UI reads `client->mouse_fire` and
  `mouse_screen_x/y`, which the SDL motion path can overwrite), and whether a real mouse click on our
  window does pick a team - a human has to verify that. Use `team <side>` in `--capture` steps meanwhile.
- **Original + our client, one session:** not re-run after the latest join/leave fix (the session ended
  in a BSOD). Run `twoplayer_probe.lua` with the original firing next to our client and check the remote
  shot effects on screen.
- **settile (2026-10-15, `cs2d_customtile.c`):** bytes and Lua results verified on the dedicated; the look of changed
  tiles is not screenshot-compared with CS2D.exe. `Ttileblend.updatetile` clearing the 0x40 flag next to a same tile is
  still not stored (we test it at draw time), so addcheck's "already like this" test can differ for such tiles. The
  advanced radar image (`s_minimap_full`) keeps the map's tiles. A cut `[252,7,9]` aborts like the original's shared
  path (`[253]` is only sent by the error window's Okay/Cancel; `knowledge/net-join.md`); we close the join socket
  with the game scene instead of leaving it open for that answer.
