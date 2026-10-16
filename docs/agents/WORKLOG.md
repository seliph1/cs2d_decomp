# Work log (append-only, newest first)

Add one entry per session: `## YYYY-MM-DD — <agent> — <topic>`, followed by a few bullets covering:

- what changed;
- how it was verified;
- what is left (move it to `OPEN_ISSUES.md`).

Keep entries short; the details go in `KNOWLEDGE.md` or in code comments.

Only the 5 newest entries live here; when adding one, move the oldest to the top of `worklog/archive.md` (do not read the archive in full; grep it).

## 2026-10-16 — Cline — o check de vitória do standard mode é por evento, não por frame (timer do round com 0/1 jogadores)
- Sintoma (relato do usuário): com o servidor vazio ou com um jogador só, o nosso dedicated reiniciava o
  round quase que imediatamente, enquanto o original deixa o round correr.
- Medição no original (`cs2d_dedicated.exe`, `tools/agent/round_timing.py` + `tools/agent/lua/round_probe.lua`,
  `round_probe_kill.lua`): com 0 jogadores nenhum round termina/inicia por 18 s; com 1 jogador (espectador
  ou CT vivo) também não; o round de um jogador só termina **quando ele morre** (`killplayer 1` → draw 3,
  3000 ms) e depois segue rodando. Nosso dedicated fazia o mesmo draw a cada 3 s (o round durava ~20 ms).
- Causa (objdump/Ghidra do dedicated, `tools/agent/dedscan.py`): o check `FUN_14015ce30(0)` é chamado só de
  eventos de jogador — o call em `0x14015085b` está dentro de `FUN_1401502a0` (o removedor/kick de jogador,
  gateado por `DAT_1407dfe6c == 0`) e o de `0x140164da3` na função de time; além deles há tail calls em
  `0x140160526` (morte, `cmp [pid+0x230], 3`), `0x1401647c4`, `0x1401840d1`, `0x14015d200/220/266`.
  Nenhuma no tick por frame (`0x1401a2xxx`), que só cuida de freezetime, C4, VIP e do time out.
- `src/cs2d_sv_game.c`: as regras de eliminação saíram do `cs2d_sv_step_round` para `cs2d_sv_check_win`
  (FUN_14015ce30(0)) e passaram a ser chamadas das mortes (`cs2d_sv_kill`, com `cs2d_sv_vip_check` antes
  para o motivo 10), das remoções (`sv_remove_player` / `cs2d_sv_kick`, `FUN_1401502a0`) e do "Game
  Commencing" do time, agora fiel ao dump (`t+ct == 0`, `t+ct == 1` com o único jogador **morto**, 1v1 e
  2v0/0v2). A explosão do C4 agora encerra o round (20) antes do dano de área, senão as mortes do blast
  decidiriam o round primeiro.
- Testes: `test_cs2d.c` passa a dirigi-lo por evento (`cs2d_sv_kill` para a morte do jogador solitário,
  `cs2d_sv_check_win` nos casos de eliminação) e ganhou dois casos novos: "uma equipe sem jogadores empata"
  e "jogador sozinho não perde o round sem evento". `build.bat` limpo, `test_cs2d.exe` **72 / 72**.
- Verificado lado a lado: nosso dedicated agora não termina round com 0/1 jogadores e encerra com draw na
  morte, com o mesmo atraso de 3000 ms do original.


## 2026-10-16 — Cline — standard game mode (sv_gamemode 0) reconstructed from cs2d_dedicated.exe

- New `docs/agents/knowledge/standard-gamemode.md`: the round end model of `FUN_14015c840` +
  `FUN_14015c080` (`type` → winner/MVP, `mode` = the reason byte of `[226]`), the 19 reason ↔ language
  message pairs (0576..0594), the win conditions of `FUN_14015ce30` (a team without players draws), the
  per-mission time out (`0x1401a3128`: vips → 12, hostages → 30, bombspots → 22, else draw; skipped
  while the C4 ticks), the round phases `DAT_1407dfea0` (0 = freezetime rejects movement at `0x1401c9a02`
  for `mp_freezetime` seconds, then 1 = playing, 2 = decided), the V.I.P. (team 3, 200 armor, cannot
  buy; death 10 / escape 11 / time out 12) and the hostage rule (>50 % of the map's hostages → 31, the
  float 0.5f at `0x1406da614`).
- `src/cs2d_sv_game.c` + `include/cs2d_server.h`: `cs2d_sv_round_end_ex(type, mode, delay, player)` with
  the winner/MVP table and the per-type MVP (`[42, id, 10, n]`), `cs2d_sv_count_missions`,
  `cs2d_sv_assign_vip` / `cs2d_sv_vip_check` / `cs2d_sv_hostage_escaped`, the freezetime phase
  (`sv_step_phase`), the mission-based time out, the VIP objective check every tick, and the bomb
  explode/defuse round ends now carry the planter/defuser as the MVP. The VIP cannot buy, movement and
  attacks are rejected while frozen, and `sv_count_side` counts team > 1 as the CT side (VIP included).
- Tests: `test_standard_gamemode` (missions, VIP 200 armor + no buy, freezetime gate, VIP 10/11, the
  `[226]` shape, the winner table, the 50 % hostage rule, the elimination cases) and the updated time out
  and hostage expectations. `build.bat` clean, `test_cs2d.exe` **72 / 72**.
