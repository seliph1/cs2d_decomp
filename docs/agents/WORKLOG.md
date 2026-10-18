# Work log (append-only, newest first)

Add one entry per session: `## YYYY-MM-DD — <agent> — <topic>`, followed by a few bullets covering:

- what changed;
- how it was verified;
- what is left (move it to `OPEN_ISSUES.md`).

Keep entries short; the details go in `KNOWLEDGE.md` or in code comments.

Only the 5 newest entries live here; when adding one, move the oldest to the top of `worklog/archive.md` (do not read the archive in full; grep it).

## 2026-10-18 — Antigravity — Dedicated Server: Mapeamento de Opcodes Residuais na Jump Table ([12..14], [18], [243], S->C drops e defaults)

- **Opcodes de Movimentação e Rotação do Jogador (`[12..14]`)**:
  - Implementado `case 12:` (`FUN_1401cbce0`): rotação pura `[12, rot u16]`, convertendo via `pl->rot = (float)rot * 0.005493164f` e ativando `pl->rot_flag = 1`.
  - Implementado `case 13:` (`FUN_1401cc0b0`) e `case 14:` (`FUN_1401cc310`): corrida + rotação (`[13]`) e caminhada + rotação (`[14]`) lendo `x u16, y u16, rot u16`, validando movimento em `cs2d_sv_validate_move` e atualizando rotação.
- **Opcode [18] No-op (`0x1401e4350`)**:
  - Implementado `case 18:` retornando `true` imediatamente. Desassemblagem em `0x1401e4350` confirmou `xor esi, esi; jmp next`, sendo um verdadeiro no-op no dispatcher dedicado.
- **Opcode [243] UTF-16LE String (`FUN_1401be4a0`)**:
  - Implementado `case 243:` consumindo `slen u16` seguido por `slen * 2` bytes de string UTF-16LE, evitando desincronização de payload no datagrama.
- **Opcodes Server-to-Client e Defaults na Jump Table**:
  - Mapeados todos os opcodes Server-to-Client que no modo Dedicated Server (`DAT_1407dfe48 != 0`) retornam código 3/4 no binário original (`15`, `21`, `22`, `25`, `29`, `33`, `35..38`, `40`, `42`, `44..49`, `51`, `52`, `55`, `58`, `59`, `61..66`, `69`, `224`, `226`, `234`, `237`, `239`, `247`, `248`): retornam `false` em `sv_recv_game`, encerrando o loop de processamento do datagrama de forma limpa.
  - Opcodes não utilizados na jump table (`73..79`, `88..99`, `101..223`, `231`, `233`, `244..246`): no binário original apontam para `0x1401e3e80` (`esi = 2`), agora caem no `default:` retornando `false` sem imprimir warnings de opcodes desconhecidos.
- **Verificação**:
  - Adicionado teste abrangente `test_server_jump_table_opcodes` em `tests/test_cs2d.c` cobrindo [12], [13], [14], no-op [18], string [243], S->C drops e defaults.
  - Suíte completa de testes aprovada: **91 / 91** testes passando (`test_cs2d.exe`).

## 2026-10-17 — Antigravity — Dedicated Server: Resposta de Server Browser, Ping Echo [FA] e Info Reply [FB]

- **Ping Echo Connectionless e In-Session (`[FA]` / 250, `FUN_1400d4ed0`)**:
  - Implementado `cs2d_sv_handle_ping` em `src/cs2d_server.c` interceptando pacotes `0xFA` antes da criação de conexões (`sv_find_conn`), ecoando os 7 bytes exatos `[01 00] [FA] [millisecs i32]` de volta para o cliente requisitante.
  - Implementado `case 250:` em `sv_recv_game` para clientes já conectados.
