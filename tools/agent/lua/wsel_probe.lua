-- Weapon selection HUD probe: gives every slot something (AK-47, Deagle, HE, Flashbang, Smoke) and holds the AK-47
addhook("spawn","ws_spawn")
function ws_spawn(id) timer(300,"ws_give",tostring(id)) end
function ws_give(id)
  for _,w in ipairs({30,3,51,52,53}) do parse("equip "..id.." "..w) end
  parse("setweapon "..id.." 30")
  print("WS give "..id)
end
