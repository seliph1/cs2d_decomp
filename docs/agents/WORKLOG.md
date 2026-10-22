# Work log (append-only, newest first)
## 2026-10-21 — Cline — dedicated: sayutf8/sayteamutf8 hooks and Trigger_Use re-use

- **Chat hooks:** `cs2d_sv_chat` only ran `say` / `sayteam`; `sayutf8` / `sayteamutf8` were
  implemented but never called (WQ2D registers `menu.sayutf8` / `menu.sayteamutf8` for its chat,
  commands and RP menu). A probe on `cs2d_dedicated.exe` showed `say` runs first, then `sayutf8`
  with the UTF-8 bytes hex-encoded as `xHH` (`"hello"` = `x68x65x6Cx6Cx6F`); a return 1 from the
  first skips the second, and either return 1 suppresses the message. `src/cs2d_sv_game.c` now
  builds the hex string (`sv_chat_hex_utf8`) and calls both hooks in that order.
- **Trigger_Use one-shot:** `cs2d_sv_use` mode 100 called `cs2d_sv_trigger_at_position`, which
  toggled the button's own state on every press; with no reset the client then rejected it forever
  (state must be 0). fun_roleplay's `rp_ticket` button at (275,154) (ints[3] = 0) could only be
  used once. The dedicated (`FUN_140138a40`) does **not** toggle the button: it validates
  type/state/team/reach/re-use-delay and calls `Te.triggerentities(1)` (the button's `trigger`
  list). Implemented that, plus the `ints[3]` re-use delay (`sv->entity_reuse_ms`, reset at round
  start / map load) and the same local delay on the client (`cs2d_ents_tick_use_cooldowns`).
- **Test runner:** `main` had a hardcoded `total = 112` that had to be edited for every new suite.
  It now uses `RUN_TEST(test_x)`, which counts the executed suites, prints `[FAILED] test_x` for a
  failing one, and accepts a suite-name filter (`test_cs2d.exe trigger_use`, ASCII
  case-insensitive; exit 2 when nothing matches).
- **Verified:** `test_cs2d.exe` -> **113 / 113** (new `test_dedicated_trigger_use`; chat hooks
  added to `test_server_phase4_lua`). Filtered run `test_cs2d.exe Trigger_Use` -> 1 / 1.
  - Live, our server + our client (`connect <ip> <port> <password>`; the join needs
    `sv_password`): `tools/agent/out/br_live5_srv.log` shows three `usebutton 275 154 state=false`
    in a row and the client keeps the button at state 0 (fun_roleplay `rp_ticket`).
  - `tools/agent/out/say_live_srv.log`: a real client message hits `say` then `sayutf8`
    (`x68x65x6C...`), and `sayteam` then `sayteamutf8`; the bot probe (`say_srv4.log`) matches the
    original's order/hex output.
  - Probes: `tools/agent/lua/{say,say_suppress,trigger_use,button_reuse}_probe.lua`.


## 2026-10-20 — Cline — dedicated: NPC Lua hooks wired (objectdamage/objectkill/hit/die/hitzone/projectile_impact)

- **Mapping (help.php `lua_hooks`, Object category):** the hooks that touch NPCs are
  `objectdamage(id,damage,player)`, `objectkill(id,player)`, `hit(...,object)`,
  `die(...,killerobject)`, `kill(...,killerobject,assistant)`, `hitzone(image,player,object,
  weapon,x,y,damage)` and `projectile_impact(...,mode,projectileid)` (mode 3 = NPC). `hit` and
  `hitzone` mention NPCs explicitly; `die`/`kill` carry the object that caused the death.
- **Dano/morte:** `cs2d_sv_damage_ex(sv, victim, attacker, dmg, weapon, object_id)` and
  `cs2d_sv_kill_ex(..., killer_object)` were added; `cs2d_sv_damage` / `cs2d_sv_kill` keep object 0.
  NPC melee/ranged, turrets, mines, laser mines, gate fields and barbed wire now pass their Tdo id.
  The `hit` hook has all 7 arguments and the `kill` hook all 7 (assistant); `kill` now only runs when
  a player caused the death (an NPC kill fires `die` only, with its id as killerobject).
- **objectupgrade:** was declared but never called; `cs2d_sv_object_upgrade` now calls it with
  (object, player, progress, total) and a return of 1 cancels the step (and the money charge).
- **Hitzone:** `cs2d_sv_hitzone_at` implements the `imagehitzone` rectangles (mode > 100 blocks,
  mode 2 HUD images skipped) and calls the 7-argument `hitzone` hook. Used by player/turret shots
  (`sv_trace_shot`) and NPC shots (`ai_npc_shoot`: player 0, NPC object id, weapon 0xf8/0xf9).
- **projectile_impact:** now fired with mode 0 wall / 1 player / 2 hostage / 3 NPC / 100 ground and
  `projectile` moved from spawn to detonation. Only RPG-7, rocket launcher, snowball and gut bomb
  collide with players/hostages/NPCs (help.php).
- **Verified:** `test_cs2d.exe` -> **112 / 112** (new `test_dedicated_npc_hooks`: hit object id, die
  killerobject without kill, cancellable objectdamage, objectkill, NPC shot → blocking hitzone with
  player 0 / NPC object id, projectile_impact mode 3).


## 2026-10-20 — Cline — dedicated: bot team broadcast and NPC AI (client sync / walls / damage)

- **Bot team:** `cs2d_sv_bot_add` set `pl->team` but never sent `[20,id,team,look]`; `[248]` has no
  team, so every client kept the bot at team 0 (Spectator) while the server already ran the AI. The
  bots therefore "walked and shot as spectators". `bot_add` now broadcasts `[20]` (and marks
  `team_chosen`). Regression: `test_dedicated_bot_and_npc_ai` scans the connection buffer for both
  `[20]` records.
- **NPC desync/walls:** the server sent `m_rot * 182.04445` in `[36,id,7]`, but `Tdo.getdata` case 7
  reads the ushort as **degrees** (`FUN_140283a10` writes `(int)m_rot`). The clients spun instead of
  walking while the server chased, which showed the NPC far away but taking damage. The old server
  motion also ignored the original tile waypoints. `src/cs2d_sv_ai.c` now ports `FUN_140283f20`:
  target picker `FUN_140283760` (405x220 + clear 32 px `FUN_14011c3a0` line, team > 0 only), path
  step `FUN_140283a10` (45 px probe + walkable adjacent tile, tile-centre `m_x/m_y`, raw degree
  `m_rot`), motion `FUN_140283120` without the extra map test, and the server-only attack timer.
  An NPC now never targets or damages a spectator or a player behind a wall.
- **Indestructible NPCs:** player shots never reached them: `cs2d_dynobj_hit_debris(30)` is false and
  `sv_trace_shot` only reported blocking tiles. The trace now stops on an NPC tile and
  `cs2d_sv_shot_at_building` accepts typ 30; `cs2d_sv_object_damage` no longer clamps NPC health to
  the building table (10 HP), and the join snapshot sets the NPC `m_rot`.
- **Attack values from `FUN_1401ed310`:** zombie 10, headcrab 2, snark 1, Vortigaunt 15 melee / 40
  ranged (400 px), soldier 5 (400 px); the old far damage came from applying damage from the server
  position at any distance.
- **Verified:** `test_cs2d.exe` -> **111 / 111** (bot `[20]`, degree `m_rot`, tile-centre waypoint,
  wall blocks target/damage, shot damages and kills an NPC).


