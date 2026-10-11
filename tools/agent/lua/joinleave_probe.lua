addhook("join","jl_join")
addhook("leave","jl_leave")
addhook("team","jl_team")
function jl_join(id) print("JL join "..id.." "..player(id,"name")) end
function jl_leave(id,r) print("JL leave "..id.." reason="..tostring(r)) end
function jl_team(id,t) print("JL team "..id.." -> "..t) end
function jl_count() print("JL players="..#player(0,"table")) end
timer(2000,"jl_count","",0)
