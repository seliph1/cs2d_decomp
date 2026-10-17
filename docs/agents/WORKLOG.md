# Work log (append-only, newest first)

Add one entry per session: `## YYYY-MM-DD — <agent> — <topic>`, followed by a few bullets covering:

- what changed;
- how it was verified;
- what is left (move it to `OPEN_ISSUES.md`).

Keep entries short; the details go in `KNOWLEDGE.md` or in code comments.

Only the 5 newest entries live here; when adding one, move the oldest to the top of `worklog/archive.md` (do not read the archive in full; grep it).

## 2026-10-17 — Antigravity — Construction: cadência de 1 Hz no Dispenser e Supply

- **Dispenser & Supply cadence throttle**: corrigido o bug em que `sv_step_dispenser_supply` era executado
  em cada frame do servidor (50/60 Hz), causando spam descontrolado de cura (+600 HP/s), dinheiro (+$30.000/s)
  e spawn excessivo de munições e itens no chão em frações de segundo.
- Em `cs2d_dedicated.exe` (`FUN_1401b5890`, `FUN_1401b7380`) e `CS2D.exe` (`FUN_14028b980`, `FUN_14028dba0`),
  o loop de step dos edifícios e seus contadores operam em **segundos (1 Hz)** via timer de 1000 ms.
- Regulado o step em `cs2d_sv_step_buildings` (`src/cs2d_sv_game.c`) usando `o->scan_ms` como temporizador de 1000 ms:
  - **Dispenser**: cura `mp_dispenser_health` (10 HP/s) e paga `mp_dispenser_money` ($500/s) uma vez por segundo;
    contador de segundos avança a 1 Hz, spawnando munição secundária no segundo 5 e primária no segundo 10.
  - **Supply**: contador de segundos avança a 1 Hz, spawnando item do slot configurado a cada 7 segundos.
- Verificado: `build.bat` compilou limpo e `test_cs2d.exe` **80 / 80** passou, com novo teste de regressão
  validando que passos adicionais dentro de 1000 ms não curam nem pagam repetidamente.
- Falta (OPEN_ISSUES): simulação de `Tpro` no servidor para granadas/rockets, feixe da Laser Mine (21),
  `mp_building_*` como cvars, e proxy capture das constantes de construção.

## 2026-10-16 — Cline — Construction part 5: Gate Field, arame farpado e minas ao pisar o tile

- **`sv_tile_object_effect`** (chamado do `cs2d_sv_validate_move` na mudança de tile, antes do teleporte) porta
  o switch de `Tpl.tilepos` (`Tpl.c:3443..3604`):
  - **Arame farpado (2)**: 20 de dano (arma `0xff`) creditado ao dono, o arame leva 60 (`Tdo.damage`) e manda
    `[36,id,5]` (o wobble). Sem teste de team — fere também o próprio lado.
  - **Gate Field (6)**: o outro lado leva **20** (arma `0xfe`, `mov r8d,0x14` em `0x140210459`) e o campo leva
    **100** (`edx=0x64`); em DM qualquer um menos o construtor, `team 0` fere todos com atacante 0.
  - **Mina (20)**: dispara para o outro lado e `Tdo.kill` explode-a com a entrada 77 da tabela de armas
    (`range` = 96, `damage` = 150) via `FUN_1401f1130` com atacante 0 — jogadores e prédios com a queda
    linear.
- As quatro cvars do Construction (`mp_teleportreload`, `mp_dispenser_health`, `mp_dispenser_money`,
  `mp_dmspawnmoney`) entraram no `cs2d_sv_settings_set` / `_get`, portanto `server.cfg`, a consola e o
  `game("...")` do Lua chegam a elas (não fazem parte do broadcast `[237]` do original).
- Verificado: `build.bat` limpo e `test_cs2d.exe` **80 / 80** (teste novo: gate field 20/100 com o time
  próprio ileso, arame 20/60 com `[36,id,5]`, e a mina que mata um jogador de 100 hp). O teste do dinheiro de
  spawn passou a verificar também o `mp_dmspawnmoney` configurado por cvar.
- Falta (OPEN_ISSUES): granadas/rockets (precisam da simulação de `Tpro` no servidor), o feixe da Laser Mine
  (21), `mp_building_*` como cvars, e o alvo da chave inglesa num proxy capture.


## 2026-10-16 — Cline — Construction part 4: explosões nos prédios e o gate `0x140ddf540` identificado

