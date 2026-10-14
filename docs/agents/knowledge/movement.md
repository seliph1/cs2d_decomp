Part of the shared knowledge; index and source conventions in `docs/agents/KNOWLEDGE.md`.

## Movement and collision

Source: REA `FUN_1402563a0` (input) and `FUN_1401f7600` (collision); proxy.

- **Speed:** (2 + weapon bonus) × time factor × 1.2 per 60 Hz frame, about 144 px/s; walking halves it.
  Movement uses 8-direction vectors.
- **Collision box:** 24×24, resolved on X first and then Y, stepping 1 px while the leading corners at
  ±13 / ±12 are free. Tile modes 1..9 block.
- **Sliding:** the original **slides** along walls at the parallel component (W+D ≈ 101 px/s vs D ≈ 138 px/s).
- **Camera:** locked to the player.
