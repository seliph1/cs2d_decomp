-- Pixel-diff reference scene (docs/agents/TOOLS.md "Pixel comparison"): forces player 1 into the game at
-- a fixed pixel position and leaves it there, so the original client and ours can be screenshotted from
-- the same camera and diffed with tools/agent/pixel_diff.py.
--   Tools/agent/pixel_diff.py needs both sides at the same server position: the position below is tile
--   (44,25) of prison_funst3r, the spot our --screenshot tour already uses (tp 44 25).
-- Both hooks arm it, because the original reaches the game by picking a team (drive_orig.ps1 tap:2/tap:1)
-- while ours is driven through its own team menu - either route has to end in the same spawn.
PD_POS = { 1424, 816 } -- pixels
addhook("join", "pd_arm")
addhook("team", "pd_arm")
function pd_arm(id)
  if id ~= 1 then return end
  timer(1500, "pd_spawn")
end
function pd_spawn()
  parse("spawnplayer 1 " .. PD_POS[1] .. " " .. PD_POS[2])
  print("PD spawn " .. PD_POS[1] .. " " .. PD_POS[2])
  timer(1200, "pd_hold")
end
function pd_hold()
  parse("setpos 1 " .. PD_POS[1] .. " " .. PD_POS[2])
  print("PD hold " .. PD_POS[1] .. " " .. PD_POS[2])
end
