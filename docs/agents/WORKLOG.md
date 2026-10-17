# Work log (append-only, newest first)

Add one entry per session: `## YYYY-MM-DD — <agent> — <topic>`, followed by a few bullets covering:

- what changed;
- how it was verified;
- what is left (move it to `OPEN_ISSUES.md`).

Keep entries short; the details go in `KNOWLEDGE.md` or in code comments.

Only the 5 newest entries live here; when adding one, move the oldest to the top of `worklog/archive.md` (do not read the archive in full; grep it).

## 2026-10-17 — Antigravity — Dedicated Server: Deathmatch (DM), Team Deathmatch (TDM), mp_randomspawn e Rotação mp_timelimit

- **Spawns Aleatórios (`mp_randomspawn 1`) em Deathmatch (`gamemode 1`) e TDM (`gamemode 2`)**:
  - Port de `0x140160da0` - `0x140160dae` e `0x140162313` chamando `0x1400d5650`: quando `mp_randomspawn == 1` e `gamemode` é 1 ou 2, o servidor sorteia tiles navegáveis (não sólidos e sem outro jogador ou objeto bloqueando) em até 100 tentativas, centralizando o jogador no tile (`tx * 32 + 16`, `ty * 32 + 16`).
- **Fallback de Spawns em Deathmatch Sem Spawns Dedicados**:
  - Em `sv_gamemode 1`, se um mapa não possui pontos de spawn do time do jogador (por exemplo, um jogador CT entrando em um mapa de DM que só tem entidades `Info_T`), `sv_pick_spawn` agora faz fallback para aceitar spawns de ambos os times (`Info_T` e `Info_CT`), garantindo que o jogador entre no jogo sem travar.
- **Rotação Automática de Mapa com `mp_timelimit` e Ciclo `mapcycle.cfg`**:
  - Port de `0x1401a3058` - `0x1401a30c5`: leitura de `mp_timelimit` em minutos (`* 60000 ms`) e medição de `now - sv->map_start_ms >= limit_ms`.
  - Em modos de respawn (`respawn == 1`) ou quando há <= 1 jogador ativo no servidor, a rotação dispara imediatamente (`cs2d_sv_change_map`) para o próximo mapa retornado por `cs2d_sv_next_map`.
  - Preservação da ordem sequencial original das entradas em `sys/mapcycle.cfg` em `sv->mapcycle` (com wrap-around para o primeiro mapa) antes da ordenação alfabética exigida pelo snapshot de join da Seção 12 (`sv->maps`).
- **Suporte na API Lua (`src/cs2d_sv_lua.c`)**:
  - `game("maptimeleft")`: retorna os segundos restantes até o limite do mapa como float, ou `1000000.0` se `mp_timelimit <= 0` (ilimitado), correspondendo exatamente ao comportamento original do BlitzMax.
  - `game("nextmap")`: retorna o nome do próximo mapa do ciclo via `cs2d_sv_next_map`.
- **Verificação**:
  - Adicionado teste completo `test_sv_deathmatch_and_timelimit` em `tests/test_cs2d.c` validando:
    - Spawn aleatório dentro das dimensões do mapa com `mp_randomspawn 1` em modos 1 (DM) e 2 (TDM).
    - Fallback de spawn para jogador CT em mapa de DM que só possui `Info_T`.
    - Resolução sequencial de `cs2d_sv_next_map` e wrap-around no fim do ciclo.
    - Gatilho automático de `mp_timelimit` em `cs2d_sv_step_round` acionando troca para o próximo mapa.
    - Consultas Lua `game('maptimeleft')` e `game('nextmap')`.
  - Suíte completa de testes compilada e aprovada: **87 / 87** testes passando (`test_cs2d.exe`).

## 2026-10-17 — Antigravity — Dedicated Server: Gamemode Auto-Restart, Shotgun Shell Timing e Env_Item Spawns

- **Reinício Automático de Round ao Mudar Modo de Jogo (`sv_gamemode` / `gamemode`)**:
  - Quando o modo de jogo é alterado via comando de console/RCON (`gamemode <n>`, `sv_gamemode <n>`) ou cvar genérica, se o valor for diferente do modo atual, o servidor encerra e reinicia o round imediatamente (`cs2d_sv_round_end(sv, 5, 0)` + `cs2d_sv_round_start(sv, 5)`), resetando pontuações, bomba e spawns, e transmitindo `[22, 5]` para todos os clientes conectados.