## 2026-10-20 — Cline — dedicated: bot AI (`bots/Standard AI.lua`) and NPC AI

- **Finding:** CS2D's AI is shipped as Lua, not native code. `bots/Standard AI.lua` +
  `bots/includes/*.lua` are in the repository; the client loads the script at startup
  (CS2D.exe `_global/140190000.c:2263`) and the engine calls its globals per bot. The dedicated has the
  same `ai_*` engine API (client dump `cs2dlua_ai_*`), which was still a set of no-ops in our port.
- **Bot engine (`src/cs2d_sv_ai.c`, new):** loads the script from `cs2d_sv_lua_init`; calls
  `ai_onspawn` on spawn, `ai_update_living` every frame (after the skill-based rotation) and
  `ai_update_dead` every second; `ai_hear_radio` / `ai_hear_chat` from `[240]` modes 3 and 1/2.
- **`ai_*` API:** `ai_move` (movement model of movement.md), `ai_goto` (BFS over floor + collision,
  0/1/2), `ai_aim`/`ai_rotate` (smooth turn: 8 + 2*skill deg/frame, calibrated), `ai_findtarget`
  (420x235 + skill, LOS with `sv_fow`), `ai_freeline` (40 px ray), `ai_attack`/`ai_iattack` (server
  resolves the hit for bots; auto reload/weapon switch), `ai_buy`, `ai_selectweapon`, `ai_reload`,
  `ai_use`, `ai_respawn`, `ai_drop`, `ai_say`/`ai_sayteam`/`ai_radio`, `ai_spray`, `ai_build`,
  `ai_debug`. `setentityaistate`/`entity("aistate")` now store/read a per-entity value reset on round
  start. `randomentity` returns **x, y** (or -100,-100) and honours the AI-state/int0 filters.
  `player(0, prop)` returns false for non-table props.
- **Bots:** `bot_count`, `bot_autofill`, `bot_keepfreeslots`, `bot_jointeam`, `bot_skill`,
  `bot_weapons`, `bot_freeze`, `bot_prefix`, `debugai` defaults and behaviour; flashbang sets
  `ai_flash = Rand(4,6)` for bots in 640x480 with a free line (Tpro.c:3236).
- **NPC AI:** `Tdo` 30 chases the closest living player with the per-type speed/turn of dynobj.md,
  sends `[36,id,7]`/`[36,id,9]` and applies server damage (calibrated values).
- **Verified:** `test_cs2d.exe` -> **111 / 111** (new `test_dedicated_bot_and_npc_ai`: script load,
  smooth aim, BFS path, target/LOS, iattack damage, flash, AI state, bot_count, NPC chase+damage).
  Live smoke on `cs2d_server.exe -offline -map de_dust2`: two bots spawned, roamed, met, fought
  (one died), round restarted; no Lua errors. `sys/server.cfg` restored, `git status sys/` clean.

## 2026-10-20 — Cline — dedicated: ping meter (`mp_anticlock` + `[247]`)

- **Goal:** the original client must show the scoreboard latency of every player.
- **Wire path (verified):** every 2nd second the server sends `[229, srv_ts]` (default) or
  `[249, srv_ts]`; the original client replies with the same timestamp; the server stores
  `(now - start_ms) - ts` in `Tpl +0xd0` and every 3rd second broadcasts
  `[247, n, ticks, in/8 u16, out/8 u16, n x {id, ping}]`. The scoreboard (`Thud_scoreboard`
  0x14012f2c0) is fed only by that list (`0xfffe` = Bot, `0xffff` = Server).
- **Real cvar:** the dedicated's name table has `mp_anticlock` (0x140141e51 / `DAT_1405a64e4`), not
  the invented `sv_antiaccel`. With `mp_anticlock 0` the original sends `[249]` (proxy
  `proxy_anticlock0.log`); the default sends `[229]`. Our settings only accepted the fake name, so
  the repository's `sys/server.cfg` line (`mp_anticlock 0`) was ignored, and the original dedicated
  reports `sv_antiaccel` as an unknown command. Renamed the field/cvar and added it to the getter.
- **Testability:** extracted `cs2d_sv_build_pinglist` (the payload builder used by the 1 s timer).
- **Verified:** the original client against `cs2d_server.exe` shows `Latency 16` (screenshot
  `tools/agent/out/srv_ping_score.png`); after the rename, with `mp_anticlock 0`, our server sends
  `[249]`, the client replies and `[247]` reports 15 ms. `test_cs2d.exe` -> **110 / 110** (new
  assertions: mp_anticlock get/set, `[249]` measurement, exact `[247]` bytes, short-buffer refusal).
- **Residual:** the original also derives `clock_diff` / `clock_ratio` from the `[229]` reply and
  kicks for "Speedhack Detected"; our port only uses it for latency (logged in OPEN_ISSUES).

## 2026-10-20 — Cline — [253] do kick não chegava ao cliente expulso

- **Sintoma (cliente original contra o nosso dedicated):** ao ser expulso, o cliente só perdia a
  conexão; não mostrava o motivo do kick.
- **Causa:** `sv_remove_player` marcava `players[id].used = false` **antes** do loop de `[253]`, então
  `conn_of_player(id)` retornava NULL e o próprio cliente expulso não recebia
  `[253, id, 16, len, reason]`. O original (`FUN_1401502a0`) percorre a lista de jogadores ainda
  joined e inclui o que está saindo; no cliente `FUN_1402eee40` -> `FUN_1401c7500`, a mensagem
  "Disconnect Reason: ..." só é mostrada quando o id do `[253]` é o jogador local.
- **Correção:** o broadcast de `[253]` roda com o jogador ainda `used`; `used = false` passou para
  depois do loop. Para `reason >= 16` a string u8 é sempre anexada (mesmo vazia), como em
  `0x140150b12`.
- **Handshake de saída (2026-10-20):** o cliente que clica em Disconnect envia `[253,0]` e **espera
  a resposta `[253, id, 0]` do servidor** para concluir (`FUN_1401c7500` em modo client devolve sem
  fechar; `FUN_1402eee40` -> `FUN_1401c7500(..., 0, ...)` fecha). O handler `[253]` agora dá
  `sv_flush` na conexão depois de `sv_remove_player`, antes de `active = false`; sem isso o cliente
  fica eternamente em "Disconnecting / Please wait". `sv_conn_reset` (novo hello do mesmo endereço)
  descarta o buffer, então um handshake duplicado não gera esse `[253]`.
- **Teste:** `test_dedicated_kick_message` confere `fd 01 10 0b "Test reason"` na conexão do
  expulso, a resposta `fd 01 00` na fila do cliente que saiu normalmente, e o mesmo anúncio no outro
  jogador. `test_cs2d.exe` -> **110 / 110**.



## 2026-10-20 — Cline — Dedicated: UTF-8 text fields and Func_Teleport walkover

- **Symptoms:** WQ2D Lua messages with accents arrived in game as `"nÃ£o"`; walking onto the map's
  `Func_Teleport` entities in `fun_roleplay` did not move the player.
- **Causes:** `cs2d_sv_msg`, the Lua menu/hudtxt builders and the `sys/serverinfo.txt` lines expanded
  each UTF-8 byte to one UTF-16 code unit, so accented text was double-encoded. The server walkover
  (`cs2d_sv_validate_move`) only handled the Tdo Teleporter Entrance/Exit buildings and never the
  `CS2D_ENT_FUNC_TELEPORT` map entity (222 in fun_roleplay).
