# Work log (append-only, newest first)

Add one entry per session: `## YYYY-MM-DD — <agent> — <topic>`, followed by a few bullets covering:

- what changed;
- how it was verified;
- what is left (move it to `OPEN_ISSUES.md`).

Keep entries short; the details go in `KNOWLEDGE.md` or in code comments.

Only the 5 newest entries live here; when adding one, move the oldest to the top of `worklog/archive.md` (do not read the archive in full; grep it).

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


