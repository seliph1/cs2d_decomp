-- use probe: puts the joining player on T and prints every "use" hook call (lines start with US)
addhook("join","us_join")
function us_join(id)
	print("US join "..id)
	timer(500,"us_team",tostring(id))
end
function us_team(id)
	parse("maket "..id)
	print("US team "..id)
	timer(1000,"us_spawn",id)
end
function us_spawn(id)
	parse("spawnplayer "..id.." 1424 1648")
end
addhook("use","us_use")
function us_use(id,event,data,x,y)
	print("US "..id.." "..event.." "..data.." "..x.." "..y)
end
addhook("usebutton","us_usebutton")
function us_usebutton(id,x,y)
	print("US button "..id.." "..x.." "..y)
end