- **Sincronização de Recarregamento de Shotguns (M3 / XM1014) e Compensação de Lag (`mode 101`)**:
  - No cliente real `CS2D.exe`, se a animação do projétil/shell expira antes de receber a confirmação do servidor (`[16, id, 4]`), o cliente adiciona uma bala localmente e envia o pacote de compensação `[16, 101]` (`0x65`).
  - No servidor, o `mode == 101` incrementava a bala, mas não atualizava `pl->reload_end_ms`. No frame seguinte (~16ms), `cs2d_sv_step_reloads` via o timer expirado e disparava `mode 4` imediatamente, inserindo uma segunda bala no servidor e transmitindo `[16, id, 4]` de volta ao cliente (que adicionava outra bala), dobrando a velocidade de reload.
  - Corrigido rearmando `pl->reload_end_ms = now + (reload_ticks * 5)` ao receber `mode == 101`, sincronizando com exatidão a cadência entre cliente e servidor sem duplicar conchas.
- **Entidades de Mapa `Env_Item` (ID 21) no Início de Round e Join**:
  - Implementado `cs2d_sv_init_env_items` e rotina de manutenção em `cs2d_sv_step_items` cobrindo os modos de spawn das entidades do mapa: 0 (One Time), 1 (Instant/Infinite), 2 (Every X sec) e 3 (On Trigger Only).
  - No início de round (`cs2d_sv_round_start`), os ground items do round anterior são limpos e as entidades `Env_Item` dos modos 0, 1 e 2 são instanciadas no solo em `sv->items`.
  - No join do jogador, a Seção 3 do join snapshot (`[252, 7, 3]`) agora transmite todos os itens de `Env_Item` já instanciados, tornando-os visíveis e interativos no mapa imediatamente ao entrar no servidor.
  - Adicionado comando de console/RCON `trigger <name>` para disparar entidades `Env_Item` com trigger configurado (`cs2d_sv_trigger_env_items`).
- **Verificação**:
  - Adicionado teste completo `test_sv_gamemode_and_env_items` em `tests/test_cs2d.c` validando reinício automático de round, rearm do timer da shotgun com proteção contra duplicação de concha e ciclo de vida de `Env_Item` (spawns iniciais, respawn instantâneo no modo 1 e acionamento por trigger).
  - Suíte completa de testes compilada e aprovada: **86 / 86** testes passando (`test_cs2d.exe`).

## 2026-10-17 — Antigravity — Itens Consumíveis, correção do bug HE (65532) e Menu da Supply Station

- **Correção do bug crítico da HE (65532) / underflow de itens no chão**:
  - Em `sv_spawn_item` (`src/cs2d_sv_game.c`), itens gerados por Supply, Dispenser ou mapa tinham `it->clip` e `it->reserve` inicializados como `0` (marcador default no wire do pacote `[33]`).
  - Ao coletar um item stackable (como HE Grenade gerada pela Supply), a subtração `it->clip -= take` causava underflow de `uint16_t` para `65535` (`0 - 1 = 65535`).
  - Como `it->clip` nunca chegava a zero, `it->active` permanecia `true` indefinidamente, decrementando a cada tick (`65535`, `65534`, `...`, `65532`).
  - O cliente original recebia `orig_ground_clip = 65535` no opcode `[25]`, mantinha o item no chão com contagem de pilha gigante (`HE (65532)`) e fornecia milhares de granadas para o inventário do jogador (mostrando `655` na HUD).
  - Corrigido chamando `cs2d_weapon_item_ammo` em `sv_spawn_item` para carregar o clip real (`default_clip = 1` para HE, `30` para fuzis) e blindando `cs2d_sv_step_items` com `if (it->clip <= take) { it->clip = 0; it->active = false; }`. O item agora é completamente removido do solo ao ser pego e a HUD do cliente mostra a contagem exata (1).
- **Implementação do Menu de Troca da Supply Station (`[26, 150]`)**:
  - O opcode `[26]` com `mode == 150` (`FUN_140138b32` no dedicado original e `FUN_14014f820` no cliente) não estava implementado em `cs2d_sv_use`, fazendo com que a seleção do menu da Supply fosse completamente ignorada e ficasse travada no modo padrão 1 (HE).
  - Implementada a validação e atualização de `o->mode = data` em `cs2d_sv_use`, com checagem de propriedade (`o->player == id`), alcance (`<= 60 px`) e broadcast confiável de `[26, player, 150, data i32, tx u16, ty u16]` para todos os clientes.
  - Implementado o handler do cliente em `src/cs2d_client.c` para sincronizar `o->mode` localmente.
- **Consumíveis e condições de coleta fiel (`FUN_14020ce60` / `FUN_14020ddc0`)**:
  - Consumíveis aplicam efeitos instantâneos sem entrar no inventário de armas. Rejeições fiéis implementadas em `cs2d_player.c` e `cs2d_sv_game.c` para HP cheio (Medikit/Bandage), dinheiro no teto de $16000 (Coins/Money/Gold), reservas de munição completas (Primary/Secondary Ammo) e coletes (Kevlar <= 64, Kevlar+Helm < 100).
