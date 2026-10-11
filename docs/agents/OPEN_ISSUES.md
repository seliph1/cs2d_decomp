# Open issues / next steps

Keep this file current: remove items when they are done (and log them in `WORKLOG.md`), and add what
you discover. Sign items that are being worked on (`[in progress: <agent>]`) so two agents do not take
the same one.

## Known gaps
- **Audio:** there is no audio system (material sounds, shots, `Env_Sound`).
- **Shots:** offline shots have no spread, and the reach of weapons other than the knife and the AK-47
  is not measured.
- **Muzzle offsets:** the per-weapon muzzle offset table (`PTR_DAT_1409f5d20` in `CS2D.exe`) is not
  recovered; values are calibrated.
- **Explosion damage fall-off:** assumed; only distance 0 was measured.
- **Fragments:** which fragment frames and tints belong to each material is inferred from captures.
- **Player kill and death:** opcode 19 is not handled.
- **Opcode 40:** "kill dropped weapon"; its format is unknown.
- **Round start and ground items:** whether dropped weapons are removed at round start is unknown.
- **Spray:** our client sends a blank spray logo.
- **Dedicated broadcast quirk:** after some round restarts the dedicated stops broadcasting entity
  changes; the original client shows the same, so it is server behaviour.
- **Buy menu:** there is no UI; `B` buys a rifle and kevlar + helmet as a shortcut.

## Verification debt
- **Original + our client, one session:** not re-run after the latest join/leave fix (the session ended
  in a BSOD). Run `twoplayer_probe.lua` with the original firing next to our client and check the remote
  shot effects on screen.
