Part of the shared knowledge; index and source conventions in `docs/agents/KNOWLEDGE.md`.

## Server Info and Team Selection UI

Source: reference screenshots from `CS2D.exe` + dedicated `opcode 240` (`FUN_1400d5190`) and `opcode 20`.

- **Window frame:** both `"<server_name> - Info"` and `"Select Team"` are `600×440` centered at `(125, 20)` on `850×480` with a 1 px horizontal divider below the title bar.
- **`"<server_name> - Info"`:** `553×374` scrollable text box (`ServerInfoBox`) + `15×374` scrollbar on the right (`ServerInfoScrollbar`) + `568×20` `Close` button at the bottom (`BtnServerInfoClose`). When joining a server, `"<server_name> - Info"` renders first and closing it reveals `"Select Team"`.
- **`"Select Team"` (`M` key):** `240×25` option buttons starting at `(205, 120)` with `5 px` vertical gap:
  - Main page: `1 Terrorists (N)` and `2 Counter-Terrorists (N)` open skin subcategories; after a `25 px` empty slot, `5 Auto-Select` and `6 Spectator` select immediately (no subcategory); `0 Close` at `(205, 420)`.
  - Subcategories (`"Select Team - Terrorists"` / `"Select Team - Counter-Terrorists"`): `1..4` skins (`Phoenix Connexion`, `L337 Krew`, `Arctic Avengers`, `Guerilla Warfare` / `Seal Team 6 (DEVGRU)`, `German GSG-9`, `UK SAS`, `French GIGN`) with per-skin player counts `(N)`, `25 px` empty slot, `5 Auto-Select`, `Back` (`y = 390`), `0 Close` (`y = 420`).