- **Verificação**:
  - Testes em `tests/test_cs2d.c` expandidos para validar o spawn sem underflow de HE, a remoção completa do item do solo ao coletar, e a alteração de modo do Supply via `[26, 150]`.
  - `build.bat` compilou limpo e todos os **85 / 85** testes passaram.

## 2026-10-17 — Antigravity — Dedicated Server: Transporte Tudp, triple inicial 03 32 00, Leave/Kick [253] e Fadeout [40]

- **Transporte Tudp e retransmissão de confiáveis (`Fudp_update`)**:
  - Implementada a fila de retransmissão confiável `net_pending[CS2D_CLIENT_MAX_PENDING]` no cliente (`include/cs2d_client.h`, `src/cs2d_client.c`), espelhando a fila do servidor dedicado.
  - Retransmissão automática a cada 500 ms de datagramas confiáveis sem ACK e liberação imediata ao receber control triple kind 1 (`[01, seq]`, `Tudp_con.clearoutbuffer`).
- **Triple inicial de handshake `03 32 00`**:
  - Em `cs2d_client_connect`, o primeiro datagrama confiável é prefixado com o triple `03 32 00` (kind 3 session reset, netrate 50) antes de `fc 00` (`[252, 0]`), reproduzindo exatamente os 7 bytes vistos no wire do cliente original (`02 00 03 32 00 fc 00`).
- **Alinhamento do protocolo de Leave / Kick (`[253]`)**:
  - Descompilação de `FUN_1401502a0` em `0x140150ae0` (`cmp edi, 0xf`): reasons `< 16` enviam 3 bytes `[253, id, reason]`; reasons `>= 16` transmitem `[253, id, reason, str_len, str]`.
  - `sv_remove_player` unificado para receber `reason_str`, disparar hook Lua com a string e fazer o broadcast do pacote completo.
  - Parser do cliente atualizado para consumir a string opcional em `reason >= 16`, evitando desincronização de opcodes subsequentes.
- **Fadeout de armas caídas (`[40]`) e `mp_startmoney`**:
  - `cs2d_sv_step_items` remove armas dropadas após 15s (`0x140a14d94`) e faz broadcast confiável de `[40, id u16]`. Itens de spawn/mapa nunca sofrem fadeout.
  - `mp_startmoney` default verificado: 800 fallback em código, 16000 no `sys/server.cfg` e clamp em `0x3e80` (16000) medido em `0x14014c217`.
- **Verificação**:
  - Novo teste `test_server_network_and_protocol` adicionado à suíte (`tests/test_cs2d.c`).
  - `build.bat` compilou limpo e todos os **84 / 84** testes passaram em `test_cs2d.exe`.

## 2026-10-17 — Antigravity — Dedicated Server: MVP por kills e eliminação 1v1

- **Separação de `kills` e `hostages_rescued` de `score` em `cs2d_sv_player_t`**:
  - `include/cs2d_server.h`: campos `kills` e `hostages_rescued` dedicados adicionados a struct `cs2d_sv_player_t`.
  - `src/cs2d_sv_game.c`: `cs2d_sv_kill` incrementa `k->kills` para mortes em inimigos (e em deathmatch `k != v`), isolado de pontos por objetivos (`score`).
  - `cs2d_sv_hostage_escaped`: incrementa `pl->hostages_rescued` para o CT responsável.
  - Reset em `cs2d_sv_round_start` limpa `kills` e `hostages_rescued` nos resets completos (`bit & 0x30ULL`) e a cada round start.
- **Fórmula de MVP 1:1 com `FUN_14015c080`**:
  - `sv_best_of_team`: calcula estritamente `pl->kills * 2 + pl->assists` (em vez de usar `score * 2 + assists`).
  - `cs2d_sv_round_end_ex`:
    - Tipos 1, 2, 60, 61 usam `sv_best_of_team` com `kills * 2 + assists`.
    - Tipo 31 (Hostage Rescue, case `0x1f`): escolhe o jogador CT com maior `hostages_rescued`.
    - Tipos 20 e 21: mantém planter e defuser como MVP.
    - VIP (time 3) estritamente excluído do MVP (`FUN_14015c080`).
- **Suporte na API Lua (`src/cs2d_sv_lua.c`)**:
  - Adicionado `kills` em `player(id, "kills")` e `stats(id, "kills")`.
  - `stats(id, "killsperdeath")` passa a calcular `kills / deaths` com `kills` isolado.
- **Verificação**:
  - `test_standard_gamemode` expandido com:
    - Partida 1v1 entre 2 jogadores reais (T vs CT) com detecção e encerramento imediato via eliminação (reason 2).
    - Validação de que a fórmula do MVP ignora score de objetivos (placar 15 vs 2) e prioriza `kills * 2 + assists`.
    - Validação de MVP por reféns resgatados e imunidade de VIP a MVP.
  - `build.bat` compilou sem avisos e todos os 83 testes de `test_cs2d.exe` passaram (**83 / 83**).
