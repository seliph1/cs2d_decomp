-- bomb probe (de_dust2): the joining player becomes T with the bomb on bomb spot (28,22); once it planted it is
-- moved to CT on the bomb tile to defuse. Lines start with BB.
addhook("join","bb_join")
function bb_join(id)
	if player(id,"bot") then return end
	timer(500,"bb_team",tostring(id))
end
function bb_team(id)
	parse("mp_autoteambalance 0")
	parse("maket "..id)
	parse("bot_add_ct")
	parse("bot_add_t")
	parse("bot_freeze 1")
	parse("sv_restartround 1")
end
addhook("startround","bb_round")
function bb_round()
	if bb_planted then return end
	timer(1000,"bb_check")
end
function bb_check()
	-- the bomb goes to a random T: restart until the real player holds it
	for _,p in ipairs(player(0,"table")) do
		local has = false
		for _,w in ipairs(playerweapons(p)) do if w == 55 then has = true end end
		if has then
			if player(p,"bot") then parse("sv_restartround 1") else bb_spawn(tostring(p)) end
			return
		end
	end
	timer(500,"bb_check")
end
function bb_spawn(id)
	id = tonumber(id)
	parse("setpos "..id.." 912 720")

	print("BB spawned T "..id)
	for _,i in ipairs(item(0,"table")) do
		print("BB item "..i.." type "..item(i,"type").." player "..item(i,"player").." at "..item(i,"x")..","..item(i,"y"))
	end
	print("BB playerweapons "..table.concat(playerweapons(id), ","))
end
addhook("bombplant","bb_plant")
function bb_plant(id,x,y)
	print("BB bombplant "..id.." "..x.." "..y)
	bb_planted = true
	timer(1000,"bb_ct",tostring(id))
end
function bb_ct(id)
	parse("makect "..id)
	parse("spawnplayer "..id.." 912 720")
	print("BB spawned CT "..id)
end
addhook("bombdefuse","bb_defuse")
function bb_defuse(id)
	print("BB bombdefuse "..id)
end
addhook("bombexplode","bb_explode")
function bb_explode(id,x,y)
	print("BB bombexplode "..id.." "..x.." "..y)
end
addhook("use","bb_use")
function bb_use(id,event,data,x,y)
	print("BB use "..id.." "..event.." "..data.." "..x.." "..y)
end
function bb_dump()
	local s = "BB dump"
	for _,i in ipairs(item(0,"table")) do
		s = s.." | item "..i.." type "..item(i,"type").." pl "..item(i,"player")
	end
	for _,p in ipairs(player(0,"table")) do
		s = s.." | p"..p.." team "..player(p,"team").." w "..table.concat(playerweapons(p), ",")
	end
	print(s)
end

