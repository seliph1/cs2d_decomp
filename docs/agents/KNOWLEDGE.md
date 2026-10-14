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
| [net-join.md](knowledge/net-join.md) | Network: join | 3 KB |
| [net-transfer.md](knowledge/net-transfer.md) | Network: join-time file transfer | 3 KB |
| [net-messages.md](knowledge/net-messages.md) | Network: client → server (proxy); Network: server → client (proxy, Lua) | 4 KB |
| [server-browser.md](knowledge/server-browser.md) | Server browser (Find Servers) | 4 KB |
| [movement.md](knowledge/movement.md) | Movement and collision | 1 KB |
| [entities.md](knowledge/entities.md) | Entities | 1 KB |
| [breakable.md](knowledge/breakable.md) | Env_Breakable and shots | 1 KB |
| [shot-effects.md](knowledge/shot-effects.md) | Shot effects | 3 KB |
| [spectator.md](knowledge/spectator.md) | Spectator | 3 KB |
| [team-menu.md](knowledge/team-menu.md) | Server Info and Team Selection UI | 1 KB |
| [map-editor.md](knowledge/map-editor.md) | Map editor | 4 KB |
| [dynobj.md](knowledge/dynobj.md) | Construction mode and dynamic objects | 9 KB |
| [capture-the-flag.md](knowledge/capture-the-flag.md) | Capture the flag | 1 KB |
| [weapon-select.md](knowledge/weapon-select.md) | Weapon selection HUD and bindings | 2 KB |
| [progress-bar.md](knowledge/progress-bar.md) | In-game Action Progress Bar (Reloading, Planting, Defusing) | 1 KB |
| [map-list.md](knowledge/map-list.md) | Map list ("New Game" / Create Server) | 1 KB |
| [buy-menu.md](knowledge/buy-menu.md) | Buy menu | 4 KB |
| [chat.md](knowledge/chat.md) | In-game chat and server messages | 2 KB |
| [scoreboard.md](knowledge/scoreboard.md) | Scoreboard | 2 KB |
| [env-object-tileblend.md](knowledge/env-object-tileblend.md) | Env_Object and tile blending | 2 KB |
| [rendering-notes.md](knowledge/rendering-notes.md) | Rendering notes | 3 KB |
| [light-engine.md](knowledge/light-engine.md) | Light engine (TSmartLighting) | 5 KB |
| [projectiles.md](knowledge/projectiles.md) | Projectiles and Persistent Fields (Tpro, Tfield) | 2 KB |
| [radar.md](knowledge/radar.md) | Advanced radar / minimap (TSmartRadar / TSmartMinimap) | 2 KB |
| [web-port.md](knowledge/web-port.md) | Web/Wasm portability (Emscripten) | 9 KB |
| [performance.md](knowledge/performance.md) | Performance (measured 2026-10-12, desktop, 850x480) | 11 KB |