- **Server Info Reply (`[FB]` / 251, `FUN_1401da400`)**:
  - Implementado `cs2d_sv_build_info_reply` e `cs2d_sv_handle_info` respondendo às requisições de consulta do navegador de servidores e do tooltip:
    - Modo 1 (consulta direta ao servidor) e Modo 2 (broadcast LAN para `255.255.255.255:36963`).
    - Geração exata dos campos de wire: `[01 00] [FB] [mode] [flags] [name u8 str] [map u8 str] [players u8] [max_players u8] ([gamemode u8] se flags & 0x20) [bots u8] [flags2 u8]`.
    - Bits de `flags`: bit 0 (senha), bit 1 (usgn_only), bit 2 (fog of war), bit 3 (friendly fire), bit 4 (versão 1018 compatível), bit 5 (gamemode presente se != 0), bit 6 (scripts Lua carregados), bit 7 (forcelight).
    - Bits de `flags2`: bit 0 (recoil dinâmico), bit 1 (offscreen damage), bit 2 (transferências ativas).
    - Modos 3 (alive ack), 4 (lista de nomes de jogadores) e 5 (detalhes de jogadores: id, nome, time, score e mortes) fiéis à rotina de decompilação `FUN_1401da400`.
  - Implementado `case 251:` em `sv_recv_game` para consultas em sessões ativas.
- **Verificação**:
  - Teste de regressão adicionado em `tests/test_cs2d.c` comprovando que o pacote gerado por `cs2d_sv_build_info_reply` para o mapa `prison_funst3r` com configurações padrão é idêntico byte a byte (36 bytes) à captura original do `cs2d_dedicated.exe`.
  - Modos 4 e 5 validados com dados reais de jogadores.
  - Suíte completa de testes aprovada: **90 / 90** testes passando (`test_cs2d.exe`).

## 2026-10-17 — Antigravity — Dedicated Server: Comunicação Cliente > Lua ([53] reqcld/clientdata, [227/228] key, [232] clientsetting, [80..87] physics rotation & [225] voice)

- **Opcode [53] `reqcld` & `clientdata` (`FUN_140129a50`, `FUN_1401780d0`, `FUN_1401d16f0`)**:
  - Implementado `cs2d_sv_reqcld(sv, id, mode, param)` enviando pacote confiável `[53, mode u8, param string (se mode 4 ou 5)]` para o jogador especificado (`id > 0`) ou broadcast (`id == 0`).
  - Implementado em `sv_recv_game` o `case 53:` consumindo respostas de client data nos modos:
    - 0: cursor no monitor `(x u16, y u16)`
    - 1: map scroll offset `((u16 - 0x8000), (u16 - 0x8000))`
    - 2: cursor absoluto no mapa `((u16 - 0x8000), (u16 - 0x8000))`
    - 3: light engine enabled (`b u8`)
    - 4: arquivo carregado (`b u8`) + SHA256 checksum string
    - 5: weapon mod info (`sub u8`, `val`, mod string)
    - 6: render3d enabled (`b u8`)
  - Hook Lua `clientdata(id, mode, data1, data2)` atualizado para passar string em `data2` nos modos 4 e 5 (`push_clientdata`).
  - Comando Lua `reqcld(id, mode [, param])` conectado à implementação do servidor (`l_reqcld`).
- **Opcodes [227] & [228] `key press` & `key release` (`FUN_1401bb510`, `FUN_1401bb620`)**:
  - Implementados `case 227:` (press) e `case 228:` (release) lendo `[op, key u16]`.
  - Resolução de keycode para nome textual (`sv_resolve_key_name`) priorizando binds customizados registrados via `addbind`, seguido de mapeamento canônico `cs2d_bind_key_name(key)` e fallback para decimal string.
  - Disparo de hook Lua `key(id, key_name, state)`.
  - `src/cs2d_bind.c` adicionado à linha de compilação de `cs2d_server.exe` em `build.bat`.
- **Opcode [232] `clientsetting` (`FUN_1401bb730`)**:
  - Implementado `case 232:` para sub-opcodes de configuração de tela do cliente:
    - sub 1: resolução de tela `[232, 1, w u16, h u16]` -> atualiza `pl->screen_w` e `pl->screen_h`.
    - sub 2: proporção 4:3 `[232, 2, s43 u8]` -> atualiza `pl->screen_aspect`.
    - sub 3: modo janela `[232, 3, win u8]` -> atualiza `pl->screen_windowed`.
  - Dispara hook Lua `clientsetting(id, sub, val1, val2)`.
  - Propriedades Lua `player(id, "screenw")`, `player(id, "screenh")`, `player(id, "screen4by3")` e `player(id, "windowed")` agora refletem os valores reais reportados pelo cliente (com fallback 850x480).
