-- Construction probe (run with sv_gamemode 3): logs builds / object kills / spawns (CP lines), spawns one object
-- of several types around the first player that spawns, gives money, and kills that player after 12 s
addhook("build","cp_build")
addhook("objectkill","cp_kill")
addhook("spawn","cp_spawn")
addhook("die","cp_die")
function cp_build(id,typ,x,y,mode,obj) print("CP build "..id.." "..typ.." "..x.." "..y.." "..mode.." "..tostring(obj)) end
function cp_kill(obj,id) print("CP objkill "..obj.." "..tostring(id)) end
function cp_die(v) print("CP die "..v) end
cp_id=0
function cp_spawn(id)
  print("CP spawn "..id.." "..player(id,"tilex").." "..player(id,"tiley").." wpn "..player(id,"weapontype"))
  if cp_id==0 then cp_id=id; timer(500,"cp_objects") end
end
function cp_objects()
  local x,y=player(cp_id,"tilex"),player(cp_id,"tiley")
  parse("setmoney "..cp_id.." 16000")
  parse("spawnobject 8 "..(x+3).." "..y.." 0 0 2 0")
  parse("spawnobject 5 "..(x-3).." "..y.." 0 0 0 0")
  parse("spawnobject 7 "..x.." "..(y+3).." 0 0 1 0")
  parse("spawnobject 2 "..x.." "..(y-3).." 45 0 2 0")
  parse("spawnobject 13 "..(x+3).." "..(y+3).." 0 0 2 0")
  parse("spawnobject 9 "..(x-3).." "..(y+3).." 0 0 2 0")
  parse("spawnobject 6 "..(x-3).." "..(y-3).." 0 0 1 0")
  local t=object(0,"table")
  for i=1,#t do local o=t[i]; print("CP obj "..o.." "..object(o,"type").." "..object(o,"tilex").." "..object(o,"tiley")) end
  timer(25000,"cp_killme")
end
function cp_killme() print("CP killme") parse("killplayer "..cp_id) end
