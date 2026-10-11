addhook("join","jl_join")
addhook("leave","jl_leave")
addhook("spawn","jl_spawn")
function jl_join(id) print("JL join "..id.." "..player(id,"name")) end
function jl_leave(id,r) print("JL leave "..id.." reason="..tostring(r)) end
function jl_spawn(id) timer(300,"jl_place",tostring(id)) end
function jl_place(id)
  id = tonumber(id)
  if id == 1 then parse("setpos 1 2224 1648") else parse("setpos "..id.." 2096 1648") parse("equip "..id.." 30") parse("setweapon "..id.." 30") end
end
function jl_count() print("JL players="..#player(0,"table")) end
timer(3000,"jl_count","",0)