- **Opcode [80] `player physics rotation` & [81..87] descarte (`FUN_1401d4910`, `FUN_1401e3cf0`)**:
  - `case 80:` lê `flags u8`; se `flags & 0x40` consome os bytes de posição/delta; se `flags & 0x80` lê `dir u16` e atualiza `pl->rot = (float)dir * 0.005493164f` (port fiel de `FUN_1401d4910`).
  - `case 81..87:` retornam `false` em `sv_recv_game` descartando o restante do datagrama, espelhando o retorno 4 da tabela de saltos da dedicated original.
- **Opcode [225] `voicechat` (`FUN_1401d68b0`)**:
  - Implementado `case 225:` consumindo sub 0 (ping), sub 1 (áudio relay com `vlen u16 + data`) e sub 2.
  - Retransmissão de áudio para o alvo/time se `sv_voicechat != 0`, e disparo de hook Lua `voice(id)`.
- **Verificação**:
  - Adicionado teste completo de regressão `test_server_client_lua_communication` em `tests/test_cs2d.c`.
  - Suíte completa de testes compilada e aprovada: **90 / 90** testes passando (`test_cs2d.exe`).

## 2026-10-17 — Antigravity — Dedicated Server: Interações Cliente-Mundo/Entidades ([31], [50], [56], [57], [60], [68], [35-37, 65], [26, 100])

- **Entity Event [31] (`FUN_1401cead0` -> `FUN_14016e0a0`)**:
  - Implementado `case 31:` lendo `[31, x u16, y u16, mode u8]` (5 bytes de payload).
  - Executa entidades de mapa no tile `(x, y)` acionando `cs2d_sv_trigger_env_items` e disparando hooks Lua `triggerentity` e `trigger`.
  - Retransmite broadcast confiável de 6 bytes `[31, x u16, y u16, mode u8]` para todos os demais jogadores conectados.
- **Portal Fire [50] (`FUN_1401d11a0` -> `FUN_140177230`)**:
  - Implementado `case 50:` lendo `[50, color u8, tx i32, ty i32]` (9 bytes de payload).
  - Registra coordenadas e estado dos portais do jogador (`pl->portal_x[color & 1]`, `pl->portal_y[color & 1]`, `pl->portal_active[color & 1]`).
  - Retransmite broadcast de 11 bytes `[50, player_id, color, tx, ty]` para todos os jogadores no servidor.
- **Weapon Impulse [56] (`FUN_1401d26a0` -> `FUN_1401694e0`)**:
  - Implementado `case 56:` lendo `[56, val u8]` (1 byte).
  - Retransmite broadcast confiável de 3 bytes `[56, player_id, val]` para os outros jogadores.
- **Flashlight [57] (`FUN_1401d28b0` -> `FUN_140106350`) & Spectator Flashlight [60] (`FUN_1401d2d40` -> `FUN_1401b2bf0`)**:
  - `case 57:` atualiza `pl->flashlight = state` respeitando `mp_flashlight != 0` (`0x1401063b7`), retransmite `[57, player_id, state]` e dispara hook Lua `flashlight`.
  - `case 60:` atualiza `pl->specflash = state` e retransmite `[60, player_id, state]` de 3 bytes confiável para todos os clientes joined.
- **Fire Direction [68] (`FUN_1401d3840` -> `FUN_140192080`)**:
  - Implementado `case 68:` lendo `[68, dir u16]` (2 bytes). Converte `(float)dir / 182.04445f` em `pl->rot` e retransmite `[70, player_id, dir]` para espectadores mortos que estão seguindo este jogador.
