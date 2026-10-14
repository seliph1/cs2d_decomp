-- CTF flag probe: a Terrorist walks onto the CT flag (20,17 on ctf_cs2dfortress), carries it, dies and drops it
addhook("join","cp_join")
addhook("spawn","cp_spawn")
addhook("flagtake","cp_take")
addhook("drop","cp_drop")
function cp_join(id) timer(500,"cp_team",tostring(id)) end
function cp_team(id) parse("maket "..id) end
cp_done=false
function cp_spawn(id)
  if cp_done then return end
  cp_done=true
  timer(1000,"cp_tp",tostring(id))
end
function cp_tp(id)
  print("CP tp")
  parse("setpos "..id.." "..(20*32+16).." "..(17*32+16))
  timer(5000,"cp_kill",id)
end
function cp_kill(id)
  print("CP kill flag="..tostring(player(tonumber(id),"flag")))
  parse("killplayer "..id)
end
function cp_take(id,team,x,y) print("CP flagtake "..id.." "..team.." "..x.." "..y) end
function cp_drop(id,iid,typ) print("CP drop "..id.." "..iid.." "..typ) end
