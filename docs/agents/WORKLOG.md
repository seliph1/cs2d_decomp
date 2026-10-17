# Work log (append-only, newest first)

Add one entry per session: `## YYYY-MM-DD — <agent> — <topic>`, followed by a few bullets covering:

- what changed;
- how it was verified;
- what is left (move it to `OPEN_ISSUES.md`).

Keep entries short; the details go in `KNOWLEDGE.md` or in code comments.

Only the 5 newest entries live here; when adding one, move the oldest to the top of `worklog/archive.md` (do not read the archive in full; grep it).

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

## 2026-10-17 — Antigravity — Dedicated Server: flags de linha de comando CLI (--map, etc.) e sanitização de mapa

- **Flags de CLI expandidas em `src/main_dedicated.c`**:
  - Suporte completo a `--map <name>`, `-map <name>`, `-m <name>`, bem como sintaxe com `=` ou `:` (`--map=fun_roleplay`).
  - Suporte uniforme a traço simples ou duplo para `--port` / `-port` / `-p`, `--offline` / `-offline` / `--off` / `-off`, `--lua` / `-lua` / `-l`, `--name`, `--pw` / `--password`, `--rcon`, `--maxplayers`, e `--help` / `-h` / `-?`.
- **Sanitização de caminhos de mapa (`cs2d_sv_sanitize_map_name`)**:
  - Implementado em `src/cs2d_server.c` e declarado em `include/cs2d_server.h`.
  - Remove aspas, espaços laterais, prefixos de pasta (`maps/`, `maps\`, diretórios absolutos) e extensão `.map` insensível a maiúsculas/minúsculas.
  - Integrado em `main_dedicated.c`, `cs2d_server_start` e `cs2d_sv_change_map`.
- **Verificação**:
  - Novo teste `test_server_cli_and_map_sanitize` em `tests/test_cs2d.c` validando variações de nomes, prefixos, extensões, aspas e inicialização do servidor com override de mapa.
  - `build.bat` compilou sem nenhum aviso (0 warnings).
  - Testes do `test_cs2d.exe` executados com sucesso total (**83 / 83**).
  - Inicialização real de `cs2d_server.exe --map fun_roleplay -offline -port 36999` e `--map=maps/fun_roleplay.map` verificada via log de execução.

## 2026-10-17 — Antigravity — Construction: Laser Mine beam, cvars dinâmicas e Wrench raycast

- **Laser Mine (`0x14000aa30` no dedicated)**:
  - Implementado `cs2d_dynobj_in_lasermine` fiel ao BlitzMax original nas 4 direções (`dir = (int)rot`: 0 down +Y, 1 left -X, 2 up -Y, 3 right +X) e `cs2d_dynobj_update_lasermine` calculando o alcance do feixe (1..6 tiles, `FUN_14028e310`) interrompido por paredes, bordas do mapa e edifícios sólidos.
  - Detonação ao cruzar o feixe em `sv_tile_object_effect` causando 150 de dano radial em raio de 200 via arma 87 (`CS2D_WEP_LASERMINE`), creditado ao criador da mina (`sv_object_kill`).
  - Renderização no cliente em `src/cs2d_render.c`: flare central e feixe de laser vermelho estendido até `target`.
- **Cvars dinâmicas `mp_building_price`, `mp_building_limit`, `mp_building_health` (`0x140118060`)**:
  - Implementados comandos de console/RCON no servidor suportando resolução por nome ("Laser Mine", "Turret", etc.) ou ID numérico (1..CS2D_DYNOBJ_TYPES).
  - Máscaras `building_*_mask` em `cs2d_server_t` preservando os valores padrão do jogo quando não sobrescritos.
  - Clamping automático da saúde de edifícios ativos existentes ao reduzir `mp_building_health` (`0x1401182a6`).
- **Alvo da chave inglesa (Wrench raycast)**:
  - Raycast probe (`sv_wrench_target`, `Tdo.getatpos`) em passos de 2px ao longo da direção de rotação até o alcance da chave inglesa (15px), parando em obstáculos sólidos.
- **Verificação**:
  - Novo teste de regressão `test_construction_laser_mine_and_cvars` adicionado em `tests/test_cs2d.c`, cobrindo geometria de rotação 0..3, cvars por nome/número, clamping de vida, detonação do feixe e raycast da Wrench.
  - Suíte completa passando: **82 / 82** testes aprovados (`test_cs2d.exe`).

