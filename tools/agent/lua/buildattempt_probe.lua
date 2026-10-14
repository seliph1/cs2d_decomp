-- buildattempt probe: the joining player goes to T with 16000; every build attempt is refused (return 1) and
-- spawned with spawnobject instead, like servers that charge buildings in another currency (lines start with BA)
parse("sv_gamemode 3")
addhook("join","ba_join")
function ba_join(id) timer(500,"ba_team",tostring(id)) end
function ba_team(id) parse("maket "..id) timer(1000,"ba_spawn",id) end
function ba_spawn(id)
  parse("spawnplayer "..id.." "..(29*32+16).." "..(83*32+16))
  parse("setmoney "..id.." 16000")
end
addhook("buildattempt","ba_build")
function ba_build(id,typ,x,y,mode)
  print("BA attempt "..id.." "..typ.." "..x.." "..y.." "..mode)
  parse("sv_soundpos \"player/build.wav\" "..(x*32+16).." "..(y*32+16))
  parse("spawnobject "..typ.." "..x.." "..y.." 0 0 "..player(id,"team").." "..id)
  print("BA objects "..#object(0,"table"))
  return 1
end
