-- spawnobject probe (any game mode): buildings spawned at load and at every round start; the joining player goes
-- to T and restarts the round once (lines start with SO)
function so_spawn(tag,x,y)
  parse("spawnobject 3 "..x.." "..y.." 0 0 1 0")
  parse("spawnobject 5 "..(x+1).." "..y.." 0 0 2 0")
  print("SO "..tag.." "..#object(0,"table"))
end
so_spawn("load",28,80)
addhook("startround","so_round")
function so_round() so_spawn("round",28,86) end
addhook("join","so_join")
function so_join(id) timer(500,"so_team",tostring(id)) end
function so_team(id) parse("maket "..id) timer(3000,"so_restart") end
function so_restart() print("SO restart") parse("restart") end
