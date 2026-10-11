addhook("spawn","bp_spawn")
addhook("break","bp_break")
addhook("hit","bp_hit")
function bp_spawn(id)
  timer(300,"bp_tp",tostring(id))
end
function bp_tp(id)
  parse("setpos "..id.." 1744 1264")
  parse("equip "..id.." 30") parse("setweapon "..id.." 30")
  print("BP setpos "..id)
end
function bp_break(x,y,id)
  print("BP break "..x.." "..y.." by "..id.." state="..tostring(entity(x,y,"state")))
end
function bp_hit(id,src,wpn,hp,ap)
  print("BP hit "..id)
end
addhook("attack","bp_attack")
function bp_attack(id)
  print("BP attack "..id.." wpn="..player(id,"weapontype").." ammo="..tostring(playerweapons and 0).." pos="..player(id,"x")..","..player(id,"y").." rot="..player(id,"rot"))
end