- **Fixes:** new `cs2d_net_utf8_to_utf16` (valid UTF-8, surrogate pairs, Latin-1 fallback) used by
  `cs2d_sv_msg`, `sv_sound`, menu `[235]`, hudtxt `[234]`, server info `[240,51]`, our client's chat /
  Lua-menu echo, and the client's UTF-16 -> UTF-8 decoder; Windows console now uses `CP_UTF8`.
  `cs2d_sv_validate_move` teleports to the centre of `(ints[0], ints[1])` for an active Func_Teleport
  and sends `[15]`; `Trigger_Move` is now gated by entity state like the client.
- **Follow-up (escaped UTF-8):** the engine's textual form documented by the Changelog
  (`"UTF-8:\x68\x65..."`) is now decoded by `cs2d_net_utf8_unescape`, including WQ2D's
  `\xC2xA9` spelling (first byte `\xNN`, continuation bytes bare `xNN`). Applied to `msg`/`msg2`/
  `sv_msg`, Lua `menu`, `hudtxt`, `print` and `parse` (WQ2D's `parse(utf8.convert(cmd))`).
- **Center tag:** `cs2d_sv_msg` now mirrors `FUN_140153900` (`0x1401546a0`): text ending in the
  uppercase `@C` loses those two characters and is sent as `[240, 0, 2, ...]` (center), otherwise
  mode 1. The client's mode-2 path calls `Thud_msg.add_center`.
- **Tests:** `test_dedicated_map_teleport` (state 0 teleports, state 1 does not, `[15]` bytes) and
  `test_dedicated_utf8_messages` (`[240]`/`[234]`/`[235]`, surrogate pair, the `©` example,
  `parse` of an escaped command, and `@C` -> mode 2 with the tag removed). `test_cs2d.exe` -> **109 / 109**.



## 2026-10-20 — Cline — Join original travava em "Loading map state" (seção 4 sem terminador)

- **Sintoma (cliente original contra o nosso dedicated):** a tela ficava em `Loading server data... /
  Loading map state...` e o console repetia `ERROR: unexpected joindata 5 (step 4 expected)` (depois 6,
  10, 7, 8, 9, 11, 12, 13 e 200).
- **Causa:** o cliente original só sai da seção 4 (estados de entidade) quando recebe um pacote
  `[252,7,4,0]`. O nosso `cs2d_sv_send_join_snapshot` enviava a seção 4 uma única vez, com até 255
  registros e **sem o count-0**; o cliente continuava esperando `joindata 4` e rejeitava todas as
  seções seguintes.
- **Correção:** seção 4 agora é enviada em blocos de 180 registros (como o original) e termina com
  `[252,7,4,0]`. Regressão em `test_dedicated_trigger_graph` (varre os datagramas pendentes atrás de
  `fc 07 04 00`). `test_cs2d.exe` -> **107 / 107**.


## 2026-10-20 — Cline — WQ2D roleplay: trigger graph, bot reqcld, API types

- **Symptom:** the WQ2D RP script loaded but did not initialise `fun_roleplay`: `rp.shutdown` was
  rejected, two `geoip_lookup` timer errors appeared for the bots, `login.query` was never loaded,
  and the map's Trigger_Start / Trigger_If chains never ran (no doors, items, lights or dungeons).
- **Root causes and fixes:**
  - `shutdown` was missing from the hook table (it exists in 1.0.1.8; probe: `rp.shutdown` adds to
    `mapchange` and `shutdown`). Added.
  - `player(bot,"ip")` was empty; the original returns `"0.0.0.0"`. Set in `cs2d_sv_bot_add`;
    `player(bot,"ping")` now reports 0 while the internal value stays 0xfffe.
  - Bots now answer `reqcld` (clientdata modes 0..6; mode 4 carries the file SHA-256), so
    `menu.join`/`menu.clientdata` load `login.query` like the original.
  - Implemented the server-side entity trigger graph in `cs2d_sv_game.c`: Trigger_Start at round
    start, Trigger_If (Lua `_cs2d_trigger_if=` chunk), Trigger_Once, Trigger_Delay (silent
    forwarding), Trigger_Move on walkover; `trigger`/`triggerposition` and [31]/Trigger_Use paths
    now use it. The Lua `trigger` hook runs once per list name and receives a boolean source;
    `triggerentity` can cancel a single entity. Plain entities only toggle their state and do not
    propagate their own trigger list.
  - API corrections verified with a probe: `game("dedicated")` boolean, `game("timeleft")`,
    `map("xsize"/"ysize")` = highest tile index, `map("tilesize")` 0, `map("botnodes")`,
    `map("nobuying"/"noweapons")`, `entity("state")` boolean, full `strs[0]`.
  - `steamstats` returns false (was an always-truthy empty table), fixing
    `index.lua:2254 attempt to compare number with table`.
  - `move`/`movetile`/`walkover` hooks are now called; the buy window is skipped before the first
    round clock; entity trigger/str0 buffers are 256 bytes (only `strs[0]` needs it, keeping the
    test binary under the 2 GB static-data limit).
- **Verification:** `fun_roleplay` startup has no LUA/hook/timer errors, `login.query` loads twice,
  the first 564 trigger-hook calls match the original exactly, object counts (27 Triple Turret / 57
  Turret) and 120 items match. Residual: 43 extra state-1 entities / 22 extra hook calls after 16 s
  (later If/Delay cascades; logged in OPEN_ISSUES). `test_cs2d.exe` -> **107 / 107**.



## 2026-10-20 — Cline — Fix: imagens-projétil do C4 colidindo/paradas (Ttween não existia no servidor)

- **Sintoma:** `cm.create_projectile` / `cm.projectile_update` (`sys/lua/c4/classmod2.lua`) fazem
  imagens se moverem com `tween_move` e leem `object(img.id,"x"/"y"/"rot")` a cada `always`. No nosso
  dedicado a imagem ficava na posição de criação (a colisão era verificada sempre no ponto de spawn,
  daí o "colide no frame 1") e nunca chegava ao alvo.
- **Causa:** `cs2d_sv_tween_*` apenas montavam e transmitiam `[54]`; **não criavam o `Ttween` local**.
  O original (`cs2dlua_tween_move` 0x14009dda0 / `Ttween.update` 0x14027fc50) mantém a lista de tweens
  no servidor e escreve `Tdo.x/y/rot/alpha/color/scale/frame` a cada frame. Sem isso `object()` e
  `imageparam()` nunca refletiam a animação.
- **Correção:** cada `cs2d_sv_tween_*` agora chama `cs2d_dynobj_read_tween` com o próprio pacote
  `[54]` (a partir de `pkt + 1`, após o opcode) e `cs2d_server_frame` roda
  `cs2d_dynobj_update_tweens` antes do hook `always`.
- **Bugs de protocolo corrigidos junto:** `tween_move(id,time,x,y[,rot])` aceita o 5º parâmetro e usa
  o modo 1 (`[54,1,dur,x,y,rot]`, corpo 10); antes só existia o modo 0. `tween_animate(id,speed,mode)`
  agora tem a assinatura correta e envia `[54,8,speed,mode]` (corpo 5, só o modo; o cliente mantém o
  frame atual). O leitor `cs2d_dynobj_read_tween` foi ajustado (modo 1 = 10, modo 8 = 5).
- **`closeplayers`/`hascloseplayers`:** agora exigem jogador existente, vivo, de time (`team >= 1`,
  `health >= 1`), como `cs2dlua_closeplayers`; slots vazios/mortos não são mais retornados (podiam
  gerar hit falso no ponto de spawn).
- **Teste:** `test_dedicated_dynamic_images` ganhou casos de `tween_move` (modo 0 e 1),
  `tween_animate` e do filtro de `closeplayers`. `test_cs2d.exe` -> **106 / 106**.


## 2026-10-20 — Cline — `object()` completo, `player("screenw"/"screenh"/"widescreen")` e espaço Tdo unificado

- **`object(id, prop)` completo** (`cs2dlua_object` em `CS2D.exe` 0x1400dfc40): além dos campos já
  existentes, agora expõe `typename`, `rootrot` (`rrot`), `rotvar`, `target`, `entity`, `entityx`,
  `entityy` e `idle` como **número** (`Tdo.idle`, não booleano). `tilex/tiley` usam o arredondamento
  do original `(int)((x + 1) * 0.03125)` (diferente de `objectat`, que usa `x / 32`). Objeto ou
  propriedade inexistente -> `false`; `object(0, "table")` continua listando todos os Tdo.
- **`closeobjects`**: passou a medir a distância euclidiana até a **posição do objeto** (canto
  superior esquerdo), não até o centro (+16), como `cs2dlua_closeobjects` 0x140059330.
- **`player(id, "screenw"/"screenh")`** já existiam; adicionado `widescreen` (0/1, inverso do
  setting 4:3) e cobertos por teste (defaults 850x480, `[232,1]` sobrescreve).
- **Espaço Tdo unificado (correção):** imagens (typ 40), construções (1..23) e NPCs (typ 30) já
  dividiam `sv->dynobj` e o mesmo `next_free_id`. O erro estava no round start: só as imagens eram
  removidas. O original faz `Tdo.freeall()` + `FUN_140190e10` (recria Env_Building e Env_NPC **na
  ordem das entidades**), então construções de jogador também somem e os IDs voltam a coincidir com
  o cliente. `cs2d_dynobj_spawn_map_objects` substitui `..._map_buildings` e também cria `Env_NPC`
  (typ 31 do mapa: rot ints[0], tipo ints[1], health ints[2], `ints[3]==1` não spawna). Saúde padrão
  dos NPCs (`Tdo.add` 0x1409ed7f0, init 0x14031a195): 1 zombie **150**, 2 headcrab **30**, 3 snark
  **20**, 4 vortigaunt **200**, 5 soldier **100**. `Tdo.entity` agora é preenchido (spawn do mapa).
- **Health de construções:** `spawnobject` usa `max_health` do tipo (como `Tdo.inibuilding` no
  servidor) e `spawnnpc` usa a tabela de saúde do NPC.
- **Cliente:** em `[22]` faz `dynobj_clear` e roda o mesmo `cs2d_dynobj_spawn_map_objects`, como o
  `FUN_140190e10` do `CS2D.exe`.
- **Teste:** `test_dedicated_dynobj_shared_space` (imagem + construção + NPC no mesmo ID space,
  round start restaura só entidades do mapa) e novas asserções em `object()`/`player()`.
  `test_cs2d.exe` -> **106 / 106**.


## 2026-10-20 — Cline — Fix: `player(id, "mousex"/"mousemapx"/"mousedist")` no dedicated

- **Sintoma:** as propriedades de mouse do `player()` retornavam a posição do jogador
  (`mousemapx/y`) ou zero (`mousex/y/mousedist`), então `cm.mouse(uid)` do mod C4 mirava no
  próprio jogador.
- **Causa:** eram stubs. O servidor original **deriva** o cursor do `rot` + `aim` que o cliente já
  manda em `[70]/[71]/[72]`, sem tráfego extra (`cs2d_dedicated.exe` 0x1400c936c..).
- **Fórmula confirmada** com `tools/agent/lua/mouse_probe.lua` no servidor original e no nosso
  (cliente original): `mousex = 425 + sin(rot)*aim`, `mousey = 240 - cos(rot)*aim` (tela virtual
  850x480), `mousemapx = x + (mousex-425)`, `mousemapy = y + (mousey-240)`,
  `mousedist = (int)distância`. Jogador morto ou sem `[70]/[71]/[72]` ainda recebido -> **-1**.
- **Implementação:** `sv_lua_mouse` em `src/cs2d_sv_lua.c`; `aim_valid` em `cs2d_sv_player_t`,
  marcado nos opcodes 70/71/72 e limpo no spawn.
- **Teste:** `test_dedicated_mouse_properties` com os valores medidos no original
  (`x=1360 y=816 rot=118.07007 aim=254 -> 649/359, 1584/935, 253`) e os casos -1.
  `test_cs2d.exe` -> **105 / 105**.

## 2026-10-20 — Cline — Fix: `addbind` não chegava ao cliente original (snapshot sem a lista de binds)

- **Sintoma:** o mod C4 registra `addbind("Q")`, `"E"`, `"leftshift"`, `"space"`, `"mouse1"`,
  `"mouse2"`, mas o hook `key` nunca disparava no cliente original.
- **Causa:** o `[252,6]` do nosso servidor escrevia **0 binds** (`src/cs2d_server.c`, byte b+22). O
  `CS2D.exe` só envia `[227]/[228]` para teclas anunciadas nessa lista (confirmado com um probe no
  `cs2d_dedicated.exe`: 6 binds -> `06 51 00 45 00 a0 00 20 00 02 00 01 00`). Sem a lista, o cliente
  não reporta nenhuma tecla.
- **Correção:** `cs2d_sv_build_join_settings` agora recebe o estado Lua e escreve `count u8 + n * u16`
  com as **VK codes** reais (`cs2d_bind_key_code`). `addbind/removebind/removeallbinds` também
  transmitem `[224, count u16, count * key u16]` (`lua_bind.SendBindListToClients`, `0x14005be90`), e
  `sv_resolve_key_name` resolve o VK code para o nome canônico (incluindo `f1`/`kp_*`, que colidem com
  ASCII e por isso usam `cs2d_bind_vk_name`).
- **Nomes:** `addbind` valida o nome, normaliza `F1` -> `f1` (a tabela original usa minúsculas) e
  recusa duplicados/desconhecidos, como `lua_bind.AddBindString`.
- **Teste:** novo `test_dedicated_lua_binds` cobre `[224]`, o `[252,6]`, duplicados, `F1`, remoção e
  o hook `key` recebendo `Q` (0x51), `f1` (0x70) e `mouse1` (1); `test_client_lua_bind_events` cobre o
  envio `[227]/[228]` do cliente. `test_cs2d.exe` -> **104 / 104**.
- **Cliente:** `cs2d_client` agora guarda a lista de binds de `[252,6]`/`[224]`, mapeia teclas,
  mouse e wheel para VK codes e envia `[227]/[228]` só para as teclas anunciadas; ao perder o foco,
  solta as binds pressionadas. Validado de ponta a ponta com `bind_probe.lua`: `Q`, `mouse1` e
  `mwheelup` chegam ao hook `key` do servidor.
- **Probe:** `tools/agent/lua/bind_probe.lua` registra os binds e mostra a lista no pacote real.

## 2026-10-20 — Cline — Fix: `spawnprojectile` do dedicated (C4 mod sem projéteis no cliente original)

- **Sintoma:** o mod em `sys/lua/c4` (`cm.spawnprojectile`, que chama
  `parse("spawnprojectile uid type x y dist rot")`) não gerava projéteis no `CS2D.exe` original.
- **Causa:** o parser em `cs2d_sv_exec` esperava `spawnprojectile <type> <tx> <ty> <dir> <flydist>` e
  `cs2d_sv_spawn_projectile` tratava x/y como **tiles** (`* 32`). A ordem real (help.php e
  `Tpro.spawnprojectile`, `CS2D.exe 0x1400a8d50`) é `player type x y flydist dir`, com coordenadas em
  **pixels** e `flydist 0` = projétil no chão. O comando antigo gerava um projétil de tipo errado,
  pertencente ao "server" (player 0), fora do mapa.
- **Correção:** parser e assinatura de `cs2d_sv_spawn_projectile` agora seguem a ordem do help.php;
  x/y são pixels; o `player` vai no byte 1 do `[58]`; `flydist <= 0` deixa `is_flying = false`; ids de
  projétil passaram a ser por dono (1..255, como `Tpro.add`) em vez de um contador global que estourava
  o byte.
- **Teste:** novo `test_dedicated_spawnprojectile_command` (ordem dos argumentos, coordenadas em pixels,
  layout do `[58]`, `flydist 0` no chão) e ajuste do `test_dedicated_p1_rules` para a sintaxe correta.
  `test_cs2d.exe` → **102 / 102**.
- **Nota:** o teste de API Lua do Phase 4 agora desativa `mp_luaserver`, porque o `sys/lua/server.lua`
  atual carrega o mod C4, cujo `menu_backend.lua` define um global `menu` que sombreia a função nativa
  `menu()` (falha pré-existente, não relacionada a projéteis).

## 2026-10-19 — Cline — Fix: cliente expulso ("Idle") logo ao entrar

- **Sintoma:** com o `sys/server.cfg` de teste (`mp_idlekick 1`, `mp_idletime` default 60) o cliente era
  chutado no primeiro segundo depois do join, antes de escolher time.
- **Causa:** `sv_recv_join1` cria a instância com `memset` e não inicializava `last_move_ms`. O
  `sv_second` compara esse campo com `cs2d_sv_now()` (`GetTickCount`), então 0 significa "ocioso desde o
  boot do Windows" — a diferença já passava de `mp_idletime` e o kick "Idle" (reason 16) saía no tick
  seguinte ao join.
- **Correção:** `pl->last_move_ms = cs2d_sv_now()` ao criar o jogador (`src/cs2d_server.c`), com o
  contador de idle passando a valer a partir do join.
- **Teste:** novo `test_dedicated_idle_join` — monta o `[252,1]` real com
  `cs2d_net_build_join_stage1`, simula servidor com 2 min de uptime, verifica que um tick de 1 s não
  expulsa e que passado `mp_idletime` sem movimento o kick continua. `test_cs2d.exe` → **101 / 101**.
- **Evidência do original:** `sys/logs/11-Oct-2026_09-09-30.txt` — join `real` às 09:09:33 e
  `real has been kicked for being idle` às 09:10:34 (61 s com o default de 60), confirmando que o
  contador começa no join.

## 2026-10-19 — Cline — Dedicated P3: reqhttp, sons, FX, grenade rebuy, damagetile

- **`reqhttp`/`httpdata`:** GET HTTP assíncrono (TCP 80, connect bloqueante + leitura não bloqueante
  no `cs2d_sv_lua_frame`, timeout de 5 s); resposta entregue como `(requestid, state, data)`, com
  `mode 1` virando tabela de bytes.
- **Sons:** `sv_sound` usa `[240,0,10,len,UTF16 path]` (modo 10); `sv_sound2`/`sv_soundpos` usam
  `[65,x+0x8000,y+0x8000,len,path]`; `sv_stopsound` usa o modo 11. Cliente: modo 10 toca
  `sfx/<path>` (volume opcional após `|`) e modo 11 para as vozes correspondentes.
- **FX:** `[48, type, x+0x8000, y+0x8000, p1, p2, (type>=100) rgb]`; comandos `explosion` (tipo 1 +
  dano radial com falloff linear) e `effect` (fire 100, smoke/colorsmoke 101, flare/sparkles 102,
  blood 8). Cliente implementa explosão, sangue, fogo, fumaça e sparkles.
- **Regras:** `mp_grenaderebuy` (flag por granada lançada/derrubada, reset no spawn) e
  `mp_mapgoalscore` (pontuação de resgate de refém).
- **`damagetile`** para construções no tile.
- **Verificação:** `build.bat` limpo; `test_cs2d.exe` → **100 / 100** (teste P3 estendido com
  formatos de som, explosão/effect, httpdata, grenade rebuy e damagetile).

## 2026-10-19 — Cline — Dedicated P2: morte/drop, tempban, construções, votos e comandos de mundo

- **Dano/armadura:** `mp_kevlar` agora é double (default 0.35, `DAT_1405a6508`) e o cálculo segue
  FUN_1401658c0 (armadura perde o dano bruto enquanto durar; armaduras especiais 201..205 escalam o
  HP sem perder pontos). Teste de fase 2 atualizado para o valor autêntico (armor 50 -> 10).
- **Morte/saída:** `mp_deathdrop` (0..4) derruba itens com `[24]` ao morrer e ao sair; `mp_killbuildings`
  (0..3) remove/keepa construções do jogador ao sair; `mp_killbuildingmoney` e
  `mp_killteambuildings` (0 unkillable, 1 killable, 2 killable+penalty) aplicados no dano/morte de Tdo.
- **Zumbis:** `mp_zombiedmg`, `mp_zombiekillscore`, `mp_zombiekillequip` e `mp_zombierecover`.
- **Admin:** `mp_tempbantime` com expiração em `cs2d_sv_ban_t.expire_ms`; `mp_kickpercent` e
  `mp_mapvoteratio` contam votos de toda a base e executam kick/tempban/mapchange; `mp_antispeeder 1`
  usa tempban; `mp_turretdamage` configurável.
- **Comandos:** `damageobject`, `killobject`, `triggerposition`, `restart`/`restartround`, `resources`.
- **Verificação:** `build.bat` limpo; `test_cs2d.exe` → **99 / 99** (novo `test_dedicated_p2_rules`).

## 2026-10-19 — Cline — Dedicated P1: hooks inválidos, config/exec, flood guard, regras e spawn

- **Hooks:** `frame`, `http`, `filetransfer`, `post_endround`, `oncollision`, `oncollisionhit`,
  `onhit` e `shutdown` removidos da lista válida (não existem em 1.0.1.8).
- **Config:** `sys/server.cfg` passa por `cs2d_sv_exec` (`cs2d_sv_exec_config`); `exec`/`include`,
  `alias`/`aliases` e `mp_wpndmg` implementados; nomes simplificados de armas (`ak47`) resolvem.
- **Limites:** flood guard por IP (`FUN_1400d4c90`, janelas 60/200/30 por 60 s + ignore), cache de
  falhas de RCON, `mp_connectionlimit`, `mp_maxclientsip` (status 23) e `mp_reservations` (expulsa
  não-reservado); medição de ping por `[229]`/`[249]` + `mp_pinglimit`; idle (`mp_idle*`).
- **Regras:** `mp_buytime` (float), `mp_unbuyable`, `mp_damagefactor`, `mp_wpndmg`,
  `mp_weaponfadeout`, `mp_dropgrenades`, `mp_teamkillpenalty`, `mp_tkpunish`,
  `mp_hostagepenalty`, `mp_vulnerablehostages`, `mp_unbuildable`, `mp_antispeeder` 1..6,
  `mp_roundlimit`/`mp_winlimit` (mapchange), `sv_gm`.
- **Comandos:** `spawnitem` (clip/reserve), `spawnobject`, `spawnnpc`, `spawnprojectile`, `banname`,
  `bans`, `unbanall`, `connections`, `mapinfo`, `maps`, `serverinfo`.
- **Correção de mapa:** `cs2d_sv_change_map` agora zera `round_number` e `mapchange_pending` (antes o
  contador de rounds nunca reiniciava, o que faria `mp_roundlimit` trocar de mapa em loop).
- **Verificação:** `build.bat` limpo; `test_cs2d.exe` → **98 / 98** (novo `test_dedicated_p1_rules`).
- **Restante (P2):** `reqhttp`/`steamstats`/`ai_*` (stubs), `sv_sound*`, `damagetile`/
  `damageobject`/`killobject`/`explosion`/`effect`/`flash*`, cvars de economia/zombie/mapvote.

## 2026-10-19 — Cline — Dedicated P0: hooks de conexão + snapshot 4/6

- **Hooks de conexão (help.php):** `connect_initplayer(id)` no fim de `sv_recv_join1` (instância criada),
  `connect(id)` no início de `sv_recv_join5` (antes de `joined`/`join`), `disconnect(id, reason id)` no
  fim de `sv_remove_player` (jogador já removido); bots disparam `connect` + `join` na hora. `connect`
  passou a receber só o `id` (a assinatura antiga ip/porta/usgn/nome estava errada) e `disconnect` o
  reason numérico (não a string do `leave`).
- **Snapshot seção 4:** `[252,7,4,count]` + `{x u16, y u16, state u8}` com os estados não-zero de
  `map.entity_state`; `[31]` grava o estado, Trigger_Use (`[26,100]`) alterna e faz broadcast de `[31]`,
  e o round start zera todos os estados (`knowledge/entities.md`).
- **Snapshot seção 6:** `[252,7,6,count]` + `{id u8, type u8, player u8, x u16, y u16}` com os
  projéteis que já pousaram (`is_flying == false`).
- **Verificação:** `build.bat` limpo; `test_cs2d.exe` → **97 / 97** (novo `test_dedicated_snapshot_world_state`
  e asserções de `connect`/`connect_initplayer`/`disconnect` na fase 4).

## 2026-10-19 — Cline — Dedicated: auditoria de lacunas + hook `select`

- **Bug corrigido:** `cs2d_sv_lua_is_valid_hook` não listava `"select"`, então `addhook("select", ...)`
  era rejeitado (o `dispatch_hook("select", ...)` já existia em `cs2d_sv_lua_hook_select`).
  Regressão adicionada em `test_server_phase4_lua` (registra, dispara e libera o hook).
- **Auditoria (código + strings UTF-16/refs de `cs2d_dedicated.exe` + `docs/cs2d_help_db_complete.json`):**
  - hooks reais em 1.0.1.8 sem chamador no port: `connect`, `connect_initplayer`, `disconnect`
    (`connect` também tem assinatura errada: a doc é `connect(id)`, nosso wrapper recebe ip/porta/usgn/nome);
  - hooks aceitos pelo nosso `addhook` que não existem no binário: `frame`, `http`, `filetransfer`,
    `post_endround`, `oncollision`, `oncollisionhit`, `onhit`, `shutdown`;
  - stubs Lua: `reqhttp` (nada), `steamstats` (tabela vazia), 19 `ai_*` (sem IA por decisão);
  - snapshot: seções 4 (estados de entidades) e 6 (projéteis no chão) sempre vazias, seção 11 é literal
    de captura;
  - ~50 cvars documentadas sem campo/comportamento, flood guard `FUN_1400d4c90` ausente,
    `server.cfg` carregado fora do `cs2d_sv_exec`, gerador do secret ainda aleatório.
- **Verificação:** `build.bat` limpo; `test_cs2d.exe` → **96 / 96**.
- **Restante:** lista priorizada em `OPEN_ISSUES.md` ("Dedicated gap audit").

Add one entry per session: `## YYYY-MM-DD — <agent> — <topic>`, followed by a few bullets covering:

- what changed;
- how it was verified;
- what is left (move it to `OPEN_ISSUES.md`).

Keep entries short; the details go in `KNOWLEDGE.md` or in code comments.

Only the 5 newest entries live here; when adding one, move the oldest to the top of `worklog/archive.md` (do not read the archive in full; grep it).

## 2026-10-18 — Antigravity — Dedicated Server: API Lua de Itens e Armas (`itemtype`, `item`, `playerammo`, `playerweapons`, `closeitems`)

- **Implementação Autêntica das 5 Funções da API Lua no Servidor Dedicado (`cs2d_dedicated.exe`)**:
  - `itemtype(type, prop)` (`FUN_14009b4c0`):
    - Validação de límites `0 <= type < CS2D_MAX_WEAPONS` e existência de definição (`def && def->name`); retorna booleano `false` para armas indefinidas (como itens de mods/scripts customizados: 80, 202, 290) ou propriedades não reconhecidas.
    - Suporte a todas as 13 propriedades oficiais: `"name"` (retorna string do nome da arma, ou `false` se não for arma CS2D, permitindo o idiomático `if itemtype(id, "name") == false then`), `"dmg"`, `"dmg_z1"`, `"dmg_z2"`, `"rate"`, `"reload"`, `"ammo"`, `"ammoin"`, `"price"`, `"range"`, `"dispersion"`, `"slot"` (retorna `def->cs2d_slot`) e `"recoil"` (retorna `def->recoil_accuracy`).
  - `item(id, prop)` (`FUN_140099a70`):
    - `item(0, "table")`: retorna tabela 1-indexed com os IDs de todos os itens ativos caídos no chão (`player == 0`); qualquer outra propriedade com id 0 retorna `false`.
    - `id > 0`: busca de item ativo no chão. Se não existir, retorna `false`.
    - Suporte a `"exists"`, `"name"`, `"type"`, `"player"` (0 no chão), `"ammo"`, `"ammoin"`, `"mode"`, `"dropped"`, `"physicsid"`.
    - **Correções críticas de paridade**: `"x"` e `"y"` retornam coordenadas **em TILES inteiros** (`it->tile_x`, `it->tile_y`) e não em pixels; `"droptimer"` retorna o tempo decorrido desde o drop em **segundos inteiros** (`(now - it->drop_time_ms) / 1000`).
  - `playerammo(id, type)` (`FUN_14004caf0`):
    - Se o jogador for válido e possuir a arma solicitada, retorna **dois valores inteiros**: `clip, reserve` (`pl->clip[type], pl->reserve[type]`).
    - Se o jogador não existir ou não carregar a arma especificada, retorna um único valor booleano `false`.
  - `playerweapons(id)` (`FUN_14004bd20`):
    - Retorna tabela Lua 1-indexed contendo os IDs dos tipos das armas carregadas pelo jogador (`pl->items`).
    - Retorna tabela vazia `{}` caso o jogador seja inválido ou não carregue itens.
  - `closeitems(id [, range])` (`FUN_14004bf30`):
    - Calcula distância Chebyshev em tiles (`max(abs(dx), abs(dy)) <= range`) em relação à posição do jogador em tiles `(int)(pl->x / 32), (int)(pl->y / 32)`. Range padrão é 64 tiles.
    - Utiliza o algoritmo nativo de buckets por distância inteira (`0 .. min(range, 5)`), garantindo que os itens mais próximos (distância 0..4 tiles) apareçam primeiro na tabela resultante.
    - Retorna tabela vazia `{}` para jogador inválido.
- **Verificação**:
  - Adicionado teste de regressão completo `test_dedicated_lua_items_and_weapons` em `tests/test_cs2d.c`, cobrindo exaustivamente todas as 13 propriedades de `itemtype`, casos de borda negativos/inválidos, `playerweapons` e `playerammo` com múltiplos retornos, propriedades de `item` (incluindo tiles inteiros e droptimer em segundos), e ordenação por proximidade de buckets em `closeitems`.
  - Compilação limpa de `test_cs2d.exe`, `cs2d_client.exe` e `cs2d_server.exe` com `build.bat`.
  - Suíte de testes aprovada com 100% de sucesso: **96 / 96** testes passando (`test_cs2d.exe`).

## 2026-10-18 — Antigravity — Dedicated Server: `settile` (`Tcustomtile`), Opcode [62], Snapshot Seção 9 [252,7,9] & Lua/Console

- **Implementação do Sistema `settile` (`Tcustomtile`) no Servidor Dedicado (`cs2d_dedicated.exe`)**:
  - `cs2d_sv_settile` (`FUN_14011ed00`):
    - Executa `Tcustomtile.startbatch`, `Tcustomtile.addcheck` (`FUN_140092770`) e `Tcustomtile.endbatch` (`FUN_1400fb050`).
    - Atualiza os tiles, modificadores e `tile_data` do mapa do servidor, recalculando `map->base_collision` e `map->collision` (integrando-se com `cs2d_map_is_solid` e `freeline` em tempo real).
    - Faz broadcast confiável do opcode `[62, x u16, y u16, tile u8, rot u8, r u8, g u8, b u8, overlay u8, blend u8]` (12 bytes) para todos os jogadores com `joined == true`.
  - **Join Snapshot Seção 9 (`FUN_1401e1130` / `0x1401e1555`)**:
    - Substituído o stub vazio pela serialização autêntica de `sv->map.custom_tiles` em blocos de até 70 itens (`0x46`):
      `[252, 7, 9, chunk_count]` seguido por 11 bytes por tile: `{x u16, y u16, tile u8, rot&3 u8, r u8, g u8, b u8, overlay u8, blend u8}`.
    - Emite pacote de fechamento com count 0 `[252, 7, 9, 0]`, permitindo que clientes conectando após modificações de `settile` recebam todos os tiles customizados.
  - **Execução no Console / CLI / RCON (`FUN_14011f210`)**:
    - Comando `settile <x> <y> <tile> [rot] [r] [g] [b] [overlay] [blenddir] [blendtype]`.
    - Trata defaults autênticos: rot 0, cores RGB 255/255/255, overlay 0, cálculo do byte de blend via `dir - 8 + type * 8`.
  - **Subsolo Lua**:
    - Registrada a função nativa global `settile(...)` na VM Lua do servidor dedicado (`src/cs2d_sv_lua.c`).
    - Suporte a comandos `settile` via `parse("settile ...")`.
    - Query `tile(x, y, prop)` (`FUN_1400c0df0`) atualizada e alinhada 1:1 com a desassemblagem do Ghidra:
      - `"hascustomframe"`: retorna booleano se o tile possui custom frame em `Tcustomtile.get`.
      - `"originalframe"`: retorna o tile antigo se customizado, ou o frame atual se intocado.
      - `"walkable"`: verifica se o tile é chão transponível.
      - `"rot"`: retorna a rotação em graus (0, 90, 180, 270) baseada em `modifier & 3`.
      - `"blend"`: retorna o ID do blend frame caso `(modifier & 0xc0) == 0x40`, ou `0` caso contrário.
      - `"color"`: retorna tabela Lua `{r=..., g=..., b=...}` caso `(modifier & 0xc0) == 0x80`, tabela vazia `{}` para outro modificador ou `0` se sem modificador.
      - `"brightness"`: retorna valor percentual `0..100` (`mod > 3`) ou `false`.
      - `"property"`, `"wall"`, `"obstacle"`, `"deadly"`, `"height"`, `"entity"`.
- **Verificação**:
  - Adicionado teste completo de regressão `test_dedicated_settile` em `tests/test_cs2d.c`: validação do broadcast do opcode 62 (12 bytes reliable), atualização da colisão e modifiers do mapa, execução via `cs2d_sv_exec`, via `parse()`, via chamada direta em Lua `settile()`, validação das queries Lua `tile()`, transmissão e parsing da Seção 9 do snapshot via `cs2d_customtile_read_snapshot`, e reversão de tile para os valores originais com limpeza da lista.
  - Suíte completa de testes aprovada com 100% de sucesso: **95 / 95** testes passando (`test_cs2d.exe`).
  - Binários `test_cs2d.exe`, `cs2d_client.exe` e `cs2d_server.exe` compilados sem erros.

## 2026-10-18 — Antigravity — Dedicated Server: Correção do Ciclo de Vida de Imagens Dinâmicas Lua e `hudimg.lua` no Cliente Original

- **Diagnóstico e Correção de `hudimg.lua` no Cliente Original (`CS2D.exe`)**:
  - **Ordem de Execução do `cs2d_sv_round_start` e Hooks (`src/cs2d_sv_game.c`)**:
    - No original (`cs2d_dedicated.exe` / `CS2D.exe`), a sequência estrita no início da rodada é: `startround_prespawn` -> `spawn` dos jogadores -> broadcast do Opcode 22 -> `startround`.
    - No código anterior, o hook `startround` estava sendo invocado antes do broadcast do Opcode 22 e antes do `cs2d_dynobj_kill` de limpeza de imagens da rodada anterior, o que destruía as novas imagens imediatamente tanto no servidor quanto no cliente (já que `CS2D.exe` chama `FUN_1402520b0` / `Tdo.freeall` ao receber o Opcode 22).
    - Ajustada a ordem para: limpar imagens antigas -> `startround_prespawn` -> spawn de jogadores com hook `spawn` -> broadcast e flush do Opcode 22 -> hook `startround`.
  - **Confiabilidade e Agrupamento dos Pacotes de Imagem (`0x140129a50`, `src/cs2d_sv_lua.c`)**:
    - A desassemblagem do executável dedicado original confirmou que modificadores de imagem (`imagecolor`, `imageblend`, `imagescale`, `imagepos`, `imagealpha`, `imageframe`) invocam `Createpacket(1, 0)` (`reliable = 1`, `new = 0`), agrupando-se no mesmo datagrama confiável com `image(...)`.
    - Alterado `send_to_player` para `fresh = false` e configuradas todas as mensagens de modificação e remoção de imagem como `reliable = true`.
  - **Resolução Padrão do Cliente (`src/cs2d_server.c`)**:
    - O cliente original não envia o Opcode 232 (`clientsetting`) no handshake inicial. Como `screen_w` e `screen_h` iniciavam zerados, scripts como `hudimg.lua` posicionavam todas as imagens em `(0, 0)` quando `mp_hudscale == 0`.
    - Inicializados `screen_w = 850` e `screen_h = 480` por padrão na alocação da conexão do jogador.
  - **Exposição de `cs2d_sv_flush` (`include/cs2d_server.h`, `src/cs2d_server.c`)**:
    - Exposta a função `cs2d_sv_flush` para permitir flush explícito de buffers após transições críticas de rodada.
- **Verificação**:
  - Test suite completa aprovada com **94 / 94** testes passando (`test_cs2d.exe`), incluindo teste dedicado de imagens dinâmicas e HUD texts.

## 2026-10-18 — Antigravity — Dedicated Server: HUD Texts (`hudtxt`, `hudtxt2`, `hudtxtclear`, `hudtxtalphafade`, `hudtxtcolorfade`, `hudtxtmove`, [234], [54])

- **Implementação Completa dos 6 Comandos e Funções de HUD Text no Servidor Dedicado**:
  - `hudtxt(id [, text, x, y, align, valign, size])` e `hudtxt2(player, id [, text, x, y, align, valign, size])` (`FUN_14011c0d0`):
    - Validação de limite `id < 0 || id >= 200` com mensagem idêntica ao original: `©255000000ERROR: hudtxt(2) - text id out of bounds (0-199 expected)!`.
    - Limite máximo de texto clampado a 400 caracteres UTF-16 (`0x190`).
    - Tamanho padrão de fonte `size = 13` (`0xd`) quando omitido ou `<= 0`.
    - Formato do pacote (enviado com `reliable = true`): `[234, id u8, chars u16, chars*u16, (x+0x4000) u16, (y+0x4000) u16, align u8, valign u8, size u8]`.
  - `hudtxtclear([player])` (`FUN_14011c5e0`):
    - Limpa todos os textos enviando pacote reliable `[234, 255]`.
  - Tweens de HUD Text (Opcode 54, `FUN_1401a8c00`):
    - Identificador de alvo mapeado como `(200 - id)` em u16 (diferenciando de tweens de imagens dinâmicas).
    - Enviados com `reliable = false` via UDP não confiável (`plVar[0x19]->send(1, 0)`).
    - `hudtxtalphafade(player, id, duration, alpha)` (`FUN_14011cc20`): Opcode 54 modo 4, clamp de alpha [0.0, 1.0].
    - `hudtxtcolorfade(player, id, duration, r, g, b)` (`FUN_14011c800`): Opcode 54 modo 5, clamp RGB [0, 255].
    - `hudtxtmove(player, id, duration, x, y)` (`FUN_14011cea0`): Opcode 54 modo 0, coordenadas com offset +32768.
    - Mensagens de erro em id inválido idênticas às originais (`ERROR: hudtxtalphafade ...`, etc.).
- **Disponibilidade Total nas 3 Camadas de Execução**:
  - Funções nativas da API Lua registradas no ambiente (`hudtxt`, `hudtxt2`, `hudtxtclear`, `hudtxtalphafade`, `hudtxtcolorfade`, `hudtxtmove`).
  - Execução via `parse("hudtxt ...")` no script Lua (compatível com `wrapper.lua` e scripts padrão).
  - Execução no console do servidor / CLI / RCON (`cs2d_sv_exec`).
- **Verificação**:
  - Teste de regressão dedicado `test_dedicated_hudtxt` adicionado em `tests/test_cs2d.c`, cobrindo chamadas diretas em Lua, comandos string via `parse()`, comandos de console CLI via `cs2d_sv_exec()`, validação byte a byte do pacote binário do opcode 234 e dos 3 tweens do opcode 54, além de validação de IDs fora dos limites.
  - Test suite aprovada com 100% de sucesso: **94 / 94** testes passando (`test_cs2d.exe`).


## 2026-10-18 — Antigravity — Dedicated Server: Objetos Dinâmicos de Imagem Lua (`Tdo` 40), Todos os Modos de Imagem, Parâmetros e Snapshot

- **Unificação dos Objetos Dinâmicos de Imagem (`Tdo` 40, `CS2D_DO_IMAGE`)**:
  - Revertida a estrutura isolada `lua->images` para gerenciar as imagens diretamente na lista de objetos dinâmicos do mundo (`sv->dynobj`), exatamente como no binário original do dedicado (`cs2d_dedicated.exe`, categoria 4 de `Tdo`).
  - Adicionados campos de dimensão (`width`, `height`) em `cs2d_dynobj_t` e exposta função portátil `cs2d_image_dimensions` em `cs2d_image.c` (usando `stbi_info` sem carregar pixels), adicionando `src/cs2d_image.c` ao build do servidor dedicado (`cs2d_server.exe`).
  - Integração total com `object(id, "type") == 40`, `object(id, "exists")`, `object(0, "table")` e queries espaciais.
- **Suporte a Todas as Funções de Imagem e Confiabilidade de Rede (`FUN_14012f2b0`)**:
  - `image(path, x, y, mode [, player])`: validação fiel (`FUN_14011aab0`), suportando tags nativas (`<light>`, `<avatar_...>`, `<tile:...>`, `<flag:...>`) e arquivos de spritesheet (`<spritesheet:path:w:h[:mask]>`). Se o arquivo não existir ou for inválido, emite mensagem autêntica `©255000000LUA ERROR: image - can't load image at '<path>'` e retorna 0. Transmissão com `reliable = true` no opcode `[51, 0, path, id, x+32768, y+32768, mode, player]`.
  - Opcodes de modificação com transmissão `unreliable = false` (conforme desassemblagem original):
    - `imagepos`: `[51, 5, id, x+32768, y+32768, rot*182.04445]`
    - `imagealpha`: `[51, 2, id, (int)(alpha*255)]` (clamp [0.0, 1.0])
    - `imagecolor`: `[51, 1, id, r, g, b]`
    - `imagescale`: `[51, 4, id, sx f32, sy f32]`
    - `imageblend`: `[51, 3, id, blend]` (clamp [0, 3])
    - `imageframe`: `[51, 7, id, frame]` (1-based byte em rede)
    - `imageshadow`: `[51, 9, id, shadow]`
    - `imagephysicsparent`: `[51, 8, id, parent_type, parent_id, mode]` (adicionado e registrado na API Lua)
    - `imagehitzone`: `[59, id, mode, xo+32768, yo+32768, abs(w), abs(h)]`
    - `freeimage`: `[51, 6, id]`, destruindo o objeto via `cs2d_dynobj_kill`
  - Implementação completa de `imageparam(id, param)` (`FUN_14012e5c0`) suportando os 9 parâmetros: `x`, `y`, `rot`, `alpha`, `path`, `frame` (1-based), `width`, `height`, `framecount` (0 para imagens normais, contagem de células para spritesheet), e retornando `false` caso o parâmetro ou o id da imagem não exista.
- **Suporte a Todos os Modos de Imagem e Filtragem de Snapshot**:
  - Modos 0 (floor), 1 (top), 2 (HUD), 3 (super top), 4 (background), e modos de seguir jogador 101..132, 133..164, 201..232, 233..263.
  - Imagens com `mode == 2` e `player > 0` são direcionadas exclusivamente para o respectivo jogador.
  - Snapshot de entrada (Join):
    - Seção 5 (`[252, 7, 5]`): inclui todos os objetos `CS2D_DO_IMAGE` visíveis (filtrando imagens HUD de outros jogadores).
    - Seção 7 (`[252, 7, 7]`): serializa blocos de até 20 imagens com cor, alpha, scale, blend, frame, shadow e hitzone, finalizando com o terminador vazio `[252, 7, 7, 0]`.
- **Limpeza de Round (`cs2d_sv_round_start`)**:
  - Todas as imagens dinâmicas (`CS2D_DO_IMAGE`) são limpas do mundo no início de um novo round, reproduzindo o comportamento nativo do dedicado original.
- **Verificação**:
  - Adicionado teste completo `test_dedicated_dynamic_images` em `tests/test_cs2d.c` validando caminhos inválidos, tags nativas, spritesheets, modificadores, `imageparam`, API `object`, modos de imagem, filtragem de snapshot entre jogadores e limpeza em novo round.
  - Suíte completa de testes aprovada: **93 / 93** testes passando (`test_cs2d.exe`).


