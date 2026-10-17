# Work log (append-only, newest first)

Add one entry per session: `## YYYY-MM-DD — <agent> — <topic>`, followed by a few bullets covering:

- what changed;
- how it was verified;
- what is left (move it to `OPEN_ISSUES.md`).

Keep entries short; the details go in `KNOWLEDGE.md` or in code comments.

Only the 5 newest entries live here; when adding one, move the oldest to the top of `worklog/archive.md` (do not read the archive in full; grep it).

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

## 2026-10-17 — Antigravity — Dedicated Server: simulação de física e detonação de projéteis (Tpro)

- **Simulação completa de voo e detonação de projéteis no servidor**: `cs2d_sv_projectile` (`[27]`) agora
  instancia projéteis voadores no array `sv->projectiles` (`cs2d_sv_projectile_t`) do servidor dedicado
  (port de `cs2d_dedicated.exe` `FUN_1401ce010`, `FUN_140190000`, `FUN_140095b80` e `FUN_140179060`).
- **Física de voo e ricochete**:
  - Armas de impacto (Rocket Launcher 48, RPG Launcher 47, Grenade Launcher 49, Molotov 73, Snowball 75,
    Gut Bomb 86): voam em alta velocidade e detonam imediatamente no primeiro obstáculo sólido do mapa
    (`sv_tile_blocks`), prédio ou jogador inimigo atingido (`FUN_140179060`).
  - Granadas arremessáveis (HE 51, Flashbang 52, Smoke 53, Gas 72, Flare 54, Air Strike 76): ricocheteiam
    ao tocar em paredes/prédios invertendo componentes de velocidade, perdendo 50% de `flydist` e 35% de
    velocidade (`speed *= 0.65f`). Pousam no solo quando `flydist <= 0`.
- **Temporizadores de pavio e expiração**:
  - HE Grenade e Flashbang possuem pavio de 1800 ms (1.8s) a partir do arremesso, detonando tanto no ar
    quanto no chão.
  - Air Strike tem contagem de 7 segundos no solo antes de desencadear os bombardeios.
- **Detonação e dano radial (`cs2d_sv_detonate_projectile`)**:
  - Conecta com `cs2d_sv_explosion_objects` aplicando dano radial com queda linear aos edifícios
    (`Tpro.c` radii: HE 40 px, Rocket/RPG 45 px, Air Strike 250 px).
  - Conecta com `cs2d_sv_damage` aplicando dano radial com queda linear e friendly fire aos jogadores
    (HE 160 px / 105 dano, Rocket 140 px / 100 dano, RPG 140 px / 325 dano).
  - Danifica reféns no raio da explosão via `cs2d_sv_damage_hostage`.
- **Correção da duplicação visual de granadas**: no servidor original (`cs2d_dedicated.exe` `FUN_140190000`),
  o opcode `[27]` é retransmitido a todos os clientes conectados com `joined == 1` **exceto o jogador que arremessou**
  (`plVar9 != param_1`), pois o cliente original já prediz e cria localmente o projétil visual (`Tpro.spawnprojectile`).
  O nosso servidor enviava para todos os jogadores inclusive o autor via `sv_broadcast_joined`, duplicando a granada na tela.
  Adicionado helper `sv_broadcast_joined_except(sv, except_id, ...)` e corrigido `[41]` (`kill_projectile`) para 3 bytes
  (`FUN_140168ac0` `[41, proj_id u8, player_id u8]`).
- Verificado: `build.bat` compilou limpo e `test_cs2d.exe` **81 / 81** passou (novo teste
  `test_server_projectiles` validando voo, ricochete com redução de `flydist`, detonação por pavio e impacto
  imediato de foguete em prédios e jogadores).
- Falta (OPEN_ISSUES): feixe da Laser Mine (21), `mp_building_*` como cvars, e proxy capture das constantes.

