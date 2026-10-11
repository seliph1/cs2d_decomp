# Work log (append-only, newest first)

Add one entry per session: `## YYYY-MM-DD — <agent> — <topic>`, followed by a few bullets covering:

- what changed;
- how it was verified;
- what is left (move it to `OPEN_ISSUES.md`).

Keep entries short; the details go in `KNOWLEDGE.md` or in code comments.

## 2026-10-11 — Claude Code — agent memory structure
- Added `AGENTS.md` and its pointers (`CLAUDE.md`, `GEMINI.md`, `.agent/rules/`), plus `docs/agents/*`,
  `tools/agent/*` (capture scripts and Lua probes) and `.mcp.json` (REA).

## 2026-10-11 — Claude Code — weapon sync, shot effects, join/leave
- **Weapon sync:** the client follows the server's inventory: opcodes 9, 16, 21, 22 (fixed record size),
  23, 24, 25, 33, 42 and 61, plus ground items. Validated with the `inv` command against the Lua dump:
  identical.
- **Shot effects:** tracer, glowdots, muzzle flash with light, wall sparks and shells (`cs2d_fx_shot`).
  Remote shots come from `[7,pid,seed]`.
- **Join/leave:** the missing client data `[252,10]` + `[67,1]` meant the server never ran `join`; the
  client now sends both. Two-client test: join 1, join 2, leave 2, leave 1; player count 0→1→2→1→0.
  The client also handles `[248]`, `[253]` and `[28]`.

## 2026-10-11 — Claude Code — Env_Breakable
- **Behaviour:** health, damage, hitscan trace, explosion and break effects (`cs2d_fx.c`). The fire
  packet is now `[7,seed]`.
- **Verification:** `test_breakables` uses numbers measured on the dedicated.

## Earlier (Claude Code / Antigravity)
- **Scenes and UI:** scene split (lobby, game, editor); UI library (textbox, dropdown, numberbox, console
  ring buffer).
- **Shadows:** fixed shadow halos and removed the invented roof parapet.
- **Entities:** entity logic validated state for state against the dedicated (fixture of 331 samples).
- **Movement and protocol:** movement is an exact port of `FUN_1401f7600`; the protocol matches the
  original client (positions, rotation, use, setpos).
- **Restart:** the offline `restart` command restarts the round (states, Trigger_Start, freeze time, random spawns).
