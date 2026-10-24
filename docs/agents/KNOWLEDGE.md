# Verified knowledge (shared agent memory)

Facts confirmed against the original binaries. Each one names its source:

- **REA**: Ghidra address in `CS2D.exe` (client) or `cs2d_dedicated.exe` (ded).
- **proxy**: a UDP capture of the original client.
- **Lua**: a probe on the dedicated server.
- **measured**: a calibrated value.

When you confirm or refute something, update the topic file (or add one and list it below). All multi-byte integers are little-endian.

## How to use this

**Do not read every topic file.** Read this index, then open only the files for the area you are working on
(grep across `docs/agents/knowledge/` is fine). Keep each topic file focused; a new area gets a new file and a row here.

| File | Topics | Size |
|---|---|---|
| [blitzmax.md](knowledge/blitzmax.md) | BlitzMax binary layout | 1 KB |
| [net-transport.md](knowledge/net-transport.md) | Network: transport (Tudp) | 1 KB |
| [net-join.md](knowledge/net-join.md) | Network: join | 9 KB |
| [net-transfer.md](knowledge/net-transfer.md) | Network: join-time file transfer | 5 KB |
| [net-messages.md](knowledge/net-messages.md) | Network: client → server (proxy); Network: server → client (proxy, Lua) | 13 KB |
| [server-browser.md](knowledge/server-browser.md) | Server browser (Find Servers) | 4 KB |
| [movement.md](knowledge/movement.md) | Movement and collision | 1 KB |
| [entities.md](knowledge/entities.md) | Entities, Env_Decal, Gen_* (particles, sprites, weather, FX), implementation checklist | 7 KB |
| [breakable.md](knowledge/breakable.md) | Env_Breakable and shots | 1 KB |
| [shot-effects.md](knowledge/shot-effects.md) | Shot effects | 5 KB |
| [spectator.md](knowledge/spectator.md) | Spectator | 5 KB |
| [team-menu.md](knowledge/team-menu.md) | Server Info and Team Selection UI | 1 KB |
| [map-editor.md](knowledge/map-editor.md) | Map editor | 8 KB |
| [dynobj.md](knowledge/dynobj.md) | Construction mode and dynamic objects | 40 KB |
| [hostages-bomb.md](knowledge/hostages-bomb.md) | Hostages, use key targets, bomb plant / defuse ([26] [29] [30]) | 5 KB |
| [capture-the-flag.md](knowledge/capture-the-flag.md) | Capture the flag | 2 KB |
| [weapon-select.md](knowledge/weapon-select.md) | Weapon selection HUD and bindings | 7 KB |
| [progress-bar.md](knowledge/progress-bar.md) | In-game Action Progress Bar (Reloading, Planting, Defusing) | 2 KB |
| [map-list.md](knowledge/map-list.md) | Map list ("New Game" / Create Server) | 1 KB |
| [buy-menu.md](knowledge/buy-menu.md) | Buy menu | 5 KB |
| [chat.md](knowledge/chat.md) | In-game chat and server messages | 7 KB |
| [hud-text.md](knowledge/hud-text.md) | Lua HUD texts (hudtxt [234], Ttween [54]) | 4 KB |
| [lua-menu.md](knowledge/lua-menu.md) | Lua Server Menu (menu() / opcode 235) | 3 KB |
| [game-menu.md](knowledge/game-menu.md) | Ingame menu (Esc), vote [43], leaderboard [230], map briefing, [252,6] tail | 4 KB |
| [scoreboard.md](knowledge/scoreboard.md) | Scoreboard | 3 KB |
| [env-object-tileblend.md](knowledge/env-object-tileblend.md) | Env_Object and tile blending | 3 KB |
| [settile.md](knowledge/settile.md) | settile: [62], [252,7,9], Tcustomtile, blend byte | 5 KB |
| [rendering-notes.md](knowledge/rendering-notes.md) | Rendering notes | 7 KB |
| [light-engine.md](knowledge/light-engine.md) | Light engine (TSmartLighting) | 10 KB |
| [projectiles.md](knowledge/projectiles.md) | Projectiles and Persistent Fields (Tpro, Tfield) | 8 KB |
| [random.md](knowledge/random.md) | TRandom (BlitzMax PRNG): the shot spread seed from [252,6] | 3 KB |
| [radar.md](knowledge/radar.md) | Advanced radar / minimap (TSmartRadar / TSmartMinimap) | 4 KB |
| [web-port.md](knowledge/web-port.md) | Web/Wasm portability (Emscripten), platform layer, text engine (GDI+ vs stb_truetype metrics) | 12 KB |
| [special-weapons.md](knowledge/special-weapons.md) | Laser, flamethrower, launchers, Portal Gun; consumable items, spawned item ammo | 6 KB |
| [sound.md](knowledge/sound.md) | Sound effects: play formulas, sound globals → files, triggers | 5 KB |
| [dedicated-server.md](knowledge/dedicated-server.md) | Dedicated server port: Tudp server side, recv 252 join checks / status codes, [252,6] fields, snapshot builders, ping block, Lua 5.1.4 static, bot AI (`bots/Standard AI.lua`), NPC AI and the NPC/object Lua hooks | 89 KB |
| [standard-gamemode.md](knowledge/standard-gamemode.md) | Standard game mode (`sv_gamemode 0`): round end reasons [226], win conditions, time out, freezetime, V.I.P., hostages, bomb | 12 KB |
| [performance.md](knowledge/performance.md) | Performance (measured 2026-10-12, desktop, 850x480) | 12 KB |