- **Explosões**: encontrei o modelo em `CS2D.exe FUN_1401f1130`
  (`docs/dump/cs2d/_global/1401f0000.c:1..169`) — três ciclos (jogadores, breakables `0x19`, e os Tdo pelo
  **centro** `x+16/y+16`) com queda linear `(raio - distância) * dano / raio` e `Tdo.damage` pelo slot de
  vtable `+0xb8`. Novo `cs2d_sv_explosion_objects` (o ciclo dos objetos) ligado à explosão do **C4**
  (350 px / 500), que antes só magoava jogadores. As granadas/rockets precisam de uma simulação de `Tpro`
  no servidor primeiro (`cs2d_sv_projectile` só relayа) — ficou anotado com os raios do `Tpro.c`
  (granada 40, rocket 45, mina 100).
- **`0x140ddf540`** (o gate que o Dispenser/Supply partilham): está numa secção **sem imagem de ficheiro**
  (a `0x140dde000`, raw size 0), ou seja é um flag de *runtime* — não um setting que eu pudesse ler.
  Aparece no `Titem.c` (pickups), `Tpro.c` (projéteis), `Te.c` (triggers), `Thostage.c` e no decaimento do
  `rsarmor` (`Tpl.c:3893`), sempre como "`DAT_140ddf540 != 0 && in game`": é o "o jogo está a correr" e
  tratá-lo como ligado no nosso servidor (que não tem pausa) está certo. `pevalue.py` passou a dizer
  "no file bytes" em vez de ler o cabeçalho quando a secção não tem raw data.
- **Modelo de rounds do `sv_gamemode 3`**: verificado que já é inerte (`cs2d_sv_check_win` sai cedo com
  `respawn != 0`; `cs2d_sv_step_round` só corre os objetivos para `respawn == 0 && balance == 1`) e
  travado por um teste novo.
- Verificado: `build.bat` limpo e `test_cs2d.exe` **79 / 79** (teste novo: sem fim de round num modo com
  respawn + a queda do dano da explosão nos prédios, com a recompensa de kill). Smoke test em
  `sv_gamemode 3` sem erros.
- Falta (OPEN_ISSUES): Gate Field (o "hurt" está no passo de tile `Tpl.tilepos`, 0x14020f7d0, precisa de um
  passe `dedscan.py refs` no dedicated) e a simulação de `Tpro` para granadas/rockets/minas.


## 2026-10-16 — Cline — Construction part 3: Dispenser/Supply, melee nos prédios e mp_dmspawnmoney

- **Dispenser** (`CS2D.exe FUN_14028b980`) e **Supply** (`FUN_14028dba0`) portados para
  `sv_step_dispenser_supply` em `src/cs2d_sv_game.c`: cura `mp_dispenser_health` (DAT_140a14dac = **10**)
  e paga `mp_dispenser_money` (DAT_140a14db0 = **500**) a quem do lado servido estiver a <48 px, e o
  contador de frames (Tdo +0x58 = o nosso `countdown`) gera munição 62 (frame 5) / 61 (frame 10) com
  `mp_infammo` desligado; o Supply gera `mp_supply_items[mode-1]` a cada 7 frames (máx. 3 por perto). Novos
  helpers `sv_spawn_item` (`[33]`), `sv_spawn_item_near` (as 8 casas da tabela 0x140bc8a00/0x140bc8a20),
  `sv_items_near`, `sv_supply_item_at`, `sv_building_serves`.
- **Corpo a corpo também fere prédios**: `cs2d_sv_shot_at_building` passou a correr para todas as armas
  menos a chave inglesa (o `Tshoot.Shoot` é agnóstico da arma), com o alcance de `cs2d_weapon_shot_reach`.
- **Dinheiro de spawn** (`FUN_140160740`): modos com respawn (DM/TeamDM/Construction/Zombies) dão
  `mp_dmspawnmoney` (DAT_1405a6554 = **2000**) em cada spawn quando o contador de segundos por jogador
  (Tpl +0x3fc, teto 499, incrementado no `sv_second`) passou de 14 — o "max every 15 sec" do Changelog; o
  primeiro spawn continua a usar `mp_startmoney`.
- Constantes lidas do `.data` com o novo `tools/agent/pevalue.py` (documentado em `TOOLS.md`).
- Verificado: `build.bat` limpo e `test_cs2d.exe` **78 / 78** (dois testes novos: dispenser/supply e o
  dinheiro de spawn). Smoke test do `cs2d_server.exe -offline` em `sv_gamemode 3` sem erros.
- Falta (OPEN_ISSUES): Gate Field (a lógica está no `Tpl.tilepos`, 0x14020f7d0) e o dano de explosão nos
  prédios; `0x140ddf540` (o gate do Dispenser/Supply) não foi identificado - tratamo-lo como ligado.
