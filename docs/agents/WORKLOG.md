# Work log (append-only, newest first)

Add one entry per session: `## YYYY-MM-DD — <agent> — <topic>`, followed by a few bullets covering:

- what changed;
- how it was verified;
- what is left (move it to `OPEN_ISSUES.md`).

Keep entries short; the details go in `KNOWLEDGE.md` or in code comments.

Only the 5 newest entries live here; when adding one, move the oldest to the top of `worklog/archive.md` (do not read the archive in full; grep it).

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


