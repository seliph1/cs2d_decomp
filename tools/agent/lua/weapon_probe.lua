addhook("spawn","wp_spawn")
addhook("drop","wp_drop")
addhook("collect","wp_collect")
addhook("select","wp_select")
addhook("reload","wp_reload")
function wp_spawn(id) timer(500,"wp_give",tostring(id)) end
function wp_give(id)
  parse("equip "..id.." 30") parse("equip "..id.." 1") parse("setweapon "..id.." 30")
  print("WP gave")
end
function wp_drop(id,iid,t,a,b,m,x,y) print("WP drop "..t.." ammo="..tostring(a).."/"..tostring(b)) end
function wp_collect(id,iid,t,a,b,m) print("WP collect "..t.." ammo="..tostring(a).."/"..tostring(b)) end
function wp_select(id,t,m) print("WP select "..t.." mode="..m) end
function wp_reload(id,m) print("WP reload "..m) end
function wp_ammo() print("WP setammo") parse("setammo 1 30 7 11") end
timer(24500,"wp_ammo")
