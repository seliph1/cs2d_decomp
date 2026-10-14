Part of the shared knowledge; index and source conventions in `docs/agents/KNOWLEDGE.md`.

## Map list ("New Game" / Create Server)

Source: dump `Tgui_ml::Load` (`0x1400af1d0`), `sys/maptypes.lst` loader `FUN_1401a4610`. Code: `src/cs2d_client.c` (`cs2d_maplist_load`).

- **Prefixes:** `sys/maptypes.lst` defines categories (`as_`, `cs_`, `de_`, `dm_`, `ctf_`, `dom_`, `con_`, `zm_`, `fy_`, `he_`, `ka_`, `awp_`, `aim_`).
- **Files scanned:** reads all files in `maps/`, keeping only those ending with `.map` (case-insensitive). Files `temp.map` and `autosave.map` are explicitly skipped. File names are converted to lowercase with `.map` stripped.
- **Grouping:** in "All Maps" view (`sys/language` 0181), maps matching each prefix are grouped under that category title in `maptypes.lst` order, sorted alphabetically within the group. Maps not matching any known prefix are grouped at the end under "Other Maps" (`sys/language` 0182).
- **Selection:** `Tgui_ml::Load` preselects the map stored in `sv_map` (`0x140a14ec0[0]`); if empty or invalid, the first playable map in the list is selected.
