-- Two clients on one dedicated (the original CS2D.exe driven by drive_orig.ps1 and our cs2d_client.exe):
-- report the join/spawn order, put both players on the same line 128 px apart and arm both with an
-- AK-47 so each can fire at the other. Used by tools/agent/capture_two.sh (Phase 8 multi-client check).
-- Same placement as twoplayer_probe.lua, which leaves player 1 unarmed - here both get a weapon.
addhook("join","tf_join")
addhook("leave","tf_leave")
addhook("spawn","tf_spawn")
function tf_join(id) print("JL join "..id.." "..player(id,"name")) end
function tf_leave(id,r) print("JL leave "..id.." reason="..tostring(r)) end
function tf_spawn(id) timer(300,"tf_place",tostring(id)) end
function tf_place(id)
  id = tonumber(id)
  if id == 1 then parse("setpos 1 2224 1648") else parse("setpos "..id.." 2096 1648") end
  parse("equip "..id.." 30")
  parse("setweapon "..id.." 30")
  print("JL place "..id.." "..player(id,"x")..","..player(id,"y"))
end
function tf_count() print("JL players="..#player(0,"table")) end
timer(3000,"tf_count","",0)