- **Descarte de Opcodes Server-to-Client [35], [36], [37], [65] (`FUN_1401e3cf0`)**:
  - Confirmado via desassemblagem que no modo dedicado original (`DAT_1407dfe48 != 0`), `0x1401cf350` (35), `0x1401cf4b0` (36), `0x1401cf630` (37) e `0x1401d3430` (65) retornam 3 imediatamente sem ler dados.
  - No dispatcher `FUN_1401e3cf0`, retornos `> 0` quebram o loop do datagrama (`break`), descartando o restante do pacote UDP. Mapeado fielmente retornando `false` em `sv_recv_game`.
- **Use Button [26] com Mode 100 (`FUN_140138a40`)**:
  - Implementado em `cs2d_sv_use` o suporte a botões de mapa (`mode 100`): valida distância euclidiana do centro do tile do botão (`<= 45 px`), dispara hook Lua `usebutton(id, x, y)`, aciona entidades vinculadas respeitando filtro de time (`int_settings[2]`) e dispara o hook Lua `trigger`.
- **Verificação**:
  - Adicionado teste de regressão abrangente `test_server_entity_and_world_interactions` em `tests/test_cs2d.c`.
  - Recompilado e aprovado com sucesso: **89 / 89** testes passando (`test_cs2d.exe`).

## 2026-10-17 — Antigravity — Dedicated Server: Dispatch de Opcodes [28] Spray, [43] Vote, [230] Leaderboard e Guarda < 7

- **Correção da desincronização de leitura do [28] Spray (`FUN_1401bcd60`)**:
  - `case 28:` em `src/cs2d_server.c` lia incorretamente apenas 2 bytes, deixando 4 bytes no stream (`[28, 0, x u16, y u16, mode u8]` são 6 bytes de payload). Esses bytes restantes eram erroneamente interpretados como opcodes em loops seguintes (gerando avisos falsos como opcode 0).
  - Corrigido o parsing para ler `sub = rd_byte(r)`: se `sub == 0`, consome `x`, `y` e `mode` (6 bytes total), dispara `cs2d_sv_lua_hook_spray` e retransmite `[28, player, 0, x, y, mode]` de forma confiável para os outros jogadores conectados. Se `sub == 2`, trata trigger/request sem dados adicionais.
- **Implementação do [43] Vote (`FUN_1401d0050`)**:
  - Implementado `case 43:` lendo 3 bytes (`type u8`, `value u16`): `type 1` = votekick (alvo = target ID), `type 2` = votemap (alvo = map index).
  - Salva em `pl->votekick` e `pl->votemap` em `cs2d_sv_player_t`, aciona o hook Lua `vote` (`cs2d_sv_lua_hook_vote`) e retransmite o voto confiável `[43, voter, type, value]` para todos os clientes conectados.
  - Serialização do registro de jogador (`cs2d_sv_write_player_record`) atualizada para escrever `pl->votekick` e `pl->votemap`.
- **Implementação do [230] Leaderboard (`FUN_1401d8360`)**:
  - Implementado `case 230:` lendo `sub`: `sub 0` (1 byte) responde com o timestamp do leaderboard `[230, 0, timestamp i32]`; `sub 1` (3 bytes: `sub` + `rank u16`) consome a requisição de linha sem enviar nada quando vazio (fiel ao comportamento em `0x1401d8650`).
- **Guarda de Opcodes < 7 em `sv_recv_messages` (`FUN_1401e3cf0`)**:
  - O dispatcher da tabela de saltos da dedicated original aceita estritamente `opcode - 7` (opcodes 7..253).
  - Bytes `< 7` (padding residual de zeros `0x00`, resíduos de controle Tudp como `0x03` no final do pacote) agora encerram a leitura do datagrama silenciosamente (`break`), espelhando o salto para `LAB_1401e3e80` no binário original sem poluir o console com warnings de opcodes desconhecidos.
- **Verificação**:
  - Adicionado teste completo de regressão `test_server_spray_vote_leaderboard_messages` em `tests/test_cs2d.c` validando o consumo exato dos bytes de spray, votekick, votemap, leaderboard e descarte limpo de opcodes residuais < 7.
  - Recompilado e aprovado com sucesso: **88 / 88** testes passando (`test_cs2d.exe`).

