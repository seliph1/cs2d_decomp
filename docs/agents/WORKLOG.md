# Work log (append-only, newest first)

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