- Left (OPEN_ISSUES): the MVP ranking stands in `score * 2 + assists` (no separate `kills` counter), and
  the `[20, id, 3, look]` VIP announcement plus the round start sound are not proxy-verified.
- **Bug found and fixed on the way (planted C4 invisible):** the user reported that a planted bomb never
  showed on the ground and could not be defused. Reproduced with our client against our dedicated
  (`tools/agent/out/bomb_shot.sh`: walk to the de_dust2 bomb spot (25,22), `slot5`, attack, walk away,
  `!shot`) and measured rather than guessed: temporary prints showed the client *did* receive `[30,3]`
  and add the ground item, so the server was the culprit - `cs2d_sv_step_items`' generic pickup fallback
  matched item 63 (`CS2D_SLOT_ITEM` hits no branch) and the planter, standing on the tile, collected the
  C4 on the next tick; the `[25]` then removed the item on every client. Fixed by skipping
  `CS2D_WEP_PLANTEDBOMB` in the pickup loop; a regression check was added to
  `test_server_phase5_bomb_hostages_bots` and the screenshot now shows the C4 sprite. Details in
  `knowledge/hostages-bomb.md`.


## 2026-10-16 — Cline — protocol: no message opcode 0; the disconnect packet is [253, reason]
- Question answered from the shipped dumps (no source change). The `Code 0` in the client's own `Disconnect Reason:` line (`sys/logs/*.txt`) is the *reason* byte of the reliable leave packet `[253, reason]`, not a message opcode.
- `FUN_1401c7500` (`0x1401c7970`) writes `0xfd` + `param_2`; "Disconnect / Quit" (`Tgui_hudmen.c:2246/2273`) passes 0, so the original sends `fd 00` - identical to ours (`[net out 4] 0e 00 fd 00`, `tools/agent/out/cli_ba.log:110`). Reason >= 0x10 appends a u8 string; reason 4 makes the dedicated drop the datagram (`docs/dump/dedicated/FUN_1401e3790_leave.c`).
- A message opcode 0 does not exist: the dedicated receive jump table `0x1406cdb14` is indexed by `opcode - 7` (7..253).
- Docs: `knowledge/net-messages.md` gained the `[253]` row and the code-table note; the `OPEN_ISSUES.md` item "Not checked: what the original sends on Disconnect" is verified and narrowed to the server-side `[253,pid,reason]`.

## 2026-10-16 — Claude — Phase 8: two-client session (original + ours) on one original dedicated
- New `tools/agent/capture_two.sh` + `tools/agent/lua/twoclient_fire_probe.lua`: one original dedicated, the
  original `CS2D.exe` behind proxy 36980 and our `cs2d_client.exe --capture` behind proxy 36981 (each client needs
  its own `udp_proxy.py`, which replies to a single address), the original driven by `drive_orig.ps1`. The script
  prints an opcode histogram per direction and restores `sys/server.cfg` / `sys/recentlyjoined.lst`.
- Recorded session `two2` (prison_funst3r, 45 s): both clients joined (ids 1 and 2), the probe placed and armed
  both, and both fired. Verified the multi-client relays in both directions, and that our `[7]` / `[10]` / `[70]`
  shapes match the original's `[7]` / `[10]` / `[71]` (byte examples in `knowledge/net-messages.md`).
- Only byte-level difference found: the original's first datagram is `02 00 03 32 00 fc 00` (control triple
  `03 32 00`), ours is `02 00 fc 00`. Recorded in `OPEN_ISSUES.md`, not implemented (the type-3 semantics are not
  decompiled and the original dedicated accepts both).
- No source change this session: `build.bat` and `test_cs2d.exe` re-run clean (71/71).

## 2026-10-16 — Claude — dedicated: Lua hook connect_attempt bound in [252,1] (status 4 + ban reason)
- Found the call site in `cs2d_dedicated.exe` with `objdump -d -M intel` plus a scan of the BlitzMax string objects
  in `.data` (REA `open_binary` worked, but the Ghidra analysis was still running and every call hit the 60 s MCP
  timeout): the hook list of slot `+0xc8` runs at `0x1401de995`, reached from `0x1401dcd41` right after the per-IP
  flood guard `FUN_1400d4c90(ip, 0xfc, 200, 60000)` (`0x1401dcd12`) and **before every sanity check**, with the 7
  documented arguments (steam id pushed as a string, `bbStringFromLong`). A non-empty returned string logs the
  `Lua connect_attempt returned:` line (`0x1405ee540`) and answers `[252,2,4,<value>]`: status **4** (ban) plus the
  string as ban reason (`0x1401dce77`, `0x1401dcec7`). Details in `knowledge/dedicated-server.md`.
- `cs2d_sv_lua_hook_connect_attempt` now takes the 7 arguments and an `out_reason` buffer (`include/cs2d_sv_lua.h`,
  `push_connect_attempt` in `src/cs2d_sv_lua.c`); `sv_recv_join1` (`src/cs2d_server.c`) calls it after the parse and
  before the checks, and the old 2-argument stub is gone. New `test_server_connect_attempt_hook` locks the argument
  order, the empty return and the cancel with status 4 (71/71).
- Left: where `connect` / `connect_initplayer` run (both documented as "no return value", so each needs its own
  evidence); the remaining Phase 1 gaps stay in OPEN_ISSUES.
