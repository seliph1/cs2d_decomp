-- Light engine reference shots: spawns player 1 and walks it through fixed positions (pixels), one every 4 s.
-- Pair with drive_orig.ps1 "shot" steps every 4 s (see docs/agents/TOOLS.md, light engine).
LP_POS = { {440,260}, {464,176}, {144,240}, {1136,336}, {1424,240}, {1424,560} }
LP_I = 0
addhook("team","lp_team")
function lp_team(id) if id == 1 then timer(1000,"lp_spawn") end end
function lp_spawn()
  parse("spawnplayer 1 "..LP_POS[1][1].." "..LP_POS[1][2])
  print("LP spawn")
  timer(4000,"lp_next","",#LP_POS - 1)
end
function lp_next()
  LP_I = LP_I + 1
  local p = LP_POS[LP_I + 1]
  parse("setpos 1 "..p[1].." "..p[2])
  print("LP pos "..LP_I.." "..p[1].." "..p[2])
end
