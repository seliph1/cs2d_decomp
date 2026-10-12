-- Spectator probe: two bots to follow, logs who each spectator watches (specswitch) and team changes
addhook("specswitch","sp_switch")
addhook("team","sp_team")
function sp_switch(id,target) print("SP switch "..id.." -> "..target) end
function sp_team(id,t) print("SP team "..id.." -> "..t) end
function sp_bots() parse("bot_add_t") parse("bot_add_ct") end
timer(1000,"sp_bots")
